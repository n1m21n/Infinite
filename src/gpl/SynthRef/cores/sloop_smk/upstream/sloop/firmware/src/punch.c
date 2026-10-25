/* SPDX-License-Identifier: GPL-3.0-only */
/* PUNCH-IN FX, pocket-operator style: hold FX, press a key (the 16 white ones, F3..G5, and,
 * 2.6, the 11 black ones) and the whole mix goes through that effect while the key is held;
 * release it and the mix comes back. Beat-synced to the tempo; loops start on the grid of the
 * running transport. A mono ring of the mix (PUNCH_N samples, 0.74 s) feeds the loops, reverse,
 * tape stop, half speed, wobble, echo, rewind, octave up and the flanger; the filters, kills,
 * crush and gate run in stereo. FREEZE holds the reverb (fx.c). Every change crossfades over
 * 64 samples. Runs in the audio ISR (mix_block, fx.c). */
#define PUNCH_N 32768u                    /* power of two */
/* the transport clock (core.h clk_pos) places the loops and the gate on the grid */
static uint32_t clk_samples(void) { return clk_pos / (uint32_t)song.g[G_BPM]; }   /* samples into the beat */
#define PUNCH_NFX 27u                     /* 16 on the white keys, 11 on the black keys (2.6) */
enum { PX_LOOP4, PX_LOOP8, PX_LOOP16, PX_LOOP32, PX_STUT, PX_REV, PX_STOP, PX_HALF,
       PX_LPF, PX_HPF, PX_TEL, PX_CRUSH, PX_DOWN, PX_GATE, PX_ECHO, PX_WOBBLE,
       /* the black keys, F#3 .. F#5 */
       PX_KLOW, PX_KMID, PX_KHIGH, PX_RWD, PX_OCT, PX_FLANGE, PX_PAN, PX_FREEZE, PX_LOOP8T, PX_MONO, PX_FADE };
static const char *const PUNCH_NAME[PUNCH_NFX] = {
    "LOOP 4", "LOOP 8", "LOOP 16", "LOOP 32", "STUTTER", "REVERSE", "STOP", "HALF",
    "LOW", "HIGH", "PHONE", "CRUSH", "ALIAS", "GATE", "ECHO", "WOBBLE",
    "KILL LOW", "KILL MID", "KILL HIGH", "REWIND", "OCT UP", "FLANGER", "AUTO-PAN", "FREEZE", "LOOP 8T", "MONO",
    "FADE OUT"};
#define PX_OCT_W 2048u                    /* OCT UP: the window of its two read taps (46 ms) */
#define PX_FRZ_CAP 8192u                  /* FREEZE: samples of the mix poured into the held reverb (186 ms) */
#define PX_RWD_ACC 17                     /* REWIND: speed change per sample (Q16): +1 to -3 in 0.35 s */
#define PX_RWD_MAX (3 << 16)
static int16_t punch_ring[PUNCH_N] __attribute__((section(".pool")));
static struct {
    volatile int8_t req;          /* effect asked for by the keys (-1 none), ISR keyboard_block */
    volatile uint8_t hold;        /* FX button held (UI main loop) */
    uint32_t keybit;              /* the key that started it */
    int8_t cur;                   /* effect playing (fading out when req differs) */
    int32_t g;                    /* wet gain Q15 */
    uint32_t w;                   /* ring write index */
    uint32_t start, t, len;       /* loop / reverse: start index, samples since, length */
    uint32_t pre;                 /* loops: samples of dry still to go to the next division */
    uint32_t k, sub;              /* position in the loop; HALF: odd / even sample */
    uint32_t dspd;                /* tape stop: speed step per sample */
    uint32_t gp;                  /* gate: samples into the 1/16 step */
    uint32_t rp;                  /* tape stop / wobble: read position Q16 */
    uint32_t spd;                 /* tape stop speed Q16 */
    uint32_t lfo;                 /* wobble phase */
    int32_t held_l, held_r, hn;   /* downsample */
    int32_t f1l, f2l, f1r, f2r, f3l, f4l, f3r, f4r;   /* filter states */
    int32_t cut;                  /* sweep, 0..127 << 8 */
    int32_t z[8][2];              /* kills: the SVF states (left 0..3, right 4..7) */
    int32_t v;                    /* REWIND: speed Q16 (signed) */
    uint32_t fv;                  /* FADE OUT: level Q23 */
    int32_t mabs;                 /* the mix's mean level, |l| + |r| (FREEZE's target), ~0.2 s */
    int32_t fg;                   /* FREEZE: the held reverb's gain, Q10 */
} punch = {.req = -1, .cur = -1, .fg = 8192};

