/* SPDX-License-Identifier: GPL-3.0-only */
/* Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
 *
 * Synth REF cores for M-VAVE's open firmwares, which share one application
 * (the FM-1 "groove" app) and one host platform:
 *   - built with -DSR_MVAVE_SMK37: SMK-37 Pro running SMK37 OPEN V01
 *     (github.com/m-vave/smk37_open, vendored in smk37_open/upstream/)
 *   - built without it: FM-1 running FM1 OPEN V14
 *     (github.com/m-vave/fm1_open, vendored in fm1_open/upstream/)
 * The firmware's own browser host (web/engine/Simulator.c) is compiled
 * natively with the rest of the open application, unmodified; this file only
 * maps that host onto the Synth REF core ABI. No proprietary SDK library is
 * linked: the open web engine replaces libfm1_engine.a entirely.
 *
 * The firmware keeps its state in C globals, so the core is SR_CORE_SINGLETON
 * and the host loads one copy of the DLL per Synth REF instance. */
#include <stdint.h>
#include <string.h>

#include "../SynthRefCoreApi.h"

#include "fm1.h"
#include "fm1_block.h"
#include "fm_patch.h"
#include "instrument.h"
#ifdef SR_MVAVE_SMK37
#include "smk37_controls.h"
#endif

/* web/engine/Simulator.c */
void sim_boot(unsigned clear);
void sim_control(unsigned kind, unsigned index, int value);
void sim_release(void);
void sim_panic(void);
const float* sim_render(void);
const uint8_t* sim_state(void);
const uint16_t* sim_pixels(void);
uint32_t sim_frame_revision(void);
uint8_t* sim_flash(void);
uint32_t sim_flash_revision(void);
uint8_t* sim_file(void);
const uint8_t* sim_factory(void);
unsigned sim_factory_size(void);
int sim_export(void);
int sim_import(unsigned bytes);
const char* sim_factory_name(unsigned index);
int sim_factory_project(unsigned index);
int sim_timbre_import(void);
int sim_midi(unsigned bytes);
const char* sim_project_name(void);
uint32_t sim_edit_revision(void);
uint32_t sim_project_revision(void);
#ifdef SR_MVAVE_SMK37
int sim_smk37(unsigned kind, unsigned id, int value);
unsigned sim_smk37_state(void);
unsigned sim_smk37_instruments(void);
const char* sim_smk37_pad_labels(void);
#endif
/* app/ (not in a public header) */
extern void groove_screen_snapshot(uint8_t state[8]);

#define CORE_EXPORT __declspec(dllexport)

enum
{
   STATE_MAGIC = 0x31535253u, /* "SRS1" */
   STATE_VERSION = 1u,
   FILE_BYTES = GROOVE_PROJECT_TUNED_BYTES,
   LED_COUNT = 41,
   SYSEX_MAX = 1024
};

#define KEYS(n) { SR_CTL_KEY, n, SR_MODE_VELOCITY, 0, 127, 0, 0 }
#define BTN(n, name) { SR_CTL_BUTTON, n, SR_MODE_SWITCH, 0, 1, 0, name }
#define ENC(n, name) { SR_CTL_ENCODER, n, SR_MODE_RELATIVE, -127, 127, 0, name }

