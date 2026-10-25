/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012, 2013 Google Inc.
 * Modified for FM1 by M-VAVE, 2026: C port, fixed-point and block processing.
 * Apache-2.0 terms and upstream revisions: THIRD_PARTY.md. */
#include "engine.h"
#include <string.h>
#include "dx7synth.h"
#include "dx7lfo.h"

FM1_DSP_CODE void dx7lfo_reset(t_dx7lfo *this,const u8 params[6],int fs,int samples)
{
    u32 lfo_unit = (u32)(samples * 25190424 /fs + 0.5f);
    int rate = params[0];  // 0..99
    int sr = rate == 0 ? 1 : (165 * rate) >> 6;
    sr *= sr < 160 ? 11 : (11 + ((sr - 160) >> 4));

    this->delta = lfo_unit * sr;
    int a = 99 - params[1];  // LFO delay
    if (a == 99) {
        this->delayinc = ~0u;
        this->delayinc2 = ~0u;
    }
    else {
        a = (16 + (a & 15)) << (1 + (a >> 4));
        this->delayinc = lfo_unit * a;
        a &= 0xff80;
        a = DSP_MAX(0x80, a);
        this->delayinc2 = lfo_unit * a;
    }
    this->waveform = params[5];
    this->sync = (params[4] != 0);
}

FM1_DSP_CODE int dx7lfo_getsample(t_dx7lfo *this)
{
    this->phase += this->delta;
    int x;
    switch (this->waveform) {
        case 0:  // triangle
        x = this->phase >> 7;
        x ^= -(this->phase >> 31);
        x &= (1 << 24) - 1;
        return x;
        case 1:  // sawtooth down
        return (~this->phase ^ (1U << 31)) >> 8;
        case 2:  // sawtooth up
        return (this->phase ^ (1U << 31)) >> 8;
        case 3:  // square
        return ((~this->phase) >> 7) & (1 << 24);
        case 4:  // sine
        return (1 << 23) + (k_sin_q24(this->phase >> 8) >> 1);
        case 5:  // s&h
        if (this->phase < this->delta) {
          this->randstate = (this->randstate * 179 + 17) & 0xff;
        }
        x = this->randstate^0x80;
        return (x + 1) << 16;
    }
    return 1 << 23;
}

FM1_DSP_CODE int dx7lfo_getdelay(t_dx7lfo *this)
{
  u32 delta = this->delaystate < (1U << 31) ? this->delayinc : this->delayinc2;
  u32 d = this->delaystate + delta;
  if (d < this->delayinc) return 1 << 24;
  this->delaystate = d;
  if (d < (1U << 31)) return 0;
  else return (d >> 7) & ((1 << 24) - 1);
}

FM1_DSP_CODE void dx7lfo_keydown(t_dx7lfo *this)
{
  if (this->sync) this->phase = (1U << 31) - 1;
  this->delaystate = 0;
}
