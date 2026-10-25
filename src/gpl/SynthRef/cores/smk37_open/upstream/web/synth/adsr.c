/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "fixed.h"
#include "adsr.h"
/* Live edits may move a held gain across its full range in about 12 ms.
 * Normal note attacks/releases retain their original curves and timing. */
enum { EDIT_SAMPLES = 512, EDIT_STEP = FM_Q30_ONE / EDIT_SAMPLES };
/* Millisecond parameter mapping is unchanged. Q32 phase replaces repeated
 * floating-point time additions and has no per-sample division. */
static uint32_t increment(uint32_t rate, unsigned ms)
{
    uint64_t samples = ((uint64_t)rate * ms + 500) / 1000;
    if (!samples) return 0;
    uint64_t step = (UINT64_C(4294967296) + samples / 2) / samples;
    return step > UINT32_MAX ? UINT32_MAX : (uint32_t)step;
}
FM1_DSP_CODE void adsr_config_set(t_dx7_adsr_config *c, u8 enabled, u8 a, u8 d, u8 s, u8 r, uint32_t rate)
{
    if (a > 100) a = 100;
    if (d > 100) d = 100;
    if (s > 100) s = 100;
    if (r > 100) r = 100;
    c->attack = increment(rate, 8U * a);
    c->decay = increment(rate, 100U + 8U * d);
    c->release = increment(rate, 100U + 19U * r);
    c->sustain = (int32_t)((int64_t)(100U + 7U * s) * FM_Q30_ONE / 1000);
    c->enabled = enabled != 0;
}
FM1_DSP_CODE void adsr_init(AdsrEnvelope *e, const t_dx7_adsr_config *c)
{
    e->attack=c->attack; e->decay=c->decay; e->sustain=c->sustain; e->release=c->release;
    e->state=ADSR_STATE_ATTACK; e->current_level=e->level_record=0;
    e->time_in_state=0; e->is_held=true;
    e->edit_samples=0;
    if(!c->enabled){e->state=ADSR_STATE_SUSTAIN;e->current_level=FM_Q30_ONE;}
}
FM1_DSP_CODE void adsr_reconfigure(AdsrEnvelope *e, const t_dx7_adsr_config *c, u8 was_enabled)
{
    if(c->enabled!=was_enabled || (c->enabled &&
       (c->attack!=e->attack || c->decay!=e->decay || c->sustain!=e->sustain || c->release!=e->release)))
        e->edit_samples=EDIT_SAMPLES;
    /* Attaching an overlay to an existing voice starts from its audible gain,
     * not a frozen attack at zero. A quick bypass/re-enable also keeps that gain. */
    if(c->enabled && !was_enabled){
        e->state=e->is_held?ADSR_STATE_SUSTAIN:ADSR_STATE_RELEASE;
        e->time_in_state=0;e->level_record=e->current_level;
    }
    e->attack=c->attack;e->decay=c->decay;e->sustain=c->sustain;e->release=c->release;
}
FM1_DSP_CODE void adsr_note_off(AdsrEnvelope *e)
{
    e->is_held=false;
    if(e->state!=ADSR_STATE_IDLE){e->state=ADSR_STATE_RELEASE;e->time_in_state=0;e->level_record=e->current_level;}
}
FM1_DSP_CODE int32_t adsr_update(AdsrEnvelope *e, u8 enabled)
{
    if(!enabled){
        if(e->current_level>=FM_Q30_ONE-EDIT_STEP){e->edit_samples=0;return e->current_level=FM_Q30_ONE;}
        return e->current_level+=EDIT_STEP;
    }
    if(e->state==ADSR_STATE_IDLE)return 0;
    uint32_t step=e->state==ADSR_STATE_ATTACK?e->attack:e->state==ADSR_STATE_DECAY?e->decay:e->release;
    uint32_t old=e->time_in_state;
    if(e->state!=ADSR_STATE_SUSTAIN)e->time_in_state+=step;
    unsigned done=e->time_in_state<old||!step||step==UINT32_MAX;
    int32_t progress=(int32_t)(e->time_in_state>>2),target=0,smoothing=FM_Q30_ONE/2;
    switch(e->state){
    case ADSR_STATE_ATTACK:
        if(done){target=FM_Q30_ONE;e->level_record=target;e->state=ADSR_STATE_DECAY;e->time_in_state=0;}
        else {target=progress;e->level_record=target;}
        break;
    case ADSR_STATE_DECAY:
        smoothing=375809638;
        if(done){target=e->sustain;e->state=ADSR_STATE_SUSTAIN;e->time_in_state=0;}
        else {target=FM_Q30_ONE-fm_mul30(FM_Q30_ONE-e->sustain,progress);e->level_record=target;}
        break;
    case ADSR_STATE_SUSTAIN:target=e->sustain;break;
    case ADSR_STATE_RELEASE:
        smoothing=FM_Q30_ONE/8;
        if(done)e->state=ADSR_STATE_IDLE;
        else target=fm_mul30(e->level_record,FM_Q30_ONE-progress);
        break;
    default:return 0;
    }
    int32_t change=fm_mul30(target-e->current_level,smoothing);
    if(e->edit_samples){
        --e->edit_samples;
        if(change>EDIT_STEP)change=EDIT_STEP;
        if(change<-EDIT_STEP)change=-EDIT_STEP;
    }
    e->current_level+=change;
    return e->current_level;
}
