/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4: the full-screen visualiser. On HOME (the TRACKS screen), HOME tapped opens it; HOME again or
 * any page closes it; SELECT steps through the twelve styles (the name shows a second); a layer held shows
 * its screen as ever, then the visualiser comes back. The keys, PLAY and REC work as always.
 *   1 OSCILLOSCOPE  2 SPECTRUM  3 SPECTROGRAM  4 LISSAJOUS  5 VU METERS  6 CIRCLE
 *   7 TAPE  8 LCD  9 BOUNCE  10 ORBIT  11 WIRES  12 SLOOP (the logo: the sail's bands are the tracks)
 * It reads what the audio already leaves for the UI: the scope buffers (audio.c, the mix at 22 kHz, left and
 * right, as if MASTER were all the way up: the picture does not follow the volume knob, even at 0), the tracks' peaks (fx.c / drums.c, as the TRACKS meters read them), the notes that started
 * (voice.c vis_hit) and the transport clock. Everything is drawn in the main loop (scope every frame,
 * other styles every other frame), in two
 * bands of 120 rows (the canvas holds 124): no cost to the audio. */
#define VIS_N 12u
static const char *const VIS_NAME[VIS_N] = {"OSCILLOSCOPE", "SPECTRUM", "SPECTROGRAM", "LISSAJOUS", "VU METERS",
                                            "CIRCLE", "TAPE", "LCD", "BOUNCE", "ORBIT", "WIRES", "SLOOP"};
/* vis_on, vis_shown_last, vis_name_t: ui_draw.c; vis_style: panel.c (kept with the settings) */
#define VIS_FFT 512u
#define VIS_SG_W 48u                                /* spectrogram: 48 columns of 5 px, 72 rows of 3 px */
#define VIS_SG_H 72u
static int16_t vis_re[VIS_FFT] __attribute__((section(".pool"))), vis_im[VIS_FFT] __attribute__((section(".pool")));
#include "scratch.h"                                /* sloop-lab: this frame's scope snapshot, in the shared scratch */
#define vis_l (main_scratch.vis[0])
#define vis_r (main_scratch.vis[1])
_Static_assert(sizeof main_scratch.vis[0] == VIS_FFT * sizeof(int16_t), "the scratch holds the snapshot");
static uint8_t vis_sg[VIS_SG_H][VIS_SG_W] __attribute__((section(".pool")));
static uint8_t vis_lj[3][256][2];                   /* Lissajous: the last three frames' points */
static struct {
    int32_t scale, ljscale, peak;                   /* auto-scales (smoothed peaks), this frame's peak */
    int16_t bar[32], cap[32], capv[32];             /* spectrum: bars, caps and their fall speed (0..1000) */
    int16_t vu[5], pk[5], pkt[5];                   /* VU: tracks + master, peak holds */
    int16_t lvl[5];                                 /* the tracks' levels (0..1000) this frame, the master */
    int16_t bh[4], bv[4];                           /* BOUNCE: height and speed (1/256 units) */
    uint8_t flash[4], kick;                         /* frames since a note started (0 = none) / a kick, fading */
    int16_t wamp[4];                                /* WIRES: each string's swing (0..1000) */
    uint8_t wn[4];                                  /* its wave count (from the note) */
    uint32_t free_q8, last_ms, wph;                 /* beats (1/256) when stopped; the wires' phase */
    uint8_t lj_n;
} vs;
/* the spectrum's 32 bands: bins of the 512-point FFT at 22 050 Hz (43 Hz each), 40 Hz .. 10 kHz */
static const uint8_t VIS_EDGE[33] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 25, 29, 35,
                                     41, 49, 58, 69, 82, 98, 116, 138, 164, 195, 232};

static int vis_shown(void) { return vis_on && !ui.home && !ui.menu && cur_page()->scope == SC_TRK; }
static void vis_open(void)
{
    vis_on = 1;
    vis_name_t = 30;
    ui.force = 1;
}
static int32_t isin(uint32_t a) { return SINE[a & 1023u]; }          /* a: 1024 a turn; Q15 */
static int32_t icos(uint32_t a) { return SINE[(a + 256u) & 1023u]; }
static uint16_t mix565(uint16_t a, uint16_t b, int32_t k)              /* a -> b, k 0..256 */
{
    int32_t r = (a >> 11) + ((((b >> 11) - (a >> 11)) * k) >> 8);
    int32_t g = ((a >> 5) & 63) + (((((b >> 5) & 63) - ((a >> 5) & 63)) * k) >> 8);
    int32_t bl = (a & 31) + ((((b & 31) - (a & 31)) * k) >> 8);
    return (uint16_t)(r << 11 | g << 5 | bl);
}
/* |x| (Q15) -> 0..1000 over 48 dB (8 log2: 6 dB a step of 8) */
static int32_t vis_lvl(int32_t a)
{
    int32_t lg = 0, v;
    if (a < 0)
        a = -a;
    if (a < 16)
        return 0;
    while ((a >> lg) > 1)
        lg++;
    v = lg * 8 + (((a << 3) >> lg) & 7);           /* 120 = 0 dB */
    return clamp((v - 56) * 1000 / 64, 0, 1000);
}

