# Synth REF

Synth REF (Turbo 0.72, **full build only**) runs a real instrument firmware inside a patch.
The firmware's own C code is compiled natively into a separate DLL (a "core"), so its engines,
presets, screen and panel logic are the instrument's own, not a re-implementation. The node
draws the instrument's panel, and every control there is an event the firmware handles as on the
hardware. Notes and controls can also come from a real MIDI controller.

Three cores are selectable today (dropdown at the top of the node): **SMK37 OPEN V01**, M-VAVE's
open firmware for the SMK-37 Pro (GPL-3.0); **FM1 OPEN V14**, M-VAVE's open firmware for the FM-1
(GPL-3.0); and **SLOOP SMK 2.6** (SLOOP 2.8), the community GPL-3.0 firmware for the SMK-37 Pro
(Experimental). The panel is built from the controls each core declares, so every instrument gets
its own layout.

| | |
|---|---|
| Node | `Synth REF`, category *Synths*, one audio output pin, one `notes` input pin |
| Code | `src/gpl/SynthRef/` (GPL-3.0-only), registered under `#if INFINITE_GPL` in `src/app/ui/PinsAndTheme.inl`; the panel is `SynthRefBody.inl`, included by `src/main.cpp` |
| Platform support | `Platform::MidiReadRawSince` (`src/platform/Platform.h`, "Turbo 0.72"): every short MIDI message with its source device, lock-free multi-consumer ring |
| Edition | Full build (`INFINITE_ENABLE_GPL=ON`). The MIT build has no trace of Synth REF |
| Related docs | `docs/SYNTH-REF-MVP-TEST-GUIDE.md` (MiniLab 3 test, pt-BR), `docs/SYNTH-REF-BANCOS.md` (banks and profiles, pt-BR), `src/gpl/README.md`, `THIRD_PARTY_NOTICES.md` |

## 1. Components

```
 Documents\Infinite-Turbo\SynthRef\            <exe>\synthref\
   (user banks, projects, profiles\)             cores\synthref_smk37_open.dll
                                                       synthref_fm1_open.dll
                                                       synthref_sloop_smk.dll
                                                 profiles\*.json (bundled)
        |                                              |
        v                                              v
 +--------------------------- Infinite.exe ----------------------------------+
 |  main thread                              audio thread                     |
 |  SynthRefNode (INode, IAudioSource)       AudioSynthRefNode (AudioNode)    |
 |   - params, patch save/load               - try_lock(core mutex)           |
 |   - CookIfNeeded: open core, profile,     - drains the command ring        |
 |     device policy, learn strip, state     - runs queued jobs               |
 |   - SynthRefBody.inl (panel, ImGui)       - pin notes + device MIDI        |
 |        |  controls / MIDI                 - core render() x N -> resampler |
 |        +----- SPSC command ring --------> - volume, peak meter             |
 |        <---- triple buffers (LCD, LEDs, state, texts) ---------+           |
 |        <---- SPSC diagnostics ring ----------------------------+           |
 |                                                                            |
 |  SynthRefCore (loader, registry)  -- LoadLibrary(private copy) --+         |
 +------------------------------------------------------------------|--------+
                                                                     v
   %TEMP%\Infinite-Turbo\synthref\smk37_open-<pid>-<n>.dll   (one copy per instance)
        exports synthref_core_entry -> SynthRefCoreApi (C ABI v1)
        adapter (mvave_open_core.c or sloop_smk_core.c) + unmodified firmware (upstream/)
```

| File | Role |
|---|---|
| `SynthRefCoreApi.h` | The C ABI between host and core (v1) |
| `SynthRefCore.h/.cpp` | Firmware registry, core loader (DLL copy per instance), user and app directories, native file dialogs |
| `SynthRefMidi.h/.cpp` | Controller profiles: JSON schema, loader, device matching, relative-encoder decoding |
| `SynthRefNode.h/.cpp` | The node: audio half, MIDI policy, resampler, persistence |
| `SynthRefBody.inl` | The panel, built from the controls the core declares (section 2): LCD, buttons, encoders, faders / wheels / pots, pads, keys, preset row, MIDI row, MIDI DIAGNOSTICS |
| `cores/mvave_open_core.c` | One adapter of the M-VAVE firmware host platform onto the ABI, shared by SMK37 OPEN (built with `-DSR_MVAVE_SMK37`) and FM1 OPEN (built without it) |
| `cores/smk37_open/upstream/`, `cores/fm1_open/upstream/` | The M-VAVE firmware sources, vendored byte for byte (see section 9) |
| `cores/smk37_open/UPSTREAM.md`, `cores/fm1_open/UPSTREAM.md` | Commit and SHA-256 prefix of every vendored file |
| `cores/sloop_smk/sloop_smk_core.c` | Adapter for SLOOP SMK. It `#include`s the firmware's own browser engine `upstream/web/audio/sloop-smk-engine.c` (which in turn includes the SMK runtime and the SLOOP 2.8 core) as one translation unit, exactly as the firmware's web build compiles it |
| `cores/sloop_smk/upstream/`, `cores/sloop_smk/generated/` | The SLOOP SMK sources, vendored byte for byte, and the six `felucca_*.h` tables generated by the firmware's own `sloop/tools/gen_*.py` |
| `cores/sloop_smk/UPSTREAM.md` | Commit and SHA-256 prefix of every vendored file |

## 2. Core ABI (v1)

Defined in `src/gpl/SynthRef/SynthRefCoreApi.h`; read the header for field-level detail. Summary:

- A core is a DLL exporting `synthref_core_entry(hostAbiVersion)`, which returns a table of plain
  C function pointers (`SynthRefCoreApi`). No C++ crosses the boundary. The host refuses a core
  whose `abiVersion` differs from `SYNTHREF_CORE_ABI_VERSION` (1) or whose `structSize` is too
  small. Fields are only ever appended.
- Buffers a core returns stay owned by the core and are valid until the next call on that instance.
- The host serialises every call on one instance, so a core needs no locks.
- `SynthRefCoreInfo`: ids and names, upstream URL + commit, SPDX licence, flags
  (`SR_CORE_SINGLETON`), internal sample rate, block size, channels, LCD size and format, the
  control list, accepted import kinds, upper bound of the saved state, and `keyboardBaseNote`
  (appended in ABI 1, read only when `structSize` covers it): the MIDI note of key control 0 at the
  panel's octave 0 (SMK-37 cores: 48, C3; FM-1: 53, F3), which the panel uses to draw the
  keyboard (black and white keys) and to work out the note of each key.
