/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* TRIO: three oscillators with ring modulation and hard sync into a 12 dB/oct multimode
 * filter, in the style of the sound chips of early 8-bit home computers. Felucca's own design.
 *
 * Oscillators: triangle, saw, pulse (PW), pitched noise (a new random value 16 times a cycle)
 * and two combined waves (saw AND triangle, pulse AND saw: the bitwise AND of the 16-bit
 * waves, as such chips mix two waveforms). WAVE picks one of 16 sets: the three waves, their
 * levels and the interactions:
 *   RING  osc 1's output flips sign with osc 3's top phase bit (the XOR ring of the triangle;
 *         osc 3 may be silent and only modulate),
 *   SYNC  osc 2 restarts whenever osc 1 starts a cycle.
 * EDIT: OSC = WAVE, INT2 / INT3 (osc 2 / osc 3 intervals, semitones), DTN (osc 2 up, osc 3
 * down, cents); TONE = MODE (LP / BP / HP / notch), CUT, RES, PW. The track's ENV / LFO -> FLT
 * move CUT, -> SHP move PW.
 *
 * Band limiting: every step of a saw or a pulse, the sync restarts of osc 2 and the ring
 * flips of osc 1 get a polyBLEP correction. The naive waves are computed and each step adds
 * its residual to the sample before it and the one after it (the voice runs one sample late
 * for that). Triangle and noise have no step correction; the combined waves alias like the
 * originals.
 *
 * Filter: the trapezoidal SVF of dsp.c with LP / BP / HP / notch outputs, a resonance that
 * stops short of self-oscillation, an input that saturates above half scale, a compressed
 * band-pass state (loud resonance gets gritty instead of ringing) and a slow random drift of
 * the cutoff. */
enum { TW_OFF, TW_TRI, TW_SAW, TW_PLS, TW_NOI, TW_ST, TW_PS };
#define TRIO_RING 1u
#define TRIO_SYNC 2u
#define TRIO_G3 11000            /* osc level with three oscillators sounding */
#define TRIO_G2 15000            /* two */
#define TRIO_G1 24000            /* one */

typedef struct {
    uint8_t w[3];                /* TW_* of osc 1..3 */
    uint8_t flags;               /* TRIO_RING | TRIO_SYNC */
    int16_t g[3];                /* Q15 levels (0: silent; osc 3 still runs for RING) */
} trio_set_t;

static const char *const N_TRIO_WAVE[] = {"SAW3", "PLS3", "PPT", "SST", "TRI3", "S&T", "P&S", "P+N",
                                          "NOIS", "SYNC", "SYNCP", "SYNC3", "RING", "RING3", "R+S", "R+SP"};
