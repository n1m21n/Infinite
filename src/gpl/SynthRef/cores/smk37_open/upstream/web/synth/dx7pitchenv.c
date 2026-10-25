/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012, 2013 Google Inc.
 * Modified for FM1 by M-VAVE, 2026: C port, fixed-point and block processing.
 * Apache-2.0 terms and upstream revisions: THIRD_PARTY.md. */
#include "engine.h"
#include <string.h>
#include "dx7synth.h"
#include "dx7pitchenv.h"

static struct {
  int value;
  u8 padding[28];
} pitchenv_unit __attribute__((aligned(32)));
static FM1_DSP_DATA const u8 ratetab[] = {
    1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
    12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47,
    49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82,
    85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125, 130, 135, 141, 147,
    153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255
};

static FM1_DSP_DATA const s8 pitchtab[] = {
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43,
    -41, -39, -37, -35, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24,
    -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10,
    -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73,
    82, 92, 103, 115, 127
};

FM1_DSP_CODE static void advance(t_dx7pitchenv *this,int newix)
{
  this->ix = newix;
  if (this->ix < 4) {
    this->targetlvl = (int)pitchtab[this->lvls[this->ix]] * 524288;
    this->rising = this->targetlvl > this->lvl;
    this->inc = ratetab[this->rates[this->ix]]*pitchenv_unit.value;
  }
}

FM1_DSP_CODE void dx7pitchenv_init(unsigned samples,unsigned fs)
{
  int value = (int)(((uint64_t)samples * (1 << 24) * 10 + (uint64_t)213 * fs / 2) / ((uint64_t)213 * fs));
  /* Creation is control-owned. freq_lut_acquire rejects a different sample
   * rate while any synth exists; avoid writing shared audio state when a
   * second instance uses that same rate and fixed envelope block size. */
  if (pitchenv_unit.value != value) pitchenv_unit.value = value;
}

FM1_DSP_CODE void dx7pitchenv_set(t_dx7pitchenv *this,u8 *rates, u8 *lvls)
{
  memcpy(this->rates,rates,4);
  memcpy(this->lvls,lvls,4);
  this->lvl = (int)pitchtab[lvls[3]] * 524288;
  this->down = 1;
  advance(this,0);
}

FM1_DSP_CODE int dx7pitchenv_getsample(t_dx7pitchenv *this)
{
  if (this->ix < 3 || (this->ix < 4 && !this->down)){
    if (this->rising) {
      this->lvl += this->inc;
      if (this->lvl >= this->targetlvl) {
        this->lvl = this->targetlvl;
        advance(this,this->ix + 1);
      }
    } else {  // !rising
      this->lvl -= this->inc;
      if (this->lvl <= this->targetlvl) {
        this->lvl = this->targetlvl;
        advance(this,this->ix + 1);
      }
    }
  }
  return this->lvl;
}

FM1_DSP_CODE void dx7pitchenv_keyup(t_dx7pitchenv *this)
{
    if(this->down){
        this->down = 0;
        advance(this,3);
    }
}
