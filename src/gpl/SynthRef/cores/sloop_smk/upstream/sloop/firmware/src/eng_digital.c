/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DIGITAL: four-operator FM. */
/* Four sine operators, eight classic 4-operator algorithms (ALG = P_E0, EDIT 1 KNOB 1),
 * op 4 with feedback, one modulation INDEX shaped by a modulator envelope.
 * Phase modulation wraps naturally in the 32-bit phase. */
static const char *const N_FMALG[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
static const char *const N_RATIO[] = {".5", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "14", "16"};
static const uint16_t RATIO_Q8[15] = {128, 256, 512, 768, 1024, 1280, 1536, 1792, 2048, 2304, 2560, 2816,
                                      3072, 3584, 4096};

static void digital_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[7] = 0;                 /* op 4 phase */
    v->s[4] = 1 << 24;           /* modulator envelope, Q24 */
    v->s[5] = v->s[6] = 0;       /* feedback history */
}

/* operator increment = carrier * ratio (Q8) without 32-bit overflow; kept below Nyquist */
static inline uint32_t fm_ratio_inc(uint32_t inc, uint32_t r)
{
    uint32_t lim = 0x73000000u / r;                      /* (inc >> 8) * r must stay < 0x73000000 */
    return (inc >> 8) > lim ? 0x73000000u : (inc >> 8) * r;
}

static inline uint32_t digital_mod(int32_t x, int32_t idx) { return (uint32_t)(x * idx) * 2065u; }

static void digital_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t alg = (uint32_t)p[P_E0] & 7u, i;
    uint32_t i1 = m->inc;
    uint32_t i2 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E1] % 15]), i3 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E2] % 15]);
    uint32_t i4 = fm_ratio_inc(m->inc, RATIO_Q8[p[P_E3] % 15]);
    int32_t me, idx, fb = p[P_E6], fb1, fb2;                 /* fb1, fb2: op 4 feedback history */
    uint32_t ph0, ph1, ph2, ph4;
    /* modulator envelope: decays (MODDEC) towards 25 %, per block */
    v->s[4] += mulq16((1 << 22) - v->s[4], ENV_EXP[p[P_E5] & 127]);
    me = v->s[4] >> 9;                                         /* Q15 */
    idx = (p[P_E4] * me) >> 15;                                 /* 0..127 */
    idx = clamp(idx + ((m->cutoff + m->shape - (64 << 8)) >> 8) + (v->vel - 96) / 4, 0, 127);
    ph0 = v->ph[0];                                             /* state in locals: out[] may alias v->s[] */
    ph1 = v->ph[1];
    ph2 = v->ph[2];
    ph4 = (uint32_t)v->s[7];
    fb1 = v->s[5];
    fb2 = v->s[6];
    for (i = 0; i < n; i++) {
        int32_t o1, o2, o3, o4, s;
        o4 = sine_i((ph4 + digital_mod((fb1 + fb2) >> 1, fb)));
        fb2 = fb1;
        fb1 = o4;
        switch (alg) {
        case 0:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i((ph1 + digital_mod(o3, idx)));
            s = sine_i((ph0 + digital_mod(o2, idx)));
            break;
        case 1:
            o3 = sine_i(ph2);
            o2 = sine_i((ph1 + digital_mod((o3 + o4) >> 1, idx)));
            s = sine_i((ph0 + digital_mod(o2, idx)));
            break;
        case 2:
            o3 = sine_i(ph2);
            o2 = sine_i((ph1 + digital_mod(o3, idx)));
            s = sine_i((ph0 + digital_mod((o2 + o4) >> 1, idx)));
            break;
        case 3:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            s = sine_i((ph0 + digital_mod((o2 + o3) >> 1, idx)));
            break;
        case 4:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            o1 = sine_i((ph0 + digital_mod(o2, idx)));
            s = (o1 + o3) >> 1;
            break;
        case 5:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i((ph1 + digital_mod(o4, idx)));
            o1 = sine_i((ph0 + digital_mod(o4, idx)));
            s = mulq15(o1 + o2 + o3, 10923);           /* / 3 without a divide */
            break;
        case 6:
            o3 = sine_i((ph2 + digital_mod(o4, idx)));
            o2 = sine_i(ph1);
            o1 = sine_i(ph0);
            s = mulq15(o1 + o2 + o3, 10923);           /* / 3 without a divide */
            break;
        default:
            o3 = sine_i(ph2);
            o2 = sine_i(ph1);
            o1 = sine_i(ph0);
            s = (o1 + o2 + o3 + o4) >> 2;
            break;
        }
        ph0 += i1;
        ph1 += i2;
        ph2 += i3;
        ph4 += i4;
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS);   /* full-scale sines: 6 dB below the others */
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[7] = (int32_t)ph4;
    v->s[5] = fb1;
    v->s[6] = fb2;
}

