/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012, 2013 Google Inc.
 * Modified for FM1 by M-VAVE, 2026: C port, fixed-point and block processing.
 * Apache-2.0 terms and upstream revisions: THIRD_PARTY.md. */
#include "engine.h"
#include <string.h>
#include "dx7synth.h"
#include "dx7env.h"

FM1_DSP_CODE static void advance(t_dx7env *this,int newix)
{
  this->ix = newix;
  if (this->ix < 4) {
    int actuallevel = dx7_outlvl(this->lvls[this->ix]) >> 1;
    actuallevel = (actuallevel << 6) + this->outlvl - 4256;
    actuallevel = DSP_MAX(16,actuallevel);
    this->targetlvl = actuallevel << 16;
    this->rising = (this->targetlvl > this->lvl);
    // rate
    int qrate = (this->rates[this->ix] * 41) >> 6;
    qrate += this->rate_scaling;
    qrate = DSP_MIN(qrate, 63);
    this->inc = (4 + (qrate & 3)) << (2 + (qrate >> 2));
  }
}

FM1_DSP_CODE void dx7env_set(t_dx7env *this,u8 *rates,u8 *lvls, int outlvl, int rate_scaling)
{
    memcpy(this->rates,rates,4);
    memcpy(this->lvls,lvls,4);
    this->outlvl = outlvl;
    this->rate_scaling = rate_scaling;
    this->lvl = 0;
    this->down = true;
    advance(this,0);
}


FM1_DSP_CODE int dx7env_getsample_64(t_dx7env *this)
{
  if (this->ix < 3 || (this->ix < 4 && !this->down)) {
    if (this->rising) {
      this->lvl = DSP_MAX(1716 << 16,this->lvl);
      this->lvl += (((17 << 24) - this->lvl) >> 24) *
                   this->inc * SYNTH_DX7_BLOCK_SAMPLES;
      if (this->lvl >= this->targetlvl) {
        this->lvl = this->targetlvl;
        advance(this,this->ix + 1);
      }
    }
    else {  // !rising
      this->lvl -= this->inc * SYNTH_DX7_BLOCK_SAMPLES;
      if (this->lvl <= this->targetlvl) {
        this->lvl = this->targetlvl;
        advance(this,this->ix + 1);
      }
    }
  }
  return this->lvl;
}

FM1_DSP_CODE void dx7env_keyup(t_dx7env *this)
{
  if (this->down) {
    this->down = 0;
    advance(this,3);
  }
}

FM1_DSP_CODE void dx7env_transfer(t_dx7env *this,t_dx7env *src)
{
    memcpy(this,src,sizeof(t_dx7env));
}

FM1_DSP_DATA const u8 dx7_lvllut[] = {
    0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46
};
