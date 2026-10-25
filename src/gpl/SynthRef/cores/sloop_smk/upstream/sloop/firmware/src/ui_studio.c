/* SPDX-License-Identifier: GPL-3.0-only */
/* LIVE screens, 240 x 240, in a teenage-engineering-like style: black, four colours (one per
 * track and per knob: blue, green, yellow, orange), white for what you touch, red for recording,
 * big numbers, lowercase labels, four dials at the bottom that show what KNOB 1..4 do.
 * Screens: TRACKS (the performance view), DRUMS (GRID / KIT, pads that flash on each hit), the
 * LAYERS (a function button held: what the 16 white keys and the knobs do now), HOLD (a hold to
 * confirm: clear, save), REC (armed / free take). Every band is drawn into the canvas only when its
 * signature changed. */
static uint8_t drum_page, drum_cursor;   /* drum_page: 0 GRID, 1 KIT, 2 SOUND (2.7: a sound's TUNE, DECAY) */
#define drum_lane pen_lane                 /* (2.7.1) one drum sound picked everywhere: the GRID, SOUND, the SEQ layer, a pad
                                            * played (seq.c pen_lane) */
/* (2.7.1) the black keys pick the drum sound on the GRID page and in the SEQ layer (the pages: OCT- / OCT+), never heard;
 * the same key again: the other sound of its pair. F#3 .. F#5: KICK / KICK 2, SNARE / SNARE 2, RIM, CLAP, HAT / PEDAL,
 * OPEN HAT, LOW TOM / HI TOM, CRASH, RIDE, SHAKER / CONGA, COWBELL */
static const uint8_t DRUM_BLACK[11][2] = {{0, 1}, {2, 8}, {7, 7}, {3, 3}, {4, 6}, {5, 5}, {9, 10}, {11, 11}, {12, 12},
                                          {13, 14}, {15, 15}};
UI_SMALL static int32_t drum_black_of(uint32_t k)       /* key k: its black key 0..10, -1 a white key */
{
    uint32_t i;
    for (i = 0; i < 11u; i++)
        if (PUNCH_BLACK[i] == k)
            return (int32_t)i;
    return -1;
}
UI_SMALL static uint32_t drum_black_key(uint32_t lane)    /* the key of the black key a sound is on */
{
    uint32_t i;
    for (i = 0; i < 11u; i++)
        if (DRUM_BLACK[i][0] == lane || DRUM_BLACK[i][1] == lane)
            return PUNCH_BLACK[i];
    return PUNCH_BLACK[0];
}
UI_SMALL static void drum_black_pick(uint32_t b)          /* black key b: its sound, or the other of its pair */
{
    const uint8_t *g = DRUM_BLACK[b % 11u];
    pen_lane = pen_lane == g[0] && g[1] != g[0] ? g[1] : g[0];
    ui_say("SOUND ", LANE_NAME[pen_lane]);
    ui.force = 1;
}
UI_SMALL static void drum_hear(uint32_t lane) { if (drum_preview) audition_lane(lane); }   /* (2.7.1) menu DRUM PREVIEW */
static void trk_short_name(uint32_t c, char *b);
UI_SMALL static int on_drum_page(void) { return !ui.home && cur_page()->scope == SC_DRUM; }

/* ---------------------------------------------------------------- style --- */
/* TE_G1 .. TE_G4: the greys (tiles, empty steps, labels, secondary text), gfx.c (2.7: on a theme's background) */
#define TE_RED RGB(255, 44, 52)          /* recording, erasing */
/* TE_COL, TE_MID, TE_DIM: the tracks' colours, gfx.c (2.7: a theme may set them) */
#define TE_DRUM TE_COL[3]

UI_SMALL static void te_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)     /* filled circle */
{
    int32_t y, x;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
}
/* a dial: a 270-degree ring (lit up to the value), a pointer; ratio 0..1000, -1 = a plain ring */
UI_SMALL static void te_dial(int32_t cx, int32_t cy, int32_t r, int32_t ratio, uint16_t c, uint16_t dim)
{
    int32_t i, end = ratio < 0 ? 768 : ratio * 768 / 1000;
    for (i = 0; i <= 768; i += 8) {
        uint32_t a = (uint32_t)(384 + i) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a], k;
        uint16_t col = ratio < 0 || i <= end ? c : dim;
        for (k = r - 2; k <= r; k++)
            cv_pset(cx + ((co * k) >> 15), cy + ((si * k) >> 15), col);
    }
    if (ratio >= 0) {
        uint32_t a = (uint32_t)(384 + end) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a];
        cv_line(cx, cy, cx + ((co * (r - 4)) >> 15), cy + ((si * (r - 4)) >> 15), C_WHITE);
        te_disc(cx, cy, 2, C_WHITE);
    }
}
UI_SMALL static void te_play_icon(int32_t x, int32_t y, int playing)
{
    int32_t i;
    if (playing)
        for (i = 0; i < 12; i++)
            cv_rect(x + i, y + i / 2, 1, 14 - i, C_WHITE);   /* a triangle */
    else
        cv_rect(x, y + 1, 12, 12, TE_G3);
}
UI_SMALL static uint32_t studio_hash(uint32_t h, const char *p)
{ while (*p) h = h * 31u + (uint8_t)*p++; return h; }
UI_SMALL static void te_lower(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
    d[i] = 0;
}
UI_SMALL static void te_text_c(int32_t cx, int32_t y, const char *s, uint16_t c)   /* centred on cx */
{
    cv_text(cx - text_w(&FONT_S, s) / 2, y, &FONT_S, s, c);
}

/* the dial strip: KNOB 1..4, label + value under each (a dial with no label: an empty column); a
 * message replaces it */