#ifdef SR_MVAVE_SMK37
#define PADS(n) { SR_CTL_PAD, n, SR_MODE_VELOCITY, 0, 127, 0, 0 }
#define PRES(n) { SR_CTL_PRESSURE, n, SR_MODE_ABSOLUTE, 0, 127, 0, 0 }
static const SynthRefControlInfo kControls[] = {
   /* Physical button ids 0..17, names as printed on the SMK-37 Pro panel. */
   BTN(0, "SHIFT"), BTN(1, "FADER BANK"), BTN(2, "HOME"), BTN(3, "SAVE"), BTN(4, "OCT -"), BTN(5, "OCT +"),
   BTN(6, "ARP"), BTN(7, "PAD / STEP"), BTN(8, "SCL"), BTN(9, "ENV / LFO"), BTN(10, "MIX"), BTN(11, "BT"),
   BTN(12, "HOME / SRC"), BTN(13, "EDIT"), BTN(14, "FX"), BTN(15, "SEQ"), BTN(16, "PLAY"), BTN(17, "REC"),
   KEYS(0), KEYS(1), KEYS(2), KEYS(3), KEYS(4), KEYS(5), KEYS(6), KEYS(7), KEYS(8), KEYS(9),
   KEYS(10), KEYS(11), KEYS(12), KEYS(13), KEYS(14), KEYS(15), KEYS(16), KEYS(17), KEYS(18), KEYS(19),
   KEYS(20), KEYS(21), KEYS(22), KEYS(23), KEYS(24), KEYS(25), KEYS(26), KEYS(27), KEYS(28), KEYS(29),
   KEYS(30), KEYS(31), KEYS(32), KEYS(33), KEYS(34), KEYS(35), KEYS(36),
   PADS(0), PADS(1), PADS(2), PADS(3), PADS(4), PADS(5), PADS(6), PADS(7),
   PADS(8), PADS(9), PADS(10), PADS(11), PADS(12), PADS(13), PADS(14), PADS(15),
   PRES(0), PRES(1), PRES(2), PRES(3), PRES(4), PRES(5), PRES(6), PRES(7),
   PRES(8), PRES(9), PRES(10), PRES(11), PRES(12), PRES(13), PRES(14), PRES(15),
   /* Endless encoders. smk37_controls.c maps physical 0..6 to the FM-1
    * application's K1..K4, SELECT, PAGE and PRESETS; 7 is master volume. */
   ENC(0, "K1"), ENC(1, "K2"), ENC(2, "K3"), ENC(3, "K4"), ENC(4, "SELECT"), ENC(5, "PAGE"), ENC(6, "PRESETS"),
   ENC(7, "VOLUME"),
   /* Faders (track volumes, two banks, firmware pickup) and the wheels. */
   { SR_CTL_FADER, 0, SR_MODE_ABSOLUTE, 0, 127, 0, "F1" },
   { SR_CTL_FADER, 1, SR_MODE_ABSOLUTE, 0, 127, 0, "F2" },
   { SR_CTL_FADER, 2, SR_MODE_ABSOLUTE, 0, 127, 0, "F3" },
   { SR_CTL_FADER, 3, SR_MODE_ABSOLUTE, 0, 127, 0, "F4" },
   { SR_CTL_FADER, 4, SR_MODE_ABSOLUTE, 0, 127, 64, "PITCH" },
   { SR_CTL_FADER, 5, SR_MODE_ABSOLUTE, 0, 127, 0, "MOD" },
   { SR_CTL_PEDAL, 0, SR_MODE_ABSOLUTE, 0, 127, 0, "SUSTAIN" },
};
#define CORE_ID "smk37_open"
#define CORE_NAME "SMK37 OPEN"
#define CORE_VERSION "V01"
#define CORE_UPSTREAM "https://github.com/m-vave/smk37_open @ b2d6bcbcd724adb13bcf85e44cfa16bb2c8153a8"
#define CORE_INSTRUMENT "mvave.smk37pro"
#define CORE_MODEL "SMK-37 Pro"
#define CORE_BASE_NOTE 48 /* key 0 = C3 */
#else
static const SynthRefControlInfo kControls[] = {
   /* Panel buttons 0..13 (fm1_controls.buttons), names from the firmware's
    * own workbench (sdk/Simulator.js). */
   BTN(0, "OCT -"), BTN(1, "OCT +"), BTN(2, "FX"), BTN(3, "SEL"), BTN(4, "ENV"), BTN(5, "LFO"), BTN(6, "EDIT"),
   BTN(7, "GLO"), BTN(8, "HOME"), BTN(9, "SAVE"), BTN(10, "ARP"), BTN(11, "SEQ"), BTN(12, "PLAY"), BTN(13, "REC"),
   /* 27 touch keys F3..G5, on / off. */
   KEYS(0), KEYS(1), KEYS(2), KEYS(3), KEYS(4), KEYS(5), KEYS(6), KEYS(7), KEYS(8), KEYS(9),
   KEYS(10), KEYS(11), KEYS(12), KEYS(13), KEYS(14), KEYS(15), KEYS(16), KEYS(17), KEYS(18), KEYS(19),
   KEYS(20), KEYS(21), KEYS(22), KEYS(23), KEYS(24), KEYS(25), KEYS(26),
   ENC(0, "SELECT"), ENC(1, "ALGORITHM"), ENC(2, "KNOB 1"), ENC(3, "KNOB 2"), ENC(4, "KNOB 3"), ENC(5, "KNOB 4"),
   ENC(6, "PRESETS"),
   /* The volume pot (an ADC, 0..1023 on the hardware), as 0..127. */
   { SR_CTL_FADER, 0, SR_MODE_CONTINUOUS, 0, 127, 127, "VOLUME" },
};
#define CORE_ID "fm1_open"
#define CORE_NAME "FM1 OPEN"
#define CORE_VERSION "V14"
#define CORE_UPSTREAM "https://github.com/m-vave/fm1_open @ 4c7496daac04a95ff10d1afdcc8f9ead2dbabc41"
#define CORE_INSTRUMENT "mvave.fm1"
#define CORE_MODEL "FM-1"
#define CORE_BASE_NOTE 53 /* key 0 = F3 */
#endif

