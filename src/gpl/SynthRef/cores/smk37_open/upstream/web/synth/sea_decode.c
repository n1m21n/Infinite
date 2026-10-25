/* SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Dani Biro; modifications (c) 2026 M-VAVE.
 * Portable browser decoder for the fixed SEA chunk format in fm1_sea.h.
 * Saturated LMS predictor; see ../../THIRD_PARTY.md for the SEA notice. */
#include "fm1_sea.h"
#include <string.h>

static int32_t clamp16(int32_t value)
{
    return value > 32767 ? 32767 : value < -32768 ? -32768 : value;
}
static int valid(const uint8_t *chunk, size_t bytes, unsigned bits,
                 const int16_t *dqt)
{
    return chunk && dqt && (bits == 5 || bits == 6) &&
        bytes == 28 + FM1_SEA_FRAMES * bits / 8 &&
        chunk[0] == 1 && chunk[1] == (0x40 | bits) &&
        chunk[2] == FM1_SEA_GROUP && chunk[3] == 0x5a;
}
static void seed(const uint8_t *chunk, int32_t state[8])
{
    for (unsigned i = 0; i < 8; ++i) {
        unsigned value = chunk[4 + i*2] | (unsigned)chunk[5 + i*2] << 8;
        state[i] = value < 32768 ? (int32_t)value : (int32_t)value - 65536;
    }
}
static void group(const uint8_t *chunk, unsigned bits, const int16_t *dqt,
                  unsigned g, int32_t state[8], int16_t pcm[FM1_SEA_GROUP])
{
    unsigned scale = (chunk[20 + g/2] >> ((g & 1) ? 0 : 4)) & 15;
    for (unsigned i = 0; i < FM1_SEA_GROUP; ++i) {
        unsigned at = (g * FM1_SEA_GROUP + i) * bits, code = 0;
        for (unsigned b = 0; b < bits; ++b)
            code = (code << 1) | ((chunk[28 + (at+b)/8] >> (7-(at+b)%8)) & 1);
        int64_t sum = 0;
        for (unsigned j = 0; j < 4; ++j) sum += (int64_t)state[j] * state[4+j];
        int32_t residual = dqt[(scale << bits) + code];
        int32_t value = clamp16((int32_t)(sum >> 13) + residual);
        int32_t delta = residual >> 4;
        for (unsigned j = 0; j < 4; ++j)
            state[4+j] = clamp16(state[4+j] + (state[j] < 0 ? -delta : delta));
        for (unsigned j = 0; j < 3; ++j) state[j] = state[j+1];
        state[3] = value;
        pcm[i] = (int16_t)value;
    }
}
int fm1_sea_decode(const uint8_t *chunk, size_t bytes, unsigned bits,
                   const int16_t *dqt, int16_t pcm[FM1_SEA_FRAMES])
{
    if (!pcm || !valid(chunk, bytes, bits, dqt)) return -1;
    int32_t state[8];
    seed(chunk, state);
    for (unsigned g = 0; g < FM1_SEA_FRAMES/FM1_SEA_GROUP; ++g)
        group(chunk, bits, dqt, g, state, pcm + g*FM1_SEA_GROUP);
    return 0;
}
int fm1_sea_decode_groups(const uint8_t *chunk, size_t bytes, unsigned bits,
    const int16_t *dqt, unsigned first, unsigned last, int32_t state[8],
    int16_t pcm[FM1_SEA_GROUP])
{
    if (!state || !pcm || !valid(chunk, bytes, bits, dqt) || first > last ||
        last >= FM1_SEA_FRAMES/FM1_SEA_GROUP) return -1;
    /* A continuation state is always saturated by the preceding group. */
    if (first) {
        for (unsigned i = 0; i < 8; ++i)
            if (state[i] < -32768 || state[i] > 32767) return -1;
    } else seed(chunk, state);
    for (unsigned g = first; g <= last; ++g) group(chunk, bits, dqt, g, state, pcm);
    return 0;
}