/* ---- drawing helpers (canvas coordinates; cv_oy picks the band) ---- */
static void vis_ellipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)
{
    int32_t y;
    if (rx < 1 || ry < 1)
        return;
    for (y = -ry; y <= ry; y++) {
        int32_t w = 0;
        while (w <= rx && (w * w) * (ry * ry) + (y * y) * (rx * rx) <= (rx * rx) * (ry * ry))
            w++;
        if (w)
            cv_rect(cx - w + 1, cy + y, 2 * w - 1, 1, c);
    }
}
static void vis_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c)  /* an outline */
{
    int32_t x = r, y = 0, e = 1 - r;
    while (x >= y) {
        cv_pset(cx + x, cy + y, c), cv_pset(cx - x, cy + y, c), cv_pset(cx + x, cy - y, c), cv_pset(cx - x, cy - y, c);
        cv_pset(cx + y, cy + x, c), cv_pset(cx - y, cy + x, c), cv_pset(cx + y, cy - x, c), cv_pset(cx - y, cy - x, c);
        y++;
        if (e < 0)
            e += 2 * y + 1;
        else
            e += 2 * (y - --x) + 1;
    }
}
static void vis_thick(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* 2 px */
{
    cv_line(x0, y0, x1, y1, c);
    cv_line(x0, y0 + 1, x1, y1 + 1, c);
}
/* a polygon, points in 1/16 px (n <= 8), filled by scan lines (even-odd) inside the band */
static void vis_poly(const int32_t *px, const int32_t *py, uint32_t n, uint16_t c)
{
    int32_t y0 = 0x7FFFFFFF, y1 = -0x7FFFFFFF, y;
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (py[i] < y0) y0 = py[i];
        if (py[i] > y1) y1 = py[i];
    }
    y0 = y0 >> 4;
    y1 = (y1 + 15) >> 4;
    if (y0 < -cv_oy) y0 = -cv_oy;
    if (y1 > (int32_t)cv_h - cv_oy) y1 = (int32_t)cv_h - cv_oy;
    for (y = y0; y < y1; y++) {
        int32_t xs[8], m = 0, sy = y * 16 + 8, a, b;
        for (i = 0; i < n && m < 8; i++) {
            uint32_t j = (i + 1u) % n;
            if ((py[i] <= sy && py[j] > sy) || (py[j] <= sy && py[i] > sy))
                xs[m++] = px[i] + (sy - py[i]) * (px[j] - px[i]) / (py[j] - py[i]);
        }
        for (a = 1; a < m; a++)                     /* (a few points: insertion sort) */
            for (b = a; b > 0 && xs[b - 1] > xs[b]; b--) {
                int32_t t = xs[b]; xs[b] = xs[b - 1]; xs[b - 1] = t;
            }
        for (a = 0; a + 1 < m; a += 2)
            cv_rect((xs[a] + 8) >> 4, y, ((xs[a + 1] + 8) >> 4) - ((xs[a] + 8) >> 4), 1, c);
    }
}
/* a seven-segment digit, the unlit segments faintly there (an LCD) */
static void vis_seg(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, int ch, uint16_t c)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* gfedcba */
    uint32_t m = ch >= 0 && ch <= 9 ? SEG[ch] : 0u;
    int32_t hh = h / 2;
    uint16_t off = te_gray(RGB(18, 18, 21));
    cv_rect(x + t, y, w - 2 * t, t, m & 0x01 ? c : off);
    cv_rect(x + w - t, y + t, t, hh - t, m & 0x02 ? c : off);
    cv_rect(x + w - t, y + hh, t, hh - t, m & 0x04 ? c : off);
    cv_rect(x + t, y + h - t, w - 2 * t, t, m & 0x08 ? c : off);
    cv_rect(x, y + hh, t, hh - t, m & 0x10 ? c : off);
    cv_rect(x, y + t, t, hh - t, m & 0x20 ? c : off);
    cv_rect(x + t, y + hh - t / 2, w - 2 * t, t, m & 0x40 ? c : off);
}

