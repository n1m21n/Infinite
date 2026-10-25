/* SPDX-License-Identifier: GPL-3.0-only */
/* Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
 *
 * Synth REF ROM core (synthref_rom.dll): runs an instrument firmware exactly
 * as its authors distribute it for their browser simulators, a WebAssembly
 * module, in the wasm3 interpreter (rom/wasm3, MIT). Nothing of the firmware
 * is rebuilt or recreated: its code, screens, fonts, icons, samples and
 * presets are the ones in the file. A ROM is sandboxed by construction: it
 * imports nothing, so it can only compute, and Synth REF reads its outputs.
 *
 * A "family" is the API a group of firmwares exports. Adding a ROM of a known
 * family needs no rebuild (drop the .wasm in Documents\Infinite-Turbo\
 * SynthRef\roms). Families:
 *   - SLOOP SMK web engine (synth_*, fw_*): sloop-smk's smk-engine.wasm.
 *
 * Reentrant: every instance has its own wasm3 runtime and linear memory. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../SynthRefCoreApi.h"
#include "wasm3/wasm3.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define CORE_EXPORT __declspec(dllexport)

enum
{
   FAMILY_NONE = 0,
   FAMILY_SLOOP_SMK = 1,
   ROM_MAX_BYTES = 64 << 20,
   WASM_STACK = 1 << 20,
   SLOOP_RATE = 44100,
   SLOOP_BLOCK = 32,
   SLOOP_LEDS = 18 + 37 + 16,
   SCREEN_TICK_FRAMES = SLOOP_RATE / 30
};

#define BTN(n, name) { SR_CTL_BUTTON, n, SR_MODE_SWITCH, 0, 1, 0, name }
#define ENC(n, name) { SR_CTL_ENCODER, n, SR_MODE_RELATIVE, -32, 32, 0, name }
#define KEYS(n) { SR_CTL_KEY, n, SR_MODE_VELOCITY, 0, 127, 0, 0 }
#define PADS(n) { SR_CTL_PAD, n, SR_MODE_VELOCITY, 0, 127, 0, 0 }
/* SLOOP SMK panel, as its web engine's fw_event takes it. */
static const SynthRefControlInfo kSloopControls[] = {
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

typedef struct Rom
{
   IM3Environment env;
   IM3Runtime rt;
   uint8_t* bytes;
   int family;
   int faulted;
   IM3Function init, midi, render, event, release, screen, status, snapshot, snapshotSize, restore;
   SynthRefCoreInfo info;
   char name[96];
   float out[SLOOP_BLOCK * 2];
   uint16_t lcd[240 * 240];
   uint32_t lcdRevision, lcdHash;
   unsigned framesSinceTick;
   uint32_t edits;
} Rom;

static const SynthRefCoreInfo kHostInfo = {
   sizeof(SynthRefCoreInfo), "rom", "ROM", "1", "https://github.com/wasm3/wasm3 @ v0.9.0",
   "GPL-3.0-only AND MIT", "rom", "", "", 0, SLOOP_RATE, SLOOP_BLOCK, 2, 0, 0, SR_LCD_NONE, 0, 0, 0, 0, 48,
};

static uint8_t* mem(Rom* r, uint32_t* size)
{
   uint32_t s = 0;
   uint8_t* p = m3_GetMemory(r->rt, &s, 0);
   if (size)
      *size = s;
   return p;
}

/* A pointer the ROM returned, checked against its memory. */
static uint8_t* at(Rom* r, uint32_t offset, uint32_t bytes)
{
   uint32_t size = 0;
   uint8_t* base = mem(r, &size);
   return base && offset <= size && bytes <= size - offset ? base + offset : 0;
}

static int ok(Rom* r, M3Result res)
{
   if (res)
      r->faulted = 1; /* a trap: the ROM stays silent from here, never the app */
   return !res;
}

static uint32_t call_u32(Rom* r, IM3Function f)
{
   uint32_t v = 0;
   if (!f || r->faulted || !ok(r, m3_CallV(f)) || !ok(r, m3_GetResultsV(f, &v)))
      return 0;
   return v;
}

static void call3(Rom* r, IM3Function f, int32_t a, int32_t b, int32_t c)
{
   if (f && !r->faulted)
      ok(r, m3_CallV(f, a, b, c));
}

static const SynthRefCoreInfo* core_info(void) { return &kHostInfo; }
static void* core_create(void) { return 0; } /* ROMs come through create_rom */

static void core_destroy(void* inst)
{
   Rom* r = (Rom*)inst;
   if (!r)
      return;
   if (r->rt)
      m3_FreeRuntime(r->rt);
   if (r->env)
      m3_FreeEnvironment(r->env);
   free(r->bytes);
   free(r);
}

static void set_error(char* error, uint32_t capacity, const char* a, const char* b)
{
   if (error && capacity)
      snprintf(error, capacity, "%s%s", a, b ? b : "");
}

static void* core_create_rom(const char* utf8Path, char* error, uint32_t errorCapacity)
{
   wchar_t wpath[1024];
   if (!utf8Path || !MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, wpath, 1024))
   {
      set_error(error, errorCapacity, "bad ROM path", 0);
      return 0;
   }
   FILE* f = _wfopen(wpath, L"rb");
   if (!f)
   {
      set_error(error, errorCapacity, "cannot open ", utf8Path);
      return 0;
   }
   fseek(f, 0, SEEK_END);
   const long n = ftell(f);
   fseek(f, 0, SEEK_SET);
   Rom* r = (Rom*)calloc(1, sizeof(Rom));
   if (!r || n <= 8 || n > ROM_MAX_BYTES || !(r->bytes = (uint8_t*)malloc((size_t)n)) || fread(r->bytes, 1, (size_t)n, f) != (size_t)n)
   {
      fclose(f);
      set_error(error, errorCapacity, "not a ROM (empty, over 64 MB or unreadable): ", utf8Path);
      core_destroy(r);
      return 0;
   }
   fclose(f);
   IM3Module mod = 0;
   M3Result res = 0;
   r->env = m3_NewEnvironment();
   r->rt = r->env ? m3_NewRuntime(r->env, WASM_STACK, 0) : 0;
   if (!r->rt || (res = m3_ParseModule(r->env, &mod, r->bytes, (uint32_t)n)) || (res = m3_LoadModule(r->rt, mod)) ||
       (res = m3_CompileModule(mod)))
   {
      set_error(error, errorCapacity, "not a WebAssembly ROM: ", res ? res : "out of memory");
      core_destroy(r);
      return 0;
   }
   /* Family by exports. */
#define FIND(field, name) m3_FindFunction(&r->field, r->rt, name)
   if (!FIND(init, "synth_init") && !FIND(midi, "synth_midi") && !FIND(render, "synth_render") && !FIND(event, "fw_event") &&
       !FIND(screen, "fw_screen") && !FIND(status, "fw_status"))
   {
      r->family = FAMILY_SLOOP_SMK;
      FIND(release, "fw_release");
      FIND(snapshot, "fw_snapshot");
      FIND(snapshotSize, "fw_snapshot_size");
      FIND(restore, "fw_restore");
   }
#undef FIND
   if (r->family == FAMILY_NONE)
   {
      set_error(error, errorCapacity, "unknown ROM family (its exports match no Synth REF family): ", utf8Path);
      core_destroy(r);
      return 0;
   }
   const char* base = strrchr(utf8Path, '\\');
   const char* base2 = strrchr(utf8Path, '/');
   base = base2 > base ? base2 : base;
   snprintf(r->name, sizeof r->name, "%s", base ? base + 1 : utf8Path);
   r->info = kHostInfo;
   r->info.coreId = "rom";
   r->info.coreName = r->name;
   r->info.coreVersion = "ROM (SLOOP SMK web engine)";
   r->info.upstream = "the ROM file as distributed by its authors";
   r->info.license = "as the ROM's authors distribute it";
   r->info.instrumentId = "mvave.smk37pro";
   r->info.manufacturer = "M-VAVE";
   r->info.model = "SMK-37 Pro";
   r->info.lcdWidth = r->info.lcdHeight = 240;
   r->info.lcdFormat = SR_LCD_RGB565;
   r->info.controlCount = (uint32_t)(sizeof kSloopControls / sizeof kSloopControls[0]);
   r->info.controls = kSloopControls;
   r->info.stateBytesMax = 16u + 4u * 1024u * 1024u;
   r->framesSinceTick = SCREEN_TICK_FRAMES;
   return r;
}

