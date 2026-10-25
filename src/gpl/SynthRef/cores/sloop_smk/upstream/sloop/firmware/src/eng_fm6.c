/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM6: classic 6-operator FM (SLOOP 2.4, from Felucca 1.0.1). The synthesis is msfa (Dexed's core), ported to
 * integer C in fm6_core.c (Apache-2.0); this file is the engine around it.
 *
 * The patch is the sound: every synth part has one (fm6_patch, the generic 155-byte single-voice layout: six
 * operators with their 4-rate / 4-level envelopes, keyboard level and rate scaling, velocity, ratio or fixed
 * frequency, detune; 32 algorithms, feedback, LFO, pitch envelope, transpose). It is edited in the web editor
 * (EDITOR_PROTOCOL.md FM6_*) and loaded from the PTCH slots. Unlike Felucca 1.0, a SLOOP project does not hold
 * it (project_t is full): the project keeps PTCH (P_E7) as it keeps every macro, and a load (project, preset,
 * user preset, PTCH turned) puts the slot's patch back on the track (fm6_track_loaded / fm6_poll). A patch the
 * editor sent is the track's own until the track loads another; "Store in bank" (fm6_bank.c) keeps it.
 * On the device the eight EDIT values are macros on top of it:
 *   ALG   PAT = the patch's algorithm, 1..32 another one
 *   FB    added to the patch's feedback (0..7)
 *   MLVL  the output level of every operator that is not a carrier (-36 .. +36 dB): the brightness
 *   MRAT  added to the coarse ratio of the modulators (ratio mode only)
 *   MEG   the modulators' envelope times: + slower (rates down to 40 steps), - faster
 *   VMOD  added to the modulators' velocity sensitivity (0..7)
 *   DTUN  spreads the carriers apart in pitch (up to about +-36 cents between the outer ones)
 *   PTCH  loads a patch: F1..F8 the factory patches, B1..B27 the patch bank (fm6_bank.c, flash)
 * The operator envelopes are the voice's amplitude and end it (engine_t.ownenv / done): the track's ADSR and
 * ENV DEST do nothing here. The track's FLT moves MLVL (LFO -> FLT), SHP the feedback, PIT the pitch (glide,
 * tune: the voice's pitch and fine factor). Six voices per part at most (engine_t.poly).
 *
 * State (RAM, NPART parts: the drum track runs no engine): per voice an fm6_note_t (fm6_note, 244 bytes, a
 * side array in the pool: voice_t.s[] is too small), per part the patch, the patch through the macros
 * (fm6_eff, rebuilt in the audio ISR when either changes) and the LFO (once a block). Adapted from Felucca
 * 1.0.1: no modulation matrix, the velocity is the voice's (UNISON scales it), the fine factor comes in
 * vmod_t.fine (voice.c folds it into inc for the other engines). */
#include "fm6_core.c"
#include "felucca_fm6.h"         /* tools/gen_fm6_patches.py: FM6_INIT, FM6_FACTORY[] */

#define FM6_POLY 6               /* engine_t.poly */
typedef struct {                 /* a part's FM6 state: its engine arena (engines.c) */
    fm6_note_t note[FM6_POLY];
    int32_t bus[2][FM6_N];       /* fm6_core_render's modulator buses: the part's own, so parts render on both cores */
} fm6_part_t;
#define FM6_BANK_N 27u           /* patch bank slots (fm6_bank.c) */
#define FM6_NSLOT (FM6_NFACTORY + FM6_BANK_N)   /* PTCH: F1..F8, B1..B27 */
#define FM6_PACKED 128u

static uint8_t fm6_patch[NPART][FP_SIZE + 1u];  /* the parts' patches (main loop writes, then fm6_pgen) */
static volatile uint8_t fm6_pgen[NPART];         /* +1 after each write of fm6_patch[t] */
static uint8_t fm6_slot[NPART];                  /* the PTCH value last loaded (main loop); 0xFF = none */
static struct {                                  /* the patch through the macros: the audio ISR's copy */
    uint8_t p[FP_SIZE + 1u];
    uint8_t gen, alg, fb, ok;
    int16_t e[7];                                /* P_E0..P_E6 it was made with */
    int32_t dt[6];                               /* DTUN: per operator, Q24 log2 */
} fm6_eff[NPART];
static fm6_lfo_t fm6_lfo[NPART];
static int32_t fm6_lfo_v[NPART], fm6_lfo_d[NPART];   /* this block's LFO value and delay (Q24) */
/* the voices' fm6_note_t: the part's engine arena (engines.c eng_arena_of) */

/* ------------------------------------------------------- patch formats --- */
/* the highest value of each byte of the 155-byte voice */
static uint32_t fm6_max(uint32_t i)
{
    static const uint8_t OPMAX[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
    static const uint8_t VMAX[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
    if (i < 126u)
        return OPMAX[i % 21u];
    if (i < FP_NAME)
        return VMAX[i - 126u];
    return 126u;
}

/* every value inside its range (a name byte outside 32..126 becomes a space) */
static void fm6_sanitize(uint8_t *v)
{
    uint32_t i;
    for (i = 0; i < FP_SIZE; i++) {
        if (i >= FP_NAME)
            v[i] = v[i] < 32u || v[i] > 126u ? ' ' : v[i];
        else if (v[i] > fm6_max(i))
            v[i] = (uint8_t)fm6_max(i);
    }
    v[FP_SIZE] = 0;
}

/* 128-byte packed record (the 32-voice bank's) -> 155-byte voice; bits a record does not use are ignored */
static void fm6_unpack(const uint8_t *b, uint8_t *v)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = b + k * 17u;
        uint8_t *d = v + k * FP_OP;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[FP_LC] = o[11] & 3u;
        d[FP_RC] = (o[11] >> 2) & 3u;
        d[FP_RS] = o[12] & 7u;
        d[FP_DET] = (o[12] >> 3) & 15u;
        d[FP_AMS] = o[13] & 3u;
        d[FP_KVS] = (o[13] >> 2) & 7u;
        d[FP_OL] = o[14] & 0x7Fu;
        d[FP_MODE] = o[15] & 1u;
        d[FP_FC] = (o[15] >> 1) & 31u;
        d[FP_FF] = o[16] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        v[FP_PR1 + i] = b[102 + i] & 0x7Fu;              /* pitch EG, algorithm */
    v[FP_ALG] &= 31u;
    v[FP_FB] = b[111] & 7u;
    v[FP_OKS] = (b[111] >> 3) & 1u;
    for (i = 0; i < 4u; i++)
        v[FP_LFS + i] = b[112 + i] & 0x7Fu;
    v[FP_LKS] = b[116] & 1u;
    v[FP_LFW] = (b[116] >> 1) & 7u;
    v[FP_LPMS] = (b[116] >> 4) & 7u;
    v[FP_TRNSP] = b[117] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        v[FP_NAME + i] = b[118 + i] & 0x7Fu;
    fm6_sanitize(v);
}

/* 155-byte voice -> 128-byte packed record (7-bit bytes: it travels in SysEx as it is) */
static void fm6_pack(const uint8_t *v, uint8_t *b)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = v + k * FP_OP;
        uint8_t *d = b + k * 17u;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[11] = (uint8_t)((o[FP_LC] & 3u) | (o[FP_RC] & 3u) << 2);
        d[12] = (uint8_t)((o[FP_RS] & 7u) | (o[FP_DET] & 15u) << 3);
        d[13] = (uint8_t)((o[FP_AMS] & 3u) | (o[FP_KVS] & 7u) << 2);
        d[14] = o[FP_OL] & 0x7Fu;
        d[15] = (uint8_t)((o[FP_MODE] & 1u) | (o[FP_FC] & 31u) << 1);
        d[16] = o[FP_FF] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        b[102 + i] = v[FP_PR1 + i] & 0x7Fu;
    b[110] &= 31u;
    b[111] = (uint8_t)((v[FP_FB] & 7u) | (v[FP_OKS] & 1u) << 3);
    for (i = 0; i < 4u; i++)
        b[112 + i] = v[FP_LFS + i] & 0x7Fu;
    b[116] = (uint8_t)((v[FP_LKS] & 1u) | (v[FP_LFW] & 7u) << 1 | (v[FP_LPMS] & 7u) << 4);
    b[117] = v[FP_TRNSP] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        b[118 + i] = v[FP_NAME + i] & 0x7Fu;
}

/* ---------------------------------------------------- the track's patch --- */
/* the patch bank (fm6_bank.c sets it with FELUCCA_FLASH): slot k's packed record -> pk, 0 = got it */
static int (*fm6_bank_read)(uint32_t k, uint8_t *pk);

/* part tr's patch = v (155 bytes, sanitized). Main loop: the ISR takes it at its next block */
static void fm6_set_patch(uint32_t tr, const uint8_t *v)
{
    uint8_t s[FP_SIZE + 1u];
    if (tr >= NPART)
        return;
    memcpy(s, v, FP_SIZE);
    fm6_sanitize(s);                                /* not in place: fm6_sync must never copy an unsanitized byte */
    memcpy(fm6_patch[tr], s, FP_SIZE + 1u);
    RING_PUBLISH();
    fm6_pgen[tr]++;
}

/* PTCH value s -> its packed record (F1..F8; the bank, an empty slot: the init voice) */
static void fm6_slot_get(uint32_t s, uint8_t *pk)
{
    if (s < FM6_NFACTORY)
        memcpy(pk, FM6_FACTORY[s], FM6_PACKED);
    else if (s >= FM6_NSLOT || !fm6_bank_read || fm6_bank_read(s - FM6_NFACTORY, pk))
        memcpy(pk, FM6_INIT, FM6_PACKED);
}

static void fm6_load_slot(uint32_t tr, uint32_t s)
{
    uint8_t pk[FM6_PACKED], v[FP_SIZE + 1u];
    if (tr >= NPART)
        return;
    fm6_slot_get(s, pk);
    fm6_unpack(pk, v);
    fm6_set_patch(tr, v);
    fm6_slot[tr] = (uint8_t)s;
}

/* a sound load put a PTCH value in (a preset, a user preset, a project, an engine change): its patch */
static uint8_t fm6_pend;                            /* (2.6) bit per part: a preset's patch waiting for the fade */
static void fm6_track_loaded(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
    if (tr < NPART && t->eng_req == ENGI_FM6) {
        uint32_t i, any = 0;
        for (i = 0; i < NVOICE; i++)
            any |= t->v[i].active;
        if (any && (t->pold || t->xf_on)) {             /* the old sound still fading out (voice.c sound_change): */
            fm6_pend |= (uint8_t)(1u << tr);           /* its patch stays until then (fm6_poll; engine_block holds */
            return;                                     /* the new notes for it) */
        }
        fm6_load_slot(tr, (uint32_t)clamp(t->p[P_E7], 0, FM6_NSLOT - 1));
    }
}

/* power-on: every part the init voice */
static void fm6_init(void)
{
    uint8_t v[FP_SIZE + 1u];
    uint32_t tr;
    fm6_unpack(FM6_INIT, v);
    for (tr = 0; tr < NPART; tr++) {
        fm6_set_patch(tr, v);
        fm6_slot[tr] = 0xFFu;
    }
}

/* main loop: PTCH turned (a knob, the editor, MIDI, a lock) -> that patch */
static void fm6_poll(void)
{
    uint32_t tr;
    for (tr = 0; tr < NPART; tr++) {
        if ((trk[tr].pold || trk[tr].xf_on) && !(trk[tr].xf_on && !trk[tr].xf))
            continue;                                   /* (a fade under way: the patch after it; faded out, the
                                                         * notes wait for it: voice.c engine_block) */
        if (trk[tr].eng_req == ENGI_FM6 && (trk[tr].p[P_E7] != fm6_slot[tr] || ((fm6_pend >> tr) & 1u)))
            fm6_load_slot(tr, (uint32_t)clamp(trk[tr].p[P_E7], 0, FM6_NSLOT - 1));
        fm6_pend &= (uint8_t)~(1u << tr);
    }
}

/* -------------------------------------------------------------- macros --- */
/* the patch through E0..E6 (audio ISR; cheap when nothing changed) */
static void fm6_sync(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk), k, j, car, n = 0;
    const int16_t *e = &t->p[P_E0];
    int32_t meg, step;
    if (tr >= NPART)
        return;
    if (fm6_eff[tr].ok && fm6_eff[tr].gen == fm6_pgen[tr]) {
        for (k = 0; k < 7u && fm6_eff[tr].e[k] == e[k]; k++)
            ;
        if (k == 7u)
            return;
    }
    fm6_eff[tr].gen = fm6_pgen[tr];
    memcpy(fm6_eff[tr].p, fm6_patch[tr], FP_SIZE);
    for (k = 0; k < 7u; k++)
        fm6_eff[tr].e[k] = e[k];
    {
        uint8_t *p = fm6_eff[tr].p;
        uint32_t alg = e[0] >= 1 && e[0] <= 32 ? (uint32_t)e[0] - 1u : p[FP_ALG] & 31u;
        fm6_eff[tr].alg = (uint8_t)alg;
        fm6_eff[tr].fb = (uint8_t)clamp(p[FP_FB] + e[1], 0, 7);
        car = fm6_carriers(alg);
        meg = clamp(e[4], -64, 63) * 40 / 64;               /* MEG: rate steps (+ slower) */
        step = clamp(e[6], 0, 127) * (12 * 13981) / 127;    /* DTUN: up to 12 cents a step (1 cent = 13981) */
        for (k = 0; k < 6u; k++) {
            uint8_t *op = p + (5u - k) * FP_OP;              /* OP1 first: carriers in their order */
            uint32_t ki = 5u - k;
            fm6_eff[tr].dt[ki] = 0;
            if ((car >> ki) & 1u) {                          /* carriers 0, +1, -1, +2, -2, +3 steps apart */
                static const int8_t SPREAD[6] = {0, 1, -1, 2, -2, 3};
                fm6_eff[tr].dt[ki] = SPREAD[n++] * step;
                continue;
            }
            if (!op[FP_MODE])
                op[FP_FC] = (uint8_t)clamp(op[FP_FC] + e[3], 0, 31);
            for (j = 0; j < 4u; j++)
                op[FP_R1 + j] = (uint8_t)clamp(op[FP_R1 + j] - meg, 0, 99);
            op[FP_KVS] = (uint8_t)clamp(op[FP_KVS] + e[5], 0, 7);
        }
        fm6_lfo_reset(&fm6_lfo[tr], p);
    }
    fm6_eff[tr].ok = 1;
}

