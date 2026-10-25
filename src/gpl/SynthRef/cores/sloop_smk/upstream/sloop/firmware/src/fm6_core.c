/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012 Google Inc.
 * Copyright 2016-2025 Pascal Gauthier.
 * C port for Felucca (integer only, no float), 2026: Leo Kuroshita (@kurogedelic), Hügelton Instruments
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* FM6 core: msfa (the 6-operator FM synthesis core of Dexed, Source/msfa) ported to C.
 * Kept from msfa: its envelope (env.cc, with ACCURATE_ENVELOPE), pitch envelope (pitchenv.cc), LFO (lfo.cc),
 * note set-up (the note: level / rate scaling, velocity, operator pitch) and its renderer (fm_core.cc,
 * fm_op_kernel.cc): one operator at a time over a block, two buses, the 32 algorithm tables, feedback on
 * the operator that has it. Changed for Felucca:
 *   - no float or double: the start-up tables (Exp2, Freqlut, Lfo, PitchEnv, the detune curve, FINE) are
 *     const tables from tools/gen_tables.py (FM6_*); the amplitude-modulation exp() is an Exp2 of the same
 *     curve; msfa's float pitch bend, MPE and microtuning are gone (Felucca's pitch, bend, glide and tune
 *     arrive as the note's pitch in 1/16 semitone plus a fine factor: fm6_note_logfreq);
 *   - the sine is Felucca's 1024-point SINE (Q15), linearly interpolated, scaled by the Q24 gain to msfa's
 *     Q24 operator output;
 *   - a block is CTL (32) samples (msfa: 64);
 *   - no portamento, no operator switches, no controllers (mod wheel, breath, ...): Felucca's matrix does that.
 * The patch is the 155-byte single-voice layout (FP_* below), operator 0 = the sixth operator, as msfa. */

#define FM6_N CTL
#define FM6_LG_N 5
#if (1 << FM6_LG_N) != FM6_N
#error "FM6_LG_N does not match CTL"
#endif

/* the 155-byte patch: 6 x 21 operator bytes (the sixth operator first), then the voice */
enum {
    FP_R1 = 0, FP_L1 = 4, FP_BP = 8, FP_LD, FP_RD, FP_LC, FP_RC, FP_RS, FP_AMS, FP_KVS, FP_OL, FP_MODE, FP_FC, FP_FF,
    FP_DET, FP_OP = 21,
    FP_PR1 = 126, FP_PL1 = 130, FP_ALG = 134, FP_FB, FP_OKS, FP_LFS, FP_LFD, FP_LPMD, FP_LAMD, FP_LKS, FP_LFW,
    FP_LPMS, FP_TRNSP, FP_NAME, FP_SIZE = 155
};

/* ---------------------------------------------------------------- math --- */
/* 2^(x / 2^24), Q24 (msfa Exp2::lookup): the Q30 mantissa from FM6_EXP2, shifted by the integer part */
static inline uint32_t fm6_mant(int32_t x)
{
    uint32_t f = (uint32_t)x & 0xFFFFFFu, k = f >> 14, a = FM6_EXP2[k];
    return (1u << 30) + a + (uint32_t)(((uint64_t)(FM6_EXP2[k + 1u] - a) * (f & 0x3FFFu)) >> 14);
}
static inline int32_t fm6_exp2(int32_t x)
{
    int32_t i = x >> 24;
    uint32_t m = fm6_mant(x);
    return i < -24 ? 0 : (int32_t)(m >> (i > 5 ? 1 : 6 - i));      /* (msfa: up to 2^6; here 2^5 at most) */
}
/* phase increment (Q24 cycles per sample) of log2 frequency x (Q24) (msfa Freqlut::lookup) */
static inline int32_t fm6_freq(int32_t x)
{
    int32_t i = x >> 24;
    uint32_t q = (uint32_t)(((uint64_t)fm6_mant(x) * FM6_FREQ_R) >> 25);    /* 2^(i + 21) Hz units */
    if (i < 0)
        return 0;
    return (int32_t)(q >> (21 - (i > 21 ? 21 : i)));
}
/* sine of a Q24 phase (one cycle = 2^24), Q15, linear between SINE's 1024 points (its 1025th = the 1st) */
static inline int32_t fm6_sin(int32_t ph)
{
    const int16_t *s = &SINE[((uint32_t)ph >> 14) & 1023u];
    return s[0] + (((s[1] - s[0]) * (int32_t)(ph & 0x3FFF)) >> 14);
}

