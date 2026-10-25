/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_DRUM808_H
#define FM1_DRUM808_H
#include "engine.h"
#include "fm1_param.h"

enum { D8_BD, D8_SD, D8_LT, D8_MT, D8_HT, D8_RS, D8_CP, D8_CB, D8_CY, D8_OH, D8_CH, D8_NUM, D8_KIT = D8_NUM };


enum { D8_PRESET_BYTES = 209 };
int drum808_nparams(int part);
const fm1_param_t *drum808_param(int part, int parameter);
#endif
