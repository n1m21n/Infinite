/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Physical controls shared by Pro, Elite, host tests and the workbench. */
#include "smk37_controls.h"
#include "instrument.h"
#include <string.h>

struct held_note { uint8_t note, channel; };
/* One ownership table: 37 keys followed by 16 Pads. A release scans it once. */
static struct held_note held_notes[53];
static struct fm1_controls panel;
static uint32_t physical_buttons, shifted_buttons;
static uint8_t bank, step_pads, pickup, fader_valid, master;
static uint8_t faders[4], targets[8], target_valid;
static uint32_t fader_feedback;
static uint16_t pedal_channels, shortcut_pads, controller_dirty, pressure_dirty;
static uint8_t pad_navigation[16], pedal_down;
static uint8_t pad_pressure[16], channel_pressure[16];
static uint16_t wheel_channels[2];
static uint8_t wheel_value[2] = {64,0};
static uint8_t wheel_last_channel[2];

/* FM1 button IDs remain the common application's interface. Physical IDs
 * come exclusively from the Pro/Elite scanner and are never used as MIDI IDs. */
static const uint8_t button_map[18] = {
    255,255,8,9,0,1,10,255,3,4,7,255,8,6,2,11,12,13
};
static const uint8_t encoder_map[7] = {2,3,4,5,0,1,6};

static int send(unsigned status, unsigned a, unsigned b)
{
    uint8_t bytes[3]={(uint8_t)status,(uint8_t)a,(uint8_t)b};
    size_t size=(status&0xf0)==0xd0?2:3;
    if(!groove_midi_local(bytes,size))return 0;
    smk37_controls_panic();
    return -1;
}

static void release(struct held_note *held)
{
    if(held->note<128) {
        unsigned note=held->note,channel=held->channel;
        held->note=255;
        for(unsigned i=0;i<53;++i)
            if(held_notes[i].note==note && held_notes[i].channel==channel) {
                /* The engine counts each physical press. Retire this one
                 * while the shared external note keeps its remaining owner. */
                const uint8_t off[3]={(uint8_t)(0x80|channel),(uint8_t)note,0};
                if(groove_midi_setting(0,3) && fm1_midi_post(off,3))smk37_controls_panic();
                return;
            }
        (void)send(0x80|channel,note,0);
    }
}

/* Shared by initialisation, panic and route edits. Keep one cold reset body;
 * hardware scanning and audio rendering retain their O2 optimisation. */
__attribute__((noinline)) void groove_midi_panel_reset(void)
{
    memset(held_notes,255,sizeof held_notes);
    memset(pad_pressure,0,sizeof pad_pressure);memset(channel_pressure,0,sizeof channel_pressure);
    pedal_channels=controller_dirty=pressure_dirty=0;wheel_channels[0]=wheel_channels[1]=0;
    memset(wheel_last_channel,255,sizeof wheel_last_channel);
}

/* The control task receives this after the engine resets a track's voices.
 * Retain note/neutral-release owners: other tracks may share the channel. */
void groove_midi_panel_track_reset(unsigned track)
{
    unsigned bit=1U<<groove_midi_input_channel(track);
    controller_dirty|=bit;pressure_dirty|=bit;
    memset(wheel_last_channel,255,sizeof wheel_last_channel);
}

void smk37_controls_panic(void)
{
    const uint8_t reset=255;
    (void)groove_midi_local(&reset,1);
    groove_midi_panel_reset();
    memset(pad_navigation,255,sizeof pad_navigation);panel.keys=0;
    /* Queue loss can discard button-up and encoder events as well as notes. */
    physical_buttons=shifted_buttons=panel.buttons=shortcut_pads=0;
    memset(panel.encoders,0,sizeof panel.encoders);
    pedal_down=0;
    wheel_value[0]=64;wheel_value[1]=0;
}

void smk37_controls_reset(void)
{
    memset(&panel,0,sizeof panel);
    /* Retain physical positions during a route edit; only a full reset changes
     * them and clears panel navigation. New notes restore controller state. */
    groove_midi_panel_reset();
    memset(pad_navigation,255,sizeof pad_navigation);
    memset(faders,0,sizeof faders);memset(targets,0,sizeof targets);
    physical_buttons=shifted_buttons=shortcut_pads=bank=step_pads=pickup=fader_valid=target_valid=0;
    fader_feedback=0;
    pedal_down=0;master=127;panel.knob=1023;
    wheel_value[0]=64;wheel_value[1]=0;
}

