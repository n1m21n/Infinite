/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* GRAIN: a granular engine over the SAMPLE material. Felucca's own design.
 *
 * Source: the SAMPLE sets (built in, IMA ADPCM in flash) and the user slots USR1..3 (XIP), the
 * same zones across the keyboard as SAMPLE: a note picks its zone, its pitch sets the grain
 * playback rate against the zone's root. Needs eng_sample.c (zones, ADPCM tables, pow2_q16).
 *
 * Decoding: no RAM copy of the source. Grains decode the ADPCM on demand from a seek index: the
 * decoder state (predictor, step index) every GR_SEG = 256 source samples of every zone of the
 * part's SRC, 3 bytes an entry (GR_NIDX entries a part: PERC, the biggest built-in set, needs
 * ~1020; a full user slot ~650; zones past the capacity stay silent). A zone always starts at the
 * state (0, 0), so zones index independently. The index is built in the block hook, one entry
 * (256 decodes) per part and block, the zones of the sounding voices first: a 1 s zone is ready
 * in ~60 ms, PERC (24 zones) in ~0.7 s. A grain starts at its exact sample: the state of the
 * entry below, then up to 255 decodes skipped. A forward grain then decodes as it plays (one
 * decode per source sample); a reverse grain reads a 128-sample window that it refills from the
 * index as it moves down (about two decodes per source sample). A SRC change or a new upload into
 * the slot (smp_user_gen) rebuilds the index.
 *
 * Grains: a pool of GR_NG per part, shared by its voices (each voice may hold its share of the
 * pool: GR_NG / sounding voices, at least 2), at most GR_POLY voices. A grain: position, length
 * (SIZE, 10..390 ms), rate (note, PITCH, a random detune), direction, a Hann window (table, linear
 * interpolation) and a level 1 / sqrt(overlap). A voice starts its grains DENS times a second
 * (1..98 /s; SPRD also jitters the interval); POS (0..100 % of the zone, + the track's SHP
 * modulation: an LFO scans, ENV -> SHP sweeps) is the centre, SPRD the random spread around it
 * (SPRD 0: a frozen position). RAND: the random detune (quadratic, up to +-1 octave) and the
 * share of reversed grains (up to 50 %). TONE: a one-pole low-pass (+ the track's FLT
 * modulation), as SAMPLE's CUT. A grain is fitted into its zone (shortened if the zone is short).
 *
 * Cost bound: the grain rate is capped at 2 source samples per output sample (max 2 decodes, 4
 * for a reverse grain); a grain only starts while the part's grains decode at most GR_LOAD_MAX
 * (8) source samples per output sample in all (a reverse grain counts twice); at most one grain
 * start per voice and block (<= 255 skipped decodes), one index entry per block. Host count, one
 * part, worst settings: ~1050 instructions / sample (~1170 while the PERC index builds), against
 * PHASE WIRE 1510, TRIO CHIP CHOIR 1455, WHEEL FULL ORGAN 1413 (8 keys, no sends). State: per part the grain pool, the reverse windows and the index
 * (gr_p, in the pool section); per voice s[0] the zone, s[1] the countdown to the next grain,
 * s[2] the low-pass, voice_t's phases are unused. */
#define GR_POLY 3                /* voices per part */
#define GR_NG 12                 /* grains per part */
#define GR_SEG_LOG2 8
#define GR_SEG (1u << GR_SEG_LOG2)   /* seek index interval, source samples */
#define GR_NIDX 1024             /* seek index entries per part (PERC: 1001) */
#define GR_MAXZ 32               /* zones per set (PERC has 24, a user slot <= 16) */
#define GR_RB 128                /* reverse window, source samples */
#define GR_STEP_MAX (2u << 16)   /* grain rate cap: 2 source samples per output sample */
#define GR_LOAD_MAX (8u << 16)   /* decodes per output sample of all a part's grains (a reverse grain counts twice) */