/* ------------------------------------------------------------ envelope --- */
typedef struct {                 /* msfa Env: level in Q24 log2 (2^24 = 6 dB), 16 more bits than the original */
    int32_t level, target, inc, statc;
    int16_t outlevel;            /* microsteps (99 * 32 = full) */
    uint8_t rscale, ix, rising, down;
} fm6_env_t;

static const uint8_t FM6_LEVELLUT[20] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};
static int32_t fm6_scaleout(int32_t ol) { return ol >= 20 ? 28 + ol : FM6_LEVELLUT[ol < 0 ? 0 : ol]; }

/* samples at 44.1 kHz a stage holds when its level does not move (msfa ACCURATE_ENVELOPE) */
static const int32_t FM6_STATICS[77] = {
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600, 584325, 507150, 502740,
    441000, 418950, 352800, 308700, 286650, 253575, 220500, 220500, 176400, 145530, 145530, 125685, 110250, 110250,
    88200, 88200, 74970, 61740, 61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562, 27562, 22050, 18522, 17640,
    15435, 14112, 13230, 11025, 9261, 9261, 7717, 6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690, 2249,
    1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837, 837, 705, 573, 573, 529, 441, 441};

/* stage ix (0..3; 4 = done) of an envelope whose rates and levels are op[FP_R1..], op[FP_L1..] */
static void fm6_env_advance(fm6_env_t *e, const uint8_t *op, uint32_t ix)
{
    int32_t lv, actual, qrate;
    e->ix = (uint8_t)ix;
    if (ix >= 4u)
        return;
    lv = op[FP_L1 + ix];
    actual = fm6_scaleout(lv) >> 1;
    actual = (actual << 6) + e->outlevel - 4256;
    actual = actual < 16 ? 16 : actual;
    e->target = actual << 16;
    e->rising = e->target > e->level;
    qrate = ((op[FP_R1 + ix] * 41) >> 6) + e->rscale;
    qrate = qrate > 63 ? 63 : qrate;
    if (e->target == e->level || (ix == 0u && lv == 0)) {
        int32_t sr = op[FP_R1 + ix] + e->rscale;
        sr = sr > 99 ? 99 : sr;
        e->statc = sr < 77 ? FM6_STATICS[sr] : 20 * (99 - sr);
        if (sr < 77 && ix == 0u && lv == 0)
            e->statc /= 20;                     /* the attack is scaled faster (a stage change, not per sample) */
    } else {
        e->statc = 0;
    }
    e->inc = (4 + (qrate & 3)) << (2 + FM6_LG_N + (qrate >> 2));
}

static void fm6_env_init(fm6_env_t *e, const uint8_t *op, int32_t outlevel, int32_t rate_scaling)
{
    e->outlevel = (int16_t)outlevel;
    e->rscale = (uint8_t)rate_scaling;
    e->level = 0;
    e->down = 1;
    fm6_env_advance(e, op, 0);
}

static int32_t fm6_env_get(fm6_env_t *e, const uint8_t *op)
{
    if (e->statc) {
        e->statc -= FM6_N;
        if (e->statc <= 0) {
            e->statc = 0;
            fm6_env_advance(e, op, e->ix + 1u);
        }
    }
    if (e->ix < 3u || (e->ix < 4u && !e->down)) {
        if (e->statc) {
            ;
        } else if (e->rising) {
            if (e->level < (1716 << 16))
                e->level = 1716 << 16;
            e->level += (((17 << 24) - e->level) >> 24) * e->inc;
            if (e->level >= e->target) {
                e->level = e->target;
                fm6_env_advance(e, op, e->ix + 1u);
            }
        } else {
            e->level -= e->inc;
            if (e->level <= e->target) {
                e->level = e->target;
                fm6_env_advance(e, op, e->ix + 1u);
            }
        }
    }
    return e->level;
}

