/* SPDX-License-Identifier: GPL-3.0-only */
/* Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
 *
 * Synth REF core ABI, version 1. A core is one instrument firmware (or any
 * open synth engine) built as its own DLL and loaded by SynthRefCoreHost. The
 * DLL exports one C function, synthref_core_entry, returning a table of plain
 * C function pointers. Nothing C++ crosses the boundary: no exceptions, no
 * STL, no allocation handed to the other side. Every buffer a core returns
 * stays owned by the core and is valid until the next call on that instance.
 *
 * Threading: the host serialises every call on one instance (it never calls
 * two functions of the same instance at once), and render() is called from
 * the audio thread. A core therefore needs no locks of its own.
 *
 * Instances: a core whose firmware keeps its state in C globals sets
 * SR_CORE_SINGLETON. The host then loads a private copy of the DLL file per
 * instance, so each copy has its own globals, and calls create() once per
 * copy. A reentrant core leaves the flag clear and may be created many times
 * in one module.
 *
 * Compatibility: the host refuses a core whose abiVersion differs from
 * SYNTHREF_CORE_ABI_VERSION. Fields are only ever appended; structSize tells
 * the host which fields a core knows. */
#ifndef SYNTHREF_CORE_API_H
#define SYNTHREF_CORE_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTHREF_CORE_ABI_VERSION 1u
#define SYNTHREF_CORE_ENTRY "synthref_core_entry"

enum
{
   SR_CORE_SINGLETON = 1u << 0 /* firmware globals: one DLL copy per instance */
};

enum
{
   SR_BOOT_CLEAR = 1u << 0 /* wipe the virtual device storage (factory state) */
};

/* Physical control kinds. Ids within a kind are the core's own. */
enum
{
   SR_CTL_BUTTON = 0,
   SR_CTL_KEY = 1,      /* value = velocity 1..127, 0 = release */
   SR_CTL_PRESSURE = 2, /* value = 0..127 */
   SR_CTL_PAD = 3,      /* value = velocity 1..127, 0 = release */
   SR_CTL_ENCODER = 4,  /* see SynthRefControlInfo.mode */
   SR_CTL_PEDAL = 6,    /* value = 0..127 */
   SR_CTL_FADER = 7,    /* see SynthRefControlInfo.mode */
   SR_CTL_KINDS = 8
};

/* How a control reports motion. A controller profile says what the hardware
 * sends; the host converts between the two (absolute <-> relative, pickup). */
enum
{
   SR_MODE_SWITCH = 0,     /* 0 / 1 */
   SR_MODE_VELOCITY = 1,   /* 0 = off, 1..127 */
   SR_MODE_RELATIVE = 2,   /* signed detents per event, -127..127 */
   SR_MODE_ABSOLUTE = 3,   /* minValue..maxValue, the core does its own pickup */
   SR_MODE_CONTINUOUS = 4  /* endless pot read as an absolute minValue..maxValue */
};

typedef struct SynthRefControlInfo
{
   uint16_t kind;
   uint16_t id;
   uint16_t mode;
   int16_t minValue;
   int16_t maxValue;
   int16_t restValue; /* value a released / centred control sends (wheels) */
   const char* name;
} SynthRefControlInfo;

enum
{
   SR_LCD_NONE = 0,
   SR_LCD_RGB565 = 1
};

/* Data a core can take from a file. */
enum
{
   SR_IMPORT_DX7_VOICE = 1u << 0,  /* single voice bulk, 163 bytes */
   SR_IMPORT_DX7_BANK = 1u << 1,   /* 32 voice bulk, 4104 bytes; slot = bank */
   SR_IMPORT_PROJECT = 1u << 2,    /* the firmware's own project file */
   SR_IMPORT_TIMBRE = 1u << 3      /* the firmware's own timbre file */
};

/* Text a core can describe (text()). */
enum
{
   SR_TEXT_PROJECT = 0,      /* current project / patch name */
   SR_TEXT_PROGRAM = 1,      /* program name; index = program */
   SR_TEXT_FACTORY = 2,      /* factory project name; index = project */
   SR_TEXT_PAD_LABEL = 3,    /* pad label; index = pad */
   SR_TEXT_TRACK_SOURCE = 4  /* engine of a track; index = track */
};

/* Integer state a core can report (query()). Unknown keys return
 * SR_QUERY_NONE, so a panel shows "unavailable" instead of a fake value. */
