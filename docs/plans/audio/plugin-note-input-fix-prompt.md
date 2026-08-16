# Fix: AU plugins (Serum, Vital, any instrument/music-effect AU) receive incomplete and corrupted note data

## Context you need before starting

The patch is `Note Sequencer -> Audio Plugin (AU instrument) -> Audio Out`. The
symptom is that the plugin ends up producing one constant, sustained sound: the
notes clearly arrive, but the result is a drone rather than a sequence. Softer
synths (the built-in Wavetable, Sampler) driven by the same sequencer behave
correctly, which localises the bug to the plugin node's note bridge rather than
to the producers.

The entire note->AU bridge is 17 lines: `src/nodes/AudioPluginNode.cpp:46-62`
inside `AudioPluginAudioNode::ProcessBlock`, feeding
`Platform::PluginScheduleMIDIEvent` at `src/platform/Platform.mm:3859`. Read
both before changing anything.

The reference for what a *correct* `NoteEvent` consumer does is
`src/nodes/WavetableSynthCore.h:305-315` and `src/nodes/SamplerNode.cpp:279-290`.
The plugin bridge does not follow that shape, and that is the root of most of
what is below.

### Working-tree changes that are already applied — do not redo them

`git status` shows three uncommitted files. These are earlier fixes to this same
bug and are **already correct**; build on top of them:

- `src/platform/Platform.mm:3865` — `schedule()` now uses
  `AUEventSampleTimeImmediate + frameOffset` instead of a bare `frameOffset`.
  That is the documented way to schedule relative to the current render cycle;
  it is right, leave it.
- `src/nodes/AudioPluginNode.cpp:49-50` — the single `Pop(evts, 64)` became a
  `while (Pop(...) > 0)` drain loop, so a block with more than 64 events no
  longer strands the remainder. Right, but see item 4 — it is now also the thing
  that delivers a whole backlog in one block.
- `src/nodes/NoteNodes.cpp` (three sequencer classes, ~lines 1898, 2349, 2539) —
  the pending gate-off test dropped the `mPendingOffSample >= mSamplePos` lower
  bound, so a note-off whose sample time had already passed now fires at offset
  0 instead of being silently discarded forever. That was a real stuck-note bug
  and is fixed.

Everything below is what is *still* wrong.

---

## The changes

### 1. `bendUpdate` events are being sent to the plugin as note-offs

`src/nodes/AudioPluginNode.cpp:54`:

```cpp
const unsigned char status = (unsigned char)((evts[i].isNoteOn ? 0x90 : 0x80));
```

`NoteEvent::bendUpdate` (`src/audio/NoteEvent.h:40-43`) means "update this held
note's bend — this is *not* a real note-on/off, and `isNoteOn` is false when it
is set." Every other consumer branches on it before falling through to note-off
(`WavetableSynthCore.h:309`, `SamplerNode.cpp:283`). This bridge does not, so
**every bend update is translated into a note-off that kills the plugin's voice.**

Producers that emit these: the Pitch Bend / Note Modify family in
`src/nodes/NoteNodes.cpp` — see the `out.bendUpdate = true` sites at lines 635,
762, 789, and 2930.

Fix: branch on `bendUpdate` first and handle it as bend (item 2), never as
note-off.

### 2. `NoteEvent::bendSemitones` never reaches the plugin at all

Even on a plain note-on, `bendSemitones` (`src/audio/NoteEvent.h:40`) is dropped
on the floor — the plugin only ever sees the integer note number. So a Pitch
Bend node upstream of an AU currently does nothing except kill notes (item 1).

**This one has a genuine design choice in it, and I did not resolve it — read
this before implementing.** `NoteEvent`'s bend is *per-voice* (keyed by
`voiceId`). MIDI pitch bend is *per-channel*. The three options:

- **(a) MPE**: one MIDI channel per active voice (channels 1-15), bend sent on
  that voice's channel. Fully correct for per-voice bend. Requires the plugin to
  be in MPE mode — Serum and Vital both support it, but it is off by default, so
  this silently does the wrong thing for a user who has not enabled it.
- **(b) Single-channel, single-voice-guarded** — *my recommendation*. Send
  channel-0 pitch bend (`0xE0`) reflecting the bend of the most recently started
  voice, and only when exactly one voice is held. With two or more voices held,
  hold the bend at 0 rather than smearing one voice's bend across the chord.
  This is correct for the mono lead/bass case that per-voice bend is actually
  used for, and is honestly wrong-but-inaudible elsewhere.