static int px_loop(int32_t fx) { return fx <= PX_STUT || fx == PX_LOOP8T; }   /* on the grid */
static int px_replay(int32_t fx) { return fx <= PX_HALF || fx == PX_LOOP8T; } /* replays a span of the ring */

static uint32_t beat_samples(void) { return (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM]; }

static int32_t ring_raw(uint32_t i) { return punch_ring[i & (PUNCH_N - 1u)]; }   /* the stored 16 bits (mix / 8) */
static int32_t ring_at(uint32_t i) { return ring_raw(i) << 3; }
static int32_t ring_q16(uint32_t p)                 /* read at Q16 position, linear (on the stored values: no overflow) */
{
    int32_t a = ring_raw(p >> 16), b = ring_raw((p >> 16) + 1u);
    return (a + (((b - a) * (int32_t)((p >> 1) & 0x7FFFu)) >> 15)) << 3;
}

static void punch_start(int32_t fx)
{
    uint32_t beat = beat_samples(), len, ph = clk_samples();
    static const uint8_t DIV[8] = {1, 2, 4, 8, 6, 1, 1, 2};   /* loops: beat / DIV */
    punch.cur = (int8_t)fx;
    punch.t = 0;
    len = fx <= PX_HALF ? beat / DIV[fx] : fx == PX_LOOP8T ? beat / 3u : beat;
    if (len > PUNCH_N - 1024u)
        len = PUNCH_N - 1024u;
    if (len < 64u)
        len = 64u;
    punch.len = len;
    punch.pre = 0;
    punch.start = punch.w;
    if (px_loop(fx) && song.playing) {                /* on the grid: */
        uint32_t back = ph % len;
        if (len - back <= 1536u) {                      /* just before a division: from that one */
            punch.pre = len - back;
        } else {                                        /* else from the last division start */
            punch.start = punch.w - back;
            punch.t = back;
        }
    }
    punch.rp = punch.w << 16;
    punch.spd = 65536u;
    punch.dspd = 65536u / len + 1u;
    punch.sub = 0;
    punch.k = punch.t % len;
    punch.lfo = 0;
    punch.hn = 0;
    punch.cut = 127 << 8;
    punch.f1l = punch.f2l = punch.f1r = punch.f2r = punch.f3l = punch.f4l = punch.f3r = punch.f4r = 0;
    memset(punch.z, 0, sizeof punch.z);
    punch.v = 65536;
    punch.fv = 32767u << 8;
    if (fx == PX_PAN && song.playing)                   /* AUTO-PAN: a turn a beat, on the grid */
        punch.lfo = ph * (0xFFFFFFFFu / beat);
    if (fx == PX_FLANGE)                                /* FLANGER: the sweep starts at its shortest delay */
        punch.lfo = 0xC0000000u;
}

/* the wet sample for the mono ring effects (after the ring got this sample); no divide per
 * sample: k is the position in the loop, counted up and wrapped */
static int32_t punch_ring_fx(int32_t fx)
{
    uint32_t len = punch.len, k = punch.k;
    int32_t y;
    switch (fx) {
    case PX_REV:
        y = ring_at(punch.start - k);
        break;
    case PX_STOP:
        y = ring_q16(punch.rp);
        punch.rp += punch.spd;
        punch.spd = punch.spd > punch.dspd ? punch.spd - punch.dspd : 0u;   /* to a stop in a beat */
        return ((y >> 3) * (int32_t)(punch.spd >> 4)) >> 9;
    case PX_WOBBLE: {
        int32_t d = 700 + (sine_i(punch.lfo) * 180 >> 15);
        punch.lfo += 4u * (0xFFFFFFFFu / FS);           /* 4 Hz */
        return ring_q16((punch.w << 16) - ((uint32_t)d << 16));
    }
    default:                                        /* loops, HALF */
        y = ring_at(punch.start + k);
        break;
    }
    if (fx != PX_HALF || (punch.sub ^= 1u) == 0u)       /* HALF: every other sample */
        punch.k = k + 1u >= len ? 0u : k + 1u;
    /* declick at the seams */
    return k < 64u ? (y * (int32_t)k) >> 6 : len - k < 64u ? (y * (int32_t)(len - k)) >> 6 : y;
}