/* ---- the frame's data ---- */
/* beats since PLAY in 1/256 (stopped: a free count at the tempo, for what moves anyway) */
static uint32_t vis_beat_q8(void)
{
    if (song.playing)
        return clk_beat * 256u + clk_pos / (BEAT_U / 256u);
    return vs.free_q8;
}
static void vis_fft(void)
{
    uint32_t i, j = 0, bit, len;
    for (i = 0; i < VIS_FFT; i++) {                 /* Hann window */
        int32_t w = (32768 - icos(i * 2u)) >> 1;
        vis_re[i] = (int16_t)((vis_l[i] * w) >> 15);
        vis_im[i] = 0;
    }
    for (i = 1; i < VIS_FFT; i++) {                 /* bit reversal */
        for (bit = VIS_FFT >> 1; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            int16_t t = vis_re[i]; vis_re[i] = vis_re[j]; vis_re[j] = t;
        }
    }
    for (len = 2; len <= VIS_FFT; len <<= 1) {      /* radix 2, halved each stage (no overflow) */
        uint32_t half = len >> 1, k;
        for (i = 0; i < VIS_FFT; i += len)
            for (k = 0; k < half; k++) {
                uint32_t a = i + k, b = a + half, ix = k * (1024u / len);
                int32_t wr = icos(ix), wi = -isin(ix);
                int32_t tr = (vis_re[b] * wr - vis_im[b] * wi) >> 15, ti = (vis_re[b] * wi + vis_im[b] * wr) >> 15;
                int32_t ar = vis_re[a], ai = vis_im[a];
                vis_re[b] = (int16_t)((ar - tr) >> 1);
                vis_im[b] = (int16_t)((ai - ti) >> 1);
                vis_re[a] = (int16_t)((ar + tr) >> 1);
                vis_im[a] = (int16_t)((ai + ti) >> 1);
            }
    }
}
/* the 32 bands of the last FFT, 0..1000 (60 dB under a full-scale sine) */
static void vis_bands(int16_t *out)
{
    uint32_t k, i;
    for (k = 0; k < 32u; k++) {
        int32_t m = 0, lg = 0, v;
        for (i = VIS_EDGE[k]; i < VIS_EDGE[k + 1] && i < VIS_FFT / 2u; i++) {
            int32_t a = vis_re[i] < 0 ? -vis_re[i] : vis_re[i], b = vis_im[i] < 0 ? -vis_im[i] : vis_im[i];
            int32_t mag = a > b ? a + b / 2 : b + a / 2;
            if (mag > m)
                m = mag;
        }
        if (m < 2) {
            out[k] = 0;
            continue;
        }
        while ((m >> lg) > 1)
            lg++;
        v = lg * 8 + (((m << 3) >> lg) & 7);        /* a full-scale sine: ~104 */
        out[k] = (int16_t)clamp((v - 24) * 1000 / 80, 0, 1000);
    }
}

