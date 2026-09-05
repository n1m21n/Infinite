# Field build step 24(+) — handoff: what's actually left of OPEN-A and OPEN-D

**Status:** decisions made, not yet implemented. This is a handoff/kickoff doc
for a fresh session, written after verifying the real state of the code
against `algorithms.md` and `design-brief-language-opens.md`, both of which
are **stale** on this topic as of this writing (commit `7f41001`, after step 23).

## Why this doc exists

`algorithms.md` §4 and `design-brief-language-opens.md` both describe OPEN-A and
OPEN-D as fully unanswered, "ask the owner" questions. That was true when those
docs were written, but commit `18206ed` ("steps 19-21": `delay()` intrinsic,
`FieldPrimitiveNode`, `FieldSynthNode`) landed **before** step 22/23 and already
ships real, tested code that answers a meaningful chunk of both. Neither doc was
updated to reflect it. A fresh session picking this up from the doc text alone
would re-litigate ground that's already settled and miss the ground that
genuinely isn't.

This doc was written after a dedicated verification pass (file:line citations
below) specifically to separate "already shipped, just undocumented" from
"still genuinely open." Do not trust `algorithms.md`'s OPEN-A/OPEN-D sections at
face value — verify against the citations here, and re-verify against the code
if this doc is more than a few weeks old (`git log -1` on it to check drift).

## What's already answered — do not re-decide these

### OPEN-A, delay-line half — ANSWERED, shipped, tested

`delay(x, samples)` is a real, bounded ring-buffer intrinsic, not a stub:

| | |
|---|---|
| Compile dispatch | `src/core/field/BackendRegister.cpp:176-230` (expression form), `:790-793` (statement form) |
| Runtime | `SampleOp::Delay`, `src/core/field/SampleRuntime.h:65-88` — read-before-write-then-advance, a textbook ring buffer |
| Caps | `kSampleMaxDelayCells = 65536`, `kSampleMaxDelayLines = 16` (`src/core/field/SampleProgram.h:30-31`) — checked at `BackendRegister.cpp:202-219`, ≈1.48 s at 44.1 kHz cumulative across all `delay()` calls in one kernel |
| Length | **must be a compile-time integer literal ≥ 1** — `delay(in, in)` is refused, enforced `BackendRegister.cpp:187-198`, confirmed by test at `src/main.cpp:41847`. There is no runtime-variable delay length, by design |
| Test coverage | `src/main.cpp:41725-41973` (SECTION 12 of `INFINITE_FIELDSAMPLETEST`) — impulse response, multi-tap, hot-reload transplant, all against `FieldSampleNode` |
| Hot reload | Buffer contents transplant across a recompile if `length` matches at the same call-site index — `BackendRegister.cpp:917-935`, consumed in `FieldSampleNode.cpp:88-119` and `FieldSynthNode.cpp:98-119` |
| Hygiene gate | `FIELDSAMPLETEST` is a Tier 1 check — `.claude/skills/run-infinite-hygiene/driver.sh:103` |

This is a real shift-register (OPEN-A option **(b)**), which covers every
delay-line, comb-filter, chorus and reverb-tail use case from the original
brief. **Do not re-implement or re-decide this.**

### OPEN-D, self-contained note-reactive synth half — ANSWERED, shipped, tested

`freq`/`gate` are real per-voice reserved names, not stubs:

| | |
|---|---|
| Compile | `SampleOp::LoadFreq`/`LoadGate`, `BackendRegister.cpp:281-292` |
| Runtime | `SampleRuntime.h:60-61`, fed from `MidiNoteToHz(mVoices.NoteAt(v))` and `mGateHeld[v]` inside the real per-voice loop, `FieldSynthNode.cpp:182-197` |
| Shipped use | Presets at `FieldSynthNode.cpp:310-400` and the default fallback at `:469` |

This covers "a self-contained oscillator that reacts to its own current note" —
a real synth voice. **Do not re-implement or re-decide this.**

## Decided — build these, do not re-open the question

