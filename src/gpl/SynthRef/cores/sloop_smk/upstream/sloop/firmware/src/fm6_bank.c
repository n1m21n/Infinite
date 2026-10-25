/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The FM6 patch bank (eng_fm6.c PTCH B1..B27): 27 packed 128-byte patches in one storage.c object
 * (OBJ_FM6BANK, A/B: 0xE5000 / 0xE6000). Unlike Felucca 1.0 (a RAM mirror in the pool) SLOOP reads it in
 * place through the plain XIP window, as the user sample slots: the device has little RAM left, and a PTCH
 * turn only copies 128 bytes out of flash (main loop). The current copy is found and checked once, at boot
 * and after every write (st_current: headers, CRC): fm6_bank_cur says which, fm6_bank_used which slots hold
 * a patch. The web editor writes it (EDITOR_PROTOCOL.md FM6_PUT / FM6_ERASE, while stopped); a full backup
 * carries it (object 8). An empty slot, or a bank of another layout, plays the init voice. A write stages
 * the bank in proj_tmp (the project load buffer: main loop, as a project load) and goes through st_save; the
 * payload ends 3472 + 256 bytes into its sector, the tail stays erased. Included by project.c (proj_tmp). */
#define FM6_BANK_MAGIC 0x42364D46u               /* "FM6B" */
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* 1, FM6_BANK_N */
    uint32_t used;                               /* bit k: slot k holds a patch */
    uint32_t rsv;
    uint8_t v[FM6_BANK_N][FM6_PACKED];
} fm6_bank_t;
_Static_assert(sizeof(fm6_bank_t) == 3472u, "FM6 bank layout");
_Static_assert(sizeof proj_tmp >= sizeof(fm6_bank_t), "the bank is staged in the project buffer");
static int8_t fm6_bank_cur = -1;                 /* the copy (0 A, 1 B) holding a valid bank, -1 = none */
static uint32_t fm6_bank_used;                   /* its used bits */
#ifndef FM6_BANK_XIP                             /* host tests: the simulated NOR */
#define FM6_BANK_XIP(copy) fm1_xip_ptr(st_sector(OBJ_FM6BANK, copy) + ST_PAYLOAD_OFF)
#endif
static const fm6_bank_t *fm6_bank_flash(uint32_t copy) { return (const fm6_bank_t *)FM6_BANK_XIP(copy & 1u); }

static int fm6_bank_valid(const fm6_bank_t *b)
{
    uint32_t k, i;
    if (b->magic != FM6_BANK_MAGIC || b->ver != 1u || b->nslot != FM6_BANK_N || (b->used >> FM6_BANK_N))
        return 0;
    for (k = 0; k < FM6_BANK_N; k++)
        for (i = 0; i < FM6_PACKED; i++)
            if (b->v[k][i] > 127u)
                return 0;
    return 1;
}

/* eng_fm6.c fm6_bank_read: slot k's record, 0 = there is one */
static int fm6_bank_get(uint32_t k, uint8_t *pk)
{
    if (k >= FM6_BANK_N || fm6_bank_cur < 0 || !((fm6_bank_used >> k) & 1u))
        return 1;
    memcpy(pk, fm6_bank_flash((uint32_t)fm6_bank_cur)->v[k], FM6_PACKED);
    return 0;
}

static int fm6_bank_has(uint32_t k) { return k < FM6_BANK_N && fm6_bank_cur >= 0 && ((fm6_bank_used >> k) & 1u); }

/* which copy holds a valid bank (the payload st_current leaves in st_buf is the one checked) */
static void fm6_bank_scan(void)
{
    st_hdr_t h;
    int c = flash_ok ? st_current(OBJ_FM6BANK, &h) : -1;
    fm6_bank_cur = -1;
    fm6_bank_used = 0;
    if (c >= 0 && h.len == sizeof(fm6_bank_t) && fm6_bank_valid((const fm6_bank_t *)st_buf)) {
        fm6_bank_cur = (int8_t)c;
        fm6_bank_used = ((const fm6_bank_t *)st_buf)->used;
    }
    fm6_bank_read = fm6_bank_get;
}

static void fm6_bank_boot(void) { fm6_bank_scan(); }   /* persist_boot */

/* the parts on a bank slot hear the bank as it is now (fm6_poll reloads them); a factory patch, or the
 * track's own, stays */
static void fm6_bank_changed(void)
{
    uint32_t t;
    for (t = 0; t < NPART; t++)
        if (fm6_slot[t] >= FM6_NFACTORY && fm6_slot[t] != 0xFFu)
            fm6_slot[t] = 0xFFu;
}

/* slot k = the packed record pk (0: erase), then the bank to flash: 0 ok, 1 bad slot, 2 flash error (the
 * bank is as it was). The record is stored through unpack / pack: every value in range. Main loop, the song
 * stopped (the caller checks: a sector erase silences the audio) */
static int fm6_bank_put(uint32_t k, const uint8_t *pk)
{
    fm6_bank_t *b = (fm6_bank_t *)&proj_tmp;
    uint8_t v[FP_SIZE + 1u];
    if (k >= FM6_BANK_N)
        return 1;
    if (!flash_ok)
        return 2;
    if (fm6_bank_cur >= 0)
        memcpy(b, fm6_bank_flash((uint32_t)fm6_bank_cur), sizeof *b);
    else
        memset(b, 0, sizeof *b);
    b->magic = FM6_BANK_MAGIC;
    b->ver = 1;
    b->nslot = FM6_BANK_N;
    b->rsv = 0;
    if (pk) {
        fm6_unpack(pk, v);
        fm6_pack(v, b->v[k]);
        b->used |= 1u << k;
    } else {
        memset(b->v[k], 0, FM6_PACKED);
        b->used &= ~(1u << k);
    }
    if (st_save(OBJ_FM6BANK, b, sizeof *b)) {
        fm6_bank_scan();                             /* (the copy that was current is untouched) */
        return 2;
    }
    fm6_bank_scan();
    fm6_bank_changed();
    return 0;
}
