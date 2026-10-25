/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "instrument.h"
#include "fm_patch.h"
#include "fm1_device.h"
#include "fm1_engine_control.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Standard 128-byte, seven-bit VMEM voices need 112 bytes on Flash. Each
 * sector consequently has room for its own CRC/commit header. The shared
 * project staging sector holds an entire replacement until recovery finishes;
 * an interrupted write never destroys the last committed bank. */
enum { BANK_MAGIC=0x31424d46U, STAGE=GROOVE_PROJECT_SLOTS*4096, PACKED_VOICE=112 };
struct bank { uint32_t magic, version, index, crc, commit; uint8_t voice[32][PACKED_VOICE]; };
_Static_assert(sizeof(struct bank) <= 4096, "DX7 bank and journal fit one sector");
static const struct bank *banks[4];
static int blocked;

static FM1_DSP_CODE void unpack7(uint8_t raw[128], const uint8_t packed[112])
{
    for (unsigned i=0;i<128;++i) {
        unsigned bit=i*7U, at=bit>>3, shift=bit&7U;
        unsigned value=packed[at]>>shift;
        if (shift>1) value|=(unsigned)packed[at+1]<<(8U-shift);
        raw[i]=(uint8_t)(value&127U);
    }
}
static void pack7(uint8_t packed[112], const uint8_t raw[128])
{
    memset(packed,0,112);
    for (unsigned i=0;i<128;++i) {
        unsigned bit=i*7U, at=bit>>3, shift=bit&7U;
        packed[at]|=(uint8_t)(raw[i]<<shift);
        if (shift>1) packed[at+1]|=(uint8_t)(raw[i]>>(8U-shift));
    }
}
FM1_DSP_CODE void groove_fm_unpack(uint8_t p[156], const uint8_t b[128])
{
    for (unsigned op=0;op<6;++op) {
        const uint8_t *v=b+op*17; uint8_t *q=p+op*21;
        memcpy(q,v,11); q[11]=v[11]&3U; q[12]=(v[11]>>2)&3U;
        q[13]=v[12]&7U; q[20]=(v[12]>>3)&15U;
        q[14]=v[13]&3U; q[15]=(v[13]>>2)&7U; q[16]=v[14];
        q[17]=v[15]&1U; q[18]=(v[15]>>1)&31U; q[19]=v[16];
    }
    memcpy(p+126,b+102,8); p[134]=b[110]&31U;
    p[135]=b[111]&7U; p[136]=(b[111]>>3)&1U;
    memcpy(p+137,b+112,4); p[141]=b[116]&1U;
    p[142]=(b[116]>>1)&7U; p[143]=(b[116]>>4)&7U;
    memcpy(p+144,b+117,11); p[155]=63;
}
void groove_fm_pack(uint8_t b[128], const uint8_t p[156])
{
    for (unsigned op=0;op<6;++op) {
        uint8_t *v=b+op*17; const uint8_t *q=p+op*21;
        memcpy(v,q,11); v[11]=q[11]|(q[12]<<2); v[12]=q[13]|(q[20]<<3);
        v[13]=q[14]|(q[15]<<2); v[14]=q[16]; v[15]=q[17]|(q[18]<<1); v[16]=q[19];
    }
    memcpy(b+102,p+126,8); b[110]=p[134]; b[111]=p[135]|(p[136]<<3);
    memcpy(b+112,p+137,4); b[116]=p[141]|(p[142]<<1)|(p[143]<<4);
    memcpy(b+117,p+144,11);
}
static int voice_valid(const uint8_t raw[128])
{
    static const uint8_t op_max[21]={99,99,99,99,99,99,99,99,99,99,99,3,3,7,3,7,99,1,31,99,14};
    static const uint8_t global_max[19]={99,99,99,99,99,99,99,99,31,7,1,99,99,99,99,1,5,7,48};
    uint8_t p[156]; groove_fm_unpack(p,raw);
    for (unsigned i=0;i<128;++i) if (raw[i]>127) return 0;
    for (unsigned i=0;i<145;++i) if (p[i]>(i<126?op_max[i%21]:global_max[i-126])) return 0;
    return 1;
}
static uint32_t checksum(const struct bank *b)
{
    uint32_t crc=groove_crc32(~0U,b,offsetof(struct bank,crc));
    return ~groove_crc32(crc,b->voice,sizeof b->voice);
}
static const struct bank *read_bank(uint32_t address)
{
    const struct bank *b=(const struct bank *)fm1_flash_map(address,sizeof *b);
    if (!b || b->magic!=BANK_MAGIC || b->version!=1 || b->index>=4 || b->commit || b->crc!=checksum(b)) return NULL;
    return b;
}
static int conflict(void)
{
    uint32_t header[3];
    if (fm1_flash_size()<GROOVE_FLASH_BYTES) return FM1_FLASH_UNAVAILABLE;
    int r=fm1_flash_read(GROOVE_SAMPLE_OFFSET,header,sizeof header);
    if (r) return r;
    return header[0]==0x53314d46U && header[2]>GROOVE_SAMPLE_LIMIT ? GROOVE_PROJECT_TUNING_SPACE : 0;
}
void groove_fm_library_init(void)
{
    fm1_engine_pause();
    memset(banks,0,sizeof banks); blocked=conflict();
    if (!blocked) {
        for (unsigned i=0;i<4;++i) {
            const struct bank *b=read_bank(GROOVE_LIBRARY_OFFSET+i*4096U);
            if (b && b->index==i) banks[i]=b;
        }
        const struct bank *staged=read_bank(STAGE);
        if (staged) banks[staged->index]=staged;
    }
    fm1_engine_resume();
}
unsigned groove_fm_library_available(void)
{
    /* A transient startup read failure must not permanently hide saved banks.
     * Capability/download checks run on the control task, never in rendering. */
    if (blocked) groove_fm_library_init();
    return !blocked;
}
FM1_DSP_CODE void groove_fm_program(uint8_t p[156], unsigned program)
{
    program%=FM1_FM_PROGRAMS;
    const struct bank *b=banks[program/32U];
    if (b) { uint8_t raw[128]; unpack7(raw,b->voice[program%32U]); groove_fm_unpack(p,raw); }
    else groove_fm_factory(p,program);
}
/* The shared staging owner calls this before any project/tuning/library write. */
int groove_fm_library_recover(void)
{
    const struct bank *staged=read_bank(STAGE);
    if (!staged) return 0;
    int r=conflict(); if (r) return r;
    unsigned index=staged->index; uint32_t address=GROOVE_LIBRARY_OFFSET+index*4096U;
    fm1_engine_pause(); banks[index]=staged;
    const struct bank *stored=read_bank(address);
    if (!stored || stored->crc!=staged->crc || memcmp(stored,staged,sizeof *stored)) {
        r=fm1_flash_erase(address);
        for (unsigned at=0;!r && at<sizeof *staged;at+=64) {
            uint8_t buffer[64]; unsigned n=sizeof *staged-at; if (n>64) n=64;
            memcpy(buffer,(const uint8_t *)staged+at,n);
            r=fm1_flash_write(address+at,buffer,n);
        }
        stored=r?NULL:read_bank(address);
        if (!r && (!stored || stored->crc!=staged->crc)) r=FM1_FLASH_IO;
    }
    if (!r) { banks[index]=stored; r=fm1_flash_erase(STAGE); }
    fm1_engine_resume(); return r;
}
int groove_fm_library_read(unsigned index, uint8_t raw[4096])
{
    if (index>=4 || !raw) return GROOVE_PROJECT_INVALID;
    int r=conflict(); if (r) return r;
    if (blocked) { groove_fm_library_init(); if (blocked) return blocked; }
    for (unsigned i=0;i<32;++i) {
        if (banks[index]) unpack7(raw+i*128,banks[index]->voice[i]);
        else { uint8_t p[156]; groove_fm_factory(p,index*32+i); groove_fm_pack(raw+i*128,p); }
    }
    return 0;
}
int groove_fm_library_write(unsigned index, const uint8_t raw[4096])
{
    if (index>=4 || !raw) return GROOVE_PROJECT_INVALID;
    for (unsigned i=0;i<32;++i) if (!voice_valid(raw+i*128)) return GROOVE_PROJECT_INVALID;
    int r=conflict(); if (r) return r;
    if (blocked) { groove_fm_library_init(); if (blocked) return blocked; }
    r=groove_storage_prepare(); if (r) return r;
    struct bank *b=malloc(sizeof *b); if (!b) return GROOVE_PROJECT_MEMORY;
    b->magic=BANK_MAGIC; b->version=1; b->index=index; b->commit=~0U;
    for (unsigned i=0;i<32;++i) pack7(b->voice[i],raw+i*128);
    b->crc=checksum(b);
    if (banks[index] && banks[index]->crc==b->crc &&
        !memcmp(banks[index]->voice,b->voice,sizeof b->voice)) { free(b); return 0; }
    fm1_engine_pause();
    r=fm1_flash_erase(STAGE);
    if (!r) r=fm1_flash_write(STAGE,b,sizeof *b);
    if (!r) {
        const uint8_t *written=fm1_flash_map(STAGE,sizeof *b);
        if (!written || memcmp(written,b,sizeof *b)) r=FM1_FLASH_IO;
    }
    if (!r) { uint32_t commit=0; r=fm1_flash_write(STAGE+offsetof(struct bank,commit),&commit,4); }
    /* A driver may report failure after programming the complete commit word.
     * Make subsequent reads agree with reboot recovery in that ambiguous case. */
    const struct bank *staged=read_bank(STAGE);
    if (staged && staged->index==index) banks[index]=staged;
    if (!r && !staged) r=FM1_FLASH_IO;
    free(b);
    if (!r) r=groove_fm_library_recover();
    fm1_engine_resume(); return r;
}