The owner made the call on all three remaining items directly (2026-09-05),
overriding the "ask first" default this doc originally carried. **Implement
exactly what's specified below.** The background sections that follow each
decision (what's missing, file:line citations, the old options table) are kept
only so the *why* isn't lost — they are not a menu to re-decide from.

### 1. OPEN-A table — DECIDED: build it now

**Spelling:** `state float name[N] = init` declares a bounded array;
`name[k]` reads, `name[k] = v` writes, `k` any int expression, clamped to
`[0, N-1]` — clamp, not wrap, not error, consistent with `.at()`'s existing
out-of-range rule from step 23. **Domains: `frame` and `sample` only** —
every catalogue use case that actually needs this (the note-transition
matrix, node co-occurrence) is `frame`-rate; nothing in the catalogue needs
an `element`- or `pixel`-domain raw-indexed table, so don't build it there.
Cap the size the same way `delay()` is capped — a `kSampleMaxTableCells`-style
constant in `SampleProgram.h`, enforced at compile time the same way
`BackendRegister.cpp:202-219` already checks `delay()`'s cumulative budget —
rather than shipping an unbounded array.

No general bounded-array-with-arbitrary-runtime-index capability exists
anywhere in Field today. Confirmed: `delay()` only supports "N samples ago" (a
fixed shift), never an arbitrary runtime offset; zero array/lookup/`[idx]`
support in `FieldIR.h`/`FieldIR.cpp` (the typed IR shared by element and pixel
domains); `AstKind::Access` (vector/swizzle indexing) is explicitly refused in
the sample domain (`BackendRegister.cpp:343-346`, "v1 is scalar-only").

**Who needs it:** `algorithms.md` §9.2 (note-transition table, a 12×12 matrix),
§9.6 (node co-occurrence), and any rung-4 entry needing a table rather than a
single remembered value. See `algorithms.md:1176-1220` and `:1341-1352` for the
worked examples that are currently unwritable syntax.

### 2. OPEN-D second audio input — DECIDED: finish the half-built dynamic-pin path

**Do not switch to `in2`.** Port `FieldPixelNode::DeclaredImageInput`
(`FieldPixelNode.h:121-138, 220`) to audio: give `FieldSampleNode`/
`FieldSynthNode` a real connectable audio-cable array, make `AudioInputSlot()`
(`FieldSynthNode.h:40`, `FieldSampleNode.h:45`) consult `mInputPins` the way
the output side already consults `mOutputPins`, and remove the
`BackendRegister.cpp:615-619` refusal so a declared `input sample float ref`
binds into the kernel's scope for real.

`design-brief-language-opens.md` proposed reusing the dynamic-pins system from
steps 11-14. **Half of that already happened, but it stops short of working:**

| | |
|---|---|
| Declaration compiles | `SampleDeclaredPin`, `src/core/field/SampleProgram.h:101-106, 134-135`, validated at `BackendRegister.cpp:566-620` |
| But never bound into scope | `BackendRegister.cpp:615-619` — comment states this explicitly: "actual audio-input slot plumbing... is later work. Deliberately NOT bound into scope." Referencing the declared name in a kernel body is a compile error, confirmed by test `src/main.cpp:42361-42392` |
| Pin-table bookkeeping exists | `PinTable`/`ReconcileFieldPins`, `src/core/field/PinTable.cpp:179`, consumed in `FieldSynthNode::Apply`, `FieldSynthNode.cpp:493-509` — tracks identity, doesn't create a cable slot |
| Cable attachment is hardcoded, not pin-table-aware | `FieldSynthNode::AudioInputSlot(int)`, `FieldSynthNode.h:40`, returns `&audioInput` only for `slot == 1`; `FieldSampleNode::AudioInputSlot`, `FieldSampleNode.h:45`, hardcoded to `slot == 0`. **Neither consults `mInputPins`.** Contrast with the *output* side (`OutputLabel`/`IsAudioOutputIndex`/`ModulatorOutput`, `FieldSynthNode.h:55-107`), which genuinely iterates `mOutputPins.Pins()` — outputs got the full pin-table treatment, inputs did not |
| Runtime confirms the gap | `AudioFieldSynthNode::ProcessBlock` only ever reads `inputs[1]` (`FieldSynthNode.cpp:131`, comment: "Slot 0 is notes, slot 1 is audio in") — no code path reads `inputs[2]` or beyond |
| Working pattern to copy | `FieldPixelNode::DeclaredImageInput` (`FieldPixelNode.h:121-138, 220`) is the real, working version of this idea for the pixel domain — a genuinely connectable `ImageCable` array, tested by `FIELDPINNODETEST` sections 8-10 (`src/main.cpp:54901-54951`). **This is the pattern to port to audio, not to reinvent.** |

**Net: no working example exists today, and none is possible without new code.**
This is real implementation work, not a documentation fix: give `FieldSampleNode`/
`FieldSynthNode` an `AudioCable` array the way `FieldPixelNode` has an `ImageCable`
array, make `AudioInputSlot()` consult `mInputPins` the way the output side
already consults `mOutputPins`, and bind the declared name into the sample
compiler's scope instead of refusing it.

### 3. OPEN-D note history — DECIDED: fold into `frame`, no sixth domain

Add `noteOn`, `notePitch`, `noteVel` to `frame`'s reserved set (`field-language`
§5's table). Semantics: a per-frame snapshot of the most recent note event —
`noteOn` is `1.0` only on the frame a note-on was registered (not held, unlike
`gate`), `notePitch` is that note's frequency in Hz (same `MidiNoteToHz`
convention `freq` already uses), `noteVel` is `0.0-1.0`. Source it from
whatever already feeds `FieldSynthNode`'s voice allocator — do not build a new
note-event path, reuse the existing one and read its most-recent-event state
once per frame.

