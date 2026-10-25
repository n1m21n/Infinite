/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
/* Sloop objects only: no sample, calibration, journal, loader or stock VM sectors.
   Nonblank sectors are authorized only by a valid Sloop A/B commit for that object. */
#define SLOOP_STORE_RANGE(a,n) ((uint32_t)(n)>0u && ( \
 ((uint32_t)(a)>=0xb1000u && (uint32_t)(a)<0xb9000u && (uint32_t)(n)<=0xb9000u-(uint32_t)(a)) || \
 ((uint32_t)(a)>=0xeb000u && (uint32_t)(a)<0xf4000u && (uint32_t)(n)<=0xf4000u-(uint32_t)(a)) || \
 ((uint32_t)(a)>=0xfe000u && (uint32_t)(a)<0xff000u && (uint32_t)(n)<=0xff000u-(uint32_t)(a))))
static uint32_t sloop_storage_sector(uint32_t object,uint32_t copy){
    if(object>=9||copy>1)return 0xffffffffu;
    if(object==0)return 0xb1000u+copy*4096u;
    if(object<=4)return 0xeb000u+(object-1u)*8192u+copy*4096u;
    if(object<=6)return 0xb3000u+(object-5u)*8192u+copy*4096u;
    if(object==7)return 0xb7000u+copy*4096u;
    return copy?0xfe000u:0xf3000u;
}
#define SLOOP_STORAGE_SECTOR(o,c) sloop_storage_sector(o,c)
