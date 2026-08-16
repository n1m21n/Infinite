---
name: infinite-code-review
description: Review code written for Infinite — by another AI, a human collaborator, or an earlier session — against this codebase's four standards: accuracy (is the math right), experimentality (is it worth having), design (does it look and feel like an instrument), and quality (efficiency, performance, and the real-time/threading rules). Use when asked to "review this code", "review this node", "check what the other AI built", "is this up to standard", "did they do this right", "review the diff", or before merging any node someone else wrote. Not the same as the generic /code-review skill — this one knows Infinite's invariants and the bugs that have actually happened here.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

## What this skill is for

Code arrives here from outside this session — another AI, another person,
an older session that didn't have the skills loaded. It usually compiles.
It usually looks fine in the diff. The failures in this codebase are almost
never syntax; they are **a dropped invariant that no compiler and no type
checker can see**, and every one of them has already happened at least once
(see the bug-trap sections in `new-audio-node`, `new-effect-node`,
`new-geometry-node`).

So this review is not a style pass. It is: *did they honor the contracts,
is the math actually right, is this node worth existing, and will it stay
fast with twenty of them on the canvas.*

---

## Step 0 — scope the review, then read the standards

```bash
git status --short
git diff --stat
```

If the user named files or a node, review those. Otherwise review the
working-tree diff plus any untracked `src/` files (`git status` above shows
them — new nodes arrive as untracked `.h`/`.cpp` pairs and are easy to miss
if you only read `git diff`).

Then read, in this order, only what applies:

| If the change touches | Read first |
|---|---|
| anything | `ARCHITECTURE.md` — find where the change lives and what owns it |
| an audio/note/synth node | `.claude/skills/new-audio-node/SKILL.md` §0, §4 |
| an audio node's body/UI | `.claude/skills/audio-node-ui/SKILL.md`, `docs/plans/audio/audio-node-ui-system.md` |
| an Effects/Color/Compositing node | `.claude/skills/new-effect-node/SKILL.md` §3, §5 |
| an `IGeometrySource` node | `.claude/skills/new-geometry-node/SKILL.md`; `ARCHITECTURE.md` "Invariants for `IGeometrySource`-consuming nodes" |
| DSP math | `src/audio/DspMath.h` — **before** judging any primitive |

**Never review from memory of how this codebase works.** Open the reference
node and compare. `GainNode` is the smallest complete audio node,
`WavetableNode` the largest, `GeometryOpNodes` the reference geometry
wrapper, `FilterDefs.cpp` the reference effect.

---

## The four standards

The user's bar, in the user's order. A finding belongs to exactly one of
these; say which when you report it.

### 1. Accuracy — is the math right

Wrong DSP or wrong geometry math is the failure that survives review most
easily, because it *sounds* like something and *renders* as something. Check:

- **Primitive reinvented instead of reused.** `src/audio/DspMath.h` already
  has PolyBLEP, TPT/Zavalishin SVF, RBJ biquads, one-pole, dB↔lin,
  equal-power pan, fast tanh. A hand-rolled biquad in a new node is a finding
  even if it works — it's a second thing to be wrong later.
- **Implemented from the primary reference, or from vibes?** The rule is
  PolyBLEP / Zavalishin TPT / RBJ cookbook / FDN / Voss-McCartney, each from
  its own paper. A filter with no coefficient derivation and magic constants
  is a finding. Ask what the reference was.
- **Clean room.** `/Users/namansoni/BespokeSynth` is GPLv3, Infinite is MIT.
  If the code or its comments show signs of having been transcribed from
  there (variable names, structure, comments), that is the single most
  serious finding this skill can produce. Flag it and stop.
- **Coefficients recomputed at the right rate.** A filter that recalculates
  its coefficients per sample when the cutoff only changes per block is a
  performance finding; one that recalculates per *block* when the param is
  being smoothed per sample is an accuracy finding (zipper noise).
- **Denormals.** Any feedback path with a decay tail — reverb, delay,
  resonator, filter feedback — needs the FTZ/DAZ guard plus a tiny-DC or
  flush check. Missing = the node quietly costs 50× CPU as it fades out.
- **Units and ranges.** dB vs linear, Hz vs normalized, radians vs turns,
  0..1 vs -1..1. Check the param range in `VisitParams` against what the
  kernel actually expects. This mismatch compiles silently.