static void vis_update(void)
{
    uint32_t i, w, hit, now = fm1_ms;
    int32_t pk[4], mpk = 0;
    /* the scope: the last 512 frames (left, right) */
    w = scope_w;
    for (i = 0; i < VIS_FFT; i++) {
        vis_l[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        vis_r[i] = scope_bufr[(w + i) & (SCOPE_N - 1u)];
        if (vis_l[i] > mpk) mpk = vis_l[i];
        if (-vis_l[i] > mpk) mpk = -vis_l[i];
    }
    /* the tracks' peaks since the last look (as the TRACKS meters take them) and the notes that started */
    fm1_irq_off();
    for (i = 0; i < NPART; i++) {
        pk[i] = trk[i].peak;
        trk[i].peak = 0;
    }
    pk[3] = drums.peak;
    drums.peak = 0;
    hit = vis_hit;
    vis_hit = 0;
    if (vis_kick_hit) {
        vis_kick_hit = 0;
        vs.kick = 255;
    }
    fm1_irq_on();
    for (i = 0; i < 4u; i++) {
        int32_t v = trk_level(i) && !trk[i].p[P_MUTE] ? vis_lvl(pk[i]) : 0;   /* (as the TRACKS meters) */
        vs.lvl[i] = (int16_t)v;
        if ((hit >> i) & 1u) {
            vs.flash[i] = 8;
            vs.wn[i] = (uint8_t)(2u + vis_note[i] % 12u / 2u);
            if (vs.wamp[i] < 1000) vs.wamp[i] = 1000;
            if (vs.bh[i] < 8 * 256)                 /* BOUNCE: a kick, unless already high up */
                vs.bv[i] = (int16_t)(3 * 256 + 51 * (int32_t)i);
        } else if (vs.flash[i]) {
            vs.flash[i]--;
        }
        vs.wamp[i] = (int16_t)(vs.wamp[i] > v ? vs.wamp[i] - (vs.wamp[i] - v) / 6 - 4 : v);
        if (vs.wamp[i] < 0) vs.wamp[i] = 0;
        /* BOUNCE physics: gravity, a bounce off the floor */
        vs.bv[i] = (int16_t)(vs.bv[i] - 56);
        vs.bh[i] = (int16_t)clamp(vs.bh[i] + vs.bv[i], 0, 34 * 256);
        if (!vs.bh[i] && vs.bv[i] < 0)
            vs.bv[i] = (int16_t)(vs.bv[i] < -256 ? -vs.bv[i] * 35 / 100 : 0);
    }
    vs.lvl[4] = (int16_t)vis_lvl(mpk);
    vs.kick = (uint8_t)(vs.kick > 20 ? vs.kick - 20 : 0);
    /* the free beat count (stopped) */
    {
        uint32_t dt = now - vs.last_ms;
        vs.last_ms = now;
        if (dt > 200u) dt = 200u;
        vs.free_q8 += dt * (uint32_t)song.g[G_BPM] * 256u / 60000u;
    }
    vs.wph += 37u;
    vs.peak = mpk;
    {                                               /* the wave's scale follows the peak */
        int32_t target = mpk < 1600 ? 1600 : mpk;
        vs.scale += (target - vs.scale) / 5;
    }
    switch (vis_style) {
    case 1: case 2: {
        int16_t b[32];
        vis_fft();
        vis_bands(b);
        if (vis_style == 1) {
            for (i = 0; i < 32u; i++) {
                int32_t bar = vs.bar[i] - vs.bar[i] * 18 / 100;
                vs.bar[i] = (int16_t)(b[i] > bar ? b[i] : bar);
                if (vs.bar[i] >= vs.cap[i]) {
                    vs.cap[i] = vs.bar[i];
                    vs.capv[i] = 0;
                } else {
                    vs.capv[i] = (int16_t)(vs.capv[i] + 4);
                    vs.cap[i] = (int16_t)(vs.cap[i] > vs.capv[i] ? vs.cap[i] - vs.capv[i] : 0);
                }
            }
        } else {                                    /* spectrogram: the new row on top */
            for (i = VIS_SG_H - 1u; i; i--)            /* (no memmove here: row by row, from the bottom) */
                memcpy(vis_sg[i], vis_sg[i - 1u], VIS_SG_W);
            for (i = 0; i < VIS_SG_W; i++) {
                uint32_t p = i * 31u * 16u / (VIS_SG_W - 1u), k = p >> 4, f = p & 15u;
                int32_t v = k < 31u ? (b[k] * (int32_t)(16u - f) + b[k + 1u] * (int32_t)f) / 16 : b[31];
                vis_sg[0][i] = (uint8_t)(v * 255 / 1000);
            }
        }
        break;
    }
    case 3: {                                       /* Lissajous: L-R across, L+R up; three frames kept */
        int32_t m = 1;
        uint8_t (*p)[2];
        for (i = 0; i < VIS_FFT; i += 2u) {
            int32_t s = vis_l[i] + vis_r[i];
            if (s < 0) s = -s;
            if (s > m) m = s;
        }
        if (m < 3000) m = 3000;
        vs.ljscale += (m - vs.ljscale) / 5;
        if (vs.ljscale < 1) vs.ljscale = 1;
        vs.lj_n = (uint8_t)((vs.lj_n + 1u) % 3u);
        p = vis_lj[vs.lj_n];
        for (i = 0; i < 256u; i++) {
            int32_t l = vis_l[i * 2u], r = vis_r[i * 2u];
            p[i][0] = (uint8_t)clamp(120 + (l - r) * 170 / vs.ljscale, 0, 239);
            p[i][1] = (uint8_t)clamp(120 - (l + r) * 95 / vs.ljscale, 0, 239);
        }
        break;
    }
    case 4:                                         /* VU: fall 3 % a frame, peaks held a second */
        for (i = 0; i < 5u; i++) {
            int32_t v = vs.lvl[i];
            vs.vu[i] = (int16_t)(v > vs.vu[i] - 30 ? v : vs.vu[i] - 30);
            if (v >= vs.pk[i]) {
                vs.pk[i] = (int16_t)v;
                vs.pkt[i] = 0;
            } else if (++vs.pkt[i] > 30) {
                vs.pk[i] = (int16_t)(vs.pk[i] > 20 ? vs.pk[i] - 20 : 0);
            }
        }
        break;
    case 11:                                        /* SLOOP: the sail's bands and the dial fall slowly */
        for (i = 0; i < 5u; i++)
            vs.vu[i] = (int16_t)(vs.lvl[i] > vs.vu[i] - 30 ? vs.lvl[i] : vs.vu[i] - 30);
        break;
    default:
        break;
    }
}

/* ---- the styles (each draws the whole screen; called once per band) ---- */
static void vis_scope(void)
{
    uint32_t i0 = 0, i;
    int32_t py = 0;
    for (i = 1; i + 240u < VIS_FFT; i++)            /* a rising zero crossing: the wave stands still */
        if (vis_l[i - 1u] < 0 && vis_l[i] >= 0) {
            i0 = i;
            break;
        }
    cv_rect(0, 120, 240, 1, C_LINE);
    for (i = 0; i < 240u; i += 40u)
        cv_rect((int32_t)i, 116, 1, 9, C_LINE);
    for (i = 0; i < 240u; i++) {
        int32_t y = 120 - vis_l[i0 + i] * 100 / (vs.scale ? vs.scale : 1);
        y = clamp(y, 2, 237);
        if (i) {
            cv_line((int32_t)i - 1, py - 1, (int32_t)i, y - 1, TE_DIM[0]);
            cv_line((int32_t)i - 1, py + 2, (int32_t)i, y + 2, TE_DIM[0]);
            vis_thick((int32_t)i - 1, py, (int32_t)i, y, TE_COL[0]);
        }
        py = y;
    }
}
static void vis_spectrum(void)
{
    uint32_t k;
    for (k = 0; k < 32u; k++) {
        int32_t x0 = 4 + (int32_t)k * 7, h = vs.bar[k] * 210 / 1000, y, cy = 232 - vs.cap[k] * 210 / 1000;
        for (y = 232; y > 232 - h; y -= 4)          /* LED segments */
            cv_rect(x0, y - 2, 5, 3, TE_COL[k / 8u]);
        cv_rect(x0, cy - 1, 5, 2, C_WHITE);
    }
}
static uint16_t vis_heat(uint32_t v)               /* 0..255: black, blue, green, yellow, white */
{
    static const uint16_t ST[5] = {RGB(0, 0, 0), RGB(40, 124, 255), RGB(30, 204, 112), RGB(255, 198, 24), RGB(255, 255, 255)};
    static const uint8_t AT[5] = {0, 77, 140, 204, 255};
    uint32_t k;
    v = (v + ((v * v) >> 8)) >> 1;                  /* (quiet parts a little darker: about v^1.4) */
    for (k = 1; k < 5u && v > AT[k]; k++)
        ;
    if (k >= 5u)
        return ST[4];
    return mix565(ST[k - 1u], ST[k], (int32_t)((v - AT[k - 1u]) * 256u / (AT[k] - AT[k - 1u])));
}
static void vis_spectrogram(void)
{
    uint32_t r, c;
    for (r = 0; r < VIS_SG_H; r++) {
        int32_t y = 24 + (int32_t)r * 3;
        if (y + 3 <= -cv_oy || y >= (int32_t)cv_h - cv_oy)
            continue;
        for (c = 0; c < VIS_SG_W; c++)
            if (vis_sg[r][c] > 8u)
                cv_rect((int32_t)c * 5, y, 5, 3, vis_heat(vis_sg[r][c]));
    }
    cv_text(2, 2, &FONT_S, "40", C_DIM);
    cv_text(30, 2, &FONT_S, "200", C_DIM);
    cv_text(132, 2, &FONT_S, "1k", C_DIM);
    cv_text(196, 2, &FONT_S, "4k", C_DIM);
}
static void vis_lissajous(void)
{
    uint32_t f, i;
    static const uint16_t COLS[3] = {RGB(10, 66, 38), RGB(20, 136, 76), RGB(30, 204, 112)};
    cv_rect(120, 30, 1, 180, C_LINE);
    cv_rect(30, 120, 180, 1, C_LINE);
    for (f = 0; f < 3u; f++) {                      /* the oldest first, dimmest */
        const uint8_t (*p)[2] = vis_lj[(vs.lj_n + 1u + f) % 3u];
        for (i = 0; i < 256u; i++)
            cv_pset(p[i][0], p[i][1], COLS[f]);
    }
    cv_text(116, 4, &FONT_S, "M", C_DIM);
    cv_text(6, 112, &FONT_S, "L", C_DIM);
    cv_text(226, 112, &FONT_S, "R", C_DIM);
}
static void vis_meters(void)
{
    uint32_t i, k;
    for (i = 0; i < 5u; i++) {
        int32_t x0 = 10 + (int32_t)i * 46, on = vs.vu[i] * 24 / 1000, pk = vs.pk[i] * 24 / 1000;
        uint16_t c = i < 4u ? TE_COL[i] : C_WHITE;
        char lab[2] = {(char)(i < 4u ? '1' + i : 'M'), 0};
        for (k = 0; k < 24u; k++) {
            int32_t y = 206 - (int32_t)k * 8;
            uint16_t col = (int32_t)k < on ? (k < 18u ? c : k < 22u ? TE_COL[2] : RGB(255, 44, 52)) : TE_G1;
            if ((int32_t)k == pk && pk > 0)
                col = C_WHITE;
            cv_rect(x0, y - 5, 35, 6, col);
        }
        cv_text(x0 + 13, 216, &FONT_S, lab, c);
    }
}
static void vis_ring(void)
{
    int32_t k = vs.kick, r0 = 52 + k * 18 / 255, rc = r0 * 6 / 10, sc = vs.peak > vs.scale ? vs.peak : vs.scale;
    int32_t px = 0, py = 0, i;
    vis_ellipse(120, 120, rc, rc, RGB(40 + 140 * k / 255, 14 + 50 * k / 255, 4));
    for (i = 0; i <= 180; i++) {                    /* the wave round the ring, 2 degrees a point */
        uint32_t a = (uint32_t)(i % 180) * 1024u / 180u, j = 20u + (uint32_t)(i % 180) * 2u;
        int32_t s = (vis_l[j - 2u] + vis_l[j - 1u] + vis_l[j] + vis_l[j + 1u] + vis_l[j + 2u]) / 5;
        int32_t rr = r0 + s * 20 / (sc > 1600 ? sc : 1600), x = 120 + ((icos(a) * rr) >> 15), y = 120 + ((isin(a) * rr) >> 15);
        if (i) {
            cv_line(px, py - 1, x, y - 1, RGB(120, 40, 10));
            cv_line(px, py + 2, x, y + 2, RGB(120, 40, 10));
            vis_thick(px, py, x, y, TE_COL[3]);
        }
        px = x, py = y;
    }
}
static void vis_tape(void)
{
    uint32_t bq = vis_beat_q8(), s;
    int32_t prog = (int32_t)((song.playing ? clk_beat * 256u + clk_pos / (BEAT_U / 256u) : 0u) % (118u * 256u)) * 1000 /
                   (118 * 256);                     /* the tape runs one reel to the other in a minute or so */
    char b[12];
    for (s = 0; s < 2u; s++) {
        int32_t cx = s ? 176 : 64, fill = s ? prog : 1000 - prog, rr = 26 + fill * 22 / 1000, k;
        vis_ellipse(cx, 110, rr, rr, te_gray(RGB(40, 40, 46)));
        vis_circle(cx, 110, 22, C_WHITE);
        vis_circle(cx, 110, 21, C_WHITE);
        for (k = 0; k < 3; k++) {                   /* three spokes, a quarter turn a beat (playing) */
            uint32_t a = (song.playing ? bq : 0u) + (uint32_t)k * 341u;
            vis_thick(cx, 110, cx + ((icos(a) * 20) >> 15), 110 + ((isin(a) * 20) >> 15), C_WHITE);
        }
        vis_ellipse(cx, 110, 5, 5, TE_COL[3]);
    }
    cv_line(60, 158, 120, 196, C_DIM);
    cv_line(120, 196, 180, 158, C_DIM);
    cv_rect(104, 190, 33, 17, TE_COL[3]);           /* the head, the level passing it */
    for (s = 0; s < 9u; s++) {
        int32_t d = (int32_t)s - 4, h = vs.lvl[4] * 30 * (5 - (d < 0 ? -d : d)) / 5000;
        cv_rect(84 + (int32_t)s * 8, 186 - h, 5, h, s == 4u ? TE_COL[3] : C_HI);
    }
    s = song.playing ? clk_beat : 0u;
    b[0] = (char)('0' + (s / 4u + 1u) / 100u % 10u);
    b[1] = (char)('0' + (s / 4u + 1u) / 10u % 10u);
    b[2] = (char)('0' + (s / 4u + 1u) % 10u);
    b[3] = '.';
    b[4] = (char)('1' + s % 4u);
    b[5] = 0;
    cv_text(8, 24, &FONT_S, b, C_WHITE);
    fmt_int(b, song.g[G_BPM]);
    cv_text(232 - text_w(&FONT_S, b), 24, &FONT_S, b, TE_COL[3]);
    cv_text(8, 220, &FONT_S, "tape", C_DIM);
}
static void vis_lcd(void)
{
    uint32_t bpm = (uint32_t)song.g[G_BPM], beat = song.playing ? clk_beat : 0u, bar = beat / 4u + 1u, k, i;
    vis_seg(18, 30, 44, 76, 7, bpm >= 100u ? (int)(bpm / 100u) : -1, C_WHITE);
    vis_seg(74, 30, 44, 76, 7, (int)(bpm / 10u % 10u), C_WHITE);
    vis_seg(130, 30, 44, 76, 7, (int)(bpm % 10u), C_WHITE);
    cv_text(190, 90, &FONT_S, "bpm", C_DIM);
    vis_seg(18, 124, 26, 46, 5, bar >= 10u ? (int)(bar / 10u % 10u) : -1, TE_COL[3]);
    vis_seg(52, 124, 26, 46, 5, (int)(bar % 10u), TE_COL[3]);
    cv_rect(88, 164, 5, 6, TE_COL[3]);
    vis_seg(100, 124, 26, 46, 5, (int)(beat % 4u + 1u), TE_COL[3]);
    for (k = 0; k < 4u; k++)
        vis_ellipse(154 + (int32_t)k * 22, 144, 8, 8, song.playing && beat % 4u == k ? TE_COL[3] : te_gray(RGB(40, 40, 46)));
    for (i = 0; i < 4u; i++)
        for (k = 0; k < 14u; k++)
            cv_rect(18 + (int32_t)k * 15, 186 + (int32_t)i * 12, 13, 9,
                    (int32_t)k < vs.lvl[i] * 14 / 1000 + (vs.lvl[i] > 0) ? TE_COL[i] : te_gray(RGB(30, 30, 34)));
}
static void vis_bounce(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        int32_t x = 36 + (int32_t)i * 56, h = vs.bh[i], y = 196 - h * 45 / 2560, sh = 16 - h / 512;
        int32_t sq = h < 512 && vs.bv[i] < 0 ? 4 : 0;   /* squashed as it lands */
        vis_ellipse(x, 205, sh < 4 ? 4 : sh, 3, te_gray(RGB(30, 30, 34)));
        vis_ellipse(x, y, 16 + sq, 16 - sq, TE_COL[i]);
        {
            char lab[2] = {(char)('1' + i), 0};
            cv_text(x - 4, 220, &FONT_S, lab, TE_COL[i]);
        }
    }
    cv_rect(8, 212, 224, 2, C_LINE);
}
static void vis_orbit(void)
{
    static const uint8_t RAD[4] = {42, 62, 82, 102}, PER[4] = {1, 2, 4, 8};
    uint32_t bq = vis_beat_q8(), i, k;
    int32_t rs = 14 + vs.lvl[4] * 22 / 1000;
    for (i = 0; i < 4u; i++) {
        uint32_t a = (bq * 4u / PER[i]) - 256u;     /* a turn = 1024: per beats, from the top */
        int32_t pr = 4 + vs.lvl[i] * 8 / 1000;
        vis_circle(120, 120, RAD[i], te_gray(RGB(36, 36, 42)));
        for (k = 1; k < 7u; k++) {                  /* its trail */
            uint32_t ak = a - k * 13u;
            vis_ellipse(120 + ((icos(ak) * RAD[i]) >> 15), 120 + ((isin(ak) * RAD[i]) >> 15), 2, 2,
                        mix565(TE_COL[i], 0, (int32_t)(60u + k * 196u / 7u)));
        }
        vis_ellipse(120 + ((icos(a) * RAD[i]) >> 15), 120 + ((isin(a) * RAD[i]) >> 15), pr, pr, TE_COL[i]);
    }
    vis_ellipse(120, 120, rs, rs, C_WHITE);
}
static void vis_wires(void)
{
    uint32_t i, x;
    for (i = 0; i < 4u; i++) {
        int32_t y0 = 40 + (int32_t)i * 52, amp = vs.wamp[i] * 22 / 1000, py = y0;
        char lab[2] = {(char)('1' + i), 0};
        for (x = 0; x < 240u; x += 2u) {           /* a standing wave: the string's shape x its swing */
            uint32_t a = x * 512u / 240u, cyc = i == 3u ? 8u : vs.wn[i] ? vs.wn[i] : 2u;   /* sin(pi x / L) */
            int32_t env = isin(a), wv = isin(x * cyc * 1024u / 240u + vs.wph * (3u + i));
            int32_t y = y0 - (((env * wv) >> 15) * amp >> 15);
            if (x)
                vis_thick((int32_t)x - 2, py, (int32_t)x, y, TE_COL[i]);
            py = y;
        }
        vis_ellipse(4, y0, 4, 4, C_WHITE);
        vis_ellipse(236, y0, 4, 4, C_WHITE);
        cv_text(10, y0 - 22, &FONT_S, lab, TE_COL[i]);
    }
}
/* the logo (tools/gen_logo.py at 0.86, centred on 120, 112), alive: the dial is the master, the sail's four
 * bands the tracks (growing with their level, flashing on a note), the boat rocks on the master's wave */