- Calls: `create/destroy/boot`, `render` (one block of interleaved floats), `midi` (one short
  message or one SysEx), `control(kind, id, value)`, `release`, `panic`, `lcd`, `leds`, `query`,
  `text`, `save_state/load_state`, `import_data`, `export_project`, `load_factory`, `load_program`.
- Control kinds: button, key, pressure, pad, encoder, pedal, fader. Each control reports motion
  as switch, velocity, relative, absolute or continuous; the host converts between what the
  controller sends and what the instrument expects (absolute to detents, pickup).
- `query` returns `SR_QUERY_NONE` for unknown keys, so the panel shows "unavailable" instead of a
  made-up value.
- `leds()` writes one byte per LED in a fixed layout: **one per button id, then one per key, then
  one per pad** (a core stops where it has no more LEDs) and returns the count; `SR_Q_LED_COUNT`
  tells the panel how many of them to show. SMK37 OPEN reports 18 (its buttons), FM1 OPEN 41
  (14 buttons + 27 key LEDs), SLOOP SMK 71 (18 buttons + 37 keys + 16 pad lights).

**The panel follows the core.** `SynthRefBody.inl` does not hard-code an instrument: it counts the
buttons, encoders, faders, pads, keys and pedal in the core's control list, takes their names from
it, and lays out the screen, the buttons, the encoders, the faders / wheels / pots, the pads and the
keys accordingly (a fader starts where the core says it rests, and faders F1..F4 show as F5..F8 on
an SMK-37 with its fader bank on).

