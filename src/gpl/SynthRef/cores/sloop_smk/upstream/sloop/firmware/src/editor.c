/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Editor protocol: SysEx for the web editor (web/EDITOR_PROTOCOL.md; v2 = user presets + live sync,
 * v3 = four tracks: the v1 / v2 commands act on the selected track, cmds 27-30 reach any track;
 * v4 = TRACK_PARAM (31) and the TRACK_CHANGED push (32), enabled by WATCH bit 1;
 * v5 = SLOOP 2.0: INFO ends with the protocol version (5), steps carry level / ratchet bytes,
 * DRUM_STEP (33) reads / writes the drum track's 16 lanes, TRACK ends with the solo mask;
 * v6 = SLOOP 2.3: backup / restore (34-36); v7 = SLOOP 2.4: the steps' nudges and parameter locks (37-40);
 * v8 = SLOOP 2.4: the steps' fill conditions (41-42); v9 = SLOOP 2.4: the FM6 engine's patches (68-71,
 * editor_fm6.c: Felucca 1.0's numbers) and the patch bank as backup object 8); v10 = SLOOP 2.5: the SYN drum
 * kits (72-76, editor_dsyn.c) and backup object 9.
 *   F0 7D 46 4C cmd args.. F7     (7D = non-commercial ID, "FL")
 * Values are 14 bit, two 7-bit bytes LSB first, offset by 8192 (so -8192..8191).
 * Every request gets a reply with the same cmd; 23/24/26 are also pushed
 * while watched. Frames arrive through sx_frame (usb.c), replies leave
 * through ota_wire_send(). */
#ifndef UI_SMALL                 /* (2.7) compiled for size: not the audio path (gfx.c UI_SMALL); the host tests: nothing */
#define UI_SMALL
#endif
#define ED_HDR0 0x7D
#define ED_HDR1 0x46
#define ED_HDR2 0x4C
enum { ED_INFO = 1, ED_GET, ED_SET, ED_DUMP, ED_DESC, ED_STEP_GET, ED_STEP_SET, ED_PRESET, ED_PROJECT, ED_NAMES,
       ED_SMP_BEGIN, ED_SMP_WRITE, ED_SMP_END, ED_SMP_ERASE, ED_SMP_INFO,
       ED_UP_LIST, ED_UP_GET, ED_UP_PUT, ED_UP_STORE, ED_UP_LOAD, ED_UP_ERASE,   /* v2: user presets */
       ED_WATCH, ED_CHANGED, ED_RELOAD, ED_PING, ED_STEP_CHANGED,              /* v2: live sync */
       ED_TRACK, ED_TRACK_MIX, ED_TRACK_DUMP, ED_TRACK_STEP,                    /* v3: tracks */
       ED_TRACK_PARAM, ED_TRACK_CHANGED,                                        /* v4: any track's parameters */
       ED_DRUM_STEP,                                                            /* v5: the 16 drum lanes */
       ED_BK_LIST, ED_BK_GET, ED_BK_PUT,                                        /* v6: backup / restore */
       ED_LOCK_GET, ED_LOCK_SET, ED_MICRO_GET, ED_MICRO_SET,                    /* v7: parameter locks, nudges */
       ED_FILL_GET, ED_FILL_SET,                                                /* v8: fill conditions */
       ED_FM6_GET = 68, ED_FM6_PUT, ED_FM6_LIST, ED_FM6_ERASE };                /* v9: FM6 patches (Felucca's numbers) */
/* v10 (SLOOP 2.5): DSYN_LIST .. DSYN_PLAY = 72..76, editor_dsyn.c; backup object 9 = the SYN kits */
/* v11 (SLOOP 2.7): backup object 11 = your theme (gfx.c theme_t), the editor's Theme page */
/* v12 (SLOOP 2.7): LIVE = 77, editor_live.c: the LIVE page (transport, sections, mix keys, punch-in FX, REC, undo) */
#define ED_PROTO 12u                                   /* the protocol version INFO ends with */

static uint8_t ed_out[600];
static uint32_t ed_n;

UI_SMALL static void ed_begin(uint32_t cmd)
{
    ed_out[0] = 0xF0;
    ed_out[1] = ED_HDR0;
    ed_out[2] = ED_HDR1;
    ed_out[3] = ED_HDR2;
    ed_out[4] = (uint8_t)cmd;
    ed_n = 5;
}
UI_SMALL static void ed_b(uint32_t v)
{
    if (ed_n < sizeof ed_out - 1u)
        ed_out[ed_n++] = (uint8_t)(v & 0x7Fu);
}
UI_SMALL static void ed_v(int32_t v)
{
    uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192);
    ed_b(u);
    ed_b(u >> 7);
}
UI_SMALL static void ed_str(const char *s, uint32_t max)   /* ASCII, 0-terminated */
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}
UI_SMALL static void ed_send(void)
{
    ed_out[ed_n++] = 0xF7;
    ota_wire_send(ed_out, ed_n);
}
UI_SMALL static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }

/* ---- user sample slots (eng_sample.c): flash SMP_USER_OFF(k) (USR1..3 at 0xA0000.., USR4 at 0xE7000) ----
 * BEGIN erases the header sector (the slot is invalid from then on), WRITE fills the data
 * (offset >= 512, erasing each further sector when the write reaches its start), END sends
 * the header: the device checks the data CRC and writes the header last. */
UI_SMALL static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;                                 /* groups: msb byte, then up to 7 bytes */
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
UI_SMALL static void ed_pack7(const uint8_t *p, uint32_t n)      /* pack7: a top-bits byte, then up to 7 bytes */
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < k; i++)
            m |= (uint32_t)(p[i] >> 7) << i;
        ed_b(m);
        for (i = 0; i < k; i++)
            ed_b(p[i] & 127u);
        p += k;
        n -= k;
    }
}
static uint8_t ed_smp_buf[512] __attribute__((aligned(4),section(".pool")));
static uint8_t ed_smp_open[SMP_USER_SLOTS];        /* SMP_BEGIN done, END not yet: WRITE / END may act */
UI_SMALL static uint32_t ed_smp_slot(uint32_t k) {
#ifdef SLOOP_SMK_HARDWARE
    (void)k;return 0x100000u; /* physical sample commands disabled */
#else
    return SMP_USER_OFF(k);
#endif
}
UI_SMALL static void ed_smp_inval(uint32_t k)
{
    fm1_irq_off();
    fl_inval(ed_smp_slot(k), SMP_USER_SIZE);
    fm1_irq_on();
}
UI_SMALL static int ed_smp_erase(uint32_t k, uint32_t all)  /* header sector, or the whole slot */
{
    uint32_t i, took;
    int rc = 0;
    usr_nz[k] = 0;
    for (i = 0; i < 16u; i++)
        usr_zone[k][i].n = 0;                     /* a sounding voice ends instead of reading 0xFF */
    for (i = 0; i < (all ? SMP_USER_SIZE / 0x1000u : 1u) && !rc; i++) {
        rc = fl_erase4k_quiet(ed_smp_slot(k) + i * 0x1000u, &took);
        fm1_wdt_feed();
    }
    ed_smp_inval(k);
    return rc;
}
UI_SMALL static int ed_smp_end(uint32_t k, const uint8_t *a, uint32_t na)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)ed_smp_buf;
    uint32_t i;
    if (!ed_smp_open[k] || usr_nz[k])
        return 6;                                  /* no BEGIN first (a header over a header: flash ANDs them) */
    if (ed_unpack7(a, na, ed_smp_buf, sizeof(smp_user_hdr_t)) != sizeof(smp_user_hdr_t))
        return 1;
    if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u ||
        h->data_len > SMP_USER_SIZE - SMP_USER_DATA)
        return 2;
    for (i = 0; i < h->nz; i++)                    /* the zones checked before anything is written */
        if (!smp_zone_ok(&h->zone[i], h->data_len))
            return 2;
    ed_smp_open[k] = 0;
    ed_smp_inval(k);
    if (st_crc32(smp_user_xip(k) + SMP_USER_DATA, h->data_len) != h->crc)
        return 3;
    if (fl_write(ed_smp_slot(k), ed_smp_buf, sizeof(smp_user_hdr_t)))
        return 4;
    ed_smp_inval(k);
    smp_user_scan(k);
    return usr_nz[k] ? 0 : 5;
}

