/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1_ENGINE_H
#define FM1_ENGINE_H
#include <stddef.h>
#include <stdint.h>
#include "fm1_block.h"
#define FM1_SYNTH_API 8U
#define FM1_DSP_ABI 3U
#ifdef FM1_HOST_TEST
#define FM1_DSP_CODE
#define FM1_DSP_DATA
#else
#define FM1_DSP_CODE __attribute__((section(".volatile_ram_code")))
#define FM1_DSP_DATA __attribute__((section(".volatile_ram.fm1"), aligned(4)))
#endif
#endif
