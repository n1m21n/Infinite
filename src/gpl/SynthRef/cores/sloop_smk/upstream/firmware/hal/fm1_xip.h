/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 XIP window: flash offset off (>= 0x4000, the app area start) reads
 * at CPU address 0x02000000 + off - 0x4000 through the SFC/cache. Data
 * outside the plain (unencrypted) window reads decrypted: see
 * fm1_flash.h fl_plain_window_init. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#define FM1_XIP(off) (0x02000000u + (uint32_t)(off) - 0x4000u)

FM1_INLINE const uint8_t *fm1_xip_ptr(uint32_t off) { return (const uint8_t *)FM1_XIP(off); }
