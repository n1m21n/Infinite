/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef DX7_SYNTH_INTERNAL_H
#define DX7_SYNTH_INTERNAL_H

#include "types.h"
#include "synth_dx7.h"
#include "dx7math.h"

#define FM_OP_CNT 6
#define DSP_MIN(a,b) ((a)<(b)?(a):(b))
#define DSP_MAX(a,b) ((a)>(b)?(a):(b))

extern const u8 dx7_lvllut[];

static inline u8 dx7_outlvl(u8 level)
{
    return level >= 20U ? 28U + level : dx7_lvllut[level];
}

#endif
