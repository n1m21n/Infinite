/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020 Electrosmith, Corp, Emilie Gillet
 * Ported to fixed-point C for Felucca, 2026: Leo Kuroshita (@kurogedelic), Hügelton Instruments
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/* Physical models of DaisySP (Source/PhysicalModeling, Filters, Noise, Utility; MIT) in fixed
 * point for a CPU without an FPU. The PHYS engine (eng_phys.c) plays them.
 *
 *   modal   Resonator + ResonatorSvf (a bank of band-pass SVF modes, stretched by STRUCTURE,
 *           their Q by DAMPING and BRIGHTNESS) excited by ModalVoice: a strike (an impulse into a
 *           low-pass SVF) or, sustained, Dust through it
 *   memb    the same resonator and strike with the modes of a circular membrane (Felucca's)
 *   string  String (the extended Karplus-Strong string of DaisySP's KarplusString: delay line,
 *           dispersion all-pass or curved bridge, DcBlock, OnePole damping, the low-note
 *           resampler) excited by StringVoice: a noise burst one period long (or Dust) through Svf
 * (DaisySP's Particle was ported too until the DUST model was dropped.)
 *
 * Number formats (no float anywhere):
 *   signals         Q20 (1.0 = 2^20) between the stages; the exciter of the modal bank in Q24, the
 *                   modes in Q22 (states bounded at +-64 per block: every 32-bit sum fits)
 *   frequencies     cycles per sample, Q32 (0.5 = 2^31)
 *   SVF (Simper)    g = tan(pi f) Q28 (up to ~4.6 at 0.499), r + g Q28, g h Q30,
 *                   h = 1 / (1 + r g + g^2) rounded down and r = 1 / q rounded up: the quantised
 *                   filter never has less damping than the exact one (no growth at the highest Q)
 *   products        32 x 32 -> 64-bit multiply (one MAC on the pi32v2), divisions 64 / 32 and only
 *                   per block (a hit of Dust: a reciprocal per block)
 * The SVF mode is written with g h folded into one coefficient: t = g h (x - (r + g) s1 - s2),
 * bp = s1 + t, s1 = bp + t, lp = s2 + g bp, s2 = lp + g bp (the band-pass output of
 * ResonatorSvf::Process without the high-pass term), four multiplies per mode and sample. Its
 * rounding leaves a floor of ~1e-5 of full scale once a low mode has died away (the integrators of
 * a mode at g ~ 1e-3 stop moving for steps below 0.5 / g LSB): below one LSB of the 16-bit output.
 *
 * Coefficients are worked out once per block (eng_phys.c: 32 samples) instead of per sample.
 * Departures from the float code: modes at or above 0.499 cycles (clamped there by DaisySP, at an
 * amplitude of 0.002) are left out, and there are 12 modes instead of 24 (the device cost); the strike positions of
 * the modes follow cos(2 pi pos i), as DaisySP's header describes (its Init gives every mode
 * cos(2 pi pos)); STRUCTURE below 0.24 bends the string's bridge, as DaisySP's header describes
 * (its SetNonLinearity clamps to 0..1, so its curved bridge is never reached); the string line is
 * 512 samples instead of 1024, so the string runs resampled below 88 Hz instead of 43 Hz, with its
 * loop filter set per string step (DaisySP's leaves it per output sample: flat by up to 60 cents
 * at C1 here) and the excitation between two string steps summed instead of dropped; the pluck can
 * have a position (a comb on the burst; 0 = DaisySP's plain burst); the exciters stop once rung out; DcBlock has error feedback (no DC from the rounding);
 * the random numbers are xorshift instead of rand(). phys_ref.cpp (host tests) compares the models with
 * the float originals (mode frequencies, decays, levels). */

#define PX_NMODE 12              /* modes of the resonator (DaisySP: 24; the cost is per mode) */
#define PX_LINE 512              /* string delay line, samples (DaisySP: 1024); below its lowest */
#define PX_LMASK (PX_LINE - 1)   /* pitch (88 Hz) the string runs resampled */
#define PX_SLINE 128             /* dispersion all-pass line (a quarter, as DaisySP) */
#define PX_SMASK (PX_SLINE - 1)
#define PX_ONE (1 << 20)         /* signal 1.0 */

/* ---------------------------------------------------------------- math --- */
static inline int32_t px_m(int32_t a, int32_t b, int sh) { return (int32_t)(((int64_t)a * b) >> sh); }
static inline int32_t px_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline uint32_t px_rand(uint32_t *s)              /* xorshift32 */
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* sin(2 pi ph), ph a turn in Q32; Q30 (odd polynomial of the quarter wave, error ~4e-6) */
static int32_t px_sin(uint32_t ph)
{
    uint32_t q = ph >> 30, r = ph & 0x3FFFFFFFu;
    int32_t t, t2, p;
    if (q & 1u)
        r = 0x40000000u - r;
    t = (int32_t)r;
    t2 = px_m(t, t, 30);
    p = 172272;
    p = 5026995 - px_m(t2, p, 30);
    p = 85569306 - px_m(t2, p, 30);
    p = 693598668 - px_m(t2, p, 30);
    p = 1686629713 - px_m(t2, p, 30);
    p = px_m(t, p, 30);
    return q & 2u ? -p : p;
}

