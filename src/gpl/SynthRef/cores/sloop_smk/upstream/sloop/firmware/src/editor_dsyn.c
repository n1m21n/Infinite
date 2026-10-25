/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v10 (SLOOP 2.5): the DRUM SYNTH page, your synthesised kits SYN1..SYN4 (drum_synth.c dsu).
 * A sound travels as its 22 dsnd_t bytes (pack7), in the struct's order. The editor changes the kits in RAM
 * (heard at once), STORE writes them to flash with the settings (only while the song is stopped: rc 3).
 *   72 DSYN_LIST  -> factory count, user count, stored (0: an edit not stored yet), the factory names,
 *                    then per user kit: its name, the factory kit it started from
 *   73 DSYN_GET   which (0.. a factory kit, 64 + k: SYNk+1) -> which, rc, name, pack7(crush, src, 16 x 22)
 *   74 DSYN_PUT   k, part, pack7 data -> k, part, rc. part 0..15: a sound (22 bytes); 16: name (8) + crush +
 *                 src; 17: the whole kit from a factory kit (1 byte: its index)
 *   75 DSYN_STORE -> rc (0 ok, 3 stop the song first, 4 flash)
 *   76 DSYN_PLAY  k, lane, velocity -> k, lane, rc: the sound, on the drum voices (the drum track's level) */
#ifndef UI_SMALL                 /* (2.7) compiled for size: not the audio path (gfx.c UI_SMALL); the host tests: nothing */
#define UI_SMALL
#endif
enum { ED_DSYN_LIST = 72, ED_DSYN_GET, ED_DSYN_PUT, ED_DSYN_STORE, ED_DSYN_PLAY };
#define ED_DSYN_USER 64u

UI_SMALL static void ed_dsyn_name(const char *n8)
{
    char s[9];
    memcpy(s, n8, 8);
    s[8] = 0;
    ed_str(s, 8);
}
UI_SMALL static uint32_t ed_dsyn_store(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (ed_flash_busy())
        return 3;
    if (!flash_ok || ed_bk_put)
        return 4;
    persist_fill(&p);
    if (settings_write(&p))
        return 4;
    persist_saved = p;
    return 0;
#else
    return 4;
#endif
}

UI_SMALL static int ed_dsyn_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint8_t b[2u + DS_LANES * sizeof(dsnd_t)];
    uint32_t i, rc;
    (void)dsu_kit(0);                                      /* (set up) */
    switch (cmd) {
    case ED_DSYN_LIST:
        if (n)
            return 0;
        ed_b(DS_NKITS); ed_b(DSU_N); ed_b(!dsu_dirty);
        for (i = 0; i < DS_NKITS; i++)
            ed_str(DS_KITS[i].name, 8);
        for (i = 0; i < DSU_N; i++) {
            ed_dsyn_name(dsu.k[i].name);
            ed_b(dsu.k[i].src);
        }
        return 1;
    case ED_DSYN_GET: {
        const dsnd_t *s = 0;
        uint32_t w = n == 1u ? a[0] : 127u, crush = 0, src = 0;
        char nm[8] = {0};
        if (w < DS_NKITS) {
            s = DS_KITS[w].s;
            crush = DS_KITS[w].crush;
            src = w;
            for (i = 0; i < 8u && DS_KITS[w].name[i]; i++)
                nm[i] = DS_KITS[w].name[i];
        } else if (w >= ED_DSYN_USER && w < ED_DSYN_USER + DSU_N) {
            const dsu_kit_t *u = &dsu.k[w - ED_DSYN_USER];
            s = u->s;
            crush = u->crush;
            src = u->src;
            memcpy(nm, u->name, 8);
        }
        ed_b(w); ed_b(s ? 0u : 1u);
        if (s) {
            ed_dsyn_name(nm);
            b[0] = (uint8_t)crush;                         /* (one pack7 stream: crush, src, the sounds) */
            b[1] = (uint8_t)src;
            memcpy(b + 2, s, DS_LANES * sizeof(dsnd_t));
            ed_pack7(b, sizeof b);
        }
        return 1;
    }
    case ED_DSYN_PUT: {
        uint32_t k = n >= 2u ? a[0] : 127u, part = n >= 2u ? a[1] : 127u, m;
        rc = 1;
        if (k < DSU_N && n > 2u) {
            m = ed_unpack7(a + 2, n - 2u, b, sizeof b);
            dsu_kit_t *u = &dsu.k[k];
            if (part < DS_LANES && m == sizeof(dsnd_t)) {
                dsnd_t d;
                memcpy(&d, b, sizeof d);
                dsu_fix_sound(&d);
                memcpy(&u->s[part], &d, sizeof d);         /* (the voices read it from the next block) */
                rc = 0;
            } else if (part == DS_LANES && m == 10u) {
                memcpy(u->name, b, 8);
                u->crush = b[8];
                u->src = (uint8_t)(b[9] % DS_NKITS);
                for (i = 0; i < 8u; i++)
                    if (u->name[i] && (u->name[i] < 32 || u->name[i] > 126))
                        u->name[i] = ' ';
                rc = 0;
            } else if (part == DS_LANES + 1u && m == 1u && b[0] < DS_NKITS) {
                dsu_from_factory(k, b[0]);
                rc = 0;
            }
            if (!rc) {
                dsu_dirty = 1;
                ui.force = 1;
            }
        }
        ed_b(k & 127u); ed_b(part & 127u); ed_b(rc);
        return 1;
    }
    case ED_DSYN_STORE:
        if (n)
            return 0;
        ed_b(ed_dsyn_store());
        return 1;
    case ED_DSYN_PLAY:
        rc = n != 3u || a[0] >= DSU_N || a[1] >= DS_LANES || !a[2] ? 1u : 0u;
        if (!rc) {
            dsu_aud_k = a[0];
            dsu_aud_lane = a[1];
            dsu_aud_vel = a[2];                            /* (last: the ISR plays it, drums.c drum_audition_poll) */
        }
        ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u); ed_b(rc);
        return 1;
    }
    return 0;
}
