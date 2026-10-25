/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_SEA_H
#define FM1_SEA_H
#include <stdint.h>
#include <stddef.h>

/* Fixed mono SEA5/SEA6 decoder in the native SDK. 320 frames per chunk,
 * 20 frames per scale, 4-bit scales, saturated int16 LMS history/weights.
 * No OS calls, allocation or shared mutable state.
 * Caller owns non-overlapping input, DQT, state and output buffers. DQT has
 * 16 * (1 << bits) int16 entries for the encoded resource's quantization. */
#define FM1_SEA_FRAMES 320u
#define FM1_SEA_GROUP 20u

/* Decode exactly one fixed-size chunk. Return 0 on success, -1 on invalid
 * arguments/header/size. Failure leaves output unchanged. */
int fm1_sea_decode(const uint8_t *chunk, size_t bytes, unsigned bits,
    const int16_t *dqt, int16_t pcm[FM1_SEA_FRAMES]);

/* Decode first..last (0..15), output the last 20 PCM frames. first=0 loads
 * the seed; otherwise state must follow group first-1 of this same chunk.
 * Success advances state through last. Failure leaves state/output unchanged.
 * Sequential sample-cache misses can decode only one group. */
int fm1_sea_decode_groups(const uint8_t *chunk, size_t bytes, unsigned bits,
    const int16_t *dqt, unsigned first, unsigned last, int32_t state[8],
    int16_t pcm[FM1_SEA_GROUP]);
#endif