- **Is there a fixture asserting on numbers?** A new DSP node without an
  `INFINITE_DSPTEST`-style fixture checking samples against an *analytic*
  expectation (SVF response at a known cutoff, ADSR segment times, delay
  accuracy in samples, RT60, gain-reduction curve) has not had its math
  verified by anyone, including the author. That is a finding regardless of
  how the code reads.

### 2. Experimentality — is this worth having

Infinite is an instrument for making things nobody asked for. A node that
is a worse version of something already on the canvas is worse than no node.

- **Does it do something the existing set can't?** Check the palette
  (`RegisterNodes()`, `FilterDefs.cpp`, `EffectDefs.cpp`) before accepting a
  new node at all. "Audio Filter but with two bands" is not a new node; EQ is
  a separate node on purpose and that reasoning is written down in
  `ARCHITECTURE.md` — a new one needs the same kind of justification.
- **Is the interesting part reachable?** The good regions of an experimental
  node are usually at the edges of its ranges. A param clamped to the safe
  middle, or a mix knob that never reaches 100% wet, has had the point
  designed out of it. Check the ranges against where the behavior actually
  gets strange.
- **Is it modulatable where it matters?** The params that are worth
  automating should be reachable from the modulator system, not baked as
  constants or hidden behind a dropdown.
- **One mode, not four.** A dropdown switching between more than two
  processing modes is a smell — it means the author shipped four half-nodes
  instead of one good one. This has gone wrong twice here (Dynamics, Delay).
  Collapse to a toggle or cut to the primary mode.

### 3. Design — does it read as an instrument

`docs/plans/audio/audio-node-ui-system.md` is prescriptive and has been
through three revisions; do not re-derive it and do not accept a node that
re-derives it either.

- **One width per body.** `BeginAudioBody` sets the scope; every helper
  derives from `gAudioBodyW` / `gAudioContentW`. A row that sets its own
  width is the exact v2 defect the system exists to prevent. Two legal
  widths: `kAudioNodeWidth` (440) and `kAudioNarrowWidth` (200, ≤2 params).
  There is no third.
- **Control count.** More than ~8 on the card (excluding visualizer and mix)
  means cut, not widen. If a design doc specifies more, the doc is wrong for
  this codebase's bar and should be updated to match the smaller node.
- **The generic-node smell.** Bare `ImGui::SliderFloat` stacks, no sections,
  no readout strip, no visualizer where one is obviously called for — the
  node works but reads as a debug panel. It should read as a plugin.
- **Labels and help.** `InputLabel(slot)` present (or the pin is an
  unlabelled dot), and one sentence in the help table in the existing voice:
  what it does, plus the one non-obvious thing.
- **Naming.** No collision with existing types (`Noise`, `Curve`, `Curves`,
  `Shape`, `Pattern`, `Transform` are taken by visual nodes). Category string
  is one whitespace-free token — `Patch.cpp` parses it with `>>`, so a space
  silently eats the type name on load and corrupts every saved patch.

### 4. Quality — efficiency, performance, and the real-time rules

The hard constraints. Violations here are crashes and dropouts, not opinions.

**Threading / real-time (audio):**
- The **two-object rule**: `INode` on the main thread owns an `AudioNode` on
  the audio thread. They communicate only through `ParamMailbox` (main→audio)
  and `MeterRing` (audio→main). Any shared mutable field is a finding, even
  a "harmless" `bool`.
- **`CookIfNeeded` does no DSP.** It drains meters and pushes dirty params,
  budget < 5 µs. Sample generation there means the node is built wrong.
- **Audio-thread prohibitions** — inside `ProcessBlock` and everything it
  calls: no allocation, no locks, no `dynamic_cast`, no `std::function` /
  `map` / `string`, no GL, no ImGui, no file I/O, no `printf`. Read the call
  tree, not just `ProcessBlock` itself; the violation is usually one level
  down in a helper.
- **Every param declared in `VisitParams` reaches the audio thread.** A param
  that shows in the UI and saves correctly but whose `CookIfNeeded` never
  pushes it does nothing at all. `AUDIOPARAMSWEEPTEST` catches this — confirm
  it ran, don't assume.