/* the loops, STUTTER, HALF and REVERSE replay ring samples [start, start + len) (REVERSE: up to start,
 * backwards). The ring keeps recording the mix, and after PUNCH_N samples (0.74 s) the writer comes
 * round into that span: it skips those slots once the loop is captured, so a held loop plays for as
 * long as the key is held. The rest of the ring keeps recording (the next effect has its past). */
static int punch_owns(int32_t fx, uint32_t w)
{
    uint32_t d = w - punch.start, len = punch.len;
    if (fx < 0 || !px_replay(fx) || fx == PX_STOP || punch.pre)
        return 0;
    if (fx == PX_REV)
        return d >= 1u && ((d + len - 1u) & (PUNCH_N - 1u)) < len;
    return d >= len && (d & (PUNCH_N - 1u)) < len;
}

/* the kills: the SVF of dsp.c tsvf_lp with its high-pass out too (Butterworth: tsvf_coef reso 40, damping 5799
 * Q12), two of them in a row (Linkwitz-Riley, 24 dB/oct) */
/* a kill on one channel for the block, in -> out (in at the mix's level), z: its four SVFs. One loop per
 * kill: no choice per sample. The states and coefficients in locals for the block (registers), each SVF step
 * inline, its high-pass made only where it is used: the same numbers as px_svf, step for step */
#define PX_SVF(A1, A2, A3, Z0, Z1, IN, LP, HP) do {                                            \
        int32_t v3_ = (IN) - Z1, v1_ = ((A1) * Z0 + (A2) * v3_) >> 13;                         \
        int32_t v2_ = Z1 + (((A2) * Z0 + (A3) * v3_) >> 13);                                   \
        HP;                                                                                     \
        Z0 = clamp(2 * v1_ - Z0, -150000, 150000);                                              \
        Z1 = clamp(2 * v2_ - Z1, -150000, 150000);                                              \
        LP = v2_;                                                                               \
    } while (0)
#define PX_HPOUT(H, IN) (H = (IN) - ((v1_ * 5799) >> 12) - v2_)
static void px_kill_run(int32_t fx, const tsvf_t *lo, const tsvf_t *hi, const int32_t *in, int32_t *out, uint32_t n,
                        int32_t (*z)[2])
{
    uint32_t i;
    int32_t x, a, b, h, t;
    int32_t a1 = lo->a1, a2 = lo->a2, a3 = lo->a3, b1 = hi->a1, b2 = hi->a2, b3 = hi->a3;
    int32_t z00 = z[0][0], z01 = z[0][1], z10 = z[1][0], z11 = z[1][1];
    int32_t z20 = z[2][0], z21 = z[2][1], z30 = z[3][0], z31 = z[3][1];
    if (fx == PX_KLOW) {                                /* above ~250 Hz */
        for (i = 0; i < n; i++) {
            x = clamp(in[i], -280000, 280000) >> 1;
            PX_SVF(a1, a2, a3, z00, z01, x, t, PX_HPOUT(h, x));
            PX_SVF(a1, a2, a3, z10, z11, h, t, PX_HPOUT(b, h));
            out[i] = b << 1;
        }
    } else if (fx == PX_KHIGH) {                        /* below ~2.5 kHz */
        for (i = 0; i < n; i++) {
            x = clamp(in[i], -280000, 280000) >> 1;
            PX_SVF(b1, b2, b3, z00, z01, x, t, (void)0);
            PX_SVF(b1, b2, b3, z10, z11, t, a, (void)0);
            out[i] = a << 1;
        }
    } else {                                            /* KILL MID: below ~250 Hz + above ~2.5 kHz */
        for (i = 0; i < n; i++) {
            x = clamp(in[i], -280000, 280000) >> 1;
            PX_SVF(a1, a2, a3, z00, z01, x, t, (void)0);
            PX_SVF(a1, a2, a3, z10, z11, t, a, (void)0);
            PX_SVF(b1, b2, b3, z20, z21, x, t, PX_HPOUT(h, x));
            PX_SVF(b1, b2, b3, z30, z31, h, t, PX_HPOUT(b, h));
            out[i] = (a + b) << 1;
        }
    }
    z[0][0] = z00, z[0][1] = z01, z[1][0] = z10, z[1][1] = z11;
    z[2][0] = z20, z[2][1] = z21, z[3][0] = z30, z[3][1] = z31;
}
#undef PX_HPOUT
#undef PX_SVF
static int32_t ring_raw_q16(uint32_t p)             /* ring_q16 without the x 8 (the stored scale) */
{
    int32_t a = ring_raw(p >> 16), b = ring_raw((p >> 16) + 1u);
    return a + (((b - a) * (int32_t)((p >> 1) & 0x7FFFu)) >> 15);
}