static void vis_sloop(void)
{
    static const int16_t BAND[4][4] = {{691, 1035, 1659, 1938}, {1118, 1462, 2006, 2285}, {1544, 1888, 2353, 2632},
                                       {1971, 2315, 2700, 2980}};   /* y0, y1, x right at y0, at y1 (1/16 px) */
    static const uint16_t DIMC[4] = {RGB(9, 27, 56), RGB(7, 45, 25), RGB(56, 44, 5), RGB(56, 22, 6)};
    uint32_t bq = vis_beat_q8(), i;
    int32_t ang = (isin(bq) * 11) >> 15;            /* +-4 degrees (1024 a turn), a sway every four beats */
    int32_t sn = isin((uint32_t)ang), cs = icos((uint32_t)ang);
    int32_t px[4], py[4], x, sc = vs.scale > 1600 ? vs.scale : 1600, end;
#define ROT(X, Y, I) do { int32_t dx_ = (X) - 120 * 16, dy_ = (Y) - 170 * 16;                       \
        px[I] = 120 * 16 + ((dx_ * cs - dy_ * sn) >> 15); py[I] = 170 * 16 + ((dx_ * sn + dy_ * cs) >> 15); } while (0)
    /* the dial: 270 degrees, lit to the master's level */
    end = vs.vu[4] * 768 / 1000;
    for (x = 0; x <= 768; x += 6) {
        uint32_t a = (uint32_t)(384 + x) & 1023u;
        vis_ellipse(120 + ((icos(a) * 99) >> 15), 112 + ((isin(a) * 99) >> 15), 5, 5, x <= end && vs.vu[4] ? C_WHITE : te_gray(RGB(40, 40, 46)));
    }
    /* the mast */
    ROT(1443, 627, 0); ROT(1571, 627, 1); ROT(1571, 2507, 2); ROT(1443, 2507, 3);
    vis_poly(px, py, 4, C_WHITE);
    /* the sail: each band dim, then lit to its track's level */
    for (i = 0; i < 4u; i++) {
        int32_t g = 256 * (250 + 750 * vs.vu[i] / 1000) / 1000, xr0 = 1659 + (BAND[i][2] - 1659) * g / 256,
                xr1 = 1659 + (BAND[i][3] - 1659) * g / 256;
        uint16_t col = vs.flash[i] ? mix565(TE_COL[i], C_WHITE, vs.flash[i] * 13) : TE_COL[i];
        ROT(1659, BAND[i][0], 0); ROT(BAND[i][2], BAND[i][0], 1); ROT(BAND[i][3], BAND[i][1], 2); ROT(1659, BAND[i][1], 3);
        vis_poly(px, py, 4, DIMC[i]);
        ROT(1659, BAND[i][0], 0); ROT(xr0, BAND[i][0], 1); ROT(xr1, BAND[i][1], 2); ROT(1659, BAND[i][1], 3);
        vis_poly(px, py, 4, col);
    }
    /* the hull */
    ROT(62 * 16, 2635, 0); ROT(178 * 16, 2635, 1); ROT(170 * 16, 2811, 2); ROT(70 * 16, 2811, 3);
    vis_poly(px, py, 4, C_WHITE);