- **Teardown.** Deleting the node mid-playback must not leave a dangling
  `AudioCable`/`NoteCable`. Generic machinery handles this; a node that adds
  its own connection bookkeeping has probably broken it.
- **Buffer lifetime.** Anything handing a buffer from main to audio thread
  uses `src/audio/SampleSlot.h`'s pending/active/retire-ring, not its own.

**Caching / invalidation (effects and 3D):**
- **`FilterNode::Signature` completeness.** Anything the shader reads that
  isn't `uSrc`/`uSrc2`'s revision or a declared `FilterParamDef` will freeze
  after the first render. `uTime` is the one sanctioned escape hatch. This
  exact bug shipped once on the 3D side (`08dd3ec`).
- **`FilterParamDef::type` matches the GLSL uniform type.** Mismatch compiles
  and reads garbage, silently.
- **Revision stamps move only on real change.** `MeshRevision()` must not
  bump just because `CookIfNeeded` ran — `DisplacementNode` did this and made
  every stateful downstream node reset every frame.
- **Side-channel forwarding.** A node with an `IGeometrySource*` input
  forwards `GetMesh`, `GetModelMatrix`, `GetMaterial`, `GetSurfaceTexture`,
  `GetMaterialTexture`, `GetMappingTransform` unless it explicitly changes
  them. Dropping one breaks every chain it sits in, with no error. Only
  `GetMappingTransform` has an automated sweep; check the others by reading.

**Drawing cost:**
- **Drawing is the real cost, not the DSP.** Scopes decimated to ~128 points,
  redraw capped at ~30 Hz, collapsed by default. Twenty nodes each drawing
  1024 ImGui segments per frame costs more than every synth and effect
  combined.
- **Shader cost under interactive drag.** A cache-miss re-render happens on
  every param drag; a large blur kernel written as nested samples instead of
  separable passes will feel laggy even though it's "just a filter."

**General C++:**
- Allocation in a per-frame path; `std::string` built per frame for an ImGui
  label; a `std::map` lookup where an index would do; an O(n²) over mesh
  verts or graph nodes that only looks fine at demo scale.

---

## Step 2 — verify before reporting

A finding you haven't confirmed against the code is noise, and noise here is
expensive because the user acts on these.

- **Read the actual code**, not the diff hunk. A dropped forward or a shared
  field is invisible in a hunk and obvious in the file.
- **Build it.** If the change is substantial:
  ```bash
  .claude/skills/run-infinite-hygiene/driver.sh
  ```
  For an audio node specifically, `.claude/skills/audio-node-sweep/driver.sh`;
  for a geometry node, `.claude/skills/geometry-transform-sweep/driver.sh`.
  A sweep failure is a confirmed finding with a reproduction attached — worth
  far more than a read-only suspicion.
- **Distinguish confirmed from suspected** in the report, and say which
  sweep or which file:line confirmed it.
- **Don't report the absence of something the generic machinery provides.**
  `InputCountFor`, connect/disconnect, save/load, undo, copy/paste and the
  topology rebuild are all generic. A node *adding* an entry to one of those
  is the finding — not a node that omits one.

---

## Step 3 — report

Group by the four standards, in the user's order: **Accuracy,
Experimentality, Design, Quality.** Within each, order by severity.

For each finding:

```
[Accuracy · confirmed] src/audio/dsp/FooKernel.h:88 — biquad coefficients
recomputed per sample while cutoff only changes per block.
Why it matters: <the consequence, concretely>
Fix: <the specific change, or the reference to follow>
```

Severity language, used consistently:
- **Blocker** — crashes, corrupts patches, violates the clean-room rule, or
  breaks an invariant with an automated sweep behind it.
- **Should fix** — real defect, no data loss: wrong math, dropped param,
  frozen cache, real-time rule violated in a cold path.
- **Consider** — design and experimentality judgments, and efficiency that
  only bites at scale.

Then close with a **verdict** in one line: ship / ship after the blockers /
send back. Do not pad it — if the code is good, say it's good and list only
what's real. A review that manufactures findings to look thorough trains the
user to ignore the next one.

If the code came from another AI and the same class of defect appears more
than once, say so explicitly and point at the skill it should have been given
(`new-audio-node`, `audio-node-ui`, ...) — that's a more useful fix than
listing the instances.
