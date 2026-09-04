# Step 21: Field Synth node (note-driven synthesis)

> **Handoff note:** this doc is written to be executable by an AI agent with
> no other context than this repository. It cites real file:line locations
> confirmed by direct investigation as of commit `58acad2` on branch
> `bugfix/field-device-ui-and-cable-fixes`. If line numbers have since
> drifted, re-locate by the named function/symbol, not the raw number.
>
> This is workstream 3 of 3 in a related set — see also
> [`step-19-sample-delay-line.md`](step-19-sample-delay-line.md) and
> [`step-20-field-primitive-node.md`](step-20-field-primitive-node.md).
> This is the largest and riskiest of the three (recovered code needs real
> re-verification, not just a straight port) — a reasonable step to
> implement last, and it benefits from step 19 landing first if a
> delay-based synth preset (Karplus-Strong) is wanted.

## Context

Field currently has "Field Effect" (`FieldSampleNode`, category
"AudioEffects") — audio-in/audio-out only, no note input. It used to be
named "Field Sample" and had a note-driven voice/synth engine, which was
deliberately removed to narrow its scope to effects only. The user now wants
that capability back as its **own, separate node type** — "Field Synth":
accepts note events, emits audio.

## Critical fact: the removed voice/note code is recoverable right now

At the time this doc was written, the old note/voice engine's removal from
`FieldSampleNode` was **an uncommitted diff sitting in the working tree on
top of HEAD** (`58acad2`) — not yet committed. **If it has since been
committed**, find it instead via `git log --follow -p -- src/nodes/
FieldSampleNode.cpp` and look for the commit that removed `VoiceAllocator`/
`NoteCable`/`SetNoteInbox` from that file, or `git log --all
--diff-filter=M -- src/nodes/FieldSampleNode.cpp` around the time "Field
Sample" was renamed to "Field Effect". **Try the uncommitted-diff path
first**, since it's the simplest:

```bash
git diff HEAD -- src/nodes/FieldSampleNode.h src/nodes/FieldSampleNode.cpp
```

If that returns nothing (meaning it's already been committed since), fall
back to the `git log` search above. Either way, **capture the recovered
source before editing `FieldSampleNode.cpp` for any other reason** — save it
to a scratch file so it survives regardless of what else happens to the
working tree.

What that removed code contains (confirmed by direct reading at the time
this doc was written): a constructed `VoiceAllocator mVoices(16)`, per-voice
`float mStateCur[16][64]`/`mStateNext[16][64]` state banking with
reset-on-`NoteOn`, `SetNoteInbox`/`MidiNoteToHz` helpers, a `NoteCable
noteInput` pin, and a deleted **"Polyphonic FM Bell"** preset that used
`freq`/`gate` directly via phase-accumulator FM synthesis. This is the
ready-made starting template for `FieldSynthNode` — adapt it into the new
node rather than writing voice-allocation code from scratch.

**Re-verify before trusting it as-is.** At the time of investigation,
re-reading this exact diff found that `mVoices.NoteOn(...)` *was* correctly
gated inside the `isNoteOn` event-processing branch — i.e. a suspected
"envelope never activates" defect was not visibly reproduced in this
snapshot as written. Don't assume a bug is present just because this code
was once removed; re-test the recovered code's actual behavior once it's
wired into the new node, and only fix what's actually observed to be broken.

## Why the sample-domain compiler needs zero changes for this

`freq` and `gate` are **already fully implemented and working** reserved
sample-domain identifiers, confirmed end to end:

- Reserved at `src/core/field/BackendRegister.cpp:29`.
- Compile to `SampleOp::LoadFreq`/`LoadGate`
  (`src/core/field/BackendRegister.cpp:194-204`).
- Executed by `src/core/field/SampleRuntime.h:58-59`, reading
  `SampleRuntimeInput::freq`/`::gate` (`SampleRuntime.h:29-39`).
- That struct's header comment explicitly documents the intended per-voice
  contract: *"the caller fetches them fresh for each voice, from that
  voice's own note-on frequency and held state... stateCur/stateNext are
  per-voice: the caller passes that voice's own state banks."*

This means Field Synth needs **zero `BackendRegister.cpp`/`SampleRuntime.h`
changes** — the compiler-side contract for per-voice `freq`/`gate`/state was
already designed for exactly this use case and has been sitting unused since
the note engine was stripped out of Field Effect. Only an `AudioNode`-side
`ProcessBlock` is needed that actually populates these per voice — which is
exactly what the recovered removed code already did.

## Design: new node type, template on `GrainMolderNode`

Create `FieldSynthNode` (new `.cpp`/`.h`) as its own node type, reusing:

- `VoiceAllocator`/`Envelope` from `src/audio/AudioVoice.h` (159 lines) —
  the standard fixed-size voice-stealing polyphonic allocator + 4-stage ADSR
  envelope already used elsewhere in this codebase.
- `NoteCable`/`NoteEventQueue`/`INode::NoteInputSlot`: `src/core/NoteCable.h`
  (typed cable, mirrors `AudioCable`); `src/audio/NoteEventQueue.h`
  (lock-free SPMC ring, capacity 256, `RegisterConsumer()`/`Push()`/
  `Pop(cursor, out, max)`); `INode::NoteInputSlot(int slot)`
  (`src/core/INode.h:186`, default returns nullptr, override to expose a
  note pin) — wired automatically by the existing `RebuildAudioTopology` pass
  in `main.cpp` (~26160-26233), which is already generic over any node
  implementing `NoteInputSlot`. **No changes needed there** for a new node
  type.
- `GrainMolderNode` (`src/nodes/GrainMolderNode.cpp/.h`) as the reference
  implementation of correct `VoiceAllocator`+`NoteEventQueue` usage in this
  codebase: note pin at slot 0, audio pin at slot 1, per-block event pop +
  per-sample frameOffset walk, `NoteOn`/`NoteOff` into the allocator,
  per-voice synthesis loop gated on `IsVoiceActive`. Use this as the
  template for `AudioFieldSynthNode::ProcessBlock`'s control-flow shape —
  **not** the older recovered `FieldSampleNode` diff's control flow. (The
  recovered diff is the right source for the *sample-domain-kernel-per-voice*
  wiring specifically — i.e. how to feed compiled `freq`/`gate`/per-voice
  state into the kernel VM — but `GrainMolderNode` is the more current/
  correct template for the surrounding note-event/voice-allocation
  plumbing.)

