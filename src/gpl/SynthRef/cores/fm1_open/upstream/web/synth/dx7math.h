/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef _DX7MATH_H_
#define _DX7MATH_H_

#include <stddef.h>

int k_sin_q24(int x);
int k_pow2_q24(int x);
int freq_lut_acquire(int sample_rate);
void freq_lut_release(void);
int freqlut_qx(int logfreq);
int k_log2_q24(unsigned int x);
int mk_sin(int phase,unsigned short gain);
#endif
