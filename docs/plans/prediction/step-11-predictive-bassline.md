# Prediction step 11: Predictive Bassline

Status: **design only, nothing built.** Written 2026-09-22. Supersedes the earlier "Predictive
Rhythm" framing from the same discussion (analyze a rhythm and repeat it) — that idea's open
question was UI/UX ("not sure how this operates"); it's resolved here by not shipping it as its
own node. Rhythm-pattern learning is not a separate concern — it's a subset of what this node
already has to learn (§2), so a standalone "repeat the rhythm" node would just be this node with
its pitch-tracking half switched off, which is a mode, not a different node.

One line: **it plays a bass part the way a real bass player would — following the harmony of
whatever's playing now, in a rhythmic voice it learned from you.** Learn button, plus whatever
params turn out to need exposing beyond that (§5 — this one may need slightly more than one knob,
unlike steps 9/10).

Line numbers are from commit `1ccdbe9`; re-grep the symbol if one has drifted.

## 1. Why this is not just Predictive Notes retuned to a low register

The user's own framing of what a bassline actually is: *rhythm, the velocity of each note, how many
times a pitch repeats, and when it switches pitch* — that's a description of a part's **rhythmic
and repetition structure**, deliberately said independently of *which* pitches. That's the tell:
a real bass player doesn't free-associate a melodic continuation the way Predictive Notes does
(§ step 6 — it sample-continues from its own learned Markov tail). A bass player's *rhythm* is
their own habit (a walking pattern, a repeated-eighth-note pulse, a syncopated pocket), but their
*pitch* choice at any moment is not free — it's constrained by whatever harmony is sounding right
now. Two different things are being predicted from two different sources:

| Dimension | Source it should track | Why |
|---|---|---|
| Rhythm (IOI), duration, velocity, repeat-count structure | **the bassline's own learned habit** (self-continuation, like Predictive Notes) | this is genuinely a style choice specific to how the owner basslines — a Markov continuation of the owner's own captured rhythmic behavior is the right model |
| Pitch | **the live incoming harmony**, not self-continuation | a bass note that ignores what's currently playing isn't a bassline, it's a wrong note; pitch has to be *reactive*, conditioned on context that updates every note, not generated from the bass part's own history |

This is why it can't be built by pointing `PredictiveNotesNode` at a lower register and calling it
done — that node's pitch viewpoint (`NoteModel::kPitch`) is unconditional absolute-MIDI
self-continuation. This node needs pitch conditioned on an external, constantly-changing signal.

## 2. The model

### 2.1 Rhythm/velocity/duration: reuse `NoteModel` almost as-is