/* the engine byte of DUMP / RELOAD / TRACK: NENGINES = the drum track (no engine) */
UI_SMALL static uint32_t ed_eng(const track_t *t) { return is_drum(t) ? NENGINES : t->eng_req % NENGINES; }

/* ---- live sync (v2): while the editor WATCHes, device-side changes are pushed.
 * Shadows of the selected track's p[] + song.g[] and of its steps are kept in step
 * with what the editor knows (its own SET / STEP_SET update them); the main loop
 * compares and pushes CHANGED / STEP_CHANGED, or RELOAD after a load or when another
 * track was selected (v3: RELOAD and STEP_CHANGED carry the selected track). Pushes
 * go out only into a half-empty SysEx ring, so they never wait. */
#define ED_PUSH_MAX 4u                                   /* frames per pass */
#define ED_NV (P_COUNT + G_COUNT)
/* v4: the parameters of the tracks that are not selected which TRACK_CHANGED follows (the mixer) */
static const uint8_t ED_TIDS[3] = {P_LEVEL, P_PAN, P_MUTE};
#define ED_NT (NTRK * 3u)
static struct {
    uint8_t on, eng, preset, sel;
    uint8_t v4;                                          /* WATCH bit 1: TRACK_CHANGED pushes too */
    uint32_t pos, last_ms, run_ms, resets;
    int16_t v[ED_NV];                                    /* TSEL->p[], then song.g[] */
    uint16_t t[ED_NV];                                   /* ms (low 16 bits) of the last push */
    uint32_t st[NSTEP];                                  /* step signatures */
    int16_t tv[ED_NT];                                   /* v4: trk[k].p[ED_TIDS[j]] at k * 3 + j */
    uint16_t tt[ED_NT];
    uint32_t tpos;
} ed_w;

UI_SMALL static int16_t *ed_val(uint32_t i) { return i < P_COUNT ? &TSEL->p[i] : &song.g[i - P_COUNT]; }
UI_SMALL static uint32_t ed_step_sig(const track_t *t, uint32_t i)   /* all 10 bytes (a drum step's lanes too), v7: + its nudge and locks, v8: + its condition */
{
    const uint8_t *b = (const uint8_t *)&t->step[i % NSTEP];
    uint32_t k, h = 0x811C9DC5u;
    for (k = 0; k < sizeof t->step[0]; k++)
        h = (h ^ b[k]) * 16777619u;
    h = (h ^ (uint8_t)t->micro[i % NSTEP]) * 16777619u;
    h = (h ^ (step_fill(t, i) + 1u)) * 16777619u;        /* v8: its fill condition */
    for (k = 0; k < NLOCK; k++)
        if (t->lock[k].step == i)
            h = (h ^ (uint32_t)(t->lock[k].param | (uint16_t)t->lock[k].val << 8)) * 16777619u;
    return h;
}
/* the drum track's step as a v1..v4 step (old editors): its first 4 lanes as GM notes, ACC when one is hard */
UI_SMALL static void ed_dstep_old(const dstep_t *d, uint32_t *n, uint8_t *notes, uint32_t *time, uint32_t *flags, uint32_t *vel)
{
    uint32_t l, k = 0, hard = 0;
    for (l = 0; l < DRUM_LANES && k < 4u; l++)
        if (dstep_has(d, l)) {
            notes[k++] = LANE_NOTE[l];
            hard |= dstep_lvl(d, l) == LV_HARD;
        }
    *n = k;
    while (k < 4u)
        notes[k++] = 0;
    *time = *n ? ST_NOTE : ST_REST;
    *flags = hard ? SF_ACCENT : 0u;
    *vel = *n ? 100u : 0u;
}
/* a v1..v4 step written into the drum track: its notes onto their lanes */
UI_SMALL static void ed_dstep_from_old(dstep_t *d, const uint8_t *a)
{
    uint32_t i, n = a[0] > 4u ? 4u : a[0];
    memset(d, 0, sizeof *d);
    if (a[5] != ST_NOTE)
        return;
    for (i = 0; i < n; i++)
        dstep_set(d, lane_of_note(a[1 + i] & 0x7Fu), (a[6] & SF_ACCENT) ? LV_HARD : LV_NORM, 0);
}
/* a step's bytes, as STEP_GET sends them (v5: + level, ratchet) */
UI_SMALL static void ed_step_out(const track_t *t, uint32_t i)
{
    uint32_t k;
    if (is_drum(t)) {
        uint32_t n, time, flags, vel;
        uint8_t nt[4];
        ed_dstep_old(&t->dstep[i], &n, nt, &time, &flags, &vel);
        ed_b(n);
        for (k = 0; k < 4u; k++)
            ed_b(nt[k]);
        ed_b(time);
        ed_b(flags);
        ed_b(vel);
        ed_b(0);                                         /* v5: lvl, hi, rat (DRUM_STEP has the lanes' own) */
        ed_b(0);
        ed_b(0);
        return;
    }
    {
        const step_t *st = &t->step[i];
        ed_b(st->n);
        for (k = 0; k < 4u; k++)
            ed_b(st->note[k]);
        ed_b(st->time);
        ed_b(st->flags);
        ed_b(st->vel);
        ed_b(st->lvl);                                   /* v5 (7 bits each: 4 x 2-bit fields, the top 1 below) */
        ed_b(st->lvl >> 7 | (st->rat >> 7) << 1);
        ed_b(st->rat);
    }
}
/* a step written: n, 4 notes, time, flags, vel [, lvl, hi, rat] */
UI_SMALL static void ed_step_in(track_t *t, uint32_t i, const uint8_t *a, uint32_t na)
{
    uint32_t k;
    if (is_drum(t)) {
        ed_dstep_from_old(&t->dstep[i], a);
        return;
    }
    {
        step_t *st = &t->step[i];
        st->n = (uint8_t)(a[0] > 4u ? 4u : a[0]);
        for (k = 0; k < 4u; k++)
            st->note[k] = a[1 + k] & 0x7Fu;
        st->time = (uint8_t)(a[5] > ST_REST ? ST_REST : a[5]);
        st->flags = a[6] & (SF_ACCENT | SF_SLIDE);
        st->vel = a[7] & 0x7Fu;
        if (na >= 11u) {
            st->lvl = (uint8_t)((a[8] & 0x7Fu) | (a[9] & 1u) << 7);
            st->rat = (uint8_t)((a[10] & 0x7Fu) | ((a[9] >> 1) & 1u) << 7);
        } else {
            st->lvl = st->rat = 0;
        }
    }
}
UI_SMALL static void ed_shadow(void)                              /* the editor is in sync */
{
    uint32_t i;
    for (i = 0; i < ED_NV; i++)
        ed_w.v[i] = *ed_val(i);
    for (i = 0; i < NSTEP; i++)
        ed_w.st[i] = ed_step_sig(TSEL, i);
    for (i = 0; i < ED_NT; i++)
        ed_w.tv[i] = trk[i / 3u].p[ED_TIDS[i % 3u]];
    ed_w.eng = (uint8_t)ed_eng(TSEL);
    ed_w.preset = TSEL->preset;
    ed_w.sel = song.sel;
    sync_reload = 0;
}
UI_SMALL static int ed_room(void) { return so_w - so_r + 8u <= SXQ / 2u; }
UI_SMALL static void ed_known(uint32_t k, uint32_t id)            /* the editor's own change of trk[k].p[id]: no push */
{
    uint32_t j;
    if (k == song.sel) {
        ed_w.v[id] = trk[k].p[id];
        return;
    }
    for (j = 0; j < 3u; j++)
        if (ED_TIDS[j] == id)
            ed_w.tv[k * 3u + j] = trk[k].p[id];
}