- **(c)** Round the bend into the note number at note-on and ignore continuous
  updates. Cheapest, but throws away exactly the feature the field exists for.

Go with (b) unless you find a reason not to, and put a comment on the chosen
approach explaining why the other two were rejected — the next person reading
this will ask.

**Pitch-bend range**: MIDI's default is ±2 semitones, which is far too narrow
for a note chain that can carry arbitrary `bendSemitones`. Either clamp to ±2
and scale accordingly, or send RPN 0 once after prepare (CC 101=0, CC 100=0,
CC 6=range, CC 38=0) to widen it. **I did not verify that Serum and Vital honour
RPN 0 through the AU bridge** — check this before relying on it, and fall back to
clamping at ±2 if it does not take.

### 3. A velocity-0 note-on is a note-off

`src/nodes/AudioPluginNode.cpp:56-57` clamps velocity to `[0, 127]` with no
floor. A note-on with `velocity <= 0.003` rounds to 0, and `0x90 note 0` is the
standard running-status spelling of a note-off — every synth including Serum and
Vital treats it as one. The codebase already knows this convention: see the
incoming-MIDI parser at `src/platform/Platform.mm:2874-2881`, which deliberately
converts `0x90` velocity 0 into a note-off.

Fix: floor note-on velocity at 1. Leave note-off velocity alone (0 is fine there).

### 4. The inbox is not drained while the plugin is loading or bypassed — this is the most likely direct cause of the reported symptom

`src/nodes/AudioPluginNode.cpp:36-40`:

```cpp
if (handle == nullptr || mBypass.load(std::memory_order_relaxed))
{
   PassThrough(in, output);
   return;          // <-- returns BEFORE the note drain at line 46
}
```

The producer keeps pushing into the queue the entire time. `NoteEventQueue` holds
256 events (`src/audio/NoteEventQueue.h:36`) and its overflow policy
(`NoteEventQueue.h:44-58`) **drops note-ons and force-overwrites the oldest
unread slot to make room for note-offs** — so once the ring fills, real note-offs
start destroying other real note-offs.

Serum and Vital are large, slow-instantiating AUs (`CookIfNeeded` polls
`Platform::PluginPoll` frame after frame precisely because of this —
`AudioPluginNode.cpp:410-427`). A sequencer running during those seconds
overflows the ring many times over. Then, on the first block after the handle is
published, the new `while (Pop(...))` loop drains the *entire* surviving backlog
and schedules all of it into that one block. What arrives at the plugin is a pile
of note-ons whose matching note-offs were destroyed by the overflow policy —
i.e. a stack of permanently stuck voices. **That is one constant sound.**

The same thing happens on every bypass toggle, and in the unpublish window during
a rate re-prepare (`AudioPluginNode.cpp:445`).

Fix: always drain the inbox, unconditionally, at the top of `ProcessBlock` —
before the `handle == nullptr || bypass` early-out. When there is no handle or
the node is bypassed, drain and **discard**. The queue must never be allowed to
back up. Keep this allocation-free and lock-free; it is the audio thread.

### 5. Nothing ever sends an all-notes-off, so held notes stick forever

I grepped `src/` for `0x7B`, `AllNotesOff`, and `Panic`: no hits. The only `0xB0`
in the tree is the *incoming* CoreMIDI parser at `src/platform/Platform.mm:2854`.

So a note that is held when any of these happens sustains indefinitely:

- the note cable is disconnected — `main.cpp:15035` calls `SetNoteInbox(nullptr)`
  and the plugin never hears the off;
- the node is bypassed mid-note (item 4's discard path makes this worse, not
  better, unless you also flush);
- the plugin is swapped or unloaded — `AudioPluginNode.cpp:213` and `:267`;
- the rate re-prepare unpublishes the handle — `AudioPluginNode.cpp:445`;
- the sequencer stops mid-gate.

Fix: add a flush that sends `0xB0 0x7B 0x00` (All Notes Off) and
`0xB0 0x78 0x00` (All Sound Off) on every channel you use, and call it from the
main-thread half at each of the four sites above, plus from the audio half when
the drain is entering discard mode with voices outstanding.