static void play(struct held_note *held,unsigned note,unsigned velocity,unsigned track,unsigned keyboard)
{
    groove_midi_poll();
    release(held);
    if(note>=128 || !velocity)return;
    smk37_screen_note(track,note);
    unsigned channel=groove_midi_input_channel(track);
    if(groove_track_role(track)==GROOVE_TRACK_OFF)return;
    unsigned bit=1U<<channel,restore=controller_dirty&bit;
    if(keyboard && pedal_down && (restore || !(pedal_channels&bit))) {
        if(send(0xb0|channel,64,127))return;
        pedal_channels|=1U<<channel;
    }
    for(unsigned w=0;keyboard && w<2;++w) if(wheel_value[w]!=(w?0U:64U) && (restore || !(wheel_channels[w]&bit))) {
        if(send((w?0xb0:0xe0)|channel,w?1:0,wheel_value[w]))return;
        wheel_channels[w]|=1U<<channel;
    }
    if(pressure_dirty&bit) {
        if(channel_pressure[channel] && send(0xd0|channel,channel_pressure[channel],0))return;
        pressure_dirty&=~bit;
    }
    if(!send(0x90|channel,note,velocity)) {
        held->note=note;held->channel=channel;
        if(keyboard)controller_dirty&=~bit;
    }
}

static void pressure(unsigned id,unsigned value)
{
    groove_midi_poll();
    if(held_notes[37+id].note>=128)return;
    unsigned channel=held_notes[37+id].channel,bit=1U<<channel;
    if(pad_pressure[id]==value && !(pressure_dirty&bit))return;
    unsigned previous=pad_pressure[id];
    pad_pressure[id]=value;
    unsigned maximum=channel_pressure[channel];
    /* Only a falling maximum needs a scan. A weaker pad cannot change the
     * channel's pressure; a rising maximum already supplies the answer. */
    if(value>maximum)maximum=value;
    else if(previous==maximum) {
        maximum=0;
        for(unsigned i=0;i<16;++i)
            if(held_notes[37+i].note<128 && held_notes[37+i].channel==channel && pad_pressure[i]>maximum)
                maximum=pad_pressure[i];
    } else if(!(pressure_dirty&bit))return;
    if(channel_pressure[channel]!=maximum || (pressure_dirty&bit)) {
        if(!send(0xd0|channel,maximum,0)) {
            channel_pressure[channel]=maximum;
            pressure_dirty&=~bit;
        }
    }
}

static void fader(unsigned id,unsigned value)
{
    unsigned track=4*bank+id;
    struct groove_view view;groove_get_view(track,0,&view);
    unsigned target=view.volume, previous=faders[id];
    unsigned moved=(fader_valid&(1U<<id)) && value!=previous;
    if(!(target_valid&(1U<<track)) || targets[track]!=target)
        pickup|=1U<<id;
    targets[track]=target;target_valid|=1U<<track;
    /* Match within one MIDI step, or cross the target between ADC samples.
     * First samples establish positions without changing a saved project. */
    if((value+1>=target && value<=target+1) ||
       ((fader_valid&(1U<<id)) && ((previous<=target && value>=target) ||
                                 (previous>=target && value<=target))))
        pickup&=~(1U<<id);
    faders[id]=value;fader_valid|=1U<<id;
    if(moved && !(pickup&(1U<<id)) && value!=target) {
        if(groove_midi_setting(track,3)) {
            groove_control_edit(track,0,(int)value-(int)target,0);
            targets[track]=value;
        }
        groove_midi_fader(track,value);
    }
    if(moved) {
        /* A generation byte preserves repeated motion between UI polls.
         * Initial ADC samples establish pickup silently. */
        fader_feedback=((fader_feedback+(1U<<24))&0xff000000U)|(1U<<23)|
            id|(track<<2)|(value<<5)|(target<<12)|
            (((pickup>>id)&1U)<<19)|(!!groove_midi_setting(track,3)<<20);
    }
}