UI_SMALL static void ed_sync(void)                                /* main loop */
{
    uint32_t i, n = 0, now = fm1_ms;
    if (!ed_w.on || now - ed_w.run_ms < 5u)
        return;
    ed_w.run_ms = now;
    if (!usb.config || usb.resets != ed_w.resets || now - ed_w.last_ms > 3000u) {
        ed_w.on = 0;                                     /* no host, USB reset, or 3 s without a request */
        return;
    }
    if (sync_reload || ed_eng(TSEL) != ed_w.eng || TSEL->preset != ed_w.preset || song.sel != ed_w.sel) {
        if (!ed_room())
            return;
        ed_shadow();
        ed_begin(ED_RELOAD);
        ed_b(ed_eng(TSEL));
        ed_b(TSEL->preset);
        ed_b(song.sel);
        ed_send();
        return;
    }
    for (i = 0; i < NSTEP && n < ED_PUSH_MAX; i++) {
        uint32_t h = ed_step_sig(TSEL, i);
        if (h == ed_w.st[i])
            continue;
        if (!ed_room())
            return;
        ed_w.st[i] = h;
        ed_begin(ED_STEP_CHANGED);
        ed_b(i);
        ed_b(song.sel);
        ed_send();
        n++;
    }
    for (i = 0; i < ED_NV && n < ED_PUSH_MAX; i++) {    /* round robin: nothing starves */
        uint32_t k = (ed_w.pos + i) % ED_NV;
        int16_t v = *ed_val(k);
        if (v == ed_w.v[k] || (uint16_t)(now - ed_w.t[k]) < 20u)
            continue;                                    /* coalesced: the latest value goes out later */
        if (!ed_room())
            return;
        ed_w.v[k] = v;
        ed_w.t[k] = (uint16_t)now;
        ed_begin(ED_CHANGED);
        ed_b(k < P_COUNT ? 0u : 1u);
        ed_b(k < P_COUNT ? k : k - P_COUNT);
        ed_v(v);
        ed_send();
        n++;
        ed_w.pos = k + 1u;
    }
    for (i = 0; ed_w.v4 && i < ED_NT && n < ED_PUSH_MAX; i++) {   /* v4: the other tracks' mix */
        uint32_t k = (ed_w.tpos + i) % ED_NT, tr = k / 3u, id = ED_TIDS[k % 3u];
        int16_t v = trk[tr].p[id];
        if (tr == song.sel || v == ed_w.tv[k] || (uint16_t)(now - ed_w.tt[k]) < 20u)
            continue;                                    /* (the selected track: CHANGED above) */
        if (!ed_room())
            return;
        ed_w.tv[k] = v;
        ed_w.tt[k] = (uint16_t)now;
        ed_begin(ED_TRACK_CHANGED);
        ed_b(tr);
        ed_b(id);
        ed_v(v);
        ed_send();
        n++;
        ed_w.tpos = k + 1u;
    }
}

/* descriptor of parameter id of track t: the engine parameters of the engine it asked for
 * (t->engine follows in the audio ISR, after a short fade) */
static const param_desc_t *ed_tdesc(const track_t *t, uint32_t id)   /* the static ones: an engine's */
{                                                                     /* desc hook is the device display only */
    if(is_drum(t) && id==P_E0) return &DRUM_KIT_DESC;
    if (id >= P_E0 && id <= P_E7)
        return &ENGINES[t->eng_req % NENGINES]->edit[id - P_E0];
    return &TP[id];
}
/* descriptor and value slot of (scope, id): scope 0 = the selected track, 1 = global */
UI_SMALL static const param_desc_t *ed_desc(uint32_t scope, uint32_t id, int16_t **vp)
{
    if (scope == 0 && id < P_COUNT) {
        *vp = &TSEL->p[id];
        return ed_tdesc(TSEL, id);
    }
    if (scope == 1 && id < G_COUNT) {
        *vp = &song.g[id];
        return &GP[id];
    }
    return 0;
}

/* ---- v6: backup / restore (web/EDITOR_PROTOCOL.md). Objects: 0 the working project, 1 the settings
 * (colours, calibration, the song order, the lights, SYNC), 2..5 the projects A..D, 6..7 the user preset
 * banks, 8 the FM6 patch bank (v9), 32..34 the user sample slots USR1..3 (read only here: restored with
 * SMP_BEGIN / WRITE / END).
 * LIST takes a snapshot of the working project and the settings; GET reads 1..256 bytes of an object.
 * PUT stages one object in RAM (begin: id, length, CRC-32; data; commit), checks it as a load would,
 * then writes it through the usual A/B commit: a cut-off restore never leaves half an object. */
#if FELUCCA_FLASH
#define ED_BK_RAW ((uint8_t *)&proj_tmp)                  /* the staging RAM (main loop, as the project loads) */
_Static_assert(sizeof proj_tmp >= sizeof(project_t) && sizeof proj_tmp >= sizeof(up_bank_t) &&
               sizeof proj_tmp >= sizeof(persist_t), "backup staging");
static persist_t ed_bk_set;                             /* LIST's snapshot of the settings */
static uint8_t ed_bk_ml[MIDI_PART];                     /* .. and of the MIDI part (2.6, object 10: project.c) */
static uint8_t ed_bk_valid, ed_bk_put, ed_bk_id;
static uint32_t ed_bk_len, ed_bk_crc, ed_bk_pos, ed_bk_ms;
UI_SMALL static void ed_bk_u32(uint32_t v) { uint32_t i; for (i = 0; i < 5u; i++) ed_b((v >> (7u * i)) & 127u); }
UI_SMALL static uint32_t ed_bk_r32(const uint8_t *a)
{
    return (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)a[4] << 28;
}

