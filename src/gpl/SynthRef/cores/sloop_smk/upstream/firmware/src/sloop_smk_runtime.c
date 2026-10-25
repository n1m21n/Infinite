/* SPDX-License-Identifier: GPL-3.0-only
 * Sloop 2.8 musical/UI core on SMK peripherals. No FM-1 GPIO or flash driver. */
#include <stdint.h>
#define SLOOP_SMK 1
static uint8_t smk37_trs_enabled;
static uint32_t smk37_trs_bytes;
#if defined(SLOOP_SMK_HARDWARE) && SMK37_ENABLE_TRS
static void smk37_trs_output(uint32_t packet);
#define MIDI_OUTPUT_HOOK(p) smk37_trs_output(p)
#endif
#define FELUCCA_ARRANGER 1
#if (defined(SLOOP_SMK_HARDWARE) && SMK37_ENABLE_SAVES) || defined(SLOOP_STORAGE_TEST_BACKEND)
#define FELUCCA_FLASH 1
#else
#define FELUCCA_FLASH 0
#endif
#define FELUCCA_ICONS 1
#ifndef SLOOP_SMK_VERSION
#define SLOOP_SMK_VERSION "SLOOP SMK 2.8.0-b3"
#endif
#define FELUCCA_VERSION SLOOP_SMK_VERSION
/* seq.c precedes gfx.c here; establish upstream's cold/UI size policy before
 * its host fallback can define UI_SMALL as empty. DSP keeps the normal -Os. */
