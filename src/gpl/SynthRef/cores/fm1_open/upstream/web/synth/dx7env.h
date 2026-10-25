/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012, 2013 Google Inc.
 * Modified for FM1 by M-VAVE, 2026: C port, fixed-point and block processing.
 * Apache-2.0 terms and upstream revisions: THIRD_PARTY.md. */
#ifndef _ENV_H_
#define _ENV_H_

typedef struct
{
  u8  rates[4];
  u8  lvls[4];
  int outlvl;
  int rate_scaling;
  // Level is stored so that 2^24 is one doubling, ie 16 more bits than
  // the DX7 itself (fraction is stored in level rather than separate
  // counter)
  int lvl;
  int targetlvl;
  int ix;
  int inc;
  u8 rising;
  u8 down;
}t_dx7env;

void dx7env_set(t_dx7env *this,u8 *rates,u8 *lvls, int outlvl, int rate_scaling);
int dx7env_getsample_64(t_dx7env *this);
void dx7env_keyup(t_dx7env *this);
void dx7env_transfer(t_dx7env *this,t_dx7env *src);
#endif