/* 2^x, x Q16 (-16 <= x < 15); Q16 */
static uint32_t px_exp2(int32_t x)
{
    int32_t i = x >> 16;
    int64_t f = (int64_t)(x & 0xFFFF) << 14, p = 16377;   /* Q30 */
    if (i < -16)
        return 0;
    if (i > 14)
        return 0xFFFFFFFFu;
    p = 165394 + ((f * p) >> 30);
    p = 1431680 + ((f * p) >> 30);
    p = 10327387 + ((f * p) >> 30);
    p = 59597083 + ((f * p) >> 30);
    p = 257941248 + ((f * p) >> 30);
    p = 744261118 + ((f * p) >> 30);
    p = (1 << 30) + ((f * p) >> 30);                     /* 2^frac, Q30 */
    return i >= -14 ? (uint32_t)(p >> (14 - i)) : (uint32_t)(p >> 28) >> (-14 - i);
}

/* DaisySP's fasttan of ResonatorSvf: tan(pi f) ~ f (pi + f^2 (a + b f^2)); f Q32 (< 0.5), Q28 */
static int32_t px_fasttan(uint32_t f)
{
    int64_t x = f >> 1, x2 = (x * x) >> 31;              /* Q31 */
    int64_t p = 169584874 + ((935957068 * x2) >> 31);    /* a + b f^2, Q24 */
    p = 52707179 + ((x2 * p) >> 31);                     /* pi + ..., Q24 */
    return (int32_t)((x * p) >> 27);
}

/* atan(x), 0 <= x <= 1, Q30 in and out (odd polynomial, error ~1e-5) */
static int32_t px_atan(int32_t x)
{
    int32_t x2 = px_m(x, x, 30), p = 12585543;
    p = 56536072 - px_m(x2, p, 30);
    p = 125018842 - px_m(x2, p, 30);
    p = 207815708 - px_m(x2, p, 30);
    p = 357151731 - px_m(x2, p, 30);
    p = 1073717407 - px_m(x2, p, 30);
    return px_m(x, p, 30);
}

static uint32_t px_isqrt64(uint64_t v)                    /* floor(sqrt(v)) */
{
    uint64_t r = 0, b = (uint64_t)1 << 62;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return (uint32_t)r;
}

/* --------------------------------------------- ResonatorSvf (band / low) --- */
typedef struct { int32_t g, rpg, gh; } px_svf_t;         /* Q28, Q28, Q30 */

/* f: cycles / sample Q32 (< 0.5), q: Q8 (> 0) */
static void px_svf_coef(px_svf_t *c, uint32_t f, uint32_t q8)
{
    int32_t g = px_fasttan(f);
    uint32_t r = (uint32_t)((((uint64_t)1 << 36) + q8 - 1u) / q8);       /* 1 / q, rounded up */
    uint64_t den = ((uint64_t)1 << 28) + (((uint64_t)r * (uint32_t)g) >> 28) + (((uint64_t)g * (uint32_t)g) >> 28);
    uint32_t d24 = (uint32_t)((den + 15u) >> 4);         /* Q24, rounded up */
    uint32_t h = (uint32_t)(((uint64_t)1 << 54) / d24);  /* Q30, rounded down */
    c->g = g;
    c->rpg = (int32_t)(r + (uint32_t)g);
    c->gh = (int32_t)(((uint64_t)h * (uint32_t)g) >> 28);
}

/* --------------------------------------------- Svf (double-sampled, Q20) --- */
typedef struct { int32_t freq, damp, drive; } px_csvf_t; /* Q30, Q28, Q30 */
typedef struct { int32_t low, band; } px_csvf_st_t;

/* Svf::SetFreq(f * sr) + SetRes(res) + the drive: f cycles / sample Q32, res Q16 (0..1), drive Q16 */
static void px_csvf_coef(px_csvf_t *c, uint32_t f, int32_t res, int32_t pre_drive)
{
    uint32_t fc = f > 0x55555555u ? 0x55555555u : f;     /* fc_max = sr / 3 */
    uint32_t a = fc >> 1 > 0x40000000u ? 0x40000000u : fc >> 1;   /* min(0.25, fc / (2 sr)) */
    int32_t fr = px_sin(a >> 1) << 1;                    /* 2 sin(pi a), Q30 */
    int32_t r4 = 0, d1, d2;
    if (res > 0) {                                       /* res^0.25 = sqrt(sqrt(res)) */
        r4 = (int32_t)px_isqrt64((uint64_t)res << 16);   /* Q16 */
        r4 = (int32_t)px_isqrt64((uint64_t)r4 << 16);
    }
    d1 = (65536 - r4) << 13;                             /* 2 (1 - res^0.25), Q28 */
    if (fr > 0) {                                        /* min(2, 2 / freq - freq / 2) */
        uint64_t inv = ((uint64_t)1 << 59) / (uint32_t)fr;
        d2 = inv > (1u << 30) ? 2 << 28 : (int32_t)inv - (fr >> 3);
        if (d2 > 2 << 28)
            d2 = 2 << 28;
    } else {
        d2 = 2 << 28;
    }
    c->freq = fr;
    c->damp = d1 < d2 ? d1 : d2;
    c->drive = (int32_t)(((int64_t)pre_drive * res) >> 2);   /* Q16 * Q16 -> Q30 */
}

