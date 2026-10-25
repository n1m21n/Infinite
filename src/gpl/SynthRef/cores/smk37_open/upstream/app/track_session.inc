/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Included by both engines after their track helpers. A session role changes
 * execution, never the eight-row project layout. Dormant project presets are
 * kept separately from the live Play instrument and survive save/load/undo. */
FM1_DSP_CODE unsigned groove_track_role(unsigned t)
{ return t < GROOVE_TRACKS ? track_roles[t] : GROOVE_TRACK_OFF; }
FM1_DSP_CODE unsigned groove_midi_input_channel(unsigned t)
{ return t < GROOVE_TRACKS ? input_channels[t] : 16; }
static unsigned track_group(unsigned role)
{
    unsigned mask = 0;
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t)
        if (track_roles[t] == role) mask |= 1U << t;
    return mask;
}
static unsigned track_audible(unsigned t)
{
    unsigned group = track_group(track_roles[t]);
    return track_roles[t] != GROOVE_TRACK_OFF && !(mute_mask & (1U << t)) &&
        (!(solo_mask & group) || (solo_mask & (1U << t)));
}
static void session_mix_refresh(void)
{
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
        unsigned audible = track_audible(t);
        if (tracks[t].audible && !audible)
            for (unsigned note = 0; note < 128; ++note)
                if ((seq_note[t].bits[note >> 3] & (1U << (note & 7U))) || arpeggio[t].note == note)
                    groove_midi_note(t, note, 0);
        tracks[t].audible = (uint8_t)audible;
    }
}
static void session_arp_apply(unsigned t, const uint8_t arp[2])
{
    memset(&arpeggio[t], 0, sizeof arpeggio[t]);
    arpeggio[t].note = 255; arpeggio[t].random = 0x9e3779b9U ^ (t + 1U);
    arpeggio[t].rate = arp[0] & 3U; arpeggio[t].mode = (arp[0] >> 2) & 3U;
    arpeggio[t].octaves = (arp[0] >> 4) & 3U; arpeggio[t].gate = arp[0] >> 6;
    arpeggio[t].swing = arp[1] & 63U; arpeggio[t].hold = (arp[1] >> 6) & 1U;
    arpeggio[t].enabled = arp[1] >> 7;
}
static void session_project_row(unsigned t, const struct groove_project *p)
{
    struct dormant_track *d = dormant[t];
    d->preset = p->preset[t]; memcpy(d->arp, p->arp[t], 2);
    d->mute = (p->mute >> t) & 1U; d->solo = (p->solo >> t) & 1U;
}
static void session_init(void)
{
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
        free(dormant[t]); dormant[t] = NULL; track_roles[t] = GROOVE_TRACK_NORMAL;
        input_channels[t] = (uint8_t)groove_channel(t);
    }
}
int groove_arrangement_capture(struct groove_project *p)
{
    /* Pattern and dormant rows belong to this control task. Only the live
     * instrument snapshot needs the render gate, inside project_capture. */
    int r = groove_project_capture(p);
    if (r) return r;
    groove_sequence_copy(p);
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) if (dormant[t]) {
        uint8_t pages = p->preset[t].sequence_pages;
        p->preset[t] = dormant[t]->preset; p->preset[t].sequence_pages = pages;
        memcpy(p->arp[t], dormant[t]->arp, 2);
        p->mute = (uint8_t)((p->mute & ~(1U << t)) | (unsigned)dormant[t]->mute << t);
        p->solo = (uint8_t)((p->solo & ~(1U << t)) | (unsigned)dormant[t]->solo << t);
    }
    return 0;
}
int groove_arrangement_tuning_capture(struct groove_tuning tables[GROOVE_TRACKS])
{
    int r = groove_tuning_capture(tables);
    if (!r) for (unsigned t = 0; t < GROOVE_TRACKS; ++t)
        if (dormant[t]) tables[t] = dormant[t]->tuning;
    return r;
}
int groove_arrangement_tuning_apply(const struct groove_tuning tables[GROOVE_TRACKS])
{
    if (!tables || memchr(tables, 0x80, sizeof(struct groove_tuning) * GROOVE_TRACKS)) return -1;
    SESSION_PAUSE();
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
        if (dormant[t]) dormant[t]->tuning = tables[t];
        else (void)groove_tuning_set(t, &tables[t]);
    }
    SESSION_RESUME();
    return 0;
}
int groove_track_role_set(unsigned t, unsigned role)
{
    if (t >= GROOVE_TRACKS || role > GROOVE_TRACK_OFF) return GROOVE_PROJECT_INVALID;
    unsigned old = track_roles[t];
    if (role == old) return 0;
    SESSION_PAUSE();
    struct dormant_track *d = dormant[t];
    if (!d) {
        struct groove_project *p = groove_project_scratch();
        int r = groove_project_capture(p);
        if (r) { SESSION_RESUME(); return r; }
        d = malloc(sizeof *d);
        if (!d) { SESSION_RESUME(); return GROOVE_PROJECT_MEMORY; }
        dormant[t] = d; session_project_row(t, p); groove_tuning_get(t, &d->tuning);
    }
    struct source next;
    unsigned replace = role != GROOVE_TRACK_OFF &&
        (!sources[t].ptr || (role == GROOVE_TRACK_NORMAL && tracks[t].type != d->preset.type));
    if (replace && source_create(&next, d->preset.type, d->preset.program)) {
        if (old == GROOVE_TRACK_NORMAL) { free(d); dormant[t] = NULL; }
        SESSION_RESUME(); return GROOVE_PROJECT_MEMORY;
    }
    panic(t);
    groove_edit_track_reset(t);
    if (role == GROOVE_TRACK_OFF || replace) {
        source_destroy(t); memset(&sources[t], 0, sizeof sources[t]);
        if (replace) sources[t] = next;
    }
    track_roles[t] = (uint8_t)role;
    if (role == GROOVE_TRACK_OFF) {
        tracks[t].type=d->preset.type; tracks[t].program=d->preset.program;
        tracks[t].volume=d->preset.volume; tracks[t].controls=d->preset.controls;
        tracks[t].modified=d->preset.modified;
    }
    if (role == GROOVE_TRACK_NORMAL || (old == GROOVE_TRACK_OFF && role == GROOVE_TRACK_PLAY)) {
        source_reset(&sources[t], d->preset.type);
        mute_mask = (mute_mask & ~(1U << t)) | (unsigned)d->mute << t;
        solo_mask = (solo_mask & ~(1U << t)) | (unsigned)d->solo << t;
        track_preset_apply(t, &d->preset); session_arp_apply(t, d->arp);
        (void)groove_tuning_set(t, &d->tuning);
    }
    if (role == GROOVE_TRACK_NORMAL) { free(d); dormant[t] = NULL; }
    if (role == GROOVE_TRACK_PLAY) { mute_mask &= ~(1U << t); solo_mask &= ~(1U << t); }
    groove_sequence_source_changed(t);
    session_mix_refresh();
    publish(t, 0); stereo_refresh(); audio_reset(); status[t].edit_dirty = 1;
    /* Undo snapshots made under a different role would reinterpret rows. */
    groove_edit_forget_history();
    SESSION_RESUME(); return 0;
}
int groove_midi_input_set(unsigned t, unsigned channel)
{
    if (t >= GROOVE_TRACKS || channel >= 16) return GROOVE_PROJECT_INVALID;
    if (input_channels[t] == channel) return 0;
    SESSION_PAUSE();
    /* A physical key may have fed several tracks on the previous channel. */
    for (unsigned i=0;i<GROOVE_TRACKS;++i) { panic(i); groove_edit_track_reset(i); }
    groove_midi_panic(); input_channels[t] = (uint8_t)channel;
    /* The board keeps physical positions, but retired notes and controller
     * destinations must not suppress fresh events on the new channel map. */
    extern void groove_midi_panel_reset(void) __attribute__((weak));
    if (groove_midi_panel_reset) groove_midi_panel_reset();
    SESSION_RESUME();
    return 0;
}