static const SynthRefCoreInfo* core_instance_info(void* inst) { return inst ? &((Rom*)inst)->info : &kHostInfo; }

static const float* render_block(Rom* r)
{
   const uint32_t p = call_u32(r, r->render);
   const int32_t* s = (const int32_t*)at(r, p, SLOOP_BLOCK * 2 * 4);
   for (unsigned i = 0; i < SLOOP_BLOCK * 2; ++i)
      r->out[i] = s ? (float)s[i] * (1.0f / 32768.0f) : 0.0f;
   r->framesSinceTick += SLOOP_BLOCK;
   return r->out;
}

static void core_boot(void* inst, uint32_t flags)
{
   (void)flags;
   Rom* r = (Rom*)inst;
   if (r->init && !r->faulted)
      ok(r, m3_CallV(r->init));
   render_block(r);
}

static const float* core_render(void* inst) { return render_block((Rom*)inst); }

static int core_midi(void* inst, const uint8_t* b, uint32_t n)
{
   Rom* r = (Rom*)inst;
   if (!b || n < 1 || n > 3 || b[0] < 0x80)
      return -1;
   call3(r, r->midi, b[0], n > 1 ? b[1] : 0, n > 2 ? b[2] : 0);
   return r->faulted ? -2 : 0;
}

static int core_control(void* inst, uint32_t kind, uint32_t id, int32_t v)
{
   Rom* r = (Rom*)inst;
   switch (kind)
   {
   case SR_CTL_BUTTON: if (id >= 18) return -1; call3(r, r->event, 0, (int32_t)id, v != 0); break;
   case SR_CTL_ENCODER: if (id >= 8) return -1; call3(r, r->event, 1, (int32_t)id, v < -32 ? -32 : v > 32 ? 32 : v); break;
   case SR_CTL_KEY: if (id >= 37) return -1; call3(r, r->event, 2, (int32_t)id, v); break;
   case SR_CTL_PAD: if (id >= 16) return -1; call3(r, r->event, 3, (int32_t)id, v); break;
   case SR_CTL_FADER:
      if (v < 0 || v > 127) return -1;
      if (id < 4) call3(r, r->event, 4, (int32_t)id, (v * 1023 + 63) / 127);
      else if (id == 4) call3(r, r->event, 5, 0, v == 64 ? 0 : (v - 64) * 128 + (v > 64 ? 127 : 0));
      else if (id == 5) call3(r, r->event, 5, 1, v);
      else return -1;
      break;
   case SR_CTL_PEDAL: call3(r, r->event, 6, 0, v >= 64); break;
   default: return -1;
   }
   if (kind != SR_CTL_KEY && kind != SR_CTL_PAD)
      ++r->edits;
   return r->faulted ? -2 : 0;
}

