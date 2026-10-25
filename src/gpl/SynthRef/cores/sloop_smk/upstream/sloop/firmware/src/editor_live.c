/* SPDX-License-Identifier: GPL-3.0-only */
/* The editor's LIVE page (SLOOP 2.7, protocol v12): included by editor.c (ed_b / ed_v write the reply); the host
 * tests include it with their own ed_b / ed_v (tests/ui_pages_test.c) */
/* ---- v12 (SLOOP 2.7): LIVE, the editor's LIVE page (web/EDITOR_PROTOCOL.md "Live"). One command, an op and its
 * arguments; every reply is the live state. What the FM-1's own buttons do, the same code:
 *   0 the state only                    1 PLAY: 0 stop, 1 play, 2 as the PLAY button (play / stop)
 *   2 a layer key: layer (5 SEL, 6 GLO, 7 SAVE), white key 0..15, down (1) / up (0)  (ui_layers.c layer_key: mute,
 *     solo, fill, fill bar, tap tempo; sections A..D on the next bar, store, loop / song, SONG REC; the song's key)
 *   3 a punch-in effect: fx 0..26, on / off (as GLO > MIDI 3 > FX CH: seq.c midi_fx)
 *   4 REC as the REC button (ui_studio.c rec_toggle)      5 undo (0) / redo (1) as EDIT + OCT- / OCT+
 *   6 ROLL (as ARP + key): track, note (the drums: lane), level, down / up; the rate is GLO's ROLL (G_ROLL)
 *   7 ERASE as it plays (as EDIT + key): track, note (the drums: lane), down / up
 *   8 the pattern (as EDIT + KNOB 1..3): track, tool (0 shift, 1 length x2 / half, 2 transpose), step +1 (1) / -1 (0)
 *   9 a chain of sections (as SAVE + several keys): n (0: the chain stops), then n sections (0..3), up to CHAIN_MAX
 *  10 THROW (as FX + OCT- / OCT+): bit 0 the reverb, bit 1 the echo, while set (0: let go)
 *  11 the master level: 0..126, 127: back to the MASTER knob (turning the knob also takes it back)
 *  12 a drum sound's TUNE and DECAY (the DRUMS screen's SOUND page): lane, TUNE + 8 (0..15), DECAY + 4 (0..7)
 * The notes themselves go as plain MIDI notes (the tracks' channels): no SysEx between a key and its sound.
 * State: flags (playing, PLAY asked, song mode, song plays, fill now, fill next bar, SONG REC), flags 2 (REC armed
 * and waiting, undo / redo there, a free take running), BPM v14, the section playing + 1, the one asked for + 1, the chain (entries, the
 * one playing), mutes, solos, REC tracks, the selected track, the punch-in effect + 1, CPU %, the song (entry,
 * entries, its bar, its bars), the sections stored (bit per section), then per track: its step, its length, its meter (8 log2 |peak|, 0 = silent);
 * then (2.7, ops 6..12) the master (0: the knob, else the page's level + 1), the THROWs (bits), the 16 drum sounds' TUNE + 8 | DECAY + 4 << 4 */
