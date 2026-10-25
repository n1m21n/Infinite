/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICER: a per-track insert, a tempo-synced 16-step gate / stutter. It works on the track's dry
 * mono signal after DIST and before LEVEL / PAN / the sends (fx.c mix_part), so the sends follow
 * the chopped sound; on the drum track before its pan and its reverb and delay sends (slicer_drums).
 *   P_SLCR    OFF / GATE / STUT
 *   P_SLPAT   one of SL_NPAT patterns of 16 steps ('x' live / open, '.' gated / repeated)
 *   P_SLRATE  the step: 1/8, 1/16, 1/32, 8T, 16T, 32T
 *   P_SLDEPTH GATE: how far a '.' step closes (100 % = silent); STUT: the level of the repeat
 * GATE: the gain moves at most 1 / SL_RAMP per sample (2.9 ms from open to closed): a closing
 *   ramp ends on the step boundary, an opening one starts on it.
 * STUT: an 'x' step plays live and is recorded (22.05 kHz, 16 bit, SL_LEN samples a track); a '.'
 *   step plays that recording from its start, looped at the step length (halved until it fits the
 *   recording), cross-faded with the live sound by DEPTH. Every pass of the loop is windowed
 *   (SL_RAMP at both ends and at the step end), the cross-fade moves as the gate does.
 * The step clock: per track, always running (also with the SLICER OFF, so switching it on lands
 * in time), restarted with the transport (seq_start -> slicer_start: step 0 starts with the
 * sequencer's step 0), in the transport clock's units (fx.c BEAT_U: exact at any tempo, no drift
 * from the steps), the track's + the global SWING as the sequencer has them (seq.c trk_grid); sample
 * exact. With the SLICER OFF and no ramp left, the signal is not touched. */
#define SL_NPAT 16
#define SL_LEN 4096u                    /* recording, 22.05 kHz samples a track: 186 ms, 8 KB */
#define SL_RAMP_LOG2 7
#define SL_RAMP (1 << SL_RAMP_LOG2)     /* 128 samples, 2.9 ms */
#define SL_SLOPE (32768 / SL_RAMP)
enum { SL_OFF, SL_GATE, SL_STUT };      /* P_SLCR (params.c N_SLCR) */

/* bit k = step k: 1 = 'x' (open / live), 0 = '.' (closed / repeat) */
static const uint16_t SL_PAT[SL_NPAT] = {
    0x5555,   /*  1 x.x.x.x.x.x.x.x. */
    0xB6DB,   /*  2 xx.xx.xx.xx.xx.x */
    0x5249,   /*  3 x..x..x..x..x.x. */
    0xB777,   /*  4 xxx.xxx.xxx.xx.x */
    0x6D6D,   /*  5 x.xx.xx.x.xx.xx. */
    0x0F0F,   /*  6 xxxx....xxxx.... */
    0x1111,   /*  7 x...x...x...x... */
    0x54A5,   /*  8 x.x..x.x..x.x.x. */
    0x5333,   /*  9 xx..xx..xx..x.x. */
    0xA929,   /* 10 x..x.x..x..x.x.x */
    0xAAFF,   /* 11 xxxxxxxx.x.x.x.x */
    0xD501,   /* 12 x.......x.x.x.xx */
    0xAAAA,   /* 13 .x.x.x.x.x.x.x.x */
    0xAB6B,   /* 14 xx.x.xx.xx.x.x.x */
    0xBB5D,   /* 15 x.xxx.x.xx.xxx.x */
    0x7597,   /* 16 xxx.x..xx.x.xxx. */
};
static const uint8_t SL_DEN[6] = {2, 4, 8, 3, 6, 12};   /* P_SLRATE (N_SLDIV): a step = 1 / DEN beats */

static int16_t sl_buf[NTRK][SL_LEN] __attribute__((section(".pool")));
typedef struct {
    uint32_t pos, len;           /* clock units into the step, its length */
    uint32_t base;               /* the step without swing (units) */
    uint32_t rp, loop;           /* repeat: read position, loop length (44.1 kHz samples), 0 = none */
    uint32_t rec;                /* recorded (22.05 kHz samples) */
    int32_t gc;                  /* gate closure, Q15: 0 = open */
    int32_t w;                   /* STUT cross-fade, Q15: 0 = live */
    int32_t half;                /* recording: the first sample of a pair */
    uint32_t half_n;             /* (samples recorded this step: odd = the second of a pair) */
    uint8_t idx;                 /* step 0..15 */
    uint8_t bit;                 /* this step's pattern bit (latched at its start) */
    uint8_t rec_on;              /* recording this step */
} sl_t;
static sl_t sl[NTRK];
static int32_t sl_dbuf[CTL];     /* the drum track's dry mono signal (slicer_drums) */

static void slicer_start(uint32_t pos)   /* seq_start: step 0 of every track, pos units in */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        sl[k].idx = 15;
        sl[k].pos = pos;
        sl[k].len = 0;
    }
}

static uint32_t sl_pattern(const track_t *t) { return SL_PAT[(uint32_t)(t->p[P_SLPAT] - 1) % SL_NPAT]; }

