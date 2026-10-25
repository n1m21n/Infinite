/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Voice allocation, envelopes, LFO and per-track rendering. Runs in the
 * audio ISR.
 *
 * Each synth part has its own NVOICE voices (so MONO / LEGATO / UNISON keep using
 * v[0..]), but only NVOICE of all the parts' voices sound at once: a part that starts
 * a voice while the budget is full takes one from any part (voice_victim): the oldest
 * released voice, else the oldest extra UNISON voice, else the oldest held voice of a
 * POLY part that is not its lowest note. A voice taken from another part fades out
 * over one block (stage 4: the envelope goes to 0, the block's amplitude ramp
 * declicks it); one of the part's own is restarted in place. Extra UNISON
 * voices only start when there is room. The drum track has its own voices (drums.c). */
static uint32_t vage;                                   /* voice ages: one clock for every part */
/* engines that play recorded material (a position, not a phase): no phases kept or spread */
#if FELUCCA_SLICE
static int eng_sampled(const engine_t *e) { return e == &ENG_SAMPLE || e == &ENG_SLICE; }
#else
static int eng_sampled(const engine_t *e) { return e == &ENG_SAMPLE; }
#endif
static int32_t lfo_wave(track_t *t, uint32_t ph)
{
    switch (t->p[P_LWAVE]) {
    case 1:
        return osc_tri(ph);
    case 2:
        return (int32_t)(ph >> 16) - 32768;
    case 3:
        return ph < 0x80000000u ? 32767 : -32767;
    case 4:
        return (int32_t)(t->lfo_rnd >> 16) - 32768;
    default:
        return osc_sine(ph);
    }
}

/* LFO 2 (SLOOP 2.6, after Felucca 1.4 by Leo Kuroshita, #154; params.c N_LSYNC): with SYNC on, one cycle lasts the
 * note value of the tempo, whatever RATE says: the phase advances 2^32 x CTL / cycle a block (two 32-bit divides; the
 * 1-bit truncation of 0xFFFFFFFF drifts by less than a millionth of a cycle a cycle). OFF: RATE's table, as before.
 * LSYNC_PULSES: a cycle in 24ths of a beat (4BAR .. 1/32) */
static const uint16_t LSYNC_PULSES[11] = {0, 384, 192, 96, 48, 24, 12, 8, 6, 4, 3};
static __attribute__((noinline)) uint32_t lfo_sync_inc(const int16_t *p)   /* (not inlined: its divides stay out of
                                                                           * the audio ISR's loop over the tracks) */
{
    uint32_t beat = (uint32_t)FS * 60u / (uint32_t)clamp(song.g[G_BPM], 20, 300), cyc, q, r;
    cyc = beat * LSYNC_PULSES[(uint32_t)p[P_LSYNC] % NELEM(LSYNC_PULSES)] / 24u;   /* (p: fx.c units_split asks too) */
    cyc = cyc ? cyc : 1u;
    q = 0xFFFFFFFFu / cyc;
    r = 0xFFFFFFFFu % cyc;
    return q * CTL + (r * CTL + CTL) / cyc;
}

static void track_lfo_tick(track_t *t)
{
    uint32_t old = t->lfo_ph;
    t->lfo_ph += t->p[P_LSYNC] ? lfo_sync_inc(t->p) : LFO_INC[t->p[P_LRATE] & 127];
    if (t->lfo_ph < old)
        t->lfo_rnd = rng();
    t->lfo_val = lfo_wave(t, t->lfo_ph);
    if (t->p[P_LPOL])                                   /* UNI: 0..+1 (the depths reach as far, one way only) */
        t->lfo_val = (t->lfo_val + 32768) >> 1;
    if (t->lfo_fade < 32767) {
        int32_t step = (int32_t)(ENV_LIN[t->p[P_LFADE] & 127] >> 9);
        t->lfo_fade = t->p[P_LFADE] ? clamp(t->lfo_fade + (step ? step : 1), 0, 32767) : 32767;
    }
}

/* voices the engine may use (POLY and UNISON): its cap, else all of them */
static uint32_t trk_nvoice(const track_t *t)
{
    uint32_t c = ENGINES[t->engine] == &ENG_PHYS ? phys_poly(t) : ENGINES[t->engine]->poly;   /* (PHYS: by MODEL) */
    return c && c < NPOLY ? c : NPOLY;
}

/* ------------------------------------------------- the shared voice budget --- */
static uint32_t voices_busy(void)                       /* sounding voices of all parts (not the fading ones) */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].stage != 4u;
    return n;
}

static uint32_t lowest_held(const track_t *t)           /* index of the lowest held note (the bass), NVOICE = none */
{
    uint32_t i, low = NVOICE;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && (low == NVOICE || t->v[i].note < t->v[low].note))
            low = i;
    return low;
}

