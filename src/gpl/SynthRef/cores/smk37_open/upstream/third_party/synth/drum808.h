/* SPDX-License-Identifier: MIT */
#ifndef FM1_DRUM808_H
#define FM1_DRUM808_H
#include "engine.h"
#include "fm1_param.h"

/* DaisySP-based percussion with FM1's existing part and project layout. */
enum { D8_BD, D8_SD, D8_LT, D8_MT, D8_HT, D8_RS, D8_CP, D8_CB, D8_CY, D8_OH, D8_CH, D8_NUM, D8_KIT = D8_NUM };


enum { D8_PRESET_BYTES = 209 };
typedef struct drum_voice drum808_kit_t;
/* Caller-owned, 32-byte-aligned storage; initialize before use. Calls on a
 * live kit belong to its audio owner or require that owner to be suspended.
 * Panic preserves all edited sounds and stops them without allocation. */
FM1_DSP_CODE size_t drum808_kit_size(void);
FM1_DSP_CODE void drum808_kit_init(drum808_kit_t *d);
FM1_DSP_CODE void drum808_kit_panic(drum808_kit_t *d);
FM1_DSP_CODE void drum808_kit_trigger(drum808_kit_t *d, int part, unsigned velocity);
/* Audio owner only. 1..11 sounding drum parts, oldest tails retire first.
 * Alternate sounds share their part's budget. Returns the retired part count. */
FM1_DSP_CODE unsigned drum808_kit_limit(drum808_kit_t *d, unsigned parts);
FM1_DSP_CODE unsigned drum808_kit_active(const drum808_kit_t *d);
/* Add 128 Q24 samples to cleared dry/send buses. In the mono path the two unused
 * sends may alias each other; all other buses are disjoint. Stereo uses pan. */
FM1_DSP_CODE void drum808_kit_render(drum808_kit_t *d, int32_t *dry, int32_t *rev, int32_t *dly);
FM1_DSP_CODE void drum808_kit_render_st(drum808_kit_t *d, int32_t *left, int32_t *right, int32_t *rev, int32_t *dly);
FM1_DSP_CODE unsigned drum808_kit_stereo(const drum808_kit_t *d);
FM1_DSP_CODE void drum808_kit_set(drum808_kit_t *d, int part, int parameter, int value);
FM1_DSP_CODE int drum808_kit_get(const drum808_kit_t *d, int part, int parameter);
int drum808_nparams(int part);
const fm1_param_t *drum808_param(int part, int parameter);
/* Project v2: 11 selectors followed by two sets of 99 parameter bytes.
 * Control only, under the audio gate. Import validates first, then stops notes;
 * store temporarily selects each reset-template variant, never a live voice. */
FM1_DSP_CODE void drum808_kit_store(drum808_kit_t *d, uint8_t values[D8_PRESET_BYTES]);
FM1_DSP_CODE int drum808_kit_load(drum808_kit_t *d, const uint8_t values[D8_PRESET_BYTES]);
FM1_DSP_CODE int drum808_preset_valid(const uint8_t values[D8_PRESET_BYTES]);
#endif
