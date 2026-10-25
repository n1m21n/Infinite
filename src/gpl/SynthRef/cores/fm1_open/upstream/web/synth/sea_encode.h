/* SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Dani Biro; modifications (c) 2026 M-VAVE.
 * Offline SEA5 encoder for browser WAV import; never linked into device audio.
 * SEA codec attribution and license: see THIRD_PARTY.md. */
#ifndef FM1_SEA_ENCODE_H
#define FM1_SEA_ENCODE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fm1_sea.h"
static inline int32_t fm1_sea_sat(int32_t x)
{
    return x > 32767 ? 32767 : x < -32768 ? -32768 : x;
}
static inline int32_t fm1_sea_predict(const int32_t state[8])
{
    int64_t sum = 0;
    for (unsigned i = 0; i < 4; ++i) sum += (int64_t)state[i] * state[4+i];
    return (int32_t)(sum >> 13);
}
static inline void fm1_sea_update(int32_t state[8], int32_t pcm, int32_t residual)
{
    int32_t delta = residual >> 4;
    for (unsigned i = 0; i < 4; ++i)
        state[4+i] = fm1_sea_sat(state[4+i] + (state[i] < 0 ? -delta : delta));
    for (unsigned i = 0; i < 3; ++i) state[i] = state[i+1];
    state[3] = pcm;
}
/* Exact floor(65536 / scale), using the fixed SEA5 scale table. */
static const int32_t sea5_reciprocal[16] = {
    65536,13107,4681,2340,1365,873,606,436,329,255,202,164,135,113,96,82
};
static void put16(uint8_t *p, uint32_t x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x>>8); }
static uint64_t penalty(const int32_t state[8])
{
    int64_t sum = 0;
    for (unsigned i = 4; i < 8; ++i) sum += (int64_t)state[i]*state[i];
    int64_t v = (sum>>18)-0x8ff;
    return v > 0 ? (uint64_t)(v*v) : 0;
}
static int32_t divide(int32_t v, int32_t reciprocal)
{
    int64_t n = ((int64_t)v*reciprocal+32768)>>16;
    return (int32_t)(n + ((v>0)-(v<0)) - ((n>0)-(n<0)));
}
static uint8_t *fm1_sea_encode(const int16_t *input, unsigned frames,
                             const int16_t *dqt)
{
    enum { bits = 5 };
    if (!input || !frames || !dqt || frames > 0x1000000u) return NULL;
    unsigned blocks = (frames+FM1_SEA_FRAMES-1)/FM1_SEA_FRAMES;
    unsigned chunk_bytes = 28+FM1_SEA_FRAMES*bits/8, bytes = blocks*chunk_bytes;
    uint8_t *data = calloc(bytes, 1), quant[65], trial_codes[FM1_SEA_GROUP];

    if (!data) return NULL;
    unsigned limit = 1u<<bits, previous = 0;
    int x = (int)limit-1;
    quant[0] = (uint8_t)x;
    for (unsigned i = 1; i < limit; i += 2) { quant[i] = quant[i+1] = (uint8_t)x; x -= 2; }
    x = 0;
    for (unsigned i = limit; i < 2*limit; i += 2) { quant[i] = quant[i+1] = (uint8_t)x; x += 2; }
    quant[2*limit] = (uint8_t)(x-2);
    int32_t state[8] = {0,0,0,0,0,0,-8192,16384};
    for (unsigned block = 0; block < blocks; ++block) {
        uint8_t *chunk = data+block*chunk_bytes;
        chunk[0] = 1; chunk[1] = (uint8_t)(0x40|bits); chunk[2] = 20; chunk[3] = 0x5a;
        for (unsigned i = 0; i < 8; ++i) put16(chunk+4+2*i, (uint16_t)state[i]);
        for (unsigned group = 0; group < FM1_SEA_FRAMES/FM1_SEA_GROUP; ++group) {
            uint64_t best_rank = UINT64_MAX;
            int32_t best_state[8];
            uint8_t best_codes[FM1_SEA_GROUP];
            unsigned best_sf = 0;
            for (unsigned search = 0; search < 16; ++search) {
                unsigned sf = (search+previous) & 15, i;
                int32_t trial[8]; memcpy(trial,state,sizeof(trial));
                uint64_t rank = 0;
                int32_t reciprocal = sea5_reciprocal[sf];
                for (i = 0; i < FM1_SEA_GROUP; ++i) {
                    unsigned position = block*FM1_SEA_FRAMES+group*FM1_SEA_GROUP+i;
                    int32_t pcm = position < frames ? input[position] : 0;
                    int32_t prediction = fm1_sea_predict(trial);
                    int32_t scaled = divide(pcm-prediction,reciprocal);
                    if (scaled > (int32_t)limit) scaled = (int32_t)limit;
                    if (scaled < -(int32_t)limit) scaled = -(int32_t)limit;
                    unsigned q = quant[(int32_t)limit+scaled];
                    int32_t residual = dqt[(sf<<bits)+q];
                    int32_t reconstructed = fm1_sea_sat(prediction+residual);
                    int64_t error = (int64_t)pcm-reconstructed;
                    rank += (uint64_t)(error*error)+penalty(trial);
                    if (rank > best_rank) break;
                    trial_codes[i] = (uint8_t)q;
                    fm1_sea_update(trial,reconstructed,residual);
                }
                if (i == FM1_SEA_GROUP && rank < best_rank) {
                    best_rank = rank; best_sf = sf;
                    memcpy(best_state,trial,sizeof(trial));
                    memcpy(best_codes,trial_codes,sizeof(best_codes));
                }
            }
            memcpy(state,best_state,sizeof(state)); previous = best_sf;
            chunk[20+group/2] |= (uint8_t)(best_sf << ((group&1) ? 0 : 4));
            for (unsigned i = 0; i < FM1_SEA_GROUP; ++i) {
                unsigned at = (group*FM1_SEA_GROUP+i)*bits;
                unsigned packed = (unsigned)best_codes[i] << (11U-(at&7U));
                chunk[28+at/8] |= (uint8_t)(packed>>8);
                if ((at&7U) > 3U)
                    chunk[29+at/8] |= (uint8_t)packed;
            }
        }

    }
    return data;
}
#endif
