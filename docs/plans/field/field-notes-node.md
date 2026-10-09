# Field Notes node: Field script in, note events out

> Status: **design, not built.** Written 2026-10-09 against `feature/ui-canvas`
> @ `f8857acb`. Line numbers drift; re-locate by symbol.
>
> Skills that own the rules this doc leans on: `field-language` (syntax),
> `field-compiler` (backend), `field-integration` (INode wiring),
> `new-audio-node` (note-node shape, wiring sites, exit criterion),
> `rhythmic-quantization-standard` (divisions), `audio-node-ui` +
> `node-ui-pillars` (body). If this doc and a skill disagree, the skill wins.

---

## 1. What it is, in one line

**Field Notes** is a note node whose body is a Field script. The script runs
on the audio thread, sample-accurate, and every `note(...)` call it makes
becomes a real `NoteEvent` on the node's note-out pin. That pin cables into
anything that takes notes today: Field Synth, Wavetable, a plugin, Arpeggiator,
Chorder, Note Echo, Note Merge, another Field Notes.

```
                    ┌──────────────── Field Notes ────────────────┐
  notes in ───────▶ │  noteOn / noteNum / noteVel   (read)        │
  (optional)        │  beat / tick(1/16)            (clock)       │
                    │  param float density = 0.5    (knobs)       │
                    │                                              │
                    │  if (tick(1/16) > 0.5) {                     │
                    │     note(deg(step % 8), 0.8, 1/16)           │ ───▶ notes out ──▶ Field Synth / Wavetable /
                    │  }                                           │                    Arpeggiator / plugin / ...
                    └──────────────────────────────────────────────┘
```

Two jobs, one node, no mode switch:

| Job | Notes-in pin | What the script does | Example |
|---|---|---|---|
| **Generate** | unplugged | reads the clock, decides when to call `note()` | Euclidean rhythm, scale walk, probability melody |
| **Process** | plugged | reads the incoming note, calls `note()` zero or more times | transpose, harmonise, ratchet, filter by velocity |

Both at once is legal (play along with a generated line).

---

## 2. Why this is a small node, not a new language feature

Almost everything already exists. Field Notes is the **sample-domain
register machine** (the same backend Field Synth and Field Effect use) hosted
in a **note-source node shape** (the same shape MIDI Notes, Arpeggiator and
Note Sequencer use).

| Already shipped | Where | Field Notes uses it for |
|---|---|---|
| Sample-domain compiler + interpreter | `src/core/field/BackendRegister.cpp`, `SampleRuntime.h` | running the script per sample, RT-safe |
| `param` → knob + ParamRef | `src/core/field/ParamTable.*` | modulatable knobs from `param` lines |
| `state`, `delay`, `if(c,a,b)`, `if/else` → Select | step 6, 9, 19 docs | memory between samples, branching |
| Note input names `noteOn` / `notePitch` / `noteVel` | `BackendRegister.cpp:30`, OPEN-D decision (`device-catalog-v1.md:38`) | reading the incoming note |
| Note-source node shape | `INoteSource` (`src/core/INode.h:65`), `AudioNode::NoteOutbox()` (`src/audio/AudioNode.h:166`) | the note-out pin |
| `NoteEvent` + `NextVoiceId()` | `src/audio/NoteEvent.h` | each emitted note, matched note-off |
| `NoteEventQueue` (256, never drops a note-off) | `src/audio/NoteEventQueue.h` | outbox; fan-out to many consumers |
| Sample-accurate beat | `Transport::BlockStartBeats()` (`src/core/Transport.h:111`) | `beat` per sample |
| Scale degree → MIDI | `MusicTime::DegreeToNote`, `NoteTheory.h` | `deg()` |
| Hot reload keeps state | step 9 §4 (transplant by name+type) | editing the script mid-play doesn't reset the groove |

**What is genuinely new** (the whole diff, in language terms):

| New | Kind | Domain | Why it can't be written in Field today |
|---|---|---|---|
| `note(pitch, vel, len)` | statement (side effect) | sample | no Field construct can *produce* a note; output names (`out`) are a single float |
| `noteNum` | reserved read name | sample | `notePitch` is Hz (synth convention); note logic wants MIDI numbers. `ftom(notePitch)` would round-trip through float log every time |
| `beat` | reserved read name | sample | no clock name exists in `sample`; `n` is a sample counter, not musical time |
| `tick(div)` | builtin | sample | edge-on-boundary is 4 lines of `state` that every user gets wrong at the loop wrap |
| `deg(d)` | builtin | sample | scale lookup needs the node's Root/Scale; not expressible as arithmetic |