void smk37_control_event(unsigned kind,unsigned id,int value)
{
    if(kind==SMK37_KEY && id<37) {
        if(!value)release(&held_notes[id]);
        else {
            unsigned track=smk37_performance_track(0);
            int note;
            if(groove_source_type(track)==GROOVE_SAMPLE) {
                /* C D E F G A B C: the eight sample slots, independent of OCT.
                 * Keep the engine's MIDI map for external input and recording. */
                static const uint8_t slots[13]={0,255,1,255,2,3,255,4,255,5,255,6,7};
                note=id<13?groove_pad_note(track,slots[id]):128;
            } else note=48+(int)id+12*smk37_performance_octave(0);
            play(&held_notes[id],(unsigned)note,(unsigned)value,track,1);
        }
    } else if(kind==SMK37_PAD && id<16) {
        unsigned bit=1U<<id;
        if(shortcut_pads&bit) {if(!value)shortcut_pads&=~bit;return;}
        pressure(id,0);release(&held_notes[37+id]);
        unsigned previous=pad_navigation[id];
        if(!value) {
            pad_navigation[id]=255;
        } else if(physical_buttons&1U) {
            shortcut_pads|=bit;pad_navigation[id]=255;
            smk37_screen_shortcut(id);
        } else {
            unsigned key=smk37_screen_pad_key(id);
            if(key<27)pad_navigation[id]=key;
            else if(!smk37_screen_pad(id,(unsigned)value,step_pads)) {
                unsigned track=smk37_performance_track(1);
                unsigned count=groove_pad_count(track);
                unsigned note=count?groove_pad_note(track,id):48+id+12*smk37_performance_octave(1);
                play(&held_notes[37+id],note,(unsigned)value,track,0);
            }
        }
        if(previous!=pad_navigation[id]) {
            panel.keys=0;
            for(unsigned i=0;i<16;++i)if(pad_navigation[i]<27)panel.keys|=1U<<pad_navigation[i];
        }
    } else if(kind==SMK37_PRESSURE && id<16) {
        pressure(id,(unsigned)value);
    } else if(kind==SMK37_ENCODER && id<8) {
        if(id<7) {
            int sum=panel.encoders[encoder_map[id]]+value;
            panel.encoders[encoder_map[id]]=sum>32767?32767:sum<-32768?-32768:sum;
        } else {
            int next=master+value;master=next<0?0:next>127?127:next;
            panel.knob=(master*1023U+63U)/127U;
        }
    } else if(kind==SMK37_FADER && id<4) fader(id,(unsigned)value);
    else if(kind==SMK37_FADER && id<6) {
        unsigned channel=groove_midi_input_channel(smk37_performance_track(0));
        unsigned wheel=id-4,neutral=wheel?0:64;
        /* ADC motion beyond a calibrated end stop can repeat the same value.
         * Keep new channel owners and neutral releases, omit duplicate bends. */
        if(value!=(int)neutral && wheel_value[wheel]==value &&
           wheel_last_channel[wheel]==channel)return;
        wheel_value[wheel]=(uint8_t)value;
        /* Initial/stationary neutral samples cannot edit a new track.
         * Return to neutral only on channels that used this wheel. */
        if(value!=(int)neutral)wheel_channels[wheel]|=1U<<channel;
        unsigned channels=value==(int)neutral?wheel_channels[wheel]:1U<<channel;
        while(channels) {
            unsigned ch=(unsigned)__builtin_ctz(channels);
            channels&=channels-1;
            if(send((wheel?0xb0:0xe0)|ch,wheel?1:0,(unsigned)value))return;
        }
        wheel_last_channel[wheel]=channel;
        if(value==(int)neutral)wheel_channels[wheel]=0;
    } else if(kind==SMK37_PEDAL) {
        pedal_down=value>=64;
        if(value>=64) {
            unsigned track=smk37_performance_track(0);
            unsigned channel=groove_midi_input_channel(track),bit=1U<<channel;
            if(!(pedal_channels&bit)) {
                if(send(0xb0|channel,64,127))return;
                pedal_channels|=bit;
            }
        } else {
            unsigned channels=pedal_channels;
            while(channels) {
                unsigned channel=(unsigned)__builtin_ctz(channels);
                channels&=channels-1;
                if(send(0xb0|channel,64,0))return;
            }
            pedal_channels=0;
        }
    } else if(kind==SMK37_BUTTON && id<18) {
        unsigned bit=1U<<id, was=physical_buttons&bit;
        if(!!was==!!value)return;
        if(value)physical_buttons|=bit;else physical_buttons&=~bit;
        if(!value)shifted_buttons&=~bit;
        if(value) {
            if(physical_buttons&1U)shifted_buttons|=bit&((1U<<9)|(1U<<12)|(1U<<15));
            if(id==1) {bank^=1;pickup=15;}
            else if(id==7) {
                unsigned instrument=!(physical_buttons&1U);
                step_pads=instrument?0:!step_pads;
                /* Cancel held menu keys before moving pages. Sounding notes
                 * retain their original channel until physical release. */
                memset(pad_navigation,255,sizeof pad_navigation);panel.keys=0;
                smk37_screen_pad_mode(step_pads,instrument);
            }
            else if(id==11)smk37_screen_page(SMK37_PAGE_BLUETOOTH);
            else if(id==12 && (physical_buttons&1U))smk37_screen_page(SMK37_PAGE_SOURCE);
            else if(id==15 && (physical_buttons&1U))smk37_screen_page(SMK37_PAGE_MIDI);
        }
        /* Bind a shortcut when its physical button is pressed. Releasing
         * SHIFT first must not turn LFO into ENV or close the SOURCE page. */
        panel.buttons=0;
        for(unsigned i=0;i<18;++i)if((physical_buttons&(1U<<i)) && button_map[i]<14) {
            if((i==12 || i==15) && (shifted_buttons&(1U<<i)))continue;
            unsigned button=button_map[i];
            if(i==9 && (shifted_buttons&(1U<<9)))button=5;
            panel.buttons|=1U<<button;
        }
    }
}