UI_SMALL static void te_dials(int32_t y0, const char *const lab[4], const char *const val[4], const int32_t ratio[4],
                     uint32_t sig, uint32_t *cache)
{
    uint32_t k;
    sig = studio_hash(sig, ui.msg_t ? ui.msg : "");
    for (k = 0; k < 4u; k++) {
        sig = studio_hash(sig * 7u + (uint32_t)ratio[k] + (ui.hot_t && ui.hot_col == k) * 5003u, lab[k]);
        sig = studio_hash(sig, val[k]);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, ui.msg_t ? 240u - (uint32_t)y0 : 40u, C_BLACK);
    if (ui.msg_t) {                                     /* a message: a white bar */
        cv_rect(0, 10, 240, 32, C_WHITE);
        cv_text((240 - text_w(&FONT_S, ui.msg)) / 2, 18, &FONT_S, ui.msg, C_BLACK);
        cv_blit(0, (uint32_t)y0);
        return;
    }
    for (k = 0; k < 4u; k++) {
        int32_t cx = 30 + 60 * (int32_t)k;
        if (!lab[k][0])
            continue;
        te_dial(cx, 12, 11, ratio[k], TE_COL[k], TE_DIM[k]);
        te_text_c(cx, 24, lab[k], TE_G3);
    }
    cv_blit(0, (uint32_t)y0);
    cv_begin(240, 16, C_BLACK);                         /* the values: white while turned */
    for (k = 0; k < 4u; k++)
        te_text_c(30 + 60 * (int32_t)k, 0, val[k], ui.hot_t && ui.hot_col == k ? C_WHITE : TE_G4);
    cv_blit(0, (uint32_t)y0 + 40u);
}

UI_SMALL static void studio_open(uint32_t scope)
{
    uint32_t i;
    if (scope == SC_SONG && rec_wait)
        rec_wait = 0;                                   /* (an arm does not follow into the song page) */
    for (i = 0; i < NPAGES; i++) if (PAGES[i].scope == scope) {
        ui.page = (uint8_t)i; ui.home = 0; song.seq_mode = 0; ui.force = 1;
        ui.msg_t = 0; ui.entry_open = 0; return;
    }
}
/* LIVE: REC, on any page. One record arm, on the selected track (it follows ALGORITHM). No
 * click (seq.c): playing, recording starts at once; stopped, REC arms and the first note
 * starts the loop (or, in an empty project, a free take that REC closes: seq.c ft_close).
 * REC again stops recording / cancels the arm (playback continues). */
UI_SMALL static void rec_toggle(void)
{
    if (ft_owns_press())
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (song.rec || rec_wait) {
        song.rec = 0;
        rec_wait = 0;
        ui_message("REC OFF");
        return;
    }
    arrangement_enabled = 0;
    if (song.playing)
        rec_begin();                    /* playing: record now */
    else
        rec_wait = 1;                   /* stopped: the first note starts it */
}

/* the loop position of track t: "bar.beat" in its pattern (1.1 .. ) */
UI_SMALL static void loop_pos(const track_t *t, char *b)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), s = t->seq_idx % len, k;
    fmt_int(b, (int32_t)(s / 16u + 1u));
    k = str_len(b);
    b[k] = '.';
    b[k + 1] = (char)('1' + (s % 16u) / 4u);
    b[k + 2] = 0;
}

/* the header of the live screens: BPM, transport, the loop position, the beat lights, REC */
UI_SMALL static void te_header(const char *title, uint16_t tc, uint32_t *cache, int32_t sect)
{
    char b[12];
    uint32_t beat = clk_beat, k, playing = song.playing;
    uint32_t sig = studio_hash((uint32_t)song.g[G_BPM] * 7u + playing * 3u + (song.rec != 0) * 1999u +
                               (playing ? beat * 131u + TSEL->seq_idx * 7919u : 0u) + (uint32_t)song.g[G_CLOCK] * 77u +
                               arrangement_enabled * 5u + (ui.bpm_t != 0) * 104729u + song.solo * 37u, title);
    sig = sig * 31u + tc;
    sig = sig * 31u + (uint32_t)(sect + 1) * 211u;    /* (2.7.2) the section A..D badge */
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 40, C_BLACK);
    fmt_int(b, song.g[G_BPM]);
    cv_text(4, 4, &FONT_L, b, ui.bpm_t ? C_WHITE : TE_G4);
    cv_text(4 + text_w(&FONT_L, b) + 4, 20, &FONT_S, "bpm", TE_G3);
    te_play_icon(104, 6, (int)playing);
    if (playing) {                                     /* bar.beat in the selected track's loop */
        loop_pos(TSEL, b);
        cv_text(122, 5, &FONT_S, b, C_WHITE);
    } else {
        cv_text(122, 5, &FONT_S, arrangement_enabled ? "song" : "loop", TE_G3);
    }
    for (k = 0; k < 4u; k++)                           /* the four beats of the bar */
        cv_rect(104 + (int32_t)k * 9, 26, 7, 7, playing && beat % 4u == k ? (k ? C_WHITE : TE_RED) : TE_G2);
    if (song.rec) {
        te_disc(224, 12, 8, TE_RED);
        cv_text(176, 4, &FONT_S, "rec", TE_RED);
        if (song.g[G_CLOCK] != 0)                       /* the click is on */
            cv_text(168, 22, &FONT_S, "click", TE_G3);
    } else {
        int32_t tx = 240 - text_w(&FONT_S, title) - 4;
        cv_text(tx, 5, &FONT_S, title, tc);
        if (sect >= 0) {                               /* (2.7.2) which section (A..D) is playing, SONG-layer colours */
            char sb[2] = {(char)('A' + (sect & 3)), 0};
            int32_t bw = text_w(&FONT_S, sb) + 8, bx = tx - bw - 6;
            cv_rect(bx, 3, bw, 14, playing ? TE_COL[sect & 3] : TE_DIM[sect & 3]);
            cv_text(bx + 4, 5, &FONT_S, sb, C_BLACK);
        }
        if (song.solo)
            cv_text(236 - text_w(&FONT_S, "solo"), 22, &FONT_S, "solo", C_WHITE);
    }
    cv_rect(0, 39, 240, 1, TE_G1);
    cv_blit(0, 0);
}

/* --------------------------------------------------------------- TRACKS --- */
/* swing, MPC style: "62%" */
UI_SMALL static void swing_str(char *b, int32_t v)            /* 0 straight .. 100 the most (params.c F_SWING) */
{
    fmt_int(b, clamp(v, 0, 100));
}

