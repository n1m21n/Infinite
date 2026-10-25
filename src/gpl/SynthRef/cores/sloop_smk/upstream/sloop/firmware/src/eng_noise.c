/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* NOISE: from analog-style noise to shift-register noise, through a resonant filter.
 *
 * MODE picks the source:
 *   ANLG  xorshift white noise, plus random impulses (DENS: their density, 0 = none)
 *   DUST  the impulses only: crackle, drops (through a resonant filter)
 *   LFSR  a long shift register (LEN: 23 down to 7 bits) clocked CLK semitones above the key
 *   META  one cycle of a short register sequence per period of the key (LEN: which one): a metallic tone
 * COLR tilts the source from white (0) through pink (64, -3 dB/oct) to brown (127, -6 dB/oct).
 * FREQ / RES: a trapezoidal SVF that morphs from low-pass (RES 0) to a narrow band-pass (RES 127);
 * TRK key-tracks FREQ (127 = one semitone a semitone, around C4). DRFT: a slow random wander of FREQ
 * (and the clock), the level following it. CRSH (ANLG, DUST): sample-and-hold and fewer bits.
 * The track's FLT moves FREQ, SHP moves COLR, PIT the key (so FREQ with TRK, and the clock).
 *
 * State: s[0] s[1] the filter, s[2] the random generator (seeded by the note and the unison detune: a
 * note from silence sounds the same each time), s[3] the drift target and its countdown, s[4..6] the pink
 * poles, s[7] the brown pole; ph[0] the clock phase (LFSR, META) or the hold count (CRSH), ph[1] the
 * register, the held sample or META's DC blocker, ph[2] the drift. */
static const char *const N_NOISE_MODE[] = {"ANLG", "DUST", "LFSR", "META"};
enum { NZ_ANLG, NZ_DUST, NZ_LFSR, NZ_META };
static const char *const N_NOISE_LONG[] = {"L23", "L20", "L17", "L15", "L13", "L11", "L9", "L7", 0};
static const char *const N_NOISE_META[] = {"127", "127B", "93", "63", "63B", "31", "15", "7", 0};
static const param_desc_t NOISE_LONG = {"LEN", F_INT, 0, 127, 0, N_NOISE_LONG, 0};   /* = edit[5] but the names */
static const param_desc_t NOISE_META = {"LEN", F_INT, 0, 127, 0, N_NOISE_META, 0};
static const param_desc_t NOISE_CLK = {"CLK", F_INT, 0, 127, 0, 0, "st"};           /* = edit[7]: above the key */

/* Galois taps of the long registers (maximal length: 2^n - 1 clocks) */
static const uint32_t NOISE_TAPS[8] = {0x420000u, 0x90000u, 1u << 16 | 1u << 13, 0x6000u, 0x100Du, 0x500u, 0x110u, 0x60u};

/* META: one period of a register's output bit (bit i of the words, from the state 1), its length,
 * and the levels of a 1 and a 0 (no DC, the larger at 16384). 7-bit taps 0x60 and 0x78, the 15-bit
 * register with its feedback from bits 0 and 6 (period 93), 6-bit 0x30 and 0x2D, 5-bit, 4-bit, 3-bit */
static const struct { uint32_t w[4]; uint8_t len; int16_t hi, lo; } NOISE_SEQ[8] = {
    {{0xD13C50C1u, 0x91C2F95Cu, 0xA58DED6Cu, 0x01FD533Bu}, 127, 16128, -16384},
    {{0xDBD165F1u, 0x9E46A1FDu, 0x84C15692u, 0x019B1CEBu}, 127, 16128, -16384},
    {{0x41008001u, 0x00492402u, 0x00824824u, 0x00000000u}, 93, 16384, -3404},
    {{0x27179461u, 0x03F566EDu, 0x00000000u, 0x00000000u}, 63, 15872, -16384},
    {{0x75A6C487u, 0x02FCA8CFu, 0x00000000u, 0x00000000u}, 63, 15872, -16384},
    {{0x05763E69u, 0x00000000u, 0x00000000u, 0x00000000u}, 31, 15360, -16384},
    {{0x00000F59u, 0x00000000u, 0x00000000u, 0x00000000u}, 15, 14336, -16384},
    {{0x0000001Du, 0x00000000u, 0x00000000u, 0x00000000u}, 7, 12288, -16384},
};