UI_SMALL static const uint8_t *ed_bk_obj(uint32_t id, uint32_t *len)   /* 0 = no such object; *len 0 = empty */
{
    *len = 0;
    if (id == 0u) {
        *len = sizeof(project_t);
        return ED_BK_RAW;
    }
    if (id == 1u) {
        *len = sizeof ed_bk_set;
        return (const uint8_t *)&ed_bk_set;
    }
    if (id >= 2u && id <= 5u) {
        if (project_used(id - 2u))
            *len = sizeof proj_slot[0];
        return (const uint8_t *)&proj_slot[id - 2u];
    }
    if (id == 6u || id == 7u) {                           /* a user preset bank, read where it is (upreset.c) */
#ifdef SLOOP_SMK
        if (up_bank[id - 6u].magic == UP_BANK_MAGIC) *len = sizeof up_bank[0];
        return (const uint8_t *)&up_bank[id - 6u];
#else
        const up_bank_t *bk = up_bank_xip[id - 6u];
        if (bk)
            *len = sizeof *bk;
        return bk ? (const uint8_t *)bk : (const uint8_t *)&up_rec_none;
#endif
    }
    if (id == 8u) {                                       /* the FM6 patch bank, in flash (XIP) */
        if (fm6_bank_cur >= 0)
            *len = sizeof(fm6_bank_t);
        return (const uint8_t *)fm6_bank_flash(fm6_bank_cur < 0 ? 0u : (uint32_t)fm6_bank_cur);
    }
    if (id == 9u) {                                       /* SLOOP 2.5: the SYN drum kits (drum_synth.c dsu) */
        (void)dsu_kit(0);
        *len = sizeof dsu;
        return (const uint8_t *)&dsu;
    }
    if (id == 10u) {                                      /* SLOOP 2.6: the MIDI part (the MIDI LEARN map, GLO > MIDI's
                                                         * channels): listed only while it is not the default (a backup
                                                         * without it stays one an older editor reads; its restore leaves
                                                         * them as they are) */
        if (!midi_part_default(ed_bk_ml))
            *len = sizeof ed_bk_ml;
        return ed_bk_ml;
    }
    if (id == 11u) {                                      /* SLOOP 2.7: your theme (gfx.c), listed only when there is one */
        if (theme_valid)
            *len = sizeof theme;
        return (const uint8_t *)&theme;
    }
    if (id >= 32u && id < 32u + SMP_USER_SLOTS) {
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(id - 32u);
        if (h->magic == SMP_USER_MAGIC && h->version == 1u && h->nz && h->nz <= 16u &&
            h->data_len <= SMP_USER_SIZE - SMP_USER_DATA)
            *len = SMP_USER_DATA + h->data_len;
        return smp_user_xip(id - 32u);
    }
    return 0;
}
static const uint8_t ED_BK_IDS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 32, 33, 34, 35};   /* 11: your theme (2.7) */   /* 35: USR4 (2.4), 9: SYN kits (2.5),
                                                                                      * 10: MIDI LEARN (2.6) */

/* a flash erase silences the audio for ~50 ms and stalls USB: only while stopped (as the panel) */
UI_SMALL static uint32_t ed_flash_busy(void) { return song.playing || transport_req; }

UI_SMALL static uint32_t ed_bk_commit(void)
{
    uint8_t *raw = ED_BK_RAW;
    uint32_t id = ed_bk_id, n = ed_bk_len;
    if (ed_bk_pos != n || st_crc32(raw, n) != ed_bk_crc)
        return 2;
    if (id == 1u)
        return settings_restore(raw, n);
    if (id <= 5u)
        return project_restore(id == 0u ? 4u : id - 2u, raw, n);
    if (id == 6u || id == 7u) {                           /* a user preset bank (n 0: empty) */
        const up_bank_t *b = (const up_bank_t *)raw;
        if (ed_flash_busy())
            return 3;
        if (n && (n != sizeof *b || b->magic != UP_BANK_MAGIC || b->rsize != sizeof(up_rec_t) || b->nslot != UP_PER_BANK))
            return 2;
        if (!flash_ok || st_save(OBJ_UPRESET0 + id - 6u, raw, n))
            return 4;
#ifdef SLOOP_SMK
        memset(&up_bank[id - 6u], 0, sizeof up_bank[0]);
        if (n) memcpy(&up_bank[id - 6u], raw, n);
        up_bank_check(id - 6u, (int)n);
#else
        up_bank_find(id - 6u);
#endif                            /* (the bank is read where it is now) */
        up_gen++;
        return 0;
    }
    if (id == 8u) {                                       /* the FM6 patch bank (n 0: empty) */
        if (ed_flash_busy())
            return 3;
        if (n && (n != sizeof(fm6_bank_t) || !fm6_bank_valid((const fm6_bank_t *)raw)))
            return 2;
        if (!flash_ok || st_save(OBJ_FM6BANK, raw, n))
            return 4;
        fm6_bank_scan();
        fm6_bank_changed();
        return 0;
    }
    if (id == 9u) {                                       /* SLOOP 2.5: the SYN drum kits, written with the settings */
        persist_t p;
        uint32_t k;
        if (ed_flash_busy())
            return 3;
        if (n != sizeof(dsu_bank_t) || !dsu_valid((const dsu_bank_t *)raw))
            return 2;
        memcpy(&dsu, raw, sizeof dsu);
        for (k = 0; k < DSU_N; k++)
            dsu_fix_kit(&dsu.k[k]);
        persist_fill(&p);
        if (!flash_ok || settings_write(&p))
            return 4;
        persist_saved = p;
        return 0;
    }
    if (id == 10u) {                                      /* SLOOP 2.6: the MIDI part, written with the settings */
        persist_t p;
        if (ed_flash_busy())
            return 3;
        if (n != MIDI_PART && n != 2u * ML_N && n != 2u * ML_N + 4u)
            return 2;
        midi_part_read(raw, n);                           /* (every entry and channel checked) */
        persist_fill(&p);
        if (!flash_ok || settings_write(&p))
            return 4;
        persist_saved = p;
        return 0;
    }
    if (id == 11u) {                                      /* SLOOP 2.7: your theme (n 0: none), written with the settings;
                                                         * sent, it is shown (MENU > COLOR on it) */
        persist_t p;
        if (ed_flash_busy())
            return 3;
        theme_t t;
        if (n && n != sizeof(theme_t) && n != 36u)        /* (36: a first-beta THM1, taken with an accent) */
            return 2;
        memset(&t, 0, sizeof t);
        memcpy(&t, raw, n);
        if (n && !theme_check(&t))
            return 2;
        if (n) {
            theme = t;
            theme_valid = 1;
            theme_on = 1;
        } else {
            memset(&theme, 0, sizeof theme);
            theme_valid = 0;
            theme_on = 0;
        }
        palette_set(settings.palette);
        persist_fill(&p);
        if (!flash_ok || settings_write(&p))
            return 4;
        persist_saved = p;
        return 0;
    }
    return 1;
}

