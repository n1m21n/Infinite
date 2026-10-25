/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef DX7_ADSR_H
#define DX7_ADSR_H
#include "types.h"
typedef enum { ADSR_STATE_IDLE, ADSR_STATE_ATTACK, ADSR_STATE_DECAY, ADSR_STATE_SUSTAIN, ADSR_STATE_RELEASE } AdsrState;
typedef struct {
    uint32_t attack, decay; /* unsigned Q32 progress increments */
    int32_t sustain;       /* Q30 level */
    uint32_t release;
    AdsrState state;
    int32_t current_level;
    uint32_t time_in_state; /* Q32 progress, reset at each stage */
    bool is_held;
    uint16_t edit_samples; /* gain slew after a live edit; uses existing padding */
    int32_t level_record;
} AdsrEnvelope;
typedef struct { uint32_t attack, decay; int32_t sustain; uint32_t release; u8 enabled; } t_dx7_adsr_config;
void adsr_config_set(t_dx7_adsr_config *, u8, u8, u8, u8, u8, uint32_t sample_rate);
void adsr_init(AdsrEnvelope *, const t_dx7_adsr_config *);
void adsr_reconfigure(AdsrEnvelope *, const t_dx7_adsr_config *, u8 was_enabled);
void adsr_note_off(AdsrEnvelope *);
int32_t adsr_update(AdsrEnvelope *, u8 enabled);
#endif