Compile-swap semantics should follow the same
`SampleSlotT<Field::SampleProgram>` pattern already used in Field Effect
(main thread `Push()`s on preset/edit change, audio thread `SwapIn()`s once
at the top of `ProcessBlock`) — this part is domain-generic and needs no new
machinery.

## New presets to invent for Field Synth

At minimum, recover and re-verify the "Polyphonic FM Bell" preset from the
recovered code as a first working example (phase-accumulator FM using
`freq`/`gate` directly). Beyond that, design a small set of genuinely
synth-flavored presets — things a note-driven voice should do that an effect
can't:

- A basic subtractive-style tone (saw/square approximation + simple filter
  via `state`-based one-pole, since the sample domain has no native
  waveform generators beyond what's expressible in the kernel language
  itself)
- A simple pluck/Karplus-Strong-style voice (natural fit given delay-line
  access from step 19, if that's landed first)
- An FM pair
- A simple pad (slow attack/release via the existing `Envelope` class rather
  than kernel-side envelope math)

Confirm which of these depend on step 19's `delay()` intrinsic
(Karplus-Strong does) and sequence accordingly if that ordering matters.

## Files to touch

- New: `src/nodes/FieldSynthNode.h`/`.cpp` — adapted from the recovered note/
  voice code (see "Critical fact" above) plus `GrainMolderNode`'s
  note-plumbing shape.
- `src/main.cpp` — `REGISTER_NODE(FieldSynthNode, "Field Synth", "Synths")`
  (or match whatever category convention the old "Field Sample"/"Synths"
  registration used — check `main.cpp` for it) plus the same class of
  dispatch sites listed in
  [`step-20-field-primitive-node.md`](step-20-field-primitive-node.md)
  (node-body draw dispatch, a `DrawFieldSynthParams` function, post-load
  `Apply()` dispatch, `.infdev` domain-string dispatch with its own domain
  string e.g. `"synth"`, drop-target lookup).
- `CMakeLists.txt` — add the new `.cpp`.

## Verification

1. Build (`cmake --build build -j 8`).
2. Wire a MIDI/note-source node into Field Synth's note pin and its audio
   output into an audio-out node.
3. Confirm polyphony works: multiple simultaneous notes produce multiple
   simultaneous voices, correctly stolen when exceeding 16.
4. Confirm the "Polyphonic FM Bell" preset (or its re-verified equivalent)
   produces actual pitched tone tracking `freq` correctly across the
   keyboard range, and that note-off correctly releases the envelope rather
   than cutting off abruptly (unless the preset explicitly wants a hard
   cutoff).
5. Deploy to `~/Desktop/Infinite.app` after a successful build and manual
   check, per this project's existing convention.
