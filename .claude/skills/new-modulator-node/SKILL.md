---
name: new-modulator-node
description: The standard procedure for adding a Modulators/Macros node to Infinite - the IModulator contract, the idempotency rule that makes Value01() safe to call many times per frame, multi-output modulators, mod-matrix/performance-matrix visibility, unbinding on delete, and the machine-checkable exit criterion. Use when implementing an LFO/random/pattern/math/shaper/macro-control node, when writing the prompt for a fresh session that will implement one, or when a modulator node's value freezes, runs at the wrong speed, jitters differently depending on how many params it drives, or vanishes from the modulation matrix.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

This is the control-signal sibling of `new-audio-node`, `new-effect-node` and
`new-geometry-node`. Read one of those first if this is your first node in
Infinite - registration, `VisitParams`, `Draw*Params` and the branch workflow
are shared. What is specific to this family is below.

---

## 0. The shape: a modulator is an INode that renders nothing

`src/core/Modulation.h`:

```cpp
class IModulator
{
public:
   virtual float Value01() = 0;   // normalised 0..1, always
};
```

That is the entire producer contract. Everything else follows from it:

- The **modulator speaks 0..1**; the *destination* maps that onto its own
  range. This is why one Macro Knob can drive a `freq` in Hz and a `mix` in
  0..1 at once. Returning 1.7 or -0.2 is a bug, not a "wider range" - see
  `ConstantNode::Value01`, which clamps for exactly this reason.
- A modulator **has no image output**. Every one of them overrides
  `GetOutputTexture() -> 0`, `GetOutputWidth/Height() -> 0` and an empty
  `CookIfNeeded(int) {}` (see `LFONode` in `src/nodes/ModulatorNodes.h`).
  The editor draws a value meter instead of a preview.
- Class shape is always `class FooNode : public INode, public IModulator`.