/* sin^2(pi i / 256), Q15: the grain window */
static const int16_t GR_HANN[257] = {
    0, 5, 20, 44, 79, 123, 177, 241, 315, 398, 491, 593, 705, 827, 958, 1098,
    1247, 1406, 1573, 1749, 1935, 2128, 2331, 2542, 2761, 2989, 3224, 3468, 3719, 3978, 4244, 4518,
    4799, 5086, 5381, 5682, 5990, 6304, 6624, 6950, 7281, 7618, 7961, 8308, 8660, 9017, 9379, 9744,
    10114, 10487, 10864, 11244, 11628, 12014, 12403, 12794, 13187, 13583, 13980, 14378, 14778, 15178, 15580, 15981,
    16383, 16786, 17187, 17589, 17989, 18389, 18787, 19184, 19580, 19973, 20364, 20753, 21139, 21523, 21903, 22280,
    22653, 23023, 23388, 23750, 24107, 24459, 24806, 25149, 25486, 25817, 26143, 26463, 26777, 27085, 27386, 27681,
    27968, 28249, 28523, 28789, 29048, 29299, 29543, 29778, 30006, 30225, 30436, 30639, 30832, 31018, 31194, 31361,
    31520, 31669, 31809, 31940, 32062, 32174, 32276, 32369, 32452, 32526, 32590, 32644, 32688, 32723, 32747, 32762,
    32767, 32762, 32747, 32723, 32688, 32644, 32590, 32526, 32452, 32369, 32276, 32174, 32062, 31940, 31809, 31669,
    31520, 31361, 31194, 31018, 30832, 30639, 30436, 30225, 30006, 29778, 29543, 29299, 29048, 28789, 28523, 28249,
    27968, 27681, 27386, 27085, 26777, 26463, 26143, 25817, 25486, 25149, 24806, 24459, 24107, 23750, 23388, 23023,
    22653, 22280, 21903, 21523, 21139, 20753, 20364, 19973, 19580, 19184, 18787, 18389, 17989, 17589, 17187, 16786,
    16384, 15981, 15580, 15178, 14778, 14378, 13980, 13583, 13187, 12794, 12403, 12014, 11628, 11244, 10864, 10487,
    10114, 9744, 9379, 9017, 8660, 8308, 7961, 7618, 7281, 6950, 6624, 6304, 5990, 5682, 5381, 5086,
    4799, 4518, 4244, 3978, 3719, 3468, 3224, 2989, 2761, 2542, 2331, 2128, 1935, 1749, 1573, 1406,
    1247, 1098, 958, 827, 705, 593, 491, 398, 315, 241, 177, 123, 79, 44, 20, 5,
    0,
};
/* grain level by overlap (grains sounding at once on average): 1 / sqrt(n), Q15 */
static const int16_t GR_NORM[GR_NG + 1] = {32767, 32767, 23170, 18918, 16384, 14654, 13377,
                                           12385, 11585, 10922, 10362, 9880, 9459};

typedef struct {
    const smp_zone_t *z;
    uint32_t pos;                /* forward: the next sample to decode; reverse: the index of a */
    int32_t frac;                /* Q16 between a and b */
    uint32_t step;               /* Q16 source samples per output sample */
    uint32_t wph, winc;          /* window phase: one turn = the grain */
    uint32_t left;               /* output samples to go */
    int32_t a, b;                /* the two source samples around the read position */
    int32_t pred, idx;           /* ADPCM state (forward) */
    uint32_t rlo;                /* reverse: the source index of rb[0] */
    int32_t gain;                /* Q15 */
    uint8_t owner;               /* voice index + 1, 0 = free */
    uint8_t rev;
    uint8_t zl;                  /* its zone in the set */
} gr_grain_t;