No sixth domain. This respects the owner's OPEN-D ruling ("folded into
existing reserved sets, not a sixth `note` domain") and Field's one-primitive
rule: the kernel is still *a body run once per sample*; `note()` is just
something that body can do.

---

## 3. The language surface

### 3.1 Reads (reserved, sample domain)

| Name | Value | Notes |
|---|---|---|
| `noteOn` | `1.0` on the sample an input note-on lands, else `0.0` | existing; edge, not held |
| `noteNum` | that note's MIDI number, `0..127` | **new**; holds after note-off, like `notePitch` |
| `notePitch` | that note's Hz | existing |
| `noteVel` | that note's velocity `0..1` | existing |
| `beat` | transport position in beats (quarter notes), per sample | **new**; frozen while stopped |
| `sr`, `n` | sample rate, sample index | existing |

Elsewhere (`frame` / `element` / `pixel` / `graph`, and Field Synth / Field
Effect) `noteNum` and `beat` are reserved and read inert or real per the
`age` precedent. Populating them in Field Synth too is cheap and recommended
(one more field on `SampleRuntimeInput`).

### 3.2 Builtins

| Builtin | Returns | Rule |
|---|---|---|
| `tick(div)` | `1.0` on the one sample where `beat` crosses a multiple of `div` beats, else `0.0` | `div` from the quantize table: `1/4` = quarter, `1/16` = sixteenth, `1/12` = eighth triplet. Never fires while stopped. Handles loop wrap (a backwards jump fires once, at the new position) |
| `deg(d)` | MIDI note of scale degree `d` | uses the node's **Root** and **Scale** dropdowns + octave 4 (`MusicTime::DegreeToNote`); negative and >7 degrees wrap octaves |
| `rand()` | already exists via step 2 pure randomness | seeded per node; Field's determinism rules hold |

### 3.3 The one new statement: `note(pitch, vel, len)`

| Arg | Unit | Rule |
|---|---|---|
| `pitch` | MIDI number | rounded to nearest int, clamped `0..127` |
| `vel` | `0..1` | `<= 0` means "don't play" (cheap way to gate a note without an `if`) |
| `len` | beats | `> 0`: the node schedules its own note-off `len` beats later. `0`: **follow the input**, the note ends when the input note that triggered this sample ends (process mode: transpose, harmonise). `0` with no input note playing = note is dropped, with a one-line warning |

Semantics:

- **Predicated, not branched.** The sample backend has no runtime jumps
  (step 9 §2). A `note()` inside `if (...) { }` compiles to one
  `EmitNote(pred, pitch, vel, len)` op whose predicate is the `if`'s
  condition register. Outside an `if`, `pred` is the constant `1` — which
  plays a note **every sample**, so the compiler refuses an unconditional
  `note()` with: *"note() outside an if plays 48000 notes a second; guard it,
  e.g. `if (tick(1/16) > 0.5) { note(...) }`"*.
- **Chords** = several `note()` calls under one guard, or `map(3) { note(deg(i*2), ...) }`
  (map unrolls at compile time, cap 64).
- **Budget:** at most **16 emits per sample, 64 per block**. Excess is
  dropped and counted on the node's readout strip (same visibility rule as
  `NoteEventQueue` overflow). Note-offs are never dropped.
- **Not reusing `emit()`.** Graph-domain `emit()` spawns nodes at edit time
  (`IRStmtKind::Emit`). `design-brief-dynamic-pins.md` already warns not to
  share that machinery; `note()` gets its own op.

### 3.4 Several input notes on the same sample

A three-note chord arrives as three note-ons with the same `frameOffset`.
Field Synth's snapshot keeps only the last (`FieldSynthNode.cpp`, the
`noteOnEdge` loop) — fine for a kick, wrong for a harmoniser.

**Proposed rule:** on a sample with *k* input note-ons, Field Notes runs the
kernel *k* times, once per event in arrival order, each with its own
`noteOn/noteNum/noteVel`; `state` carries through (events are processed in
sequence, like a VST MIDI effect walking its event list). `n` and `beat` are
the same for all *k* runs. `tick()` fires on the first run only.

