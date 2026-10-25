/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Editor protocol v9: the FM6 patches (EDITOR_PROTOCOL.md "FM6 patches", cmds 68..71, Felucca 1.0's numbers).
 * A patch travels as the 128-byte packed record (every byte 7-bit: no pack7). Targets: 0 a part's own patch
 * (track 0..NPART-1; the drum track has none), 1 a bank slot (0..FM6_BANK_N-1, flash: only while the song is
 * stopped, rc 3 as the other flash writes), 2 a factory patch (0..FM6_NFACTORY-1, read only). */
#ifndef UI_SMALL                 /* (2.7) compiled for size: not the audio path (gfx.c UI_SMALL); the host tests: nothing */
#define UI_SMALL
#endif
enum { ED_FM6_TRACK, ED_FM6_BANK, ED_FM6_FACTORY };

UI_SMALL static void ed_fm6_name(const uint8_t *pk)       /* the record's name, trailing spaces off */
{
    char s[11];
    uint32_t i, n = 0;
    for (i = 0; i < 10u; i++) {
        s[i] = (char)(pk[118 + i] >= 32u && pk[118 + i] <= 126u ? pk[118 + i] : ' ');
        if (s[i] != ' ')
            n = i + 1u;
    }
    s[n] = 0;
    ed_str(s, 10);
}

/* a bank write: 0 ok, 1 bad slot, 2 flash (or a backup restore holds the staging RAM), 3 stop the song first */
UI_SMALL static uint32_t ed_fm6_bank_write(uint32_t k, const uint8_t *pk)
{
#if FELUCCA_FLASH
    int r;
    if (ed_flash_busy())
        return 3;
    if (ed_bk_put)
        return 2;
    r = fm6_bank_put(k, pk);
    return r == 1 ? 1u : r ? 2u : 0u;
#else
    (void)k;
    (void)pk;
    return 2;
#endif
}

UI_SMALL static int ed_fm6_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint8_t pk[FM6_PACKED];
    uint32_t i, rc;
    switch (cmd) {
    case ED_FM6_GET:                                       /* target, index -> target, index, rc, [128 bytes] */
        rc = n != 2u || a[0] > ED_FM6_FACTORY ? 1u : 0u;
        if (!rc) {
            if (a[0] == ED_FM6_TRACK && a[1] < NPART)
                fm6_pack(fm6_patch[a[1]], pk);
            else if (a[0] == ED_FM6_BANK && a[1] < FM6_BANK_N)
                rc = !fm6_bank_read || fm6_bank_read(a[1], pk) ? 2u : 0u;
            else if (a[0] == ED_FM6_FACTORY && a[1] < FM6_NFACTORY)
                memcpy(pk, FM6_FACTORY[a[1]], FM6_PACKED);
            else
                rc = 1;
        }
        ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u); ed_b(rc);
        for (i = 0; !rc && i < FM6_PACKED; i++)
            ed_b(pk[i]);
        return 1;
    case ED_FM6_PUT:                                       /* target, index, 128 bytes -> target, index, rc */
        rc = n != 2u + FM6_PACKED ? 1u : 0u;
        if (!rc && a[0] == ED_FM6_TRACK && a[1] < NPART) {
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(a + 2, v);                          /* (every value into its range) */
            fm6_set_patch(a[1], v);
            fm6_slot[a[1]] = (uint8_t)trk[a[1]].p[P_E7];   /* the part's own patch now, PTCH as it is: fm6_poll keeps
                                                            * it until PTCH turns or a sound loads */
            ui.force = 1;
        } else if (!rc && a[0] == ED_FM6_BANK && a[1] < FM6_BANK_N) {
            rc = ed_fm6_bank_write(a[1], a + 2);
        } else {
            rc = 1;
        }
        ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u); ed_b(rc);
        return 1;
    case ED_FM6_LIST:                                      /* -> factory count, bank count, then per slot: used, name */
        if (n)
            return 0;
        ed_b(FM6_NFACTORY); ed_b(FM6_BANK_N);
        for (i = 0; i < FM6_NFACTORY + FM6_BANK_N; i++) {
            uint32_t used = i < FM6_NFACTORY || (fm6_bank_read && !fm6_bank_read(i - FM6_NFACTORY, pk));
            ed_b(used);
            if (used)
                ed_fm6_name(i < FM6_NFACTORY ? FM6_FACTORY[i] : pk);
            else
                ed_b(0);
        }
        return 1;
    case ED_FM6_ERASE:                                     /* bank index -> index, rc */
        rc = n != 1u || a[0] >= FM6_BANK_N ? 1u : ed_fm6_bank_write(a[0], 0);
        ed_b(n ? a[0] : 127u); ed_b(rc);
        return 1;
    }
    return 0;
}