/* one pass of the double-sampled SVF; returns band */
static inline void px_csvf_pass(const px_csvf_t *c, px_csvf_st_t *s, int32_t in)
{
    int32_t b = s->band, notch = in - px_m(c->damp, b, 28), high, b3;
    s->low += px_m(c->freq, b, 30);
    high = notch - s->low;
    b3 = (int32_t)(((int64_t)px_m(b, b, 20) * b) >> 20);
    s->band = b + px_m(c->freq, high, 30) - px_m(c->drive, b3, 30);
}

/* ------------------------------------------------------------- Modal --- */
typedef struct {
    int32_t s[PX_NMODE][2];                              /* mode states (s1, s2), Q22 */
    int32_t e1, e2;                                      /* exciter (ResonatorSvf<1>, low-pass), Q24 */
    uint32_t rng;
    int32_t benv;                                        /* MEMB: the pitch bend after a strike, Q16 (1 at it) */
    uint8_t trig;
} px_modal_t;

typedef struct { int32_t g, rpg, gh, a; } px_mode_t;    /* px_svf_t + the mode's gain (Q28) */
typedef struct {                                         /* per block */
    px_mode_t m[PX_NMODE];
    uint32_t n;                                          /* modes below 0.499 */
    px_svf_t ex;                                         /* exciter low-pass */
    int32_t strike;                                      /* strike impulse, Q16 (0 = sustained) */
    uint32_t dust_thr;                                   /* Dust: hit when rand < thr (Q32) */
    uint32_t dust_inv;                                   /* 2^47 / thr: a hit u / thr = u inv >> 32, Q15 */
    int32_t dust_gain;                                   /* (4 - 3 dust_f) accent bow, Q16 */
    int32_t dust_dc;                                     /* its mean, Q20 (taken off the aux) */
} px_modal_blk_t;

/* Resonator::CalcStiff, Q16 in and out */
static int32_t px_stiff(int32_t s)
{
    if (s < 16384)
        return -((16384 - s) >> 2);
    if (s < 19661)
        return 0;
    if (s < 58982)
        return (int32_t)(((int64_t)(s - 19661) * 109227) >> 16);   /* / 0.6 */
    s = (s - 58982) * 10;                                /* (s - 0.9) / 0.1 */
    s = px_m(s, s, 16);
    return 98304 - (px_sin(((uint32_t)s << 15) + 0x40000000u) >> 15);   /* 1.5 - cos(pi s) / 2 */
}

/* ModalVoice::Process, the exciter's per-block part (MODAL and MEMB): its low-pass, the strike or,
 * bowed, Dust. f0c: the note (cycles Q32, <= 0.25); b, d: brightness and damping with the accent */
static __attribute__((noinline)) void px_modal_exciter(px_modal_blk_t *B, px_modal_t *M, uint32_t f0c, int32_t brightness,
                                                       int32_t b, int32_t d, int32_t accent, int32_t bow)
{
    uint32_t cut;
    uint64_t fr;
    int32_t ex;
    /* exciter low-pass: cutoff = min(f 2^((b (2 - b) - 0.5) range / 12), 0.499) */
    ex = (int32_t)(((int64_t)px_m(b, 131072 - b, 16) - 32768) * (bow ? 36 : 60) / 12);   /* octaves, Q16 */
    fr = ((uint64_t)f0c * (bow ? 4u : 2u) * px_exp2(ex)) >> 16;
    cut = fr > 2143260078u ? 2143260078u : fr < 65536u ? 65536u : (uint32_t)fr;
    px_svf_coef(&B->ex, cut, bow ? 179u : 384u);         /* q 0.7 / 1.5 */
    B->strike = 0;
    B->dust_thr = 0;
    if (bow) {                                           /* Dust: density 0.3 (5e-5 + (1 - 5e-5) b^4) */
        int32_t dn = px_m(brightness, brightness, 16), df;
        dn = px_m(dn, dn, 16);
        df = 3 + px_m(dn, 65533, 16);
        B->dust_thr = (uint32_t)df * 19661u;             /* 0.3 df, Q32 */
        B->dust_inv = (uint32_t)(((uint64_t)1 << 47) / B->dust_thr);
        B->dust_gain = px_m(px_m(px_m(262144 - 3 * df, accent, 16), bow, 16), (int32_t)px_exp2(-px_m(d, 217579, 16)), 16);
        B->dust_dc = (int32_t)(((uint64_t)B->dust_thr * (uint32_t)B->dust_gain) >> 29);
    } else if (M->trig) {                                /* the strike: amplitude 2^(2 cut^2) / cut */
        int32_t amp = px_m(7864 + px_m(5243, accent, 16), 65536 - (d >> 1), 16);   /* (0.12 + 0.08 a)(1 - d / 2) */
        uint32_t c2 = (uint32_t)(((uint64_t)cut * cut) >> 32);
        uint64_t v = ((uint64_t)(uint32_t)amp * px_exp2((int32_t)(c2 >> 15))) << 16;   /* Q48 */
        v /= cut;                                        /* Q16 */
        B->strike = v > 0x7FFFFFFFu ? 0x7FFFFFFF : (int32_t)v;
        M->trig = 0;
    }
}

/* the Q of the lowest mode (Q8) and its factor from one mode to the next (*ql, Q16): DaisySP's Resonator,
 * 500 2^(d 79.7 / 6) and b (2 - b) 0.85 + 0.15 of the brightness STRUCTURE and DAMPING leave */