static void fm6_env_key(fm6_env_t *e, const uint8_t *op, int down)
{
    if (e->down != (uint8_t)down) {
        e->down = (uint8_t)down;
        fm6_env_advance(e, op, down ? 0u : 3u);
    }
}

/* ------------------------------------------------------ pitch envelope --- */
typedef struct {
    int32_t level, target, inc;
    uint8_t ix, rising, down, rsv;
} fm6_penv_t;

static const uint8_t FM6_PENV_RATE[100] = {
    1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49, 51, 53, 54, 56,
    58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125, 130, 135, 141,
    147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};
static const int8_t FM6_PENV_TAB[100] = {
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31, -30, -29,
    -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6,
    -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127};

static void fm6_penv_advance(fm6_penv_t *e, const uint8_t *p, uint32_t ix)
{
    e->ix = (uint8_t)ix;
    if (ix < 4u) {
        e->target = FM6_PENV_TAB[p[FP_PL1 + ix] % 100u] * (1 << 19);
        e->rising = e->target > e->level;
        e->inc = FM6_PENV_RATE[p[FP_PR1 + ix] % 100u] * FM6_PENV_UNIT;
    }
}
static void fm6_penv_init(fm6_penv_t *e, const uint8_t *p)
{
    e->level = FM6_PENV_TAB[p[FP_PL1 + 3] % 100u] * (1 << 19);
    e->down = 1;
    fm6_penv_advance(e, p, 0);
}
static int32_t fm6_penv_get(fm6_penv_t *e, const uint8_t *p)
{
    if (e->ix < 3u || (e->ix < 4u && !e->down)) {
        if (e->rising) {
            e->level += e->inc;
            if (e->level >= e->target) {
                e->level = e->target;
                fm6_penv_advance(e, p, e->ix + 1u);
            }
        } else {
            e->level -= e->inc;
            if (e->level <= e->target) {
                e->level = e->target;
                fm6_penv_advance(e, p, e->ix + 1u);
            }
        }
    }
    return e->level;
}
static void fm6_penv_key(fm6_penv_t *e, const uint8_t *p, int down)
{
    if (e->down != (uint8_t)down) {
        e->down = (uint8_t)down;
        fm6_penv_advance(e, p, down ? 0u : 3u);
    }
}

/* ----------------------------------------------------------------- LFO --- */
typedef struct {
    uint32_t phase, delta, dstate, dinc, dinc2;
    uint8_t wave, rnd, sync, rsv;
} fm6_lfo_t;

static void fm6_lfo_reset(fm6_lfo_t *l, const uint8_t *p)   /* the patch's LFO (msfa Lfo::reset); phase kept */
{
    int32_t a = 99 - (p[FP_LFD] % 100u);
    l->delta = FM6_LFO_DELTA[p[FP_LFS] % 100u];
    if (a == 99) {
        l->dinc = l->dinc2 = ~0u;
    } else {
        a = (16 + (a & 15)) << (1 + (a >> 4));
        l->dinc = (uint32_t)(FM6_LFO_UNIT * a);
        a &= 0xff80;
        a = a < 0x80 ? 0x80 : a;
        l->dinc2 = (uint32_t)(FM6_LFO_UNIT * a);
    }
    l->wave = p[FP_LFW];
    l->sync = p[FP_LKS] != 0;
}

