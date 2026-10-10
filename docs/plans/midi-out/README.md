# MIDI Out: drive hardware synths from Infinite

Status: planned (2026-10-08). Origin: cables.gl vetting. The owner reports users with hardware synths who need Infinite to play them.
Skills to load before building: `new-audio-node`, `audio-node-ui`, `node-ui-pillars`, `windows-parity`, `linux-parity`, `audio-pipeline-sweep`, `rhythmic-quantization-standard`.

## What exists today (input only)

| Piece | Where |
|---|---|
| MIDI input, all 3 OSes | CoreMIDI `src/platform/Platform.mm`, WinMM `src/platform/win/MidiWin.cpp`, ALSA sequencer `src/platform/linux/MidiLinux.cpp` |
| Input API | `Platform::MidiStart/MidiRead/MidiDeviceSummary` (`Platform.h:828+`) |
| Clock **in** (follow 0xF8) | `Platform::MidiClockIsPresent/MidiClockBpm` (`Platform.h:985+`) |
| Note stream | `NoteEvent` (`src/audio/NoteEvent.h`): note, velocity 0..1, on/off, `frameOffset` in block. **No channel field.** |
| MIDI to plugins | `Platform::PluginScheduleMIDIEvent` (RT-safe, frame offset) |

Nothing sends MIDI to a device: no output ports anywhere in `src/`.

## Decisions (made; owner can override)

| # | Question | Decision | Why |
|---|---|---|---|
| D1 | One node or several? | **One `MIDI Out` node (Utility)** with a note input pin + optional CC rows + clock toggle | Hardware users think "one port, one synth". Three nodes would just repeat the device and channel picker three times |
| D2 | Channel | Per-node `channel` 1-16 param. `NoteEvent` stays channel-less | Keeps every existing note node untouched. Multi-channel = more MIDI Out nodes |
| D3 | CC out | Up to 4 CC rows on the node: `cc#` + modulatable `value` knob, sent on change (de-duplicated, ≤ 1 msg/ms per CC) | Uses existing modulation: any LFO/macro cable drives a hardware knob |
| D4 | Clock out | Toggle per node: 24 ppqn 0xF8 from `Transport` + Start/Stop/Continue + Song Position Pointer on locate | Standard "Infinite as master" behaviour. Disabled automatically when Infinite is *following* incoming clock on the same device (no feedback loop) |
| D5 | Threading | Audio thread pushes `{bytes, sampleTime}` into a lock-free SPSC ring per node. A **MIDI-out thread** per platform drains it and calls the OS | OS MIDI calls are not RT-safe (WinMM especially). Same discipline as `PluginScheduleMIDIEvent` |
| D6 | Timing | macOS: `MIDISend` with a `MIDITimeStamp` computed from the sample time (sample-accurate). Linux: ALSA seq queue with a scheduled tick. Windows: WinMM `midiOutShortMsg` sent immediately by a 1 ms-timer thread (jitter ≈ 1 ms, documented) | Each OS's best native option; no new dependency |
| D7 | Latency offset | `offset ms` param (-50..+50) per node | Hardware synths and audio-interface latency differ; users line up by ear. Standard in Ableton's "MIDI clock sync delay" |
| D8 | Virtual port | macOS + Linux: also publish an "Infinite" virtual source. Windows: none (WinMM can't) | Lets DAWs/other apps receive notes on mac/Linux. Windows users use loopMIDI; say so in help |
| D9 | Hanging notes | Track open notes per node; send note-offs on delete, bypass, device change, transport stop, patch close. Panic = CC123 + CC120 on all 16 ch | The most common bug class in MIDI out (`audio-node-sweep` teardown rules) |
| D10 | Device identity | Save device **name** (not handle), rebind by name on load, like input's `MidiRebindStaleDevice` | Handles change per launch on every OS |
| D11 | Out of scope v1 | MPE out, NRPN, SysEx, pitch-bend/aftertouch out, MIDI 2.0 | Can follow once v1 is proven on real hardware. Pitch bend is the first candidate |

## Platform API (new, 3-sided)

```
Platform::MidiOutListDevices() -> vector<string>
Platform::MidiOutOpen(name, outError) -> MidiOutHandle*      // main thread
Platform::MidiOutClose(handle)
Platform::MidiOutSend(handle, bytes, len, hostTimeOrSampleTime) // MIDI-out thread only
Platform::MidiOutVirtualAvailable()
```
Each one needs macOS, Windows and Linux implementations in the same change (`AGENTS.md` rule 3).

## Node

- Category Utility, pins: `notes` in. Optional `clock` toggle; CC rows draw as knobs (pillar grid).
- Body: device dropdown, channel, offset ms, clock on/off, 4 CC rows, a small activity LED, Panic button.
- Wiring sites: per `new-audio-node` (two-object rule: main-thread node + `AudioNode` that consumes the note inbox).
- Help tables (`project-node-help-coverage`: 4 hand-kept tables + per-OS `#if`).

## Tests
- Headless: inject notes → assert byte stream + timestamps through a fake `MidiOutSend` sink (no hardware in CI).
- `AUDIOTEARDOWNSWEEPTEST`: delete mid-note → note-off emitted.
- Clock: 120 BPM for 4 bars = 384 pulses ±0; start/stop/SPP order.
- Manual: one real synth per OS before the release notes claim it.

## Phases
1. Platform out API ×3 + MIDI-out thread + notes + channel + panic.
2. CC rows + clock out + offset.
3. Virtual ports (mac/Linux), help, sweeps.