/* the step clock enters the next step */
static void sl_enter(const track_t *t, sl_t *s)
{
    uint32_t mode = (uint32_t)t->p[P_SLCR], sw;
    s->pos = s->pos >= s->len ? s->pos - s->len : 0;   /* (the overshoot: no drift) */
    s->idx = (uint8_t)((s->idx + 1u) & 15u);
    s->base = BEAT_U / SL_DEN[(uint32_t)t->p[P_SLRATE] % 6u];
    sw = (uint32_t)clamp(t->p[P_SSWING] + song.g[G_SWING], 0, 100) * s->base / 200u;   /* as seq.c trk_grid */
    s->len = (s->idx & 1u) ? s->base - sw : s->base + sw;
    s->bit = (uint8_t)((sl_pattern(t) >> s->idx) & 1u);
    s->rp = 0;
    s->loop = 0;
    s->rec_on = 0;
    if (mode != SL_STUT) {
        s->rec = 0;                                 /* nothing old to repeat when STUT comes on */
    } else if (s->bit) {
        s->rec = 0;                                 /* a live step: record it */
        s->rec_on = 1;
        s->half_n = 0;
    } else if (s->rec >= 2u * SL_RAMP) {            /* a repeat: the last live step, looped */
        uint32_t l = s->base / (uint32_t)song.g[G_BPM];   /* (in samples) */
        while (l > 2u * SL_LEN)
            l >>= 1;                                /* (a long step: a half, a quarter .. of it) */
        s->loop = l < 2u * s->rec ? l : 2u * s->rec;
    }
}

/* m samples of one step (no boundary inside); left: samples to the step end at the first */
static void sl_seg(const track_t *t, sl_t *s, int16_t *buf, int32_t *b, uint32_t m, uint32_t left0)
{
    uint32_t mode = (uint32_t)t->p[P_SLCR], j, nbit = (sl_pattern(t) >> ((s->idx + 1u) & 15u)) & 1u;
    int32_t depth = t->p[P_SLDEPTH] * 258;          /* Q15, 0..32766 */
    int32_t tc = mode == SL_GATE && !s->bit ? depth : 0;           /* gate closure of this step */
    int32_t tn = mode == SL_GATE && !nbit ? depth : 0;             /* .. of the next one */
    int32_t tw = mode == SL_STUT && s->loop ? depth : 0;           /* repeat level */
    for (j = 0; j < m; j++) {
        int32_t x = b[j], y, tg = tc;
        uint32_t left = left0 - j;                  /* samples to the step end, >= 1 */
        if (left <= (uint32_t)SL_RAMP && tn > tg)
            tg = tn;                                /* close by the boundary */
        s->gc += clamp(tg - s->gc, -SL_SLOPE, SL_SLOPE);
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        y = s->gc ? x - mulq16(x, (uint32_t)s->gc << 1) : x;
        if (s->rec_on) {                            /* 2:1, the pair's mean; Q15 >> 2 (the mix's headroom) */
            if ((s->half_n++) & 1u) {
                buf[s->rec] = (int16_t)clamp((s->half + x) >> 3, -32768, 32767);
                if (++s->rec >= SL_LEN)
                    s->rec_on = 0;
            } else {
                s->half = x;
            }
        }
        if (s->loop) {                              /* the repeat, windowed, read between samples */
            uint32_t rp = s->rp, e = s->loop - 1u - rp;
            int32_t r = buf[rp >> 1];
            if (rp & 1u)
                r = (r + buf[((rp >> 1) + 1u) & (SL_LEN - 1u)]) >> 1;
            r <<= 2;
            if (rp < e)
                e = rp;
            if (left - 1u < e)
                e = left - 1u;
            if (e < (uint32_t)SL_RAMP)
                r = (r * (int32_t)e) >> SL_RAMP_LOG2;
            if (++s->rp >= s->loop)
                s->rp = 0;
            y += mulq16(r - y, (uint32_t)s->w << 1);
        } else if (s->w) {
            y -= mulq16(y, (uint32_t)s->w << 1);    /* (the repeat ended on a boundary: it is 0 there) */
        }
        b[j] = y;
    }
}

/* the step clock over n samples and the SLICER on b (0: the clock only, the track is silent) */
static void slicer_track(const track_t *t, int32_t *b, uint32_t n)
{
    uint32_t k = (uint32_t)(t - trk), i = 0, bpm = (uint32_t)song.g[G_BPM];
    sl_t *s = &sl[k];
    int act = b && (t->p[P_SLCR] != SL_OFF || s->gc || s->w);
    while (i < n) {
        uint32_t m, left;
        while (s->pos >= s->len)
            sl_enter(t, s);
        left = (s->len - s->pos + bpm - 1u) / bpm;  /* samples to the boundary */
        m = left < n - i ? left : n - i;
        if (act)
            sl_seg(t, s, sl_buf[k], b + i, m, left);
        s->pos += m * bpm;
        i += m;
    }
}

/* a silent part must still be rendered: a repeat is playing or fading (fx.c mix_part) */
static int slicer_busy(const track_t *t)
{
    const sl_t *s = &sl[t - trk];
    return s->w || (t->p[P_SLCR] == SL_STUT && s->loop);
}

/* the drum track: as drums_render, through the SLICER when it is on (or still fading) */
static void slicer_drums(int32_t *ml, int32_t *mr, int32_t *rev, int32_t *dly, uint32_t n)
{
    const track_t *t = TDRUM;
    const sl_t *s = &sl[TRK_DRUM];
    uint32_t i;
    if (t->p[P_SLCR] == SL_OFF && !s->gc && !s->w) {
        slicer_track(t, 0, n);
        drums_render(ml, mr, rev, dly, n);
        return;
    }
    for (i = 0; i < n; i++)
        sl_dbuf[i] = 0;
    drums_render_mono(sl_dbuf, n);
    slicer_track(t, sl_dbuf, n);
    {
        int32_t send = send_thr(song.g[G_DRREV] * 258, thr_rev), dsend = send_thr(song.g[G_DRDLY] * 258, thr_dly), pan = t->p[P_PAN];
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        for (i = 0; i < n; i++) {
            int32_t x = sl_dbuf[i];
            ml[i] += (x * gl) >> 12;
            mr[i] += (x * gr) >> 12;
            if (send)
                rev[i] += mulq15(x, send);
            if (dsend)
                dly[i] += mulq15(x, dsend);
        }
    }
}