static const trio_set_t TRIO_SETS[16] = {
    {{TW_SAW, TW_SAW, TW_SAW}, 0, {TRIO_G3, TRIO_G3, TRIO_G3}},                 /* SAW3 */
    {{TW_PLS, TW_PLS, TW_PLS}, 0, {TRIO_G3, TRIO_G3, TRIO_G3}},                 /* PLS3 */
    {{TW_PLS, TW_PLS, TW_TRI}, 0, {TRIO_G3, TRIO_G3, TRIO_G3 + 3000}},          /* PPT */
    {{TW_SAW, TW_SAW, TW_TRI}, 0, {TRIO_G3, TRIO_G3, TRIO_G3 + 3000}},          /* SST */
    {{TW_TRI, TW_TRI, TW_TRI}, 0, {TRIO_G3 + 2000, TRIO_G3 + 2000, TRIO_G3 + 2000}},   /* TRI3 */
    {{TW_ST, TW_ST, TW_TRI}, 0, {TRIO_G3 + 3000, TRIO_G3 + 3000, TRIO_G3}},     /* S&T */
    {{TW_PS, TW_PS, TW_TRI}, 0, {TRIO_G3 + 3000, TRIO_G3 + 3000, TRIO_G3}},     /* P&S */
    {{TW_PLS, TW_OFF, TW_NOI}, 0, {TRIO_G2, 0, TRIO_G2 - 4000}},                /* P+N */
    {{TW_NOI, TW_OFF, TW_OFF}, 0, {TRIO_G1, 0, 0}},                             /* NOIS */
    {{TW_SAW, TW_SAW, TW_OFF}, TRIO_SYNC, {TRIO_G2 - 5000, TRIO_G2 + 5000, 0}}, /* SYNC */
    {{TW_PLS, TW_PLS, TW_OFF}, TRIO_SYNC, {TRIO_G2 - 5000, TRIO_G2 + 5000, 0}}, /* SYNCP */
    {{TW_SAW, TW_SAW, TW_TRI}, TRIO_SYNC, {TRIO_G3 - 3000, TRIO_G3 + 4000, TRIO_G3}},  /* SYNC3 */
    {{TW_TRI, TW_OFF, TW_OFF}, TRIO_RING, {TRIO_G1, 0, 0}},                     /* RING */
    {{TW_TRI, TW_OFF, TW_TRI}, TRIO_RING, {TRIO_G2 + 2000, 0, TRIO_G2 - 3000}}, /* RING3 */
    {{TW_TRI, TW_SAW, TW_OFF}, TRIO_RING | TRIO_SYNC, {TRIO_G2 + 2000, TRIO_G2, 0}},   /* R+S */
    {{TW_TRI, TW_PLS, TW_OFF}, TRIO_RING | TRIO_SYNC, {TRIO_G2 + 2000, TRIO_G2, 0}},   /* R+SP */
};
static const char *const N_TRIO_MODE[] = {"LP", "BP", "HP", "NOT"};
/* filter output = (in * a + bp * b + lp * c) >> 10; HP and notch take k * bp off (k: per block) */
static const int16_t TRIO_MIX[4][3] = {{0, 0, 1024}, {0, 1400, 0}, {1024, 0, -1024}, {1024, 0, 0}};

/* polyBLEP of a step j (the value after it minus the one before, at level g Q15) that lies a
 * fraction x (Q15) of a sample before sample i: half the step is spread over the sample before
 * it (cr[0]) and sample i (cr[1]), with the usual quadratic residual; cr = the block's
 * corrections + i */
static inline void trio_blep(int32_t j, int32_t x, int32_t g, int32_t *cr)
{
    int32_t h = ((j >> 1) * g) >> 15, y = 32767 - x;
    cr[0] += (h * ((x * x) >> 15)) >> 15;
    cr[1] -= (h * ((y * y) >> 15)) >> 15;
}

/* how far (Q15 of one sample) a phase is past a step it crossed: past / inc */
static inline int32_t trio_frac(uint32_t past, uint32_t inc)
{
    uint32_t d = inc >> 15, x;
    if (!d)
        return 0;
    x = past / d;
    return x > 32767u ? 32767 : (int32_t)x;
}

typedef struct {
    uint32_t pw;                 /* pulse width as a phase */
    int32_t pdc, psm;            /* means of the naive pulse and of pulse AND saw (taken off) */
} trio_pw_t;

static inline int32_t trio_tri16(uint32_t ph)          /* the 16-bit triangle of a combined wave */
{
    return (int32_t)(((ph & 0x80000000u) ? ~ph : ph) >> 15) & 0xFFFF;
}
static inline int32_t trio_st(uint32_t ph) { return ((int32_t)(ph >> 16) & trio_tri16(ph)) - 16384; }
static inline int32_t trio_ps(uint32_t ph, const trio_pw_t *q)
{
    return (ph >= q->pw ? (int32_t)(ph >> 16) : 0) - q->psm;
}

/* naive value of a wave at phase ph (the step of a sync restart) */
static int32_t trio_naive(uint32_t w, uint32_t ph, const trio_pw_t *q)
{
    switch (w) {
    case TW_TRI:
        return osc_tri(ph);
    case TW_SAW:
        return (int32_t)(ph >> 16) - 32768;
    case TW_PLS:
        return (ph < q->pw ? 32767 : -32768) - q->pdc;
    case TW_ST:
        return trio_st(ph);
    case TW_PS:
        return trio_ps(ph, q);
    default:
        return 0;
    }
}