static const SynthRefCoreInfo kInfo = {
   sizeof(SynthRefCoreInfo),
   CORE_ID,
   CORE_NAME,
   CORE_VERSION,
   CORE_UPSTREAM,
   "GPL-3.0-only WITH GPL-3.0-linking-exception AND Apache-2.0 AND MIT",
   CORE_INSTRUMENT,
   "M-VAVE",
   CORE_MODEL,
   SR_CORE_SINGLETON,
   FM1_SAMPLE_RATE,
   FM1_BLOCK_FRAMES,
   FM1_CHANNELS,
   FM1_SCREEN_WIDTH,
   FM1_SCREEN_HEIGHT,
   SR_LCD_RGB565,
   (uint32_t)(sizeof kControls / sizeof kControls[0]),
   kControls,
   SR_IMPORT_DX7_VOICE | SR_IMPORT_DX7_BANK | SR_IMPORT_PROJECT | SR_IMPORT_TIMBRE,
   /* magic, version, then four [u32 size][bytes] chunks */
   8u + 4u * 4u + GROOVE_SAMPLE_OFFSET + GROOVE_SAMPLE_BYTES + FILE_BYTES + 4u,
   CORE_BASE_NOTE,
};

static int sCreated;
static int sToken;
static unsigned sKnob = 1023; /* volume pot, 0..1023 */

static const SynthRefCoreInfo* core_info(void) { return &kInfo; }

static void* core_create(void)
{
   if (sCreated)
      return 0; /* globals: one instance per loaded copy */
   sCreated = 1;
   return &sToken;
}

static void core_destroy(void* inst)
{
   (void)inst;
   sCreated = 0;
}

/* SMK-37: puts the firmware's control layer in SMK-37 mode (sim_boot leaves it
 * in FM-1 mode until the first SMK-37 event). A centred pitch wheel sends
 * nothing. */
static void enter_panel_mode(void)
{
#ifdef SR_MVAVE_SMK37
   (void)sim_smk37(SMK37_FADER, 4, 64);
#endif
}

static void set_knob(unsigned level)
{
   sKnob = level > 1023u ? 1023u : level;
   sim_control(3, 0, (int)sKnob);
   enter_panel_mode();
}

static void core_boot(void* inst, uint32_t flags)
{
   (void)inst;
   sim_boot((flags & SR_BOOT_CLEAR) ? 1u : 0u);
   sKnob = 1023;
   enter_panel_mode();
}

static const float* core_render(void* inst)
{
   (void)inst;
   return sim_render();
}

static int core_midi(void* inst, const uint8_t* bytes, uint32_t count)
{
   (void)inst;
   if (!bytes || !count || count > SYSEX_MAX)
      return -1;
   if (count > 3 && (bytes[0] != 0xF0 || bytes[count - 1] != 0xF7))
      return -1;
   memcpy(sim_file(), bytes, count);
   return sim_midi(count);
}