Two categories use this interface, and the difference is intent, not
mechanism (`ConstantNode`'s class comment states it): **Modulators** generate
or transform a signal (LFO, Random, Pattern, Math, Compare, Smoothing,
Envelope, Mod Curve, Path, Image/Audio Analyze); **Macros** are performed by
hand (`src/nodes/MacroNodes.h` - Knob, Slider, XY, Toggle, Trigger, NumBox,
Radio Selector, Step Gate).

---

## 1. Read these before writing code

| File | Why |
|---|---|
| `src/core/Modulation.h` | `IModulator`, `ParamRef`, `Modulation::Source` (lo/hi range, enabled, legacy polarity) - the whole binding model |
| `src/nodes/ModulatorNodes.h` / `.cpp` | ~20 worked examples; find the closest shape and copy it |
| `src/nodes/MacroNodes.h` | if this is a performed control rather than a generator |
| `src/core/Transport.h` | the clock every time-varying modulator must read from |
| `.claude/skills/node-param-audit/SKILL.md` | how a control becomes modulatable at all; read before writing the node's own params |

---

## 2. The idempotency rule - the trap unique to this family

**`Value01()` is called an unbounded number of times per frame.** Once per
binding in the modulation apply loop, again by the modulation matrix panel,
again by the value meter on the node body, and again by every downstream
modulator that has it patched into an input pin (`MathNode::Value01` calls
`inputA->Value01()`). Nothing deduplicates those calls.

So **`Value01()` must be a pure function of the clock and the node's params,
not an advance-by-one-step.** Every stateless modulator in the file follows
this - `LFONode`, `RandomNode` and `PatternNode` all compute position from
`Transport::Instance().Beats()` and derive the value from it:

```cpp
const double beats = Transport::Instance().Beats();
const double pos   = beats / std::max(0.01f, rateBeats);
```

A modulator that genuinely needs state (a one-pole filter, an envelope
follower) uses the **beats memo** idiom instead - `SmoothNode::Value01`:

```cpp
if (mLast >= 0.0f && beats == mLastBeats)
   return mLast;               // already advanced this tick - don't double-apply
mLast = mLast + (target - mLast) * (1.0f - k);
mLastBeats = beats;
```

Symptom when you get this wrong: the modulator's rate changes depending on
how many params it drives, or how many panels are open. That is not a
performance problem, it is this bug.

**Reading the clock is also what makes a modulator pause-aware.** Rates are
in *beats*, not seconds, so changing global BPM retimes everything and Pause
freezes it. A modulator that reads wall-clock time keeps running while the
transport is paused - a real inconsistency users notice immediately.

---

## 3. Multi-output modulators

A node carrying more than one value (Macro XY) reports it on `INode`:

```cpp
int OutputCount() const override { return 2; }
const char* OutputLabel(int i) const override { return i == 0 ? "x" : "y"; }
IModulator* ModulatorOutput(int i) override;   // distinct object per index
```

Returning `nullptr` from `ModulatorOutput` falls back to the node itself for
output 0, which is what every single-output modulator wants - do not override
it unless you actually have several values. `ModulatorForOutput` (main.cpp)
is what the connect path uses, so a multi-output node that forgets
`ModulatorOutput` silently binds every pin to output 0.

---

## 4. The wiring checklist

1. **Class** in `src/nodes/ModulatorNodes.h`/`.cpp` (or `MacroNodes.*` for a
   performed control), deriving from `INode` *and* `IModulator`. New file
   pair only if the node is big enough to deserve one - then add it to
   `CMakeLists.txt`.
2. **`VisitParams`** listing every saveable field. Names are stable patch
   keys - renaming one silently drops it from existing patches.
3. **Register** in `RegisterNodes()` (`src/main.cpp` ~3350):
   `REGISTER_NODE(FooNode, Foo, "Modulators");`. The category string must
   stay one word - `Patch.cpp` reads it with `>>`, and a space corrupts the
   save format.
4. **`Draw*Params` / `Draw*Body`** in `main.cpp`, using the `Mod*` widget
   family (`ModKnob`, `ModSlider`, `ModSliderInt`, `ModCheckbox`,
   `DropdownButton`, `ModColorEdit`) so the modulator's *own* params are
   themselves modulatable. A raw `ImGui::SliderFloat` here is the single
   most common defect this family ships with - see `node-param-audit`.
5. **Input pins**, if it transforms rather than generates: override
   `ModulatorInputSlot(int)` and `ModulatorInputCount()`, and fall back to a
   `constantIn`-style field when nothing is patched (every transform node in
   `ModulatorNodes.cpp` does: `input ? input->Value01() : constantIn`). A
   transform with no fallback reads 0 when unpatched, which looks like a dead
   node.
6. **Nothing to do for deletion** - `Modulation::UnbindAllFor(nodeIndex)`
   already drops a deleted node both as target and as source generically.
   Do not add a per-node teardown path.
7. **Nothing to do for the matrices** - a node that is an `IModulator`
   appears in the modulation matrix automatically, and its params appear in
   the performance matrix's *Assign Parameter* picker as soon as they
   register a `ParamRef` (step 4).

---

## 5. Bug traps, each of which has already happened here

- **Unclamped `Value01()`.** The destination maps 0..1 onto its own span; a
  value outside that range writes past the param's declared min/max before
  `ShapeToParam` clamps it, producing a control that "sticks" at its ends.
- **Advancing state per call** (§2). The single defining trap of this family.
- **Wall-clock instead of `Transport::Beats()`** (§2) - keeps running through
  Pause and ignores BPM.
- **Two spawned copies marching in lockstep.** `RandomNode::NextSeed()`
  exists precisely because a shared default seed made every Random on the
  canvas identical. Any node with pseudo-randomness needs a per-spawn seed.
- **Float params are addressed by draw-order ordinal** (`gParamCounter++`),
  so a conditionally-drawn float control renumbers every float param below
  it and existing cables land on the wrong knob. Discrete params are
  label-hashed and safe. Prefer drawing float params unconditionally
  (disabled, not hidden); see `node-param-audit/SKILL.md` for the full rule.
- **A "bipolar" modulator implemented as -1..1.** The convention here is that
  bipolarity lives in the *binding's* lo/hi range (`Modulation::Source::lo/hi`)
  or in `ModDepthNode`, not in the producer's output range.

---

## 6. Tests

```bash
.claude/skills/modulation-sweep/driver.sh
```

That covers the generic invariants (bind/save/load round trip, matrix
visibility, bounds, unbind-on-delete). Add a node-specific fixture only for
behaviour a generic sweep cannot know - a shape table, an analytic rate, a
curve's fixed points - following the `INFINITE_MODTEST` block's pattern.

`python3 scripts/audit_node_params.py --out docs/node_param_audit.md` and
check the new node's row: every control that should take a cable must read
modulatable.

---

## 7. Exit criterion - state it machine-checkably in every prompt

> The node is done when: `.claude/skills/run-infinite-hygiene/driver.sh`
> passes with the node registered (which round-trips all ~167 node types
> through save/load and spawns each one with its params open);
> `.claude/skills/modulation-sweep/driver.sh` passes; the node's row in
> `docs/node_param_audit.md` shows no non-text gaps; and driving one
> destination param with it produces the same value trace as driving eight
> (the §2 idempotency rule), verified by binding it to several params at once
> and reading the modulation matrix.

---

## 8. Prompt template for a fresh session

> Add a `<name>` modulator to Infinite (category `Modulators` / `Macros`).
> Follow `.claude/skills/new-modulator-node/SKILL.md` exactly.
> Behaviour: `<what the value does over time / as a function of its input>`.
> Params: `<list, with ranges and defaults>`.
> It must derive from `INode` *and* `IModulator`, return 0..1 clamped from
> `Value01()`, compute that value as a pure function of
> `Transport::Instance().Beats()` and its params (or use the SmoothNode beats-memo
> idiom if it is genuinely stateful), declare every param in `VisitParams`,
> and draw its controls with the `Mod*` widget family so they are themselves
> modulatable. Start on a branch per `.claude/skills/git-branch-workflow/SKILL.md`.
> Done when the exit criterion in §7 of that skill is met.