typedef struct {
    gr_grain_t g[GR_NG];
    int16_t rb[GR_NG][GR_RB];    /* reverse windows */
    int16_t ipred[GR_NIDX];      /* seek index: the state before sample k * GR_SEG of a zone */
    uint8_t iidx[GR_NIDX];
    uint16_t zbase[GR_MAXZ], zcnt[GR_MAXZ], zdone[GR_MAXZ];   /* entries of a zone: first, all, built */
    uint32_t stamp;              /* what the index was built for (user slots: smp_user_gen) */
    int32_t rng;
    uint8_t src;                 /* SRC + 1 the index holds, 0 = none */
    uint8_t nz;
} gr_part_t;
/* (its state: the part's engine arena, engines.c eng_arena_of) */

static uint32_t gr_nz(uint32_t src) { return src < SMP_NSETS ? SMP_SETS[src].nz : usr_nz[(src - SMP_NSETS) % SMP_USER_SLOTS]; }
static const smp_zone_t *gr_zone(uint32_t src, uint32_t zl)
{
    return src < SMP_NSETS ? &SMP_ZONES[SMP_SETS[src].z0 + zl] : &usr_zone[(src - SMP_NSETS) % SMP_USER_SLOTS][zl & 15u];
}
static uint32_t gr_stamp(uint32_t src)
{
    return src < SMP_NSETS ? 0u : usr_nz[(src - SMP_NSETS) % SMP_USER_SLOTS] ? smp_user_gen : 0xFFFFFFFFu;
}
static inline uint32_t gr_rnd(gr_part_t *P) { return noise32(&P->rng); }
static inline uint32_t gr_scale(uint32_t n, uint32_t f16) { return (n >> 16) * f16 + (((n & 0xFFFFu) * f16) >> 16); }

/* one IMA ADPCM sample of zone z at index pos */
static inline int32_t gr_dec(const smp_zone_t *z, uint32_t pos, int32_t *pred, int32_t *idx)
{
    uint32_t b = SMP_DATA[z->off + (pos >> 1)], code = (pos & 1u) ? (b >> 4) : (b & 15u);
    int32_t step = IMA_STEP[*idx], vd = step >> 3;
    if (code & 4u)
        vd += step;
    if (code & 2u)
        vd += step >> 1;
    if (code & 1u)
        vd += step >> 2;
    *pred = clamp(*pred + ((code & 8u) ? -vd : vd), -32768, 32767);
    *idx = clamp(*idx + IMA_IDX[code & 7u], 0, 88);
    return *pred;
}

/* the zone of a note (as SAMPLE: the last zone that holds it; a built-in set falls back to its
 * first zone, a user slot stays silent), -1 = none */
static int32_t gr_find(uint32_t src, uint32_t note)
{
    uint32_t i, nz = gr_nz(src);
    int32_t zl = src < SMP_NSETS ? 0 : -1;
    for (i = 0; i < nz && i < GR_MAXZ; i++) {
        const smp_zone_t *z = gr_zone(src, i);
        if (note >= z->lo && note <= z->hi)
            zl = (int32_t)i;
    }
    return zl;
}

static void gr_reset(gr_part_t *P, uint32_t src, uint32_t stamp)
{
    uint32_t i, tot = 0, nz = gr_nz(src);
    P->src = (uint8_t)(src + 1u);
    P->stamp = stamp;
    P->nz = (uint8_t)(nz < GR_MAXZ ? nz : GR_MAXZ);
    for (i = 0; i < GR_NG; i++)
        P->g[i].owner = 0;
    for (i = 0; i < P->nz; i++) {
        uint32_t c = (gr_zone(src, i)->n + GR_SEG - 1u) >> GR_SEG_LOG2;
        if (tot + c > GR_NIDX)
            c = 0;                                  /* past the capacity: silent */
        P->zbase[i] = (uint16_t)tot;
        P->zcnt[i] = (uint16_t)c;
        P->zdone[i] = c ? 1u : 0u;
        if (c) {
            P->ipred[tot] = 0;                      /* a zone starts at the state (0, 0) */
            P->iidx[tot] = 0;
        }
        tot += c;
    }
}

