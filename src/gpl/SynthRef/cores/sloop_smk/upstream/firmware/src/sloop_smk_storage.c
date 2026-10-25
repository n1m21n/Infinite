/* SPDX-License-Identifier: GPL-3.0-only */
#include "smk37_public_profile.h"
#include "sloop_smk_storage_map.h"
#include "smk37_store.h"
#include "smk37_sample_store.h"
static uint8_t sloop_samples_owned,sloop_samples_formatting,sloop_samples_requested;
static uint32_t sloop_samples_unit;
#define FL_RANGE_OK(a,n) (SMK37_ENABLE_SAVES && (SLOOP_STORE_RANGE(a,n) ||     (SMK37_ENABLE_SAMPLES && (sloop_samples_owned || sloop_samples_formatting) &&      (SMK37_SAMPLE_RANGE(a,n) || (sloop_samples_formatting && SMK37_SAMPLE_JOURNAL_RANGE(a,n))))))
#define fm1_xip_ptr smk37_flash_xip_ptr
#include "fm1_flash.h"
#include "smk37_flash_profile.h"
#undef fm1_xip_ptr
static uint8_t flash_ok;
static void sloop_storage_silence(void);
static int sloop_storage_read_raw(uint32_t off,void *dst,uint32_t n){
    if(off>0x100000u||n>0x100000u-off)return -1;
    uint8_t *p=dst;
    while(n){unsigned k=n>256?256:n;uint32_t f=irq_save();int rc=FL_FAR(fl_read_ram)(off,p,k);irq_restore(f);
        if(rc)return rc;p+=k;off+=k;n-=k;fm1_wdt_feed();}return 0;
}
static int st_read(uint32_t off,void *dst,uint32_t n){return (!n||SLOOP_STORE_RANGE(off,n))?sloop_storage_read_raw(off,dst,n):-1;}
#include "../../sloop/firmware/src/storage.c"
#include "sloop_smk_storage_policy.c"
static int st_erase(uint32_t off){
    if(!flash_ok||song.playing||transport_req||!sloop_storage_write_allowed(off,4096)||(off&4095))return -1;
    uint32_t took_us; /* RAM driver always writes the elapsed-time result. */
    sloop_storage_silence();return fl_erase4k(off,&took_us);
}
static int st_prog(uint32_t off,const void *src,uint32_t n){
    if(!flash_ok||song.playing||transport_req||!sloop_storage_write_allowed(off,n))return -1;
    return fl_write(off,src,n);
}
static int sloop_storage_probe(void){
    uint8_t trim[556],block[256];uint32_t f=irq_save(),id=FL_FAR(fl_jedec_ram)();irq_restore(f);
    if(!smk37_flash_supported_id(id)||sloop_storage_read_raw(0xc1000u,trim,sizeof trim)||!smk37_trim_keys_valid(trim,sizeof trim))return 0;
    uint32_t crc=~0u;
    for(unsigned a=0;a<0x4000;a+=256){if(sloop_storage_read_raw(a,block,256))return 0;crc=smk37_profile_crc(block,256,crc);}
    if(~crc!=SMK37_PUBLIC_HEAD_CRC)return 0;
    /* SMK application extends to 0x9d000: never use FM-1's 0x93000 window. */
    f=irq_save();SFCENC_CON&=(uint8_t)~2u;SFCENC_UNENC_L=FL_XIP(0x9d000u);SFCENC_UNENC_H=0x07ffffffu;SFCENC_CON|=2u;
    fl_inval(0x9d000u,0x63000u);irq_restore(f);
    sloop_storage_scan();
#if SMK37_ENABLE_SAMPLES
    sloop_samples_unit=smk37_store_crc(trim,sizeof trim);
    const smk37_store_io_t read_only={sloop_storage_read_raw,0,0};
    sloop_samples_owned=smk37_sample_owner_state(&read_only,sloop_samples_unit)==2;
    for(unsigned k=0;k<4;k++)smp_user_scan(k);
#endif
    return 1;
}
#define SLOOP_STORAGE_PROBE() sloop_storage_probe()