static int32_t fm6_lfo_sample(fm6_lfo_t *l)   /* 0..1, Q24; once a block */
{
    int32_t x;
    l->phase += l->delta;
    switch (l->wave) {
    case 0:                                       /* triangle */
        x = (int32_t)(l->phase >> 7);
        x ^= -(int32_t)(l->phase >> 31);
        return x & ((1 << 24) - 1);
    case 1:                                       /* saw down */
        return (int32_t)((~l->phase ^ (1u << 31)) >> 8);
    case 2:                                       /* saw up */
        return (int32_t)((l->phase ^ (1u << 31)) >> 8);
    case 3:                                       /* square */
        return (int32_t)(((~l->phase) >> 7) & (1u << 24));
    case 4:                                       /* sine */
        return (1 << 23) + (fm6_sin((int32_t)(l->phase >> 8)) << 8);
    case 5:                                       /* sample and hold */
        if (l->phase < l->delta)
            l->rnd = (uint8_t)((l->rnd * 179 + 17) & 0xff);
        return ((l->rnd ^ 0x80) + 1) << 16;
    }
    return 1 << 23;
}

static int32_t fm6_lfo_delay(fm6_lfo_t *l)   /* the delay ramp, 0..1 Q24; once a block */
{
    uint32_t d = l->dstate < (1u << 31) ? l->dinc : l->dinc2, s = l->dstate + d;
    if (s < l->dstate)                            /* past 2^32: delayed in full */
        return 1 << 24;
    l->dstate = s;
    return s < (1u << 31) ? 0 : (int32_t)((s >> 7) & ((1u << 24) - 1u));
}

static void fm6_lfo_key(fm6_lfo_t *l)
{
    if (l->sync)
        l->phase = (1u << 31) - 1u;
    l->dstate = 0;
}

/* ------------------------------------------------------- the algorithms --- */
enum { FM6_OB1 = 1, FM6_OB2 = 2, FM6_OADD = 4, FM6_IB1 = 16, FM6_IB2 = 32, FM6_FBIN = 64, FM6_FBOUT = 128 };
static const uint8_t FM6_ALG[32][6] = {      /* per operator (the sixth first): in bus, out bus, add, feedback */
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14},
    {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14},
    {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14},
    {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04},
    {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}};

/* bit k: operator k (the sixth first) is a carrier of algorithm a (0..31): it writes the output bus (msfa's
 * isCarrier also counts the modulators that add into bus 1 or 2) */
static uint32_t fm6_carriers(uint32_t a)
{
    uint32_t k, c = 0;
    for (k = 0; k < 6u; k++)
        c |= ((FM6_ALG[a & 31u][k] & 3u) ? 0u : 1u) << k;
    return c;
}

/* ------------------------------------------------------- the operators --- */
typedef struct {                 /* msfa FmOpParams */
    int32_t level_in;            /* Q24 log2 */
    int32_t gain_out;            /* Q24 */
    int32_t freq;                /* Q24 cycles per sample */
    int32_t phase;               /* Q24 */
} fm6_op_t;

#define FM6_THRESH 1120          /* msfa kLevelThresh: an operator below it on both block ends is skipped */

/* one operator over the block: modulated by in (0: none), into out (add: += else =). Four loops, so none
 * tests a flag per sample */
#define FM6_OP_LOOP(PH, STORE)                                                    \
    for (i = 0; i < FM6_N; i++) {                                               \
        g += dg;                                                                \
        out[i] STORE (int32_t)(((int64_t)fm6_sin(PH) * g) >> 15);               \
        phase = (int32_t)((uint32_t)phase + (uint32_t)freq);                    \
    }
static __attribute__((noinline)) void fm6_op_run(int32_t *out, const int32_t *in, int32_t phase, int32_t freq,
                                                 int32_t g1, int32_t g2, int add)
{
    int32_t dg = (g2 - g1 + (FM6_N >> 1)) >> FM6_LG_N, g = g1;
    uint32_t i;
    if (in && add)
        FM6_OP_LOOP(phase + in[i], +=)
    else if (in)
        FM6_OP_LOOP(phase + in[i], =)
    else if (add)
        FM6_OP_LOOP(phase, +=)
    else
        FM6_OP_LOOP(phase, =)
}

