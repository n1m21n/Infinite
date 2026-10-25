/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Phase distortion. The waveforms are a C port of the oscillator of
 * CrispyZebra (Leo Kuroshita, GPL-3.0, github.com/hugelton/CrispyZebra), the
 * author's own phase-distortion core: a -cos table read through a bent phase whose
 * bend is DCW. WAVE2 alternates with WAVE every other cycle. A second
 * line (DTN) can be mixed or ring-modulated; SUB adds a sine an octave down.
 * Envelopes are Felucca's own (ADSR, ENV -> DCW). */
static const char *const N_PD_WAVE[] = {"SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};
static const char *const N_PD_WAVE2[] = {"-", "SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};
static const char *const N_PD_LINE[] = {"MIX", "RING"};

static inline int32_t pd_cos(uint32_t ph16)             /* -cos, Q15, from the 16-bit PD phase */
{
    return -sine_i(((ph16 & 0xFFFFu) << 16) + 0x40000000u);
}

/* one wave's bend for a block: break points and the slopes of the bent phase
 * (Q16 reciprocals), so a sample costs a multiply instead of a divide (the
 * bent phase comes out the same or 1 lower in 65536) */
typedef struct {
    uint32_t w, x, peak, rez;
    uint32_t k0, k1;
} pd_t;

static inline uint32_t pd_slope(uint32_t span, uint32_t len) { return (span << 16) / (len ? len : 1u); }

static void pd_setup(pd_t *b, uint32_t w, uint32_t dcw)
{
    uint32_t x;
    b->w = w;
    switch (w) {
    case 0:                                                  /* SAW */
        x = 32768u + ((dcw * 32767u) >> 16);
        b->k0 = pd_slope(32768u, x);
        b->k1 = pd_slope(32767u, 65535u - x);
        break;
    case 1:                                                  /* SQUARE */
        x = 32768u - ((dcw * 31120u) >> 16);
        b->k0 = pd_slope(32768u, x);
        b->k1 = pd_slope(32767u, x);
        break;
    case 2:                                                  /* PULSE */
        x = 65535u - ((dcw * 63487u) >> 16);
        b->k0 = pd_slope(65535u, x);
        break;
    case 3:                                                  /* DOUBLE SINE */
        x = 65535u - ((dcw * 49151u) >> 16);
        b->k0 = pd_slope(65535u, x);
        b->k1 = pd_slope(65535u, 65535u - x);
        break;
    case 4:                                                  /* SAW-PULSE */
        x = 65535u - ((dcw * 32767u) >> 16);
        b->peak = (x * (32768u + ((dcw * 29491u) >> 16))) >> 16;
        if (!b->peak)
            b->peak = 1;
        b->k0 = pd_slope(32768u, b->peak);
        b->k1 = pd_slope(32767u, x - b->peak);
        break;
    default:                                                 /* RESONANCE */
        x = 0;
        b->rez = (dcw * 44000u) >> 16;
        break;
    }
    b->x = x;
}

/* one sample at phase ph (16 bit): bipolar Q15 */
static int32_t pd_wave(const pd_t *b, uint32_t ph)
{
    uint32_t pd, x = b->x;
    switch (b->w) {
    case 0:                                                  /* SAW */
        pd = ph < x ? (ph * b->k0) >> 16 : 32768u + (((ph - x) * b->k1) >> 16);
        break;
    case 1:                                                  /* SQUARE */
        if (ph < x)
            pd = (ph * b->k0) >> 16;
        else if (ph < 32768u)
            pd = 32768u;
        else if (ph < 32768u + x)
            pd = 32768u + (((ph - 32768u) * b->k1) >> 16);
        else
            pd = 65535u;
        break;
    case 2:                                                  /* PULSE */
        pd = ph < x ? (ph * b->k0) >> 16 : 65535u;
        break;
    case 3:                                                  /* DOUBLE SINE */
        pd = ph < x ? (ph * b->k0) >> 16 : ((ph - x) * b->k1) >> 16;
        break;
    case 4:                                                  /* SAW-PULSE */
        if (ph >= x)
            pd = 65535u;
        else if (ph < b->peak)
            pd = (ph * b->k0) >> 16;
        else
            pd = 32768u + (((ph - b->peak) * b->k1) >> 16);
        break;
    default: {                                               /* RESONANCE: windowed sine, rez = pitch of the core */
        uint32_t rp = (ph + ((ph * b->rez) >> 16) * 7u) & 0xFFFFu, win;
        int32_t core = (pd_cos(rp) * 5) >> 3;
        if (b->w == 5)
            win = 65535u - ph;                               /* saw window */
        else if (b->w == 6)
            win = ph < 32768u ? ph << 1 : (65535u - ph) << 1; /* triangle window */
        else
            win = ph < 16384u ? ph << 2 : ph > 49152u ? (65535u - ph) << 2 : 65535u;   /* trapezoid */
        if (win > 65535u)
            win = 65535u;
        return (core * (int32_t)win) >> 16;                  /* |core| <= 20479: fits 32 bits */
    }
    }
    return pd_cos(pd);
}

static void phase_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = 0;
    v->ph[2] = 0;
    v->s[0] = 0;                 /* WAVE / WAVE2 toggle, line 1 */
    v->s[1] = 0;                 /* line 2 */
}

static void phase_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t w1 = (uint32_t)p[P_E0] & 7u, w2 = (uint32_t)p[P_E1], i;
    int32_t depth = (p[P_E2] << 8) + m->cutoff + mulq15(m->envq15, p[P_E3] * 256);
    uint32_t dcw, inc = m->inc, inc2;
    int32_t det = p[P_E4], line2 = det != 0 || p[P_E5], ring = p[P_E5], sub = p[P_E6] * 200;
    int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t tg0 = v->s[0], tg1 = v->s[1];                    /* WAVE / WAVE2 toggles */
    pd_t b1, b2;
    depth = clamp(depth + (m->shape - (64 << 8)), 0, 127 << 8);
    dcw = (uint32_t)depth * 65535u / (127u << 8);
    dcw = (dcw * 56000u) >> 16;                              /* the classic range of the bend */
    inc2 = pitch_inc(clamp(m->pitch16 + d16, 0, 2047));
    inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
    pd_setup(&b1, w1, dcw);
    pd_setup(&b2, w2 ? w2 - 1u : w1, dcw);                   /* WAVE2 (every other cycle) */
    for (i = 0; i < n; i++) {
        uint32_t old = ph0;
        int32_t s;
        s = pd_wave(tg0 ? &b2 : &b1, ph0 >> 16);
        ph0 += inc;
        if (ph0 < old)
            tg0 ^= 1;
        if (line2) {
            uint32_t old2 = ph1;
            int32_t s2 = pd_wave(tg1 ? &b2 : &b1, ph1 >> 16);
            ph1 += inc2;
            if (ph1 < old2)
                tg1 ^= 1;
            s = ring ? mulq15(s, s2) : (s + s2) >> 1;     /* both within +-32767 */
        }
        if (sub) {
            ph2 += inc >> 1;
            s += mulq15(osc_sine(ph2), sub);
        }
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = tg0;
    v->s[1] = tg1;
}

static const preset_t PHASE_PRESETS[] = {
    /* name, {WAVE, WAVE2, DCW, ENV, DTN, LINE, SUB, -}, {A D S R}, fenv, mono */
    {"CZ BASS", {5, 0, 36, 70, 0, 0, 72, 0}, {0, 64, 64, 22}, 0, 1, FX(5, 0, 0, 4), XP(P_GLIDE + 1, 36, P_TRANS + 1, -24)},
    {"SOFT KEYS", {3, 0, 40, 70, 0, 0, 0, 0}, {0, 80, 20, 50}, 0, 0, FX(0, 35, 14, 28)},
    {"CZ STRING", {0, 4, 50, 40, 12, 0, 0, 0}, {50, 90, 105, 75}, 0, 0, FX(0, 50, 20, 55)},
    {"RESO PLUCK", {5, 0, 60, 60, 0, 0, 0, 0}, {0, 70, 30, 60}, 0, 0, FX(0, 0, 30, 30)},
    {"CZ BRASS", {0, 0, 30, 90, 0, 0, 0, 0}, {8, 70, 90, 40}, 0, 0, FX(0, 20, 14, 26)},
    /* SLOOP 2.5 */
    {"CZ LEAD", {0, 0, 70, 60, 6, 0, 0, 0}, {2, 70, 110, 30}, 0, 1, FX(0, 10, 25, 25), XP(P_GLIDE + 1, 30)},
    {"CZ PAD", {1, 4, 45, 30, 14, 0, 0, 0}, {70, 90, 115, 85}, 0, 0, FX(0, 50, 20, 60)},
    {"CZ RESO", {6, 0, 80, 90, 0, 0, 0, 0}, {0, 70, 60, 40}, 0, 0, FX(0, 10, 25, 30)},
};

static const engine_t ENG_PHASE = {
    "PHASE", {"PHS", "LINE"},
    {
        {"WAVE", F_ENUM, 0, 7, 0, N_PD_WAVE, 0},
        {"WAVE2", F_ENUM, 0, 8, 0, N_PD_WAVE2, 0},
        {"DCW", F_PCT, 0, 127, 60, 0, 0},
        {"ENV", F_PCT, 0, 127, 64, 0, 0},
        {"DTN", F_INT, 0, 127, 0, 0, "ct"},
        {"LINE", F_ENUM, 0, 1, 0, N_PD_LINE, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    PHASE_PRESETS, sizeof(PHASE_PRESETS) / sizeof(PHASE_PRESETS[0]), 0, phase_note_on, phase_render,
    0x05DF, {P_E2, P_E3, P_E4, P_REL},
};
