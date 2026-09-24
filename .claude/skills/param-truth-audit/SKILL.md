---
name: param-truth-audit
description: "Check that DSP does what the control says: declared range vs DSP clamp/formula, BeginDisabled gates vs real short-circuits, mailbox params actually read on every implied path. Use after adding/changing a node param, clamp or disabled gate, before claiming \"the knob does X\", or for \"does the UI match the DSP\"."
---

## When to use (full scope)

Checks that a node's DSP does what its knob/slider/checkbox tells the user it does - numeric range agreement between a control's declared bounds and the DSP clamp/formula that actually consumes it, whether a control that looks gated (BeginDisabled) matches what the DSP actually short-circuits, and whether a param that's pushed into the mailbox is actually read by every code path its UI implies it reaches. Use after adding or changing any node param, any std::clamp on a mailbox value, any BeginDisabled/EndDisabled gate on a control, before claiming "the knob does X", or when asked "does the UI match the DSP", "is this knob's range right", "check the params against the math".

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Why this exists

Two bugs shipped in AnalogNode the same day, both the same shape: **what a
control visually promised and what the DSP actually did had quietly drifted
apart**, and nothing in the existing sweeps would have caught either.

1. The `pw` knob was wrapped in `BeginDisabled()` unless `wave1` or `wave2`
   was Square - implying pw only affects Square oscillators. True for osc1,
   false for osc2: the DSP's osc2 `Generate()` call never received `pw1` at
   all, so it silently ran at a hardcoded 0.5 regardless of what the knob
   said or what wave2 was set to. The knob's *gate* lied about what the DSP
   actually consumed.
2. The `fm` knob declared and documented `0..1`. The DSP clamped its
   consumer to `0..2` - a full octave of range the knob could never dial in,
   left over from an earlier version of the formula. The knob's *range*
   didn't match the DSP's own stated range for the same value.

`node-param-audit` inventories which controls are modulatable.
`audio-node-sweep` proves a param round-trips through save/load and reaches
the audio thread within a block. Neither one reads the DSP's own math and
compares it to what the control claims - that gap is this skill.

## Pattern classes

Every "UI lied about the DSP" bug found so far is one of these five shapes.
Work an audit by going down this table, not by running the script and
stopping. Detection methods verified against this codebase, not assumed:

| # | Pattern class | What disagrees | Detection | Status here |
|---|---|---|---|---|
| 1 | Numeric range mismatch | knob's `[lo,hi]` vs. DSP's `std::clamp` bounds on the same mailbox param | mechanical - `scripts/audit_param_truth.py` | covered, see below |
| 2 | Param dropped on a sibling call site | a param visually scoped to "the sound" is threaded to one of N symmetric sources (oscillators/channels/taps) but not the others | manual - diff the call sites side by side | not scriptable (needs call-graph judgement) |
| 3 | Gate vs. DSP disagreement | `BeginDisabled()` condition says a control is inert/live in a state where the DSP does the opposite | manual - read the gate condition against every DSP branch it's meant to cover | not scriptable (needs semantic match) |
| 4 | Label/unit/taper mismatch | caption/format string/taper (`dbTaper`, `%.0f Hz`) implies a curve or unit the DSP doesn't actually apply | manual - compare caption+taper call to the formula that consumes the field downstream | not scriptable (needs meaning, not bounds) |
| 5 | Undiscoverable ceiling | a value the DSP clamps but the UI shows no min/max for (text input, mod-only param) | manual - check tooltip/help text states the effective ceiling | not scriptable (absence-of-text check) |

Two other shapes were hypothesized and checked against the actual codebase,
not just assumed absent:

- **Pre-push clamp** (clamping `p.field` before `mMailbox.Push(kFoo,
  p.field)` rather than after, at the `SmoothedValue` read) - checked all 65
  `mMailbox.Push(k` call sites, zero wrap their argument in `std::clamp`/
  `std::min`/`std::max`. Not a pattern that exists here today; if one ever
  does, class 1 above would need a second regex arm to catch it, since the
  current script only looks at the post-Push clamp site.
- **Manual min/max clamp idiom** (`std::max(lo, std::min(hi, x))` in place of
  `std::clamp`) - 178 occurrences in `src/main.cpp` + `src/nodes/`, but zero
  co-occur with `SmoothedValue` on the same line. These are internal DSP math
  bounds (filter coefficients, safety limits inside an algorithm), not
  knob-consuming clamps, so they're out of scope for class 1 and don't need
  their own detection - they aren't a UI/DSP disagreement because no knob
  claims a range over them.

## Run this first

```bash
python3 scripts/audit_param_truth.py
```