static int core_control(void* inst, uint32_t kind, uint32_t id, int32_t value)
{
   (void)inst;
#ifdef SR_MVAVE_SMK37
   if (kind >= SR_CTL_KINDS || kind == 5)
      return -1;
   return sim_smk37(kind, id, value);
#else
   switch (kind)
   {
   case SR_CTL_BUTTON: if (id >= 14) return -1; sim_control(0, id, value != 0); return 0;
   case SR_CTL_KEY: if (id >= 27) return -1; sim_control(1, id, value > 0); return 0;
   case SR_CTL_ENCODER:
      if (id >= 7 || value < -127 || value > 127) return -1;
      sim_control(2, id, value);
      return 0;
   case SR_CTL_FADER:
      if (id != 0 || value < 0 || value > 127) return -1;
      set_knob(((unsigned)value * 1023u + 63u) / 127u);
      return 0;
   default: return -1;
   }
#endif
}

static void core_release(void* inst)
{
   (void)inst;
   sim_release();
}

static void core_panic(void* inst)
{
   (void)inst;
   sim_panic();
}

static const uint16_t* core_lcd(void* inst, uint32_t* revision)
{
   (void)inst;
   if (revision)
      *revision = sim_frame_revision();
   return sim_pixels();
}

/* ABI layout: one LED per button id, then one per key (then pads). */
static uint32_t core_leds(void* inst, uint8_t* out, uint32_t capacity)
{
   (void)inst;
   const uint8_t* fw = sim_state() + 168; /* the application's 41 LEDs: 14 buttons, 27 keys */
#ifdef SR_MVAVE_SMK37
   /* The SMK-37's 18 buttons light the application's button LEDs through
    * smk37_controls.c's button_map (255 = no LED); its keys have none. */
   static const uint8_t map[18] = { 255, 255, 8, 9, 0, 1, 10, 255, 3, 4, 7, 255, 8, 6, 2, 11, 12, 13 };
   for (uint32_t i = 0; out && i < capacity && i < 18; ++i)
      out[i] = map[i] < 14 ? fw[map[i]] : 0;
   return 18;
#else
   const uint32_t n = capacity < LED_COUNT ? capacity : LED_COUNT;
   if (out)
      memcpy(out, fw, n);
   return LED_COUNT;
#endif
}

/* sim_state(): [0] track, [1] page, then 20 bytes per track from byte 8. */
static const uint8_t* track_view(unsigned track) { return sim_state() + 8 + track * 20; }

static int32_t core_query(void* inst, uint32_t key)
{
   (void)inst;
   const unsigned track = sim_state()[0] % GROOVE_TRACKS;
#ifdef SR_MVAVE_SMK37
   const unsigned hw = sim_smk37_state(), perf = sim_smk37_instruments();
#endif
   switch (key)
   {
   case SR_Q_TRACK: return (int32_t)track;
   case SR_Q_TRACK_COUNT: return GROOVE_TRACKS;
   case SR_Q_PROGRAM:
      return groove_source_type(track) == GROOVE_DX7 ? (int32_t)track_view(track)[1] : -1;
   case SR_Q_PROGRAM_COUNT: return FM1_FM_PROGRAMS;
   case SR_Q_FACTORY_COUNT:
   {
      int32_t n = 0;
      while (n < GROOVE_FACTORY_PROJECTS && sim_factory_name((unsigned)n))
         ++n;
      return n;
   }
   case SR_Q_PLAYING: return (int32_t)track_view(track)[6];
   case SR_Q_TEMPO_X10:
   {
      const uint8_t* v = track_view(track);
      return (int32_t)(v[10] | (v[11] << 8));
   }
#ifdef SR_MVAVE_SMK37
   case SR_Q_LED_COUNT: return 18;
#else
   case SR_Q_LED_COUNT: return LED_COUNT;
#endif
   case SR_Q_EDIT_REVISION: return (int32_t)(sim_edit_revision() + sim_project_revision() + sim_flash_revision());
#ifdef SR_MVAVE_SMK37
   case SR_Q_SHIFT: return (int32_t)((hw >> 3) & 1u);
   case SR_Q_FADER_BANK: return (int32_t)(hw & 1u);
   case SR_Q_PICKUP_MASK: return (int32_t)((hw >> 8) & 0xFu);
   case SR_Q_MASTER: return (int32_t)(hw >> 16);
   case SR_Q_KEY_CHANNEL: return (int32_t)((perf >> 6) & 15u);
   case SR_Q_PAD_CHANNEL: return (int32_t)((perf >> 10) & 15u);
   case SR_Q_PAD_STEPS: return (int32_t)((hw >> 1) & 1u);
   case SR_Q_KEY_OCTAVE: return (int32_t)((perf >> 14) & 7u) - 2;
#else
   case SR_Q_MASTER: return (int32_t)sKnob;
   case SR_Q_KEY_CHANNEL: return (int32_t)(groove_midi_input_channel(track) & 15u);
#endif
   default: return SR_QUERY_NONE;
   }
}

