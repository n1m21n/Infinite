# Prompt: Pattern modulator → real step sequencer UI

In /Users/namansoni/infinte. Read this whole file before writing code.

## Goal

`PatternNode` (Modulators/Pattern) is a good sequencer wearing a bad UI:
eight stacked `ModSlider` rows. Replace the body with a **step grid of
draggable vertical bars** — the thing every hardware step sequencer looks
like — that can display bipolar steps and that adapts its step count to
the transport's time signature.

## Verified current state

- `PatternNode` — `src/nodes/ModulatorNodes.h:109-147`. `kSteps = 8`,
  `float steps[kSteps]` (0..1), `int length`, `float stepBeats`,
  `bool smoothSteps`, `float low`, `float high`. `CurrentStep()` returns
  the playhead. `Value01()` in `src/nodes/ModulatorNodes.cpp`.
- Serialization: `VisitParams` writes per-step keys `"step0".."step7"`
  (`ModulatorNodes.h:133-143`). Adding `"step8".."step15"` is additive —
  old patches simply leave the new ones at their defaults. Safe.
- Current UI: `DrawPatternParams`, `src/main.cpp:3396-3416`. It already
  highlights the active step by pushing `ImGuiCol_FrameBg`.
- Registered at `src/main.cpp:2257`. Help text at `src/main.cpp:12701`
  and `13130` — both need updating.
- Transport time signature: `Transport::TimeSigNumerator()` /
  `TimeSigDenominator()` (`src/core/Transport.h:87-88`), beats-per-bar
  computed at `Transport.h:94`.
- The house UI conventions for audio-node bodies are in the
  **`audio-node-ui` skill** — read it before drawing anything. It covers
  step grids specifically.

## What to build

### Steps

Raise `kSteps` to **16**. `length` (1..16) controls how many are live and
how many bars are drawn. Default `length` stays 8 so nothing visibly
changes for existing patches.

Add a **"fit to bar"** control: when on, `length` is driven from
`Transport::TimeSigNumerator()` (e.g. 4/4 → 4 steps per bar × a
steps-per-beat setting), so the pattern lines up with the bar instead of
free-running. Keep this to one checkbox plus at most one divisor
dropdown — do not build a full rhythm editor. When off, `length` is
manual, as today.

### The bar grid

Replace the eight sliders with a single custom widget drawn on
`ImGui::GetWindowDrawList()`, following `DrawCurveEditor`
(`src/main.cpp:3898-3992`) as the structural model — it already
establishes the pattern for an interactive `ImDrawList` widget in this
codebase (invisible button for input capture, static drag-target pointer,
screen-space mapping).

Behaviour:

- One vertical bar per live step, filling the node's body width.
- **Click-drag sets a bar's value. Dragging horizontally across bars
  paints them** — this is the single most important interaction; a
  sequencer you have to set one slider at a time is the thing being
  replaced.
- Shift-drag = fine adjust. Right-click a bar = reset to centre.
- The playhead step (`CurrentStep()`, only when `< length`) is drawn
  highlighted, keeping the existing amber accent
  (`IM_COL32(255, 190, 90, 255)` is used for modulator meters at
  `src/main.cpp:12560`).
- Step index labels along the bottom, beat/bar boundaries marked with a
  brighter gridline so 4/4 groupings read at a glance.

### Bipolar display

Add `bool bipolar = false` to the node (serialized via `VisitParams`).

**`steps[]` stays stored as 0..1 regardless.** No serialization change,
no contract change. `bipolar` only changes rendering and drag mapping:

- off: bars grow from the bottom, value = 0..1 as today.
- on: a centre line at 0.5; bars grow **up from centre for values > 0.5
  and down from centre for values < 0.5**, and the readout shows
  −1..+1 (`display = (v - 0.5) * 2`).

A bar drawn below centre genuinely subtracts once its binding is in
bipolar mode — that's `00-modulation-polarity.md`. **Read that file
first**; this node's bipolar toggle is a display convention that only
means something musically because of the binding mode. If 00 hasn't
landed yet, this still works, it just reads as "below the middle".

### Keep

`stepBeats`, `smoothSteps` (glide), `low`, `high` — all unchanged, drawn
below the grid as ordinary rows. Do not add anything else. Target ~7
controls total (`length`/fit, `stepBeats`, glide, bipolar, low, high).

## Exit criteria

1. `cmake --build build -j"$(sysctl -n hw.ncpu)"` clean.
2. An existing saved patch containing a Pattern node loads with its eight
   step values intact and sounds/looks identical.
3. Set 16 steps with varied values, save, quit, reload — all 16 survive.
4. Horizontal paint-drag across the grid works and feels right.
5. Playhead highlight tracks the transport and stops at `length`.
6. Toggling bipolar changes only the drawing, never the stored values —
   verify by toggling it, saving, and diffing the patch file.
7. Update the two help-text strings (`src/main.cpp:12701`, `13130`).
8. Run `/run-infinite-hygiene`.