/* FREEZE, before the buses (fx.c mix_block): while it plays the reverb holds (no loss); for its first
 * PX_FRZ_CAP samples the dry mix pours into it, then nothing does (the sends too: a held reverb fed
 * for ever would only get louder). rev: the reverb send; l, r: the dry mix */
static void punch_freeze_feed(int32_t *rev, const int32_t *l, const int32_t *r, uint32_t n)
{
    uint32_t i;
    int frz = punch.cur == PX_FREEZE;
    if (frz && !rev_frz) {                              /* (2.6) held from now on: the lines at half, their gain
                                                         * doubled (no step), so the loop's peaks have room (a big
                                                         * reverb held hard clipped on its int16 lines, losing level) */
        for (i = 0; i < sizeof rev_line / sizeof rev_line[0]; i++)
            rev_line[i] = (int16_t)HALF0(rev_line[i]);
        for (i = 0; i < 4u; i++)
            fx.line_lp[i] = HALF0(fx.line_lp[i]);
        punch.fg = 8192;                                /* x 8: the old x 4 on the full lines */
    }
    rev_frz = (uint8_t)frz;
    if (!rev_frz)
        return;
    for (i = 0; i < n; i++)
        rev[i] = punch.t + i < PX_FRZ_CAP ? clamp(HALF0(rev[i]) + ((l[i] + r[i]) >> 4), -165000, 165000) : 0;   /* (fx_buses'
                                                         * mulq15(rev_in, 13000): no overflow) */
}

