/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SAMPLEDRUM_H
#define FM1_SAMPLEDRUM_H
#include "engine.h"
#include "fm1_param.h"

enum { SAMPLE_DRUM_PADS = 8, SAMPLE_DRUM_VOICES = 24, SAMPLE_DRUM_PARAMS = 6 };
/* All eight pads share this bank. A single sample may use every available
 * complete SEA5 block; there is no separate per-pad duration allocation. */
enum { SAMPLE_DRUM_BANK_BYTES = 466944, SAMPLE_DRUM_BANK_HEADER = 512,
       SAMPLE_DRUM_CHUNK_FRAMES = 320, SAMPLE_DRUM_CHUNK_BYTES = 228,
       SAMPLE_DRUM_MAX_FRAMES = (SAMPLE_DRUM_BANK_BYTES - SAMPLE_DRUM_BANK_HEADER) /
                               SAMPLE_DRUM_CHUNK_BYTES * SAMPLE_DRUM_CHUNK_FRAMES };
enum {
    SAMPLE_DRUM_LEVEL,
    SAMPLE_DRUM_TUNE,
    SAMPLE_DRUM_LENGTH,
    SAMPLE_DRUM_CHOKE,
    SAMPLE_DRUM_VARIATION,
    SAMPLE_DRUM_VELOCITY_LPF
};
const fm1_param_t *sampledrum_param(unsigned parameter);
/* Immutable sample binding; pause the instrument before replacing storage. */
int sampledrum_bind(unsigned pad, const uint8_t *data, unsigned bytes, unsigned frames, const char *name);
#endif