/* one oscillator over the block: mix[i] += its sample at level g, its steps into cr; one loop per
 * wave, so the wave is not looked at per sample. wx (osc 1 of a SYNC set: saw or pulse): where it
 * starts a cycle, how far before sample i (Q15), else -1 */
static void trio_pass(uint32_t w, uint32_t *php, uint32_t inc, int32_t g, const trio_pw_t *q, int32_t *nz,
                      int32_t *nst, int32_t *mix, int32_t *cr, int16_t *wx, uint32_t n)
{
    uint32_t ph = *php, o, i, pw = q->pw;
    int32_t pdc = q->pdc, z = *nz;
    switch (w) {
    case TW_TRI:
        for (i = 0; i < n; i++) {
            ph += inc;
            mix[i] += (osc_tri(ph) * g) >> 15;
        }
        break;
    case TW_SAW:
        for (i = 0; i < n; i++) {
            o = ph;
            ph += inc;
            if (ph < o) {
                int32_t x = trio_frac(ph, inc);
                trio_blep(-65536, x, g, cr + i);
                if (wx)
                    wx[i] = (int16_t)x;
            }
            mix[i] += (((int32_t)(ph >> 16) - 32768) * g) >> 15;
        }
        break;
    case TW_PLS:
        for (i = 0; i < n; i++) {
            o = ph;
            ph += inc;
            if (ph < o) {
                int32_t x = trio_frac(ph, inc);
                trio_blep(65535, x, g, cr + i);
                if (wx)
                    wx[i] = (int16_t)x;
            }
            if (ph - pw < o - pw)                       /* crossed pw */
                trio_blep(-65535, trio_frac(ph - pw, inc), g, cr + i);
            mix[i] += (((ph < pw ? 32767 : -32768) - pdc) * g) >> 15;
        }
        break;
    case TW_NOI:
        for (i = 0; i < n; i++) {
            o = ph;
            ph += inc;
            if ((o ^ ph) & 0x08000000u)
                z = (int32_t)(noise32(nst) >> 16) - 32768;
            mix[i] += (z * g) >> 15;
        }
        break;
    case TW_ST:
        for (i = 0; i < n; i++) {
            ph += inc;
            mix[i] += (trio_st(ph) * g) >> 15;
        }
        break;
    case TW_PS:
        for (i = 0; i < n; i++) {
            ph += inc;
            mix[i] += (trio_ps(ph, q) * g) >> 15;
        }
        break;
    default:                                            /* silent: the phase still runs (RING) */
        ph += inc * n;
        break;
    }
    *php = ph;
    *nz = z;
}

/* osc 1 as a triangle whose sign osc 3's top phase bit flips (RING; osc 3 starts at p3); each flip
 * is a step of twice the triangle's value. wx as in trio_pass */
static void trio_ring(uint32_t *php, uint32_t inc, int32_t g, uint32_t p3, uint32_t inc3, int32_t *mix, int32_t *cr,
                      int16_t *wx, uint32_t n)
{
    uint32_t ph = *php, o, o3, i;
    for (i = 0; i < n; i++) {
        int32_t s, sg;
        o = ph;
        ph += inc;
        o3 = p3;
        p3 += inc3;
        if (wx && ph < o)
            wx[i] = (int16_t)trio_frac(ph, inc);
        s = osc_tri(ph);
        sg = (p3 & 0x80000000u) ? -g : g;
        if ((o3 ^ p3) & 0x80000000u)                    /* flipped: a step of 2 s */
            trio_blep(2 * s, trio_frac(p3 & 0x7FFFFFFFu, inc3), sg, cr + i);
        mix[i] += (s * sg) >> 15;
    }
    *php = ph;
}