### 3.5 Example scripts (also the shipped presets)

All bare names, no sigils, valid against `field-language` §4.

**Transpose +7 (process)**
```
if (noteOn > 0.5) {
   note(noteNum + 7, noteVel, 0)
}
```

**Harmoniser: add a fifth and an octave (process)**
```
if (noteOn > 0.5) {
   note(noteNum, noteVel, 0)
   note(noteNum + 7, noteVel * 0.7, 0)
   note(noteNum + 12, noteVel * 0.5, 0)
}
```

**Ratchet: every input note becomes 4 quick hits (process)**
```
param float rate = 0.0625 [0.03125, 0.25]
state float left = 0
state float pitch = 60
state float vel = 0
if (noteOn > 0.5) { left = 4; pitch = noteNum; vel = noteVel }
if (left > 0.5 && tick(rate) > 0.5) {
   note(pitch, vel, rate * 0.5)
   left = left - 1
}
```

**Euclidean 5-in-16 on the root (generate)**
```
param float hits = 5 [1, 16]
state float step = 0
if (tick(1/16) > 0.5) {
   on = floor((step + 1) * hits / 16) - floor(step * hits / 16)
   note(deg(0), 0.9 * on, 1/16)
   step = (step + 1) % 16
}
```

**Random scale walk (generate)**
```
param float density = 0.6 [0, 1]
param float spread = 2 [1, 7]
state float d = 0
if (tick(1/8) > 0.5) {
   d = clamp(d + floor((rand() * 2 - 1) * spread + 0.5), -7, 14)
   play = if(rand() < density, 1, 0)
   note(deg(d), (0.6 + 0.3 * rand()) * play, 1/8)
}
```

