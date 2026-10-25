/* SPDX-License-Identifier: GPL-3.0-only */
/* Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
 *
 * Synth REF core: M-VAVE SMK-37 Pro running SLOOP SMK ("Felucca SMK", the
 * community GPL-3.0 firmware: github.com/ChanceTheMaker/sloop-smk, vendored
 * unmodified in upstream/, see UPSTREAM.md). The firmware's own browser engine
 * (web/audio/sloop-smk-engine.c, which #includes the SMK runtime and the
 * SLOOP 2.8 core) is included here as one translation unit, exactly as its
 * web build compiles it; this file only maps it onto the Synth REF core ABI.
 * Built without the "all rights reserved" Felucca assets: the knob-card icons
 * on the screen are blank (generated/felucca_icons.h, a blank atlas).
 *
 * Globals throughout: SR_CORE_SINGLETON, one DLL copy per instance. */
#include "upstream/web/audio/sloop-smk-engine.c"

#include "../../SynthRefCoreApi.h"

#define CORE_EXPORT __declspec(dllexport)

enum
{
   SR_STATE_MAGIC = 0x314C5253u, /* "SRL1" */
   SR_STATE_VERSION = 1u,
   LED_BUTTONS = 18,
   LED_KEYS = 37,
   LED_PADS = 16,
   LED_COUNT = LED_BUTTONS + LED_KEYS + LED_PADS,
   SCREEN_TICK_FRAMES = FS / 30 /* the firmware's UI tick + redraw, 30 Hz */
};

#define BTN(n, name) { SR_CTL_BUTTON, n, SR_MODE_SWITCH, 0, 1, 0, name }
#define ENC(n, name) { SR_CTL_ENCODER, n, SR_MODE_RELATIVE, -32, 32, 0, name }
#define KEYS(n) { SR_CTL_KEY, n, SR_MODE_VELOCITY, 0, 127, 0, 0 }
#define PADS(n) { SR_CTL_PAD, n, SR_MODE_VELOCITY, 0, 127, 0, 0 }
static const SynthRefControlInfo kControls[] = {
   /* fw_event kind 0 ids, names from the firmware's own workbench. */
   BTN(0, "KNOB BANK"), BTN(1, "FADER BANK"), BTN(2, "<"), BTN(3, ">"), BTN(4, "OCT -"), BTN(5, "OCT +"),
   BTN(6, "ARP"), BTN(7, "NOTE REPEAT"), BTN(8, "SCALE"), BTN(9, "CHORD"), BTN(10, "GLO"), BTN(11, "BT / EDIT"),
   BTN(12, "PATCH / HOME"), BTN(13, "PARA / SAVE"), BTN(14, "FX"), BTN(15, "SEQ"), BTN(16, "PLAY / STOP"), BTN(17, "REC"),
   KEYS(0), KEYS(1), KEYS(2), KEYS(3), KEYS(4), KEYS(5), KEYS(6), KEYS(7), KEYS(8), KEYS(9),
   KEYS(10), KEYS(11), KEYS(12), KEYS(13), KEYS(14), KEYS(15), KEYS(16), KEYS(17), KEYS(18), KEYS(19),
   KEYS(20), KEYS(21), KEYS(22), KEYS(23), KEYS(24), KEYS(25), KEYS(26), KEYS(27), KEYS(28), KEYS(29),
   KEYS(30), KEYS(31), KEYS(32), KEYS(33), KEYS(34), KEYS(35), KEYS(36),
   PADS(0), PADS(1), PADS(2), PADS(3), PADS(4), PADS(5), PADS(6), PADS(7),
   PADS(8), PADS(9), PADS(10), PADS(11), PADS(12), PADS(13), PADS(14), PADS(15),
   ENC(0, "VOLUME"), ENC(1, "TEMPO"), ENC(2, "PRESETS"), ENC(3, "TRACK"), ENC(4, "K1"), ENC(5, "K2"), ENC(6, "K3"),
   ENC(7, "K4"),
   { SR_CTL_FADER, 0, SR_MODE_ABSOLUTE, 0, 127, 0, "F1" },
   { SR_CTL_FADER, 1, SR_MODE_ABSOLUTE, 0, 127, 0, "F2" },
   { SR_CTL_FADER, 2, SR_MODE_ABSOLUTE, 0, 127, 0, "F3" },
   { SR_CTL_FADER, 3, SR_MODE_ABSOLUTE, 0, 127, 0, "F4" },
   { SR_CTL_FADER, 4, SR_MODE_ABSOLUTE, 0, 127, 64, "PITCH" },
   { SR_CTL_FADER, 5, SR_MODE_ABSOLUTE, 0, 127, 0, "MOD" },
   { SR_CTL_PEDAL, 0, SR_MODE_ABSOLUTE, 0, 127, 0, "SUSTAIN" },
};