#undef ROT
    /* the sea: the master's wave */
    for (x = 0; x < 240; x++) {
        int32_t s = x ? (vis_l[x - 1] + 2 * vis_l[x] + vis_l[x + 1]) / 4 : vis_l[0];   /* (smoothed: a sea, not a hiss) */
        int32_t y = 182 + s * 5 / sc + ((isin((uint32_t)(x * 9) + bq) * 3) >> 15);
        cv_rect(x, y, 1, 240 - y, RGB(12, 34, 76));
        cv_rect(x, y, 1, 2, TE_COL[0]);
    }
}

/* The scope follows every UI tick; heavier styles keep the half-rate budget.
 * Each band is fully composed before transfer. Never clear the panel first:
 * forced redraws (including live input) would expose a black frame. */
static void vis_draw(void)
{
    uint32_t pass;
    if (vis_shown_last && !ui.force && vis_style % VIS_N != 0u && (ui.frame & 1u)) {
        return;
    }
    vis_shown_last = 1;
    vis_update();
    for (pass = 0; pass < 2u; pass++) {
        cv_begin(240, 120, C_BLACK);
        cv_oy = -(int32_t)(pass * 120u);
        switch (vis_style % VIS_N) {
        case 0: vis_scope(); break;
        case 1: vis_spectrum(); break;
        case 2: vis_spectrogram(); break;
        case 3: vis_lissajous(); break;
        case 4: vis_meters(); break;
        case 5: vis_ring(); break;
        case 6: vis_tape(); break;
        case 7: vis_lcd(); break;
        case 8: vis_bounce(); break;
        case 9: vis_orbit(); break;
        case 10: vis_wires(); break;
        default: vis_sloop(); break;
        }
        if (!pass && (vis_name_t || ui.msg_t)) {    /* the style's name a second (or a message) */
            char n[8];
            const char *s = ui.msg_t ? ui.msg : VIS_NAME[vis_style % VIS_N];
            cv_rect(0, 0, 240, 20, C_BLACK);
            cv_text(120 - text_w(&FONT_S, s) / 2, 2, &FONT_S, s, C_WHITE);
            if (!ui.msg_t) {
                fmt_int(n, (int32_t)(vis_style % VIS_N) + 1);
                str_cpy(n + str_len(n), "/12", 4);
                cv_text(4, 2, &FONT_S, n, C_DIM);
            }
        }
        cv_oy = 0;
        cv_blit(0, pass * 120u);
    }
    if (vis_name_t && !(ui.frame & 1u))
        vis_name_t--;
}
/* SELECT on the visualiser: the next / previous style (round) */
static void vis_select(int32_t s)
{
    vis_style = (uint8_t)((vis_style + (s > 0 ? 1u : VIS_N - 1u)) % VIS_N);
    vis_name_t = 30;
    ui.force = 1;
    settings_later = 1;                                 /* (kept with the settings, once stopped) */
}