/* the operator with feedback (fb: its last two outputs) */
static __attribute__((noinline)) void fm6_op_fb(int32_t *out, int32_t phase, int32_t freq, int32_t g1, int32_t g2,
                                                int32_t *fb, int32_t shift, int add)
{
    int32_t dg = (g2 - g1 + (FM6_N >> 1)) >> FM6_LG_N, g = g1, y0 = fb[0], y = fb[1];
    uint32_t i;
    for (i = 0; i < FM6_N; i++) {
        int32_t s = (y0 + y) >> (shift + 1);
        g += dg;
        y0 = y;
        y = (int32_t)(((int64_t)fm6_sin(phase + s) * g) >> 15);
        out[i] = add ? out[i] + y : y;
        phase = (int32_t)((uint32_t)phase + (uint32_t)freq);
    }
    fb[0] = y0;
    fb[1] = y;
}

/* msfa FmCore::render: the six operators of algorithm alg into out (Q24), through the two modulator buses
 * bus (scratch: the part's, eng_fm6.c fm6_part_t, so that parts render on both cores). Unlike msfa, out
 * starts empty: the first carrier sounding writes it, the next ones add. Returns 0 when none sounded (out untouched) */
static int fm6_core_render(int32_t *out, int32_t (*bus)[FM6_N], fm6_op_t *op, uint32_t alg, int32_t *fb, int32_t fb_shift)
{
    const uint8_t *a = FM6_ALG[alg & 31u];
    uint8_t has[3] = {0, 0, 0};
    uint32_t k;
    for (k = 0; k < 6u; k++) {
        uint32_t f = a[k], inbus = (f >> 4) & 3u, outbus = f & 3u;
        int add = (f & FM6_OADD) != 0;
        int32_t *o = outbus ? bus[outbus - 1u] : out;
        int32_t g1 = op[k].gain_out, g2 = fm6_exp2(op[k].level_in - (14 << 24));
        op[k].gain_out = g2;
        if (g1 >= FM6_THRESH || g2 >= FM6_THRESH) {
            if (!has[outbus])
                add = 0;
            if (inbus == 0u || !has[inbus]) {
                if ((f & 0xc0u) == 0xc0u && fb_shift < 16)
                    fm6_op_fb(o, op[k].phase, op[k].freq, g1, g2, fb, fb_shift, add);
                else
                    fm6_op_run(o, 0, op[k].phase, op[k].freq, g1, g2, add);
            } else {
                fm6_op_run(o, bus[inbus - 1u], op[k].phase, op[k].freq, g1, g2, add);
            }
            has[outbus] = 1;
        } else if (!add) {
            has[outbus] = 0;
        }
        op[k].phase = (int32_t)((uint32_t)op[k].phase + ((uint32_t)op[k].freq << FM6_LG_N));
    }
    return has[0];
}

/* ------------------------------------------------------------ the note --- */
typedef struct {                 /* a sounding note (msfa's note): 244 bytes */
    fm6_env_t env[6];
    fm6_op_t op[6];
    int32_t base[6];             /* the operator's pitch: ratio: log2 offset from the note's (Q24); fixed: log2 Hz */
    int32_t fb[2];
    fm6_penv_t penv;
    uint8_t fixed;               /* bit k: operator k has a fixed frequency */
    uint8_t live, down, rsv;
    int32_t dc;                  /* (eng_fm6.c) the output's DC blocker */
} fm6_note_t;

static const int32_t FM6_COARSE[32] = {
    -16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600, 50331648, 53182516, 55732705,
    58039632, 60145690, 62083076, 63876816, 65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858,
    74816848, 75892776, 76922906, 77910978, 78860292, 79773775, 80654032, 81503396, 82323963, 83117622};
static const uint8_t FM6_VELDATA[64] = {
    0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163, 166, 170, 173, 174, 178, 181, 184,
    186, 189, 190, 194, 196, 198, 200, 202, 205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230,
    232, 233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254};
static const uint8_t FM6_EXPSCALE[33] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66,
                                         80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};
static const uint8_t FM6_PMS[8] = {0, 10, 20, 33, 55, 92, 153, 255};
static const uint32_t FM6_AMSENS[4] = {0, 4342338, 7171437, 16777216};
#define FM6_LOGF0 50857777       /* log2 of MIDI note 0's frequency, Q24 */