static const preset_t DIGITAL_PRESETS[] = {
    /* ALG R2 R3 R4 INDEX MODDEC FDBK - ; ALG 5 (4 here) = two stacks: 2 -> 1 and 4 -> 3 (the tine) */
    {"RHODES", {4, 1, 1, 14, 26, 35, 0, 0}, {0, 85, 35, 55}, 0, 0, FX(0, 30, 12, 26), XP(P_LD_AMP + 1, 26, P_LRATE + 1, 84)},
    {"DX RHODES", {4, 1, 1, 14, 85, 45, 0, 0}, {0, 82, 40, 55}, 0, 0, FX(0, 45, 20, 30)},
    {"WURLI", {4, 1, 2, 3, 62, 35, 8, 0}, {0, 75, 30, 45}, 0, 0, FX(10, 20, 10, 20), XP(P_LD_AMP + 1, 16, P_LRATE + 1, 89)},
    {"CLAV", {3, 1, 3, 5, 90, 25, 20, 0}, {0, 58, 34, 18}, 0, 0, FX(10, 10, 6, 10)},
    /* house piano: bright, fast hammer, the tine stack for the attack */
    {"M1 PIANO", {4, 1, 2, 13, 74, 30, 12, 0}, {0, 86, 30, 48}, 0, 0, FX(0, 24, 14, 26)},
    /* afro house / amapiano: round keys with a bell on top */
    {"AFRO KEYS", {4, 1, 1, 7, 40, 50, 0, 0}, {0, 82, 40, 60}, 0, 0, FX(0, 30, 26, 36)},
    {"TRAP BELL", {4, 8, 1, 4, 80, 70, 0, 0}, {0, 92, 0, 70}, 0, 0, FX(0, 0, 30, 40)},
    {"MUSIC BOX", {4, 4, 3, 9, 45, 75, 0, 0}, {0, 90, 0, 75}, 0, 0, FX(0, 10, 32, 45)},
    {"MARIMBA", {4, 4, 1, 1, 60, 30, 0, 0}, {0, 80, 0, 60}, 0, 0, FX(0, 0, 22, 30)},
    {"KALIMBA", {4, 4, 1, 6, 52, 24, 0, 0}, {0, 78, 0, 58}, 0, 0, FX(0, 0, 26, 38)},
    /* pluggnb / cloud rap: a soft bell, lots of space */
    {"PLUGG BELL", {4, 3, 1, 4, 56, 60, 0, 0}, {0, 90, 0, 82}, 0, 0, FX(0, 22, 36, 56)},
    {"GLASS PAD", {4, 2, 1, 5, 34, 100, 0, 0}, {62, 92, 112, 92}, 0, 0, FX(0, 50, 26, 62), XP(P_LD_PIT + 1, 1, P_LRATE + 1, 40)},
    {"FM BASS", {0, 1, 1, 1, 34, 40, 8, 0}, {0, 60, 70, 25}, 0, 1, FX(0, 0, 0, 6), XP(P_GLIDE + 1, 36, P_TRANS + 1, -24)},
    /* SLOOP 2.5: mallets, steel drum, harp, a finger bass */
    {"STEEL DRUM", {4, 2, 2, 2, 45, 45, 0, 0}, {0, 88, 0, 66}, 0, 0, FX(0, 12, 18, 30)},
    {"GLOCKEN", {4, 5, 1, 8, 40, 60, 0, 0}, {0, 95, 0, 85}, 0, 0, FX(0, 0, 24, 40), XP(P_TRANS + 1, 12)},
    {"CELESTA", {4, 2, 1, 4, 30, 50, 0, 0}, {0, 84, 0, 70}, 0, 0, FX(0, 15, 20, 40), XP(P_TRANS + 1, 12)},
    {"XYLOPHONE", {4, 4, 3, 3, 70, 18, 0, 0}, {0, 77, 0, 58}, 0, 0, FX(0, 0, 14, 26), XP(P_TRANS + 1, 12)},
    {"HARP", {4, 1, 1, 2, 45, 30, 10, 0}, {0, 92, 0, 80}, 0, 0, FX(0, 15, 20, 50)},
    {"BASS GTR", {4, 1, 1, 2, 40, 30, 6, 0}, {0, 76, 40, 22}, 0, 1, FX(0, 0, 0, 6), XP(P_TRANS + 1, -24)},
    {"TUBE BELL", {4, 6, 1, 13, 50, 90, 0, 0}, {0, 100, 0, 90}, 0, 0, FX(0, 10, 25, 50)},
    {"FM PLUCK", {4, 2, 1, 3, 70, 15, 0, 0}, {0, 78, 0, 54}, 0, 0, FX(0, 10, 28, 30)},
    {"FM GRAND", {4, 1, 2, 12, 60, 40, 10, 0}, {0, 90, 20, 60}, 0, 0, FX(0, 20, 14, 30)},
};

static const engine_t ENG_DIGITAL = {
    "DIGITAL", {"OPS", "MOD"},
    {
        {"ALG", F_ENUM, 0, 7, 0, N_FMALG, 0},
        {"R2", F_ENUM, 0, 14, 1, N_RATIO, 0},
        {"R3", F_ENUM, 0, 14, 1, N_RATIO, 0},
        {"R4", F_ENUM, 0, 14, 1, N_RATIO, 0},
        {"IDX", F_PCT, 0, 127, 60, 0, 0},
        {"MDEC", F_TIME, 0, 127, 60, 0, 0},
        {"FB", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    DIGITAL_PRESETS, sizeof(DIGITAL_PRESETS) / sizeof(DIGITAL_PRESETS[0]), -1, digital_note_on, digital_render,
    0xFD20, {P_E4, P_E5, P_E6, P_REL},
};