static uint32_t put_text(char* out, uint32_t capacity, const char* s, uint32_t max)
{
   if (!out || !capacity)
      return 0;
   uint32_t n = 0;
   while (s && n < max && n + 1 < capacity && s[n])
   {
      out[n] = s[n];
      ++n;
   }
   while (n > 0 && out[n - 1] == ' ')
      --n;
   out[n] = 0;
   return n;
}

static uint32_t core_text(void* inst, uint32_t which, uint32_t index, char* out, uint32_t capacity)
{
   (void)inst;
   static const char* const kSources[GROOVE_SOURCES] = { "DX7", "303", "808", "PSG", "PLUCK", "SAMPLE" };
   switch (which)
   {
   case SR_TEXT_PROJECT: return put_text(out, capacity, sim_project_name(), 12);
   case SR_TEXT_PROGRAM:
   {
      if (index >= FM1_FM_PROGRAMS)
         return 0;
      uint8_t patch[156];
      groove_fm_program(patch, index);
      char name[11];
      for (int i = 0; i < 10; ++i)
      {
         const uint8_t c = patch[145 + i];
         name[i] = c >= 32 && c < 127 ? (char)c : ' ';
      }
      name[10] = 0;
      return put_text(out, capacity, name, 10);
   }
   case SR_TEXT_FACTORY: return index < GROOVE_FACTORY_PROJECTS ? put_text(out, capacity, sim_factory_name(index), 32) : 0;
#ifdef SR_MVAVE_SMK37
   case SR_TEXT_PAD_LABEL: return index < 16 ? put_text(out, capacity, sim_smk37_pad_labels() + index * 8, 7) : 0;
#endif
   case SR_TEXT_TRACK_SOURCE:
   {
      if (index >= GROOVE_TRACKS)
         return 0;
      const unsigned type = groove_source_type(index);
      return put_text(out, capacity, type < GROOVE_SOURCES ? kSources[type] : "", 8);
   }
   default: return 0;
   }
}

static uint8_t* put_u32(uint8_t* p, uint32_t v)
{
   memcpy(p, &v, 4);
   return p + 4;
}

static uint32_t get_u32(const uint8_t* p)
{
   uint32_t v;
   memcpy(&v, p, 4);
   return v;
}

static unsigned master_level(void)
{
#ifdef SR_MVAVE_SMK37
   return sim_smk37_state() >> 16;
#else
   return sKnob;
#endif
}

/* State: magic, version, then [u32 size][bytes] chunks:
 *   1. user storage (projects, DX7 library, settings: Flash below the samples)
 *   2. sample bank, empty when it is still the factory bank
 *   3. working project (the firmware's own project file)
 *   4. master level (u32, 0..1023) */
