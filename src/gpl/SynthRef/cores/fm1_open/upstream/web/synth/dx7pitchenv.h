/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012, 2013 Google Inc.
 * Modified for FM1 by M-VAVE, 2026: C port, fixed-point and block processing.
 * Apache-2.0 terms and upstream revisions: THIRD_PARTY.md. */
#ifndef _DX7PITCHENV_H
#define _DX7PITCHENV_H

#include <stddef.h>

typedef struct
{
  u8  rates[4];
  u8  lvls[4];
  int lvl;
  int targetlvl;
  int ix;
  int inc;
  u8 rising;
  u8 down;
}t_dx7pitchenv;

void dx7pitchenv_init(unsigned samples,unsigned fs);
void dx7pitchenv_set(t_dx7pitchenv *this,u8 *rates, u8 *lvls);
int  dx7pitchenv_getsample(t_dx7pitchenv *this);
void dx7pitchenv_keyup(t_dx7pitchenv *this);
#endif