static const SynthRefCoreInfo kInfo = {
   sizeof(SynthRefCoreInfo),
   "sloop_smk",
   "SLOOP SMK",
   "2.6 (SLOOP 2.8)",
   "https://github.com/ChanceTheMaker/sloop-smk @ 08c823081d9ab394077767a0604df7799ad449b0",
   "GPL-3.0-only AND Apache-2.0 AND MIT AND OFL-1.1 AND CC0-1.0",
   "mvave.smk37pro",
   "M-VAVE",
   "SMK-37 Pro",
   SR_CORE_SINGLETON,
   FS,
   CTL,
   2,
   240,
   240,
   SR_LCD_RGB565,
   (uint32_t)(sizeof kControls / sizeof kControls[0]),
   kControls,
   0,
   16u + (uint32_t)sizeof(session_v2_t),
   48, /* key 0 = C3 */
};

static int sCreated;
static int sToken;
static float sOut[CTL * 2];
static uint16_t sScreen[240 * 240];
static uint32_t sScreenRevision, sScreenHash;
static unsigned sFramesSinceTick = SCREEN_TICK_FRAMES;
static uint32_t sEdits; /* panel edits since boot: the state export trigger */

static const SynthRefCoreInfo* core_info(void) { return &kInfo; }

static void* core_create(void)
{
   if (sCreated)
      return 0;
   sCreated = 1;
   return &sToken;
}

static void core_destroy(void* inst)
{
   (void)inst;
   sCreated = 0;
}

static const float* render_block(void)
{
   const int32_t* p = synth_render();
   for (unsigned i = 0; i < CTL * 2; ++i)
      sOut[i] = (float)p[i] * (1.0f / 32768.0f);
   sFramesSinceTick += CTL;
   return sOut;
}

static void core_boot(void* inst, uint32_t flags)
{
   (void)inst;
   (void)flags; /* a fresh DLL copy is always factory state */
   synth_init();
   render_block(); /* settles the voices, as the workbench does */
}

static const float* core_render(void* inst)
{
   (void)inst;
   return render_block();
}

static int core_midi(void* inst, const uint8_t* bytes, uint32_t count)
{
   (void)inst;
   if (!bytes || count < 1 || count > 3 || bytes[0] < 0x80)
      return -1;
   synth_midi(bytes[0], count > 1 ? bytes[1] : 0, count > 2 ? bytes[2] : 0);
   return 0;
}

static int core_control(void* inst, uint32_t kind, uint32_t id, int32_t value)
{
   (void)inst;
   switch (kind)
   {
   case SR_CTL_BUTTON: if (id >= 18) return -1; fw_event(0, id, value != 0); break;
   case SR_CTL_ENCODER: if (id >= 8) return -1; fw_event(1, id, value < -32 ? -32 : value > 32 ? 32 : value); break;
   case SR_CTL_KEY: if (id >= 37) return -1; fw_event(2, id, value); break;
   case SR_CTL_PAD: if (id >= 16) return -1; fw_event(3, id, value); break;
   case SR_CTL_FADER:
      if (value < 0 || value > 127) return -1;
      if (id < 4) fw_event(4, id, (value * 1023 + 63) / 127);
      else if (id == 4) fw_event(5, 0, value == 64 ? 0 : (value - 64) * 128 + (value > 64 ? 127 : 0));
      else if (id == 5) fw_event(5, 1, value);
      else return -1;
      break;
   case SR_CTL_PEDAL: fw_event(6, 0, value >= 64); break;
   default: return -1;
   }
   if (kind != SR_CTL_KEY && kind != SR_CTL_PAD)
      ++sEdits;
   return 0;
}

static void core_release(void* inst)
{
   (void)inst;
   fw_release();
}

static void core_panic(void* inst)
{
   (void)inst;
   fw_release();
   for (unsigned ch = 0; ch < 16; ++ch)
      synth_midi(0xB0u | ch, 123, 0);
}

/* The firmware draws on its UI tick; run it at 30 Hz of audio, byte-swap its
 * big-endian frame into RGB565 and bump the revision only when it changed. */
static const uint16_t* core_lcd(void* inst, uint32_t* revision)
{
   (void)inst;
   if (sFramesSinceTick >= SCREEN_TICK_FRAMES)
   {
      sFramesSinceTick = 0;
      const uint16_t* s = fw_screen();
      uint32_t h = 2166136261u;
      for (unsigned i = 0; i < 240 * 240; ++i)
      {
         const uint16_t v = (uint16_t)((s[i] >> 8) | (s[i] << 8));
         sScreen[i] = v;
         h = (h ^ v) * 16777619u;
      }
      if (h != sScreenHash)
      {
         sScreenHash = h;
         ++sScreenRevision;
      }
   }
   if (revision)
      *revision = sScreenRevision;
   return sScreen;
}