UI_SMALL static void studio_tracks_draw(void)
{
    static uint32_t head, rows[NTRK], footer;
    uint32_t i, j;
    int32_t sect = song.playing && live_sec >= 0 ? live_sec : (int32_t)song.g[G_SLOT] - 1;   /* (2.7.2) the section playing */
    te_header("tracks", TE_G3, &head, sect);
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        char b[24], e[16];
        uint32_t selected = song.sel == i, len = (uint32_t)clamp(t->p[P_SLEN], 1, 64);
        uint32_t level = i == TRK_DRUM ? song.g[G_DRLVL] : t->p[P_LEVEL];
        uint32_t silent = trk_silent(t) || !level, pos = t->seq_idx % len, rec = (song.rec >> i) & 1u;
        uint32_t solo = (song.solo >> i) & 1u, bank = song.playing ? pos / 16u : 0u, h;
        uint16_t col = TE_COL[i], dim = TE_DIM[i];
        if (i == TRK_DRUM) {
            str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
            te_lower(e, "drums", sizeof e);
        } else {
            trk_short_name(i, b);
            te_lower(e, ENGINES[t->eng_req % NENGINES]->name, sizeof e);
        }
        b[13] = 0;
        h = studio_hash(selected + level * 7u + silent * 997u + rec * 1999u + solo * 4999u + len * 37u +
                        song.playing * 7u + pos * 71u + bank * 13u, b);
        for (j = 0; j < len; j++) h = h * 31u + (uint32_t)trk_step_on(t, j);
        if (!ui.force && h == rows[i]) continue;
        rows[i] = h;
        cv_begin(240, 36, C_BLACK);
        cv_rect(2, 3, 26, 30, selected ? col : dim);   /* the track tile */
        {
            char n[2] = {(char)('1' + i), 0};
            cv_text(11, 10, &FONT_S, n, selected ? C_BLACK : col);
        }
        cv_text(34, 1, &FONT_S, b, selected ? C_WHITE : TE_G4);
        if (34 + text_w(&FONT_S, b) + 6 + text_w(&FONT_S, e) < 196)
            cv_text(34 + text_w(&FONT_S, b) + 6, 1, &FONT_S, e, selected ? col : TE_G3);
        if (rec) {
            cv_rect(200, 2, 36, 15, TE_RED);
            cv_text(206, 1, &FONT_S, "rec", C_WHITE);
        } else if (solo) {
            cv_rect(200, 2, 36, 15, C_WHITE);
            cv_text(202, 1, &FONT_S, "solo", C_BLACK);
        } else if (silent) {
            cv_text(204, 1, &FONT_S, "mute", TE_G3);
        }
        for (j = 0; j < 16u; j++) {                    /* the 16 steps in view */
            uint32_t p = bank * 16u + j, on = 0;
            if (len <= 16u) {
                if (p < len) on = trk_step_on(t, p) ? 2u : 1u;
            } else {                                    /* > 16 and stopped: the whole pattern, folded */
                uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k;
                if (song.playing) {
                    if (p < len) on = trk_step_on(t, p) ? 2u : 1u;
                } else {
                    on = 1;
                    for (k = a; k < z; k++) if (trk_step_on(t, k)) on = 2;
                }
            }
            cv_rect(34 + (int32_t)j * 10, 21, 8, 9,
                    !on ? C_BLACK : on == 2u ? (silent ? TE_G3 : selected ? col : dim) : TE_G1);
            if (on == 2u && !selected && !silent)
                cv_rect(34 + (int32_t)j * 10, 21, 8, 2, col);
            if (song.playing && p == pos)
                cv_rect(34 + (int32_t)j * 10, 31, 8, 2, C_WHITE);
        }
        cv_rect(198, 23, 38, 5, TE_G1);                /* the level */
        if (!silent)
            cv_rect(198, 23, (int32_t)level * 38 / 127, 5, selected ? col : TE_G3);
        cv_blit(0, 40 + i * 36);
    }
    {   /* KNOB 1 swing (the global groove), 2 level, 3 steps, 4 pan of the selected track */
        track_t *t = TSEL;
        static char v[4][8];
        static const char *const lab[4] = {"swing", "level", "steps", "pan"};
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        int32_t ratio[4];
        uint32_t lvl = is_drum(t) ? song.g[G_DRLVL] : t->p[P_LEVEL];
        swing_str(v[0], song.g[G_SWING]);
        fmt_int(v[1], t->p[P_MUTE] ? 0 : (int32_t)lvl * 100 / 127);
        fmt_int(v[2], t->p[P_SLEN]);
        fmt_int(v[3], t->p[P_PAN]);
        ratio[0] = song.g[G_SWING] * 10;
        ratio[1] = t->p[P_MUTE] ? 0 : (int32_t)lvl * 1000 / 127;
        ratio[2] = (t->p[P_SLEN] - 1) * 1000 / 63;
        ratio[3] = (t->p[P_PAN] + 64) * 1000 / 127;
        te_dials(184, lab, val, ratio, song.sel, &footer);
    }
}

/* ---------------------------------------------------------------- DRUMS --- */
static const char *const LV_NAME[4] = {"norm", "ghost", "soft", "hard"};
UI_SMALL static uint32_t lvl_rank(uint32_t lvl);                 /* (below) ghost 0, soft 1, norm 2, hard 3 */
UI_SMALL static uint16_t lvl_col(uint32_t lvl)                   /* a hit's colour by its level */
{
    return lvl == LV_GHOST ? TE_DIM[3] : lvl == LV_SOFT ? TE_MID[3] : lvl == LV_HARD ? C_WHITE : TE_DRUM;
}
static uint16_t pad_lit[DRUM_LANES];                   /* pads and key LEDs: frames left lit */
static uint8_t key_lit[27];                             /* menu NOTES, a synth track: frames its key stays lit */
static uint8_t key_lit_trk = 0xFF;                      /* (the track key_lit is for) */
UI_SMALL static void pads_tick(void)                             /* once a frame: the hits since the last one */
{
    uint32_t i, hits, n[NPART][4], k, w;
    fm1_irq_off();
    hits = drums.hits;
    drums.hits = 0;
    for (i = 0; i < NPART; i++)
        for (w = 0; w < 4u; w++) {
            n[i][w] = note_hits[i][w];
            note_hits[i][w] = 0;
        }
    fm1_irq_on();
    for (i = 0; i < DRUM_LANES; i++) {
        if ((hits >> i) & 1u) pad_lit[i] = 6;
        else if (pad_lit[i]) pad_lit[i]--;
    }
    /* a synth track: each note it started lights its key 6 frames (~0.1 s), so a short sequencer note
     * (a 1/16 at GATE 50 %: 60..80 ms) is seen even when it ends between two frames */
    if (song.sel != key_lit_trk) {
        memset(key_lit, 0, sizeof key_lit);
        key_lit_trk = song.sel;
    }
    for (k = 0; k < 27u; k++)
        if (key_lit[k])
            key_lit[k]--;
    if (song.sel < NPART && (n[song.sel][0] | n[song.sel][1] | n[song.sel][2] | n[song.sel][3]))
        for (k = 0; k < 27u; k++) {
            uint32_t note = kb_map(TSEL, k);
            if (note < 128u && (n[song.sel][note >> 5] >> (note & 31u)) & 1u)
                key_lit[k] = 6;
        }
}