/* the voice to give up for a new one (see the top); soft: only released voices and
 * extra UNISON voices of other parts. Returns its index, *pp its part; NVOICE = none */
static uint32_t voice_victim(const track_t *self, int soft, track_t **pp)
{
    uint32_t p, i, best = NVOICE, cat = 4;
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        uint32_t mode = (uint32_t)t->p[P_VOICE], nu = mode == V_UNISON ? trk_nvoice(t) : 1u;
        uint32_t low = mode == V_POLY ? lowest_held(t) : NVOICE;
        if (soft && t == self)
            continue;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            uint32_t c;
            if (!v->active || v->stage == 4u)
                continue;
            if (!v->gate)
                c = 1;                                  /* released */
            else if (mode == V_UNISON && i > 0 && i < nu)
                c = 2;                                  /* an extra UNISON voice */
            else if (!soft && mode == V_POLY && i != low)
                c = 3;                                  /* held, not the bass */
            else
                continue;
            if (c < cat || (c == cat && v->age < (*pp)->v[best].age)) {
                cat = c;
                best = i;
                *pp = t;
            }
        }
    }
    return best;
}

static uint32_t voice_kills;                            /* voices given up (budget, overload): console, hostsim */
#define KILL_BLOCKS 2u                                  /* 1.5 ms: no click on a bass, little extra load */
static void voice_kill(voice_t *v)                      /* fade out over the next blocks (env_tick stage 4) */
{
    v->gate = 0;
    if (v->stage != 4u)
        v->kill = KILL_BLOCKS;
    v->stage = 4;
    voice_kills++;
}

/* overload (after Felucca 1.0; audio.c asks for it when two halves in a row took > 85 % of their time):
 * fade one voice out over the next blocks (voice_kill: no click), one more each half while it lasts. A
 * single late half (a USB burst, a flash write) sheds nothing. First the quietest releasing voice; else
 * the oldest held one that is not a part's lowest POLY note nor its MONO / LEGATO / UNISON lead, as the
 * voice budget keeps them: a dense chord on a heavy engine thins out from the top, bass and lead stay. */
static uint32_t shed_count;

static void shed_voice(void)
{
    uint32_t p, i;
    voice_t *best = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && !v->gate && v->stage != 4u && (!best || v->env < best->env))
                best = v;
        }
    if (best) {
        voice_kill(best);
        shed_count++;
        return;
    }
    for (p = 0; p < NPART; p++) {
        const track_t *t = &trk[p];
        uint32_t mode = (uint32_t)t->p[P_VOICE], low = mode == V_POLY ? lowest_held(t) : 0u;
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && v->gate && v->stage != 4u &&
                ((mode == V_POLY && i != low) || (mode != V_POLY && i > 0u)) && (!best || v->age < best->age))
                best = v;
        }
    }
    if (best) {
        voice_kill(best);
        shed_count++;
    }
}

/* the budget is full: free a voice for part t. 0 = nothing to take (soft) */
static int voice_room(track_t *t, int soft)
{
    track_t *vp = 0;
    uint32_t k;
    if (voices_busy() < NVOICE)
        return 1;
    k = voice_victim(t, soft, &vp);
    if (k == NVOICE)
        return !soft;                                   /* hard: over the budget (cannot happen with 3 parts) */
    voice_kill(&vp->v[k]);
    return 1;
}

/* POLY allocation (within the engine's voice cap). Same note: reuse its voice. Else a free voice: ROTATE takes
 * the next one round-robin (release tails ring out), REUSE (or any GLIDE) takes
 * the free voice whose last pitch is closest, so poly portamento moves each
 * voice the shortest way. Steal: the oldest released voice, else the oldest
 * held one, never the lowest held note (the bass). A free voice needs room in the
 * shared budget: when the voice to give up is one of this part's, it is restarted
 * in place instead. */