static uint64_t px_modal_q(int32_t structure, int32_t b, int32_t d, int32_t *ql)
{
    int32_t bb = px_m(px_m(b, 65536 - px_m(structure, 19661, 16), 16), 65536 - px_m(d, 19661, 16), 16);
    *ql = px_m(px_m(bb, 131072 - bb, 16), 55706, 16) + 9830;
    return ((uint64_t)500 * px_exp2((int32_t)(((int64_t)d * 870537) >> 16))) >> 8;
}

/* ModalVoice::Process + Resonator::Process, the per-block part. f0: the note (cycles Q32);
 * structure, brightness, damping, accent, pos: Q16 0..1; bow: Q16, 0 = struck (ModalVoice
 * sustain off), > 0 = sustained by Dust at that level (sustain on) */
static __attribute__((noinline)) void px_modal_block(px_modal_blk_t *B, px_modal_t *M, uint32_t f0, int32_t structure, int32_t brightness,
                           int32_t damping, int32_t accent, int32_t pos, int32_t bow)
{
    int32_t b = brightness + px_m(accent >> 2, 65536 - brightness, 16);
    int32_t d = damping + px_m(accent >> 2, 65536 - damping, 16);
    int32_t stiff = px_stiff(structure), st2, stretch = 65536, ql;
    uint32_t i, phs;
    uint64_t q8, harm;
    px_modal_exciter(B, M, f0 > 0x40000000u ? 0x40000000u : f0, brightness, b, d, accent, bow);
    /* the modes */
    f0 = (uint32_t)(((uint64_t)f0 * (uint32_t)(((uint64_t)1 << 32) /   /* NthHarmonicCompensation(3) */
                     (uint32_t)(65536 + stiff + px_m(stiff, stiff < 0 ? 60948 : 64225, 16)))) >> 16);
    q8 = px_modal_q(structure, b, d, &ql);
    st2 = stiff;
    harm = f0;
    phs = 0;
    for (i = 0; i < PX_NMODE; i++) {
        uint64_t mf = (harm * (uint32_t)stretch) >> 16;
        int32_t att, amp;
        if (mf >= 2143260078u)
            break;
        att = 65536 - (int32_t)(mf >> 15);               /* 1 - 2 f */
        amp = px_m(px_sin(phs + 0x40000000u), 16384, 16);   /* cos(2 pi pos i) / 4, Q30 * Q16 -> Q30 */
        {
            px_svf_t c;
            px_svf_coef(&c, (uint32_t)mf, (uint32_t)(256u + ((mf * q8) >> 32)));
            B->m[i].g = c.g;
            B->m[i].rpg = c.rpg;
            B->m[i].gh = c.gh;
            B->m[i].a = px_m(amp, att, 18);              /* Q28 */
        }
        stretch += st2;
        st2 = px_m(st2, st2 < 0 ? 60948 : 64225, 16);    /* * 0.93 / 0.98 */
        harm += f0;
        q8 = (q8 * (uint32_t)ql) >> 16;
        phs += (uint32_t)pos << 16;
    }
    B->n = i;
    for (; i < PX_NMODE; i++)                            /* out of the band: silent */
        M->s[i][0] = M->s[i][1] = 0;
    for (i = 0; i < B->n; i++) {                         /* bound the states at +-64 (far beyond any real use): */
        M->s[i][0] = px_clamp(M->s[i][0], -(1 << 28), 1 << 28);   /* every sum of the next block fits */
        M->s[i][1] = px_clamp(M->s[i][1], -(1 << 28), 1 << 28);   /* in 32 bits */
    }
}

/* ----------------------------------------------------------- Membrane --- */
/* MEMB (Felucca's, on the modal resonator above): the modes of a struck circular membrane instead of a
 * bar's. The ratios of the zeros of the Bessel functions J_m (an ideal head: inharmonic), blended by HARM
 * into those of a loaded head (a weighted centre patch, as a tabla's, pulls the first modes onto harmonics:
 * 1 2 3 3 4 4 5 5 5 6; Raman, 1934). The strike position goes from the centre, which sounds only the
 * circular modes (m = 0), to the rim, where the m > 0 modes are full and the fundamental is weak. After a
 * strike the pitch falls by BEND semitones (~60 ms: a tom's drop, a tabla's glide). 10 modes. */
#define PX_NMEMB 10
static const uint32_t PX_MEMB_IDEAL[PX_NMEMB] = {65536, 104421, 139955, 150432, 173871, 191188, 206797, 229386, 235830, 239039};
static const uint32_t PX_MEMB_LOADED[PX_NMEMB] = {65536, 131072, 196608, 197263, 262144, 261489, 327680, 328991, 326369, 393216};
static const uint8_t PX_MEMB_M[PX_NMEMB] = {0, 1, 2, 0, 3, 1, 4, 2, 0, 5};   /* (m, n): m nodal diameters */