UI_SMALL static void drum_screen_draw(void)
{
    static uint32_t head, title_sig, body_sig, footer;
    uint32_t i, j, len = (uint32_t)clamp(TDRUM->p[P_SLEN], 1, 64), kit = drum_kit(), sig, bank;
    if (drum_cursor >= len) drum_cursor = (uint8_t)(len - 1u);
    bank = drum_cursor / 16u;
    {
        char st[16], nm[9];
        const char *style = DRUM_KIT_STYLES[kit];
        if (kit >= DRUM_SYN) {                          /* SYN1..4: the name you gave it (editor DRUM SYNTH) */
            memcpy(nm, dsu_kit(kit - DRUM_SYN)->name, 8);
            nm[8] = 0;
            if (nm[0])
                style = nm;
        }
        te_lower(st, style, sizeof st);
        st[12] = 0;
        te_header(st, TE_DRUM, &head, -1);
    }
    sig = kit * 131u + drum_page * 7u + drum_lane * 977u + bank * 31u + len + (uint32_t)drum_tweaked() * 7919u;   /* title band */
    if (ui.force || sig != title_sig) {
        char b[8];
        title_sig = sig;
        cv_begin(240, 44, C_BLACK);
        cv_rect(2, 4, 34, 34, TE_DRUM);
        fmt_int(b, (int32_t)kit + 1);
        cv_text(19 - text_w(&FONT_S, b) / 2, 13, &FONT_S, b, C_BLACK);
        cv_text(44, 6, &FONT_L, DRUM_KIT_NAMES[kit], C_WHITE);
        cv_text(194, 1, &FONT_S, "grid", drum_page == 0 ? C_WHITE : TE_G3);
        cv_text(194, 15, &FONT_S, "kit", drum_page == 1 ? C_WHITE : TE_G3);
        cv_text(194, 29, &FONT_S, "sound", drum_page == 2 ? C_WHITE : TE_G3);
        cv_rect(188, 5 + (int32_t)drum_page * 14, 3, 9, TE_DRUM);
        cv_blit(0, 40);
    }
    sig = drum_page + drum_lane * 7u + drum_cursor * 101u + song.playing * 71u + len * 3u + ui.sheet * 7919u +
          (uint32_t)(TDRUM->micro[drum_cursor] + 64) * 104729u + step_fill(TDRUM, drum_cursor) * 13u;
    for (i = 0; i < DRUM_LANES; i++) sig = sig * 5u + (uint32_t)(drum_tune[i] + 8) * 9u + (uint32_t)(drum_dec[i] + 4);
    if (song.playing) sig = sig * 31u + TDRUM->seq_idx;
    for (i = 0; i < DRUM_LANES; i++) sig = sig * 3u + (pad_lit[i] != 0);
    for (i = 0; i < len; i++) {
        const dstep_t *s = &TDRUM->dstep[i];
        sig = sig * 31u + dstep_mask(s);
        sig = sig * 31u + s->lvl[0] + s->lvl[1] * 7u + s->lvl[2] * 49u + s->lvl[3] * 343u;
        sig = sig * 31u + s->rat[0] + s->rat[1] * 7u + s->rat[2] * 49u + s->rat[3] * 343u;
    }
    if (ui.force || sig != body_sig) {
        body_sig = sig;
        cv_begin(240, 100, C_BLACK);
        if (!drum_page) {                              /* GRID: the 16 lanes x 16 steps of this bank */
            for (i = 0; i < DRUM_LANES; i++) {
                int32_t y = (int32_t)i * 6 + 2;
                cv_rect(2, y, 6, 5, pad_lit[i] ? C_WHITE : i == drum_lane ? TE_DRUM : TE_G2);
                for (j = 0; j < 16u; j++) {
                    uint32_t p = bank * 16u + j;
                    const dstep_t *s = &TDRUM->dstep[p < NSTEP ? p : 0];
                    uint16_t c;
                    if (p >= len) continue;
                    c = dstep_has(s, i) ? lvl_col(dstep_lvl(s, i)) : i == drum_lane ? TE_G2 : TE_G1;
                    if (song.playing && p == TDRUM->seq_idx && c == TE_G1) c = TE_G2;
                    cv_rect(12 + (int32_t)j * 14, y, 12, 5, c);
                    if (dstep_has(s, i) && dstep_rat(s, i)) {   /* a ratchet: a notch per extra hit */
                        uint32_t r;
                        for (r = 0; r < dstep_rat(s, i); r++)
                            cv_rect(13 + (int32_t)j * 14 + (int32_t)r * 3, y + 2, 2, 1, C_BLACK);
                    }
                    if (p == drum_cursor && i == drum_lane) {
                        cv_rect(11 + (int32_t)j * 14, y - 1, 14, 1, C_WHITE);
                        cv_rect(11 + (int32_t)j * 14, y + 5, 14, 1, C_WHITE);
                    }
                }
            }
            if (ui.sheet) {                            /* (2.7.2) OCT+ held: the hit's sheet */
                const dstep_t *s = &TDRUM->dstep[drum_cursor];
                char r[4], nu[8];
                const char *val[6] = {r, nu, FILL_N[step_fill(TDRUM, drum_cursor) % 3u], "", "", ""};
                int8_t m = TDRUM->micro[drum_cursor];
                str_cpy(r, dstep_has(s, drum_lane) ? "x1" : "--", 4);
                if (dstep_has(s, drum_lane))
                    r[1] = (char)('1' + dstep_rat(s, drum_lane));
                fmt_int(nu, m);
                if (m > 0) { nu[0] = '+'; fmt_int(nu + 1, m); }
                sheet_draw(112, 1, DSHEET_N, val, ui.sheet - 1u, TE_DIM[3]);
            }
        } else {                                       /* KIT: 16 pads, lit on each hit; SOUND (2.7): the one KNOB 1 picks
                                                        * framed, a tuned or shortened / lengthened one marked */
            for (i = 0; i < DRUM_LANES; i++) {
                int32_t x = 2 + (int32_t)(i % 4u) * 60, y = (int32_t)(i / 4u) * 25;
                cv_rect(x, y, 56, 23, pad_lit[i] ? TE_DRUM : TE_G1);
                if (drum_page == 2u && i == drum_lane) {
                    cv_rect(x, y, 56, 1, C_WHITE), cv_rect(x, y + 22, 56, 1, C_WHITE);
                    cv_rect(x, y, 1, 23, C_WHITE), cv_rect(x + 55, y, 1, 23, C_WHITE);
                }
                if (drum_page == 2u && (drum_tune[i] || drum_dec[i]))
                    cv_rect(x + 3, y + 3, 4, 4, TE_DRUM);
                te_text_c(x + 28, y + 4, LANE_SHORT[i], pad_lit[i] ? C_BLACK : i == drum_lane ? C_WHITE : TE_G3);
            }
        }
        cv_blit(0, 84);
    }
    {
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        static const char *const LG[4] = {"sound", "step", "hit", "level"}, *const LK[4] = {"kit", "level", "reverb", "pan"},
                                 *const LS[4] = {"sound", "tune", "decay", "level"};
        int32_t ratio[4];
        if (!drum_page) {
            const dstep_t *s = &TDRUM->dstep[drum_cursor];
            str_cpy(v[0], LANE_SHORT[drum_lane], 8);
            fmt_int(v[1], drum_cursor + 1);
            str_cpy(v[2], dstep_has(s, drum_lane) ? "on" : "--", 4);
            str_cpy(v[3], dstep_has(s, drum_lane) ? LV_NAME[dstep_lvl(s, drum_lane)] : "--", 8);
            ratio[0] = (int32_t)drum_lane * 1000 / (DRUM_LANES - 1);
            ratio[1] = (int32_t)drum_cursor * 1000 / (int32_t)(len > 1u ? len - 1u : 1u);
            ratio[2] = v[2][0] == 'o' ? 1000 : 0;
            ratio[3] = dstep_has(s, drum_lane) ? (int32_t)lvl_rank(dstep_lvl(s, drum_lane)) * 333 : 0;   /* ghost left ..
                                                                     * hard right (2.5: it followed the stored order) */
            te_dials(184, LG, val, ratio, 1u, &footer);
        } else if (drum_page == 2u) {                  /* SOUND (2.7): the sound, its TUNE and DECAY, the drums' level */
            str_cpy(v[0], LANE_SHORT[drum_lane], 8);
            fmt_int(v[1], drum_tune[drum_lane]);
            if (drum_tune[drum_lane] > 0) { v[1][0] = '+'; fmt_int(v[1] + 1, drum_tune[drum_lane]); }
            fmt_int(v[2], drum_dec[drum_lane]);
            if (drum_dec[drum_lane] > 0) { v[2][0] = '+'; fmt_int(v[2] + 1, drum_dec[drum_lane]); }
            fmt_int(v[3], song.g[G_DRLVL] * 100 / 127);
            ratio[0] = (int32_t)drum_lane * 1000 / (DRUM_LANES - 1);
            ratio[1] = (drum_tune[drum_lane] - DRUM_TUNE_MIN) * 1000 / (DRUM_TUNE_MAX - DRUM_TUNE_MIN);
            ratio[2] = (drum_dec[drum_lane] - DRUM_DEC_MIN) * 1000 / (DRUM_DEC_MAX - DRUM_DEC_MIN);
            ratio[3] = song.g[G_DRLVL] * 1000 / 127;
            te_dials(184, LS, val, ratio, 3u, &footer);
        } else {
            fmt_int(v[0], (int32_t)kit + 1);
            fmt_int(v[1], song.g[G_DRLVL] * 100 / 127);
            fmt_int(v[2], song.g[G_DRREV] * 100 / 127);
            fmt_int(v[3], TDRUM->p[P_PAN]);
            ratio[0] = (int32_t)kit * 1000 / (int32_t)(DRUM_KITS - 1u);
            ratio[1] = song.g[G_DRLVL] * 1000 / 127;
            ratio[2] = song.g[G_DRREV] * 1000 / 127;
            ratio[3] = (TDRUM->p[P_PAN] + 64) * 1000 / 127;
            te_dials(184, LK, val, ratio, 2u, &footer);
        }
    }
}