static void core_release(void* inst)
{
   Rom* r = (Rom*)inst;
   if (r->release && !r->faulted)
      ok(r, m3_CallV(r->release));
}

static void core_panic(void* inst)
{
   Rom* r = (Rom*)inst;
   core_release(r);
   for (int ch = 0; ch < 16; ++ch)
      call3(r, r->midi, 0xB0 | ch, 123, 0);
}

/* The ROM draws on its UI tick: run it at 30 Hz of audio, byte-swap its
 * big-endian frame into RGB565, new revision only when it changed. */
static const uint16_t* core_lcd(void* inst, uint32_t* revision)
{
   Rom* r = (Rom*)inst;
   if (r->framesSinceTick >= SCREEN_TICK_FRAMES)
   {
      r->framesSinceTick = 0;
      const uint16_t* s = (const uint16_t*)at(r, call_u32(r, r->screen), 240 * 240 * 2);
      if (s)
      {
         uint32_t h = 2166136261u;
         for (unsigned i = 0; i < 240 * 240; ++i)
         {
            const uint16_t v = (uint16_t)((s[i] >> 8) | (s[i] << 8));
            r->lcd[i] = v;
            h = (h ^ v) * 16777619u;
         }
         if (h != r->lcdHash)
         {
            r->lcdHash = h;
            ++r->lcdRevision;
         }
      }
   }
   if (revision)
      *revision = r->lcdRevision;
   return r->lcd;
}

static const uint32_t* status_words(Rom* r) { return (const uint32_t*)at(r, call_u32(r, r->status), 16 * 4); }