/* as px_modal_block (struck only); harm, pos: Q16 0..1, bend: semitones Q16 */
static __attribute__((noinline)) void px_memb_block(px_modal_blk_t *B, px_modal_t *M, uint32_t f0, int32_t harm, int32_t brightness,
                                                    int32_t damping, int32_t accent, int32_t pos, int32_t bend)
{
    int32_t b = brightness + px_m(accent >> 2, 65536 - brightness, 16);
    int32_t d = damping + px_m(accent >> 2, 65536 - damping, 16);
    int32_t ql, w[6], pm = pos + (pos >> 1), inv;
    uint32_t i;
    uint64_t q8, f = f0;
    if (M->trig)
        M->benv = 65536;
    if (M->benv) {                                       /* the bend: f 2^(bend benv / 12), benv -> 0 in ~60 ms */
        f = (f * px_exp2(px_m(bend, M->benv, 16) / 12)) >> 16;
        M->benv = px_m(M->benv, 64220, 16);              /* 0.98 per block of 32 */
        if (M->benv < 64)
            M->benv = 0;
    }
    if (f > 0x40000000u)
        f = 0x40000000u;
    px_modal_exciter(B, M, (uint32_t)f, brightness, b, d, accent, 0);
    q8 = px_modal_q(0, b, d, &ql);
    w[0] = 65536 - px_m(pos, 39322, 16);                 /* m = 0: 1 at the centre, 0.4 at the rim */
    w[1] = pm > 65536 ? 65536 : pm;                      /* m > 0: (1.5 pos)^m, at most 1 */
    for (i = 2; i < 6u; i++)
        w[i] = px_m(w[i - 1], w[1], 16);
    for (i = 0; i < PX_NMEMB; i++) {
        uint32_t r = PX_MEMB_IDEAL[i] + (uint32_t)px_m((int32_t)(PX_MEMB_LOADED[i] - PX_MEMB_IDEAL[i]), harm, 16);
        uint64_t mf = (f * r) >> 16;
        px_svf_t c;
        if (mf >= 2143260078u)
            break;
        px_svf_coef(&c, (uint32_t)mf, (uint32_t)(256u + ((mf * q8) >> 32)));
        B->m[i].g = c.g;
        B->m[i].rpg = c.rpg;
        B->m[i].gh = c.gh;
        inv = (int32_t)(((uint64_t)1 << 32) / r);
        B->m[i].a = px_m(px_m(w[PX_MEMB_M[i]] << 14, 65536 - (int32_t)(mf >> 15), 18),   /* w (1 - 2 f) / ratio^2, */
                         px_m(inv, inv, 16), 16);        /* Q28: a mode's level from the strike rises as ratio^2 */
        q8 = (q8 * (uint32_t)ql) >> 16;
    }
    B->n = i;
    for (; i < PX_NMODE; i++)
        M->s[i][0] = M->s[i][1] = 0;
    for (i = 0; i < B->n; i++) {
        M->s[i][0] = px_clamp(M->s[i][0], -(1 << 28), 1 << 28);
        M->s[i][1] = px_clamp(M->s[i][1], -(1 << 28), 1 << 28);
    }
}

/* n (<= 32) samples: out[i] = the resonator (Q20), aux[i] = the exciter (Q20). The exciter first (skipped
 * once a strike has rung out), then the modes */
static void px_modal_run(const px_modal_blk_t *B, px_modal_t *M, int32_t *out, int32_t *aux, uint32_t n)
{
    uint32_t i, k, nm = B->n;
    int32_t x[32];
    if (n > 32u)
        n = 32u;
    for (i = 0; i < n; i++)                              /* (no else below: a cold block after the function, */
        aux[i] = x[i] = 0;                               /* jumping back, reads as a loop to the budget) */
    if (B->dust_thr || B->strike || M->e1 || M->e2) {
        int32_t e1 = M->e1, e2 = M->e2, t, bp, big = 0;
        uint32_t rs = M->rng;
        const px_svf_t ex = B->ex;
        for (i = 0; i < n; i++) {                        /* ResonatorSvf<1>, low-pass */
            int64_t in = 0;
            if (B->dust_thr) {                           /* Dust */
                uint32_t u = px_rand(&rs);
                if (u < B->dust_thr)
                    in = (int64_t)(((((uint64_t)u * B->dust_inv) >> 32) * (uint32_t)B->dust_gain) >> 7);   /* Q24 */
            } else if (!i && B->strike) {
                in = (int64_t)B->strike << 8;
            }
            t = (int32_t)((ex.gh * (in - px_m(ex.rpg, e1, 28) - e2)) >> 30);
            bp = e1 + t;
            e1 = bp + t;
            t = px_m(ex.g, bp, 28);
            bp = e2 + t;                                 /* lp */
            e2 = bp + t;
            aux[i] = (bp >> 4) - (B->dust_thr ? B->dust_dc : 0);
            x[i] = bp >> 2;                              /* the modes run in Q22 */
            big |= (bp + 256) >> 9;                      /* |lp| >= 256 (Q24) somewhere in the block */
        }
        if (!big && !B->dust_thr && e1 > -256 && e1 < 256 && e2 > -256 && e2 < 256)
            e1 = e2 = 0;                                 /* rung out (-96 dB) */
        M->e1 = px_clamp(e1, -(1 << 30), 1 << 30);
        M->e2 = px_clamp(e2, -(1 << 30), 1 << 30);
        M->rng = rs;
    }
    for (i = 0; i < n; i++) {                            /* the modes, band-pass */
        int64_t acc = 0;
        const px_mode_t *c = B->m;
        int32_t (*st)[2] = M->s, xi = x[i];
        for (k = 0; k < nm; k++, c++, st++) {
            int32_t s1 = st[0][0], s2 = st[0][1], t, v;
            t = px_m(c->gh, xi - px_m(c->rpg, s1, 28) - s2, 30);
            v = s1 + t;
            st[0][0] = v + t;
            t = px_m(c->g, v, 28);
            st[0][1] = s2 + t + t;
            acc += (int64_t)c->a * v;
        }
        out[i] = (int32_t)(acc >> 30);                   /* Q28 * Q22 -> Q20 */
    }
}