#if defined(__clang__)
#define UI_SMALL __attribute__((minsize))
#else
#define UI_SMALL
#endif
#include "felucca_tables.h"
#include "../../sloop/firmware/src/libc.c"
static struct { volatile uint32_t notes,buttons; } fm1_in;
#ifndef SLOOP_SMK_HARDWARE
static void fm1_irq_off(void){}
static void fm1_irq_on(void){}
static uint8_t sample_memory[4][0x14000];
#ifndef SMP_USER_XIP
#define SMP_USER_XIP(k) sample_memory[k]
#endif
#else
static const uint8_t empty_sample[512];
#if SMK37_ENABLE_SAMPLES
static const uint8_t *sloop_sample_xip(unsigned k);
#define SMP_USER_XIP(k) sloop_sample_xip(k)
#else
#define SMP_USER_XIP(k) empty_sample
#endif
#endif
#include "../../sloop/firmware/src/core.h"
#include "sloop_smk_cpu1.h"
#include "../../sloop/firmware/src/engines.c"
#include "../../sloop/firmware/src/drums.c"
#include "../../sloop/firmware/src/params.c"
#include "../../sloop/firmware/src/voice.c"
#include "../../sloop/firmware/src/slicer.c"
#include "../../sloop/firmware/src/fx.c"
#ifndef SLOOP_SMK_HARDWARE
#define MQ 256u
static uint32_t midi_in_q[MQ],mi_r,mi_w;
static void midi_out_event(uint32_t p){
#ifdef MIDI_OUTPUT_HOOK
    MIDI_OUTPUT_HOOK(p);
#else
    (void)p;
#endif
}
#else
#include "../../sloop/firmware/src/usb.c"
#endif
#include "../../sloop/firmware/src/arranger.c"
#include "../../sloop/firmware/src/seq.c"
#include "../../sloop/firmware/src/gfx.c"
#if FELUCCA_FLASH
#ifdef SLOOP_STORAGE_TEST_BACKEND
#include SLOOP_STORAGE_TEST_BACKEND
#else
#include "sloop_smk_storage.c"
#endif
#endif
#if defined(SLOOP_SMK_HARDWARE) && SMK37_ENABLE_SAMPLES
#include "sloop_smk_samples.c"
#endif
#if !FELUCCA_FLASH
static uint32_t st_crc32(const void *p,uint32_t n){const uint8_t*b=p;uint32_t c=~0u;while(n--){c^=*b++;for(unsigned i=0;i<8;i++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
#endif
#include "sloop_smk_ui.c"
#if defined(SLOOP_SMK_HARDWARE) && SMK37_ENABLE_SAMPLES
static void sloop_samples_service(void){
    if(!sloop_samples_requested)return;
    sloop_samples_requested=0;
    ui_message(sloop_sample_initialize_request()?"SAMPLES INIT FAILED":"3 SAMPLE SLOTS READY");
    ui.force=1;
}
#endif
static uint32_t sloop_master_level = 1024;
static uint8_t sloop_knob_bank,sloop_fader_bank,sloop_pad_bank;
static uint64_t sloop_keys;
#include "smk37_pad_feedback.h"
static smk37_pad_feedback_t sloop_pad_feedback;
static uint16_t sloop_pads;
static uint8_t sloop_note[53][4],sloop_n[53],sloop_owner[53],sloop_channel[53],sloop_key_layer[37];
static uint8_t sloop_sustain;
static uint32_t sloop_status[16];
static void sloop_init(void){
    lights_lvl=LIGHTS_LOW; /* Default on; a valid saved brightness still wins below. */
    memset(&song,0,sizeof song);memset(trk,0,sizeof trk);
    for(unsigned i=0;i<G_COUNT;i++)song.g[i]=GP[i].def;
    fm6_init();
    for(unsigned k=0;k<NTRK;k++){
        track_defaults(&trk[k]);track_defaults_steps(&trk[k]);
        if(k<NPART){set_engine_of(&trk[k],TRK_DEF[k][0]);apply_preset_to(&trk[k],TRK_DEF[k][1]);trk[k].engine=trk[k].eng_req;}
    }
    TDRUM->p[P_E0]=DRUM_DEFAULT_KIT;song.master_q12=sloop_master_level=1024;ed_master=0;
    memset(proj_slot,0,sizeof proj_slot);memset(up_bank,0,sizeof up_bank);
#if !FELUCCA_FLASH
    persist_boot();
#else
    arr_defaults(&arrangement);
#endif
    panel_init();settings_init();layers_init();bank_resolve();
    settings.palette=4;palette_set(4);go_home();ui.force=1;
#ifndef SLOOP_SMK_HARDWARE
    memset(sample_memory,255,sizeof sample_memory);
#endif
    for(unsigned k=0;k<4;k++)smp_user_scan(k);
}
static void sloop_midi(unsigned st,unsigned a,unsigned b){
    if(st<128||st>=240||a>=128||b>=128)return;
    if(mi_w-mi_r>=MQ){panic_req=15;mi_r=mi_w;return;}
    midi_in_q[mi_w++%MQ]=(st>>4)|(st<<8)|(a<<16)|(b<<24);
}
static void sloop_note_event(unsigned key,unsigned velocity){
    if(key>=53)return;
    if(!velocity){
        if(key<37&&sloop_key_layer[key]){fm1_in.notes&=~(1u<<(key-5));sloop_key_layer[key]=0;return;}
        unsigned t=sloop_owner[key];for(unsigned i=0;i<sloop_n[key];i++){input_off(&trk[t],sloop_note[key][i]);midi_out_note(sloop_channel[key],sloop_note[key][i],0);}
        sloop_n[key]=0;return;
    }
    if(sloop_n[key])return;
    if(key>=5&&key<32&&layer_now()!=LY_PLAY){fm1_in.notes|=1u<<(key-5);sloop_key_layer[key]=1;return;}
    unsigned t=key>=37?TRK_DRUM:song.sel%NTRK;
    sloop_owner[key]=t;sloop_channel[key]=(uint8_t)trk_midi_ch(t);
    if(t==TRK_DRUM){unsigned lane=key>=37?key-37:lane_of_note(48+key);drum_input(lane,vel_lvl(velocity),0,1);midi_out_note(trk_midi_ch(t),LANE_NOTE[lane],velocity);sloop_n[key]=1;sloop_note[key][0]=LANE_NOTE[lane];return;}
    int note=clamp(48+(int)key+song.octave*12,0,127);
    if(trk[t].p[P_CHORD])sloop_n[key]=chord_play_notes(&trk[t],note,chord_mods(),sloop_note[key]);
    else{sloop_n[key]=1;sloop_note[key][0]=(uint8_t)note;}
    for(unsigned i=0;i<sloop_n[key];i++){
        unsigned n=sloop_note[key][i];step_in_push(n|0x80u|t<<8);input_on(&trk[t],n,velocity);midi_out_note(trk_midi_ch(t),n,velocity);
    }
}
static const int8_t sloop_buttons[18]={-1,-1,B_OCTDN,B_OCTUP,B_OCTDN,B_OCTUP,B_ARP,B_ARP,B_SCL,B_SCL,B_GLO,B_EDIT,B_HOME,B_SAVE,B_FX,B_SEQ,B_PLAY,B_REC};
static void sloop_event(unsigned kind,unsigned id,int value){
    if(kind==7){transport_req=value?1:2;return;}
    if(kind==0&&id<18){
        if(id<2){if(value){if(id==0)sloop_knob_bank^=1;else sloop_fader_bank^=1;}return;}
        unsigned b=sloop_buttons[id];
        uint32_t bit=1u<<panel.btn[b];if(value){if(!(fm1_in.buttons&bit))edges_btn|=bit;smk_physical_buttons|=bit;}else smk_physical_buttons&=~bit;fm1_in.buttons=smk_physical_buttons|fm1_remote_btn;
    }
    if(kind==1&&id<8){
        value=clamp(value,-32,32);
        if(id==0&&!sloop_knob_bank){sloop_master_level=clamp(song.master_q12+value*64,0,4096);ed_master=0;song.master_q12=sloop_master_level;ui.force=1;return;}
        if(sloop_knob_bank&&id==1){if(song.sel<NPART)set_engine((TSEL->eng_req+NENGINES+value)%NENGINES);return;}
        if(sloop_knob_bank&&id==3){ui.page=(ui.page+NPAGES+value)%NPAGES;ui.home=0;ui.force=1;return;}
        const unsigned roles[8]={EN_ALGO,EN_SELECT,EN_PRESET,EN_ALGO,EN_K1,EN_K2,EN_K3,EN_K4};
        unsigned role=roles[id];encs[panel.enc[role]]+=value;ui.enc_t[role]=fm1_ticks()-61000u*FM1_TICKS_PER_US;
    }
    if(kind==2&&id<37){if(value)sloop_keys|=(uint64_t)1<<id;else sloop_keys&=~((uint64_t)1<<id);sloop_note_event(id,clamp(value,0,127));}
    if(kind==3&&id<16){smk37_pad_feedback_event(&sloop_pad_feedback,id,value,fm1_ms);if(value)sloop_pads|=1u<<id;else sloop_pads&=~(1u<<id);sloop_note_event(37+id,clamp(value,0,127));}
    if(kind==4&&id<4){unsigned p=sloop_fader_bank?P_ATK+id:P_LEVEL;track_t*t=sloop_fader_bank?TSEL:&trk[id];t->p[p]=clamp(value,0,1023)*127/1023;ui.force=1;}
    if(kind==5){for(unsigned t=0;t<NPART;t++)if(t==song.sel||sloop_sustain||trk[t].nheld){if(!id){trk[t].bend=clamp(value,-8192,8191);unsigned bend=trk[t].bend+8192;if(trk_midi_ch(t)<16)midi_out_event(14u|((0xe0u|trk_midi_ch(t))<<8)|((bend&127u)<<16)|((bend>>7)<<24));}else{unsigned mod=clamp(value,0,127);midi_cc(&trk[t],1,mod);if(trk_midi_ch(t)<16)midi_out_event(11u|((0xb0u|trk_midi_ch(t))<<8)|(1u<<16)|(mod<<24));}}}
    if(kind==6){sloop_sustain=!!value;for(unsigned t=0;t<NPART;t++){midi_cc(&trk[t],64,value?127:0);if(trk_midi_ch(t)<16)midi_out_event(11u|((0xb0u|trk_midi_ch(t))<<8)|(64u<<16)|((value?127u:0u)<<24));}}
}
static void sloop_release(void){
    for(unsigned k=0;k<53;k++)sloop_note_event(k,0);
    for(unsigned t=0;t<NPART;t++){
        midi_cc(&trk[t],64,0);trk[t].bend=0;
        unsigned ch=trk_midi_ch(t);
        if(ch<16){midi_out_event(11u|((0xb0u|ch)<<8)|(64u<<16));midi_out_event(14u|((0xe0u|ch)<<8)|(64u<<24));}
    }
    memset(&sloop_pad_feedback,0,sizeof sloop_pad_feedback);
    sloop_keys=sloop_pads=sloop_sustain=fm1_in.notes=fm1_in.buttons=smk_physical_buttons=fm1_remote_btn=0;edges_btn=notes_seen=0;memset(encs,0,sizeof encs);layer_unlock();ui.home_t0=ui.hold_kind=ui.step_held=0;lk_r=lk_w;panic_req=15;
}
static void sloop_tick(void){uint32_t k=(ed_master?ed_master-1u:0u)*1023u/126u;song.master_q12=ed_master?(k*k)>>8:sloop_master_level;ui_input();ui_leds();ui_draw();autosave_tick();sections_flush();}
static uint32_t *sloop_get_status(void){
    uint32_t leds=0;for(unsigned k=2;k<18;k++){unsigned q=led_pos[panel.btn[(unsigned)sloop_buttons[k]]];if(q!=255&&((fm1_led[q>>3]>>(q&7))&1))leds|=1u<<k;}
    leds|=sloop_knob_bank|(sloop_fader_bank<<1);
    sloop_status[0]=leds;sloop_status[1]=sloop_keys;sloop_status[2]=sloop_keys>>32;sloop_status[3]=smk37_pad_feedback_mask(&sloop_pad_feedback,fm1_ms);sloop_status[4]=ui.page;sloop_status[5]=song.sel;sloop_status[6]=song.playing;sloop_status[7]=sloop_knob_bank;sloop_status[8]=sloop_fader_bank;sloop_status[9]=song.octave+3;sloop_status[10]=sloop_pad_bank;sloop_status[11]=song.master_q12;
    for(unsigned i=0;i<4;i++)sloop_status[12+i]=(sloop_fader_bank?TSEL->p[P_ATK+i]:trk[i].p[P_LEVEL])*1023u/127u;return sloop_status;
}
