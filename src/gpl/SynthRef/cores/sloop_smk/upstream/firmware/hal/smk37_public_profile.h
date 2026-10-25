/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "smk37_trim.h"
/* Identical protected boot region in official Pro016 and Elite016 files.
 * A model/layout fingerprint, never an owner's calibration fingerprint. */
#define SMK37_PUBLIC_HEAD_CRC 0x3a04ad23u
#define SMK37_TRIM_SECTOR_BYTES 4096u
#ifndef SMK37_PUBLIC_BUILD
#define SMK37_PUBLIC_BUILD 0
#endif
static uint32_t smk37_profile_crc(const uint8_t *p,unsigned n,uint32_t c){
    while(n--){c^=*p++;for(unsigned b=0;b<8;++b)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return c;
}
/* Some instruments have an entirely erased USRTRIM sector.
 * Preserve that state; never invent calibration or accept a partly erased
 * invalid record. The caller snapshots and verifies the entire sector. */
static int smk37_trim_sector_supported(const uint8_t *p,unsigned n){
    if(n!=SMK37_TRIM_SECTOR_BYTES)return 0;
    if(smk37_trim_keys_valid(p,n))return 1;
    for(unsigned i=0;i<n;++i)if(p[i]!=0xff)return 0;
    return 1;
}
