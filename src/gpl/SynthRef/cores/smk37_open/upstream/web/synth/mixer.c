/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "mixer.h"
#include "fixed.h"
#include <string.h>

/* Shared effect settings; processing is in web/engine/effects.c. */
void fm1_fx_set(int32_t decay, int32_t feedback, int32_t level, unsigned delay);
void fm1_fx_configure(const uint8_t values[4], unsigned delay_ticks)
{
    fm1_fx_set(268435456 + (int32_t)((int64_t)values[0] * 644245094 / 127),
        (int32_t)((int64_t)values[2] * 858993459 / 127),
        (int32_t)((int64_t)values[3] * FM_Q30_ONE / 127),
        delay_ticks);
}

/* Preserve headroom until the master limiter. */
FM1_DSP_CODE void fm1_bus_output128(int32_t *out, const int32_t *left, const int32_t *right)
{
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) {
        out[2*i] = left[i] / 2;
        out[2*i+1] = right[i] / 2;
    }
}

/* +6 dB nominal gain: signed Q30 represents 2.0 minus one LSB. */
#define MASTER_NOMINAL_GAIN INT32_MAX
static int32_t master_gain = MASTER_NOMINAL_GAIN;
void fm1_master_reset(void) { master_gain = MASTER_NOMINAL_GAIN; }
FM1_DSP_CODE unsigned fm1_master_gain(void) { return (unsigned)master_gain; }
FM1_DSP_CODE void fm1_master_process128(int32_t *out)
{
    uint32_t peak = 0;
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
        int32_t value = out[i];
        uint32_t magnitude = value < 0 ? 0U - (uint32_t)value : (uint32_t)value;
        if (magnitude > peak) peak = magnitude;
    }
    /* Linked stereo peak limiter, -0.5 dBFS ceiling. Attack uses this block's
     * lookahead; release returns toward the fixed +6 dB gain over about
     * 186 ms. Limit the combined coefficient before scaling, so added gain
     * never pushes loud blocks beyond the ceiling. This is not automatic
     * normalization: quiet signals all receive the same nominal gain. */
    const unsigned ceiling = 7919643U;
    int32_t target = peak > ceiling / 2U ? (int32_t)(((uint64_t)ceiling << 30) / peak) : MASTER_NOMINAL_GAIN;
    if (!peak) {
        master_gain = MASTER_NOMINAL_GAIN;
        return; /* The buffer already contains exact zeros. */
    } else if (target < master_gain) master_gain = target;
    else {
        master_gain += (MASTER_NOMINAL_GAIN - master_gain + 63) / 64;
        if (master_gain > target) master_gain = target;
    }
    if (master_gain == FM_Q30_ONE) return;
    if (master_gain == MASTER_NOMINAL_GAIN) {
        /* Here peak <= ceiling/2 < 2^30: trunc(x * (2 - 2^-30))
         * equals 2*x - sign(x), including exact zero and negative samples. */
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
            int32_t x = out[i];
            out[i] = x * 2 - (x > 0) + (x < 0);
        }
    } else {
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i)
            out[i] = (int32_t)(((int64_t)out[i] * master_gain) / FM_Q30_ONE);
    }
}

uint32_t fm1_mix128(int32_t *mix, const int32_t *mono, int32_t gain_q29, unsigned add)
{
    uint32_t peak = 0;
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) {
        int32_t value = fm_sat32(((int64_t)mono[i] * gain_q29) >> 29);
        mix[i] = add ? fm_add_sat(mix[i], value) : value;
        uint32_t magnitude = value < 0 ? 0U - (uint32_t)value : (uint32_t)value;
        if (magnitude > peak) peak = magnitude;
    }
    return peak;
}
uint32_t fm1_mix_mono_stereo128(int32_t *left, int32_t *right, const int32_t *mono, int32_t gain_q29)
{
    uint32_t peak = 0;
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) {
        int32_t value = fm_sat32(((int64_t)mono[i] * gain_q29) >> 29);
        left[i] = fm_add_sat(left[i], value);
        right[i] = fm_add_sat(right[i], value);
        uint32_t magnitude = value < 0 ? 0U - (uint32_t)value : (uint32_t)value;
        if (magnitude > peak) peak = magnitude;
    }
    return peak;
}
void fm1_send128(int32_t *mix, const int32_t *send, int32_t gain_q29)
{
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i)
        mix[i] = fm_add_sat(mix[i], fm_sat32(((int64_t)send[i] * gain_q29) >> 29));
}

#if !defined(__GCC_PI32V2__)
FM1_DSP_CODE void fm1_gain_ramp128(int32_t *samples, int32_t gain, int32_t step)
{
    for(unsigned i=0;i<FM1_BLOCK_FRAMES;++i){
        gain+=step;samples[i]=fm_sat32(((int64_t)samples[i]*gain)>>29);
    }
}
#endif
