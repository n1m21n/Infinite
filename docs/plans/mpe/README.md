# MPE input (R597, proposed for v0.4.8)

Status: APPROVED for v0.4.7 (owner, 2026-10-07). Open question closed: no MPE hardware is assumed; correctness is
proven by the synthetic-stream fixture, and a hands-on pass is a post-release follow-up for whoever owns a controller.

## Goal
A user with an MPE controller (Seaboard, Osmose, Linnstrument, Push) plays a chord and bends, presses and slides
each note on its own. Normal keyboards behave exactly as today.

## What MPE is, on the wire
- Lower zone: master channel 1, member channels 2-8 (or 2-15). Upper zone mirrors from channel 16.
- Each held note sits on its own member channel, so channel-level messages become per-note:
  pitch bend (default range +-48 semitones on members), channel pressure, CC74 (slide).
- Zone setup arrives as RPN 6 (MPE Configuration Message). Many devices just send on member channels with no RPN.

## Today (verified in code before writing)
- NoteEvent (src/audio/NoteEvent.h) carries note, velocity, isNoteOn, frameOffset, source. No channel, no per-note expression.
- Pitch bend and channel aftertouch are bindable controls on all three platforms (Platform.mm, win/MidiWin.cpp,
  linux/MidiLinux.cpp, wired in main.cpp). Whole-keyboard only.

## Design
1. MIDI layer (3 platforms): keep the channel on note-on/off and on bend/pressure/CC74 messages. New MPE switch on the
   MIDI input node, default OFF. With it off nothing changes.
2. NoteEvent gets `channel` (0-15). Note-offs still match by (source, note); the channel is extra, not the key.
3. Per-note expression state: small fixed table keyed by (source, note) holding bend, pressure, slide. Written by the
   MIDI thread, read by voices through atomics. No allocation, no locks, no dynamic_cast in ProcessBlock
   (two-object audio rule).
4. Voices (Oscillator, Wavetable, Sampler first): read their note's bend as a pitch offset (range param, default 48 st,
   user-settable), pressure and slide as modulation sources routed through the existing modulation system.
5. UI: three new bindable sources beside pitch bend/aftertouch ("Note bend", "Note pressure", "Note slide"), the MPE
   switch and a bend-range control on the MIDI input node. Nothing else new.

## Out of scope
MPE output, MIDI 2.0, zone reconfiguration UI, per-note expression on every node (start with the three voices).

## Tests
- Fixture: feed a synthetic MPE stream (two notes, different channels, one bent) offline and assert only that voice's
  pitch moves; note-off on one channel does not release the other.
- MPE switch off: bend on a member channel behaves exactly as the current whole-keyboard bend (regression).
- Param round trip and teardown sweeps for any new param; windows-parity and linux-parity reviewed.
- Hands-on pass with a real controller is a follow-up, not a release gate (fixture is the gate).

## Effort and order
M-L. R555 is not done, so builds stay slow; accept it. Skills to load: new-audio-node, modulation-sweep,
audio-pipeline-sweep, windows-parity, linux-parity, node-ui-pillars.