/* the level order of the knobs: ghost < soft < norm < hard */
static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};
UI_SMALL static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }

/* the GRID page shown, no layer held: its keys are steps (seq.c kb_grid) */
UI_SMALL static int grid_keys_on(void) { return on_drum_page() && !drum_page && song.sel == TRK_DRUM && !ui.menu && !ui.sheet; }
/* a key down on the GRID page: the white keys are the 16 steps of this page of steps for the sound of
 * KNOB 1 — an empty one is set (NORM; 2.7.1: silent, for live), a set one cleared. 2.7.1: the black keys pick the sound
 * (drum_black_pick), OCT- / OCT+ the page (steps 1-16, 17-32, 33-48, 49-64), as in the SEQ layer. The cursor follows */
UI_SMALL static void grid_key(uint32_t k)
{
    uint32_t len = trk_len(TDRUM), idx;
    int32_t w = punch_key(k), b = drum_black_of(k);
    dstep_t *st;
    if (w < 0) {
        if (b >= 0)
            drum_black_pick((uint32_t)b);
        return;
    }
    idx = drum_cursor / 16u * 16u + (uint32_t)w;
    if (idx >= len)
        return;
    drum_cursor = (uint8_t)idx;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
    st = &TDRUM->dstep[idx];
    fm1_irq_off();
    if (dstep_has(st, drum_lane)) {
        dstep_clr(st, drum_lane);
        if (!dstep_mask(st)) {                          /* an empty step keeps no lock, no nudge, no condition */
            lock_del(TDRUM, idx, P_COUNT);
            TDRUM->micro[idx] = 0;
            step_fill_set(TDRUM, idx, FC_NORM);
        }
    } else {
        dstep_set(st, drum_lane, LV_NORM, 0);
        TDRUM->seq_active = 1;
    }
    fm1_irq_on();
    sync_reload = 1;                                    /* (2.7.1: not heard: the pattern plays it) */
}

/* (2.7.2) the GRID's sheet (OCT+ held), as the NOTES page's: the hit of KNOB 1's sound on KNOB 2's step. RATCHET (its
 * hits in the step), NUDGE and FILL (the step's, every sound in it), MOVE (KNOB 2: a step a detent), COPY (OCT+: to the
 * next step, then MOVE moves the copy), DELETE (OCT+) */