static const param_desc_t *noise_desc(const track_t *t, uint32_t k)
{
    int32_t mode = t->p[P_E0];
    if (k == 5u && mode >= NZ_LFSR)
        return mode == NZ_LFSR ? &NOISE_LONG : &NOISE_META;
    if (k == 7u && mode >= NZ_LFSR)
        return &NOISE_CLK;
    return 0;
}

/* phase increment of pitch p (1/16 semitone) past the table's top, up to one step a sample */
static uint32_t noise_inc(int32_t p)
{
    uint32_t sh = 0, inc;
    while (p > 2047) {
        p -= 192;
        sh++;
    }
    inc = pitch_inc((uint32_t)clamp(p, 0, 2047));
    return sh && inc >= 0x80000000u >> (sh - 1u) ? 0xFFFFFFFFu : inc << sh;
}

static uint32_t isqrt32(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

static void noise_note_on(track_t *t, voice_t *v)
{
    uint32_t seed = (v->note + 1u) * 0x9E3779B9u ^ (uint32_t)v->fine * 0x85EBCA6Bu;   /* UNISON: each its own */
    (void)t;
    v->s[0] = v->s[1] = 0;
    v->s[2] = (int32_t)(seed ? seed : 1u);
    v->s[3] = 0;                                      /* a new drift target at once */
    v->s[4] = v->s[5] = v->s[6] = v->s[7] = 0;
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->ph[2] = 0;
}

static void noise_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t mode = (uint32_t)p[P_E0] & 3u, sel = ((uint32_t)p[P_E5] >> 4) & 7u, i;
    int32_t res = p[P_E3], drft = p[P_E6], crsh = mode < NZ_LFSR ? p[P_E7] : 0;
    int32_t ic1 = v->s[0], ic2 = v->s[1], rng = v->s[2], b0 = v->s[4], b1 = v->s[5], b2 = v->s[6], br = v->s[7];
    uint32_t ph = v->ph[0], reg = v->ph[1];         /* state in locals: out[] may alias v->s[] */
    int32_t drift = (int32_t)v->ph[2], d16 = 0, gain = 32767, gq, colr, cut, k, a1, a2, a3, g, bpg, mix, cf, thr = 0;
    uint32_t inc = 0, taps = NOISE_TAPS[sel];
    /* drift: a new random target every 0.7..3 s, followed with a 0.7 s lag */
    if (drft) {
        int32_t tg = v->s[3] >> 16, cnt = v->s[3] & 0xFFFF;
        if (--cnt <= 0) {
            uint32_t r = noise32(&rng);
            tg = (int32_t)(r >> 16) - 32768;
            cnt = 1024 + (int32_t)(r & 3071u);
        }
        v->s[3] = (int32_t)((uint32_t)tg << 16 | (uint32_t)cnt);
        drift += ((tg << 4) - drift) >> 10;           /* Q19 */
        d16 = ((drift >> 11) * drft * drft) >> 14;    /* up to about +-16 semitones */
        gain = 32767 - ((((32767 - (drift >> 4)) >> 1) * drft) >> 8);   /* down to -6 dB as it falls */
    }
    /* COLR (+ SHP), 0..127 << 8: white -> pink -> brown */
    colr = clamp((p[P_E1] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    cf = colr <= (64 << 8) ? colr << 1 : ((colr - (64 << 8)) * 520) >> 8;   /* the crossfade, Q15 */
    /* FREQ (+ FLT), key-tracked by TRK around C4, the drift: SVF cutoff units (256 an index, 0.86 st) */
    cut = (p[P_E2] << 8) + m->cutoff + (((m->pitch16 - 60 * 16) * p[P_E4] * 151) >> 10) + ((d16 * 4785) >> 8);
    cut = clamp(cut, 0, 127 << 8);
    g = SVF_G[cut >> 8];
    if (cut < (127 << 8))
        g += ((SVF_G[(cut >> 8) + 1] - g) * (cut & 255)) >> 8;
    k = 8192 - res * 7800 / 127;                      /* damping, Q12: 2.0 .. 0.1 */
    a1 = (int32_t)((4096u << 13) / (uint32_t)(4096 + ((g * (g + k)) >> 12)));
    a2 = (a1 * g) >> 12;
    a3 = (a2 * g) >> 12;
    bpg = (int32_t)isqrt32((uint32_t)k << 12);        /* band-pass x 1.5 sqrt(damping): its level holds with the Q */
    mix = res * 258;                                  /* low-pass -> band-pass, Q15 */
    {   /* level: make up most of what the filter takes from white noise (less for pink and brown), +12 dB for
         * the sparse DUST, the drift's level; Q12, mk in 1/256 octave: 2^(mk / 256), at most x 16 */
        int32_t wf = 32767 - ((colr * 230) >> 8), mk = (((((127 << 8) - cut) >> 8) * 7) * wf) >> 15;
        mk += 214 + (mode == NZ_DUST ? 512 : 0);
        if (mk > 1279)
            mk = 1279;
        gq = (4096 + ((mk & 255) << 4)) << (mk >> 8);
        gq = (gq * (gain >> 3)) >> 12;
        if (gq > 65535)
            gq = 65535;
    }
    if (mode <= NZ_DUST && p[P_E5]) {                 /* impulses: 1 .. 2000 a second */
        uint32_t e = (uint32_t)p[P_E5] * 22u, r = 1u << (e >> 8);   /* 2^(DENS * 11 / 127), Q8 fraction */
        r += (r * (e & 255u)) >> 8;
        thr = (int32_t)((r * 97391u) >> 16);          /* per 2^16 of a sample: rate * 65536 / FS, Q16 */
    }
    if (mode == NZ_LFSR) {
        uint32_t mask = taps;                         /* the register's own bits (MODE / LEN changes) */
        inc = noise_inc(m->pitch16 + p[P_E7] * 16 + d16);
        mask |= mask >> 1;
        mask |= mask >> 2;
        mask |= mask >> 4;
        mask |= mask >> 8;
        mask |= mask >> 16;
        if (!(reg & mask))                            /* a new note: the register from the note's seed */
            reg = (uint32_t)rng | 1u;
        reg &= mask;
    } else if (mode == NZ_META) {
        inc = p[P_E7] || d16 ? noise_inc(m->pitch16 + p[P_E7] * 16 + d16) : m->inc;
    }
    for (i = 0; i < n; i++) {
        int32_t w, x, v1, v2, v3, y;
        switch (mode) {
        case NZ_DUST:
        case NZ_ANLG: {
            uint32_t r = noise32(&rng);
            w = mode == NZ_ANLG ? (int32_t)(r >> 17) - 16384 : 0;
            if ((int32_t)(r & 0xFFFFu) < thr) {       /* an impulse, random size and sign */
                int32_t a = 8192 + (int32_t)((r >> 18) & 0x7FFFu);
                w += (r & 0x20000u) ? a : -a;
            }
            break;
        }
        case NZ_LFSR: {
            uint32_t o = ph;
            ph += inc;
            if (ph < o)
                reg = (reg >> 1) ^ (-(reg & 1u) & taps);
            w = (reg & 1u) ? 12000 : -12000;
            break;
        }
        default: {
            uint32_t j = ((ph >> 16) * NOISE_SEQ[sel].len) >> 16;
            ph += inc;
            w = (NOISE_SEQ[sel].w[j >> 5] >> (j & 31u)) & 1u ? NOISE_SEQ[sel].hi : NOISE_SEQ[sel].lo;
            break;
        }
        }
        x = w;
        if (colr) {                                   /* pink: three leaky poles (Kellet), brown: a leaky integrator */
            int32_t pk;
            b0 = ((b0 * 32690) >> 15) + ((w * 3245) >> 15);
            b1 = ((b1 * 31555) >> 15) + ((w * 9716) >> 15);
            b2 = ((b2 * 18678) >> 15) + ((w * 17247) >> 14);
            pk = (b0 + b1 + b2 + ((w * 6055) >> 15)) >> 2;
            if (colr <= (64 << 8)) {
                x = w + mulq15(pk - w, cf);
            } else {
                br = ((br * 32580) >> 15) + ((w * 3480) >> 15);
                x = pk + mulq15(br - pk, cf);
            }
        }
        /* the SVF (dsp.c tsvf_lp, with the band-pass), rounded: at low cutoffs the floor of the shifts
         * walked the integrators off zero (DC) */
        v3 = x - ic2;
        v1 = (a1 * ic1 + a2 * v3 + 4096) >> 13;
        v2 = ic2 + ((a2 * ic1 + a3 * v3 + 4096) >> 13);
        ic1 = clamp(2 * v1 - ic1, -150000, 150000);
        ic2 = clamp(2 * v2 - ic2, -150000, 150000);
        v1 = (v1 * bpg * 3) >> 13;
        y = v2 + (((v1 - v2) * (mix >> 3)) >> 12);
        y = soft_knee(((y >> 4) * gq) >> 8, 24000);
        if (mode == NZ_META) {                        /* the knee bends a lopsided tone: a DC blocker, ~14 Hz */
            reg += (uint32_t)(((y << 8) - (int32_t)reg) >> 9);
            y -= (int32_t)reg >> 8;
        }
        if (crsh) {                                   /* hold 1..16 samples, 16 .. 4 bits */
            if ((int32_t)--ph <= 0 || ph > 16u) {
                ph = 1u + (uint32_t)crsh / 8u;
                reg = (uint32_t)(y >> (crsh / 11) << (crsh / 11));
            }
            y = (int32_t)reg;
        }
        out[i] += voice_amp(y, m, i);
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = rng;
    v->s[4] = b0;
    v->s[5] = b1;
    v->s[6] = b2;
    v->s[7] = br;
    v->ph[0] = ph;
    v->ph[1] = reg;
    v->ph[2] = (uint32_t)drift;
}

/* {MODE, COLR, FREQ, RES, TRK, DENS, DRFT, CRSH / CLK} */
static const preset_t NOISE_PRESETS[] = {
    {"WIND", {NZ_ANLG, 64, 70, 100, 127, 0, 70, 0}, {90, 90, 110, 90}, 0, 0, FX(0, 30, 20, 80)},
    {"RAIN", {NZ_DUST, 30, 88, 80, 64, 105, 40, 0}, {10, 90, 127, 70}, 0, 0, FX(0, 0, 20, 70)},
    /* a 7-bit register (127 steps) clocked 84 semitones (7 octaves) above the key: one period per key period,
     * a pitched chip-style buzz */
    {"NZ ARCADE", {NZ_LFSR, 0, 127, 0, 127, 112, 0, 84}, {0, 80, 0, 50}, 0, 0, FX(0, 0, 10, 20)},
    {"NZ METAL", {NZ_META, 0, 100, 40, 100, 32, 0, 0}, {0, 75, 40, 60}, 10, 0, FX(0, 20, 30, 45)},
    /* SLOOP 2.5 */
    {"OCEAN", {NZ_ANLG, 100, 50, 20, 0, 0, 110, 0}, {100, 90, 127, 100}, 0, 0, FX(0, 20, 0, 70)},
    {"VINYL", {NZ_DUST, 0, 110, 10, 0, 70, 20, 0}, {0, 64, 127, 40}, 0, 0, FX(0, 0, 0, 10)},
    {"HISS", {NZ_ANLG, 0, 115, 0, 0, 0, 0, 0}, {20, 64, 127, 40}, 0, 0, FX(0, 0, 0, 20)},
    {"RISER", {NZ_ANLG, 30, 60, 110, 127, 0, 0, 0}, {100, 90, 127, 60}, 0, 0, FX(0, 20, 30, 60)},
    {"NZ SNARE", {NZ_ANLG, 20, 90, 30, 64, 0, 0, 0}, {0, 60, 0, 44}, 0, 0, FX(0, 0, 0, 25)},
    {"BITCRUSH", {NZ_ANLG, 40, 90, 40, 64, 0, 0, 100}, {0, 70, 110, 40}, 0, 0, FX(0, 0, 20, 30)},
    {"RADIO", {NZ_LFSR, 20, 70, 90, 64, 0, 60, 48}, {10, 70, 120, 50}, 0, 0, FX(0, 10, 30, 40)},
};

static const engine_t ENG_NOISE = {
    .name = "NOISE",
    .page_title = {"SRC", "MOVE"},
    .edit = {
        {"MODE", F_ENUM, 0, 3, 0, N_NOISE_MODE, 0},
        {"COLR", F_PCT, 0, 127, 64, 0, 0},
        {"FREQ", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"TRK", F_PCT, 0, 127, 64, 0, 0},
        {"DENS", F_PCT, 0, 127, 0, 0, 0},           /* LFSR, META: the register (noise_desc) */
        {"DRFT", F_PCT, 0, 127, 0, 0, 0},
        {"CRSH", F_PCT, 0, 127, 0, 0, 0},           /* LFSR, META: the clock above the key */
    },
    .presets = NOISE_PRESETS,
    .npresets = NELEM(NOISE_PRESETS),
    .note_on = noise_note_on,
    .render = noise_render,
    .fil_page = 0,
    .color = 0xC618,
    .macro = {P_E2, P_E3, P_E1, P_REL},
    .desc = noise_desc,
};