Traces every `std::clamp(mMailbox.SmoothedValue(kFoo), lo, hi)` in
`src/nodes/*.{h,cpp}` back to the struct field that feeds `kFoo` (via
`mMailbox.Push(kFoo, params.field)`), finds that field's own knob/slider call
in its node's `Draw*Body` in `src/main.cpp` (scoped to that one node's dispatch
- see below - so an unrelated node with a same-named field, e.g. two nodes
both having a `detune`, never gets cross-matched), and reports:

- **RANGE MISMATCH, DSP narrower** - the knob visibly lets the user drag past
  a point where the DSP has already stopped responding. The audible lie: the
  knob keeps moving, the sound stops changing. Fix by widening the DSP clamp
  to match the knob, *if* the DSP can actually do something meaningful out
  there - otherwise narrow the knob instead (case-by-case, not automatic).
- **RANGE MISMATCH, DSP wider** - dead headroom the knob can never reach (the
  `fm` bug). Fix by narrowing the DSP clamp to match the knob's documented
  range, unless there's an actual reason to widen the knob and expose that
  headroom - that's a design call, make it deliberately.
- **untraceable clamps** - bounds that aren't bare numeric literals (Nyquist
  guards like `(float)mSampleRate * 0.48f`) or that pass through an affine
  transform before the clamp (`0.5f + SmoothedValue(...) * 8.0f`). Not
  necessarily bugs - most are deliberate safety clamps or tapers - but the
  script can't verify them, so it lists them instead of skipping them
  silently. Eyeball each one: does the *effective* range the affine transform
  produces still roughly match what the knob's caption and format string
  (`"%.0f Hz"`, `"%.2f"`) imply to the user?

The script is static analysis only (regex over source, same technique as
`node-param-audit`'s `audit_node_params.py`) - no build, no launch. It covers
pattern class 1 only. Classes 2-5 from the taxonomy above have no script -
work them by reading the code, per node:

1. **Class 2 - dropped on a sibling call site.** If a node generates from two
   or more sources (oscillators, channels, taps, voices), grep every
   `Generate(`/process call each source makes and confirm a param that
   visually applies to "the sound" (not obviously scoped to just one source)
   is actually threaded to all of them - not just the first one written.
   This is exactly the pw/osc2 bug: it wasn't a missing clamp, it was a
   parameter silently dropped on one of two symmetric call sites. Diff the
   call sites for each source side by side; a parameter present on one and
   absent on the sibling, with no comment explaining why, is the defect.

2. **Class 3 - gate vs. DSP disagreement.** Read the `BeginDisabled()`
   condition (`wave1 != kAWaveSquare && wave2 != kAWaveSquare`), then read
   the DSP path for every state that condition allows through vs. blocks. If
   there's a state where the gate says "inert" but the DSP still reads and
   uses the value (or vice versa - gate says "live" but the value never
   reaches the DSP for that state), that's the same class of lie as a range
   mismatch, just boolean instead of numeric. Related but distinct from
   `invariant-interaction-audit`, which looks for a *sibling control* undoing
   a guarantee - this is about the gate and the DSP disagreeing on the same
   control's own state.

3. **Class 4 - label/unit/taper mismatch.** A knob captioned `"%.0f Hz"`
   whose backing field is actually consumed as a ratio or a cents offset
   downstream is a truthfulness bug even with a perfectly matched numeric
   range. A knob using `dbTaper`/`freqTaper` implies a specific perceptual
   curve - if the DSP applies a *different* curve to the same value (e.g. the
   UI tapers logarithmically but the DSP treats the raw value linearly), the
   knob's motion won't track what the user hears across its range the way its
   taper choice promises it will.

4. **Class 5 - undiscoverable ceiling.** A value that's clamped in the DSP
   but not visibly bounded in the UI (no `min`/`max` shown, e.g. a text input
   or a modulation-only param) still needs its effective ceiling discoverable
   somewhere - a tooltip, the caption's format string, or the node's help
   text - so a user driving it from a modulation source isn't guessing where
   it saturates.

## After finding something

- Class 1, numeric range mismatch: fix per the script's suggested direction
  above, rebuild, rerun `INFINITE_DSPTEST=1` (existing DSP self-tests should
  be unaffected unless you changed real behavior, not just dead headroom),
  rerun the script to confirm clean.
- Class 2, dropped on a sibling call site: fix by threading the param to the
  missing call site, then check whether the same shape exists elsewhere in
  the same node (a third oscillator? a stereo pair?) and in sibling nodes
  with the same multi-source structure - `invariant-interaction-audit`'s
  "check every other call site" habit applies here too.
- If you touched any node UI while fixing this, `node-ui-pillars` still
  governs layout - a range fix is not a license to also restyle the row.
