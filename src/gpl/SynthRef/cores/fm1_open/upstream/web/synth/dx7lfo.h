/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012, 2013 Google Inc.
 * Modified for FM1 by M-VAVE, 2026: C port, fixed-point and block processing.
 * Apache-2.0 terms and upstream revisions: THIRD_PARTY.md. */
#ifndef _DX7LFO_H_
#define _DX7LFO_H_

typedef struct
{
  u32 phase;
  u32 delta;
  u8 waveform;
  u8 randstate;
  u8 sync;
  u32 delaystate;
  u32 delayinc;
  u32 delayinc2;
}t_dx7lfo;

void dx7lfo_reset(t_dx7lfo *this,const u8 params[6],int fs,int samples);
int dx7lfo_getsample(t_dx7lfo *this);
int dx7lfo_getdelay(t_dx7lfo *this);
void dx7lfo_keydown(t_dx7lfo *this);
#endif
