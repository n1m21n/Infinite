/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_BASS303_H
#define FM1_BASS303_H
#include "engine.h"
#include "fm1_param.h"

/* parameter indices (pages of four: the UI shows KNOB 1-4) */
enum {
    BASS303_CUTOFF, BASS303_RESO, BASS303_ENVMOD, BASS303_DECAY,     /* page 1 */
    BASS303_ACCENT, BASS303_WAVE, BASS303_TUNE, BASS303_VOLUME,      /* page 2 */
    BASS303_DRIVE, BASS303_DRVTYPE, BASS303_SLIDE, BASS303_ACCDEC,   /* page 3 */
    BASS303_NPARAMS
};
enum { BASS303_DRV_OFF, BASS303_DRV_SOFT, BASS303_DRV_RAT };


enum { BASS303_VOICES = 4 };
const fm1_param_t *bass303_param(int parameter);
#endif
