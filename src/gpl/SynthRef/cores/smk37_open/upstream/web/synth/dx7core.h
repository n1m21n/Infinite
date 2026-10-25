/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012, 2013 Google Inc.
 * Modified for FM1 by M-VAVE, 2026: C port, fixed-point and block processing.
 * Apache-2.0 terms and upstream revisions: THIRD_PARTY.md. */
#ifndef _DX7CORE_H_
#define _DX7CORE_H_

typedef struct  {
  int gaincur;
  int gaindst;
  int freq;
  int phase;
}t_dx7op_params;

void dx7core_compute_64(int *output, int *scratch, t_dx7op_params *params,
                        int algorithm, int *fb_buf, int feedback_shift);

#endif