/* osc 2, saw or pulse, restarted where osc 1 starts a cycle (wx, from trio_pass / trio_ring): the
 * restart lies x before sample i, so osc 2 is x of its own cycle in; the jump from where it was
 * to its start is one step */
static void trio_sync(uint32_t w, uint32_t *php, uint32_t inc, int32_t g, const trio_pw_t *q, const int16_t *wx,
                      int32_t *mix, int32_t *cr, uint32_t n)
{
    uint32_t ph = *php, o, i, pw = q->pw;
    int32_t pdc = q->pdc, pls = w == TW_PLS;
    for (i = 0; i < n; i++) {
        o = ph;
        ph += inc;
        if (wx[i] >= 0) {
            uint32_t back = (uint32_t)wx[i] * (inc >> 15);
            trio_blep(trio_naive(w, 0, q) - trio_naive(w, ph - back, q), wx[i], g, cr + i);
            ph = o = back;                              /* no step of its own in this sample */
        }
        if (pls) {
            if (ph < o)
                trio_blep(65535, trio_frac(ph, inc), g, cr + i);
            if (ph - pw < o - pw)
                trio_blep(-65535, trio_frac(ph - pw, inc), g, cr + i);
            mix[i] += (((ph < pw ? 32767 : -32768) - pdc) * g) >> 15;
        } else {
            if (ph < o)
                trio_blep(-65536, trio_frac(ph, inc), g, cr + i);
            mix[i] += (((int32_t)(ph >> 16) - 32768) * g) >> 15;
        }
    }
    *php = ph;
}

/* osc 2 / 3 increment: the base pitch + semitones + cents, with the voice's fine factor (unison,
 * TUNE) that m->inc carries */
static uint32_t trio_inc(int32_t pitch16, int32_t semi, int32_t ct, int32_t fine)
{
    int32_t d16 = ct * 16 / 100, rem = ct * 16 - d16 * 100;      /* rem: 1/1600 semitone */
    uint32_t inc = pitch_inc(clamp(pitch16 + semi * 16 + d16, 0, 2047));
    return inc + (uint32_t)((int32_t)(inc >> 12) * (rem * 2367 / 16000 + fine));
}

static void trio_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0;
    v->ph[1] = 0x1C000000u;      /* start phases a little apart (a third apart would cancel all but
                                  * every third harmonic of equal-pitched saws until DTN drifts them) */
    v->ph[2] = 0x38000000u;
    v->s[0] = v->s[1] = 0;       /* filter */
    if (!v->s[2])
        v->s[2] = 0x2468ACE + (int32_t)v->age;   /* noise state */
    v->s[3] = 0;                 /* held noise value */
    v->s[4] = 0;                 /* the sample waiting for its step corrections */
    v->s[5] = 0;                 /* cutoff drift */
}