/* --------------------------------------------------------------- voice --- */
static fm6_part_t *fm6_part_of(track_t *t)
{
    return t < &trk[0] || t >= &trk[NPART] ? 0 : (fm6_part_t *)eng_arena_of(t, ENGI_FM6);
}
static fm6_note_t *fm6_note_of(track_t *t, voice_t *v)
{
    fm6_part_t *P = fm6_part_of(t);
    uint32_t i;
    if (!P)
        return 0;
    i = (uint32_t)(v - t->v);
    return i < FM6_POLY ? &P->note[i] : 0;
}

static void fm6_note_on(track_t *t, voice_t *v)
{
    fm6_note_t *n = fm6_note_of(t, v);
    uint32_t tr = (uint32_t)(t - trk);
    const uint8_t *p;
    if (!n)
        return;
    fm6_sync(t);
    p = fm6_eff[tr].p;
    fm6_note_init(n, p, (int32_t)v->note + p[FP_TRNSP] - 24, v->vel,
                  (!v->env && !v->env_out) || !n->live);    /* from silence: phases and gains from 0 */
    fm6_lfo_key(&fm6_lfo[tr]);
}

static void fm6_block(track_t *t)       /* once a block and part: the macros, the LFO */
{
    uint32_t tr = (uint32_t)(t - trk);
    if (tr >= NPART)
        return;
    fm6_sync(t);
    fm6_lfo_v[tr] = fm6_lfo_sample(&fm6_lfo[tr]);
    fm6_lfo_d[tr] = fm6_lfo_delay(&fm6_lfo[tr]);
}