static int32_t fm6_scale_vel(int32_t vel, int32_t sens)
{
    int32_t v = FM6_VELDATA[(vel < 0 ? 0 : vel > 127 ? 127 : vel) >> 1] - 239;
    return ((sens * v + 7) >> 3) << 4;
}
static int32_t fm6_scale_rate(int32_t note, int32_t sens)
{
    int32_t x = note / 3 - 7;
    x = x < 0 ? 0 : x > 31 ? 31 : x;
    return (sens * x) >> 3;
}
static int32_t fm6_scale_curve(int32_t group, int32_t depth, int32_t curve)
{
    int32_t s;
    if (curve == 0 || curve == 3)
        s = (group * depth * 329) >> 12;
    else
        s = (FM6_EXPSCALE[group > 32 ? 32 : group] * depth * 329) >> 15;
    return curve < 2 ? -s : s;
}
static int32_t fm6_scale_level(int32_t note, const uint8_t *op)
{
    int32_t off = note - op[FP_BP] - 17;
    return off >= 0 ? fm6_scale_curve((off + 1) / 3, op[FP_RD], op[FP_RC])
                    : fm6_scale_curve(-(off - 1) / 3, op[FP_LD], op[FP_LC]);
}

/* the operator's pitch (msfa note osc_freq): ratio mode as an offset from the note, fixed mode absolute */
static int32_t fm6_osc_base(const uint8_t *op, int32_t note, int32_t extra)
{
    int32_t det = op[FP_DET];
    if (!op[FP_MODE]) {
        int32_t lf = (int32_t)FM6_DETUNE[note] * (det - 7) + FM6_COARSE[op[FP_FC] & 31u] + extra;
        return op[FP_FF] ? lf + FM6_FINE[op[FP_FF] % 100u] : lf;
    }
    return ((4458616 * ((op[FP_FC] & 3) * 100 + op[FP_FF] % 100u)) >> 3) + (det > 7 ? 13457 * (det - 7) : 0);
}

/* a note-on (msfa note init) of patch p, MIDI note `note` (transposed), velocity vel. fresh: from silence
 * (phases, gains, feedback 0); else a retrigger keeps them, as msfa does */
static void fm6_note_init(fm6_note_t *n, const uint8_t *p, int32_t note, int32_t vel, int fresh)
{
    uint32_t k;
    note = note < 0 ? 0 : note > 127 ? 127 : note;
    if (fresh) {
        for (k = 0; k < 6u; k++)
            n->op[k].phase = n->op[k].gain_out = 0;
        n->fb[0] = n->fb[1] = 0;
        n->dc = 0;
    }
    n->fixed = 0;
    for (k = 0; k < 6u; k++) {
        const uint8_t *op = p + k * FP_OP;
        int32_t ol = fm6_scaleout(op[FP_OL]) + fm6_scale_level(note, op);
        ol = (ol > 127 ? 127 : ol) << 5;
        ol += fm6_scale_vel(vel, op[FP_KVS]);
        fm6_env_init(&n->env[k], op, ol < 0 ? 0 : ol, fm6_scale_rate(note, op[FP_RS]));
        n->base[k] = fm6_osc_base(op, note, 0);
        n->fixed |= (uint8_t)((op[FP_MODE] & 1u) << k);
    }
    fm6_penv_init(&n->penv, p);
    n->live = n->down = 1;
}

static void fm6_note_key(fm6_note_t *n, const uint8_t *p, int down)
{
    uint32_t k;
    for (k = 0; k < 6u; k++)
        fm6_env_key(&n->env[k], p + k * FP_OP, down);
    fm6_penv_key(&n->penv, p, down);
    n->down = (uint8_t)down;
}

/* the log2 frequency (Q24) of a pitch in 1/16 semitone plus a fine factor (1 + fine / 4096: Felucca's
 * unison detune, tune and bend below 1/16 semitone): log2(1 + x) ~ x / ln 2 for these small factors */
static int32_t fm6_note_logfreq(int32_t pitch16, int32_t fine)
{
    return FM6_LOGF0 + (int32_t)(((int64_t)pitch16 * ((1 << 24) / 12)) >> 4) + fine * 5909;
}