static voice_t *voice_alloc(track_t *t, uint32_t note)
{
    uint32_t i, np = trk_nvoice(t), best = np, low = np, nfree = 0;
    int32_t bd = 0x7FFFFFFF;
    for (i = 0; i < np; i++) {
        if (t->v[i].active && t->v[i].note == note)
            return &t->v[i];
        nfree += !t->v[i].active;
    }
    if (nfree && voices_busy() >= NVOICE) {
        track_t *vp = 0;
        uint32_t k = voice_victim(t, 0, &vp);
        if (k < np && vp == t)
            return &t->v[k];                            /* our own: restart it in place (no click) */
        if (k < NVOICE)
            voice_kill(&vp->v[k]);
    }
    if (t->p[P_ALLOC] || t->p[P_GLIDE]) {
        for (i = 0; i < np; i++) {
            int32_t d = t->v[i].pitch_cur - (int32_t)note * 16;
            if (t->v[i].active)
                continue;
            d = d < 0 ? -d : d;
            if (!t->v[i].pitch_cur)
                d = 0x7FFFFFF0;                          /* never played: last choice */
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
    } else {
        for (i = 0; i < np && best == np; i++) {
            uint32_t k = (t->rr + i) % np;
            if (!t->v[k].active)
                best = k;
        }
        t->rr = (uint8_t)((best == np ? t->rr : best) + 1u) % np;
    }
    if (best < np)
        return &t->v[best];
    for (i = 0; i < np; i++)                             /* the lowest held note is protected */
        if (t->v[i].gate && (low == np || t->v[i].note < t->v[low].note))
            low = i;
    for (i = 0; i < np; i++)                             /* oldest released (not one fading out for another part) */
        if (!t->v[i].gate && t->v[i].stage != 4u && (best == np || t->v[i].age < t->v[best].age))
            best = i;
    if (best == np)
        for (i = 0; i < np; i++)                         /* oldest held, not the bass */
            if (i != low && (best == np || t->v[i].age < t->v[best].age))
                best = i;
    return &t->v[best == np ? 0 : best];
}

/* glide, in 1/4096 semitone a control tick (pitch_cur . pitch_frac): RATE = GLIDE is the time an octave
 * takes (any interval at that speed), TIME = GLIDE is the time of any glide. A sequencer SLIDE with
 * GLIDE 0 glides in SLIDE_T (TIME), as an acid slide */
#define SLIDE_T 54                                       /* TIME_MS index: ~50 ms */
static int32_t glide_ticks(int32_t g)                    /* control ticks of TIME_MS index g */
{
    int32_t k = (int32_t)ENV_LIN[g & 127];
    return k ? (int32_t)((1u << 24) / (uint32_t)k) : 1;
}
static void glide_set(track_t *t, voice_t *v, int glide)
{
    int32_t d, ticks;
    if (!glide || !v->pitch_cur) {
        v->pitch_cur = v->pitch16;
        v->pitch_frac = 0;
        v->gstep = 0;
        return;
    }
    if (t->p[P_GLMODE] || !t->p[P_GLIDE]) {
        d = (v->pitch16 << 8) - ((v->pitch_cur << 8) + v->pitch_frac);
        d = d < 0 ? -d : d;
        ticks = glide_ticks(t->p[P_GLIDE] ? t->p[P_GLIDE] : SLIDE_T);
        v->gstep = d / ticks > 0 ? d / ticks : 1;
    } else {
        v->gstep = 0;
    }
}

static void voice_start(track_t *t, voice_t *v, uint32_t note, uint32_t vel, int glide)
{
    const engine_t *e = ENGINES[t->engine];
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s0 = v->s[0], s1 = v->s[1], s4 = v->s[4], s5 = v->s[5], s6 = v->s[6], s7 = v->s[7];
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = (int32_t)note * 16;
    glide_set(t, v, glide);
    v->penv = 32767;                                    /* the pitch envelope starts over (not on legato) */
    if (!sounding) {
        v->env = 0;
        v->env_out = 0;
    }                                                   /* sounding: the attack starts from the current level */
    e->note_on(t, v);
    if (sounding && !eng_sampled(e)) {                 /* retrigger / steal: keep phases and filter states */
        v->ph[0] = ph0;                                 /* (resetting them clicks) */
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        if (e == &ENG_ANALOG) {
            v->s[0] = s0;
            v->s[1] = s1;
        } else if (e == &ENG_DIGITAL) {
            v->s[5] = s5;
            v->s[6] = s6;
            v->s[7] = s7;                               /* op 4 phase; the modulator envelope restarts */
        } else if (e == &ENG_LOFI) {
            v->s[0] = s0;
            v->s[4] = s4;
        } else if (e == &ENG_TRIO) {
            v->s[0] = s0;                               /* filter */
            v->s[1] = s1;
            v->s[4] = s4;                               /* the sample waiting for its step corrections */
        } else if (e == &ENG_PHASE) {
            v->s[0] = s0;                               /* the WAVE / WAVE2 toggles go with the phases kept */
            v->s[1] = s1;
        } else if (e == &ENG_NOISE) {
            v->s[0] = s0;                               /* the filter and the colour poles (2.5) */
            v->s[1] = s1;
            v->s[4] = s4;
            v->s[5] = s5;
            v->s[6] = s6;
            v->s[7] = s7;
        }
    }
}

/* MONO / LEGATO / UNISON: one note on one voice (eight for UNISON, or the
 * engine's voice cap, spread by DETUNE over the same width) */
static void mono_play(track_t *t, uint32_t note, uint32_t vel, int retrig, int glide)
{
    uint32_t mode = (uint32_t)t->p[P_VOICE], nv = mode == V_UNISON ? trk_nvoice(t) : 1u, i;
    for (i = 0; i < nv; i++) {
        voice_t *v = &t->v[i];
        int32_t k = 2 * (int32_t)i - (int32_t)(nv - 1u);   /* -7 .. 7 */
        if (nv > 1u && nv < NVOICE)
            k = k * (NVOICE - 1) / (int32_t)(nv - 1u);      /* fewer voices: the outer ones as wide */
        v->fine = nv > 1u ? k * t->p[P_DETUNE] * 56 / 889 : 0;   /* up to ~±40 cents */
        if (retrig || !v->active || !v->gate) {
            int was_sounding = v->active && v->stage != 0;
            if ((!v->active || v->stage == 4u) && !voice_room(t, i > 0))
                continue;                                   /* no room for this extra UNISON voice */
            /* level: about the same sum for 8 or 4 voices at random phases */
            voice_start(t, v, note, nv >= NVOICE ? vel * 36u / 100u : nv > 1u ? vel / 2u : vel, glide);
            if (nv > 1u && i && !eng_sampled(ENGINES[t->engine]) && !was_sounding) {   /* random start phases: */
                static uint32_t seed = 0x1234567u;          /* in phase they stack, evenly spread they cancel */
                seed = seed * 1664525u + 1013904223u;
                v->ph[0] += seed;
            }
        } else {                                            /* legato: new pitch, same envelope */
            v->note = (uint8_t)note;
            v->pitch16 = (int32_t)note * 16;
            if (vel > v->vel && nv == 1u)
                v->vel = (uint8_t)vel;
            glide_set(t, v, glide);
        }
    }
    t->mono_note = (uint8_t)note;
}

/* the note the mono stack asks for (PRIO: LAST / LOW / HIGH), 0 = none */
static uint32_t mono_pick(const track_t *t)
{
    uint32_t i, n;
    if (!t->nmono)
        return 0;
    n = t->mono_stack[t->nmono - 1u];
    for (i = 0; i < t->nmono; i++) {
        if (t->p[P_PRIO] == 1 && t->mono_stack[i] < n)
            n = t->mono_stack[i];
        if (t->p[P_PRIO] == 2 && t->mono_stack[i] > n)
            n = t->mono_stack[i];
    }
    return n;
}

static void mono_remove(track_t *t, uint32_t note)
{
    uint32_t j, k = 0;
    for (j = 0; j < t->nmono; j++)
        if (t->mono_stack[j] != note)
            t->mono_stack[k++] = t->mono_stack[j];
    t->nmono = (uint8_t)k;
}

/* the notes each synth part started since the UI last looked (menu NOTES: a short note lights its key
 * for a few frames, as a drum hit does; ui_studio.c pads_tick). Set here (mostly the audio ISR), taken
 * by the UI with the IRQ off */
static volatile uint32_t note_hits[NPART][4];
/* the visualiser (ui_vis.c): the tracks a note started on (bit per track), each one's last note, a kick */
static volatile uint8_t vis_hit, vis_kick_hit, vis_note[NTRK];

static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t any = 0, i, mode = (uint32_t)t->p[P_VOICE];
    if (trk_silent(t))
        return;                                         /* MUTE, or another track soloed */
    vis_hit |= (uint8_t)(1u << ((uint32_t)(t - trk) & 3u));
    vis_note[(uint32_t)(t - trk) & 3u] = (uint8_t)note;
    if (is_drum(t)) {                                   /* the drum track: GM drums (drums.c) */
        if (note == 35u || note == 36u)
            vis_kick_hit = 1;
        drum_on(note, vel);
        return;
    }
    if ((uint32_t)(t - trk) < NPART && note < 128u)
        note_hits[t - trk][note >> 5] |= 1u << (note & 31u);
    if (t->xf_on || t->eng_req != t->engine || t->pold) {   /* engine switch / preset under way: after the fade */
        for (i = 0; i < t->xp_n && t->xp_note[i] != note; i++)
            ;
        if (i == t->xp_n && t->xp_n < 4u)
            t->xp_n++;
        i = i < t->xp_n ? i : t->xp_n - 1u;             /* (full: the last one is replaced) */
        t->xp_note[i] = (uint8_t)note;
        t->xp_vel[i] = (uint8_t)vel;
        return;
    }
    for (i = 0; i < NVOICE; i++)
        any |= t->v[i].gate;
    if (!any) {                                        /* fresh phrase: LFO retrigger (TRIG NOTE) and fade */
        if (!t->p[P_LTRIG])
            t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
        t->lfo_fade = 0;
    }
    if (mode == V_POLY) {
        voice_t *v = voice_alloc(t, note);
        t->nmono = 0;                                   /* no stale mono stack after a mode change */
        t->mono_note = 0;
        v->fine = 0;
        voice_start(t, v, note, vel, t->p[P_GLIDE] != 0);
        return;
    }
    {
        uint32_t sounding = t->mono_note && t->v[0].active && t->v[0].gate, want;
        mono_remove(t, note);
        if (t->nmono >= 8u)
            mono_remove(t, t->mono_stack[0]);
        t->mono_stack[t->nmono++] = (uint8_t)note;
        want = mono_pick(t);
        if (sounding && want == t->mono_note)
            return;                                     /* LOW / HIGH priority: this key does not win */
        if (mode == V_MONO)                             /* MONO: always retrigger, glide when GLIDE is set */
            mono_play(t, want, vel, 1, t->p[P_GLIDE] != 0 || t->slide_glide);
        else                                            /* LEGATO / UNISON: glide only between held notes */
            mono_play(t, want, vel, !sounding, sounding && (t->p[P_GLIDE] != 0 || t->slide_glide));
    }
}

