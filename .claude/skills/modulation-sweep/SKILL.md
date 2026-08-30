---
name: modulation-sweep
description: Sweeps Infinite's modulation system end to end - every modulation source (LFO, Random, Pattern, Envelope, macros, MIDI CC, analyzers), every destination (any control that registers a ParamRef), and the binding in between (range, polarity, enable, save/load, unbind on delete). Combines the runtime fixtures with the source-only param audit. Use when asked "check the modulators", "does modulation still work", "why doesn't this cable move the knob", "which params can't be modulated", after adding a modulator or any node control, or when a patch loads with its modulation missing or pointing at the wrong knob.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
.claude/skills/modulation-sweep/driver.sh
```

`--skip-build` reuses `build/`. Runs the asserted fixtures, prints the value
trace from the observation fixture, and regenerates
`docs/node_param_audit.md`.

## The three halves of "modulation works"

**1. The source produces a value.** Any node implementing `IModulator` and
returning 0..1 from `Value01()`. The trap specific to sources is idempotency -
`Value01()` is called once per binding, plus once per panel, plus once per
downstream modulator, so it must be a pure function of the transport clock
rather than a step-forward. See `new-modulator-node` §2; `MODTEST`'s value
trace is where a rate that depends on load shows up.

**2. The destination is reachable.** A control is modulatable if and only if
its widget calls `Modulation::RegisterParam` with a `ParamRef` - which is the
same single fact behind "it draws a pin", "it takes a cable" and "it appears
in the performance matrix's Assign Parameter picker". A raw
`ImGui::SliderFloat` registers nothing. `scripts/audit_node_params.py` (run by
the driver) inventories every node's controls and marks each reachable or not;
see `node-param-audit` for how to read it.

**3. The binding maps one onto the other.** `Modulation::Source`
(`src/core/Modulation.h`) holds `lo`/`hi` **in the destination's own units**,
plus `enabled`, plus the legacy `polarity`/`depth`/`centre` fields that exist
only so patches saved before lo/hi decode to the identical range. The apply
loop computes `lo + (hi - lo) * clamp(v01, 0, 1)` and then snaps to the
destination's grid (`ShapeToParam`). Anything surprising about a modulated
value is almost always the range or the snap, not the source.

## What each fixture proves

| Fixture | Proves |
|---|---|
| `MODMATRIXTEST` | bind, unbind, enable/disable; a disabled binding leaves the destination where it was rather than snapping back |
| `MODMATRIXGEOM` | the matrix pads its rows in scroll-invariant coordinates - a screen-space computation made the table grow one row per scroll step and scroll without end |
| `MODBOUNDSTEST` | range mapping, clamping, and integer snapping at the destination |
| `MACROTEST` | macro controls as sources, across a save/load |
| `PERFMATRIXTEST` | the performance matrix's patch round trip, including multi-target elements and page names (headless) |
| `ROUNDTRIPTEST` | every registered node type's params survive save/load |
| `MODTEST` *(observation)* | LFO → Shape size: beats, source value and destination value per frame. No verdict - read the trace |

## The known fragility: float params are addressed by draw order

- **Float** destinations are keyed by an ordinal (`gParamCounter++`, 0..399).
  A node that draws a float control *conditionally* renumbers every float
  param below it, and existing bindings land on a different knob.
- **Discrete** destinations (bool/enum) are keyed by a hash of their label
  into 400..799 (`DiscreteParamSlot`), specifically so a conditionally-drawn
  dropdown cannot renumber its neighbours. A cable into a control not drawn
  this frame goes inactive and reattaches when it returns.

This is known and unfixed - see `docs/plans/performance-matrix/README.md`. If a
bug report says "the cable jumped to a different control when I changed a
mode", this is why. Prefer drawing float params unconditionally (disabled
rather than hidden).

## Blind spots

- **Nothing sweeps every source against every destination type.** The
  fixtures use Shape/Range-to-Range/LFO as stand-ins. A modulator whose
  `Value01()` misbehaves only under a specific destination's snapping is not
  covered.
- **The idempotency rule is not asserted anywhere.** `MODTEST` would *show* a
  rate that changes with the number of bindings, but only if someone reads the
  trace with that question in mind. A fixture that binds one source to N
  destinations and compares the trace against N=1 is the missing check.
- **Expressions** (`Modulation::SetExpression`) share the destination pin with
  cables - an expression only drives the field while nothing is patched. No
  fixture covers the handover in both directions.
- **MIDI CC / analyzer sources** need real input; they are not exercised
  headless.
