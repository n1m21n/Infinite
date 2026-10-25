/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_MIXER_H
#define FM1_MIXER_H
#include "engine.h"

/* Fixed 128-frame Q24 buses, at least four-byte aligned and disjoint.
 * Gain is signed Q29. Mix returns the largest absolute scaled input as an
 * unsigned Q24 magnitude (INT32_MIN has magnitude 0x80000000). add=0 replaces
 * the bus, add=1 saturates the sum. The master limiter controls
 * output headroom. No allocation.
 */
/* In-place Q29 gain ramp; add step before each of exactly 128 samples.
 * Caller bounds gains to [0,2]. The wide product saturates like fm1_mix128. */
FM1_DSP_CODE void fm1_gain_ramp128(int32_t *samples, int32_t gain_q29, int32_t step_q29);
FM1_DSP_CODE uint32_t fm1_mix128(int32_t *mix, const int32_t *mono, int32_t gain_q29, unsigned add);
/* Add the same scaled mono signal to two independent stereo buses. Shares
 * the gain and peak calculation; each destination saturates independently. */
FM1_DSP_CODE uint32_t fm1_mix_mono_stereo128(int32_t *left, int32_t *right, const int32_t *mono, int32_t gain_q29);
FM1_DSP_CODE void fm1_send128(int32_t *mix, const int32_t *send, int32_t gain_q29);

/* Convert Q24 to interleaved Q23 while preserving headroom. Left/right may
 * be identical; output must not alias either input. Call fm1_master_process128()
 * afterward to apply the linked stereo master limiter. */
FM1_DSP_CODE void fm1_bus_output128(int32_t *out, const int32_t *left, const int32_t *right);
FM1_DSP_CODE void fm1_master_process128(int32_t *out);
void fm1_master_reset(void);
FM1_DSP_CODE unsigned fm1_master_gain(void); /* Q30, nominal +6 dB = INT32_MAX. */

/* Shared reverb and delay. Configure while rendering is paused. Render
 * filters the send buses in place and adds returns to the dry buses;
 * all four buffers must be disjoint. No allocation during rendering. */
/* 2.04 seconds at 44.1 kHz / 8. A bounded wrap saves 20 KiB versus
 * rounding two histories up to 16384 entries. No division in rendering. */
enum { FM1_FX_DELAY_SAMPLES = 11264 };
void fm1_fx_configure(const uint8_t values[4], unsigned delay_ticks);
FM1_DSP_CODE void fm1_fx_clear(void);
FM1_DSP_CODE unsigned fm1_fx_active(void);
FM1_DSP_CODE void fm1_fx_render(int32_t *left, int32_t *right, int32_t *rev, int32_t *dly);
#endif
