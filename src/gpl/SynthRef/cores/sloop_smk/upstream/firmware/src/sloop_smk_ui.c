/* SPDX-License-Identifier: GPL-3.0-only */
static int32_t encs[7];
#ifndef SLOOP_SMK_HARDWARE
static uint32_t fm1_ticks(void) {return fm1_ms*24000u;}
#endif
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) {int32_t s=encs[e];encs[e]=0;return s;}
static uint8_t fm1_led[16],fm1_led_dim[16],fm1_led_bg[16];
static volatile uint16_t fm1_led_bg_ns;
#define FM1_NCOL 16u
/* A virtual LED matrix with the firmware's logical IDs (buttons 0..13,
   notes 14..40). No GPIO matrix or hardware calibration is needed. */
static const int8_t FM1_KEYMAP[5][16]={
    {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
    {16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31},
    {32,33,34,35,36,37,38,39,40,-1,-1,-1,-1,-1,-1,-1},
    {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}};
static void fm1_led_key(uint32_t id,int on) {(void)id;(void)on;}
static uint32_t edges_btn,notes_seen,smk_physical_buttons;
static volatile uint32_t fm1_remote_btn;
static uint32_t fm1_input_edges(int x) {uint32_t next=smk_physical_buttons|fm1_remote_btn; uint32_t e=edges_btn|(next&~fm1_in.buttons); (void)x; fm1_in.buttons=next; edges_btn=0;return e;}
static uint32_t fm1_input_note_edges(void) {uint32_t e=fm1_in.notes&~notes_seen;notes_seen=fm1_in.notes;return e;}
#ifndef SLOOP_SMK_HARDWARE
static void fm1_wdt_feed(void) {}
#endif
static int32_t fm1_adc_read(int c) {(void)c;return -1;}
#ifndef SLOOP_SMK_HARDWARE
static void fm1_delay_ms(uint32_t ms) {fm1_ms+=ms;}
#endif
static struct {uint32_t magic,stage,page,home,ui_frames;} felucca_dbg;
#define FELUCCA_ICONS 1
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N],scope_bufr[SCOPE_N];
static uint32_t scope_w;
#ifndef SLOOP_SMK_HARDWARE
static struct {uint32_t up,config,suspended,setups,sof_seen,resets;} usb;
#endif
#ifndef SLOOP_SMK_HARDWARE
static int usb_cdc_now(void) {return 0;}
static int usb_rate_now(void) {return 0;}
#endif
#include "../../sloop/firmware/src/panel.c"
#include "../../sloop/firmware/src/ui.c"
#include "../../sloop/firmware/src/upreset.c"
#include "../../sloop/firmware/src/project.c"
#include "../../sloop/firmware/src/ui_song.c"
#include "../../sloop/firmware/src/ui_studio.c"
#include "../../sloop/firmware/src/icons.c"
#include "../../sloop/firmware/src/ui_draw.c"
#include "../../sloop/firmware/src/ui_vis.c"
#include "../../sloop/firmware/src/ui_layers.c"
#include "../../sloop/firmware/src/ui_menu.c"
#include "../../sloop/firmware/src/ui_input.c"