/* one block (msfa note compute): the LFO's value and delay (Q24), the note's log2 frequency, the patch's
 * algorithm and feedback (already through the macros), dt[k]: a pitch offset per operator (Q24),
 * modlvl: a level offset of the operators that are not carriers (Q24 log2). Writes out (Q24); 0 = silent
 * (out untouched) */
static int fm6_note_compute(fm6_note_t *n, const uint8_t *p, int32_t *out, int32_t (*bus)[FM6_N], int32_t lfo_val,
                             int32_t lfo_delay, int32_t logfreq, uint32_t alg, int32_t fb, const int32_t *dt, int32_t modlvl)
{
    uint32_t pmd = (uint32_t)((p[FP_LPMD] % 100u) * 165u >> 6) * (uint32_t)lfo_delay;   /* Q32 */
    int32_t senslfo = FM6_PMS[p[FP_LPMS] & 7u] * (lfo_val - (1 << 23));
    int32_t pmod = (int32_t)(((int64_t)pmd * senslfo) >> 39), pitch_mod;
    uint32_t amd = (uint32_t)((p[FP_LAMD] % 100u) * 165u >> 6), amod, car = fm6_carriers(alg), k;
    pmod = pmod < 0 ? -pmod : pmod;
    pitch_mod = fm6_penv_get(&n->penv, p) + (senslfo < 0 ? -pmod : pmod);
    amod = (uint32_t)(((int64_t)amd * lfo_delay) >> 8);                 /* Q24 */
    amod = (uint32_t)(((int64_t)amod * ((1 << 24) - lfo_val)) >> 24);
    for (k = 0; k < 6u; k++) {
        const uint8_t *op = p + k * FP_OP;
        int32_t level = fm6_env_get(&n->env[k], op);
        uint32_t ams = FM6_AMSENS[op[FP_AMS] & 3u];
        n->op[k].freq = fm6_freq((n->fixed >> k) & 1u ? n->base[k] : logfreq + n->base[k] + pitch_mod + dt[k]);
        if (ams) {                                 /* msfa: pt = exp(sensamp / 262144 * 0.07 + 12.2) */
            uint32_t sensamp = (uint32_t)(((uint64_t)amod * ams) >> 24);
            int32_t lg = FM6_AMS_C0 + (int32_t)(((uint64_t)sensamp * FM6_AMS_K) >> 10);
            uint32_t pt = fm6_mant(lg) >> (30 - (lg >> 24));   /* 2^17.6 .. 2^24.1: the Q30 mantissa >> 13..6 (2.4.1: it
                                                                * was shifted left, past 32 bits: a random level, noise) */
            level -= (int32_t)(((uint64_t)(uint32_t)level * ((uint64_t)pt << 4)) >> 28);
        }
        if (!((car >> k) & 1u))
            level += modlvl;
        n->op[k].level_in = level < 0 ? 0 : level > (17 << 24) ? 17 << 24 : level;
    }
    return fm6_core_render(out, bus, n->op, alg, n->fb, fb ? 8 - fb : 16);
}

/* the note can be dropped: every carrier silent for good (released and done, or held at a silent
 * sustain whose release can only fall) */
static int fm6_note_done(const fm6_note_t *n, const uint8_t *p, uint32_t alg)
{
    uint32_t car = fm6_carriers(alg), k;
    for (k = 0; k < 6u; k++) {
        const fm6_env_t *e = &n->env[k];
        if (!((car >> k) & 1u))
            continue;
        if (n->op[k].gain_out >= FM6_THRESH)
            return 0;
        if (e->ix < 3u)
            return 0;
        if (e->ix == 3u && !e->down && e->rising)
            return 0;
        if (e->ix == 3u && e->down) {              /* holding: a release towards L4 must not rise */
            int32_t a = (fm6_scaleout(p[k * FP_OP + FP_L1 + 3]) >> 1 << 6) + e->outlevel - 4256;
            if ((a < 16 ? 16 : a) << 16 > e->level)
                return 0;
        }
    }
    return 1;
}