/* every carrier has gone silent for good: the voice ends (voice.c, engine_t.done) */
static int fm6_done(track_t *t, voice_t *v)
{
    fm6_note_t *n = fm6_note_of(t, v);
    uint32_t tr = (uint32_t)(t - trk);
    if (!n || !n->live)
        return 1;
    if (!fm6_note_done(n, fm6_eff[tr].p, fm6_eff[tr].alg))
        return 0;
    n->live = 0;
    return 1;
}

static void fm6_render(track_t *t, voice_t *v, int32_t *out, uint32_t len, const vmod_t *m)
{
    fm6_part_t *P = fm6_part_of(t);
    fm6_note_t *n = fm6_note_of(t, v);
    uint32_t tr = (uint32_t)(t - trk), i;
    const uint8_t *p;
    int32_t bus[FM6_N], lf, fb, lvl, k, a, da;
    if (!n || !n->live || len != FM6_N)
        return;
    p = fm6_eff[tr].p;
    if (!v->gate && n->down)
        fm6_note_key(n, p, 0);
    lf = fm6_note_logfreq(m->pitch16 + (p[FP_TRNSP] - 24) * 16, m->fine);
    fb = clamp(fm6_eff[tr].fb + ((m->shape - (64 << 8)) >> 11), 0, 7);
    lvl = clamp(t->p[P_E2], -64, 63) * 24 + clamp(m->cutoff >> 8, -150, 150) * 16;   /* microsteps */
    if (!fm6_note_compute(n, p, bus, P->bus, fm6_lfo_v[tr], fm6_lfo_d[tr], lf, fm6_eff[tr].alg, fb, fm6_eff[tr].dt, lvl << 16))
        return;                                         /* every carrier below the threshold */
    /* the voice's amplitude ramp x VOICE_FS (one carrier at full: VOICE_FS / 2), six voices in unison ~ one */
    k = t->p[P_VOICE] == V_UNISON ? VOICE_FS * 2 / 5 : VOICE_FS;
    a = mulq15(m->amp0, k);
    da = (mulq15(m->amp1, k) - a) >> FM6_LG_N;
    for (i = 0; i < FM6_N; i++) {
        int32_t x = bus[i] - n->dc;
        n->dc += x >> 10;                                 /* a one-pole high-pass, ~7 Hz: FM puts sidebands at 0 Hz */
        a += da;
        out[i] += (int32_t)(((int64_t)x * a) >> 26);
    }
}