/* l, r: the mix before the master (fx.c mix_block), n samples, in place */
static void punch_process(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    int32_t want = punch.req, pstep;
    uint32_t beat, step, echo_d, binc, fdec;
    static int32_t kl[CTL], kr[CTL];                    /* (static: the ISR stack is small) */
    tsvf_t c1, c2;
    if (punch.cur != PX_FREEZE) {                       /* (2.6) the mix's level, for FREEZE (every 4th sample) */
        int32_t a = 0;
        for (i = 0; i < n; i += 4u)
            a += (l[i] < 0 ? -l[i] : l[i]) + (r[i] < 0 ? -r[i] : r[i]);
        a = a / (int32_t)((n + 3u) / 4u);
        punch.mabs += (a - punch.mabs) >> 7;
    } else {                                            /* FREEZE: the held level follows the mix's before it (a big
                                                         * reverb held at x 4 jumped up to +11 dB); while it is fed,
                                                         * only down (it grows) */
        int32_t a = 0;
        for (i = 0; i < n; i += 4u)
            a += (rev_l[i & (CTL - 1u)] < 0 ? -rev_l[i & (CTL - 1u)] : rev_l[i & (CTL - 1u)]) +
                 (rev_r[i & (CTL - 1u)] < 0 ? -rev_r[i & (CTL - 1u)] : rev_r[i & (CTL - 1u)]);
        a = (int32_t)(((int64_t)(a / (int32_t)((n + 3u) / 4u)) * punch.fg) >> 10);
        if (a > punch.mabs + (punch.mabs >> 4))
            punch.fg -= punch.fg >> 6;
        else if (a < punch.mabs - (punch.mabs >> 4) && punch.t >= PX_FRZ_CAP)
            punch.fg += (punch.fg >> 6) + 1;
        punch.fg = clamp(punch.fg, 1024, 16384);
    }
    if (punch.cur < 0 && want < 0) {                   /* idle: only the ring */
        for (i = 0; i < n; i++)
            punch_ring[punch.w++ & (PUNCH_N - 1u)] = (int16_t)clamp((l[i] + r[i]) >> 4, -32768, 32767);
        return;
    }
    if (punch.cur < 0 && want >= 0)
        punch_start(want);
    beat = beat_samples();                              /* per block: no divide per sample */
    step = beat / 4u ? beat / 4u : 1u;
    echo_d = beat * 3u / 4u;
    if (echo_d > PUNCH_N - 64u) echo_d = PUNCH_N - 64u;
    binc = 0xFFFFFFFFu / beat;                          /* a turn a beat */
    fdec = (32767u << 8) / (2u * beat);                 /* FADE OUT: to nothing in two beats */
    if (punch.cur == PX_GATE)
        punch.gp = (song.playing ? clk_samples() : punch.t) % step;
    {   /* filter sweeps: per block */
        int32_t fx = punch.cur;
        if (fx == PX_LPF && punch.g > 0)
            punch.cut = punch.cut > (34 << 8) ? punch.cut - 96 : 34 << 8;   /* closes over ~1 s */
        if (fx == PX_HPF && punch.g > 0)
            punch.cut = punch.cut > (88 << 8) ? punch.cut - 64 : 88 << 8;
        if (fx >= PX_KLOW && fx <= PX_KHIGH) {
            static tsvf_t klo, khi;                   /* the kills: 251 Hz, 2.44 kHz (Butterworth); made once */
            if (!klo.a1) {
                tsvf_coef(&klo, 43 << 8, 40);
                tsvf_coef(&khi, 89 << 8, 40);
            }
            if (n <= CTL) {
                px_kill_run(fx, &klo, &khi, l, kl, n, &punch.z[0]);
                px_kill_run(fx, &klo, &khi, r, kr, n, &punch.z[4]);
            }
        } else {
            tsvf_coef(&c1, fx == PX_LPF ? punch.cut : fx == PX_HPF ? (127 << 8) - punch.cut + (40 << 8) : 96 << 8, 90);
            tsvf_coef(&c2, 58 << 8, 40);              /* PHONE: low cut ~ 500 Hz */
        }
    }
    {   /* (2.7.2) GLO > MASTER 2 > FADE: the wet gain's ramp when an effect is engaged / released. 512 (0, ~1.5 ms, as
         * before, just anti-click) down to ~2 at 15 (~0.37 s): a slow fade in and out of the effect */
        int32_t f = (int32_t)song.g[G_FXFADE];
        pstep = 512 / (1 + (f < 0 ? 0 : f > 15 ? 15 : f) * 17);
        if (pstep < 1)
            pstep = 1;
    }
    for (i = 0; i < n; i++) {
        int32_t x = l[i], y = r[i], wl = x, wr = y, m = (x + y) >> 1;
        int32_t fx = punch.cur;
        uint32_t target = want == fx ? 32767u : 0u;
        if (!punch_owns(fx, punch.w))                   /* (a held loop keeps its material) */
            punch_ring[punch.w & (PUNCH_N - 1u)] = (int16_t)clamp(m >> 3, -32768, 32767);
        switch (fx) {
        case PX_LPF:
            wl = tsvf_lp(&c1, x >> 1, &punch.f1l, &punch.f2l) << 1;
            wr = tsvf_lp(&c1, y >> 1, &punch.f1r, &punch.f2r) << 1;
            break;
        case PX_HPF:
            wl = x - (tsvf_lp(&c1, x >> 1, &punch.f1l, &punch.f2l) << 1);
            wr = y - (tsvf_lp(&c1, y >> 1, &punch.f1r, &punch.f2r) << 1);
            break;
        case PX_TEL: {
            int32_t b = tsvf_lp(&c1, m >> 1, &punch.f1l, &punch.f2l);        /* < 2.5 kHz */
            b -= tsvf_lp(&c2, b, &punch.f3l, &punch.f4l);                   /* > 500 Hz */
            wl = wr = softclip(b << 2) >> 1;
            break;
        }
        case PX_CRUSH:
            wl = x >= 0 ? x & ~0x7FF : -((-x) & ~0x7FF);   /* towards zero: no offset */
            wr = y >= 0 ? y & ~0x7FF : -((-y) & ~0x7FF);
            break;
        case PX_DOWN:
            if (--punch.hn <= 0) {
                punch.hn = 8;
                punch.held_l = x;
                punch.held_r = y;
            }
            wl = punch.held_l;
            wr = punch.held_r;
            break;
        case PX_GATE: {
            uint32_t p = punch.gp;
            int32_t gg = p < step / 2u ? 32767 : 0;
            punch.gp = p + 1u >= step ? 0u : p + 1u;
            if (p < 64u) gg = (int32_t)p * 512;
            else if (p >= step / 2u && p < step / 2u + 64u) gg = 32767 - (int32_t)(p - step / 2u) * 512;
            wl = ((x >> 3) * gg) >> 12;
            wr = ((y >> 3) * gg) >> 12;
            break;
        }
        case PX_KLOW:
        case PX_KMID:
        case PX_KHIGH:
            wl = kl[i & (CTL - 1u)];                    /* (px_kill_run, for the block) */
            wr = kr[i & (CTL - 1u)];
            break;
        case PX_RWD: {                                  /* the record pulled back: slows, stops, spins backwards */
            uint32_t back = (uint16_t)(punch.w - (punch.rp >> 16));   /* samples behind the writer */
            int32_t room = (int32_t)PUNCH_N - 512 - (int32_t)back, e;   /* before the writer's overwritten past */
            if (room <= 0) {                            /* all of it heard: silence until the key is up */
                wl = wr = 0;
                break;
            }
            e = room < 1024 ? room * 32 : 32767;
            wl = wr = (((ring_raw_q16(punch.rp) * e) >> 15) << 3);
            punch.rp += (uint32_t)punch.v;
            if (punch.v > -PX_RWD_MAX)
                punch.v -= PX_RWD_ACC;
            break;
        }
        case PX_OCT: {                                  /* two taps read at twice the speed, half a window apart */
            uint32_t d1 = PX_OCT_W - punch.k, d2 = d1 > PX_OCT_W / 2u ? d1 - PX_OCT_W / 2u : d1 + PX_OCT_W / 2u;
            int32_t g1 = (int32_t)(d1 <= PX_OCT_W / 2u ? d1 : PX_OCT_W - d1) << 5;   /* triangles: g1 + g2 = 1 */
            int32_t g2 = 32768 - g1;
            punch.k = punch.k + 1u >= PX_OCT_W ? 0u : punch.k + 1u;
            wl = wr = (ring_raw(punch.w - d1) * g1 + ring_raw(punch.w - d2) * g2) >> 12;
            break;
        }
        case PX_FLANGE: {                               /* 0.6..6 ms, swept over a bar, left and right a quarter apart */
            uint32_t dl = (26u << 16) + (uint32_t)((sine_i(punch.lfo) + 32768) * 240),
                     dr = (26u << 16) + (uint32_t)((sine_i(punch.lfo + 0x40000000u) + 32768) * 240);
            int32_t el = ring_raw_q16((punch.w << 16) - dl), er = ring_raw_q16((punch.w << 16) - dr);
            punch.lfo += binc >> 2;
            punch_ring[punch.w & (PUNCH_N - 1u)] =       /* feedback (the ring is the delay line) */
                (int16_t)clamp((m >> 3) + (((el + er) * 9000) >> 15), -32768, 32767);
            wl = ((x + (el << 3)) * 5) >> 3;
            wr = ((y + (er << 3)) * 5) >> 3;
            break;
        }
        case PX_PAN: {                                  /* the mix in mono, left to right and back once a beat */
            uint32_t a = (uint32_t)(((sine_i(punch.lfo) * 28000) >> 15) + 32768) << 14;   /* 0..a quarter turn */
            int32_t gl = (sine_i(a + 0x40000000u) * 46341) >> 15, gr = (sine_i(a) * 46341) >> 15;   /* x sqrt 2 */
            punch.lfo += binc;
            wl = ((m >> 4) * gl) >> 11;                 /* (>> 4: no overflow on a hot mix, gl up to 46341) */
            wr = ((m >> 4) * gr) >> 11;
            break;
        }
        case PX_FREEZE: {                               /* the held reverb (rev_l / rev_r), the dry mix fading under it */
            int32_t c = punch.t < PX_FRZ_CAP ? (int32_t)(punch.t << 2) : 32767;   /* PX_FRZ_CAP << 2 = 32768 */
            int32_t fl = (rev_l[i & (CTL - 1u)] * punch.fg) >> 10, fr = (rev_r[i & (CTL - 1u)] * punch.fg) >> 10;
            wl = x + ((((fl - x) >> 3) * (c >> 3)) >> 9);
            wr = y + ((((fr - y) >> 3) * (c >> 3)) >> 9);
            break;
        }
        case PX_MONO:
            wl = wr = m;
            break;
        case PX_FADE: {
            int32_t v = (int32_t)(punch.fv >> 8), gq = (v * v) >> 15;   /* squared: the ear hears it even */
            punch.fv = punch.fv > fdec ? punch.fv - fdec : 0u;
            wl = ((x >> 3) * gq) >> 12;
            wr = ((y >> 3) * gq) >> 12;
            break;
        }
        case PX_ECHO: {
            uint32_t d = echo_d;
            int32_t e;
            e = (ring_raw(punch.w - d) * 18000) >> 12;  /* (x 8 after: no overflow on a hot mix) */
            punch_ring[punch.w & (PUNCH_N - 1u)] = (int16_t)clamp((m + e) >> 3, -32768, 32767);   /* feedback */
            wl = x + e;
            wr = y + e;
            break;
        }
        default:
            if (fx < 0)
                break;
            if (punch.pre) {                            /* waiting for the division */
                if (!--punch.pre) {
                    punch.start = punch.w + 1u;
                    punch.k = 0;
                }
                break;
            }
            wl = wr = punch_ring_fx(fx);
            break;
        }
        if ((uint32_t)punch.g < target)
            punch.g = punch.g + pstep > 32767 ? 32767 : punch.g + pstep;
        else if ((uint32_t)punch.g > target)
            punch.g = punch.g < pstep ? 0 : punch.g - pstep;
        l[i] = x + ((((wl - x) >> 3) * (punch.g >> 3)) >> 9);   /* (no 32-bit overflow) */
        r[i] = y + ((((wr - y) >> 3) * (punch.g >> 3)) >> 9);
        punch.w++;
        punch.t++;
        if (punch.g == 0 && want != fx)                 /* faded out: none until the next block, which starts the
                                                         * next one with its own set-up (kills, filters, gate:
                                                         * made per block); at most 31 samples dry */
            punch.cur = -1;
    }
}