/* STRUM (2.4, P_STRUM): a chord's notes start one after the other, |P_STRUM| ms apart (> 0 from the lowest up, as
 * a guitar's down stroke; < 0 from the highest down). The later ones wait here, in samples; a note-off before its
 * start cancels it, a track's all-off (STOP) clears them */
#define STQ 16u
static struct { uint8_t on, trk, note, vel; uint32_t left; } stq[STQ];
static void strum_cancel(const track_t *t, uint32_t note, int all)
{
    uint32_t i;
    for (i = 0; i < STQ; i++)
        if (stq[i].on && stq[i].trk == (uint8_t)(t - trk) && (all || stq[i].note == note))
            stq[i].on = 0;
}
static void trk_note_off(track_t *t, uint32_t note)
{
    uint32_t i, k = 0, mode = (uint32_t)t->p[P_VOICE];
    if (is_drum(t))
        return;                                         /* one-shots */
    strum_cancel(t, note, 0);
    for (i = 0; i < t->xp_n; i++)                       /* not sounding yet (engine switch): forget it */
        if (t->xp_note[i] != note) {
            t->xp_note[k] = t->xp_note[i];
            t->xp_vel[k++] = t->xp_vel[i];
        }
    t->xp_n = (uint8_t)k;
    mono_remove(t, note);                               /* in any mode: a key let go after a VOICE change must
                                                         * not stay in the MONO stack (after Felucca 1.0) */
    if (mode != V_POLY) {
        uint32_t nv = mode == V_UNISON ? trk_nvoice(t) : 1u;
        if (t->mono_note == note) {
            uint32_t next = mono_pick(t);
            if (next) {                                 /* fall back to a held note, legato */
                mono_play(t, next, t->v[0].vel, 0, t->p[P_GLIDE] != 0);
            } else {
                for (i = 0; i < nv; i++)
                    if (t->v[i].gate) {
                        t->v[i].gate = 0;
                        t->v[i].stage = 3;
                    }
                t->mono_note = 0;
            }
        }
    }
    for (i = 0; i < NVOICE; i++)                        /* POLY voices (also ones left from another mode: */
        if (t->v[i].note == note && t->v[i].gate &&     /* VOICE changed while they were held) */
            (mode == V_POLY || i >= (mode == V_UNISON ? trk_nvoice(t) : 1u) || t->mono_note != note)) {
            t->v[i].gate = 0;
            t->v[i].stage = 3;
        }
}

