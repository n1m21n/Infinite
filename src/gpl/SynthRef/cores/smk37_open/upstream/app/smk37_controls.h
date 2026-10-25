/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef SMK37_CONTROLS_H
#define SMK37_CONTROLS_H
#include "fm1.h"
enum { SMK37_BUTTON, SMK37_KEY, SMK37_PRESSURE, SMK37_PAD,
       SMK37_ENCODER, SMK37_COMBINATION, SMK37_PEDAL, SMK37_FADER };
void smk37_controls_reset(void);
void smk37_control_event(unsigned kind, unsigned id, int value);
void smk37_controls_snapshot(struct fm1_controls *out);
void smk37_controls_panic(void);
void smk37_controls_release(void);
unsigned smk37_fader_bank(void);
unsigned smk37_pad_steps(void);
unsigned smk37_shift(void);
uint32_t smk37_pad_color(unsigned pad, unsigned lit);
unsigned smk37_pickup_mask(void);
uint32_t smk37_fader_feedback(void);
unsigned smk37_master_level(void);
void smk37_master_set(unsigned level);
unsigned smk37_screen_track(void);
int smk37_screen_octave(void);
unsigned smk37_performance_track(unsigned pad);
int smk37_performance_octave(unsigned pad);
unsigned smk37_pad_focus(void);
unsigned smk37_pad_aftertouch(void);
void smk37_screen_note(unsigned owner, unsigned note);
void smk37_screen_pad_mode(unsigned steps, unsigned instrument);
void smk37_panel_preferences_read(uint8_t data[4]);
void smk37_panel_preferences_apply(const uint8_t data[4]);
void groove_settings_panel_changed(void);
int smk37_screen_pad(unsigned pad, unsigned velocity, unsigned steps);
unsigned smk37_screen_pad_key(unsigned pad);
enum { SMK37_PAGE_BLUETOOTH, SMK37_PAGE_SOURCE, SMK37_PAGE_MIDI };
void smk37_screen_page(unsigned page);
void smk37_screen_shortcut(unsigned pad);
#endif
