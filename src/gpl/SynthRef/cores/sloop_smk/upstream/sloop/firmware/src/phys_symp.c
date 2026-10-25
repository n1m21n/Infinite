/* SPDX-License-Identifier: MIT
 * Copyright (c) 2015 Emilie Gillet
 * Ported to fixed-point C for Felucca, 2026: Leo Kuroshita (@kurogedelic), Hügelton Instruments
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/* Sympathetic strings after Rings (Emilie Gillet, MIT; rings/dsp/part.cc RenderStringVoice and
 * string.cc), in fixed point, for the PHYS engine's SYMP model (eng_phys.c). The plucked main string is
 * phys_dsp.c's (DaisySP's String, itself from Rings); its output drives a few more strings tuned to other
 * notes, which ring on by resonance. As in Rings: the sympathetic strings get the main string's output
 * scaled down as their only input, a brightness of b (2 - b) twice (brighter than the main string's) and a
 * long decay (RT60 0.07 s 2^(8 d (2 - d)) for a damping d above the main string's: 0.25 + 0.72 DAMP, 0.8 s
 * .. 11 s; Rings: 0.7 + 0.27), and the voice's output is the sum of all the strings.
 *
 * Departures: three sympathetic strings (Rings: up to seven, with detuned copies) on chords picked by the
 * caller (Rings: an interpolated table, or its quantised chord table); each is a plain delay loop (a
 * 256-sample line of 16-bit values (Q12), linear interpolation, a two-tap FIR loop filter b x + (1 - b) x', the
 * loop gain of its RT60) instead of Rings' String with its dispersion, IIR damping filter and LFO-moved
 * pickup position; a string whose period exceeds the line sounds an octave (or two) higher. */
#define PX_YLINE 256             /* sympathetic string line (16-bit) at half the rate: 86 Hz and up, lower folds up */
#define PX_YMASK (PX_YLINE - 1)
#define PX_NSYMP 3

typedef struct {                                         /* coefficients (px_symp_block; kept while unchanged) */
    uint32_t d[PX_NSYMP];                                /* read delay, whole samples */
    int32_t h[PX_NSYMP][3];                              /* the loop: taps d, d + 1, d + 2 (Q24): the loop gain, the
                                                          * FIR b x + (1 - b) x' and the linear interpolation in one */
    int32_t cin;                                         /* the main string into them, Q16 */
    uint32_t on;                                         /* strings on (bits) */
} px_symp_cf_t;

typedef struct {
    px_string_t s;                                       /* the main string (phys_dsp.c) */
    int16_t line[PX_NSYMP][PX_YLINE];                    /* the sympathetic strings, Q12 (+-8) */
    int32_t dcx, dcy, dce;                               /* the DC blocker on their input (Rings: part.cc), its
                                                          * rounding error fed back (as DcBlock) */
    uint32_t wp;
    int32_t yl;                                          /* the strings' last sum (half rate), Q12 */
    int32_t key[6];                                      /* the inputs cf was worked out for */
    px_symp_cf_t cf;
} px_symp_t;