static void trk_all_off(track_t *t)
{
    uint32_t i;
    strum_cancel(t, 0, 1);
    for (i = 0; i < NVOICE; i++) {
        t->v[i].gate = 0;
        t->v[i].stage = t->v[i].active ? 3 : 0;
    }
    t->nmono = 0;
    t->mono_note = 0;
    t->xp_n = 0;
}

/* note i of a chord of n (notes[]): at once, or strummed (rank: its place from the lowest, or the highest) */
static void trk_note_chord(track_t *t, const uint8_t *notes, uint32_t n, uint32_t i, uint32_t vel)
{
    int32_t s = t->p[P_STRUM];
    uint32_t rank = 0, j, q;
    if (!s || n < 2u || is_drum(t)) {
        trk_note_on(t, notes[i], vel);
        return;
    }
    for (j = 0; j < n; j++)                             /* its place: lowest first (down), highest first (up) */
        if (j != i && (s > 0 ? notes[j] < notes[i] || (notes[j] == notes[i] && j < i)
                             : notes[j] > notes[i] || (notes[j] == notes[i] && j < i)))
            rank++;
    if (!rank) {
        trk_note_on(t, notes[i], vel);
        return;
    }
    strum_cancel(t, notes[i], 0);
    for (q = 0; q < STQ && stq[q].on; q++)
        ;
    if (q == STQ) {                                     /* (full: at once) */
        trk_note_on(t, notes[i], vel);
        return;
    }
    stq[q].trk = (uint8_t)(t - trk);
    stq[q].note = (uint8_t)notes[i];
    stq[q].vel = (uint8_t)vel;
    stq[q].left = rank * (uint32_t)(s < 0 ? -s : s) * (uint32_t)FS / 1000u;
    stq[q].on = 1;
}
/* each audio block: the strummed notes due */
static void strum_block(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < STQ; i++)
        if (stq[i].on) {
            if (stq[i].left <= n) {
                stq[i].on = 0;
                trk_note_on(&trk[stq[i].trk % NTRK], stq[i].note, stq[i].vel);
            } else {
                stq[i].left -= n;
            }
        }
}

