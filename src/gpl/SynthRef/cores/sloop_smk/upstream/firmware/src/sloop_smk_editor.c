/* SPDX-License-Identifier: GPL-3.0-only */
#if !FELUCCA_FLASH
static uint8_t flash_ok;
static void fl_inval(uint32_t a,uint32_t n){(void)a;(void)n;}
#endif
#ifdef SLOOP_SMK_HARDWARE
static int fl_erase4k_quiet(uint32_t a,uint32_t*t){(void)a;(void)t;return -1;}
#if !FELUCCA_FLASH
static int fl_write(uint32_t a,const void*p,uint32_t n){(void)a;(void)p;(void)n;return -1;}
#endif
#else
static uint8_t *sloop_sample_at(uint32_t a,uint32_t n){
    for(unsigned k=0;k<4;k++){uint32_t base=SMP_USER_OFF(k);if(a>=base&&a-base<=SMP_USER_SIZE&&n<=SMP_USER_SIZE-(a-base))return sample_memory[k]+(a-base);}return 0;
}
static int fl_erase4k_quiet(uint32_t a,uint32_t*t){uint8_t*p=sloop_sample_at(a,4096);if(!p||(a&4095))return -1;memset(p,255,4096);if(t)*t=0;return 0;}
static int fl_write(uint32_t a,const void*p,uint32_t n){uint8_t*d=sloop_sample_at(a,n);if(!d)return -1;memcpy(d,p,n);return 0;}
#endif
#if !FELUCCA_FLASH
static int ed_flash_busy(void){return song.playing || transport_req;}
#endif
#if defined(SLOOP_SMK_HARDWARE) && SMK37_ENABLE_SAMPLES
#define SLOOP_ED_SAMPLES 1
#endif
#include "../../sloop/firmware/src/editor.c"