/* ------------------------------------------------------------ String --- */
typedef struct {
    int32_t line[PX_LINE];                               /* DelayLine<1024> (here 512), Q20 */
    int32_t sline[PX_SLINE];                             /* the dispersion all-pass line */
    uint32_t wp, swp;
    int32_t disp, bridge;                                /* dispersion noise, curved bridge (Q20) */
    int32_t dc_x, dc_y, dc_e;                            /* DcBlock (+ its rounding error) */
    int32_t lp;                                          /* OnePole damping filter */
    int32_t ein;                                         /* excitation since the last string step */
    uint32_t src;                                        /* resampler phase, Q30 (up to 2.0) */
    int32_t out0, out1;                                  /* the last two outputs */
    px_csvf_st_t ex;                                     /* exciter Svf */
    px_csvf_t exc;
    uint32_t bk, bn, bd;                                 /* burst: sample, length (a period), comb delay */
    uint32_t nz_a, nz_b;                                 /* burst noise, and the same noise bd samples later */
    uint32_t rng;
    uint8_t trig;
} px_string_t;

typedef struct {                                         /* per block */
    uint32_t delay;                                      /* samples, Q16 */
    int32_t src_ratio;                                   /* Q30, 1.0 = no resampling */
    int32_t comp;                                        /* damping compensation, Q30 */
    int32_t G;                                           /* OnePole: g / (1 + g), Q30 */
    int32_t m_ap, m_main;                                /* ap / main delay out of the delay, Q30 */
    int32_t ap_gain;                                     /* Q30 */
    int32_t noise_amt, noise_flt;                        /* Q30 */
    int32_t curving;                                     /* bridge curving, Q30 */
    uint8_t curved;                                      /* non-linearity < 0: curved bridge */
    uint32_t dust_thr;                                   /* sustained: Dust (Q32), 0 = the burst */
    uint32_t dust_inv;                                   /* 2^47 / thr */
    int32_t dust_gain;                                   /* (8 - 6 dust_f) accent bow, Q16 */
    int32_t dust_dc;                                     /* its mean, Q20 (taken off the aux) */
} px_string_blk_t;

/* 1 / f, f Q32 cycles (>= 2^16: the callers clamp it): samples Q16 */
static uint32_t px_period(uint32_t f) { return (uint32_t)(((uint64_t)1 << 48) / f); }

/* StringVoice::Process + String::ProcessInternal, the per-block part (as px_modal_block); pos (Q16)
 * places the pluck: 0 = DaisySP's plain burst, else the burst minus itself pos / 2 periods later */