UI_SMALL static int ed_backup(uint32_t cmd, const uint8_t *a, uint32_t na)   /* 1: a backup command (reply built) */
{
    uint32_t i, len, rc;
    const uint8_t *p;
    if (cmd == ED_BK_LIST) {
        rc = !flash_ok ? 4u : 0u;
        if (!rc) {
            proj_capture((project_t *)ED_BK_RAW);           /* the working project, as it is now */
            persist_fill(&ed_bk_set);
            midi_part_fill(ed_bk_ml);
            ed_bk_valid = 1;
            ed_bk_put = 0;
        }
        ed_b(rc);
        ed_b(rc ? 0u : (uint32_t)sizeof ED_BK_IDS - (ed_bk_obj(10u, &len) && !len ? 1u : 0u) -
                           (ed_bk_obj(11u, &len) && !len ? 1u : 0u));
        for (i = 0; !rc && i < sizeof ED_BK_IDS; i++) {
            p = ed_bk_obj(ED_BK_IDS[i], &len);
            if ((ED_BK_IDS[i] == 10u || ED_BK_IDS[i] == 11u) && !len)
                continue;                                 /* (nothing learned / no theme: not in the list) */
            ed_b(ED_BK_IDS[i]);
            ed_bk_u32(len);
            ed_bk_u32(st_crc32(p, len));
            fm1_wdt_feed();                               /* (a sample slot: up to 80 KiB through the CRC) */
        }
        return 1;
    }
    if (cmd == ED_BK_GET) {                               /* id, off (5), count (2) -> id, rc, off, count, data */
        uint32_t off = na >= 6u ? ed_bk_r32(a + 1) : 0u, count = na >= 8u ? (uint32_t)a[6] | (uint32_t)a[7] << 7 : 0u;
        p = na >= 1u ? ed_bk_obj(a[0], &len) : 0;
        rc = na != 8u || !p || !count || count > 256u || off > len || count > len - off ? 1u
             : !ed_bk_valid || (a[0] == 0u && ed_bk_put) ? 5u : 0u;   /* 5: LIST first (the snapshot is gone) */
        ed_b(na ? a[0] : 127u);
        ed_b(rc);
        ed_bk_u32(off);
        ed_b(rc ? 0u : count & 127u);
        ed_b(rc ? 0u : count >> 7);
        if (!rc)
            ed_pack7(p + off, count);
        return 1;
    }
    if (cmd == ED_BK_PUT) {                               /* op, id, ... -> op, id, rc */
        uint32_t op = na >= 2u ? a[0] : 99u, id = na >= 2u ? a[1] : 127u;
        rc = 1;
        if (!flash_ok) {
            rc = 4;
        } else if (op == 0u && na == 12u && (id <= 11u)) {  /* begin: id, length (5), CRC-32 (5) */
            len = ed_bk_r32(a + 2);
            if (id >= 2u || len) {                        /* (the working project and the settings are never empty) */
                if (len <= sizeof proj_tmp) {
                    ed_bk_put = 1;
                    ed_bk_valid = 0;                      /* (the staging RAM is the snapshot's) */
                    ed_bk_id = (uint8_t)id;
                    ed_bk_len = len;
                    ed_bk_crc = ed_bk_r32(a + 7);
                    ed_bk_pos = 0;
                    ed_bk_ms = fm1_ms;
                    rc = 0;
                }
            }
        } else if (!ed_bk_put || ed_bk_id != id || fm1_ms - ed_bk_ms > 15000u) {
            rc = 5;                                       /* no begin for this object (or too long ago) */
        } else if (op == 1u && na >= 9u && ed_bk_r32(a + 2) == ed_bk_pos) {   /* data: off (5), pack7 */
            uint32_t k = ed_unpack7(a + 7, na - 7u, ed_smp_buf, 256u);
            if (k && k <= ed_bk_len - ed_bk_pos) {
                memcpy(ED_BK_RAW + ed_bk_pos, ed_smp_buf, k);
                ed_bk_pos += k;
                ed_bk_ms = fm1_ms;
                rc = 0;
            }
        } else if (op == 2u && na == 2u) {                /* commit */
            rc = ed_bk_commit();
            ed_bk_put = 0;
            if (!rc) {
                sync_reload = 1;
                ui.force = 1;
            }
        } else if (op == 3u && na == 2u) {                /* abort */
            ed_bk_put = 0;
            rc = 0;
        }
        ed_b(op & 127u);
        ed_b(id & 127u);
        ed_b(rc);
        return 1;
    }
    return 0;
}
#else
UI_SMALL static int ed_backup(uint32_t cmd, const uint8_t *a, uint32_t na)   /* no flash: nothing to back up */
{
    (void)a;
    (void)na;
    if (cmd < ED_BK_LIST || cmd > ED_BK_PUT)
        return 0;
    ed_b(4);
    return 1;
}
#endif

#include "editor_fm6.c"                               /* v9: the FM6 patches (68..71) */
#include "editor_dsyn.c"                              /* v10: the SYN drum kits (72..76) */

#ifdef SLOOP_ED_SAMPLES
#include "../../../firmware/src/sloop_smk_editor_samples.c"
#endif
#include "editor_live.c"                              /* v12: the LIVE page (77) */