/* a key (0..26, from F3) -> its white key index 0..15, -1 = a black key (the layers' steps use it too) */
static int32_t punch_key(uint32_t k)
{
    static const int8_t W[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* from F */
    int32_t i = W[k % 12u];
    return i < 0 ? -1 : (int32_t)(k / 12u) * 7 + i;
}
/* the black keys, F#3 .. F#5, and their effects 16..26 */
static const uint8_t PUNCH_BLACK[PUNCH_NFX - 16u] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
/* the keyboard (seq.c keyboard_block) while FX is held: a key -> its effect (-1 none) */
static int32_t punch_fx_of_key(uint32_t k)
{
    int32_t w = punch_key(k);
    uint32_t i;
    if (w >= 0)
        return w;
    for (i = 0; i < PUNCH_NFX - 16u; i++)
        if (PUNCH_BLACK[i] == k)
            return (int32_t)(16u + i);
    return -1;
}
static uint32_t punch_key_of_fx(uint32_t fx)          /* an effect -> its key (the lit key) */
{
    static const uint8_t K[7] = {0, 2, 4, 6, 7, 9, 11};   /* the white keys of an octave, from F */
    fx %= PUNCH_NFX;
    return fx < 16u ? fx / 7u * 12u + K[fx % 7u] : PUNCH_BLACK[fx - 16u];
}
