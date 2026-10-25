/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "fixed.h"

/* Shared control-rate polynomial: duplicating it at every pitch/envelope
 * call site consumes memory without changing the integer calculation. */
FM1_DSP_CODE __attribute__((noinline)) uint32_t fm_exp2_fixed(int32_t x, unsigned fraction)
{
    int n = (x >> 24) + ((x >> 23) & 1);
    /* The centered fraction is in [-0.5, 0.5), so Q32 fits signed S32.
     * This is exactly the former Q30 product >> 30, including negatives. */
    int32_t f = (int32_t)(((int64_t)x - (int64_t)n * FM_Q24_ONE) * 256);
    int32_t p = 164774;
    p = (int32_t)(((int64_t)p * f) >> 32) + 1438807;
    p = (int32_t)(((int64_t)p * f) >> 32) + 10327774;
    p = (int32_t)(((int64_t)p * f) >> 32) + 59596201;
    p = (int32_t)(((int64_t)p * f) >> 32) + 257941207;
    p = (int32_t)(((int64_t)p * f) >> 32) + 744261145;
    p = (int32_t)(((int64_t)p * f) >> 32) + FM_Q30_ONE;
    int shift = 30 - (int)fraction - n;
    if (shift >= 32) return 0;
    if (shift >= 0) return (uint32_t)p >> shift;
    if (shift <= -32 || (uint32_t)p > (UINT32_MAX >> -shift)) return UINT32_MAX;
    return (uint32_t)p << -shift;
}
