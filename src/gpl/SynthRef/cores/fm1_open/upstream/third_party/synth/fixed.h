/* SPDX-License-Identifier: MIT */
#ifndef FM1_FIXED_H
#define FM1_FIXED_H
#include "engine.h"
#include <limits.h>

/* Signed Q24 buses have 128 times full-scale headroom. Coefficients use Q30
 * (exact unity); recursive histories may use Q27. Keep products in 64 bits
 * until their documented reduction. No signed left shift or overflowing add.
 * Signed right shifts are arithmetic. */
#define FM_Q24_ONE INT32_C(16777216)
#define FM_Q27_ONE INT32_C(134217728)
#define FM_Q30_ONE INT32_C(1073741824)
static FM1_DSP_CODE inline int32_t fm_mul30(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 30);
}
static FM1_DSP_CODE inline int32_t fm_sat32(int64_t value)
{
    return value > INT32_MAX ? INT32_MAX : value < INT32_MIN ? INT32_MIN : (int32_t)value;
}
static FM1_DSP_CODE inline int32_t fm_add_sat(int32_t a, int32_t b)
{
    return fm_sat32((int64_t)a + b);
}
/* 2^x: x is signed Q24, result has 'fraction' fractional bits. The centered
 * Horner polynomial avoids a large table and retains near-unity resolution
 * for long releases. All coefficients are compile-time integers. */
FM1_DSP_CODE uint32_t fm_exp2_fixed(int32_t x, unsigned fraction);
static FM1_DSP_CODE inline uint32_t fm_note_hz_q16(int32_t note_q16)
{
    int32_t exponent = (int32_t)(((int64_t)note_q16 - 69 * 65536) * 256 / 12);
    /* MIDI plus tune/bend spans notes -26..153. Resolve the octave separately
     * so very low notes keep all 30 mantissa bits until the final Hz rounding. */
    int octave = exponent >> 24;
    int32_t fraction = exponent - octave * FM_Q24_ONE;
    unsigned shift = (unsigned)(14 - octave);
    uint64_t value = (uint64_t)fm_exp2_fixed(fraction, 30) * 440;
    return (uint32_t)((value + (UINT64_C(1) << (shift - 1))) >> shift);
}
#endif