/* engine switch, at each block start (events_block), before any note of the block. The UI writes
 * eng_req and the new engine's P_E0..P_E7 together (IRQ off). The part's sounding voices (released by
 * the preset change) then fade out over XF_BLOCKS blocks on the old engine, rendered with its own
 * parameters (pe_old: an engine never reads another engine's values, which index its tables); only
 * then the engine switches and the notes that came during the fade start on it. A part with nothing
 * sounding switches at once. */
#define XF_BLOCKS 8u                                    /* 8 x 32 samples: 5.8 ms (2.6, as Clipper: a bass fades
                                                         * without a thump; 2.9 ms before) */
/* SLOOP 2.6: a preset (factory or user) about to load on t: the sound as it is now kept for the fade (UI, before the
 * new values; IRQ off). The voices sounding then fade out over XF_BLOCKS rendered with these values, the part's DIST,
 * INSERT, level trim and sends included (fx.c mix_part), as for an engine switch: no jump of a filter, a drive or a
 * release in the middle of a note (a click). A fade under way keeps the sound it started from */
static void sound_change(track_t *t)
{
    if (!t->pold) {                                     /* (the ISR reads p_old only once pold is set: no IRQ-off
                                                         * here, the caller's own IRQ-off section stays whole; an
                                                         * engine fade without a preset takes it too, never an old
                                                         * p_old) */
        memcpy(t->p_old, t->p, sizeof t->p_old);
        __asm__ volatile("" ::: "memory");
        t->pold = 1;
    }
}
static void engine_block(track_t *t)
{
    uint32_t i, any = 0;
    if (t->xf_on || t->eng_req != t->engine || t->pold) {
        if (!t->xf_on) {
            for (i = 0; i < NVOICE; i++)
                any |= t->v[i].active;
            if (any) {
                t->xf_on = 1;
                t->xf = XF_BLOCKS;
                return;
            }
        } else if (t->xf) {
            return;                                     /* still fading (track_render counts down) */
        }
        for (i = 0; i < NVOICE; i++) {                  /* faded to 0: gone */
            voice_t *v = &t->v[i];
            v->active = v->gate = 0;
            v->stage = 0;
            v->env = v->env_out = 0;
        }
        if (t->xf_on && t->eng_req == ENGI_FM6 && ((fm6_pend >> ((uint32_t)(t - trk) % NPART)) & 1u))
            return;                                     /* (2.6) FM6: the new notes wait for the preset's patch
                                                         * (eng_fm6.c fm6_poll, main loop): not a note of the old */
        t->engine = t->eng_req % NENGINES;
        t->xf_on = 0;
        t->pold = 0;
        t->nmono = 0;
        t->mono_note = 0;
        {
            uint32_t n = t->xp_n;
            t->xp_n = 0;
            for (i = 0; i < n; i++)
                trk_note_on(t, t->xp_note[i], t->xp_vel[i]);
        }
    }
    for (i = 0; i < 8u; i++)                            /* the engine's own values, for a later fade */
        t->pe_old[i] = t->p[P_E0 + i];
}

/* ENV SYNC (SLOOP 2.6, after Felucca 1.5 by Leo Kuroshita, #175; P_ESYNC, params.c ESYNC_NAMES): ATK DEC REL as
 * note values of the tempo. The part's three steps, made once a block before its voices (track_render; the parts
 * render one at a time), as gen_tables.py makes ENV_LIN / ENV_EXP for a time in ms: the attack's linear step
 * 2^24 CTL / T (rounded), the decay's and release's 1 - e^-x (x = 4.6 CTL / T: ~99 % of the way after T), here as
 * x - x^2/2 + x^3/6 - x^4/24 (x <= 0.33 at the shortest, 1/64T at 240 BPM: under 0.04 % off). The coefficient is Q16 as
 * ENV_EXP's: past ~2 s it is a few units, the time within half of one (4BAR at 90 BPM: 2.5 %). Value 0 (and a time
 * below a block) takes the tables' 0, as with SYNC OFF. OFF: the tables' values, as before (looked up once a block here
 * instead of once a voice in env_tick: the same numbers) */