static uint32_t core_save_state(void* inst, uint8_t* dst, uint32_t capacity)
{
   (void)inst;
   const uint8_t* flash = sim_flash();
   unsigned factoryBytes = sim_factory_size();
   const uint8_t* factory = sim_factory();
   const int samplesFactory = factoryBytes <= GROOVE_SAMPLE_BYTES &&
                              memcmp(flash + GROOVE_SAMPLE_OFFSET, factory, factoryBytes) == 0;
   const uint32_t sampleBytes = samplesFactory ? 0u : GROOVE_SAMPLE_BYTES;
   /* groove_project_export returns 0 and always writes the tuned size. */
   if (sim_export() != 0)
      return 0;
   const uint32_t projectBytes = FILE_BYTES;
   const uint32_t need = 8u + 4u * 4u + GROOVE_SAMPLE_OFFSET + sampleBytes + projectBytes + 4u;
   if (!dst)
      return need;
   if (capacity < need)
      return 0;
   uint8_t* p = put_u32(dst, STATE_MAGIC);
   p = put_u32(p, STATE_VERSION);
   p = put_u32(p, GROOVE_SAMPLE_OFFSET);
   memcpy(p, flash, GROOVE_SAMPLE_OFFSET);
   p += GROOVE_SAMPLE_OFFSET;
   p = put_u32(p, sampleBytes);
   memcpy(p, flash + GROOVE_SAMPLE_OFFSET, sampleBytes);
   p += sampleBytes;
   p = put_u32(p, projectBytes);
   memcpy(p, sim_file(), projectBytes);
   p += projectBytes;
   p = put_u32(p, 4u);
   p = put_u32(p, master_level());
   return (uint32_t)(p - dst);
}

static int core_load_state(void* inst, const uint8_t* src, uint32_t bytes)
{
   (void)inst;
   if (!src || bytes < 8u + 4u * 4u || get_u32(src) != STATE_MAGIC || get_u32(src + 4) != STATE_VERSION)
      return -1;
   const uint8_t* p = src + 8;
   const uint8_t* end = src + bytes;
   const uint32_t flashBytes = get_u32(p);
   if (flashBytes != GROOVE_SAMPLE_OFFSET || (uint32_t)(end - p) < 4u + flashBytes + 4u)
      return -2;
   const uint8_t* flashData = p + 4;
   p += 4 + flashBytes;
   const uint32_t sampleBytes = get_u32(p);
   if ((sampleBytes != 0 && sampleBytes != GROOVE_SAMPLE_BYTES) || (uint32_t)(end - p) < 4u + sampleBytes + 4u)
      return -3;
   const uint8_t* sampleData = p + 4;
   p += 4 + sampleBytes;
   const uint32_t projectBytes = get_u32(p);
   if (projectBytes == 0 || projectBytes > FILE_BYTES || (uint32_t)(end - p) < 4u + projectBytes + 8u)
      return -4;
   const uint8_t* projectData = p + 4;
   p += 4 + projectBytes;
   const uint32_t master = get_u32(p) == 4u ? get_u32(p + 4) : 1023u;

   /* Factory storage first (samples), then the saved user storage over it,
    * then a warm boot so the firmware reads its storage like a power-up. */
   sim_boot(1);
   uint8_t* flash = sim_flash();
   memcpy(flash, flashData, flashBytes);
   if (sampleBytes)
      memcpy(flash + GROOVE_SAMPLE_OFFSET, sampleData, sampleBytes);
   sim_boot(0);
   memcpy(sim_file(), projectData, projectBytes);
   const int imported = sim_import(projectBytes);
   enter_panel_mode();
   set_knob(master);
   return imported != 0 ? -5 : 0;
}

static int dx7_checksum_ok(const uint8_t* data, uint32_t n)
{
   uint32_t sum = 0;
   for (uint32_t i = 0; i < n; ++i)
      sum += data[i];
   return ((128u - (sum & 127u)) & 127u) == data[n];
}

