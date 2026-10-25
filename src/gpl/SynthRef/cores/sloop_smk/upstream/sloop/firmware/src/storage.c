/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload (from offset 256),
 * then the 32-byte header at offset 0 last. The header is the commit record;
 * on load the valid copy with the highest seq wins, so a torn write leaves
 * the previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#ifndef UI_SMALL                 /* (2.7) compiled for size: not the audio path (gfx.c UI_SMALL); the host tests and the loader: nothing */
#define UI_SMALL
#endif
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)

/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): settings 0xFC000 / 0xFD000, projects
 * 0x97000..0x9EFFF, user sample slots 0xA0000..0xDBFFF (eng_sample.c), user preset banks 0xDC000..0xDFFFF
 * (upreset.c); the working project (autosave, project.c): copy A 0x9F000, copy B 0xFE000 (the two sectors
 * left: A/B needs no two neighbours); the FM6 patch bank (fm6_bank.c, SLOOP 2.4): 0xE5000 / 0xE6000, the first
 * two sectors of the free 0xE5000..0xFBFFF after the update staging (FL_FM6, fm1_flash.h) */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_AUTOSAVE = OBJ_UPRESET0 + 2, OBJ_FM6BANK, OBJ_COUNT };

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32(const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

UI_SMALL static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
#ifdef SLOOP_STORAGE_SECTOR
    return SLOOP_STORAGE_SECTOR(obj, copy);
#else
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_AUTOSAVE)
        return copy ? 0xFE000u : 0x9F000u;
    if (obj == OBJ_FM6BANK)
        return 0xE5000u + copy * ST_SECTOR;
    if (obj >= OBJ_UPRESET0 && obj < OBJ_AUTOSAVE)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0x97000u + (obj - OBJ_PROJECT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
#endif
}

#include "scratch.h"                          /* sloop-lab: the payload buffer is the main loop's shared scratch */
#define st_buf (main_scratch.st)
_Static_assert(sizeof main_scratch.st == ST_PAYLOAD_MAX, "the scratch holds a payload");

UI_SMALL static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (obj >= OBJ_COUNT || copy > 1u)
        return -1;
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > ST_PAYLOAD_MAX ||   /* (slot: the copy
                                                     * it was written to; after Felucca 1.0) */
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

UI_SMALL static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* payload -> st_buf, CRC ok: 0 */
{
    if (st_read(st_sector(obj, copy) + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}

/* the current copy: the valid one with the highest seq (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is left in st_buf); *h gets its header. */
UI_SMALL static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || b.seq > a.seq)) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

#ifdef ST_XIP_PTR                                  /* sloop-lab: the firmware (felucca.c); the host tests have no XIP */
/* the current copy's payload where it is, in the flash's plain XIP window (fm1_flash.h fl_plain_window_init; the
 * driver drops the cache lines it erases and programs, so a save is seen at once): its CRC is checked through the
 * window, nothing is copied. 0 = no valid copy, else *len = the payload's length. The copy is the one st_current
 * would take: the valid header with the highest seq, A on a tie, the other when its payload fails. */
UI_SMALL static const void *st_xip(uint32_t obj, uint32_t *len)
{
    st_hdr_t h[2];
    int v[2], i, first;
    v[0] = st_head(obj, 0, &h[0]) == 0;
    v[1] = st_head(obj, 1, &h[1]) == 0;
    first = v[1] && (!v[0] || h[1].seq > h[0].seq) ? 1 : 0;
    for (i = 0; i < 2; i++) {
        uint32_t c = (uint32_t)(first ^ i);
        const uint8_t *p = ST_XIP_PTR(st_sector(obj, c) + ST_PAYLOAD_OFF);
        if (v[c] && st_crc32(p, h[c].len) == h[c].crc) {
            *len = h[c].len;
            return p;
        }
    }
    return 0;
}
#endif

/* load object into dst (up to max bytes); returns the length, or -1 */
UI_SMALL static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (st_current(obj, &h) < 0)
        return -1;
    if (h.len > max)
        h.len = max;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

/* SLOOP 2.7: a side record in a copy's own sector, between the header (0..31) and the payload (256..): bytes
 * ST_SIDE_OFF.. carry what an older SLOOP must never see in the payload (it checks the payload's exact length:
 * the settings object's would no longer be one it reads). st_save3 programs st_side there with the payload,
 * before the header, so it is committed with its copy; an older save leaves the area erased (0xFF), and the record
 * goes with that copy. The record checks itself (project.c: a magic and a CRC): st_side_load reads the bytes. */
#define ST_SIDE_OFF 64u
#define ST_SIDE_MAX (ST_PAYLOAD_OFF - ST_SIDE_OFF)
static const void *st_side;                         /* the next save's side record (0: none), for one save */
static uint32_t st_side_len;
_Static_assert(sizeof(st_hdr_t) <= ST_SIDE_OFF, "the side record starts after the header");
/* the current copy's side bytes (n <= ST_SIDE_MAX) into dst: 0, or -1 (no valid copy) */
UI_SMALL static int st_side_load(uint32_t obj, void *dst, uint32_t n)
{
    st_hdr_t h;
    int c = st_current(obj, &h);
    if (c < 0 || n > ST_SIDE_MAX)
        return -1;
    return st_read(st_sector(obj, (uint32_t)c) + ST_SIDE_OFF, dst, n) ? -1 : 0;
}

/* an object in up to three parts, src then src2 then src3 (SLOOP 2.5: the settings and the SYN kits after them;
 * 2.6: then the MIDI LEARN map) */
UI_SMALL static int st_save3(uint32_t obj, const void *src, uint32_t len, const void *src2, uint32_t len2, const void *src3,
                    uint32_t len3)
{
    uint32_t seq, base, off;
    int cur, rc;
    st_hdr_t h;
    if (obj >= OBJ_COUNT || len + len2 + len3 > ST_PAYLOAD_MAX)
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    base = st_sector(obj, cur == 0 ? 1u : 0u);       /* write the other copy */
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    for (off = 0; off < len2; off++)
        st_buf[len + off] = ((const uint8_t *)src2)[off];
    len += len2;
    for (off = 0; off < len3; off++)
        st_buf[len + off] = ((const uint8_t *)src3)[off];
    len += len3;
    if ((rc = st_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return rc;
    }
    if (st_side && st_side_len && st_side_len <= ST_SIDE_MAX) {   /* (2.7) the side record, before the commit */
        uint8_t sb[ST_SIDE_MAX];
        for (off = 0; off < st_side_len; off++)
            sb[off] = ((const uint8_t *)st_side)[off];      /* (RAM source) */
        if ((rc = st_prog(base + ST_SIDE_OFF, sb, st_side_len)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)(cur == 0 ? 1 : 0);
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = cur == 0 ? 1u : 0u;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))   /* the whole record */
            return -7;
    }
    return 0;
}
UI_SMALL static int st_save2(uint32_t obj, const void *src, uint32_t len, const void *src2, uint32_t len2)
{
    return st_save3(obj, src, len, src2, len2, 0, 0);
}
UI_SMALL static int st_save(uint32_t obj, const void *src, uint32_t len) { return st_save2(obj, src, len, 0, 0); }
