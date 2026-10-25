/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Browser reverb and delay. */
#include "mixer.h"
#include "fixed.h"
#include <string.h>
static struct effects_state {
    int16_t delay[FM1_FX_DELAY_SAMPLES], comb[916];
    unsigned index, at[4], quiet;
    int32_t input[2], filtered[2], output[2];
} effects_state;
static struct { int32_t decay, feedback, level; unsigned delay, timeout; } effects;

void fm1_fx_set(int32_t decay, int32_t feedback, int32_t level, unsigned delay)
{
    effects.decay = decay; effects.feedback = feedback;
    effects.level = level; effects.delay = delay;
    effects.timeout = delay > 1944U ? delay * 8U * 24U : FM1_SAMPLE_RATE * 8U;
}
FM1_DSP_CODE void fm1_fx_clear(void)
{
    if (effects_state.quiet == UINT32_MAX) return;
    memset(&effects_state, 0, sizeof effects_state);
    effects_state.quiet = UINT32_MAX;
}

FM1_DSP_CODE unsigned fm1_fx_active(void)
{
    return effects_state.quiet != UINT32_MAX;
}
static FM1_DSP_CODE int16_t effects_sample(int32_t value)
{
    if (value >= FM_Q24_ONE) return 32767;
    if (value <= -FM_Q24_ONE) return -32768;
    return (int16_t)(value / 512);
}
static FM1_DSP_CODE void effects_filter128(int32_t *bus, int32_t *first, int32_t *second)
{
    int32_t a = *first, b = *second;
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) {
        /* Low-pass coefficient 0.22 in signed Q32. */
        int32_t sample = (int32_t)effects_sample(bus[i]) * 512;
        a += (int32_t)(((int64_t)(sample - a) * 944892805) >> 32);
        b += (int32_t)(((int64_t)(a - b) * 944892805) >> 32);
        bus[i] = b;
    }
    *first = a; *second = b;
}
FM1_DSP_CODE void fm1_fx_render(int32_t *left_mix, int32_t *right_mix,
                                int32_t *rev, int32_t *dly)
{
    static FM1_DSP_DATA const unsigned length[4] = {149, 211, 263, 293};
    struct effects_state *s = &effects_state;
    unsigned signal = 0;
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i)
        if (rev[i] > 167 || rev[i] < -167 || dly[i] > 167 || dly[i] < -167) { signal = 1; break; }
    if (signal) s->quiet = 0;
    else if (s->quiet == UINT32_MAX) return;
    else {
        s->quiet += FM1_BLOCK_FRAMES;
        if (s->quiet >= effects.timeout) { fm1_fx_clear(); return; }
    }
    effects_filter128(rev, &s->input[0], &s->filtered[0]);
    effects_filter128(dly, &s->input[1], &s->filtered[1]);
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; i += 8) {
        unsigned offset = 0;
        int32_t r[4];
        for (unsigned k = 0; k < 4; ++k) {
            unsigned at = offset + s->at[k];
            r[k] = (int32_t)s->comb[at] * 512;
            s->comb[at] = effects_sample(rev[i] + fm_mul30(r[k], effects.decay));
            if (++s->at[k] == length[k]) s->at[k] = 0;
            offset += length[k];
        }
        unsigned read = s->index + FM1_FX_DELAY_SAMPLES - effects.delay;
        if (read >= FM1_FX_DELAY_SAMPLES) read -= FM1_FX_DELAY_SAMPLES;
        int32_t d = (int32_t)s->delay[read] * 512;
        s->delay[s->index] = effects_sample(dly[i] + fm_mul30(d, effects.feedback));
        if (++s->index == FM1_FX_DELAY_SAMPLES) s->index = 0;
        int32_t dl = (fm_mul30((r[0] + r[1]) / 4 + d, effects.level) - s->output[0]) / 8;
        int32_t dr = (fm_mul30((r[2] + r[3]) / 4 + d, effects.level) - s->output[1]) / 8;
        int32_t left = s->output[0], right = s->output[1];
        for (unsigned j = 0; j < 8; ++j) {
            left += dl; right += dr;
            left_mix[i+j] = fm_add_sat(left_mix[i+j], left);
            right_mix[i+j] = fm_add_sat(right_mix[i+j], right);
        }
        s->output[0] = left; s->output[1] = right;
    }
}