#define SR_QUERY_NONE ((int32_t)INT32_MIN)
enum
{
   SR_Q_TRACK = 0,         /* selected track */
   SR_Q_TRACK_COUNT = 1,
   SR_Q_PROGRAM = 2,       /* program of the selected track, -1 = none */
   SR_Q_PROGRAM_COUNT = 3,
   SR_Q_FACTORY_COUNT = 4,
   SR_Q_SHIFT = 5,
   SR_Q_FADER_BANK = 6,
   SR_Q_PICKUP_MASK = 7,   /* bit per fader waiting for pickup */
   SR_Q_MASTER = 8,        /* 0..1023 */
   SR_Q_KEY_CHANNEL = 9,   /* MIDI channel of the keyboard's track, 0..15 */
   SR_Q_PAD_CHANNEL = 10,
   SR_Q_PAD_STEPS = 11,    /* pads edit sequencer steps */
   SR_Q_PLAYING = 12,
   SR_Q_TEMPO_X10 = 13,
   SR_Q_KEY_OCTAVE = 14,
   SR_Q_LED_COUNT = 15,
   SR_Q_EDIT_REVISION = 16 /* bumps whenever stored state changes */
};

typedef struct SynthRefCoreInfo
{
   uint32_t structSize;
   const char* coreId;        /* stable key saved in patches, e.g. "smk37_open" */
   const char* coreName;      /* "SMK37 OPEN" */
   const char* coreVersion;   /* "V01" */
   const char* upstream;      /* source URL + commit */
   const char* license;       /* SPDX expression */
   const char* instrumentId;  /* "mvave.smk37pro" */
   const char* manufacturer;
   const char* model;
   uint32_t flags;            /* SR_CORE_* */
   uint32_t sampleRate;       /* fixed internal rate; the host resamples */
   uint32_t blockFrames;      /* frames per render() */
   uint32_t channels;         /* interleaved output channels (1 or 2) */
   uint32_t lcdWidth, lcdHeight, lcdFormat;
   uint32_t controlCount;
   const SynthRefControlInfo* controls;
   uint32_t importKinds;      /* SR_IMPORT_* */
   uint32_t stateBytesMax;    /* upper bound of save_state() */
   /* Appended in ABI 1 (read only when structSize covers them): */
   int32_t keyboardBaseNote;  /* MIDI note of SR_CTL_KEY 0 at the panel's octave 0 */
} SynthRefCoreInfo;

typedef struct SynthRefCoreApi
{
   uint32_t abiVersion; /* SYNTHREF_CORE_ABI_VERSION */
   uint32_t structSize;
   const SynthRefCoreInfo* (*info)(void);

   void* (*create)(void); /* NULL = failure */
   void (*destroy)(void* inst);
   void (*boot)(void* inst, uint32_t flags);

   /* One block of info->blockFrames * info->channels interleaved floats. */
   const float* (*render)(void* inst);

   /* One complete short message (1..3 bytes) or one SysEx (F0 .. F7). */
   int (*midi)(void* inst, const uint8_t* bytes, uint32_t count);
   int (*control)(void* inst, uint32_t kind, uint32_t id, int32_t value);
   void (*release)(void* inst); /* every held control up */
   void (*panic)(void* inst);   /* release + all sound off */

   const uint16_t* (*lcd)(void* inst, uint32_t* revision);
   /* LED / indicator states, one byte each (0 = off): one per button id, then
    * one per key, then one per pad (a core stops where it has no more LEDs).
    * Returns the count. */
   uint32_t (*leds)(void* inst, uint8_t* out, uint32_t capacity);
   int32_t (*query)(void* inst, uint32_t key);
   /* Writes a NUL-terminated string, returns its length (0 = none). */
   uint32_t (*text)(void* inst, uint32_t which, uint32_t index, char* out, uint32_t capacity);

   /* Full instance state (virtual storage + working project). With dst ==
    * NULL returns the size needed. load_state returns 0 on success. */
   uint32_t (*save_state)(void* inst, uint8_t* dst, uint32_t capacity);
   int (*load_state)(void* inst, const uint8_t* src, uint32_t bytes);

   /* SR_IMPORT_* data from a file. 0 = success, <0 = refused. */
   int (*import_data)(void* inst, uint32_t kind, uint32_t slot, const uint8_t* data, uint32_t bytes);
   /* The firmware's own project file of the working project. */
   uint32_t (*export_project)(void* inst, uint8_t* dst, uint32_t capacity);

   /* Preset navigation as the instrument's panel does it. <0 = unsupported. */
   int (*load_factory)(void* inst, uint32_t index);
   int (*load_program)(void* inst, uint32_t program);

   /* Appended in ABI 1 (used only when structSize covers them, NULL when a
    * core has none): a ROM core runs a firmware image exactly as its authors
    * distribute it (synthref_rom.dll: their WebAssembly simulator builds).
    * create_rom replaces create(); each instance then has its own info. */
   void* (*create_rom)(const char* utf8Path, char* error, uint32_t errorCapacity);
   const SynthRefCoreInfo* (*instance_info)(void* inst);
} SynthRefCoreApi;

typedef const SynthRefCoreApi* (*SynthRefCoreEntryFn)(uint32_t hostAbiVersion);

#ifdef __cplusplus
}
#endif

#endif