/* the coefficients, when an input changed. f[k]: the sympathetic strings' frequencies (cycles Q32, 0 = off); brightness, damping, amount Q16 0..1 */
static __attribute__((noinline)) void px_symp_block(px_symp_t *Y, const uint32_t *f, int32_t brightness, int32_t damping,
                                                    int32_t amount)
{
    px_symp_cf_t *B = &Y->cf;
    int32_t b = px_m(brightness, 131072 - brightness, 16), dd = 16384 + px_m(damping, 47186, 16), lfd, k8;
    uint32_t k;
    if (Y->key[0] == (int32_t)f[0] && Y->key[1] == (int32_t)f[1] && Y->key[2] == (int32_t)f[2] && Y->key[3] == brightness &&
        Y->key[4] == damping && Y->key[5] == amount)
        return;
    Y->key[0] = (int32_t)f[0];
    Y->key[1] = (int32_t)f[1];
    Y->key[2] = (int32_t)f[2];
    Y->key[3] = brightness;
    Y->key[4] = damping;
    Y->key[5] = amount;
    b = px_m(b, 131072 - b, 16);                         /* b (2 - b), twice */
    b = 32768 + (b >> 1);                                /* the FIR: 0.5 .. 1 */
    lfd = px_m(dd, 131072 - dd, 16);
    k8 = (int32_t)px_exp2(-8 * lfd);                     /* 2^(-8 lfd): 0.07 s / RT60 */
    B->cin = amount >> 4;                                /* at most 1 / 16 (of a pair: 1 / 8) */
    B->on = 0;
    for (k = 0; k < PX_NSYMP; k++) {
        uint32_t p, e;
        int32_t c, fr;
        if (f[k] < 65536u)
            continue;
        p = f[k] >= 0x40000000u ? 0 : (uint32_t)(((uint64_t)1 << 47) / f[k]);   /* period, half-rate steps Q16 */
        while (p > (uint32_t)(PX_YLINE - 4) << 16)       /* fold up an octave */
            p >>= 1;
        p -= (uint32_t)(65536 - b);                      /* less the FIR's delay */
        if (p < 2u << 16)
            continue;
        /* gain per period 2^(-10 period / RT60) = 2^(-10 p / (1543.5 steps) * k8) */
        e = (uint32_t)(((((uint64_t)p * 27826874u) >> 32) * (uint32_t)k8) >> 16);   /* octaves, Q16 (10 / 1543.5: Q32) */
        c = e > 15u << 16 ? 0 : (int32_t)(px_exp2(-(int32_t)e) << 8);   /* Q24 */
        if (c > 16775500)
            c = 16775500;                                /* 0.9999 */
        fr = (int32_t)(p & 0xFFFFu);
        B->d[k] = p >> 16;
        B->h[k][0] = px_m(c, px_m(b, 65536 - fr, 16), 16);
        B->h[k][1] = px_m(c, px_m(b, fr, 16) + px_m(65536 - b, 65536 - fr, 16), 16);
        B->h[k][2] = px_m(c, px_m(65536 - b, fr, 16), 16);
        B->on |= 1u << k;
    }
}

/* n (<= 32, even) samples: out[i] = in[i] (the main string) + the sympathetic strings it drives. They run at half
 * the rate (the input summed over each pair, the output interpolated linearly), a string at a time over the
 * block (its line is read at least a step back: the order is kept). The loop truncates towards 0: rounding
 * leaves a dead band in which a small value never decays (a limit cycle), a floor a bias that rings at DC
 * (a comb resonates there too) */
static __attribute__((noinline)) void px_symp_run(px_symp_t *Y, const int32_t *in, int32_t *out, uint32_t n)
{
    const px_symp_cf_t *B = &Y->cf;
    uint32_t i, k, m;
    int32_t dcx = Y->dcx, dcy = Y->dcy, dce = Y->dce, x[16], acc[16], yl = Y->yl;
    if (n > 32u)
        n = 32u;
    m = n >> 1;
    for (i = 0; i < m; i++) {                            /* their input: the string, a pair summed, less its DC */
        int32_t v = px_m(in[2 * i] + in[2 * i + 1], B->cin, 16);   /* (~10 Hz), Q12 */
        int64_t a = (int64_t)1070617000 * dcy + dce;
        dcy = v - dcx + (int32_t)(a >> 30);
        dce = (int32_t)(a & 0x3FFFFFFF);
        dcx = v;
        x[i] = (dcy + 128) >> 8;
        acc[i] = 0;
    }
    for (k = 0; k < PX_NSYMP; k++) {
        int16_t *l = Y->line[k];
        uint32_t wp = Y->wp, d = B->d[k];
        int32_t h0 = B->h[k][0], h1 = B->h[k][1], h2 = B->h[k][2];
        if (!(B->on >> k & 1u))
            continue;
        for (i = 0; i < m; i++) {
            uint32_t t = wp + d;
            int64_t s = (int64_t)h0 * l[t & PX_YMASK] + (int64_t)h1 * l[(t + 1u) & PX_YMASK] +
                        (int64_t)h2 * l[(t + 2u) & PX_YMASK];
            int32_t y = px_clamp((int32_t)((s + ((s >> 63) & 0xFFFFFF)) >> 24) + x[i], -32767, 32767);
            l[wp] = (int16_t)y;
            acc[i] += y;
            wp = (wp - 1u) & PX_YMASK;
        }
    }
    for (i = 0; i < m; i++) {
        out[2 * i] = in[2 * i] + ((yl + acc[i]) << 7);
        out[2 * i + 1] = in[2 * i + 1] + (acc[i] << 8);
        yl = acc[i];
    }
    if (n & 1u)
        out[n - 1u] = in[n - 1u] + (yl << 8);
    Y->wp = (Y->wp - m) & PX_YMASK;
    Y->yl = yl;
    Y->dcx = dcx;
    Y->dcy = dcy;
    Y->dce = dce;
}