_Static_assert(CTL == 32, "esync_one: 4.6 CTL in Q16 is 9646899");
static uint32_t esync_k[NTRK][3];   /* ATK's step, DEC's and REL's coefficient (Q16): the part's, this block (env_tick);
                                     * per track (dualcore: a part renders on each core at once) */
static uint32_t esync_one(uint32_t quarter, int32_t v, uint32_t i)
{
    uint32_t u = ESYNC_UNITS[esync_idx(v)], T = quarter * u / 96u, x, x2, x3, x4;   /* T: samples */
    if (T < CTL)
        return i ? ENV_EXP[0] : ENV_LIN[0];
    if (!i)
        return ((1u << 24) * CTL + T / 2u) / T;
    x = (9646899u + T / 2u) / T;                        /* 4.6 CTL (CTL 32), Q16, rounded */
    if (x > 49152u)                                     /* (below ~200 samples: 0.75, the series 0.3 % low) */
        x = 49152u;
    x2 = x * x >> 16;
    x3 = x2 * x >> 16;
    x4 = x3 * x >> 16;
    x = x - x2 / 2u + x3 / 6u - x4 / 24u;
    return x < 1u ? 1u : x > 65535u ? 65535u : x;
}
static __attribute__((noinline)) void esync_coef(const track_t *t)   /* (not inlined: its divides stay out of the
                                                                    * audio ISR's loop over the voices) */
{
    const int16_t *p = t->p;
    uint32_t *ek = esync_k[(uint32_t)(t - trk) % NTRK];
    if (p[P_ESYNC]) {
        uint32_t q = (uint32_t)FS * 60u / (uint32_t)clamp(song.g[G_BPM], 20, 300);   /* a beat (a quarter), samples */
        ek[0] = esync_one(q, p[P_ATK], 0);
        ek[1] = esync_one(q, p[P_DEC], 1);
        ek[2] = esync_one(q, p[P_REL], 2);
    } else {                                            /* OFF: the tables, as always */
        ek[0] = ENV_LIN[p[P_ATK] & 127];
        ek[1] = ENV_EXP[p[P_DEC] & 127];
        ek[2] = ENV_EXP[p[P_REL] & 127];
    }
}

/* one control tick (CTL samples) of the amplitude envelope; returns Q15 */
static int32_t env_tick(track_t *t, voice_t *v)
{
    const uint32_t *ek = esync_k[(uint32_t)(t - trk) % NTRK];   /* the part's steps (esync_coef) */
    const int16_t *p = t->p;
    int32_t sus = (int32_t)p[P_SUS] << 17;              /* Q24 */
    switch (v->stage) {
    case 1:
        v->env += (int32_t)ek[0];                       /* ATK's step (esync_coef: the table's, or ENV SYNC's) */
        if (v->env >= (1 << 24)) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env += mulq16(sus - v->env, ek[1]);
        break;
    case 3:
        v->env -= mulq16(v->env, ek[2]);
        if (v->env < (1 << 12)) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    case 4:                                             /* given up for another part: faded out over KILL_BLOCKS */
        if (v->kill > 1u) {
            v->env -= v->env / v->kill;
            v->kill--;
        } else {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
            v->kill = 0;
        }
        break;
    default:
        v->env = 0;
        break;
    }
    return v->env >> 9;
}