/* Buttons 0..17 (bits of status[0]), then 37 keys (none lit), then 16 pads. */
static uint32_t core_leds(void* inst, uint8_t* out, uint32_t capacity)
{
   (void)inst;
   const uint32_t* st = fw_status();
   for (uint32_t i = 0; out && i < capacity && i < LED_COUNT; ++i)
   {
      if (i < LED_BUTTONS)
         out[i] = (uint8_t)((st[0] >> i) & 1u);
      else if (i < LED_BUTTONS + LED_KEYS)
         out[i] = 0;
      else
         out[i] = (uint8_t)((st[3] >> (i - LED_BUTTONS - LED_KEYS)) & 1u);
   }
   return LED_COUNT;
}

static int32_t core_query(void* inst, uint32_t key)
{
   (void)inst;
   const uint32_t* st = fw_status();
   switch (key)
   {
   case SR_Q_TRACK: return (int32_t)st[5];
   case SR_Q_TRACK_COUNT: return NPART + 1;
   case SR_Q_PLAYING: return (int32_t)st[6];
   case SR_Q_MASTER: return (int32_t)(st[11] * 1023u / 4096u);
   case SR_Q_FADER_BANK: return (int32_t)st[8];
   case SR_Q_KEY_OCTAVE: return (int32_t)st[9] - 3;
   case SR_Q_KEY_CHANNEL:
   {
      /* The selected part's MIDI IN channel (GLO > MIDI), drums for the kit. */
      const unsigned sel = song.sel < NPART ? song.sel : 3u;
      const unsigned ch = midi_set[sel];
      return ch >= 1 && ch <= 16 ? (int32_t)ch - 1 : 0;
   }
   case SR_Q_LED_COUNT: return LED_COUNT;
   case SR_Q_EDIT_REVISION: return (int32_t)sEdits;
   default: return SR_QUERY_NONE;
   }
}

static uint32_t core_text(void* inst, uint32_t which, uint32_t index, char* out, uint32_t capacity)
{
   (void)inst;
   (void)which;
   (void)index;
   if (out && capacity)
      out[0] = 0;
   return 0;
}

static void put_u32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get_u32(const uint8_t* p)
{
   uint32_t v;
   memcpy(&v, p, 4);
   return v;
}

/* State: "SRL1", version, size, reserved, then the firmware's own session
 * snapshot (projects, slots, user presets, arrangement, FM6 patches,
 * settings, MIDI learn and the user sample memory) with its CRCs. */
static uint32_t core_save_state(void* inst, uint8_t* dst, uint32_t capacity)
{
   (void)inst;
   const uint32_t n = fw_snapshot_size();
   if (!dst)
      return 16u + n;
   if (capacity < 16u + n)
      return 0;
   const uint8_t* snap = fw_snapshot();
   put_u32(dst, SR_STATE_MAGIC);
   put_u32(dst + 4, SR_STATE_VERSION);
   put_u32(dst + 8, n);
   put_u32(dst + 12, 0);
   memcpy(dst + 16, snap, n);
   return 16u + n;
}

static int core_load_state(void* inst, const uint8_t* src, uint32_t bytes)
{
   (void)inst;
   if (!src || bytes < 16u || get_u32(src) != SR_STATE_MAGIC || get_u32(src + 4) != SR_STATE_VERSION)
      return -1;
   const uint32_t n = get_u32(src + 8);
   if (n != fw_snapshot_size() || bytes != 16u + n)
      return -2;
   synth_init();
   render_block();
   uint8_t* snap = fw_snapshot(); /* the firmware's own buffer, restored from */
   memcpy(snap, src + 16, n);
   const int r = fw_restore();
   ++sEdits;
   return r ? -3 : 0;
}

static int core_import(void* inst, uint32_t kind, uint32_t slot, const uint8_t* data, uint32_t bytes)
{
   (void)inst;
   (void)kind;
   (void)slot;
   (void)data;
   (void)bytes;
   return -1;
}

static uint32_t core_export_project(void* inst, uint8_t* dst, uint32_t capacity)
{
   (void)inst;
   (void)dst;
   (void)capacity;
   return 0;
}

static int core_load_factory(void* inst, uint32_t index)
{
   (void)inst;
   (void)index;
   return -1;
}

static int core_load_program(void* inst, uint32_t program)
{
   (void)inst;
   (void)program;
   return -1;
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