void smk37_controls_snapshot(struct fm1_controls *out)
{
    groove_midi_poll();
    *out=panel;memset(panel.encoders,0,sizeof panel.encoders);
}
unsigned smk37_fader_bank(void) {return bank;}
unsigned smk37_pad_steps(void) {return step_pads;}
unsigned smk37_shift(void) {return physical_buttons&1U;}
unsigned smk37_pad_aftertouch(void)
{ return channel_pressure[groove_midi_input_channel(smk37_performance_track(1))]; }
uint32_t smk37_pad_color(unsigned pad,unsigned lit)
{
    if(smk37_shift()) {
        if(pad<8)return pad==smk37_screen_track()?0x183030:0x041010;
        if(groove_track_role(pad-8)==GROOVE_TRACK_OFF)return 0x040404;
        struct groove_view view;groove_get_view(pad-8,0,&view);
        return view.muted?0x301008:0x081808;
    }
    return lit?0x183010:step_pads?0x100818:0x040804;
}
unsigned smk37_pickup_mask(void) {return pickup;}
uint32_t smk37_fader_feedback(void) {return fader_feedback;}
unsigned smk37_master_level(void) {return panel.knob;}
void smk37_master_set(unsigned level)
{panel.knob=level;master=(level*127U+511U)/1023U;}
void smk37_controls_release(void)
{
    for(unsigned i=0;i<37;++i)smk37_control_event(SMK37_KEY,i,0);
    for(unsigned i=0;i<16;++i)smk37_control_event(SMK37_PAD,i,0);
    for(unsigned i=0;i<18;++i)smk37_control_event(SMK37_BUTTON,i,0);
    smk37_control_event(SMK37_PEDAL,0,0);
    /* Navigation releases only moved wheels. An unsolicited neutral CC1
     * would overwrite the 303's saved envelope modulation just by browsing. */
    for(unsigned w=0;w<2;++w)if(wheel_value[w]!=(w?0:64))
        smk37_control_event(SMK37_FADER,4+w,w?0:64);
    memset(panel.encoders,0,sizeof panel.encoders);
}