UI_SMALL static void ed_handle(const uint8_t *f, uint32_t n)   /* f: the bytes between F0 and F7 */
{
    uint32_t cmd = f[3], i;
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u;
    int16_t *vp;
    const param_desc_t *d;
    ed_begin(cmd);
#ifdef SLOOP_ED_SAMPLES
    if(sloop_ed_samples(cmd,a,na)){ed_send();return;}
#endif
    if (ed_backup(cmd, a, na)) {                           /* v6: backup / restore */
        ed_send();
        return;
    }
    if (ed_fm6_handle(cmd, a, na)) {                       /* v9: FM6 patches */
        ed_send();
        return;
    }
    if (ed_dsyn_handle(cmd, a, na)) {                      /* v10: the SYN drum kits */
        ed_send();
        return;
    }
    switch (cmd) {
    case ED_LIVE:                                          /* v12: the LIVE page */
        if (na < 1u)
            return;
        ed_live(a, na);
        break;
    case ED_INFO:
        ed_str(FELUCCA_VERSION, 24);
        ed_b(NENGINES);
        ed_b(P_COUNT);
        ed_b(G_COUNT);
        ed_b(NSTEP);
        ed_b(P_E0);
        for (i = 0; i < NENGINES; i++)
            ed_str(ENGINES[i]->name, 8);
        ed_b(NTRK);                                       /* v3 */
        ed_b(ED_PROTO);                                   /* v5..: the protocol version (9: FM6 patches, 10: SYN kits) */
        break;
    case ED_GET:
    case ED_SET:
        if (na < 2u || !(d = ed_desc(a[0], a[1], &vp)))
            return;
        if (cmd == ED_SET && na >= 4u) {
            if (a[0] == 1 && a[1] == G_ENGSEL) {          /* engine change: the safe path */
                set_engine((uint32_t)clamp(ed_rv(a + 2), 0, NENGINES - 1));
            } else if (d->max > d->min) {
                *vp = (int16_t)clamp(ed_rv(a + 2), d->min, d->max);
            }
            ui.force = 1;
            ed_w.v[a[0] ? P_COUNT + a[1] : a[1]] = *vp;   /* the editor's own change: no push */
        }
        ed_b(a[0]);
        ed_b(a[1]);
        ed_v(*vp);
        break;
    case ED_DUMP:
        for (i = 0; i < ED_NV; i++)                       /* the editor gets them all here */
            ed_w.v[i] = *ed_val(i);
        ed_b(ed_eng(TSEL));
        ed_b(TSEL->preset);
        for (i = 0; i < P_COUNT; i++)
            ed_v(TSEL->p[i]);
        for (i = 0; i < G_COUNT; i++)
            ed_v(song.g[i]);
        break;
    case ED_DESC:
        if (na < 2u || !(d = ed_desc(a[0], a[1], &vp)))
            return;
        ed_b(a[0]);
        ed_b(a[1]);
        ed_b(d->fmt);
        ed_v(d->min);
        ed_v(d->max);
        ed_v(d->def);
        ed_str(d->label, 8);
        ed_str(d->unit, 8);
        if (d->fmt == F_ENUM && d->names)
            for (i = 0; i <= (uint32_t)(d->max - d->min) && i < 64u; i++)   /* (the 34 drum kits) */
                ed_str(d->names[i], 8);
        break;
    case ED_STEP_GET:
    case ED_STEP_SET:
        if (na < 1u || a[0] >= NSTEP)
            return;
        if (cmd == ED_STEP_SET && na >= 9u) {
            fm1_irq_off();
            ed_step_in(TSEL, a[0], a + 1, na - 1u);
            fm1_irq_on();
            ui.force = 1;
        }
        ed_w.st[a[0]] = ed_step_sig(TSEL, a[0]);
        ed_b(a[0]);
        ed_step_out(TSEL, a[0]);
        break;
    case ED_PRESET:                                        /* engine, preset */
        if (na < 2u || a[0] >= NENGINES)
            return;
        if (a[0] != TSEL->eng_req)
            set_engine(a[0]);
        apply_preset(a[1]);
        ui.force = 1;
        ed_b(ed_eng(TSEL));
        ed_b(TSEL->preset);
        break;
    case ED_PROJECT: {                                     /* 0 = load, 1 = save, 2 = query; slot 0..3 */
        uint32_t project_rc = 0;
        if (na < 2u || a[0] > 2u)
            return;
#if FELUCCA_ARRANGER
        if (a[0] < 2u && (song.playing || transport_req)) {
            ed_b(a[0]); ed_b(a[1] & 3u); ed_b(project_used(a[1] & 3u));
            ed_b(1);                                  /* optional status: transport busy */
            break;
        }
#endif
        if (a[0] == 1u)
            project_rc = project_save(a[1] & 3u);
        else if (a[0] == 0u)
            project_load(a[1] & 3u);
#ifdef SLOOP_PROJECT_WRITABLE
        if (a[0] == 2u)
            project_rc = !flash_ok ? 2u : !SLOOP_PROJECT_WRITABLE(a[1]) ? 4u : 0u;
#endif
        ed_b(a[0]);
        ed_b(a[1] & 3u);
        ed_b(project_used(a[1] & 3u));
#if defined(SLOOP_SMK) && FELUCCA_FLASH
        /* Existing optional status tail: old editors ignore it. No new command ID. */
        ed_b(project_rc);
#endif
        break;
    }
    case ED_NAMES:                                         /* preset names of an engine */
        if (na < 1u || a[0] >= NENGINES)
            return;
        ed_b(a[0]);
        ed_b(ENGINES[a[0]]->npresets);
        for (i = 0; i < ENGINES[a[0]]->npresets; i++)
            ed_str(ENGINES[a[0]]->presets[i].name, 12);
        for (i = 0; i < 2u; i++)                           /* then the two edit-page titles */
            ed_str(ENGINES[a[0]]->page_title[i], 8);
        break;
    case ED_SMP_BEGIN:                                     /* slot -> slot, rc */
    case ED_SMP_ERASE: {
        uint32_t rc;
        if (na < 1u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        rc = ed_smp_erase(a[0], cmd == ED_SMP_ERASE) ? 1u : 0u;
        ed_smp_open[a[0]] = (uint8_t)(cmd == ED_SMP_BEGIN && !rc);
        ed_b(a[0]);
        ed_b(rc);
        break;
    }
    case ED_SMP_WRITE: {                                   /* slot, off (3 x 7 bit), pack7 data -> slot, off, rc */
        uint32_t off, len, rc = 0, took;
        if (na < 5u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        off = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        len = ed_unpack7(a + 4, na - 4u, ed_smp_buf, 256u);
        if (off < SMP_USER_DATA || (off & 0xFFu) || !len || off + len > SMP_USER_SIZE)
            rc = 1;
        else if (usr_nz[a[0]] || !ed_smp_open[a[0]])
            rc = 4;                                        /* slot in use: SMP_BEGIN first (voices read it) */
        else {
            if (!(off & 0xFFFu))                           /* first write into a sector: erase it */
                rc = fl_erase4k_quiet(ed_smp_slot(a[0]) + off, &took) ? 2u : 0u;
            if (!rc && fl_write(ed_smp_slot(a[0]) + off, ed_smp_buf, len))
                rc = 3;
        }
        ed_b(a[0]);
        ed_b(off);
        ed_b(off >> 7);
        ed_b(off >> 14);
        ed_b(rc);
        break;
    }
    case ED_SMP_END:                                       /* slot, pack7 header -> slot, rc */
        if (na < 2u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        ed_b(a[0]);
        ed_b((uint32_t)ed_smp_end(a[0], a + 1, na - 1u));
        break;
    case ED_SMP_INFO:                                      /* -> per slot: zones (0 = empty), name, data KiB */
        #ifdef SLOOP_SMK_HARDWARE
        /* This first port has no physical sample write allocation. */
        ed_b(0);ed_b(0);break;
#endif
        ed_b(SMP_USER_SLOTS);
        ed_b(SMP_USER_SIZE / 1024u);
        for (i = 0; i < SMP_USER_SLOTS; i++) {
            const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(i);
            char nm[9] = {0};
            uint32_t j;
            ed_b(usr_nz[i]);
            for (j = 0; usr_nz[i] && j < 8u; j++)
                nm[j] = h->name[j] >= 32 && h->name[j] < 127 ? h->name[j] : 0;
            ed_str(nm, 8);
            ed_b(usr_nz[i] ? (h->data_len + 1023u) / 1024u : 0u);
        }
        break;
    case ED_UP_LIST: {                                     /* start, count -> start, count, total, per slot: used, engine, name */
        uint32_t s0, cnt;
        if (na < 2u)
            return;
        s0 = a[0];
        cnt = a[1] > 16u ? 16u : a[1];
        if (s0 >= UP_SLOTS)
            cnt = 0;
        else if (s0 + cnt > UP_SLOTS)
            cnt = UP_SLOTS - s0;
        ed_b(s0);
        ed_b(cnt);
        ed_b(UP_SLOTS);
        for (i = s0; i < s0 + cnt; i++) {
            char nm[13] = {0};
            uint32_t j, u = (uint32_t)up_used(i);
            for (j = 0; u && j < 12u; j++)
                nm[j] = up_rec(i)->name[j];
            ed_b(u);
            ed_b(u ? up_rec(i)->engine : 0u);
            ed_str(nm, 12);
        }
        break;
    }
    case ED_UP_GET: {                                      /* slot -> slot, used, engine, name, P_COUNT x v14, 16 x (note, flags) */
        int16_t v[P_COUNT];
        char nm[13] = {0};
        const up_rec_t *r;
        uint32_t u;
        if (na < 1u || a[0] >= UP_SLOTS)
            return;
        r = up_rec(a[0]);
        u = (uint32_t)up_used(a[0]);
        if (u)
            up_values(r, v);                               /* today's P_* order, clamped */
        for (i = 0; u && i < 12u; i++)
            nm[i] = r->name[i];
        ed_b(a[0]);
        ed_b(u);
        ed_b(u ? r->engine : 0u);
        ed_str(nm, 12);
        for (i = 0; i < P_COUNT; i++)
            ed_v(u ? v[i] : 0);
        for (i = 0; i < 16u; i++) {
            ed_b(u ? r->note[i] : 0u);
            ed_b(u ? r->flags[i] : 0u);
        }
        break;
    }
    case ED_UP_PUT: {                                      /* slot, engine, name, values, pattern -> slot, rc */
        static up_rec_t r;
        uint32_t slot = 0, rc;
        if (na < 1u)
            return;
        rc = (uint32_t)up_parse(a, na, &r, &slot);
        if (!rc) {
            int16_t v[P_COUNT];
            up_values(&r, v);                              /* each value inside its range */
            for (i = 0; i < P_COUNT; i++)
                r.p[i] = v[i];
            rc = ed_flash_busy() ? 3u : up_put(slot, &r) ? 2u : 0u;
        }
        ed_b(a[0]);
        ed_b(rc);
        break;
    }
    case ED_UP_STORE: {                                    /* slot, name -> slot, rc */
        char nm[13] = {0};
        uint32_t n0, rc = 1;
        if (na < 1u)
            return;
        for (n0 = 0; 1u + n0 < na && a[1 + n0] && n0 < 13u; n0++)
            ;
        if (a[0] < UP_SLOTS && (!n0 || up_name_ok(a + 1, n0))) {   /* "" = automatic name */
            for (i = 0; i < n0; i++)
                nm[i] = (char)a[1 + i];
            int r = ed_flash_busy() ? -3 : up_store(a[0], nm);
            rc = r == -3 ? 3u : r == 1 ? 1u : r ? 2u : 0u;   /* 1: the drum track is selected; 3: stop first */
        }
        ed_b(a[0]);
        ed_b(rc);
        break;
    }
    case ED_UP_LOAD:                                       /* slot -> slot, rc */
        if (na < 1u)
            return;
        ed_b(a[0]);
        ed_b(a[0] < UP_SLOTS && !up_load(a[0]) ? 0u : 1u);
        break;
    case ED_UP_ERASE:
        if (na < 1u)
            return;
        ed_b(a[0]);
        ed_b(a[0] >= UP_SLOTS ? 1u : ed_flash_busy() ? 3u : up_put(a[0], 0) ? 2u : 0u);
        break;
    case ED_WATCH:                                         /* on -> on */
        if (na < 1u)
            return;
        ed_w.on = a[0] & 1u;
        ed_w.v4 = (uint8_t)(ed_w.on && (a[0] & 2u));     /* v4: also TRACK_CHANGED; the reply says it is known */
        ed_w.resets = usb.resets;
        if (ed_w.on)
            ed_shadow();
        ed_b(ed_w.on | ed_w.v4 << 1);
        break;
    case ED_PING:
        ed_b(0);
        break;
    case ED_TRACK:                                         /* [track] -> selected, NTRK, per track: engine, preset, level, mute, armed */
        if (na >= 1u && a[0] < NTRK && a[0] != song.sel) {
            track_select(a[0]);
            if (ed_w.on)
                ed_shadow();                               /* the editor re-reads it: no RELOAD for this */
        }
        ed_b(song.sel);
        ed_b(NTRK);
        for (i = 0; i < NTRK; i++) {
            ed_b(ed_eng(&trk[i]));
            ed_b(trk[i].preset);
            ed_v(i == TRK_DRUM ? song.g[G_DRLVL] : trk[i].p[P_LEVEL]);
            ed_b(trk[i].p[P_MUTE] != 0);
            ed_b((song.rec >> i) & 1u);
        }
        ed_b(song.solo & 15u);                            /* v5: the tracks soloed */
        break;
    case ED_TRACK_MIX: {                                   /* track [, level v14, mute] -> track, level, mute */
        track_t *t;
        int16_t *lv;
        if (na < 1u || a[0] >= NTRK)
            return;
        t = &trk[a[0]];
        lv = a[0] == TRK_DRUM ? &song.g[G_DRLVL] : &t->p[P_LEVEL];
        if (na >= 4u) {
            *lv = (int16_t)clamp(ed_rv(a + 1), 0, 127);
            t->p[P_MUTE] = (int16_t)(a[3] ? 1 : 0);
            if (a[0] == TRK_DRUM)                          /* the editor's own change: no push */
                ed_w.v[P_COUNT + G_DRLVL] = *lv;
            else
                ed_known(a[0], P_LEVEL);
            ed_known(a[0], P_MUTE);
            ui.force = 1;
        }
        ed_b(a[0]);
        ed_v(*lv);
        ed_b(t->p[P_MUTE] != 0);
        break;
    }
    case ED_TRACK_DUMP:                                    /* track -> track, engine, preset, P_COUNT x v14 */
        if (na < 1u || a[0] >= NTRK)
            return;
        ed_b(a[0]);
        ed_b(ed_eng(&trk[a[0]]));
        ed_b(trk[a[0]].preset);
        for (i = 0; i < P_COUNT; i++)
            ed_v(trk[a[0]].p[i]);
        break;
    case ED_TRACK_STEP:                                    /* track, index [, step] -> track, index, step (as STEP_GET) */
        if (na < 2u || a[0] >= NTRK || a[1] >= NSTEP)
            return;
        if (na >= 10u) {
            fm1_irq_off();
            ed_step_in(&trk[a[0]], a[1], a + 2, na - 2u);
            fm1_irq_on();
            ui.force = 1;
        }
        if (a[0] == song.sel)
            ed_w.st[a[1]] = ed_step_sig(&trk[a[0]], a[1]);
        ed_b(a[0]);
        ed_b(a[1]);
        ed_step_out(&trk[a[0]], a[1]);
        break;
    case ED_DRUM_STEP: {                                   /* index [, on 16 bits, lvl 32 bits, rat 32 bits as 7-bit groups]
                                                            * -> index, on (3), lvl (5), rat (5): the drum track's 16 lanes */
        dstep_t *d;
        uint32_t on, lv, rt;
        if (na < 1u || a[0] >= NSTEP)
            return;
        d = &TDRUM->dstep[a[0]];
        if (na >= 14u) {
            on = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)(a[3] & 3u) << 14;
            lv = (uint32_t)a[4] | (uint32_t)a[5] << 7 | (uint32_t)a[6] << 14 | (uint32_t)a[7] << 21 | (uint32_t)(a[8] & 15u) << 28;
            rt = (uint32_t)a[9] | (uint32_t)a[10] << 7 | (uint32_t)a[11] << 14 | (uint32_t)a[12] << 21 | (uint32_t)(a[13] & 15u) << 28;
            fm1_irq_off();
            memset(d, 0, sizeof *d);
            for (i = 0; i < DRUM_LANES; i++)
                if ((on >> i) & 1u)
                    dstep_set(d, i, (lv >> (2u * i)) & 3u, (rt >> (2u * i)) & 3u);
            fm1_irq_on();
            ui.force = 1;
            if (song.sel == TRK_DRUM)
                ed_w.st[a[0]] = ed_step_sig(TDRUM, a[0]);
        }
        on = dstep_mask(d);
        lv = (uint32_t)d->lvl[0] | (uint32_t)d->lvl[1] << 8 | (uint32_t)d->lvl[2] << 16 | (uint32_t)d->lvl[3] << 24;
        rt = (uint32_t)d->rat[0] | (uint32_t)d->rat[1] << 8 | (uint32_t)d->rat[2] << 16 | (uint32_t)d->rat[3] << 24;
        ed_b(a[0]);
        ed_b(on);
        ed_b(on >> 7);
        ed_b(on >> 14);
        for (i = 0; i < 5u; i++)
            ed_b(lv >> (7u * i));
        for (i = 0; i < 5u; i++)
            ed_b(rt >> (7u * i));
        break;
    }
    case ED_TRACK_PARAM: {                                 /* track, id [, v14] -> track, id, v14 */
        track_t *t;
        if (na < 2u || a[0] >= NTRK || a[1] >= P_COUNT)
            return;
        t = &trk[a[0]];
        d = ed_tdesc(t, a[1]);
        if (na >= 4u) {
            if (d->max > d->min)                           /* as SET: clamped; a fixed value stays */
                t->p[a[1]] = (int16_t)clamp(ed_rv(a + 2), d->min, d->max);
            ed_known(a[0], a[1]);
            ui.force = 1;
        }
        ed_b(a[0]);
        ed_b(a[1]);
        ed_v(t->p[a[1]]);
        break;
    }
    /* v7 (SLOOP 2.4): the parameter locks and nudges of a track (core.h plock_t / micro, seq.c lock_step) */
    case ED_LOCK_GET: {                                    /* track -> track, n, n x (step, param, v14) */
        const track_t *t;
        uint32_t n = 0;
        if (na < 1u || a[0] >= NTRK)
            return;
        t = &trk[a[0]];
        for (i = 0; i < NLOCK; i++)
            n += t->lock[i].step < NSTEP;
        ed_b(a[0]);
        ed_b(n);
        for (i = 0; i < NLOCK; i++)
            if (t->lock[i].step < NSTEP) {
                ed_b(t->lock[i].step);
                ed_b(t->lock[i].param);
                ed_v(t->lock[i].val);
            }
        break;
    }
    case ED_LOCK_SET: {                                    /* track, step, param [, v14] (no value: delete)
                                                            * -> track, step, param, rc, has, v14 */
        track_t *t;
        uint32_t rc = 0, has;
        int k;
        if (na < 3u || a[0] >= NTRK)
            return;
        t = &trk[a[0]];
        if (a[1] >= NSTEP || a[2] >= P_COUNT)
            rc = 1;
        else if (!p_lockable(a[2]))
            rc = 2;
        else {
            fm1_irq_off();
            if (na >= 5u)
                rc = lock_set(t, a[1], a[2], ed_rv(a + 3)) ? 0u : 3u;   /* 3: no free slot */
            else
                lock_del(t, a[1], a[2]);
            fm1_irq_on();
            ui.force = 1;
            if (a[0] == song.sel)
                ed_w.st[a[1]] = ed_step_sig(t, a[1]);     /* the editor's own change: no push */
        }
        k = rc == 1u ? -1 : lock_find(t, a[1], a[2], 0);
        has = k >= 0;
        ed_b(a[0]);
        ed_b(a[1]);
        ed_b(a[2]);
        ed_b(rc);
        ed_b(has);
        ed_v(has ? t->lock[k].val : 0);
        break;
    }
    case ED_MICRO_GET:                                     /* track -> track, NSTEP x (nudge + 64) */
        if (na < 1u || a[0] >= NTRK)
            return;
        ed_b(a[0]);
        for (i = 0; i < NSTEP; i++)
            ed_b((uint32_t)(trk[a[0]].micro[i] + 64));
        break;
    case ED_MICRO_SET: {                                   /* track, step, nudge + 64 -> track, step, nudge + 64 (clamped) */
        track_t *t;
        if (na < 3u || a[0] >= NTRK || a[1] >= NSTEP)
            return;
        t = &trk[a[0]];
        fm1_irq_off();
        t->micro[a[1]] = (int8_t)clamp((int32_t)a[2] - 64, MICRO_MIN, MICRO_MAX);
        fm1_irq_on();
        ui.force = 1;
        if (a[0] == song.sel)
            ed_w.st[a[1]] = ed_step_sig(t, a[1]);
        ed_b(a[0]);
        ed_b(a[1]);
        ed_b((uint32_t)(t->micro[a[1]] + 64));
        break;
    }
    /* v8 (SLOOP 2.4): the steps' fill conditions (core.h FC_*, seq.c step_fill): 2 bits a step, as stored */
    case ED_FILL_GET:                                      /* track -> track, pack7 of the NSTEP / 4 bytes */
        if (na < 1u || a[0] >= NTRK)
            return;
        ed_b(a[0]);
        ed_pack7(trk[a[0]].fill, NSTEP / 4u);
        break;
    case ED_FILL_SET: {                                    /* track, step, cond -> track, step, cond (3 -> 0) */
        track_t *t;
        if (na < 3u || a[0] >= NTRK || a[1] >= NSTEP)
            return;
        t = &trk[a[0]];
        fm1_irq_off();
        step_fill_set(t, a[1], a[2] % 3u);
        fm1_irq_on();
        ui.force = 1;
        if (a[0] == song.sel)
            ed_w.st[a[1]] = ed_step_sig(t, a[1]);
        ed_b(a[0]);
        ed_b(a[1]);
        ed_b(step_fill(t, a[1]));
        break;
    }
    default:
        return;
    }
    ed_send();
}

/* main loop: editor frames first; anything else stays for ota_service() */
UI_SMALL static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n;
    ed_sync();                                             /* v2 pushes (while watched) */
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_HDR0 || p[1] != ED_HDR1 || p[2] != ED_HDR2)
        return;
    ed_w.last_ms = fm1_ms;                                 /* any request keeps WATCH alive */
    if (p[3] >= ED_SMP_BEGIN) {                            /* large frames: handled in place, then freed */
        ed_handle(p, n);
        ota_frame_done();
        return;
    }
    {
        static uint8_t f[64];
        uint32_t k = n > sizeof f ? sizeof f : n, i;
        for (i = 0; i < k; i++)
            f[i] = p[i];
        ota_frame_done();                                  /* free the frame buffer before replying */
        ed_handle(f, k);
    }
}