/* one index entry: the zone of a sounding voice first, else the first one not done */
static void gr_build(gr_part_t *P, const track_t *t)
{
    uint32_t i, zl = GR_MAXZ, k, e, pos;
    int32_t pred, idx;
    const smp_zone_t *z;
    for (i = 0; i < GR_POLY && zl == GR_MAXZ; i++) {
        int32_t vz = t->v[i].s[0];
        if (t->v[i].active && vz >= 0 && vz < P->nz && P->zdone[vz] < P->zcnt[vz])
            zl = (uint32_t)vz;
    }
    for (i = 0; i < P->nz && zl == GR_MAXZ; i++)
        if (P->zdone[i] < P->zcnt[i])
            zl = i;
    if (zl == GR_MAXZ)
        return;
    z = gr_zone(P->src - 1u, zl);
    k = P->zdone[zl];
    e = P->zbase[zl] + k - 1u;
    pred = P->ipred[e];
    idx = P->iidx[e];
    pos = (k - 1u) << GR_SEG_LOG2;
    for (i = 0; i < GR_SEG; i++)
        gr_dec(z, pos + i, &pred, &idx);
    P->ipred[e + 1u] = (int16_t)pred;
    P->iidx[e + 1u] = (uint8_t)idx;
    P->zdone[zl] = (uint16_t)(k + 1u);
}

/* the decoder at sample p of zone zl (p's entry must be built) */
static void gr_seek(const gr_part_t *P, const smp_zone_t *z, uint32_t zl, uint32_t p, int32_t *pred, int32_t *idx)
{
    uint32_t e = P->zbase[zl] + (p >> GR_SEG_LOG2), q;
    *pred = P->ipred[e];
    *idx = P->iidx[e];
    for (q = p & ~(GR_SEG - 1u); q < p; q++)
        gr_dec(z, q, pred, idx);
}

/* reverse: decode [hi - GR_RB + 1, hi] into the grain's window */
static void gr_fill(const gr_part_t *P, gr_grain_t *g, int16_t *rb, uint32_t zl, uint32_t hi)
{
    uint32_t lo = hi >= GR_RB - 1u ? hi - (GR_RB - 1u) : 0u, q;
    int32_t pred, idx;
    gr_seek(P, g->z, zl, lo, &pred, &idx);
    for (q = lo; q <= hi; q++)
        rb[q - lo] = (int16_t)gr_dec(g->z, q, &pred, &idx);
    g->rlo = lo;
}