Skipping a note is done with `vel = 0`, never `len = 0` (`len = 0` means
"follow the input" and would hang off a note that doesn't exist).

---

## 4. Node shape and pins

Shape row from `new-audio-node` §2: **Note source / processor**
(`INode` + `INoteSource`).

| Slot | Pin | Type | Label |
|---|---|---|---|
| in 0 | notes in | `NoteCable` via `NoteInputSlot(0)` | `notes` |
| out 0 | notes out | `NoteOutbox()` | `notes` |

- Category **`Notes`** (one token; `Patch.cpp` reads it with `>>`).
- Display name **`Field Notes`**. Collision check: no node of that name in
  `src/app/graph/NodeRegistry.cpp`; `Notes`, `Pattern`, `Shape` are the
  dangerous neighbours, none collide.
- No declared extra pins in v1 (`input sample audio x` is a Field Synth thing).
  A later step can let `input note float` read a modulator; not now.

Two objects, per `new-audio-node` §0.2:

```
 main thread                               audio thread
 ┌──────────────────────┐   ParamMailbox   ┌────────────────────────────┐
 │ FieldNotesNode       │ ───────────────▶ │ AudioFieldNotesNode        │
 │  INode + INoteSource │   SampleSlot     │  ProcessBlock:             │
 │  editor, compile,    │ ───────────────▶ │   drain inbox events       │
 │  params, save/load   │   (program swap) │   per sample: run kernel   │
 │                      │ ◀─────────────── │   EmitNote → mOutbox.Push  │
 │  readout strip       │    MeterRing     │   due note-offs → Push     │
 └──────────────────────┘ (emits, drops)   └────────────────────────────┘
```

The audio half owns:

| Member | Size | Why |
|---|---|---|
| `NoteEventQueue mOutbox` | 256 events | the out pin |
| pending-off table | 128 entries `{voiceId, note, offBeat}` | `len > 0` notes; flushed on stop, on delete, on program swap with all-notes-off |
| follow table | 128 entries `{inVoiceId → outVoiceIds[16]}` | `len = 0` notes end when their input note ends |
| one `state` bank | `kSampleMaxRegs` | not per voice: Field Notes is monophonic as a *program*, polyphonic as an *output* |

Stuck-note rules (each one is a bug that has already happened on some other
node):

1. Transport stop → note-off for everything pending.
2. Node delete / bypass toggled on → note-off for everything pending, before
   the outbox is detached.
3. Script recompiles → keep state (step 9 transplant), keep pending offs.
   A failed compile keeps the last working program (`field-compiler` error
   rule), so nothing stops.
4. Input cable removed → treat as note-off for every `len = 0` follower.

Bypass (per the bypass rule: single-input node): bypassed = notes in pass
straight to notes out, kernel does not run.

---

## 5. The body

Grammar from `audio-node-ui` / `node-ui-pillars`. Kept to the KHS bar:

```
┌ Field Notes ─────────────────────────── ● ┐
│ ┌───────────────────────────────────────┐ │
│ │ code editor (same widget as Field      │ │
│ │ Synth), errors underlined in place     │ │
│ └───────────────────────────────────────┘ │
│  [Preset ▾]  [Root  C ]  [Scale  Major ]  │   ← field wells, no chevrons
│  (knob)(knob)(knob)(knob)  ← one per param │
│ ▮▮ ▮  ▮▮▮ ▮   ▮▮  ← note-roll strip, 2 bars │
│ 3 notes/beat · 0 dropped · C4 E4 G4        │   ← readout strip
└────────────────────────────────────────────┘
```

| Control | Count | Notes |
|---|---|---|
| Preset | 1 | same list mechanism as Field Synth |
| Root, Scale | 2 | feed `deg()`; scale list from `MusicTime`. Hidden if the script never calls `deg()`? **No**: always shown, so the body doesn't jump when you type |
| `param` knobs | 0..N | generated by `ParamTable`, modulatable dots per pillar rules |
| Note-roll strip | 1 visual | last 2 bars of emitted notes, decimated, ≤30 Hz redraw (audio-node-ui cost rule) |

No mix knob (note node).

---

## 6. Wiring sites

From `new-audio-node` §3, plus the Field-specific ones from
`field-integration`. Nothing else should need touching; if it does, the node
is built wrong.

| # | Site | Change |
|---|---|---|
| 1 | `src/nodes/FieldNotesNode.h/.cpp` | new; audio class forward-declared, `unique_ptr`, out-of-line ctor/dtor |
| 2 | `CMakeLists.txt` | add the `.cpp` |
| 3 | `src/app/graph/NodeRegistry.cpp` | `REGISTER_NODE(FieldNotesNode, Field Notes, "Notes")` next to `FieldSynthNode` |
| 4 | body dispatch | `DrawFieldNotesBody` + branch in the audio body ladder |
| 5 | help text | 4 hand-kept tables + per-OS `#if` branches (memory: node help coverage); check both directions |
| 6 | `src/core/field/BackendRegister.cpp` | reserve `noteNum`, `beat`; lower `note()` → `EmitNote`, `tick()`, `deg()`; refuse unguarded `note()` |
| 7 | `src/core/field/SampleProgram.h` / `SampleRuntime.h` | `SampleOp::EmitNote`, `Tick`, `Deg`, `LoadNoteNum`, `LoadBeat`; `SampleRuntimeInput` gains `noteNum`, `beat`, `beatPerSample`, `root`, `scale`, an emit sink pointer |
| 8 | `FieldSynthNode.cpp` / `FieldSampleNode.cpp` | populate `noteNum`, `beat` (so the names mean the same everywhere); their `EmitNote` sink is null → compiler refuses `note()` there with *"note() needs a Field Notes node"* |
| 9 | `ElementBackend.cpp`, `GlslBackend.cpp` | `noteNum`/`beat` read inert, like `noteOn` today |
| 10 | patch-skill facts | regenerate with `tools/gen-patch-skill.py` so `infinite-patch-authoring` knows the node |
| 11 | templates | one starter template: Field Notes → Field Synth → Audio Out |

`EmitNote` on the audio thread writes into a fixed per-block array the node
owns (no allocation), then the node turns those into `NoteEvent`s with
`NextVoiceId()` and the sample's `frameOffset`.

---

## 7. Real-time and threading

| Rule | How it's met |
|---|---|
| no alloc / locks / strings in `ProcessBlock` | emit sink, pending-off, follow tables all fixed arrays sized at construction |
| `CookIfNeeded` < 5 µs, no DSP | drains the MeterRing (emit count, drops, last 16 emitted notes for the strip), pushes dirty params |
| program swap | `SampleSlot` + `DrainRetired`, exactly as Field Synth |
| cost | one kernel run per sample (+ one per extra same-sample note-on). The register machine already runs 16 voices × 48 k in Field Synth; one instance is noise |
| timing | `frameOffset` = sample index where `EmitNote` fired, so emitted notes are sample-accurate, not block-quantised |
| note cycles | Field Notes → Field Notes chains are fine; a loop is refused by `WouldCreateNoteCycle`, unchanged |

---

## 8. Tests

| Fixture | Asserts |
|---|---|
| `INFINITE_FIELDNOTESTEST` (new, headless, no device) | (1) transpose preset: C4 in → G4 out, same `frameOffset`, matched note-off when input ends. (2) Euclidean 5/16 at 120 BPM: exactly 5 note-ons per bar, on the analytic sample indices. (3) chord in → harmoniser → 9 note-ons, all same sample. (4) `len = 1/16` → note-off exactly `sr * 60/120/4` samples later. (5) stop mid-note → every note gets its off; zero stuck. (6) 100 emits in one sample → 16 kept, drop counter = 84, no off lost. (7) unguarded `note()` → compile error text matches. (8) recompile mid-play keeps `state.step` |
| `AUDIOPARAMSWEEPTEST`, `AUDIOTEARDOWNSWEEPTEST` | picked up generically; delete mid-playback, zero xruns, zero stuck notes downstream |
| `ROUNDTRIPTEST` | script text, Root, Scale, every `param` survive save → load → undo → copy/paste |
| `field-testing` corpus | add the 5 presets to `tests/field/corpus.txt` |
| `INFINITE_FIELDSAMPLETEST` | new section: `noteNum` and `beat` read correctly in Field Synth |

Exit criterion = `new-audio-node` §6, with "makes the sound" read as "emits
the events": Field Notes → Field Synth → Audio Out plays the Euclidean preset
in time with the metronome.

---

## 9. Build order (3 blocks, one branch: `feature/field-notes`)

| Block | Contents | Done when |
|---|---|---|
| **A. Language** | `noteNum`, `beat`, `tick`, `deg`, `note()` lowering + `EmitNote` op + unguarded refusal; inert reads in other backends; Field Synth populates `noteNum`/`beat` | `INFINITE_FIELDSAMPLETEST` new section OK; corpus compiles |
| **B. Node** | `FieldNotesNode` + audio half, pins, outbox, pending-off + follow tables, stuck-note rules, bypass pass-through, registration, help text | `INFINITE_FIELDNOTESTEST` 1–8 OK; sweeps pass |
| **C. Body + presets** | editor, Root/Scale wells, param knobs, note-roll strip, 5 presets, starter template, patch-skill regen | hygiene passes; template plays in time |

---

## 10. Decisions proposed (owner approves / changes)

| # | Decision | Proposed | Alternative |
|---|---|---|---|
| D1 | Which backend runs the script | **sample domain**, sample-accurate | frame domain (60 Hz = up to 16 ms jitter: audible) |
| D2 | Output spelling | **`note(pitch, vel, len)` statement** | reserved outputs `pitch`/`vel`/`trig` (mono only, no chords) |
| D3 | Pitch unit | **MIDI number**, plus new `noteNum` read | Hz everywhere (matches `notePitch`, but every script does `ftom`) |
| D4 | `len = 0` means | **follow the input note** | always require a length (breaks transpose/harmonise of held notes) |
| D5 | Same-sample chord input | **run the kernel once per event** (§3.4) | keep Field Synth's last-wins snapshot (harmoniser plays one note of three) |
| D6 | Unguarded `note()` | **compile error** | allow it (48 k notes/s, instantly floods the queue) |
| D7 | Scale source | **node's own Root/Scale wells** | a project-wide key (doesn't exist yet; would be a separate feature) |
| D8 | Name | **Field Notes** | Field Sequencer, Field MIDI |

---

## 11. Open questions (not blocking v1)

- **Note-roll strip vs. readout only** — the strip is the one thing that makes
  a generative script legible. Cost is bounded (≤30 Hz, decimated) but it is
  the biggest UI piece.
- **CC / pitch bend out** — `NoteEvent` already carries `bendSemitones`. A
  `bend(semitones)` statement is a natural v2; MIDI CC out depends on the
  MIDI-out plan (`docs/plans/midi-out/`).
- **Prediction tie-in** — Predictive Notes could feed Field Notes, and Field
  Notes' `noteNum` history is exactly the rung-4 "learns from what you play"
  input `algorithms.md` §9.2 wanted. Leave the door open, build nothing.
