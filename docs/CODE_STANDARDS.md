# Code Standards

This is the one place that answers "how do we do things here" across the
project, independent of any single node or feature. It doesn't repeat what's
already written down elsewhere — it points to it. If a rule below and a
skill/doc disagree, the more specific doc wins; update this file.

Related docs: [ARCHITECTURE.md](../ARCHITECTURE.md) (where code lives),
`.claude/skills/infinite-code-review/SKILL.md` (the four review standards:
accuracy, experimentality, design, quality), `.claude/skills/new-audio-node/`,
`.claude/skills/new-effect-node/`, `.claude/skills/new-geometry-node/` (the
procedure + bug traps for each node category).

---

## 1. Code logic / language standards

- **No premature abstraction.** Three similar `DrawXxxParams` functions is
  better than a shared helper built for a fourth node that doesn't exist yet.
  This codebase's own shape proves it: `main.cpp` is one file with a
  consistent per-node-type pattern (registration → params → body → link
  validation) rather than a framework of base classes. Follow that pattern,
  don't "improve" it into one.
- **Match the file's existing idiom before introducing a new one.** If a
  node category has a reference implementation (`GainNode` for audio,
  `GeometryOpNodes` for geometry, `FilterDefs.cpp` for table-driven effects),
  copy its shape. A new node that looks structurally different from its
  siblings is itself a review finding.
- **Reuse existing primitives over reinventing them.** `src/audio/DspMath.h`
  has PolyBLEP, TPT/Zavalishin SVF, RBJ biquads, one-pole, dB↔lin, equal-power
  pan, fast tanh — a hand-rolled biquad elsewhere is a finding even if correct.
  The geometry side has the equivalent expectation for `src/core/Mesh.h`
  matrix/mesh math.
- **Comments explain why, never what.** Default to none. Write one only for
  a non-obvious constraint, a workaround for a specific bug, or an invariant
  that would otherwise get silently broken (see `AudioPluginNode.h`'s
  documented `ParamMailbox`-bypass comment as the model).
- **Clean-room discipline.** `/Users/namansoni/BespokeSynth` is GPLv3-licensed
  reference material; Infinite is MIT. Code, variable names, or comment
  structure that looks transcribed from it is the single most serious finding
  this project can produce — never copy from it, only from its cited DSP
  papers/algorithms (PolyBLEP, Zavalishin TPT, RBJ cookbook, FDN, Voss-McCartney).

## 2. Error handling

- **Validate only at real boundaries** — file load, patch deserialization,
  user-provided expressions/formulas, plugin hosting. Internal call paths
  between trusted engine code don't need defensive checks; the invariant is
  the contract (e.g. `IGeometrySource`'s accessors), not a runtime guard.
- **Never silently drop data.** The recurring bug class here is a node that
  forwards some side-channel accessors (`GetMesh()`, `GetMaterial()`,
  `GetMappingTransform()`, ...) but forgets others — no crash, no error, just
  a downstream node quietly missing something. Forward all-or-nothing, don't
  add a `try/catch`-shaped guard around a partial forward.
- **No swallowed failures.** The dev/test harness (`INFINITE_*` env vars)
  exists so failures show up as printed pass/fail verdicts, not as silent
  wrong output. A new stateful or numeric node without a corresponding check
  is a finding, not an optional nicety — see Testing below.

## 3. Data flow

- **Respect the two-DAG split.** Image/geometry cables (`ImageCable`,
  `IGeometrySource`) and the audio/note DAG (`AudioEffectNode`,
  `ParamMailbox`) are separate systems with separate threading rules — don't
  blur them (e.g. touching audio state from the main-thread param UI without
  going through the mailbox).
- **Revision/generation stamps must be honest.** A node's `MeshRevision()` (or
  any generation counter) only moves when its actual output changed —
  bumping it just because `CookIfNeeded` ran again breaks every downstream
  node that resets state on "did my input change." (`DisplacementNode` did
  this; `REVISIONSWEEPTEST` now guards it generically.)
- **Cross-thread state has one owner.** The `AudioPluginNode` pattern — main
  thread owns, audio thread only reads through `std::atomic<T*>`, retire-not-
  destroy for one generation — is the template for any new cross-thread
  handoff, not something to reinvent per node (`SampleSlot.h` is the same
  pattern generalized for sample playback).

