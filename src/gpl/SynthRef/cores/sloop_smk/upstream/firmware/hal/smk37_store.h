/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
/* Private Pro016 allocation candidate: blank gap between USRFLASH and USR.
 * Runtime driver must independently authorize it. No boot/calibration/stock
 * save region is part of this range. Four slots, each with A/B sector commits. */
#define SMK37_STORE_LO 0xeb000u
#define SMK37_STORE_HI 0xf3000u
/* Separate noncontiguous preset A/B sectors, opt-in at build and runtime. */
#define SMK37_PRESET_A 0xf3000u
#define SMK37_PRESET_B 0xfe000u
#define SMK37_PRESET_RANGE(a,n) ((uint32_t)(n)>0u && \
    (((uint32_t)(a)>=SMK37_PRESET_A && (uint32_t)(a)<SMK37_PRESET_A+4096u && (uint32_t)(n)<=SMK37_PRESET_A+4096u-(uint32_t)(a)) || \
     ((uint32_t)(a)>=SMK37_PRESET_B && (uint32_t)(a)<SMK37_PRESET_B+4096u && (uint32_t)(n)<=SMK37_PRESET_B+4096u-(uint32_t)(a))))
#define SMK37_STORE_PAYLOAD 3968u
#define SMK37_PRESET_V1_SIZE 2564u
#define SMK37_PRESET_V2_SIZE 3116u
#define SMK37_SESSION_V1_SIZE 3648u
#define SMK37_SESSION_V2_SIZE 3904u
typedef struct {
    int (*read)(uint32_t,void *,uint32_t);
    int (*erase)(uint32_t);
    int (*program)(uint32_t,const void *,uint32_t);
} smk37_store_io_t;
typedef struct { uint32_t magic,version,slot,sequence,length,crc,reserved,header_crc; } smk37_store_header_t;
static inline int smk37_store_range(uint32_t a,uint32_t n) {
    return a>=SMK37_STORE_LO && a<SMK37_STORE_HI && n && n<=SMK37_STORE_HI-a;
}
static uint32_t smk37_store_crc(const void *data,uint32_t n) {
    const uint8_t *p=data; uint32_t c=~0u;
    while(n--) { c^=*p++; for(unsigned b=0;b<8;++b)c=(c>>1)^((0u-(c&1u))&0xedb88320u); }
    return ~c;
}
static int smk37_store_read_copy(const smk37_store_io_t *io,unsigned slot,unsigned copy,
                                 void *payload,uint32_t length,smk37_store_header_t *h) {
    if(slot>=4 || copy>1 || !length || length>SMK37_STORE_PAYLOAD)return -1;
    uint32_t a=SMK37_STORE_LO+(slot*2+copy)*4096u;
    if(io->read(a,h,sizeof *h) || h->magic!=0x534d4b39u || (h->version!=1 && h->version!=2) ||
       h->slot!=slot || (h->length!=length && !(length==SMK37_SESSION_V2_SIZE && h->length==SMK37_SESSION_V1_SIZE) && !(length==SMK37_PRESET_V2_SIZE && h->length==SMK37_PRESET_V1_SIZE)) || h->reserved ||
       h->header_crc!=smk37_store_crc(h,28))return -1;
    uint32_t offset=h->version==1 ? 256u : 128u;
    if(h->length>4096u-offset || io->read(a+offset,payload,h->length) || h->crc!=smk37_store_crc(payload,h->length))return -1;
    if(h->length==SMK37_PRESET_V1_SIZE && length==SMK37_PRESET_V2_SIZE)memset((uint8_t *)payload+SMK37_PRESET_V1_SIZE,0,length-SMK37_PRESET_V1_SIZE);
    if(h->length==SMK37_SESSION_V1_SIZE && length==SMK37_SESSION_V2_SIZE) {
        /* Preserve Test10-12 metadata while the upstream FUN8 importer reads
         * the first 3584 bytes. No flash writes during migration. */
        memcpy((uint8_t *)payload+3840,(uint8_t *)payload+3584,64);
        memset((uint8_t *)payload+3584,0,256);
    }
    return 0;
}
/* Scratch is owned by main-loop storage, never used concurrently with audio. */
static uint8_t smk37_store_scratch[SMK37_STORE_PAYLOAD];
static int smk37_store_load(const smk37_store_io_t *io,unsigned slot,void *payload,
                            uint32_t length,uint32_t *sequence) {
    smk37_store_header_t a,b;
    int va=!smk37_store_read_copy(io,slot,0,payload,length,&a);
    int vb=!smk37_store_read_copy(io,slot,1,smk37_store_scratch,length,&b);
    if(vb && (!va || (b.sequence!=a.sequence && b.sequence-a.sequence<0x80000000u))) {
        memcpy(payload,smk37_store_scratch,length); if(sequence)*sequence=b.sequence; return 1;
    }
    if(va){if(sequence)*sequence=a.sequence;return 0;}
    return -1;
}
static int smk37_store_save(const smk37_store_io_t *io,unsigned slot,const void *payload,
                            uint32_t length,void *previous) {
    if(slot>=4 || !length || length>SMK37_STORE_PAYLOAD)return -1;
    uint32_t seq=0;
    int current=smk37_store_load(io,slot,previous,length,&seq);
    unsigned copy=current==0 ? 1u : 0u;
    uint32_t a=SMK37_STORE_LO+(slot*2+copy)*4096u;
    if(!smk37_store_range(a,4096) || io->erase(a))return -1;
    uint32_t offset=length>3840u ? 128u : 256u;
    const uint8_t *p=payload;
    for(uint32_t off=0;off<length;) {
        uint32_t room=256u-((offset+off)&255u);
        uint32_t n=length-off>room ? room : length-off;
        if(io->program(a+offset+off,p+off,n))return -1;
        off+=n;
    }
    /* Verify payload before publishing the commit header. */
    if(io->read(a+offset,smk37_store_scratch,length) || memcmp(payload,smk37_store_scratch,length))return -1;
    smk37_store_header_t h={0x534d4b39u,offset==128u ? 2u : 1u,slot,seq+1u,length,smk37_store_crc(payload,length),0,0};
    h.header_crc=smk37_store_crc(&h,28);
    if(io->program(a,&h,sizeof h))return -1;
    return smk37_store_read_copy(io,slot,copy,previous,length,&h) || memcmp(payload,previous,length) ? -1 : 0;
}
/* Authorize only blank slots or slots with a valid committed copy. A failed
 * replacement may leave the other sector incomplete; keep the committed one.
 * An interrupted first-ever save has no committed copy: fail closed for recovery. */
static int smk37_store_allocation_ok(const smk37_store_io_t *io,uint32_t length)
{
    smk37_store_header_t h;
    for(unsigned slot=0;slot<4;++slot){
        if(!smk37_store_read_copy(io,slot,0,smk37_store_scratch,length,&h) ||
           !smk37_store_read_copy(io,slot,1,smk37_store_scratch,length,&h))continue;
        for(uint32_t a=SMK37_STORE_LO+slot*8192u;a<SMK37_STORE_LO+(slot+1)*8192u;a+=256){
            if(io->read(a,smk37_store_scratch,256))return 0;
            for(unsigned i=0;i<256;++i)if(smk37_store_scratch[i]!=255)return 0;
        }
    }
    return 1;
}
