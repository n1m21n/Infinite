# 05 — Audio architecture documentation + staleness sweep

Standards and maintenance work. Independent of everything else; can run any
time, including in parallel with 01–04.

Two problems. First, `ARCHITECTURE.md` documents the audio *node library* and
nothing about the audio *engine*: the whole real-time architecture — the audio
thread, the topology generation swap, `ParamMailbox`/`MeterRing`, the two-object
rule, `Transport`'s audio clock, `NoteEventQueue`, `Platform::AudioDeviceOpen` —
is undocumented, and §2 Engine/Runtime Core and §5 Platform Layer list no audio
files at all. Second, three status documents are actively wrong, which is worse
than missing: a reader trusts them.

Paste everything below into a fresh Claude Code session.

---

Document Infinite's audio architecture and correct three stale status documents
(/Users/namansoni/infinte).

## Part 1 — `ARCHITECTURE.md`'s missing audio engine section

Currently the file's only audio content is a 21-line "Audio / note node system"
subsection at `ARCHITECTURE.md:70-89` covering the `AudioEffectNode` +
`EffectDef`/`IEffectKernel` table, `Wavetable` bank sharing, and where
per-effect UI lives. `AudioEngine` is not mentioned anywhere in the file.

Add the real-time architecture, at the same altitude as the rest of the
document — explain the invariants a contributor must not break, not a file
listing. Cover at least:

- The two-object rule: an `INode` on the main thread owning an `AudioNode` on
  the audio thread, communicating only through `ParamMailbox` and `MeterRing`.
- The audio-thread constraint list, cross-referencing
  `src/audio/AudioNode.h:6-12` as the authoritative copy rather than
  duplicating it (a second copy will drift).
- Topology publish: atomic swap with one-generation retire
  (`AudioEngine.h:66-74`), pooled buffers sized so allocation only ever happens
  on the main thread at build time (`AudioEngine.h:11-18`), `kAudioMaxBlockFrames`
  = 4096 with truncation above it (`AudioEngine.cpp:120`).
- `ParamMailbox` as atomic latest-value-wins, and **why it is not a ring** —
  `ParamMailbox.h:16-19` records the removed version and the invariant it broke.
  That is exactly the kind of reasoning that gets re-broken if undocumented.
- `NoteEventQueue`'s SPSC design and its overflow asymmetry (note-ons may drop,
  note-offs may not). If 03 has landed, describe the post-fix guarantee.
- How params reach the audio thread: modulators evaluated at
  `main.cpp:24516-24534`, then cook at `:24741`, then `PushParams` → atomics.
  Cross-reference `docs/plans/audio/cook-rate-decision.md` for the
  last-cooked-value (TouchDesigner-style) decision and its rationale.
- `Transport` driven by the audio callback, and `Platform::AudioDeviceOpen`'s
  `AVAudioEngine`/`AVAudioSourceNode` setup (`Platform.mm:2098-2169`).
- The three audio test fixtures (`INFINITE_DSPTEST`,
  `INFINITE_AUDIOPARAMSWEEPTEST`, `INFINITE_AUDIOTEARDOWNSWEEPTEST`) and the
  documented sweep blind spots — plus any fixture 01–04 added.

Also add the audio files to §2 Engine/Runtime Core and §5 Platform Layer, which
currently list none.

## Part 2 — correct three stale documents

Verified against the code on 2026-08-14. Confirm each yourself before editing;
do not take this list on trust.

**`docs/plans/audio/STATUS.md`:**
- P3a Notes says "1 of 7". Actually **all seven note nodes ship** — Note
  Sequencer, Arpeggiator, Note Filter, Note Modify, Note Echo and Note Router
  are registered at `main.cpp:2082-2097` with bodies and visualizers. Only
  **Note Display** is genuinely missing.
- P3b Synths lists **Sampler** as left. It shipped in commits `e142041` and
  `f50cef7`.
- The P4/P5 hardening line lists the **xrun counter** as left. It exists:
  `AudioEngine.h:78, 145`, UI with tooltip at `main.cpp:17521-17548`.
- The Modulators table's "1 of 4 new" framing is misleading — LFO, Random,
  Pattern, Math, Path, Palette, Audio File, Audio Analyze, Vibrato, Note to CV,
  Macro Knob and Macro XY are all registered (`main.cpp:2042-2060`). Only
  Shaper, Mod Recorder and the unified Macro are outstanding. Reframe so the
  count reflects what a reader would check.

**`docs/plans/README.md`** — the geometry status table has three wrong rows:
- Phase 2 (one geometry interface): **done**, `d133b8b` + `b1e5800`. `IGeometrySource`
  is folded (`src/nodes/Geometry3DNodes.h:94`), all four `dynamic_cast` ladders
  are now generic `GeometryInputSlot` loops (`main.cpp:2476, 2570, 11602, 11990,
  22745`), and `DisconnectAllTo`'s crash-risk comment is gone, replaced by the
  generic loop at `main.cpp:11571`.
- Phase 3 (per-element colour): **done**, `ee96988` + `c3bb72f`.
- Phase 6 (point distribution): **done**, `0de43bd`.
- Phase 5 (curves) is correctly marked not started. Phase 4's row is missing its
  hash: `9ffc6ae`.

**`docs/plans/optimization/research-implementation-map.md`** — do not rewrite
it; it is a record of decisions taken at a point in time. Add a dated header
note saying it was written before `src/audio/` existed, that §1.1/§1.2/§2.1 were
re-graded satisfied on 2026-08-14, and pointing at
`docs/plans/optimization/prompts/README.md` for the current re-grade. Correct
one factual error in place: §3.2 claims `OceanNode` is a single Gerstner wave;
`MeshOps::Ocean` (`src/core/Mesh.cpp:1410-1457`) sums 1–8 components.

## Rules

1. Match `ARCHITECTURE.md`'s existing altitude and voice. It explains why
   invariants exist; it is not a file index. Read enough of it first to match.
2. Do not duplicate the constraint list, the effect-table docs, or the UI
   system docs — cross-reference them. Duplicated docs drift.
3. Verify every claim in Part 2 against the code before editing. If any is
   wrong, report it rather than propagating it.
4. Do not change code in this session. If you find a code bug, write it down
   and leave it.

## Exit criteria — report each explicitly

1. `ARCHITECTURE.md` has an audio engine section covering all the bullets above,
   and audio files appear in §2 and §5.
2. All three documents corrected, each claim independently verified.
3. Report any claim in Part 2 that turned out to be wrong.
4. A reader who knows C++ but not this codebase could, from the new section
   alone, state correctly why `ParamMailbox` is not a ring buffer and what
   happens if they allocate in `ProcessBlock`. That is the bar.