## 4. Testing

- **New DSP needs an analytic fixture**, not "it sounds right" — an
  `INFINITE_DSPTEST`-style check against a known expectation (SVF response at
  a known cutoff, ADSR segment timing, delay accuracy in samples, RT60, GR
  curve). Math without this hasn't been verified by anyone, including its
  author.
- **New nodes get swept, not hand-checked.** `audio-node-sweep` (param
  round-trip + no-crash teardown) and `geometry-transform-sweep` (transform
  propagation + honest revision stamps) are generic and must be wired in for
  any new node in those categories rather than writing a one-off manual check.
- **UI changes get driven, not just typechecked.** Before calling a UI change
  done, run it — `run-infinite-hygiene` for a full pass, or the app directly
  for something narrow. A green compiler is not a working feature.
- **Full-app baseline before pushing.** `run-infinite-hygiene` drives the
  self-test harness end-to-end (undo/redo, save/load, groups, macros, audio
  sweeps, full node-type round trip) — this is the pre-commit bar, not an
  optional extra pass.

## 5. Bug finding / fixes

- **Fix the root cause, never bypass the check.** No `--no-verify`, no
  disabling a sweep because it's inconvenient this week. If a sweep is
  wrong, fix the sweep; if the code is wrong, fix the code.
- **Every fixed bug class becomes a standing invariant, not a one-off patch.**
  The pattern in this repo: a bug happens once (e.g. three separate nodes
  forgetting `GetMappingTransform()`), then a generic sweep is added so the
  *class* of bug can't recur silently. When you fix something structural,
  ask whether it needs a sweep/fixture, not just a diff.
- **Use `infinite-code-review` before merging anything from outside the
  session** — another AI, another person, an old session without today's
  skills loaded. Compiling and looking fine in the diff is not evidence of
  correctness here; the failures are invariant violations no compiler sees.

## 6. Compiling standards

- CMake-based build (`CMakeLists.txt`); no separate style/lint config exists
  yet. Keep new source files following the existing `src/<category>/` layout
  (`nodes/`, `audio/`, `audio/dsp/`, `core/`, `platform/`) rather than
  introducing a new top-level directory for a single node.
- New node types register through `NodeFactory` / the `EffectDef` table —
  never add a bespoke registration path; see the `new-*-node` skills for the
  exact wiring sites (`RegisterNodes()`, `CMakeLists.txt` entries, etc.).
- As this project scales, if compiler warnings or a linter get turned on,
  record the decision and the flags here rather than leaving it implicit in
  someone's local setup.

## 7. Architecture standards

- **`ARCHITECTURE.md` is the source of truth for "where does this go."** Six
  categories (Node Library, Engine/Runtime Core, Editor UI, App Features,
  Platform Layer, Dev/Test Harness) — find the right one before writing code,
  don't guess from proximity in `main.cpp`.
- **One node = one `DrawXxxParams`/`DrawXxxBody` pair**, following the
  two-object rule for audio nodes (`INode` main-thread half +  `AudioNode`
  audio half) and the three-object exception only where the codebase already
  has one (plugin hosting). Don't invent a fourth shape.
- **New node categories get a skill, not tribal knowledge.** The existing
  `new-audio-node` / `new-effect-node` / `new-geometry-node` skills are the
  model: a wiring checklist, the bug traps that motivated each rule, and a
  machine-checkable exit criterion. If a new node category emerges that
  doesn't fit the existing three, write its procedure down the same way
  instead of leaving it as one-off review comments.
- **Objective-C / platform-specific code stays behind `Platform.h`.** Node
  and engine code stays pure C++; native shims (file dialogs, image/model
  decode, plugin hosting's `NSWindow`) are isolated so the audio thread never
  touches ARC or sends an Objective-C message.

---

## As this grows

This doc should stay a thin index, not accumulate detail that belongs in
`ARCHITECTURE.md` or a skill. When a new standard is needed:

1. Check if it's really a new *category* of node/feature — if so, it likely
   wants its own skill (see §7), not a paragraph here.
2. Check if it's a recurring bug class — if so, it wants a sweep/fixture
   (see §5), and a one-line pointer here.
3. Only add a new numbered section here if it doesn't fit 1-7 above.
