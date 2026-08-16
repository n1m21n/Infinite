# Prompt: convert Modulators/Envelope into a mod-signal shaper

In /Users/namansoni/infinte. Read this whole file before writing code.

## Goal

`EnvelopeNode` (palette: **Modulators / Envelope**) currently consumes
note events and needs an audio-thread node to do it. Convert it into a
pure **modulator-in → modulator-out shaper**: an ADSR contour applied to
an incoming modulation signal, with no note input, no pulse mode, and no
audio node.

The motivating case: an LFO modulating a frequency, where you want the
LFO's swing to follow an ADSR shape rather than being constant.

## Scope — read this before touching anything

**Only `EnvelopeNode` changes.** The word "envelope" appears in several
unrelated places in this codebase and none of them are in scope:

- `Envelope` in `src/audio/AudioVoice.h` — the per-voice DSP envelope used
  by synth voices. **Do not touch.** Its self-test at
  `src/main.cpp:15095-15145` must keep passing untouched.
- `DrawEnvelopePanel` / `DrawEditableADSR` — the shared interactive ADSR
  widget used by the wavetable engines. **Do not touch it — reuse it.**
- Any envelope inside `src/audio/EffectDefs.cpp` or the synth modes.

`AudioEnvelopeNode` (`src/nodes/NoteNodes.cpp:264`) is **only** used by
`EnvelopeNode` — verified, its declaration is at `src/nodes/NoteNodes.h:15`
and its sole construction sites are `NoteNodes.cpp:379/397/404`. So it can
be deleted with the note input. Re-verify this with grep before deleting.

## Verified current state

- `EnvelopeNode` — `src/nodes/NoteNodes.h:94-133`. Has `NoteCable noteInput`,
  `AudioNodeForNotePorts()`, `mAudioNode`, `trigger` (kTriggerNote /
  kTriggerPulse), `rateDiv`, `gatePct`, and `attackMs` / `decayMs` /
  `sustainLevel` / `releaseMs`.
- Registered `REGISTER_NODE(EnvelopeNode, Envelope, "Modulators");` —
  `src/main.cpp:2323`.
- Body drawn by `DrawEnvelopeBody`, `src/main.cpp:7501-7543`. It already
  uses `DrawEnvelopePanel("envelope - drag the handles", ...)` — the
  interactive draggable-handle ADSR widget. **Keep that; it is exactly the
  "interactive curve" this node should have.** Delete only the pulse-mode
  row (`trigger` dropdown, `rate` dropdown, `gate` knob) below it.
- Note-pin slot comment at `src/main.cpp:2671` mentions Envelope by name —
  check whether removing the note input affects that unified-slot logic.

## The design — how ADSR applies to a continuous signal

An ADSR needs a gate; a mod cable carries a continuous value. Resolve it
like this:

**Gate = the input signal crossing a threshold.** Rising through
`threshold` starts attack→decay→sustain; falling back below it starts
release. With an LFO patched in, the LFO retriggers the envelope once per
cycle, and each cycle's swing is shaped by the ADSR — which is the
requested behaviour.

**Output = the input scaled by the envelope level, around centre:**

```cpp
out01 = 0.5f + (in01 - 0.5f) * envLevel;
```

At `envLevel == 0` the output sits at 0.5 (the modulator has no say); at
`envLevel == 1` the input passes through unchanged. This is deliberately
the same convention as `ModDepthNode` — see its comment at
`src/nodes/ModulatorNodes.h:276-285`, which explains why collapsing toward
0.5 is what "no modulation" has to mean given that a binding overrides its
destination. It also means the node reads correctly under bipolar
bindings (`00-modulation-polarity.md`).

**Rejected alternative**, for the record: outputting the raw envelope
level and ignoring the input. That makes the node an envelope *generator*
with a trigger input, not a shaper, and it discards the input signal —
not what was asked for.

## What to build

Strip from `EnvelopeNode`:

- `NoteCable noteInput`, `NoteInputSlot()`, `InputLabel()` for notes,
  `AudioNodeForNotePorts()`, `mAudioNode`, `mLastCookFrame`
- `enum Trigger`, `trigger`, `rateDiv`, `gatePct`
- the whole `AudioEnvelopeNode` class

Add, following `ModDepthNode` (`ModulatorNodes.h:286-310`) as the
structural template:

```cpp
IModulator* input = nullptr;
IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
int ModulatorInputCount() const override { return 1; }
const char* InputLabel(int) const override { return "in"; }
float constantIn = 0.5f;   // used when nothing is patched
float threshold = 0.5f;    // rising edge above this = gate on
```

Keep `attackMs`, `decayMs`, `sustainLevel`, `releaseMs` with their
**existing `VisitParams` keys unchanged**, so those values survive in
already-saved patches.

`Value01()` now runs the ADSR on the main thread (per-frame, driven off
`Transport`/frame time — no audio thread involved). Reuse the DSP shape of
`src/audio/AudioVoice.h`'s `Envelope` if you can do so without modifying
it; otherwise write a small self-contained ADSR in the node. Do not
refactor `AudioVoice.h` to share code — that file is out of scope.

Whether the node stays in `NoteNodes.h/.cpp` or moves to
`ModulatorNodes.h/.cpp` is your call once it has no note dependencies;
moving it is tidier but touches `CMakeLists` and includes. Say which you
chose.

Control count after this: A, D, S, R, threshold, constantIn — six, plus
the interactive curve. Add nothing else.

## Patch compatibility — state this in your summary

Existing patches with a note cable into Envelope will **lose that cable**
on load (the note input no longer exists), and the node will fall back to
`constantIn`. That is an accepted, intentional break. Verify the loader
drops the orphaned `note` record gracefully rather than erroring or
crashing — check `src/core/Patch.cpp:330-341` and the note-cable rebuild
path in `main.cpp`. If it doesn't degrade cleanly, make it.

## Exit criteria

1. `cmake --build build -j"$(sysctl -n hw.ncpu)"` clean, and grep confirms
   `AudioEnvelopeNode` has no remaining references.
2. The `Envelope` DSP self-test at `src/main.cpp:15095-15145` still passes,
   unmodified.
3. LFO → Envelope → a filter cutoff: the cutoff's swing visibly grows and
   decays on the ADSR contour, retriggering once per LFO cycle.
4. Nothing patched in: output holds steady, driven by `constantIn`.
5. A/D/S/R values from a pre-existing saved patch load unchanged.
6. Loading an old patch that had a note cable into Envelope does not crash
   and does not corrupt the rest of the patch.
7. Spawn / wire / delete during playback — no crash, no dangling cable.
8. Run `/audio-node-sweep`, then `/run-infinite-hygiene`.
9. Update the Envelope help text (grep `"Envelope"` in the two description
   lists near `src/main.cpp:12701` and `13130`) — it currently describes a
   note-driven envelope.