Note the deliberate "no `Reset()` override" comment at
`AudioPluginNode.cpp:77-79`. Its reasoning — that dropping the handle in `Reset`
would silently unload the plugin — is correct and should stay. It is *not* an
argument against flushing notes. If you add a `Reset()` override, it must flush
notes and leave `mHandle` alone; say so in the comment so the next reader does
not undo it.

### 6. `frameOffset` is not clamped to the current block

`src/nodes/AudioPluginNode.cpp:59` passes `evts[i].frameOffset` straight through.
Serum and Vital ship as AUv2 components reached through `AUAudioUnitV2Bridge`,
which forwards this as `inOffsetSampleFrame` to `MusicDeviceMIDIEvent`; an offset
past the end of the slice is not defined to do anything useful and is commonly
dropped. Combined with item 4's stale backlog, offsets well past `numFrames` are
reachable today.

Fix: clamp to `[0, output.numFrames - 1]` at the call site. The producers already
clamp their own (the working-tree `NoteNodes.cpp` change does exactly this), but
the consumer must not trust them.

---

## Explicitly out of scope

**Do not** extend `NoteEvent` to carry MIDI channel, CC, aftertouch, poly
pressure, or program change. The user's ask was "make sure plugins receive all
kinds of note data", and the honest answer is that the achievable set is bounded
by what `src/audio/NoteEvent.h` can express: note-on, note-off, and per-voice
bend. Widening that struct means touching every producer and every consumer in
`NoteNodes.cpp`, `WavetableSynthCore.h`, `SamplerNode.cpp`, `GranularNode.cpp`
and more — a separate, much larger change that should be scoped on its own.
Fix the six items above, which make the data the struct *already carries* arrive
intact.

Also leave the VST3 path alone; this is the AU bridge only.

---

## Verification

Extend the existing headless fixture rather than writing a new one.
`RunPluginScanTest` lives at `src/main.cpp:20139` and already covers the whole
Objective-C plugin boundary — enumerate, instantiate, prepare, render, params,
save/load round trip. It deliberately picks an *effect* AU (`au:aufx:dely:appl`,
AUDelay) because effects are predictable to assert on.

Add a second half that picks an **instrument**. `EnumerateAudioUnits` does
include them — `IsHostablePluginType` at `src/platform/Platform.mm:3207` admits
`kAudioUnitType_MusicDevice` and `kAudioUnitType_MusicEffect`, and
`ComponentTypeAcceptsNotes` at `:3213` is what sets `desc.acceptsNotes`. Select
by identifier prefix `au:aumu:` with an `:appl` suffix (Apple's DLSMusicDevice
ships on every macOS install). **Print the enumeration and read the exact
identifier rather than hardcoding one** — `MakeAuIdentifier` at `:3171` builds it
from raw FourCCs, so the DLS subtype carries a trailing space (`'dls '`) and is
easy to get wrong by guessing.

Then, driving a real `NoteEventQueue` the way `main.cpp:16478` and `:19173`
already do, assert:

1. note-on -> rendered output goes non-silent;
2. matching note-off -> output decays to silence within a bounded number of
   blocks (this is the regression test for the stuck note);
3. a `bendUpdate` event between them does **not** silence the output (item 1);
4. events pushed while `mHandle == nullptr` do not survive to be replayed once
   the handle is published (item 4);
5. a note held across `Unload()` does not leave the plugin sounding (item 5).

One thing I did not confirm: `PluginPrepare`'s bus negotiation
(`src/platform/Platform.mm:3495-3515`) treats `negotiatedIn == 0` as acceptable
and only `negotiatedOut == 0` as fatal, which *looks* correct for an instrument
with no input bus — but no instrument has ever been run through it, since
`PLUGINSCANTEST` only ever chose effects. Verify it actually prepares an
instrument cleanly before assuming the note assertions above are testing what you
think they are.

Finally:

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Confirm it compiles clean, then run both:

```bash
.claude/skills/audio-node-sweep/driver.sh
```

```bash
INFINITE_PLUGINSCANTEST=1 ./build/Infinite.app/Contents/MacOS/Infinite
```

`AudioPluginNode` carries a documented `AUDIOPARAMSWEEPTEST` baseline (see the
class comment at `src/nodes/AudioPluginNode.h:27-42` on why its mapped params
bypass `ParamMailbox`); if the sweep's expected-failure list needs updating
because of a change you made, update it deliberately and say why — do not just
widen it until it passes.
