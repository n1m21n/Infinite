/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012, 2013 Google Inc.
 * Modified for FM1 by M-VAVE, 2026: C port, fixed-point and block processing.
 * Apache-2.0 terms and upstream revisions: THIRD_PARTY.md. */
#ifndef _DX7CTRL_H_
#define _DX7CTRL_H_


typedef struct{
   int range;
   bool pitchswitch;
   bool ampswitch;
   bool egswitch;
}ctrl_mod;


typedef struct
{
   int mastertune;
   u8  pitchrange;
   u8  pitchstep;
   s16 pitchbend;

   u8  pitchrangeup;
   u8  pitchrangedn;

   u8  pitch_mod;
   u8  amp_mod;
   u8  eg_mod;

   int modwheel_cc;
   int breath_cc;
   int foot_cc;
   int aftertouch_cc;
   ctrl_mod wheel;
   ctrl_mod foot;
   ctrl_mod breath;
   ctrl_mod aftertouch;
   u8  opswitch;
}t_dx7ctrl;

#endif