static void trio_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const trio_set_t *ws = &TRIO_SETS[(uint32_t)p[P_E0] & 15u];
    uint32_t w1 = ws->w[0], w2 = ws->w[1], w3 = ws->w[2], i;
    uint32_t ring = ws->flags & TRIO_RING, sync = ws->flags & TRIO_SYNC;
    int32_t g1 = ws->g[0], g2 = ws->g[1], g3 = ws->g[2];
    uint32_t base = pitch_inc(m->pitch16);
    int32_t fine = (base >> 12) ? (int32_t)(m->inc - base) / (int32_t)(base >> 12) : 0;
    uint32_t inc1 = m->inc;
    uint32_t inc2 = trio_inc(m->pitch16, p[P_E1], p[P_E3], fine);
    uint32_t inc3 = trio_inc(m->pitch16, p[P_E2], -p[P_E3], fine);
    int32_t duty = clamp((p[P_E7] << 8) + m->shape - (64 << 8), 768, 32000);   /* Q15 */
    trio_pw_t q;
    uint32_t mode = (uint32_t)p[P_E4] & 3u;
    int32_t reso = p[P_E6], k = 8192 - reso * 7168 / 127; /* damping 2.0 .. 0.25 (Q12): no self-oscillation */
    int32_t ma = TRIO_MIX[mode][0], mb = TRIO_MIX[mode][1], mc = TRIO_MIX[mode][2];
    int32_t a1, a2, a3, g, cut, cur;
    int32_t mix[CTL], cr[CTL + 1];                      /* blocks are CTL long (dsp.c amp_at) */
    int16_t wx[CTL];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], ph3s = ph2;   /* state in locals: out[] may alias v->s[] */
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2], nz = v->s[3], prev = v->s[4], drift = v->s[5];
    if (n > CTL)
        n = CTL;
    q.pw = (uint32_t)duty << 17;
    q.pdc = 2 * duty - 32768;
    q.psm = 32768 - ((duty * duty) >> 15);
    if (mode >= 2u)
        mb = -(k >> 2);                                 /* HP / notch: in - k bp (- lp) */
    /* the cutoff wanders a little, once per block (the slightly unstable filter of the chips) */
    drift += (((int32_t)(noise32(&nst) >> 22) - 512) - drift) >> 3;
    cut = clamp((p[P_E5] << 8) + m->cutoff + drift, 0, 127 << 8);
    g = SVF_G[cut >> 8];
    if ((cut >> 8) < 127)
        g += ((SVF_G[(cut >> 8) + 1] - g) * (cut & 255)) >> 8;
    a1 = (int32_t)((4096u << 13) / (uint32_t)(4096 + ((g * (g + k)) >> 12)));
    a2 = (a1 * g) >> 12;
    a3 = (a2 * g) >> 12;
    for (i = 0; i < n; i++) {
        mix[i] = cr[i] = 0;
        wx[i] = -1;
    }
    cr[n] = 0;
    trio_pass(w3, &ph2, inc3, g3, &q, &nz, &nst, mix, cr, 0, n);   /* first: RING reads its start phase */
    if (ring)
        trio_ring(&ph0, inc1, g1, ph3s, inc3, mix, cr, sync ? wx : 0, n);
    else
        trio_pass(w1, &ph0, inc1, g1, &q, &nz, &nst, mix, cr, sync ? wx : 0, n);
    if (sync)
        trio_sync(w2, &ph1, inc2, g2, &q, wx, mix, cr, n);
    else
        trio_pass(w2, &ph1, inc2, g2, &q, &nz, &nst, mix, cr, 0, n);
    cur = prev;
    for (i = 0; i < n; i++) {
        int32_t x = cur + cr[i], y, v1, v2, v3, a;     /* one sample late: its corrections are complete */
        cur = mix[i];
        a = x < 0 ? -x : x;                             /* the input saturates above 16000 (soft knee) */
        if (a > 16000) {
            a = 16000 + (softclip((a - 16000) * 2) >> 1);
            x = x < 0 ? -a : a;
        }
        x >>= 1;
        /* SVF (dsp.c tsvf_lp, with the band-pass out); the band-pass state is compressed */
        v3 = x - ic2;
        v1 = (a1 * ic1 + a2 * v3) >> 13;
        v2 = ic2 + ((a2 * ic1 + a3 * v3) >> 13);
        ic1 = 2 * v1 - ic1;
        ic2 = clamp(2 * v2 - ic2, -150000, 150000);
        a = clamp(ic1, -49152, 49152);                  /* beyond +-49152 a quarter of the excess */
        ic1 = a + ((ic1 - a) >> 2);
        y = (x * ma + v1 * mb + v2 * mc) >> 10;
        a = y < 0 ? -y : y;                             /* linear up to half scale, then a soft knee */
        if (a > 16000) {
            a = 16000 + (softclip((a - 16000) * 2) >> 1);
            y = y < 0 ? -a : a;
        }
        out[i] += mulq15(mulq15(y << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    prev = cur + cr[n];
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
    v->s[3] = nz;
    v->s[4] = prev;
    v->s[5] = drift;
}

static const preset_t TRIO_PRESETS[] = {
    /* name, {WAVE, INT2, INT3, DTN, MODE, CUT, RES, PW}, {A D S R}, fenv, mono. INT2 / INT3: one-finger chords */
    {"FAT BASS", {0, 0, -12, 9, 0, 72, 45, 64}, {0, 64, 80, 25}, 40, 1, FX(22, 0, 0, 6), XP(P_TRANS + 1, -24)},
    {"MIN STAB", {0, 3, 7, 6, 0, 82, 20, 64}, {0, 66, 26, 36}, 30, 0, FX(0, 20, 20, 30)},
    {"MIN7 STAB", {3, 3, 10, 5, 0, 75, 15, 64}, {0, 66, 28, 40}, 20, 0, FX(0, 25, 18, 30)},
    /* rave: a bright minor stab, the filter snapping shut */
    {"RAVE STAB", {0, 3, 7, 14, 0, 98, 30, 64}, {0, 66, 30, 34}, 40, 0, FX(16, 20, 24, 30)},
    /* dub techno: a dark minor 7th chord into the delay */
    {"DUB CHORD", {3, 3, 10, 6, 0, 68, 40, 64}, {0, 66, 22, 46}, 28, 0, FX(0, 10, 72, 40)},
    {"SAW PAD", {0, 0, 12, 30, 0, 72, 10, 64}, {52, 90, 115, 86}, 6, 0, FX(0, 40, 20, 60)},
    {"SYNC LEAD", {9, 9, 0, 0, 0, 82, 30, 64}, {2, 70, 100, 40}, 18, 1, FX(0, 10, 30, 22), XP(P_GLIDE + 1, 56)},
    {"HOOVER", {0, 0, -12, 40, 0, 70, 20, 64}, {10, 70, 110, 40}, 10, 1, FX(10, 40, 10, 24), XP(P_GLIDE + 1, 70)},
    /* SLOOP 2.5: a musette accordion, a 70s string machine, an 80s poly synth */
    {"ACCORDION", {1, 0, 12, 14, 0, 92, 10, 40}, {6, 70, 118, 24}, 0, 0, FX(0, 20, 0, 22)},
    {"ENSEMBLE", {0, 12, 0, 10, 0, 85, 0, 64}, {50, 90, 118, 80}, 0, 0, FX(0, 90, 10, 55)},
    {"80S POLY", {0, 0, 12, 12, 0, 70, 30, 64}, {2, 72, 70, 50}, 34, 0, FX(0, 50, 22, 34)},
    {"SQR LEAD", {1, 0, 12, 4, 0, 90, 20, 50}, {2, 70, 110, 30}, 10, 1, FX(0, 10, 28, 22), XP(P_GLIDE + 1, 40)},
    {"POWER STAB", {0, 7, 12, 8, 0, 85, 25, 64}, {0, 70, 50, 36}, 30, 0, FX(16, 10, 20, 25)},
    {"PULSE PAD", {1, 0, 12, 20, 0, 70, 10, 30}, {60, 90, 115, 85}, 0, 0, FX(0, 50, 20, 60), XP(P_LD_SHP + 1, 25, P_LRATE + 1, 30)},
};

static const engine_t ENG_TRIO = {
    "TRIO", {"OSC", "TONE"},
    {
        {"WAVE", F_ENUM, 0, 15, 0, N_TRIO_WAVE, 0},
        {"INT2", F_SEMI, -24, 24, 0, 0, 0},
        {"INT3", F_SEMI, -24, 24, -12, 0, 0},
        {"DTN", F_INT, 0, 50, 6, 0, "ct"},
        {"MODE", F_ENUM, 0, 3, 0, N_TRIO_MODE, 0},
        {"CUT", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 40, 0, 0},
        {"PW", F_PCT, 0, 127, 64, 0, 0},
    },
    TRIO_PRESETS, sizeof(TRIO_PRESETS) / sizeof(TRIO_PRESETS[0]), 1, trio_note_on, trio_render,
    0xFFE0, {P_E5, P_E6, P_E7, P_E1},
};