| Core | Panel |
|---|---|
| SMK37 OPEN | 18 buttons, 8 encoders (K1-K4, SELECT, PAGE, PRESETS, VOLUME), 4 faders + pitch / mod wheels + sustain pedal, 16 pads, 37 keys |
| FM1 OPEN | 14 buttons (OCT-, OCT+, FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC), 7 encoders (SELECT, ALGORITHM, KNOB 1-4, PRESETS), the volume pot as a 0..127 fader (VOLUME, scaled to the firmware's 0..1023 ADC), 27 keys F3..G5 with key LEDs, no pads |
| SLOOP SMK | 18 buttons (KNOB BANK, FADER BANK, <, >, OCT -, OCT +, ARP, NOTE REPEAT, SCALE, CHORD, GLO, BT / EDIT, PATCH / HOME, PARA / SAVE, FX, SEQ, PLAY / STOP, REC), 8 encoders (VOLUME, TEMPO, PRESETS, TRACK, K1-K4), faders F1-F4, pitch / mod wheels, sustain, 37 keys, 16 pads with pad lights |

Verified, per core:

| Core | Rate | Block | Output | LCD |
|---|---|---|---|---|
| SMK37 OPEN | 44100 Hz | 128 frames | stereo | 240x240 RGB565 (8 tracks, 128 DX7 programs) |
| FM1 OPEN | 44100 Hz | 128 frames | stereo | 240x240 RGB565 (same application family as SMK37 OPEN) |
| SLOOP SMK | 44100 Hz | 32 frames | stereo (the firmware's int32 output, divided by 32768) | 240x240 RGB565 |

## 3. Instances and the registry

The firmware keeps its state in C globals, so the core sets `SR_CORE_SINGLETON`. For every
instance the host copies the core DLL to `%TEMP%\Infinite-Turbo\synthref\<coreId>-<pid>-<n>.dll`,
loads that copy (`LoadLibraryExW`), and calls `create()` once per copy. Each copy has its own
globals, so several Synth REF nodes are independent. (FM1VST by Quixotic7 solves the same
problem on macOS by loading one module copy per instance.) The copy is deleted when the core
closes; copies a crashed session left behind are removed the first time a core opens in the next
session (a copy still loaded by another running Infinite cannot be deleted and is skipped).

`SynthRefFirmwares()` in `SynthRefCore.cpp` is the reviewed list of instrument + firmware pairs.
Cores are never discovered from disk, so an arbitrary DLL cannot be loaded.

| coreId | Instrument | Firmware | Status | Note |
|---|---|---|---|---|
| `smk37_open` | M-VAVE SMK-37 Pro | SMK37 OPEN V01 | Supported | whole application built natively; bit-exact with its official simulator |
| `smk37_stock` | M-VAVE SMK-37 Pro | Stock V016 | Unavailable | factory firmware for the JieLi chip; no open engine of it runs on a PC |
| `sloop_smk` | M-VAVE SMK-37 Pro | SLOOP SMK 2.6 / SLOOP 2.8 | Experimental | community GPL-3.0 firmware, its own engines, screen and panel; built without the all-rights-reserved Felucca artwork, so the knob-card icons are blank |
| `sloop_fm1` | M-VAVE FM-1 | SLOOP 2.8.1 | Unavailable | community GPL-3.0 firmware; its host preview has no screen, panel or preset loading yet |
| `fm1_open` | M-VAVE FM-1 | FM1 OPEN V14 | Supported | M-VAVE's open FM-1 firmware (GPL-3.0), the whole application built natively; the same application family and layout as SMK37 OPEN |

Statuses are `Supported` (tested, works), `Experimental` (partly working) and `Unavailable`
(known, no executable core). Unavailable entries are listed but not selectable, and the host
refuses to open them.

## 4. Threading and real time

All calls into a core are made under one mutex (`AudioSynthRefNode::mCoreMutex`).

- The audio thread only `try_lock`s it. If the main thread holds it (swapping a core, a rare
  heavy operation), that one block is silent; the callback never waits.
- Controls and MIDI go main thread to audio thread through a lock-free SPSC command ring
  (1024 entries). The audio thread executes them right before rendering.
- What the panel needs comes back lock-free through triple buffers: the LCD (when the firmware
  redrew it, at most 60 Hz), LEDs and queries (about every 20 ms of audio), texts (project name,
  pad labels, track sources; about every 100 ms). A separate SPSC ring (512 entries) carries MIDI
  DIAGNOSTICS records.
- One-off operations (load a preset, import a file, export the project) run as a **job**: the
  main thread posts it, the audio thread runs it before its next render, and the main thread
  waits (up to 400 ms, then takes the job back if the audio thread never started it and runs it
  under the lock). With no audio running (no block for 250 ms), the main thread runs the job
  itself under the lock.
- The patch's copy of the firmware state is refreshed **asynchronously** (its own job slot): when
  the firmware's edit revision changes and has been still for 300 ms, an export is posted and
  `CookIfNeeded` picks the result up on a later frame, never waiting for it. A core swap first
  settles a pending export.
- With **no audio running**, `CookIfNeeded` advances the firmware itself, in real time, silently
  (`IdleTick`, at most 64 core blocks per frame): its screen, presets, panel and MIDI
  DIAGNOSTICS keep working with the audio off, like the hardware with the volume down. This is
  the firmware's clock, not DSP for the graph; it stops as soon as an audio callback renders the
  node, and the resampler then restarts from silence.
- Inputs are never dropped while the main thread holds the lock: pin notes and device MIDI stay
  queued for the next block. Removing the notes cable, changing device or mapping mode, losing
  focus or reloading a profile releases every note, pad and button the source still holds.
- Work that allocates (refreshing the program and factory-project names) runs on the main thread
  under the lock, never as an audio job.
- Core boot and load (`OpenCoreFromState`) run on the main thread.
- Output gain (-60..+12 dB, -60 = mute) is smoothed; the peak meter is a lock-free atomic max.

### Resampler

The core renders at its fixed rate (44.1 kHz) in blocks of its own size (128 frames for the
M-VAVE cores, 32 for SLOOP SMK); the engine runs at the device rate. A windowed-sinc polyphase resampler converts: 32 taps, 256 interpolated phases (linear
between phases), Kaiser window (beta 7), cutoff 0.92 x the lower Nyquist, unity DC gain. At equal
rates it is bypassed and the output is the core's samples bit for bit. The design target in the
source comments is a passband flat to about 20 kHz at 44.1 to 48 kHz and images below -80 dB;
that was not measured here.

## 5. MIDI policy

Infinite opens each MIDI input once (JUCE) and shares it, so there are no conflicting
connections. The node reads the raw-message ring and keeps only messages of its selected device.

**Where notes come from**

| Situation | Notes |
|---|---|
| `notes` pin cabled | Only from the pin. Notes from the device are dropped (logged in MIDI DIAGNOSTICS), except the note-off of a key held from before the pin was cabled |
| Pin not cabled | From the device selected on the node |

Knobs, faders and pads keep coming from the device even with the pin cabled.

**Which Synth REF listens**

| `lock` | Behaviour |
|---|---|
| on | Always receives from its device |
| off | Receives while this Synth REF is the focused one. A Synth REF becomes focused when it is selected in the editor or when a device is chosen on it. Selecting a node that is not a Synth REF does not change the focus, so the last selected Synth REF keeps it |

Messages that arrive while the node is not active are logged as ignored. When a node stops being
active, the notes it was holding are released.

**Keyboard notes.** Notes (from the pin or a device) are sent to the firmware as MIDI notes on
the channel of the firmware's keyboard track (`SR_Q_KEY_CHANNEL`), so they play the selected KEY
track at the pitch pressed. A note-off follows its note-on to the channel it started on, even if
the keyboard moved to another track meanwhile. Pin notes use velocity = round(127 x velocity);
Infinite's bend (semitones) maps onto the firmware's +-2 semitone range.

**Other messages.** Pitch bend, CC1, channel pressure, program change and polyphonic aftertouch
are forwarded to the keyboard channel (pitch bend, CC1 and channel pressure follow the profile's
`pass` flags; program change and polyphonic aftertouch are always forwarded). CC64 drives the
firmware's sustain pedal (unless the profile sets `"sustain"` to something other than `"pedal"`).
CC120 and CC123 are forwarded.

**Mapping mode**

| Mode | Behaviour |
|---|---|
| AUTO MAP | A controller profile (section 6) says which CC or note drives which physical control. Unmapped CCs are forwarded to the keyboard channel |
| MIDI LEARN | Profile mapping is off. Encoders (K1..K8 in the strip, named after the instrument's encoders), faders F1..F4 and 16 pads appear as node parameters, so Infinite's own MIDI learn, modulators and the mouse drive them. Knob values 0..1 are turned into encoder detents (127 per full turn); faders map 0..1 to 0..127; pads are gates (velocity 100). Only CC1, CC64, CC120 and CC123 are passed from the device in this mode, and the profile's pad and button notes are not routed |

**Device hotplug.** Every 2 s the node checks its saved device. If it is open, the status reads
"connected". If it is not open but Windows lists an input of the same name (the part before
` #`), the node rescans and reports "reconnected" (or that it could not be opened, for example
because another app holds it). Otherwise it reports "not connected - plug it in (the selection is
kept)". The RESCAN button is MIDI DEVICE RESCAN and reopens every input.

## 6. Controller profiles (AUTO MAP)

A profile is a JSON file for one combination CONTROLLER + MIDI MODE + INSTRUMENT FIRMWARE.

**Locations.** Bundled: `<exe>\synthref\profiles` (source: `assets/synthref/profiles`, copied by
the build). User: `Documents\Infinite-Turbo\SynthRef\profiles` (created on first use). Files are
read in name order, bundled first, then user; a profile with the same `id` as an earlier one
replaces it, so the user's wins. A file that fails to parse is skipped and its error is shown in
MIDI DIAGNOSTICS ("profile error"). "reload profiles" in MIDI DIAGNOSTICS re-reads both folders.

**Selection.** The node's profile menu either names a profile (`id` and `core` must match) or is
"auto profile": the first profile for the core whose `device.match` / `device.exclude` accept the
device name.

**Schema (`schema` = 1)**

| Field | Type | Meaning |
|---|---|---|
| `schema` | int | Must be 1 |
| `id` | string | Unique key, saved in patches. Required |
| `name` | string | Shown in the panel |
| `core` | string | `coreId` the profile is for, e.g. `smk37_open`. Required |
| `mode` | string | Free text: the controller mode this profile assumes (shown in diagnostics) |
| `device.match` | string[] | The device key must contain one of these (case-insensitive) |
| `device.exclude` | string[] | ... and none of these |
| `keyboard.channel` | int 1..16 or `"any"` | Channel of the controller's keyboard. Unmapped notes on other channels are dropped. `"any"` or absent: notes on any unmapped channel play the keyboard (the bundled profiles use `"any"`) |
| `pads.channel` | int 1..16 or absent | Channel of the pad notes; absent = every channel |
| `pads.first_note` | int, default 36 | First pad note |
| `pads.count` | int 0..128, default 16 | Number of consecutive pad notes |
| `pads.first_id` | int, default 0 | Instrument pad id of the first note (0-based) |
| `controls[]` | array | One entry per mapped message, below |
| `controls[].cc` or `.note` | int 0..127 | The message. `note` makes it a note mapping |
| `controls[].channel` | int 1..16 or absent | Absent = every channel |
| `controls[].target` | string | `encoder`, `fader`, `button`, `pad`, `pedal`, `pressure`, `keyboard` or `none` |
| `controls[].id` | int, 0-based | Instrument control id: the id the core declares for that control (SMK37 OPEN: encoder 0..7 = K1..K4, SELECT, PAGE, PRESETS, VOLUME; fader 0..3 = F1..F4. FM1 OPEN: encoder 0..6 = SELECT, ALGORITHM, KNOB 1-4, PRESETS; fader 0 = VOLUME. SLOOP SMK: encoder 0..7 = VOLUME, TEMPO, PRESETS, TRACK, K1..K4; fader 0..3 = F1..F4) |
| `controls[].input` | string, default `absolute` | `absolute`, `relative64`, `relative2c` or `relative16` |
| `controls[].label` | string | Text shown in diagnostics |
| `pass.pitchbend`, `pass.modwheel`, `pass.pressure` | bool, default true | Forward pitch bend, CC1, channel pressure to the keyboard channel |
| `pass.sustain` | string | `"pedal"` (default) sends CC64 to the sustain pedal |

An entry with an unknown `target` or `input` name, or a CC/note outside 0..127, makes the whole
file fail to load.

**Input encodings**

| `input` | Meaning | Decoded as |
|---|---|---|
| `absolute` | 0..127 position | Difference to the last value, as detents; the first message only establishes the position. For a fader: the value itself |
| `relative64` | 64 = still, 65.. = +1.., 63.. = -1.. (Arturia "Relative #1") | value - 64 |
| `relative2c` | 1.. = +1.., 127.. = -1.. (two's complement, "Relative #2") | value (below 64) or value - 128 |
| `relative16` | 16 = still, 17.. = +, 15.. = - ("Relative #3") | value - 16 |

An absolute encoder stops at 0 and 127 (no more movement is reported at the ends); endless
behaviour needs the controller set to a relative mode and the profile `input` changed to match.

**Example: `arturia-minilab3.smk37_open.json`** (bundled)

```json
{
  "schema": 1,
  "id": "arturia-minilab3.smk37_open",
  "name": "Arturia MiniLab 3 -> SMK-37 (SMK37 OPEN)",
  "core": "smk37_open",
  "mode": "ARTURIA or USER program, factory CCs (MIDI Control Center defaults); not the DAW program",
  "device": {
    "match": ["minilab3", "minilab 3"],
    "exclude": ["mcu", "hui", "din thru", "alv"]
  },
  "keyboard": { "channel": "any" },
  "pads": { "channel": 10, "first_note": 36, "count": 16, "first_id": 0 },
  "controls": [
    { "cc": 74, "target": "encoder", "id": 0, "input": "absolute", "label": "encoder 1 -> K1" },
    { "cc": 71, "target": "encoder", "id": 1, "input": "absolute", "label": "encoder 2 -> K2" },
    { "cc": 76, "target": "encoder", "id": 2, "input": "absolute", "label": "encoder 3 -> K3" },
    { "cc": 77, "target": "encoder", "id": 3, "input": "absolute", "label": "encoder 4 -> K4" },
    { "cc": 93, "target": "encoder", "id": 4, "input": "absolute", "label": "encoder 5 -> SELECT" },
    { "cc": 18, "target": "encoder", "id": 5, "input": "absolute", "label": "encoder 6 -> PAGE" },
    { "cc": 19, "target": "encoder", "id": 6, "input": "absolute", "label": "encoder 7 -> PRESETS" },
    { "cc": 16, "target": "encoder", "id": 7, "input": "absolute", "label": "encoder 8 -> VOLUME" },
    { "cc": 82, "target": "fader", "id": 0, "input": "absolute", "label": "fader 1 -> F1" },
    { "cc": 83, "target": "fader", "id": 1, "input": "absolute", "label": "fader 2 -> F2" },
    { "cc": 85, "target": "fader", "id": 2, "input": "absolute", "label": "fader 3 -> F3" },
    { "cc": 17, "target": "fader", "id": 3, "input": "absolute", "label": "fader 4 -> F4" },
    { "cc": 114, "target": "encoder", "id": 6, "input": "relative64", "label": "main encoder -> PRESETS" },
    { "cc": 112, "target": "encoder", "id": 4, "input": "relative64", "label": "shift + main encoder -> SELECT" }
  ],
  "pass": { "pitchbend": true, "modwheel": true, "pressure": true, "sustain": "pedal" }
}
```

Reading it: the four faders drive F1..F4 (the firmware does its own pickup: the on-screen fader
turns amber until the hardware fader crosses the stored value); pads on channel 10, notes 36-51
(bank A 36-43, bank B 44-51), drive SMK pads 1-16; keys and knobs are accepted on any channel
(a real MiniLab 3 was found sending its keys on channel 15). Synth REF
never writes to the controller's memory and never resets or flashes it.

**Bundled MiniLab 3 profiles** (`assets/synthref/profiles`, one per core):

| Profile | Mapping |
|---|---|
| `arturia-minilab3.smk37_open.json` | keys and knobs on any channel; encoders 1-4 -> K1-K4, 5 SELECT, 6 PAGE, 7 PRESETS, 8 VOLUME; faders -> F1-F4; main encoder PRESETS, shift + main SELECT; pads (channel 10, notes 36-51) -> pads |
| `arturia-minilab3.fm1_open.json` | encoders 1-4 -> KNOB 1-4, 5 SELECT, 6 ALGORITHM, 7 PRESETS; fader 1 -> VOLUME; main encoder PRESETS, shift + main SELECT. The FM-1 has no pads, so the MiniLab pads (channel 10) press panel buttons: bank A (36-43) PLAY, REC, SEQ, ARP, HOME, SAVE, OCT -, OCT +; bank B (44-49) FX, SEL, ENV, LFO, EDIT, GLO. Its `pass.sustain` is `"pass"`, not `"pedal"`, so CC64 does not drive the pedal control |
| `arturia-minilab3.sloop_smk.json` | encoders 1-4 -> K1-K4, 5 TEMPO, 6 PRESETS, 7 TRACK, 8 VOLUME; faders -> F1-F4; main encoder PRESETS, shift + main TRACK; pads (channel 10, notes 36-51) -> pads |

## 7. Persistence

The patch stores the node's parameters (`VisitParams` order is the patch format: append only):

| Key | Content |
|---|---|
| `core` | `coreId` |
| `volume` | dB |
| `view` | 0 compact, 1 full |
| `device` | MIDI input key, empty = none |
| `lock` | bool |
| `mapmode` | 0 AUTO MAP, 1 MIDI LEARN |
| `profile` | profile id, empty = automatic |
| `k1`..`k8` | MIDI LEARN knob strip, 0..1 |
| `f1`..`f4` | MIDI LEARN fader strip, 0..1 |
| `state` | base64 of the core's `save_state()` |

`state` is the firmware's whole state, so a patch carries its projects, imported DX7 banks and
working project. SMK37 OPEN and FM1 OPEN share the format `SRS1`; the layout is (all `u32`
little-endian):

```
"SRS1" (0x31535253)   magic
1                     version
[u32 size][bytes] x 4 chunks:
  1. user flash area       69632 bytes (projects, DX7 library, settings)
  2. sample bank          466944 bytes, or size 0 when it is still the factory bank
  3. working project      the firmware's own project file (.fm1)
  4. master level         u32, 0..1023
```

The node keeps `state` current: when the core reports a new edit revision and has been still for
300 ms (an edit burst exports once), the state is exported again, so save, undo and copy carry
the firmware as it is. A patch load or undo that changes `state` reopens the core from it. A
state the core does not accept is replaced by the factory state and the node says so.

If the core is missing when a patch loads, the node stays silent, shows the error, and leaves
`state` untouched for when the core returns.

SLOOP SMK uses its own state format, not SRS1: `"SRL1"` (0x314C5253), a version, then the
firmware's own session snapshot (`fw_snapshot` / `fw_restore`): projects, slots, user presets,
arrangement, FM6 patches, settings, MIDI learn and the user sample memory. It is about 478 KB as
base64 in the patch (478,392 characters measured). SLOOP SMK has no file imports and no factory or
program dropdowns: presets are browsed on the instrument with PRESETS / PATCH.

## 8. Build

Full build only (`INFINITE_ENABLE_GPL=ON`, the default `build-windows.bat`).

- MSVC cannot build the firmware C (weak symbols, GNU attributes), so CMake fetches a pinned
  **llvm-mingw 20260922** (`llvm-mingw-20260922-ucrt-x86_64.zip`, SHA-512 pinned in
  `CMakeLists.txt`) into `build/<preset>/_deps` once. `INFINITE_LLVM_MINGW_DIR` points at an
  existing toolchain instead. If no compiler is found the build continues and the node reports
  its core as missing.
- Three DLLs are built into `<exe>\synthref\cores` by the custom target `synthref_cores`:
  `synthref_smk37_open.dll`, `synthref_fm1_open.dll` and `synthref_sloop_smk.dll`.
  `assets/synthref` is copied to `<exe>\synthref`.
- The two M-VAVE cores come out of one CMake loop, `foreach(_core smk37_open fm1_open)`. Each is
  built with the flags of the firmware's own host build (`tools/Build-Web.ps1`) on a native
  target: `-std=c11 -O2 -fwrapv -fno-fast-math -ffp-contract=off -DFM1_HOST_TEST
  -DFM1_OPEN_WEB -Wall -Wextra` (plus `-Wno-unused-function -Wno-array-parameter`), from the shared
  adapter `cores/mvave_open_core.c` plus that core's `upstream/app` (without `app/board`),
  `upstream/web` and `upstream/third_party/synth`. Only `smk37_open` adds `-DSR_MVAVE_SMK37`,
  which selects the SMK-37 behaviour in the adapter.
- `synthref_sloop_smk.dll` is compiled from the single file `cores/sloop_smk/sloop_smk_core.c`,
  which includes the firmware's browser engine. Flags: `-std=c11 -O2 -fwrapv -fno-builtin
  -Wno-int-to-pointer-cast`, with include paths `upstream`, `generated` and
  `upstream/firmware/hal`.
- The MIT build (`build-windows.bat mit`) leaves all of this out.

No proprietary SDK library is linked. The M-VAVE SDK's `sdk/lib/libfm1_engine.a` is a JieLi
PI32v2 ELF and is unusable on x86; the firmware's open web engine (`web/engine/Simulator.c`,
which its browser workbench runs) replaces it.

## 9. Updating the vendored firmware

Each firmware is vendored unmodified under its own `cores/<id>/upstream/`: SMK37 OPEN (86 files,
1.3 MB), FM1 OPEN (81 files) and SLOOP SMK (83 upstream files plus 6 generated headers). Synth REF
only adds its own adapter next to it. Do not edit files under `upstream/` or `generated/`.

**M-VAVE cores (SMK37 OPEN, FM1 OPEN)**

1. Clone `github.com/m-vave/smk37_open` or `github.com/m-vave/fm1_open` at the new commit.
2. `python scripts/synthref_vendor_mvave.py <smk37_open|fm1_open> <path to the clone>` (the script
   was `synthref_vendor_smk37.py` before FM1 OPEN). It copies, byte for byte, only what the host
   build compiles (`app/` without `app/board/`, `web/`, `third_party/synth/`, the public SDK
   headers `sdk/include/*.h`, the licence files) and rewrites that core's `UPSTREAM.md` with the
   commit and a SHA-256 prefix per file. It never copies `sdk/lib`, `sdk/firmware`,
   `sdk/include/libc` or tools.
3. Review the diff of `src/gpl/SynthRef/cores/<id>/upstream`, including licence headers and
   `THIRD_PARTY.md`. Update `THIRD_PARTY_NOTICES.md` if the notices changed.
4. Rebuild; check that the build still has no warnings at `-Wall -Wextra`.
5. Re-run the bit-exact comparison against the official WASM build (section 11) and
   `INFINITE_SYNTHREFTEST` (section 14).

**SLOOP SMK**

1. Clone `github.com/ChanceTheMaker/sloop-smk` at the new commit (branch `integrate/sloop-2.6-smk`).
2. Generate the six `felucca_*.h` headers (tables, CC0 samples, drum kits, FM6 patches, OFL font,
   icons) into a folder with the firmware's own `sloop/tools/gen_*.py`. Read each script before
   running it, and run it with `python -I`. The icons header must come from a **blank** 192x72
   atlas (names from `sloop/tools/icons.json`, no artwork): the "all rights reserved" `icons.png`
   is never vendored. The samples header is byte-identical without the Hügelton sample pack.
3. `python -I scripts/synthref_vendor_sloop_smk.py <clone of sloop-smk> <generated headers dir>`.
   It copies, byte for byte, the include closure of the firmware's browser engine
   (`web/audio/sloop-smk-engine.c`: the SMK runtime and the SLOOP 2.8 core it `#include`s) plus
   the licence files, keeping the upstream layout so the engine's relative `#include`s work
   unmodified, copies the generated headers into `generated/`, and rewrites `UPSTREAM.md` with the
   commit and a SHA-256 prefix per file. Never vendored: `assets/icons.png`, `docs/panel.jpg` and
   the Hügelton sample pack (`LICENSING.md`: all rights reserved).
4. Review the diff, including `LICENSING.md`, then update `THIRD_PARTY_NOTICES.md`, rebuild and
   re-run `INFINITE_SYNTHREFTEST`.

Currently vendored:

| Core | Source | Commit |
|---|---|---|
| SMK37 OPEN | `github.com/m-vave/smk37_open` | `b2d6bcbcd724adb13bcf85e44cfa16bb2c8153a8` |
| FM1 OPEN V14 | `github.com/m-vave/fm1_open` | `4c7496daac04a95ff10d1afdcc8f9ead2dbabc41` (2026-10-21) |
| SLOOP SMK | `github.com/ChanceTheMaker/sloop-smk`, branch `integrate/sloop-2.6-smk` (its `sloop/` is SLOOP 2.8 from `github.com/isod89/sloop-fm1` @ `b77bf55`) | `08c823081d9ab394077767a0604df7799ad449b0` |

## 10. Adding an instrument or firmware

1. New folder `src/gpl/SynthRef/cores/<id>/` with `upstream/` (vendored, with a vendoring script
   that records commit and hashes) and an adapter `.c` implementing `SynthRefCoreApi` and
   exporting `synthref_core_entry`. Set `SR_CORE_SINGLETON` if the firmware keeps globals.
2. A registry entry in `SynthRefCore.cpp` (`SynthRefFirmwares`) with the DLL name and a status.
3. A build rule in `CMakeLists.txt`, next to the existing ones ("Synth REF firmware cores"; another
   M-VAVE open firmware only needs its name in the `foreach` loop).
4. No panel code: declare the instrument's controls in the adapter (buttons, encoders, faders,
   pads, keys with their names, plus `keyboardBaseNote` and the LED layout of section 2) and
   `SynthRefBody.inl` lays them out.
5. Controller profiles in `assets/synthref/profiles`.
6. Licence review of every vendored file, then an entry in `THIRD_PARTY_NOTICES.md` and a row in
   `src/gpl/README.md`. GPL-2.0-only code cannot come in; non-commercial licences never do.

## 11. Feasibility and Phase 0 evidence

| Firmware | DSP on a PC | Timbres | Licence | Status |
|---|---|---|---|---|
| SMK37 OPEN V01 | proven, bit-exact | 128 DX7 voices, projects, 303 / 808 / PSG / pluck / sample engines | GPL-3.0-only with linking exception; Apache-2.0; MIT | done (Supported) |
| FM1 OPEN V14 (`m-vave/fm1_open` @ `4c7496da`, 2026-10-21) | built natively: same application family and layout as SMK37 OPEN (its sibling), compiles with zero warnings; the same scenario as the SMK37 OPEN proof renders RMS 0.08255 | 128 DX7 voices, 303 / 808 / PSG / pluck / sample engines, projects | GPL-3.0-only with linking exception; Apache-2.0; MIT | done (Supported) |
| SLOOP SMK (`ChanceTheMaker/sloop-smk` @ `08c82308`) | built: the firmware's browser engine compiled as one translation unit, 44.1 kHz, 32-frame blocks; its own engines (ANALOG / FM6 / PHASE / ..., no 303 / 808) | | GPL-3.0-only; Apache-2.0; MIT; OFL; CC0 | done (Experimental): its own screen and panel; knob-card icons blank; no file import |
| SLOOP 2.8.1 for the FM-1 (`sloop_fm1`) | its host preview has no screen, panel or preset loading | | GPL-3.0-only | Unavailable |
| Stock V016 | no open engine; factory firmware for the JieLi chip | | | Unavailable |
| FM1VST firmwares (`Quixotic7/FM1VST`, GPL-3.0) | feasibility only: see section 15 | | | not integrated |

**Phase 0 proof (SMK37 OPEN).** The firmware's browser simulator (`web/engine/Simulator.c`, all
of `app/` except `app/board`, `web/`, `third_party/synth`) compiles natively with llvm-mingw clang
23 (release 20260922, ucrt x86_64) with **zero warnings at `-Wall -Wextra`**, using the same flags
as upstream's `tools/Build-Web.ps1`. The native DLL rendered a 4 s C-major arpeggio (notes 60, 64,
67, 72, velocity 100) at 44.1 kHz: RMS 0.0825, peak 0.4148, DC -0.000025. The same sequence
through the official WASM build (WASI SDK 27, the exact `Build-Web.ps1` flags, run under Node) is
**bit-identical: 352768 of 352768 samples equal, max difference 0**. The tolerance used was exact.

## 12. Licences and dependencies

| Component | Where | Licence |
|---|---|---|
| Synth REF host (node, loader, profiles, panel, adapter) | `src/gpl/SynthRef/` | GPL-3.0-only |
| SMK37 OPEN app, web engine and SDK headers (M-VAVE) | `cores/smk37_open/upstream/app`, `web`, `sdk/include` | GPL-3.0-only WITH GPL-3.0-linking-exception |
| FM1 OPEN app, web engine and SDK headers (M-VAVE) | `cores/fm1_open/upstream/app`, `web`, `sdk/include` | GPL-3.0-only WITH GPL-3.0-linking-exception |
| DX7 core (port of msfa: Google Inc., Pascal Gauthier) and the 128 factory DX7 voices | `upstream/web/synth/dx7*`, `upstream/app/factory_dx7*.inc` (SMK37 OPEN), `upstream/app/fm_factory.inc` (FM1 OPEN) | Apache-2.0 |
| Open303 (Robin Schmidt), DaisySP / Plaits drums and pluck (Electrosmith, Emilie Gillet), emu2149 PSG (Mitsutaka Okazaki), SVF (stmlib), SEA codec (Dani Biro) | `upstream/third_party/synth`, `upstream/web/synth` | MIT |
| SLOOP SMK firmware: SLOOP 2.8 core, SMK runtime and browser engine (Leo Kuroshita and contributors; SMK port) | `cores/sloop_smk/upstream/` | GPL-3.0-only |
| msfa FM6 engine (Google Inc., Pascal Gauthier; C port by Leo Kuroshita) | `sloop_smk/upstream/sloop/firmware/src/fm6_core.c` | Apache-2.0 |
| DaisySP and Rings physical models (Electrosmith, Emilie Gillet; fixed-point ports) | `sloop_smk/upstream/sloop/firmware/src/phys_dsp.c`, `phys_symp.c` | MIT |
| Terminus font (the screen font, in `felucca_font.h`) and CC0 samples (in `felucca_samples.h`) | `cores/sloop_smk/generated/` | SIL OFL 1.1; CC0 1.0 |
| llvm-mingw 20260922 clang | build tree only | build tool, not shipped |
| JUCE (JSON for profiles, MIDI input) | already linked into Infinite | see `THIRD_PARTY_NOTICES.md` |

Exact notices: `THIRD_PARTY_NOTICES.md`, `cores/smk37_open/upstream/THIRD_PARTY.md`,
`cores/fm1_open/upstream/THIRD_PARTY.md` and `cores/sloop_smk/upstream/LICENSING.md` /
`upstream/sloop/LICENSING.md`.

**Not vendored (M-VAVE cores):** `sdk/lib` (proprietary JieLi engine library), `sdk/firmware`
(stock V16 images), `sdk/include/libc`, `app/board` (hardware drivers), `tools`. The M-VAVE binary
SDK is not used.

**Not vendored (SLOOP SMK):** `assets/icons.png`, the panel art and the Hügelton Sample Pack,
which `sloop/LICENSING.md` keeps "all rights reserved" (so the knob-card icons are blank). The
root `LICENSING.md` of `sloop-smk` is older text and disagrees with it; Synth REF follows the
stricter `sloop/LICENSING.md`.

**Timbre sources**

| Source | Status |
|---|---|
| 128 DX7 voices inside the firmware (SMK37 OPEN, FM1 OPEN) | Apache-2.0, bundled with the core |
| SLOOP SMK's CC0 samples | compiled into `generated/felucca_samples.h`, bundled with the core; the Hügelton Sample Pack is not |
| `jonathaslacerda/smk-37-pro-docs` sysex | 36 valid DX7 32-voice dumps (4104 bytes) named after Yamaha ROM/VRC cartridges, unknown provenance. **Never bundled**; user import only |
| `isod89/sloop-fm1` `assets/samples-cc0` | 40 CC0 WAVs (its hiphop-pack is **not** CC0). Not used by this core yet |

How a user imports banks without recompiling: `docs/SYNTH-REF-BANCOS.md`.

## 12b. ROMs: firmwares as their authors distribute them

Synth REF also runs a firmware **exactly as its authors distribute it** for their browser
simulators: a WebAssembly module. Nothing is rebuilt or recreated; the screens, fonts, icons,
samples and presets are the ones in the file, so the LCD is identical to the authors' simulator.

- Drop the `.wasm` in `Documents\Infinite-Turbo\SynthRef\roms` (the **ROMS** button on the node
  opens the folder and rereads it). Each ROM appears in the firmware menu as `ROM | <file>`.
  No rebuild of Infinite.
- `synthref_rom.dll` (`src/gpl/SynthRef/rom/rom_core.c`) runs it in **wasm3** v0.9.0 (MIT,
  vendored unmodified in `rom/wasm3`), compiled at load time (`m3_CompileModule`) so the audio
  thread only executes. A ROM imports nothing: it is sandboxed by construction (no file, network or
  OS access) and a trap only silences that node. Native stack guard: 512 KB
  (`d_m3MaxNativeStack`), a trap past it, never a crash. Each instance has its own runtime and
  linear memory (no DLL copies).
- ABI: `create_rom(path)` and `instance_info(inst)` were appended to `SynthRefCoreApi`; ids are
  `rom:<file>`; state is the ROM's own snapshot ("SRR1" + size + bytes).
- **Families** (the API a group of firmwares exports; a ROM of a known family loads without a
  rebuild, a new family needs its mapping once in `rom_core.c`):
  - SLOOP SMK web engine (`synth_*`, `fw_*`, zero imports): `web/audio/smk-engine.wasm` of
    github.com/ChanceTheMaker/sloop-smk. Tested: the generic battery passes; its audio, state
    size and screen match the natively built `sloop_smk` core exactly; ~9 % of one core.
  - Not yet: M-VAVE workbench (`sim_*`; the WASM is embedded in the workbench HTML).
- Not available as a ROM today: SLOOP for the FM-1 (its authors ship the hardware `.fwsc` and a
  sound-only preview without screen), FM1VST cores (macOS bundles), stock firmwares (JieLi CPU
  images; running them would need an emulator of a proprietary, undocumented CPU).
- Synth REF does not ship third-party ROMs; users load the files they obtain from the authors.

## 13. Known limitations

- **Sample import** (WAV into the firmware's sample bank) is not implemented.
- The **LCD is display-only**: the firmware's web "touch targets" are not used.
- With the **audio off** the firmware runs silently on the main thread (section 4); the meter
  stays at zero until audio starts.
- **Pad colours are not shown**; only pressed and MIDI-held states.
- **External MIDI clock sync** of the firmware sequencer to Infinite's transport is not implemented.
- **Multi-instance** works by DLL copies in `%TEMP%\Infinite-Turbo\synthref` (one per instance).
- **Encoders in absolute mode** stop at 0 and 127. For endless behaviour set them to relative in
  the controller and change `input` in a user copy of the profile.
- Two cores are Supported (SMK37 OPEN V01, FM1 OPEN V14) and one is Experimental (SLOOP SMK).
  `smk37_stock` and `sloop_fm1` are Unavailable and not selectable.
- Imported files (SMK37 OPEN and FM1 OPEN): a `.syx` must be a DX7 single voice (163 bytes) or
  32-voice bank (4104 bytes); files above 1 MiB are refused.
- **SLOOP SMK**: the knob-card icons on the firmware's screen are blank (the Felucca artwork is
  all rights reserved and not vendored). There is no file import (LOAD FILE) and no factory or
  program dropdown: presets are browsed on the instrument with PRESETS / PATCH, and the whole
  firmware state is saved in the patch (about 478 KB as base64).
- **FM-1 (FM1 OPEN)**: the keyboard octave is not reported by FM1 OPEN (`SR_Q_KEY_OCTAVE` is
  unknown to the adapter), so the panel does not follow the firmware's octave on that
  instrument.

## 14. Test status

`INFINITE_SYNTHREFTEST` (headless, real core, in the gate's full-build list; source
`src/gpl/SynthRef/SynthRefTests.inl`), 2026-10-24, Release, RTX 4060 laptop. Detailed battery for
SMK37 OPEN:

| Check | Result |
|---|---|
| Core loads from `<exe>\synthref\cores`, info | SMK37 OPEN V01, 44100 Hz, 128 frames, 2 ch, 102 controls |
| Silence with no notes | peak 0.0 |
| Bit-exact vs the bare core at 44.1 kHz (resampler bypassed) | 44100 samples, 0 differ |
| A4 level / DC / clipping, 44.1 and 48 kHz | rms 0.064, peak 0.170, DC < 2.5e-4 |
| Tuning A4 | 439.69 Hz (-1.2 cents); 44.1 vs 48 kHz differ by 0.01 cents |
| Velocity (panel key path) | vel 20 rms 0.054, vel 120 rms 0.096 |
| Polyphony | 4 notes rms 0.161 vs 1 note 0.064 |
| Note-off + release, panic | last 100 ms after 5 s / 3 s: 0.0 |
| Block-size independence (host blocks 64 / 333 / 512 / 2048 at 48 kHz) | 0 samples differ |
| Sample rates 22.05 / 32 / 88.2 / 96 kHz | all sound, pitch within 0.03 cents of 48 kHz |
| Two instances | A plays program 17, B silent on program 0 |
| Every factory project and every DX7 program while playing | 16/16, 128/128, all samples finite |
| State save / load | 98,568 base64 chars; two fresh nodes from it render identical audio |
| Missing core | silent, error shown, saved state untouched; comes back with its state |
| Firmware screen | 11,318 lit pixels; reacts to the FX button |
| Notes pin | plays (rms 0.080) and releases to 0.0 |
| MiniLab 3 profile | loads, maps as documented; the MCU port is excluded |
| CPU, one instance, 6-note chord, 48 kHz | 6.6 ms per audio second (0.66 % of one core) |

The table above is the detailed battery for SMK37 OPEN. `SYNTHREFTEST` also runs a **generic
battery for every selectable core** (the registry entries that are not Unavailable): the core
loads; silence with no notes; a MIDI note sounds and releases; a panel key plays; output is
independent of the host block size (512 vs 333); two instances are independent; saving and
loading the state is reproducible; the firmware screen is lit; CPU per instance. It also checks
that Unavailable entries are refused. Results, 2026-10-24, one instance, 48 kHz, all checks OK:

| Core | Note rms | State (base64 chars) | Screen (lit pixels) | CPU, one core |
|---|---|---|---|---|
| `smk37_open` | 0.0824 | 98,568 | 11,318 | 0.73 % |
| `fm1_open` | 0.0824 | 98,568 | 11,345 | 0.90 % |
| `sloop_smk` | 0.0597 | 478,392 | 13,471 | 0.54 % |

FM1 OPEN and SMK37 OPEN give the same note level and state size because they are the same
application family; the same scenario renders RMS 0.08255 on both. None of this replaces
listening or the hardware tests of `SYNTH-REF-MVP-TEST-GUIDE.md` (the MiniLab 3 itself, with the
FM1 OPEN and SLOOP SMK profiles).

Native vs official WebAssembly simulator (Phase 0, section 11): bit-identical for SMK37 OPEN. No
bit-exact comparison against a WebAssembly build is recorded for FM1 OPEN or SLOOP SMK.

Generic sweeps: `AUDIOPARAMSWEEPTEST` (all 16 params round-trip and reach the node) and
`AUDIOTEARDOWNSWEEPTEST` (spawn, wire, delete mid-playback) pass with Synth REF in them; the MIT
build compiles and contains no Synth REF code or files. `DSPTEST` shows 6 failures (envelope,
sampler) that the 0.71 reference build shows identically: not related.

Not covered by automated tests (needs the hardware, see `SYNTH-REF-MVP-TEST-GUIDE.md`): the
MiniLab 3 itself (device selection, AUTO MAP of encoders / faders / pads, hotplug), the panel by
mouse, and listening.

## 15. Next steps

Candidates, **not integrated**: the firmwares of **FM1VST** (`github.com/Quixotic7/FM1VST`,
GPL-3.0): Tonitor 0.55, Felucca 1.6.1, PIGEON 0.4.1, ChoralRoot 0.26.1, SLOOP 2.8, sloopDX 4.0 and
Melodee 1.4.5. In a feasibility test all seven were built as Windows DLLs and make sound. They are
not integrated yet because:

- their generated headers (fonts, icons, samples) need the firmwares' own Pillow-based generators;
- ChoralRoot and Melodee ship Casio CZ-1 and Sequential Prophet-5 factory data with no licence,
  which must be omitted;
- PIGEON needs a bank uploaded by SysEx.

Also open: the FM-1 variant of SLOOP (`sloop_fm1`, SLOOP 2.8.1), which needs a host with a screen,
panel and preset loading, and the items of section 13.