static int core_import(void* inst, uint32_t kind, uint32_t slot, const uint8_t* data, uint32_t bytes)
{
   (void)inst;
   if (!data)
      return -1;
   const unsigned track = sim_state()[0] % GROOVE_TRACKS;
   switch (kind)
   {
   case SR_IMPORT_DX7_BANK:
      /* F0 43 0n 09 20 00, 4096 bytes of 32 packed voices, checksum, F7 */
      if (bytes != 4104 || slot >= 4 || data[0] != 0xF0 || data[1] != 0x43 || (data[2] & 0xF0) != 0 ||
          data[3] != 0x09 || data[4] != 0x20 || data[5] != 0x00 || data[4103] != 0xF7 ||
          !dx7_checksum_ok(data + 6, 4096))
         return -2;
      return groove_fm_library_write(slot, data + 6) ? -3 : 0;
   case SR_IMPORT_DX7_VOICE:
   {
      /* F0 43 0n 00 01 1B, 155 bytes of one unpacked voice, checksum, F7 */
      if (bytes != 163 || data[0] != 0xF0 || data[1] != 0x43 || (data[2] & 0xF0) != 0 || data[3] != 0x00 ||
          data[4] != 0x01 || data[5] != 0x1B || data[162] != 0xF7 || !dx7_checksum_ok(data + 6, 155))
         return -2;
      if (groove_source_type(track) != GROOVE_DX7)
         return -4;
      uint8_t patch[156];
      memcpy(patch, data + 6, 155);
      patch[155] = 0x3F; /* all six operators on */
      return groove_fm_import(track, patch) ? -3 : 0;
   }
   case SR_IMPORT_PROJECT:
      if (bytes == 0 || bytes > FILE_BYTES)
         return -2;
      memcpy(sim_file(), data, bytes);
      return sim_import(bytes) != 0 ? -3 : 0;
   case SR_IMPORT_TIMBRE:
      if (bytes != sizeof(struct groove_preset))
         return -2;
      memcpy(sim_file(), data, bytes);
      return sim_timbre_import() ? -3 : 0;
   default:
      return -1;
   }
}

static uint32_t core_export_project(void* inst, uint8_t* dst, uint32_t capacity)
{
   (void)inst;
   if (!dst)
      return FILE_BYTES;
   if (capacity < FILE_BYTES || sim_export() != 0)
      return 0;
   memcpy(dst, sim_file(), FILE_BYTES);
   return FILE_BYTES;
}

static int core_load_factory(void* inst, uint32_t index)
{
   (void)inst;
   if (index >= GROOVE_FACTORY_PROJECTS || sim_factory_project(index))
      return -1;
   return sim_import(FILE_BYTES) != 0 ? -2 : 0;
}

/* A DX7 library program onto the selected track, as the workbench's library
 * browser does (Simulator.c sim_library_load, which only SMK37 OPEN exports). */
static int core_load_program(void* inst, uint32_t program)
{
   (void)inst;
   uint8_t view[8];
   groove_screen_snapshot(view);
   const unsigned track = view[0];
   if (track >= GROOVE_TRACKS || program >= FM1_FM_PROGRAMS || groove_source_type(track) != GROOVE_DX7)
      return -1;
   struct groove_preset tone;
   int r = groove_timbre_export(track, (uint8_t*)&tone, sizeof tone);
   if (r)
      return r;
   tone.program = (uint8_t)program;
   tone.modified = 0;
   memset(&tone.controls, 0, sizeof tone.controls);
   tone.controls.envelope[2] = 100;
   groove_fm_program(tone.data, program);
   return groove_timbre_import(track, (uint8_t*)&tone, sizeof tone);
}

static const SynthRefCoreApi kApi = {
   SYNTHREF_CORE_ABI_VERSION,
   sizeof(SynthRefCoreApi),
   core_info,
   core_create,
   core_destroy,
   core_boot,
   core_render,
   core_midi,
   core_control,
   core_release,
   core_panic,
   core_lcd,
   core_leds,
   core_query,
   core_text,
   core_save_state,
   core_load_state,
   core_import,
   core_export_project,
   core_load_factory,
   core_load_program,
};

CORE_EXPORT const SynthRefCoreApi* synthref_core_entry(uint32_t hostAbiVersion)
{
   return hostAbiVersion == SYNTHREF_CORE_ABI_VERSION ? &kApi : 0;
}