/* render one block of a part into out (cleared here); returns the voices rendered */
static uint32_t track_render(track_t *t, int32_t *out, uint32_t n)
{
    const engine_t *e = ENGINES[t->engine];
    const int16_t *p = t->p;
    uint32_t i;
    int32_t lfo = mulq15(t->lfo_val, t->lfo_fade);
    /* TUNE in cents: whole 1/16 semitones in the pitch, the rest as a fine factor (no dead zone) */
    int32_t tune = song.g[G_TUNE] >= 0 ? song.g[G_TUNE] * 16 / 100 : -((-song.g[G_TUNE] * 16 + 99) / 100);
    int32_t tune_fine = (song.g[G_TUNE] * 16 - tune * 100) * 2367 / 16000;   /* rest, in 1/4096 (1 ct = 2.367) */
    uint32_t nr = 0, fade = t->xf_on && t->xf;
    int32_t bendq;
    int16_t pe_new[8];
    for (i = 0; i < n; i++)
        out[i] = 0;
    if (fade)                                           /* engine switch: the old engine, its own values */
        for (i = 0; i < 8u; i++) {
            pe_new[i] = t->p[P_E0 + i];
            t->p[P_E0 + i] = t->pe_old[i];
        }
    track_lfo_tick(t);
    esync_coef(t);                                      /* this block's envelope steps for env_tick (ENV SYNC) */
    t->bend_s = (int16_t)(t->bend_s + ((t->bend - t->bend_s) >> 2) + (t->bend > t->bend_s) - (t->bend < t->bend_s));
    bendq = ((int32_t)t->bend_s * clamp(song.g[G_BEND], 1, 12)) >> 1;   /* MIDI pitch bend (2.6): 1/4096 semitone,
                                                                         * gliding (a quarter of the way a block) */
    if (e->block)                                       /* the engine's per-part work (DRAWBAR: bars, rotor) */
        e->block(t);
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        int32_t env, pitch;
        if (!v->active)
            continue;
        {
            env = env_tick(t, v);
            if (e->ownenv && v->active) {               /* the engine's envelopes (FM6): they end the voice */
                if (e->done(t, v)) {
                    v->active = v->gate = 0;
                    v->stage = 0;
                    v->kill = 0;
                    v->env = v->env_out = 0;
                    continue;
                }
                if (v->stage != 4u) {                   /* (the ADSR's release never ends it; a voice given up for
                                                         * another part still fades out: env_tick stage 4) */
                    v->env = 1 << 24;
                    env = 32767;
                }
            }
            if (e->amp)                                 /* the engine's own amplitude curve */
                env = e->amp(t, v, env);
            m.envq15 = e->ownenv ? 0 : env;
            m.amp1 = e->ownenv ? env : mulq15(env, v->vel * 258);
            if (p[P_LD_AMP])
                m.amp1 = mulq15(m.amp1, 32767 - mulq15(p[P_LPOL] ? lfo : (lfo + 32768) >> 1, p[P_LD_AMP] * 258));
            if (fade)                                   /* linear to 0 over the fade */
                m.amp1 = m.amp1 * (int32_t)(t->xf - 1u) / (int32_t)XF_BLOCKS;
            m.amp0 = v->env_out;
            v->env_out = m.amp1;
        }
        if (v->pitch_cur != v->pitch16 || v->pitch_frac) {   /* glide, in 1/4096 semitone */
            int32_t q = (v->pitch_cur << 8) + v->pitch_frac, d = (v->pitch16 << 8) - q;
            int32_t st = v->gstep ? v->gstep : (int32_t)((ENV_LIN[p[P_GLIDE] & 127] * 3u) >> 10);   /* RATE: an octave in GLIDE */
            if (st < 1)
                st = 1;
            q += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
            v->pitch_cur = q >> 8;
            v->pitch_frac = (uint8_t)(q & 255);
        }
        if (v->penv)                                    /* the pitch envelope: ~40 ms */
            v->penv = (int16_t)(v->penv - ((v->penv * 590 + 32767) >> 15));
        if (!env && !m.amp0 && v->stage == 2 && !eng_sampled(e))
            continue;                                   /* held at a silent sustain (SUS 0): nothing to render */
        {   /* the pitch in 1/4096 semitone: glide, LFO, the pitch envelope; the fraction goes into the increment */
            int32_t q = (v->pitch_cur << 8) + v->pitch_frac + ((lfo * p[P_LD_PIT] * 3) >> 7) +
                        ((v->penv * p[P_ED_PIT] * 3) >> 7) + bendq;
            pitch = (q >> 8) + tune;
            m.pitch16 = clamp(pitch, 0, 2047);
            m.inc = pitch_inc(m.pitch16);
            q = (q & 255) * 3792 >> 16;                 /* the fraction, as fine (1/16 st = 14.8) */
            m.fine = v->fine + tune_fine + q;
            if (m.fine)                                 /* unison detune, fine tune and the fraction */
                m.inc += (uint32_t)((int32_t)(m.inc >> 12) * m.fine);
        }
        m.cutoff = ((lfo * p[P_LD_FLT]) >> 7) + ((m.envq15 * p[P_ED_FLT]) >> 7);
        if (v->vel > 110)                               /* accent opens the filter with the env */
            m.cutoff += (m.envq15 * 24) >> 7;
        m.shape = (64 << 8) + ((lfo * p[P_LD_SHP]) >> 7) + ((m.envq15 * p[P_ED_SHP]) >> 7);
        e->render(t, v, out, n, &m);
        nr++;
    }
    if (fade) {
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = pe_new[i];
        t->xf--;
    }
    return nr;
}
