---
name: node-param-audit
description: Inventory every Infinite node's on-screen controls and which of them a modulation cable can reach. Regenerates docs/node_param_audit.md from src/main.cpp - a per-node table of every knob, slider, checkbox, dropdown and colour swatch, marked modulatable (registers a ParamRef, so it draws a pin, takes a cable, and appears in the performance matrix's Assign Parameter picker) or not. Use when asked "can this param be modulated", "which controls are still not modulatable", "what params does node X have", after adding or changing any Draw*Body / Draw*Params function, or before claiming that everything is modulatable.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
python3 scripts/audit_node_params.py --out docs/node_param_audit.md
```

It prints a one-line summary and writes the full report. It reads source only -
it does not build, launch, or drive the app, which is the point: spawning all
~135 node types by hand and eyeballing each control is exactly what this
replaces.

## What "modulatable" means here

One property, three consequences. A control is modulatable when its widget
calls `Modulation::RegisterParam` with a `ParamRef`. That single registration
is what makes it:

1. draw a modulation pin,
2. accept a dropped cable,
3. appear in the performance matrix's *Assign Parameter* picker
   (via `gParamPinScreenList`).

So "I can't drag a cable onto it" and "I can't assign it to a perf control" are
never two bugs. They are always the same missing registration.

## The widget families

Modulatable wrappers (in `src/main.cpp`):

| widget | for |
| --- | --- |
| `ModKnob` / `ModKnobInt` | knobs |
| `ModSlider` / `ModSliderInt` | sliders |
| `ModCheckbox` | checkboxes / bools |
| `DropdownButton` | enum dropdowns in a params block |
| `AudioBareDropdown` | header dropdowns whose button text is the label (octave, semitone, wavetable) |
| `AudioKnobRow::Knob/KnobInt/Fader/Dropdown/DropdownKnob` | audio node rows |
| `ModColorEdit` | colour swatches (palette bindings, separate pin block) |

Raw `ImGui::SliderInt`, `ImGui::DragFloat`, `ImGui::Checkbox`, `ImGui::Combo`
etc. register nothing. Finding one of those in a `Draw*Params` function is the
bug; swapping it for the `Mod*` equivalent above is the fix. `ImGui::InputText`
and `ImGui::Button` are excluded from the gap count on purpose - text and
actions are not values.

## Addressing, and why it is fragile

- **Float params** are addressed by a *draw-order ordinal* (`gParamCounter++`,
  range 0..399). A node that shows a control conditionally renumbers every
  float param below it. This is a known, unfixed fragility - see
  `docs/plans/performance-matrix/README.md`.
- **Discrete params** (bool / enum) are addressed by a *hash of their label*
  into 400..799 (`DiscreteParamSlot` in `src/main.cpp`), precisely so a
  conditionally-drawn dropdown cannot renumber its neighbours. A cable into a
  control that is not drawn this frame simply goes inactive - the link pass
  skips any link whose pin was not declared - and reattaches when it returns.

If a bug report says "the cable jumped to a different control when I changed a
mode", check which of the two schemes the destination is on.

## Reading the report

Each node gets a table of `control | type | modulatable`. Nodes with any
non-text gap are flagged in the heading with a count, so a scan for `**` finds
every node that still has an unreachable control. The header block carries the
totals - a regression shows up as the modulatable count dropping.

Node types listed under "no Draw* function found" (Group, Null, Viewport,
Constant, Output) draw through shared/generic paths the script cannot
attribute; check those by hand if they matter.