`NoteModel`'s `kIoi`, `kDur`, `kVel` viewpoints ([`audio/NoteModel.h:29`](../../../src/audio/NoteModel.h))
are exactly "rhythm, how long each note holds, how hard it's played" — already multi-viewpoint,
variable-order Markov, already handle "how many times in a row" implicitly (a repeated symbol in
the Markov context is exactly how a variable-order model captures "this tends to repeat 3 times
then move"; no separate repeat-counter needs inventing). **Reuse `NoteModel::Tables`/`Player`
wholesale for these three viewpoints.** This is captured from the owner playing/recording an
example bassline during Learn — the same two-object capture-then-build flow as
`PredictiveNotesNode` (§6.1 of step 6), not from the melody it will eventually accompany. Learn a
bass *feel*; apply it under *anything*.

### 2.2 Pitch: a new, conditioned viewpoint — relative to a live root, not absolute

`NoteModel::kPitch` is explicitly documented as "absolute MIDI note, not interval+register" and
explicitly *not* a good fit for a key-relative model without a "separate key-fold" (the class
comment in `NoteModel.h` says this outright). That separate key-fold is exactly what this node
needs to add:

1. **Root tracking**: at each moment this node needs to emit a bass note, look at the currently
   held note(s) on its (separate) harmony input and take the **lowest currently-held note** as the
   root. This is a deliberate simplification, same spirit as `NoteModel`'s own documented ones
   (§ step 6): true chord-quality detection (is it a root-position triad, an inversion, a
   7th chord needing a 3rd or 5th instead of the root) is a real MIR problem and explicitly out of
   scope for a first version. Lowest-held-note-as-root is what most simple auto-bass features in
   other tools actually do, and it's right often enough to be useful immediately.
2. **Relative pitch as the learned symbol, not absolute pitch**: during Learn, instead of recording
   `NoteModel::Event::note` as absolute MIDI, record the **interval from the root active at that
   moment** (semitones, signed, likely small range — a bass mostly plays the root, the 5th, and
   passing tones, rarely more than an octave-plus away). This is a new, small viewpoint —
   call it `kPitchRel` — parallel to `kPitch` but keyed to root-relative degree instead of absolute
   note. It reuses the same `CtxSlot`/`PairSlot`/Markov machinery `ViewModel` already provides;
   it is a new *alphabet*, not new modeling code.
3. **At play time**: sample a relative-pitch symbol from the learned `kPitchRel` model (conditioned
   on its own recent relative-pitch history, same as any other viewpoint), then resolve it against
   *whatever the root is right now* (not the root at learn time) to get the actual note to play.
   This is the entire trick: the Markov model captures the owner's *habit* ("mostly root, sometimes
   the 5th, occasional chromatic passing tone down to the next root"), and habit gets re-expressed
   against live harmony every time, so the bass genuinely follows chord changes it was never
   trained on.

### 2.3 What this does *not* need to solve

- Full chord-quality/inversion detection (§2.2.1) — lowest-note-as-root, revisit only if it proves
  wrong often enough in practice to matter.
- A second independent Markov model for pitch vs. relative-pitch — §2.2.2 is additive to
  `NoteModel`'s existing viewpoint scheme, not a parallel system.
- Reconciling rhythm-viewpoint timing with the harmony input's timing — the bass plays on **its
  own** learned rhythm (§2.1), sampling root-relative pitch (§2.2) at each of its own onsets. The
  two inputs (harmony source, bass's own learned rhythm) are decoupled in time, which is realistic
  — a bass player doesn't necessarily re-articulate on every chord change, and does sometimes play
  through a change on the same held note before resolving to the new root on their own next onset.

## 3. Two note inputs, not one

Unlike every other note-consuming predictor in this document set, this node needs **two** `NoteCable`
slots: one is the harmony/root source (read live, never captured into the learned model), the other
is the example-bassline source used only during Learn (§2.1). These can be the same wire in the
simple case (learn from the same line you'll later harmonize against — e.g. record a bassline once
against a fixed chord loop, then let it re-harmonize against a different progression later) or two
different wires (learn the *feel* from one recorded example, apply it live against a different,
real-time harmony source) — both are legitimate and the two-slot design supports both without extra
modes.

```
NoteCable* NoteInputSlot(int slot) override {
   return slot == 0 ? &harmonyInput : slot == 1 ? &learnInput : nullptr;
}
const char* InputLabel(int slot) const override {
   return slot == 0 ? "harmony" : slot == 1 ? "learn from" : nullptr;
}
```

## 4. Two objects, per `new-audio-node`

`PredictiveBasslineNode : INode, INoteSource` (main thread: Learn state on `learnInput`, table
build for the four viewpoints including the new `kPitchRel`) / `AudioPredictiveBasslineNode :
AudioNode` (audio thread: tracks the current root from `harmonyInput`'s held notes each block,
samples the learned rhythm/duration/velocity/relative-pitch model at its own onsets, resolves
relative pitch against the current root, emits `NoteEvent`s).

Root tracking needs a small piece of per-block state — "which notes are currently held on the
harmony input" — that's new (Predictive Notes doesn't need to track held state on its input, it
just captures/replays events). Closest existing precedent to copy the shape from:
`AudioQuantizerNode::mOutNote` (`NoteNodes.cpp:1465`, a small `GetOrInsert(voiceId)` map) already
tracks per-voice held state for a different reason; the held-notes-for-root-tracking structure here
is the same shape (small fixed-capacity voice→note map), not the same code.

## 5. Params — likely more than one knob, unlike steps 9/10

Two things are being learned (rhythm-family and pitch-family), and it's plausible the owner wants
to dial their influence separately rather than one blended `mix`:

```
v.Bool("learning", mLearning);
v.Float("mix", mix);          // 0 = harmony input passed through unchanged, 1 = full generated bass
v.Int("register", register);  // octave offset applied after root resolution — keeps generated
                               // notes in a bass range regardless of where the harmony input sits
```

Whether `register` earns its own knob or gets folded into a fixed convention (e.g. always the
octave below the root, no user control) is a real open question — **decide during implementation**,
after hearing it against real harmony input; this is exactly the kind of call that's premature to
lock from a design doc. Default assumption for a first pass: fixed convention, no extra knob, to
keep the node as close to the "one learn button, minimal knobs" shape as the rest of the category —
add `register` only if it turns out to be needed.

## 6. Traps

| Trap | Why |
|---|---|
| Building a second, parallel modeling system for pitch | §2.2.2 — `kPitchRel` is a new viewpoint *inside* the existing `NoteModel` scheme, not a reason to hand-roll a second Markov implementation. |
| Learning absolute pitch instead of root-relative | Reproduces exactly the limitation `NoteModel.h`'s own class comment already warns about — a learned walk that leaves its key the moment the harmony changes from what was captured at learn time. |
| Treating "lowest held note" as chord-quality detection | It isn't (§2.2.1) — don't let the design quietly grow scope into real harmonic analysis; ship the simplification, note it plainly in the node's own help text the way `Curves`' help text already states its own scope precisely. |
| Coupling the bass's onset timing to the harmony input's onset timing | §2.3 — they're deliberately decoupled; forcing the bass to re-trigger on every harmony change would make it sound like a chord-tracking arpeggiator, not a bass player. |
| Forgetting the two-input wiring in the note-help tables | Two `NoteCable` slots on one node is less common in this codebase; check both the right-click help and the hand-kept 4-table system (memory: `project_node_help_coverage`) cover slot 0 and slot 1 distinctly. |

## 7. Test

Propose `INFINITE_PREDBASSLINETEST`.

1. Learn a simple example (steady quarter notes, root-only) against a fixed harmony; play back
   against the *same* harmony — output pitch matches root, rhythm matches the learned quarter-note
   pulse.
2. Same learned model, played back against a **different, transposed** harmony input never seen
   during Learn — output pitch tracks the new root (proves relative-pitch resolution, not
   memorized absolute notes).
3. Chord change mid-note: bass does not re-trigger until its own next learned onset (proves
   decoupled timing, §2.3).
4. Two-input wiring: `harmonyInput` disconnected mid-playback does not crash; falls back to holding
   the last known root (graceful degradation, not silence or a dangling read).
5. `new-audio-node` §7 exit criterion across both `NoteCable` slots: spawn, wire both, save/load,
   delete mid-playback, no dangling binding on either slot.

## 8. Exit criterion

`PREDBASSLINETEST` passes; `audio-node-sweep`/`audio-pipeline-sweep` clean; `node-ui-pillars`
checklist clean; owner has played a real chord progression against a learned bass feel and confirms
it tracks changes correctly and still sounds like *their* bass playing, not a generic walking-bass
algorithm.