UI_SMALL static int drum_hit_shift(int32_t dir, int copy)   /* the hit a step on / back (copy: the next step); 0: no room */
{
    uint32_t len = trk_len(TDRUM), to = (uint32_t)((int32_t)drum_cursor + (copy ? 1 : dir));
    dstep_t *st = &TDRUM->dstep[drum_cursor];
    if (!dstep_has(st, drum_lane) || (!copy && dir < 0 && !drum_cursor) || to >= len ||
        dstep_has(&TDRUM->dstep[to], drum_lane))
        return 0;
    fm1_irq_off();
    dstep_set(&TDRUM->dstep[to], drum_lane, dstep_lvl(st, drum_lane), dstep_rat(st, drum_lane));
    if (!copy)
        dstep_clr(st, drum_lane);
    fm1_irq_on();
    drum_cursor = (uint8_t)to;
    return 1;
}
UI_SMALL static void drum_sheet_knob(uint32_t k, int32_t s)
{
    dstep_t *st = &TDRUM->dstep[drum_cursor];
    uint32_t row = ui.sheet - 1u;
    if (k == 0u) {
        ui.sheet = (uint8_t)(1 + clamp((int32_t)row + s, 0, 5));
        return;
    }
    if (k != 1u)
        return;
    undo_mark(TDRUM, (undo_sess += 4u) | 3u);
    fm1_irq_off();
    if (row == 0u && dstep_has(st, drum_lane))
        dstep_set(st, drum_lane, dstep_lvl(st, drum_lane), (uint32_t)clamp((int32_t)dstep_rat(st, drum_lane) + s, 0, 3));
    else if (row == 1u)
        TDRUM->micro[drum_cursor] = (int8_t)clamp(TDRUM->micro[drum_cursor] + s, MICRO_MIN, MICRO_MAX);
    else if (row == 2u)
        step_fill_set(TDRUM, drum_cursor, (uint32_t)clamp((int32_t)step_fill(TDRUM, drum_cursor) % 3 + (s > 0 ? 1 : -1), 0, 2));
    fm1_irq_on();
    if (row == 3u)
        for (; s; s += s > 0 ? -1 : 1)
            if (!drum_hit_shift(s > 0 ? 1 : -1, 0)) {
                ui_message(dstep_has(&TDRUM->dstep[drum_cursor], drum_lane) ? "NO ROOM" : "NO HIT");
                break;
            }
    sync_reload = 1;
}
UI_SMALL static void drum_sheet_do(void)                 /* OCT+ tapped in the sheet */
{
    undo_mark(TDRUM, (undo_sess += 4u) | 3u);
    if (ui.sheet == 5u) {
        if (drum_hit_shift(1, 1)) {
            ui.sheet = 4;                                 /* (MOVE: KNOB 2 moves the copy) */
            ui_message("COPIED");
        } else {
            ui_message(dstep_has(&TDRUM->dstep[drum_cursor], drum_lane) ? "NO ROOM" : "NO HIT");
        }
    } else if (ui.sheet == 6u) {
        fm1_irq_off();
        dstep_clr(&TDRUM->dstep[drum_cursor], drum_lane);
        fm1_irq_on();
        ui.sheet = 0;
        ui_message("HIT DELETED");
    }
    sync_reload = 1;
}