static uint32_t core_leds(void* inst, uint8_t* out, uint32_t capacity)
{
   const uint32_t* st = status_words((Rom*)inst);
   for (uint32_t i = 0; out && i < capacity && i < SLOOP_LEDS; ++i)
      out[i] = !st ? 0
             : i < 18 ? (uint8_t)((st[0] >> i) & 1u)
             : i < 55 ? 0
                      : (uint8_t)((st[3] >> (i - 55)) & 1u);
   return SLOOP_LEDS;
}

static int32_t core_query(void* inst, uint32_t key)
{
   Rom* r = (Rom*)inst;
   const uint32_t* st = status_words(r);
   if (!st)
      return SR_QUERY_NONE;
   switch (key)
   {
   case SR_Q_TRACK: return (int32_t)st[5];
   case SR_Q_TRACK_COUNT: return 4;
   case SR_Q_PLAYING: return (int32_t)st[6];
   case SR_Q_MASTER: return (int32_t)(st[11] * 1023u / 4096u);
   case SR_Q_FADER_BANK: return (int32_t)st[8];
   case SR_Q_KEY_OCTAVE: return (int32_t)st[9] - 3;
   /* SLOOP's default MIDI IN channels: parts 1-3 on 1-3, drums on 10. */
   case SR_Q_KEY_CHANNEL: return st[5] < 3 ? (int32_t)st[5] : 9;
   case SR_Q_LED_COUNT: return SLOOP_LEDS;
   case SR_Q_EDIT_REVISION: return (int32_t)r->edits;
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

/* State: "SRR1", version, size, reserved, then the ROM's own snapshot. */
static uint32_t core_save_state(void* inst, uint8_t* dst, uint32_t capacity)
{
   Rom* r = (Rom*)inst;
   if (!r->snapshot || !r->snapshotSize)
      return 0;
   const uint32_t n = call_u32(r, r->snapshotSize);
   if (!n || n > 4u * 1024u * 1024u)
      return 0;
   if (!dst)
      return 16u + n;
   const uint8_t* snap = at(r, call_u32(r, r->snapshot), n);
   if (capacity < 16u + n || !snap)
      return 0;
   const uint32_t head[4] = { 0x31525253u, 1u, n, 0u };
   memcpy(dst, head, 16);
   memcpy(dst + 16, snap, n);
   return 16u + n;
}

static int core_load_state(void* inst, const uint8_t* src, uint32_t bytes)
{
   Rom* r = (Rom*)inst;
   uint32_t head[4];
   if (!src || bytes < 16 || !r->snapshot || !r->restore)
      return -1;
   memcpy(head, src, 16);
   const uint32_t n = call_u32(r, r->snapshotSize);
   if (head[0] != 0x31525253u || head[1] != 1u || head[2] != n || bytes != 16u + n)
      return -2;
   core_boot(r, 0);
   uint8_t* snap = at(r, call_u32(r, r->snapshot), n); /* the ROM's own buffer */
   if (!snap)
      return -3;
   memcpy(snap, src + 16, n);
   const uint32_t res = call_u32(r, r->restore);
   ++r->edits;
   return res ? -4 : 0;
}

static int core_import(void* i, uint32_t k, uint32_t s, const uint8_t* d, uint32_t b)
{
   (void)i, (void)k, (void)s, (void)d, (void)b;
   return -1;
}
static uint32_t core_export_project(void* i, uint8_t* d, uint32_t c)
{
   (void)i, (void)d, (void)c;
   return 0;
}
static int core_load_factory(void* i, uint32_t x)
{
   (void)i, (void)x;
   return -1;
}
static int core_load_program(void* i, uint32_t x)
{
   (void)i, (void)x;
   return -1;
}

static const SynthRefCoreApi kApi = {
   SYNTHREF_CORE_ABI_VERSION, sizeof(SynthRefCoreApi), core_info, core_create, core_destroy, core_boot, core_render,
   core_midi, core_control, core_release, core_panic, core_lcd, core_leds, core_query, core_text, core_save_state,
   core_load_state, core_import, core_export_project, core_load_factory, core_load_program, core_create_rom,
   core_instance_info,
};

CORE_EXPORT const SynthRefCoreApi* synthref_core_entry(uint32_t hostAbiVersion)
{
   return hostAbiVersion == SYNTHREF_CORE_ABI_VERSION ? &kApi : 0;
}