#define ED_LIVE 77u
UI_SMALL static void ed_live(const uint8_t *a, uint32_t na)
{
    uint32_t i, m = 0, s = 0, play;
    if (na >= 2u) {
        switch (a[0]) {
        case 1:
            play = a[1] == 2u ? !song.playing : a[1];
            if (!play && (song.playing || transport_req))
                transport_req = 2;
            else if (play == 1u && !song.playing && !transport_req) {
                if (arrangement_enabled && !arr_valid(&arrangement, arrangement_ready()))
                    ui_message("EMPTY SECTION: REC");
                else
                    transport_req = 1;
            }
            break;
        case 2:
            if (na >= 4u && a[1] >= LY_SCALE && a[1] <= LY_SONG && a[2] < 16u)
                layer_key(a[1], key_of_white(a[2]), a[3] & 1u);
            break;
        case 3:
            if (na >= 3u && a[1] < PUNCH_NFX) {
                fm1_irq_off();
                midi_fx(53u + punch_key_of_fx(a[1]), a[2] & 1u);
                fm1_irq_on();
            }
            break;
        case 4:
            if (ft_on)
                transport_req = 1;                   /* a free take (an empty project): REC closes it and the loop
                                                      * plays, as the REC button (seq.c ft_block); PLAY does the same */
            else
                rec_toggle();
            ui.force = 1;
            break;
        case 5:
            ui_message(undo_swap(a[1] & 1u) ? (a[1] & 1u ? "REDO" : "UNDO") : "NOTHING TO UNDO");
            break;
        case 6:                                         /* ROLL: the key 0x80 + note (keyboard_block leaves it alone) */
        case 7:                                         /* ERASE */
            if (na >= (a[0] == 6u ? 5u : 4u) && a[1] < NTRK) {
                track_t *t = &trk[a[1]];
                uint32_t nt = is_drum(t) ? a[2] & 15u : a[2] & 127u, down = a[a[0] == 6u ? 4 : 3] & 1u;
                fm1_irq_off();
                if (a[0] == 6u) {
                    for (i = 0; i < NROLL; i++)
                        if (roll[i].on && roll[i].key == (0x80u | nt) && roll[i].trk == a[1])
                            roll_end(i);
                    if (down)
                        roll_start(0x80u | nt, t, nt, a[3] & 3u);
                } else if (down) {
                    if (!erasing(t) || er_trk != a[1])
                        undo_erase_sess = (undo_sess += 4u) | 2u;
                    if (er_trk != a[1]) {
                        er_lanes = 0;
                        er_notes[0] = er_notes[1] = er_notes[2] = er_notes[3] = 0;
                        er_trk = (uint8_t)a[1];
                    }
                    if (is_drum(t))
                        er_lanes |= (uint16_t)(1u << nt);
                    else
                        er_notes[nt >> 5] |= 1u << (nt & 31u);
                    erase_now(t);
                } else if (er_trk == a[1]) {
                    if (is_drum(t))
                        er_lanes &= (uint16_t)~(1u << nt);
                    else
                        er_notes[nt >> 5] &= ~(1u << (nt & 31u));
                }
                fm1_irq_on();
            }
            break;
        case 8:                                         /* the pattern: each change one undo */
            if (na >= 4u && a[1] < NTRK) {
                int32_t d = a[3] & 1u ? 1 : -1;
                ui.step_sess = 0;
                if (a[2] == 0u)
                    pattern_rotate(&trk[a[1]], d);
                else if (a[2] == 1u)
                    pattern_length(&trk[a[1]], d);
                else if (a[2] == 2u)
                    pattern_transpose(&trk[a[1]], d);
                ui.step_sess = 0;
                sync_reload = 1;
            }
            break;
        case 9:                                         /* a chain (ui_layers.c chain_release): the first is asked for */
            if (a[1] == 0u) {
                chain_n = 0;
            } else if (song.playing && !arrangement_clock.running && a[1] <= CHAIN_MAX && na >= 2u + a[1]) {
                for (i = 0; i < a[1]; i++)
                    if (!((arrangement_ready() >> (a[2 + i] & 3u)) & 1u))
                        break;
                if (i == a[1]) {
                    fm1_irq_off();
                    for (i = 0; i < a[1]; i++)
                        chain_sec[i] = a[2 + i] & 3u;
                    chain_i = 0;
                    live_req = (int8_t)(a[2] & 3u);
                    chain_bars = (uint8_t)section_bars(a[2] & 3u);
                    chain_n = (uint8_t)(a[1] >= 2u ? a[1] : 0u);
                    fm1_irq_on();
                } else {
                    ui_message("EMPTY SECTION");
                }
            }
            break;
        case 10:
            thr_ed = (uint8_t)(a[1] & 3u);
            break;
        case 11:
            ed_master_k = 0xFFFFu;
            ed_master = (uint8_t)(a[1] >= 127u ? 0u : a[1] + 1u);
            break;
        case 12:
            if (na >= 4u) {
                drum_tune[a[1] & 15u] = (int8_t)clamp((int32_t)a[2] - 8, DRUM_TUNE_MIN, DRUM_TUNE_MAX);
                drum_dec[a[1] & 15u] = (int8_t)clamp((int32_t)a[3] - 4, DRUM_DEC_MIN, DRUM_DEC_MAX);   /* (the DRUMS
                                                         * screen sees them: its signature has them; nothing else redraws) */
            }
            break;
        default:
            break;
        }
    }
    for (i = 0; i < NTRK; i++) {
        m |= (trk[i].p[P_MUTE] != 0) << i;
    }
    ed_b(a[0]);
    ed_b((song.playing != 0) | (transport_req != 0) << 1 | (arrangement_enabled != 0) << 2 |
         (arrangement_clock.running != 0) << 3 | (fill_now != 0) << 4 | (fill_arm != 0) << 5 | (srec != 0) << 6);
    ed_b((rec_wait != 0) | (undo.valid && !undo.undone) << 1 | (undo.valid && undo.undone) << 2 | (ft_on != 0) << 3);
    ed_v(song.g[G_BPM]);
    ed_b((uint32_t)(live_sec + 1));
    ed_b((uint32_t)(live_req + 1));
    ed_b(chain_n);
    ed_b(chain_i);
    ed_b(m);
    ed_b(song.solo & 15u);
    ed_b(song.rec & 15u);
    ed_b(song.sel);
    ed_b((uint32_t)(punch.req + 1));
    ed_b(clamp((int32_t)((song.cpu_q8 * 100u) >> 8), 0, 127));
    ed_b(arrangement_clock.index);
    ed_b(arrangement.count);
    ed_b(arrangement_clock.bar);
    ed_b(arrangement.entry[arrangement_clock.index % ARR_STEPS].bars);
    ed_b(arrangement_ready() & 15u);
    for (i = 0; i < NTRK; i++) {
        int32_t pk = i == TRK_DRUM ? drums.peak : trk[i].peak, lg = 0;
        ed_b(trk[i].seq_idx);
        ed_b(trk_len(&trk[i]));
        if (pk >= 64) {
            while ((pk >> lg) > 1)
                lg++;
            s = (uint32_t)(lg * 8 + (((pk << 3) >> lg) & 7));   /* 48 (-54 dB) .. 128 (+6 dB), as the TRACKS meters */
        } else {
            s = 0;
        }
        ed_b(s > 127u ? 127u : s);
    }
    ed_b(ed_master);
    ed_b(thr_ed | thr_req);
    for (i = 0; i < DRUM_LANES; i++)
        ed_b((uint32_t)(drum_tune[i] + 8) | (uint32_t)(drum_dec[i] + 4) << 4);
}