UI_SMALL static void drum_screen_input(uint32_t pressed, uint32_t home, uint32_t notes)
{
    uint32_t k, b;
    int32_t s;
    if (song.sel != TRK_DRUM || home == 1u) {
        go_home();
        return;
    }
    if (!drum_page) {                                  /* (2.7.2) OCT+ held: the hit's sheet; tapped: the page */
        uint32_t ev, ob = 1u << panel.btn[B_OCTDN];
        pressed = octp_filter(pressed, &ev);
        if (ev == 2u && !ui.sheet)
            ui.sheet = 1;
        else if (ev == 1u && ui.sheet)
            drum_sheet_do();
        else if (ev == 1u)
            pressed |= 1u << panel.btn[B_OCTUP];
        if (ui.sheet && (pressed & ob)) {
            ui.sheet = 0;
            pressed &= ~ob;
        }
        if (ev)
            ui.force = 1;
    } else {
        ui.sheet = 0;
    }
    for (k = 0; k < NB; k++) if ((pressed >> panel.btn[k]) & 1u) {
        b = k;
        if ((b == B_OCTDN || b == B_OCTUP) && !drum_page) {   /* (2.7.1) GRID: OCT- / OCT+ the page of steps */
            uint32_t len = trk_len(TDRUM), pages = (len + 15u) / 16u, pg = drum_cursor / 16u, idx;
            pg = (pg + (b == B_OCTUP ? 1u : pages - 1u)) % pages;
            idx = pg * 16u + drum_cursor % 16u;
            drum_cursor = (uint8_t)(idx < len ? idx : len - 1u);
            ui.force = 1;
            continue;
        }
        if (b == B_PLAY) {
            if (ft_owns_press()) ;
            else if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) ui_message("EMPTY SECTION: REC");
            else transport_req = song.playing ? 2 : 1;
        } else if (b == B_SEQ || b == B_EDIT) {
            drum_page = (uint8_t)((drum_page + 1u) % 3u);   /* GRID -> KIT -> SOUND (2.7) */
            ui.msg_t = 0;
            ui.force = 1;
        } else if (b == B_SAVE) {
            studio_open(SC_SONG);
            return;
        }
    }
    if ((s = panel_enc(EN_SELECT)) && (uint32_t)clamp((int32_t)drum_page + s, 0, 2) != drum_page) {   /* SELECT: grid <-
                                                                     * kit -> sound (2.7), its pages */
        drum_page = (uint8_t)clamp((int32_t)drum_page + s, 0, 2);
        ui.msg_t = 0;
        ui.force = 1;
    }
    if (drum_page == 2u && notes) {                    /* SOUND (2.7): the key played picks its sound (it is heard
                                                        * as the keys always play it) */
        for (k = 0; k < 32u; k++)
            if ((notes >> k) & 1u) {
                drum_lane = (uint8_t)lane_of_key(k);
                ui.force = 1;
            }
    }
    if ((s = panel_enc(EN_ALGO)) && !ft_on) {
        track_select((uint32_t)clamp((int32_t)song.sel + s, 0, 3));
        if (song.sel != TRK_DRUM) {
            go_home();
            return;
        }
    }
    if ((s = panel_enc(EN_PRESET))) TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + s, 0, DRUM_KITS - 1);
    for (k = 0; k < 4u; k++) if ((s = panel_enc(EN_K1 + k))) {
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (!drum_page && ui.sheet) {
            drum_sheet_knob(k, s);
        } else if (!drum_page) {
            dstep_t *st = &TDRUM->dstep[drum_cursor];
            if (k == 0) {                              /* the sound (heard: DRUM PREVIEW ON, 2.7.1) */
                uint8_t l = (uint8_t)clamp(drum_lane + s, 0, DRUM_LANES - 1);
                if (l != drum_lane) drum_hear(l);
                drum_lane = l;
            }
            if (k == 1) {                              /* the step: what it holds, heard (2.7.1: DRUM PREVIEW ON) */
                uint8_t c = (uint8_t)clamp(drum_cursor + s, 0, TDRUM->p[P_SLEN] - 1);
                if (c != drum_cursor && drum_preview) audition_step(&TDRUM->dstep[c]);
                drum_cursor = c;
            }
            if (k >= 2) {
                if (song.playing && arrangement_enabled) { ui_message("STOP THE SONG FIRST"); continue; }
                undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
                fm1_irq_off();
                if (k == 2) {
                    if (s > 0) dstep_set(st, drum_lane, LV_NORM, 0);
                    else dstep_clr(st, drum_lane);
                } else if (dstep_has(st, drum_lane)) {
                    uint32_t r = (uint32_t)clamp((int32_t)lvl_rank(dstep_lvl(st, drum_lane)) + (s > 0 ? 1 : -1), 0, 3);
                    dstep_set(st, drum_lane, LV_UP[r], dstep_rat(st, drum_lane));
                }
                fm1_irq_on();
                sync_reload = 1;
            }
        } else if (drum_page == 2u) {                  /* SOUND (2.7) */
            if (k == 0) {
                uint8_t l = (uint8_t)clamp(drum_lane + s, 0, DRUM_LANES - 1);
                if (l != drum_lane) drum_hear(l);
                drum_lane = l;
            }
            if (k == 1 || k == 2) {                    /* TUNE, DECAY: the next hit has them (heard at once) */
                int8_t *p = k == 1 ? &drum_tune[drum_lane] : &drum_dec[drum_lane];
                int32_t lo = k == 1 ? DRUM_TUNE_MIN : DRUM_DEC_MIN, hi = k == 1 ? DRUM_TUNE_MAX : DRUM_DEC_MAX;
                int8_t nv = (int8_t)clamp(*p + s, lo, hi);
                if (nv != *p) {
                    *p = nv;
                    if (!song.playing)
                        audition_lane(drum_lane);
                }
            }
            if (k == 3) song.g[G_DRLVL] = (int16_t)clamp(song.g[G_DRLVL] + s, 0, 127);
        } else {
            if (k == 0) TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + s, 0, DRUM_KITS - 1);
            if (k == 1) song.g[G_DRLVL] = (int16_t)clamp(song.g[G_DRLVL] + s, 0, 127);
            if (k == 2) song.g[G_DRREV] = (int16_t)clamp(song.g[G_DRREV] + s, 0, 127);
            if (k == 3) TDRUM->p[P_PAN] = (int16_t)clamp(TDRUM->p[P_PAN] + s, -64, 63);
        }
    }
}

/* ---------------------------------------------------------------- REC --- */
/* a big seven-segment digit, TE style: w x h, segments t thick */
UI_SMALL static void te_digit(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t d, uint16_t c)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* gfedcba */
    uint32_t m = SEG[d % 10u];
    int32_t hh = h / 2;
    if (m & 0x01) cv_rect(x + t, y, w - 2 * t, t, c);                       /* a */
    if (m & 0x02) cv_rect(x + w - t, y + t, t, hh - t, c);                  /* b */
    if (m & 0x04) cv_rect(x + w - t, y + hh, t, hh - t, c);                 /* c */
    if (m & 0x08) cv_rect(x + t, y + h - t, w - 2 * t, t, c);               /* d */
    if (m & 0x10) cv_rect(x, y + hh, t, hh - t, c);                         /* e */
    if (m & 0x20) cv_rect(x, y + t, t, hh - t, c);                          /* f */
    if (m & 0x40) cv_rect(x + t, y + hh - t / 2, w - 2 * t, t, c);          /* g */
}

static uint8_t rec_shown;
/* the four tracks, compact, from y0: the one that records is framed in red */
UI_SMALL static void rec_rows(uint32_t rt, uint32_t take, uint32_t y0, uint32_t *cache)
{
    uint32_t sig = rt * 3u + rec_wait + take * 5u + drum_kit() * 977u + y0, i, j;
    for (i = 0; i < NTRK; i++) {
        char b[16];
        if (i == TRK_DRUM) str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
        else trk_short_name(i, b);
        sig = studio_hash(sig, b) + (uint32_t)trk[i].p[P_SLEN] * 31u;
        for (j = 0; j < NSTEP; j++) sig = sig * 3u + (uint32_t)trk_step_on(&trk[i], j);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 76, C_BLACK);
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, 64), armed = i == rt;
        int32_t y = (int32_t)i * 19;
        char b[16];
        if (armed) cv_rect(0, y, 240, 18, TE_RED), cv_rect(1, y + 1, 238, 16, C_BLACK);
        cv_rect(4, y + 3, 12, 12, armed ? TE_COL[i] : TE_DIM[i]);
        if (i == TRK_DRUM) str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
        else trk_short_name(i, b);
        b[10] = 0;
        cv_text(22, y + 1, &FONT_S, b, armed ? C_WHITE : TE_G3);
        for (j = 0; j < 16u; j++) {
            uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k, on = 0;
            if (z == a) z = a + 1u;
            for (k = a; k < z && k < NSTEP; k++) if (trk_step_on(t, k)) on = 1;
            cv_rect(106 + (int32_t)j * 8, y + 5, 6, 8, on ? (armed ? TE_COL[i] : TE_DIM[i]) : TE_G1);
        }
    }
    cv_blit(0, y0);
}