No `noteOn`/`notePitch`/`noteVel` identifiers exist anywhere in
`src/core/field` or `src/nodes` today (grep, zero hits). `freq`/`gate` are
live-only — reset per note-on, nothing auto-populates a `state` cell with note
history. A genuine sixth `note` domain was the more "honest" answer for
sample-accurate timing but is explicitly **not** what's being built — folding
into `frame` was chosen to avoid a new domain/backend.

## Sequencing — build one at a time, not three parallel branches

These are three independent pieces of compiler/backend work that touch
overlapping files (`BackendRegister.cpp`, `SampleProgram.h`, `PinTable.cpp`).
Implement and merge them **one at a time, each on its own branch** (see
Workflow below) — parallel branches here are a merge-conflict risk for no
benefit, since none of the three depends on either of the others to compile.

## A related bug, NOT in scope for this step — already flagged separately

While verifying `delay()`, a real correctness bug was found: `FieldSynthNode`
shares one delay buffer (`mDelayBuffer`/`mDelayCursors`) across every
polyphonic voice (`FieldSynthNode.cpp:41-44, 182-197`) instead of allocating
one per voice. Two simultaneous notes both using `delay()` would corrupt each
other's ring buffer. It's currently latent — no shipped `FieldSynthNode` preset
uses `delay()` yet — but is a landmine for whoever writes the first
Karplus-Strong-style preset. **This has already been spawned as its own
background task** (title: "Fix FieldSynthNode's shared (non-per-voice) delay
buffer"). Don't duplicate it here; if picking up this step's work touches the
same files, check whether that task has landed first.

## Required reading, in order

1. This doc.
2. `docs/plans/field/algorithms.md` §4 — but treat its OPEN-A/OPEN-D prose as
   superseded by this doc's "already answered" and "decided" sections above.
3. `docs/plans/field/design-brief-language-opens.md` — same caveat.
4. `.claude/skills/field-compiler/SKILL.md` and `field-domains/SKILL.md` — this
   is compiler/IR work, load these before touching `FieldIR.h`/`BackendRegister.cpp`.
5. `.claude/skills/field-integration/SKILL.md` §3 for `ParamRef`/pin wiring
   mechanics for the second-input path.
6. `docs/plans/field/step-19-sample-delay-line.md`, `step-21-field-synth-node.md`
   — the original plans for the code this doc corrects against; useful for
   why `delay()` and `freq`/`gate` were shaped the way they were.

## Workflow

Per this repo's standing convention: each piece below gets its **own branch**,
`feature/field-step-NN-<slug>`, off `main`, with its own commit(s), built one
at a time in this order:

- **Step 24**: OPEN-A table declaration (`state float name[N]`, frame + sample).
- **Step 25**: OPEN-D second audio input (dynamic-pin completion).
- **Step 26**: OPEN-D note-history (`noteOn`/`notePitch`/`noteVel` in `frame`).

## Exit criteria, per piece implemented

Mirror the pattern `field-state` §9 and steps 22/23 already established:

1. Builds clean.
2. A fixture in `src/main.cpp` exercises the new syntax under
   `INFINITE_FIELDSAMPLETEST`/`FIELDPINNODETEST` (whichever applies), not just
   a manual check.
3. The relevant node's cost/pin readout on its face reflects the new
   capability (byte count for a table, a real cable slot for a second input),
   the same way `PixelStateBank::BytesInUse()` and `ImageCable` already do.
4. `/run-infinite-hygiene` passes.
5. `docs/plans/field/algorithms.md` §4 is updated to mark the resolved item
   **ANSWERED AND BUILT (step NN)**, in the same style as OPEN-B/OPEN-C's
   entries — and its "already answered" delay-line/freq-gate omissions (the
   ones this doc corrects) get fixed in the same pass, since a session doing
   this work will already have the context loaded.
6. `docs/plans/field/language-decisions-and-presets.md` §1's row for the
   resolved item gets the same "Resolved. Option (x)." annotation style.
