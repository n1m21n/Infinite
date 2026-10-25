/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
/* Independent of FM-1's contiguous sample map. The journal is in a sector
 * checked blank in both private stock reads. No format-on-boot behavior. */
#define SMK37_SAMPLE_SIZE 0x14000u
#define SMK37_SAMPLE_JOURNAL 0xbf000u
#define SMK37_SAMPLE_RANGE(a,n) ((n)>0 && (((a)>=0x9d000u && (a)<0xb1000u && (n)<=0xb1000u-(a)) || ((a)>=0xc2000u && (a)<0xea000u && (n)<=0xea000u-(a))))
#define SMK37_SAMPLE_JOURNAL_RANGE(a,n) ((a)>=0xbf000u && (a)<0xc0000u && (n)>0 && (n)<=0xc0000u-(a))
static uint32_t smk37_sample_address(unsigned k){return k==0?0x9d000u:k==1?0xc2000u:k==2?0xd6000u:0;}
typedef struct {uint32_t magic,version,unit,layout,slots,size,state,crc;} smk37_sample_owner_t;
static smk37_sample_owner_t smk37_sample_owner(uint32_t unit,unsigned state){
    smk37_sample_owner_t h={0x53534d53u,1,unit,0x20261021u,3,SMK37_SAMPLE_SIZE,state,0};
    h.crc=smk37_store_crc(&h,28);return h;
}
/* 0 stock/partial intent; 1 resumable initialization; 2 committed; -1 foreign.
 * A torn marker can only be completed if all written bits match this unit's
 * exact expected record. An uncommitted intent also requires the stock CRCs. */
static int smk37_sample_owner_state(const smk37_store_io_t *io,uint32_t unit){
    uint8_t block[256];int intent=1,active=1;
    smk37_sample_owner_t a=smk37_sample_owner(unit,1),b=smk37_sample_owner(unit,2);
    for(unsigned p=0;p<4096;p+=256){
        if(io->read(SMK37_SAMPLE_JOURNAL+p,block,256))return -1;
        for(unsigned i=0;i<256;++i){
            uint8_t expect=p==0&&i<32?((uint8_t *)&a)[i]:p==256&&i<32?((uint8_t *)&b)[i]:255;
            if((block[i]&expect)!=expect)return -1;
            if(p==0&&block[i]!=expect)intent=0;
            if(p==256&&block[i]!=expect)active=0;
        }
    }
    return intent?(active?2:1):0;
}
static int smk37_sample_stock_matches(const smk37_store_io_t *io,const uint32_t expected[3]){
    uint8_t block[256];
    for(unsigned k=0;k<3;++k){uint32_t c=~0u;
        for(unsigned p=0;p<SMK37_SAMPLE_SIZE;p+=256){
            if(io->read(smk37_sample_address(k)+p,block,256))return 0;
            for(unsigned i=0;i<256;++i){c^=block[i];for(unsigned b=0;b<8;++b)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}
        }
        if(~c!=expected[k])return 0;
    }return 1;
}
static int smk37_sample_initialize(const smk37_store_io_t *io,uint32_t unit,const uint32_t expected[3]){
    int state=smk37_sample_owner_state(io,unit);
    if(state==2)return 0; /* never erase an already initialized sample */
    if(state<0 || (!state && !smk37_sample_stock_matches(io,expected)))return -1;
    smk37_sample_owner_t h=smk37_sample_owner(unit,1),read;
    if(io->program(SMK37_SAMPLE_JOURNAL,&h,32) || io->read(SMK37_SAMPLE_JOURNAL,&read,32) || memcmp(&h,&read,32))return -1;
    /* Header invalidation is enough: upload erases data sectors on demand.
     * Existing project/preset, trim/radio and OEM identity sectors are excluded. */
    uint8_t block[256];
    for(unsigned k=0;k<3;++k){uint32_t base=smk37_sample_address(k);
        if(io->erase(base))return -1;
        for(unsigned p=0;p<4096;p+=256){if(io->read(base+p,block,256))return -1;for(unsigned i=0;i<256;++i)if(block[i]!=255)return -1;}
    }
    h=smk37_sample_owner(unit,2);
    if(io->program(SMK37_SAMPLE_JOURNAL+256,&h,32))return -1;
    return smk37_sample_owner_state(io,unit)==2?0:-1;
}
/* Sequential upload, duplicate packets idempotent. A reset requires BEGIN
 * again. END publishes the validated FSMP header last. */
typedef struct {uint32_t next;uint8_t active;} smk37_sample_upload_t;
static int smk37_sample_begin(const smk37_store_io_t *io,smk37_sample_upload_t *u,unsigned k,int owned){
    if(k>=3||!owned)return 1;u[k].active=0;
    if(io->erase(smk37_sample_address(k)))return 2;
    u[k].next=512;u[k].active=1;return 0;
}
static int smk37_sample_write(const smk37_store_io_t *io,smk37_sample_upload_t *u,unsigned k,unsigned off,const uint8_t *p,unsigned n,int owned){
    uint8_t check[256];
    if(k>=3||!owned||!n||n>256||off<512||(off&255)||off>=SMK37_SAMPLE_SIZE||n>SMK37_SAMPLE_SIZE-off)return 1;
    if(!u[k].active)return 4;
    uint32_t a=smk37_sample_address(k)+off;
    if(off<u[k].next)return off+n<=u[k].next&&!io->read(a,check,n)&&!memcmp(p,check,n)?0:1;
    if(off!=u[k].next)return 1;
    if(!(off&4095)&&io->erase(a))return 2;
    if(io->program(a,p,n)||io->read(a,check,n)||memcmp(p,check,n))return 3;
    u[k].next=off+n;return 0;
}