/* REC armed (stopped: waiting for the first note, or PLAY), its count-in, or a free take running
 * (seq.c). Armed, the dials say how it records: KNOB 1 MODE free / tempo (an empty project),
 * KNOB 2 LENGTH (1, 2 or 4 bars), KNOB 3 START note / count, KNOB 4 QUANT on / off (2.7) (ui_input.c rec_knobs). The four tracks
 * stay in view; the REC LED blinks while armed, is lit during the take. */
UI_SMALL static void rec_screen_draw(void)
{
    static uint32_t head, body, rows, foot, dials;
    static uint8_t layout;
    uint32_t sig, take = ft_on, empty = take || project_empty(), rt = take ? ft_trk % NTRK : song.sel;
    uint32_t blink = (fm1_ms / 250u) & 1u, secs = take ? ft_t * CTL / FS : 0u, bars = 0, bpm = 0;
    uint32_t lay = take ? 1u : 2u, free = empty && !rec_tempo && !take, count = ci_on;
    if (take)
        bars = ft_fit(ft_t, &bpm);
    if (!rec_shown || lay != layout) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    rec_shown = 1;
    layout = (uint8_t)lay;
    te_header(take ? "free take" : count ? "count-in" : "rec ready", TE_RED, &head, -1);
    if (take) {                                         /* free take: the time, the loop it makes */
        sig = 1000003u + rt * 7919u + blink * 31u + secs * 131u + bars * 17u + bpm * 3u;
        if (ui.force || sig != body) {
            char b[24];
            uint32_t n = secs > 99u ? 99u : secs;
            body = sig;
            cv_begin(240, 106, C_BLACK);
            te_disc(18, 30, 9, blink ? TE_RED : TE_DIM[3]);
            if (n >= 10u)
                te_digit(36, 4, 32, 56, 7, n / 10u, C_WHITE);
            te_digit(76, 4, 32, 56, 7, n % 10u, C_WHITE);
            cv_text(112, 44, &FONT_S, "s", TE_G3);
            cv_rect(132, 6, 1, 54, TE_G1);
            if (bars) {
                fmt_int(b, (int32_t)bars);
                cv_text(146, 4, &FONT_L, b, TE_COL[rt & 3u]);
                cv_text(146 + text_w(&FONT_L, b) + 6, 18, &FONT_S, bars == 1u ? "bar" : "bars", TE_G3);
                fmt_int(b, (int32_t)bpm);
                cv_text(146, 42, &FONT_S, b, C_WHITE);
                cv_text(146 + text_w(&FONT_S, b) + 4, 42, &FONT_S, "bpm", TE_G3);
            } else {
                cv_text(146, 4, &FONT_L, "-", TE_G3);
            }
            te_text_c(120, 80, "press rec on the 1", TE_RED);
            cv_blit(0, 42);
        }
        rec_rows(rt, take, 148u, &rows);
        if (ui.force || foot != 1u) {
            foot = 1u;
            cv_begin(240, 16, C_BLACK);
            cv_text(4, 0, &FONT_S, "rec: close", TE_G3);
            cv_text(236 - text_w(&FONT_S, "play: drop"), 0, &FONT_S, "play: drop", TE_G3);
            cv_blit(0, 224);
        }
        return;
    }
    foot = 0;
    /* armed / counting in: what happens next */
    sig = 2000003u + empty * 7u + free * 11u + rec_count * 13u + count * 17u + ci_beat * 19u + blink * 31u +
          rec_quant_off * 37u;
    if (ui.force || sig != body) {
        static const char *const L1[3] = {"play freely", "play a note", "press play"};
        static const char *const L2[3] = {"then rec on the 1", "it starts the loop", "4 clicks, then rec"};
        static const char *const L3[3] = {"the tempo follows you", "play: go  rec: cancel", "rec: cancel"};
        uint32_t m = free ? 0u : rec_count ? 2u : 1u;
        body = sig;
        cv_begin(240, 64, C_BLACK);
        if (count) {                                    /* the count-in: 4, 3, 2, 1 */
            te_digit(96, 4, 30, 54, 6, 4u - (ci_beat > 3u ? 3u : ci_beat), C_WHITE);
            te_disc(60, 31, 12, TE_RED);
            cv_text(144, 14, &FONT_S, "count-in", TE_G4);
            cv_text(144, 34, &FONT_S, "rec: cancel", TE_G3);
        } else {
            te_disc(28, 30, 18, blink ? TE_RED : TE_DIM[3]);
            te_disc(28, 30, 7, C_BLACK);
            cv_text(60, 6, &FONT_S, L1[m], C_WHITE);
            cv_text(60, 24, &FONT_S, L2[m], TE_G4);
            cv_text(60, 42, &FONT_S, L3[m], TE_G3);
        }
        cv_blit(0, 42);
    }
    rec_rows(rt, 0u, 106u, &rows);
    {   /* the dials: how it records */
        static char v[3][10];
        static const char *const LAB_E[4] = {"mode", "length", "start", "quant"};
        static const char *const LAB_F[4] = {"mode", "", "", "quant"};
        static const char *const LAB_N[4] = {"", "length", "start", "quant"};
        static const char *const LAB_0[4] = {"", "", "", ""};
        const char *val[4] = {v[0], v[1], v[2], rec_quant_off ? "off" : "on"};   /* (2.7) QUANT */
        int32_t ratio[4] = {0, 0, 0, 0};
        uint32_t len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, 64);
        str_cpy(v[0], rec_tempo ? "tempo" : "free", sizeof v[0]);
        if (len % 16u == 0u) {
            fmt_int(v[1], (int32_t)(len / 16u));
            str_cpy(v[1] + str_len(v[1]), len == 16u ? " bar" : " bars", 6);
        } else {
            fmt_int(v[1], (int32_t)len);
            str_cpy(v[1] + str_len(v[1]), " st", 4);
        }
        str_cpy(v[2], rec_count ? "count" : "note", sizeof v[2]);
        ratio[0] = rec_tempo ? 1000 : 0;
        ratio[1] = (int32_t)(len - 1u) * 1000 / 63;
        ratio[2] = rec_count ? 1000 : 0;
        ratio[3] = rec_quant_off ? 0 : 1000;
        {
            const char *const *lab = count ? LAB_0 : !empty ? LAB_N : free ? LAB_F : LAB_E;
            uint32_t k;
            for (k = 0; k < 4u; k++)
                if (!lab[k][0])
                    val[k] = "";                        /* (a dial not shown: no value either) */
            te_dials(184, lab, val, ratio, rt * 7u + count * 3u, &dials);
        }
    }
}