/* start a grain of voice vi (zone zl) */
static void gr_spawn(gr_part_t *P, const track_t *t, uint32_t vi, uint32_t zl, uint32_t iv, const vmod_t *m)
{
    const int16_t *p = t->p;
    const smp_zone_t *z = gr_zone(P->src - 1u, zl);
    gr_grain_t *g = 0;
    uint32_t i, n = z->n, len, step, span, c, sp, r1 = gr_rnd(P), r2 = gr_rnd(P), ov, rev;
    int32_t d16, amt, posq, pos;
    for (i = 0; i < GR_NG && !g; i++)
        if (!P->g[i].owner)
            g = &P->g[i];
    if (!g || n < 8u || !P->zcnt[zl])
        return;
    /* length: 441 * 2^(SIZE / 24) output samples (10 ms .. 390 ms) */
    len = (441u * pow2_q16(p[P_E2] * 8)) >> 16;
    /* rate: the note against the zone's root, PITCH, the random detune (RAND^2, up to +-1 oct) */
    amt = p[P_E6] * p[P_E6] * 192 / (127 * 127);
    d16 = m->pitch16 - z->root16 + p[P_E4] * 16 + (((int32_t)(r1 & 0xFFFFu) - 32768) * amt >> 15);
    step = (pow2_q16(clamp(d16, -1536, 576)) >> 8) * (z->rate >> 8);
    step = step > GR_STEP_MAX ? GR_STEP_MAX : step < 256u ? 256u : step;
    rev = ((r1 >> 16) & 255u) < (uint32_t)p[P_E6];  /* RAND 127: half of them */
    span = (len * step) >> 16;                      /* source samples the grain reads */
    if (span + 3u > n) {                            /* a short zone: shorter grain */
        span = n - 3u;
        len = (span << 8) / (step >> 8);
    }
    if (len < 16u)
        return;
    /* position: POS (+ the SHP modulation) is the centre, SPRD the random spread */
    posq = clamp((p[P_E1] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    c = gr_scale(n - 1u, (uint32_t)posq * 65535u / (127u << 8));
    sp = gr_scale(n, (uint32_t)p[P_E5] * 516u);
    pos = (int32_t)c + (int32_t)gr_scale(sp, r2 & 0xFFFFu) - (int32_t)(sp >> 1);
    if (rev)
        pos = clamp(pos, (int32_t)span, (int32_t)n - 2);
    else
        pos = clamp(pos, 0, (int32_t)(n - 3u - span));
    if ((uint32_t)pos >> GR_SEG_LOG2 >= P->zdone[zl])
        return;                                     /* not indexed yet */
    for (i = 0, c = step << rev; i < GR_NG; i++)    /* the part's decode load: a cap on the cost */
        if (P->g[i].owner)
            c += P->g[i].step << P->g[i].rev;
    if (c > GR_LOAD_MAX)
        return;
    g->z = z;
    g->step = step;
    g->frac = 0;
    g->wph = 0;
    g->winc = 0xFFFFFFFFu / len;
    g->left = len;
    g->rev = (uint8_t)rev;
    g->zl = (uint8_t)zl;
    ov = len / (iv ? iv : 1u);
    g->gain = GR_NORM[ov < 1u ? 1u : ov > GR_NG ? GR_NG : ov];
    g->owner = (uint8_t)(vi + 1u);
    if (rev) {
        int16_t *rb = P->rb[g - P->g];
        g->pos = (uint32_t)pos;
        gr_fill(P, g, rb, zl, (uint32_t)pos + 1u);
        g->a = rb[g->pos - g->rlo];
        g->b = rb[g->pos + 1u - g->rlo];
    } else {
        gr_seek(P, z, zl, (uint32_t)pos, &g->pred, &g->idx);
        g->a = gr_dec(z, (uint32_t)pos, &g->pred, &g->idx);
        g->b = gr_dec(z, (uint32_t)pos + 1u, &g->pred, &g->idx);
        g->pos = (uint32_t)pos + 2u;
    }
}

static inline int32_t gr_win(uint32_t wph)         /* the Hann window, Q15 */
{
    uint32_t wi = wph >> 24;
    return GR_HANN[wi] + (((GR_HANN[wi + 1u] - GR_HANN[wi]) * (int32_t)((wph >> 9) & 0x7FFFu)) >> 15);
}

/* one grain into acc; returns 0 when it ended. The window (times the grain's level) is a
 * linear ramp over the block between its exact values at the block's ends. */
static int gr_run(gr_part_t *P, gr_grain_t *g, int32_t *acc, uint32_t n)
{
    uint32_t i, m = n < g->left ? n : g->left, step = g->step, pos = g->pos;
    int32_t a = g->a, b = g->b, frac = g->frac, w0, w1, wq, dq;
    const smp_zone_t *z = g->z;
    w0 = mulq15(gr_win(g->wph), g->gain);
    g->wph += g->winc * m;
    g->left -= m;
    w1 = g->left ? mulq15(gr_win(g->wph), g->gain) : 0;
    wq = w0 * 1024;
    dq = m == CTL ? ((w1 - w0) * 1024) >> CTL_LOG2 : m ? (w1 - w0) * 1024 / (int32_t)m : 0;
    if (!g->rev) {
        int32_t pred = g->pred, idx = g->idx;
        for (i = 0; i < m; i++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            acc[i] += (s * (wq >> 10)) >> 15;
            wq += dq;
            frac += (int32_t)step;
            while (frac >= 65536) {
                frac -= 65536;
                a = b;
                b = pos < z->n ? gr_dec(z, pos++, &pred, &idx) : a;
            }
        }
        g->pred = pred;
        g->idx = idx;
    } else {
        int16_t *rb = P->rb[g - P->g];
        for (i = 0; i < m; i++) {
            int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
            acc[i] += (s * (wq >> 10)) >> 15;
            wq += dq;
            frac -= (int32_t)step;
            while (frac < 0) {
                frac += 65536;
                b = a;
                if (pos) {                          /* (fitted: always) */
                    pos--;
                    if (pos < g->rlo)
                        gr_fill(P, g, rb, g->zl, pos);
                }
                a = rb[pos - g->rlo];
            }
        }
    }
    g->pos = pos;
    g->a = a;
    g->b = b;
    g->frac = frac;
    if (!g->left)
        g->owner = 0;
    return g->left != 0;
}

static void grain_note_on(track_t *t, voice_t *v)
{
    gr_part_t *P = (gr_part_t *)eng_arena_of(t, ENGI_GRAIN);
    uint32_t vi = (uint32_t)(v - t->v) % NVOICE, i;
    v->s[0] = gr_find((uint32_t)t->p[P_E0] % SMP_NALL, v->note);
    v->s[1] = 0;                                    /* the first grain at once */
    if (!v->env) {                                  /* a fresh voice: no grains left from before */
        for (i = 0; i < GR_NG; i++)
            if (P->g[i].owner == vi + 1u)
                P->g[i].owner = 0;
        v->s[2] = 0;
    }
}

/* per part and block: the index follows SRC (and a new upload), grains of ended voices go,
 * one index entry is built */
static void grain_block(track_t *t)
{
    gr_part_t *P = (gr_part_t *)eng_arena_of(t, ENGI_GRAIN);
    uint32_t src = (uint32_t)t->p[P_E0] % SMP_NALL, st = gr_stamp(src), i;
    if (!P->rng)
        P->rng = 0x2545F491;
    if (P->src != src + 1u || P->stamp != st) {
        gr_reset(P, src, st);
        for (i = 0; i < NVOICE; i++)
            if (t->v[i].active)
                t->v[i].s[0] = gr_find(src, t->v[i].note);
    }
    for (i = 0; i < GR_NG; i++)
        if (P->g[i].owner && !t->v[(P->g[i].owner - 1u) % NVOICE].active)
            P->g[i].owner = 0;
    gr_build(P, t);
}

static void grain_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    gr_part_t *P = (gr_part_t *)eng_arena_of(t, ENGI_GRAIN);
    const int16_t *p = t->p;
    uint32_t vi = (uint32_t)(v - t->v) % NVOICE, i, mine = 0, nact = 0, iv;
    int32_t zl = v->s[0], acc[CTL], lp, y = v->s[2];
    if (n > CTL)
        n = CTL;
    if (zl < 0 || zl >= P->nz || P->src != (uint32_t)p[P_E0] % SMP_NALL + 1u) {
        if (zl < 0)
            v->active = 0;                          /* a user slot key with no zone: silent */
        return;
    }
    for (i = 0; i < n; i++)
        acc[i] = 0;
    for (i = 0; i < GR_POLY; i++)
        nact += t->v[i].active;
    for (i = 0; i < GR_NG; i++)
        if (P->g[i].owner == vi + 1u)
            mine += gr_run(P, &P->g[i], acc, n);
    /* the next grain: DENS a second, 1..98 /s (SPRD jitters the interval) */
    iv = (44100u * pow2_q16(-p[P_E3] * 10)) >> 16;
    v->s[1] -= (int32_t)n;
    if (v->s[1] <= 0) {
        uint32_t r = gr_rnd(P);
        int32_t j = (((int32_t)(r & 0xFFFFu) - 32768) * p[P_E5]) / 254;   /* +-SPRD / 2, Q15 */
        v->s[1] += (int32_t)iv + mulq15((int32_t)iv, j);
        if (v->s[1] < 0)
            v->s[1] = 0;
        if (mine < (nact > 1u ? (GR_NG / nact > 2u ? GR_NG / nact : 2u) : GR_NG))
            gr_spawn(P, t, vi, (uint32_t)zl, iv, m);
    }
    lp = 4000 + ((clamp((p[P_E7] << 8) + m->cutoff, 0, 127 << 8) * 28767) >> 15);
    for (i = 0; i < n; i++) {
        y += mulq15(clamp(clamp(acc[i], -65535, 65535) - y, -65535, 65535), lp);
        out[i] += mulq15(mulq15(y, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->s[2] = y;
}

static const preset_t GRAIN_PRESETS[] = {
    /* name, {SRC, POS, SIZE, DENS, PTCH, SPRD, RAND, TONE}, {A D S R}, fenv, mono.
     * SRC: the SAMPLE sets (0 PIANO, 2 VIBES, 5 FLUTE...: tools/gen_samples.py CC0_SETS) */
    {"LOFI CLOUD", {0, 45, 95, 85, 0, 35, 12, 80}, {70, 90, 120, 90}, 0, 0, FX(0, 45, 20, 70)},
    {"VIBE HAZE", {2, 30, 100, 90, 0, 25, 10, 90}, {50, 100, 120, 95}, 0, 0, FX(0, 40, 25, 75)},
    {"FLUTE DUST", {5, 50, 108, 72, 0, 10, 10, 85}, {50, 100, 127, 100}, 0, 0, FX(0, 30, 20, 70)},
    /* SLOOP 2.5: strings as a cloud, a piano shimmer an octave up, a dark horn drone */
    {"STR CLOUD", {4, 40, 100, 90, 0, 30, 12, 80}, {70, 100, 120, 95}, 0, 0, FX(0, 50, 20, 75)},
    {"SHIMMER", {0, 30, 90, 95, 12, 40, 15, 110}, {60, 100, 120, 100}, 0, 0, FX(0, 40, 35, 85)},
    {"DRONE", {3, 50, 115, 80, -12, 20, 20, 50}, {90, 100, 127, 100}, 0, 0, FX(0, 30, 15, 70)},
    {"PNO FREEZE", {0, 40, 127, 127, 0, 5, 0, 100}, {80, 100, 127, 100}, 0, 0, FX(0, 30, 20, 75)},
    {"HORN CLOUD", {3, 30, 100, 90, 0, 30, 15, 90}, {70, 100, 120, 95}, 0, 0, FX(0, 40, 20, 70)},
};

static const engine_t ENG_GRAIN = {
    "GRAIN", {"GRAN", "SPRY"},
    {
        {"SRC", F_ENUM, 0, SMP_NALL - 1, 0, SMP_ALL_NAMES, 0},
        {"POS", F_PCT, 0, 127, 32, 0, 0},
        {"SIZE", F_PCT, 0, 127, 80, 0, 0},
        {"DENS", F_PCT, 0, 127, 80, 0, 0},
        {"PTCH", F_SEMI, -24, 24, 0, 0, 0},
        {"SPRD", F_PCT, 0, 127, 30, 0, 0},
        {"RAND", F_PCT, 0, 127, 10, 0, 0},
        {"TONE", F_PCT, 0, 127, 127, 0, 0},
    },
    GRAIN_PRESETS, sizeof(GRAIN_PRESETS) / sizeof(GRAIN_PRESETS[0]), 1, grain_note_on, grain_render,
    0x87F0, {P_E1, P_E2, P_E3, P_E5}, GR_POLY, 0, 0, grain_block,
};