static __attribute__((noinline)) void px_string_block(px_string_blk_t *B, px_string_t *S, uint32_t f0, int32_t structure, int32_t brightness,
                            int32_t damping, int32_t accent, int32_t bow, int32_t pos)
{
    int32_t b = brightness + px_m(accent >> 2, 65536 - brightness, 16);
    int32_t d = damping + px_m(accent >> 2, 65536 - damping, 16);
    int32_t nl, dcut, sp, sc, x;
    uint32_t f = f0 > 0x40000000u ? 0x40000000u : f0, delay, df, ratio;
    if (f < 65536u)                                      /* 0.67 Hz */
        f = 65536u;
    /* StringVoice::SetStructure: the non-linearity, < 0 curved bridge, > 0 dispersion */
    nl = structure < 15729 ? (int32_t)(((int64_t)(structure - 15729) * 273024) >> 16)
         : structure > 17039 ? (int32_t)(((int64_t)(structure - 17039) * 88562) >> 16) : 0;
    nl = px_clamp(nl, -65536, 65536);
    /* the exciter: at a trigger (and all the time when sustained) */
    if (S->trig || bow) {
        int32_t oct = (int32_t)(((int64_t)px_m(b, 131072 - b, 16) - 32768) * 6);   /* * 72 / 12 */
        uint64_t c = ((uint64_t)f * 4u * px_exp2(oct)) >> 16;
        px_csvf_coef(&S->exc, c > 2143260078u ? 2143260078u : (uint32_t)c, bow ? 65536 : 32768, 32768);
        if (S->trig) {                                   /* a burst one period long */
            uint32_t per = px_period(f);
            S->bk = 0;
            S->bn = per >> 16;
            S->bd = (uint32_t)(((uint64_t)per * (uint32_t)pos) >> 33);   /* pos / 2 of a period */
            S->nz_a = S->nz_b = px_rand(&S->rng) | 1u;
            S->trig = 0;
        }
    }
    B->dust_thr = 0;
    if (bow) {
        int32_t dn = px_m(brightness, brightness, 16), dfx;
        dn = px_m(dn, dn, 16);
        dfx = 3 + px_m(dn, 65533, 16);
        B->dust_thr = (uint32_t)dfx * 19661u;
        B->dust_inv = (uint32_t)(((uint64_t)1 << 47) / B->dust_thr);
        B->dust_gain = px_m(px_m(px_m(524288 - 6 * dfx, accent, 16), bow, 16), (int32_t)px_exp2(-px_m(d, 217579, 16)), 16);
        B->dust_dc = (int32_t)(((uint64_t)B->dust_thr * (uint32_t)B->dust_gain) >> 29);
    }
    /* String::ProcessInternal; with nl < 0 it runs with |nl| (CURVED_BRIDGE) */
    B->curved = nl < 0;
    if (nl < 0)
        nl = -nl;
    delay = px_period(f);
    if (delay > (uint32_t)(PX_LINE - 4) << 16)
        delay = (uint32_t)(PX_LINE - 4) << 16;
    if (delay < 4u << 16)
        delay = 4u << 16;
    ratio = (uint32_t)(((uint64_t)delay * f) >> 18);     /* delay * frequency, Q30 */
    B->src_ratio = ratio >= 1073634406u ? 1 << 30 : (int32_t)ratio;   /* >= 0.9999: none */
    if (B->src_ratio == 1 << 30)
        S->src = 1u << 30;
    B->delay = delay;
    dcut = 786432 + px_m(px_m(d, d, 16), 3932160, 16) + b * 24;   /* 12 + 60 d^2 + 24 b semitones, Q16 */
    if (dcut > 84 << 16)
        dcut = 84 << 16;
    {
        uint64_t x64 = ((uint64_t)f * px_exp2(dcut / 12)) >> 16;
        df = x64 > 2143260078u ? 2143260078u : (uint32_t)x64;
    }
    if (d >= 62259) {                                    /* crossfade to infinite decay */
        int32_t ti = (d - 62259) * 20;
        b += px_m(ti, 65536 - b, 16);
        df += (uint32_t)(((uint64_t)(2147054347u - df) * (uint32_t)ti) >> 16);
        dcut += px_m(ti, (128 << 16) - dcut, 16);
    }
    if (B->src_ratio < 1 << 30) {                        /* resampled: the filter runs once per string step */
        uint64_t x64 = ((uint64_t)df << 30) / (uint32_t)B->src_ratio;
        df = x64 > 2134624419u ? 2134624419u : (uint32_t)x64;
    }
    if (df > 2134624419u)                                /* OnePole: f < 0.497 */
        df = 2134624419u;
    {   /* OnePole: g = tan(pi f), G = g / (1 + g) = sin / (sin + cos) */
        int32_t s = px_sin(df >> 1), c = px_sin((df >> 1) + 0x40000000u);
        B->G = (int32_t)(((int64_t)s << 30) / (s + c));
    }
    /* damping compensation 1 - 2 atan(1 / ratio) / 2 pi, ratio = 2^(dcut / 12) >= 2 */
    x = (int32_t)(((uint64_t)1 << 46) / px_exp2(dcut / 12));   /* 1 / ratio, Q30 */
    B->comp = (1 << 30) - px_m(px_atan(x), 341782638, 30);    /* * 1 / pi */
    sp = px_m(px_m(nl, 131072 - nl, 16), 14746, 16);     /* nl (2 - nl) 0.225, Q16 */
    sc = (int32_t)(((uint64_t)delay * 238) >> 16);       /* 160 / sr * delay, Q16 */
    sc = px_clamp(sc, 65536, 137626);
    B->m_ap = sp << 14;
    B->m_main = (1 << 30) - px_m(sp, px_m(26739 - px_m(sp, 20185, 16), sc, 16), 2);
    {
        int32_t na = nl > 49152 ? (nl - 49152) * 4 : 0;  /* 4 (nl - 0.75) */
        B->noise_amt = px_m(px_m(na, na, 16), 107374182, 16);   /* ^2 * 0.1, Q30 */
        B->noise_flt = (64424509 + px_m(px_m(b, b, 16), 1009317315, 16));   /* 0.06 + 0.94 b^2 */
        B->curving = px_m(px_m(nl, nl, 16), 10737418, 16);     /* nl^2 0.01 */
        B->ap_gain = -(int32_t)(((int64_t)663568203 * nl) / (9830 + nl));   /* -0.618 nl / (0.15 + nl) */
    }
}

static inline int32_t px_hermite(const int32_t *l, uint32_t wp, uint32_t delay)   /* DelayLine::ReadHermite */
{
    uint32_t t = wp + (delay >> 16);
    int32_t xm1 = l[(t - 1u) & PX_LMASK], x0 = l[t & PX_LMASK], x1 = l[(t + 1u) & PX_LMASK], x2 = l[(t + 2u) & PX_LMASK];
    int64_t f = (int64_t)(delay & 0xFFFF);
    int32_t c = (x1 - xm1) >> 1, v = x0 - x1, w = c + v, a = w + v + ((x2 - x0) >> 1), bn = w + a;
    int64_t y = ((a * f) >> 16) - bn;
    y = ((y * f) >> 16) + c;
    return (int32_t)((y * f) >> 16) + x0;
}

