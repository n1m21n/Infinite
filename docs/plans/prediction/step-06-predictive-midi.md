# Prediction step 6: the Predictive MIDI node (source A, Learn from notes)

A note node that captures a wired note chain, learns it (README §8.2), and plays standalone in that
style. Source B ("follow my movement") is **not** in this step.

Line numbers are from commit `35221c1`; re-grep the symbol if one has drifted.

## Start

Prereq: step 1 merged (it can run in parallel with steps 3–5; it does not need the Drift node).

```bash
git switch main && git pull --ff-only
git switch -c feature/prediction-step-06-predictive-midi
```

Skills: **`new-audio-node`** (the two-object rule, wiring sites, exit criterion; follow it exactly),
`audio-node-ui`, `node-ui-pillars`, `audio-node-sweep`, `audio-pipeline-sweep`.

## Files to read first

| File / symbol | Why |
|---|---|
| `src/audio/NoteEvent.h` `NoteEvent` (`note`, `velocity`, `isNoteOn`, `frameOffset`, `source`, `voiceId`, `bendSemitones`, `bendUpdate`), `NextVoiceId()` | everything there is to learn from, and how to emit |
| `src/core/INode.h:62` `INoteSource` | the note-producer contract |
| `src/nodes/NoteNodes.h:673` `ArpeggiatorNode` (+ `AudioArpeggiatorNode`), `:198` `NoteFilterNode` | closest shapes: note in → notes out, and the main/audio object split |
| `src/audio/AudioNode.h:162` `NoteOutbox()`, `:182` `SetNoteInbox()` | how notes enter and leave the audio object |
| `src/core/Transport.h` `Beats()`, `BeatsPerBar()`, `Tempo()` | onsets in beats (`frameOffset` is only within the block) |
| `src/audio/ParamMailbox.h` | knob values only; **not** for tables |
| [README.md](README.md) §2.5 (threads), §8.2–§8.4 | the model, meter and controls |

## What to build

### 6.1 Two objects (per `new-audio-node`)

- `PredictiveNotesNode : INode, INoteSource` (main thread): params, Learn state, UI, table ownership.
- `AudioPredictiveNotesNode : AudioNode` (audio thread): in Learn mode it passes input notes through and
  **captures** them; in Play mode it samples from the current tables and emits `NoteEvent`s scheduled by
  `frameOffset`.

### 6.2 Capture path (audio → main)

The audio object pushes `{absSample, note, velocity, isNoteOn, bend}` into a preallocated SPSC ring
(4096 entries; drop and count on overflow). The main thread drains it each frame and converts to beats
with `Transport` (`absSample = blockStart + frameOffset`).

### 6.3 Model (worker thread, README §2.5)

Multiple-viewpoint variable-order Markov, blended PPM-style over orders 0…Memory, one model per
viewpoint (README §8.2 table). The build runs on the step-1 worker thread (or its own) when Learn
stops or a plateau is detected.

**Tables:** fixed-capacity flat arrays (context hash → counts), sized at Learn time, never grown on
the audio thread.

### 6.4 Tables to audio: double buffer

```
worker builds Tables* next  →  main: std::atomic<Tables*> live.exchange(next)  →  old pointer queued
audio reads live.load(acquire) once per block
main frees the retired table a frame later, after the audio thread has acknowledged a new block
```

Never free on the audio thread. Never send tables through `ParamMailbox`.

### 6.5 Sampling (audio thread)

At each onset: blend the order distributions → apply **Stray** `p_i ∝ p_i^(1/T)`, renormalised. At the
bottom of the Stray knob (T < 0.1), switch to **longest-exact-context replay**, not greedy argmax
(README §8.4). Clamp to Range. Duration and velocity get their spreads. RNG is a per-node xorshift,
seeded from a param, with no allocation.

### 6.6 Learn meter

On the main thread, after each captured bar: the held-out 20% cross-entropy vs order 0, drawn as a
small curve. The plateau rule (3 bars with < 2% improvement) stops Learn. Defaults: 8 bars or 64 notes,
whichever is later.

### 6.7 Controls (6)

Learn (button), Stray, Memory, Length spread, Velocity spread, Range. Params go through `ParamMailbox`.
The learned tables are saved with the patch through `VisitParams` `Text` (base64, versioned), so a
loaded patch plays without re-learning.

## Traps

| Trap | Why |
|---|---|
| Onsets from `frameOffset` alone | It resets every block. Use block start + offset → beats. |
| Allocating in Play | Tables are fixed-size; the RNG and scratch are members. |
| Freeing tables on the audio thread | The main thread frees them, one frame later. |
| Note-off matching by pitch | Use `voiceId` (`NoteEvent.h` comment): stamp the note-on id on its note-off. |
| Greedy decoding as "replay" | It loops on the likeliest short cycle. |

## Test: `INFINITE_PREDMIDITEST`

1. Feed a 2-bar deterministic loop ×2 → with Stray at minimum, output = the loop, note for note.
2. Feed a random C-major stream (200 notes) → output pitches ⊂ C major at Stray = 1; at Stray max,
   they are uniform within Range.
3. Table swap during playback: no dropout; the old table is freed on the main thread (assert its thread id).
4. Delete the node mid-playback: no crash, no dangling cable (`audio-node-sweep` teardown).
5. Save/load: the learned tables round-trip and the output is identical with the same seed.

## Exit criterion

`PREDMIDITEST` passes; `audio-node-sweep` (`AUDIOPARAMSWEEPTEST`, `AUDIOTEARDOWNSWEEPTEST`) passes;
the `new-audio-node` exit criterion is met.

## Shipped (deviations from the plan above)

- **Pitch is the absolute note**, not interval + register: a learned scale stays inside its scale
  when the walk continues. **No bend viewpoint** was learned.
- **Onset spacing is on a 1/24-beat grid**; spacing 0 means "same onset", which is how chords are learned.
- **The patch saves the training events** (`model`, versioned base64), not the raw tables. Tables are a
  pure function of the events, so a loaded patch rebuilds identical tables and plays without re-learning.
- **Table builds use `std::async`** - no shared worker thread exists yet.
- **Controls**: Learn/Stop button with a held-out gain meter, stray, memory, length, velocity, low, high.
- **Test**: `INFINITE_PREDMIDITEST` (exact replay, in-key sampling, stray extremes, table swap under a live
  audio thread, save/load determinism, the Learn path, plus the improved Random Note / Chorder theory).
- **Add-ons shipped with this step**: Random Note Generator now uses a constrained melodic walk and the
  Chorder uses functional-harmony transitions with minimum-movement voicing (`audio/NoteTheory.h`);
  the Chorder `rate` is a `MusicTime::RateDivisionList()` dropdown (1 bar ... 1/64, dotted, triplet).
  Caveat: swapping the Chorder's rate knob for a dropdown may reorder its param ordinals for existing
  modulation cables.
