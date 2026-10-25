/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
/* 856014: original tested Pro. EB6014: Pro stock-restore report in
 * ChanceTheMaker/felucca-smk#17 (andyddd, 2026-10-18).
 * Keep an exact allowlist. A matching density byte is not sufficient.
 * Existing boot, calibration, allocation and write-range checks still apply.
 * EB6014 ROM-helper recovery is reported; our driver needs separate validation. */
static int smk37_flash_supported_id(uint32_t id){
    return id==0x856014u || id==0xeb6014u;
}