/* the exciter of n samples into aux (Q20): Dust, or the burst through the Svf low-pass */
static void px_string_excite(const px_string_blk_t *B, px_string_t *S, int32_t *aux, uint32_t n)
{
    uint32_t i, bk = S->bk, bend = S->bn + S->bd;
    px_csvf_st_t ex = S->ex;
    for (i = 0; i < n; i++) {
        int32_t in = 0, x = 0;
        if (B->dust_thr) {
            uint32_t u = px_rand(&S->rng);
            if (u < B->dust_thr)
                x = (int32_t)(((((uint64_t)u * B->dust_inv) >> 32) * (uint32_t)B->dust_gain) >> 11);   /* Q20 */
        } else if (bk < bend) {                          /* rand in -1..1; with a pluck position, the */
            if (bk < S->bn)                              /* half of it minus its half pos / 2 periods */
                x = (int32_t)(px_rand(&S->nz_a) >> (S->bd ? 12 : 11)) - (S->bd ? 1 << 19 : 1 << 20);   /* later */
            if (S->bd && bk >= S->bd)
                x -= (int32_t)(px_rand(&S->nz_b) >> 12) - (1 << 19);
            bk++;
        }
        if (x || ex.low || ex.band) {
            int32_t l0;
            px_csvf_pass(&S->exc, &ex, x);
            l0 = ex.low;
            px_csvf_pass(&S->exc, &ex, x);
            in = (l0 + ex.low) >> 1;
            ex.band = px_clamp(ex.band, -(5 << 20), 5 << 20);   /* stays far below (DaisySP's +-1 burst: */
            ex.low = px_clamp(ex.low, -(16 << 20), 16 << 20);   /* ~1.3); bounded all the same */
            if (!x && ex.low > -16 && ex.low < 16 && ex.band > -16 && ex.band < 16)
                ex.low = ex.band = 0;                    /* rung out */
        }
        aux[i] = in - (B->dust_thr ? B->dust_dc : 0);   /* (the string has its own DC blocker) */
    }
    S->bk = bk;
    S->ex = ex;
}

/* the string over n samples, excited by in[] (Q20): out[] */
static void px_string_run(const px_string_blk_t *B, px_string_t *S, const int32_t *in, int32_t *out, uint32_t n)
{
    uint32_t i, wp = S->wp, swp = S->swp, rs = S->rng;
    uint32_t src = S->src;
    int32_t o0 = S->out0, o1 = S->out1, lp = S->lp, disp = S->disp, bridge = S->bridge;
    int32_t dcx = S->dc_x, dcy = S->dc_y, dce = S->dc_e, ein = S->ein;
    for (i = 0; i < n; i++) {
        ein += in[i];                                    /* resampled: the input since the last step */
        /* the string */
        src += (uint32_t)B->src_ratio;
        if (src > 1u << 30) {
            uint32_t dl = (uint32_t)(((uint64_t)B->delay * (uint32_t)B->comp) >> 30);
            int32_t s;
            src -= 1u << 30;
            if (!B->curved) {
                int32_t nz = (int32_t)(px_rand(&rs) >> 12) - (1 << 19);   /* rand - 0.5 */
                disp += px_m(B->noise_flt, nz - disp, 30);
                dl = (uint32_t)(((uint64_t)dl * (uint32_t)((1 << 30) + px_m(disp, B->noise_amt, 20))) >> 30);
                {
                    uint32_t ap = (uint32_t)(((uint64_t)dl * (uint32_t)B->m_ap) >> 30);
                    uint32_t mn = (uint32_t)(((uint64_t)dl * (uint32_t)B->m_main) >> 30);
                    if (ap >= 4u << 16 && mn >= 4u << 16) {
                        uint32_t t = wp + (mn >> 16);
                        int32_t a = S->line[t & PX_LMASK], bb = S->line[(t + 1u) & PX_LMASK], rd, w;
                        s = a + px_m(bb - a, (int32_t)(mn & 0xFFFF), 16);
                        rd = S->sline[(swp + (ap >> 16)) & PX_SMASK];   /* DelayLine::Allpass */
                        w = s + px_m(B->ap_gain, rd, 30);
                        S->sline[swp] = w;
                        swp = (swp - 1u) & PX_SMASK;
                        s = rd - px_m(w, B->ap_gain, 30);
                    } else {
                        s = px_hermite(S->line, wp, dl);
                    }
                }
            } else {
                int32_t val, as;
                dl = (uint32_t)(((uint64_t)dl * (uint32_t)((1 << 30) - px_m(bridge, B->curving, 20))) >> 30);
                s = px_hermite(S->line, wp, dl);
                as = s < 0 ? -s : s;
                val = as - 26214;                        /* |s| - 0.025 */
                bridge = val > 0 ? (s > 0 ? 2 * val : -3 * val) : 0;
            }
            s = px_clamp(s + ein, -20 * PX_ONE, 20 * PX_ONE);
            ein = 0;
            {   /* DcBlock (with the rounding error fed back) */
                int64_t acc = (int64_t)1073498345 * dcy + dce;
                int32_t y = s - dcx + (int32_t)(acc >> 30);
                dce = (int32_t)(acc & 0x3FFFFFFF);
                dcx = s;
                dcy = y;
                s = y;
            }
            {   /* OnePole low-pass */
                int32_t v = px_m(B->G, s - lp, 30);
                s = lp + v;
                lp = s + v;
            }
            S->line[wp] = s;                             /* DelayLine::Write */
            wp = (wp - 1u) & PX_LMASK;
            o1 = o0;
            o0 = s;
        }
        out[i] = o1 + px_m(o0 - o1, (int32_t)src, 30);            /* CrossFade, linear */
    }
    S->wp = wp;
    S->swp = swp;
    S->rng = rs;
    S->src = src;
    S->out0 = o0;
    S->out1 = o1;
    S->lp = lp;
    S->disp = disp;
    S->bridge = bridge;
    S->dc_x = dcx;
    S->dc_y = dcy;
    S->dc_e = dce;
    S->ein = px_clamp(ein, -20 * PX_ONE, 20 * PX_ONE);
}
