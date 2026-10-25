/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
/* Audio/event consumer writes; main-loop snapshot must exclude that IRQ.
 * A release in the same audio block as its press still gets a visible pulse.
 * Only physical pad events feed this state, never sequencer drum activity. */
typedef struct { uint32_t at[16]; uint16_t seen, held; } smk37_pad_feedback_t;
static void smk37_pad_feedback_event(smk37_pad_feedback_t *s, unsigned pad,
                                    unsigned velocity, uint32_t now) {
    if (pad >= 16) return;
    uint16_t bit=(uint16_t)(1u<<pad);
    if (velocity) { s->at[pad]=now; s->seen|=bit; s->held|=bit; }
    else s->held&=(uint16_t)~bit;
}
static uint16_t smk37_pad_feedback_mask(smk37_pad_feedback_t *s, uint32_t now) {
    uint16_t mask=s->held;
    for(unsigned i=0;i<16;i++)
        if(s->seen&(1u<<i)) {
            if((uint32_t)(now-s->at[i])<120u)mask|=(uint16_t)(1u<<i);
            else s->seen&=(uint16_t)~(1u<<i);
        }
    return mask;
}
