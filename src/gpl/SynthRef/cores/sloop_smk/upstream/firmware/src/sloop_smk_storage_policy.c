/* SPDX-License-Identifier: GPL-3.0-only */
/* Included after native storage.c; exercised unchanged by the host flash test. */
static uint16_t sloop_storage_owned;
/* Nonzero only inside the explicit legacy-slot replacement transaction. */
static uint32_t sloop_storage_reclaim_base;
static int sloop_storage_blank(uint32_t base){
    uint8_t b[256];
    for(unsigned p=0;p<4096;p+=sizeof b){if(st_read(base+p,b,sizeof b))return 0;
        for(unsigned i=0;i<sizeof b;i++)if(b[i]!=255)return 0;}
    return 1;
}
static void sloop_storage_scan(void){
    sloop_storage_owned=0;
    for(unsigned o=0;o<OBJ_COUNT;o++){st_hdr_t h;
        if(st_current(o,&h)>=0 || (sloop_storage_blank(st_sector(o,0))&&sloop_storage_blank(st_sector(o,1))))
            sloop_storage_owned|=1u<<o;
    }
}
static int sloop_storage_write_allowed(uint32_t a,uint32_t n){
    if(!SLOOP_STORE_RANGE(a,n))return 0;
    if(sloop_storage_reclaim_base && a>=sloop_storage_reclaim_base &&
       a<sloop_storage_reclaim_base+4096u && n<=sloop_storage_reclaim_base+4096u-a)return 1;
    for(unsigned o=0;o<OBJ_COUNT;o++)if(sloop_storage_owned&(1u<<o))
        for(unsigned c=0;c<2;c++){uint32_t base=st_sector(o,c);
            if(a>=base&&a<base+4096u&&n<=base+4096u-a)return 1;}
    return 0;
}

#define SLOOP_PROJECT_WRITABLE(slot) ((sloop_storage_owned >> (OBJ_PROJECT0 + ((slot) & 3u))) & 1u)