/* --------------------------------------------------------- the engine --- */
static const char *const N_FM6_ALG[] = {"PAT", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                        "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25",
                                        "26", "27", "28", "29", "30", "31", "32", 0};
static const char *const N_FM6_PATCH[] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "B1", "B2", "B3", "B4",
                                          "B5", "B6", "B7", "B8", "B9", "B10", "B11", "B12", "B13", "B14", "B15",
                                          "B16", "B17", "B18", "B19", "B20", "B21", "B22", "B23", "B24", "B25",
                                          "B26", "B27", 0};
_Static_assert(NELEM(N_FM6_PATCH) == FM6_NSLOT + 1u, "a PTCH name per slot");

/* {ALG, FB, MLVL, MRAT, MEG, VMOD, DTUN, PTCH}: the factory patch F1..F8 as it is, DTUN on the pad. The ADSR
 * does nothing on this engine (ownenv): kept at its defaults. Names as ui.c BANK[] lists them (by kind) */
static const preset_t FM6_PRESETS[] = {
    {"TINE EP", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0, FX(0, 45, 25, 35)},
    {"GLASS BELL", {0, 0, 0, 0, 0, 0, 0, 1}, {0, 0, 127, 0}, 0, 0, FX(0, 10, 30, 70)},
    {"ROUND BASS", {0, 0, 0, 0, 0, 0, 0, 2}, {0, 0, 127, 0}, 0, 1, FX(0, 0, 10, 10)},
    {"BRASS SECT", {0, 0, 0, 0, 0, 0, 0, 3}, {0, 0, 127, 0}, 0, 0, FX(0, 25, 20, 40)},
    {"SOFT PAD", {0, 0, 0, 0, 0, 0, 30, 4}, {0, 0, 127, 0}, 0, 0, FX(0, 60, 30, 70)},
    {"WOOD BARS", {0, 0, 0, 0, 0, 0, 0, 5}, {0, 0, 127, 0}, 0, 0, FX(0, 0, 25, 40)},
    {"DRAWBARS", {0, 0, 0, 0, 0, 0, 0, 6}, {0, 0, 127, 0}, 0, 0, FX(10, 40, 0, 30)},
    {"NYLON PICK", {0, 0, 0, 0, 0, 0, 0, 7}, {0, 0, 127, 0}, 0, 0, FX(0, 20, 35, 30)},
};

static const engine_t ENG_FM6 = {
    "FM6", {"OPS", "PATCH"},
    {
        {"ALG", F_INT, 0, 32, 0, N_FM6_ALG, 0},
        {"FB", F_INT, -7, 7, 0, 0, 0},
        {"MLVL", F_BIPCT, -64, 63, 0, 0, 0},
        {"MRAT", F_INT, -16, 16, 0, 0, 0},
        {"MEG", F_BIPCT, -64, 63, 0, 0, 0},
        {"VMOD", F_INT, -7, 7, 0, 0, 0},
        {"DTUN", F_PCT, 0, 127, 0, 0, 0},
        {"PTCH", F_INT, 0, FM6_NSLOT - 1, 0, N_FM6_PATCH, 0},
    },
    FM6_PRESETS, NELEM(FM6_PRESETS), -1, fm6_note_on, fm6_render,
    0x4E99, {P_E2, P_E3, P_E4, P_E7}, FM6_POLY, 0, 0, fm6_block,
    .ownenv = 1, .done = fm6_done,
};
