# Audio node UI — design review (pre-P3b)

Review of the shipped v2 audio node UI (`audio-node-ui-system.md` v2, implemented
in `src/main.cpp:3531-3846`) against the screenshots of Oscillator / Gain / Mixer
/ Note Sequencer, and against how commercial synth UIs actually solve the same
problems. Written before P3b so the grammar is fixed once, not re-fixed 30 times.

Every claim below is verified against the tree; file:line references are live.

---

## 0. Verdict

The v2 spec is not wrong about *intent* — wide horizontal rack strip, knobs for
performance params, one visualizer, nothing hidden. It is wrong about
**mechanics**, and the mechanics are the entire reason it looks bad.

The single root cause, from which most of the visible ugliness follows:

> **`kAudioNodeWidth` (440px, `src/main.cpp:135`) is a number the spec talks
> about but nothing enforces.** imgui-node-editor sizes a node to its widest
> child. Nothing in `DrawAudioNodeBody` constrains the node to 440 — the
> visualizer is *drawn* at exactly 440, `NodeSeparator` draws its rule at
> `kPreviewSize` (190px, `src/main.cpp:1229-1236`, never updated for audio
> nodes), and the knob row is whatever it happens to add up to (~556px for
> Oscillator). So one node body contains **three different widths**, and the
> node's real width is set by the widest of them.

That is exactly what the Oscillator screenshot shows: the black scope box stops
~75% across the node, and the `tuning` / `phase / stereo` / `fm (2-op)` rules
stop at ~30%. Nothing aligns to either edge because nothing shares an edge.

The second root cause is a spec decision, not a bug:

> **v2 overcorrected v1's over-hiding into "nothing is hidden, ever" (§1).**
> The Oscillator is now ~800px tall and 17 rows deep, in a canvas where it sits
> next to 3D and image nodes. No shipping synth does this. Vital, Serum, Phase
> Plant and Bitwig's Polymer all show *everything* too — but inside a **fixed-
> size frame**, in grouped panels laid out in 2–3 columns, never in one
> ever-growing vertical column. "Always visible" and "unbounded height" are
> separable, and v2 conflated them.

---

## 1. Verified defects

### 1a. Hover value renders in the wrong place — real bug, cheap fix

`KnobFloat` calls `ImGui::SetTooltip` at **`src/main.cpp:895`**, inside
`ed::Begin()`/`ed::End()` (`src/main.cpp:10223` … `16284`) with no
`ed::Suspend()`/`ed::Resume()` around it. imgui-node-editor applies the canvas
transform to anything drawn between those calls, so the tooltip is positioned
and scaled in *canvas* space instead of screen space — it lands offset from the
cursor by an amount proportional to zoom and pan. That is precisely the `0.72`
box floating up-and-left of the `detune` knob in screenshot 2.

The same bug exists on the drag-rejection tooltip at **`src/main.cpp:15243`**.
(The only tooltip in the file that is correct is `src/main.cpp:9727`, which is
outside the editor entirely.)

Fix: wrap both in `ed::Suspend()` / `ed::Resume()`. But see §3d — the *design*
answer is to not use a tooltip for knob values at all.

### 1b. `NodeSeparator` is hard-coded to the image-node width

`src/main.cpp:1229-1236` draws its rule and its `ImGui::Dummy` at `kPreviewSize`
(190px). Every `NodeSeparator("tuning")` etc. in an audio node therefore draws a
stub line across a third of the node. It needs a width parameter.

### 1c. The knob row is wider than the visualizer, and overflows its budget

Per-knob cost in `ModKnob` (`src/main.cpp:932-944`): 14px mod pin + 4px gap +
52px knob = **70px**, plus 6px `ItemSpacing` (`src/main.cpp:8359`) = 76px pitch.
`DropdownButton` costs `width` + 6 + the label it draws *to its right*
(`src/main.cpp:447-455`) ≈ 176px for `waveform`.

Oscillator's Tier 1 row = 176 + 5×76 − 6 ≈ **556px**, against a declared
`kAudioNodeWidth` of 440. The node grows to 556; the scope stays 440. Confirmed
by the screenshot's ~0.79 box-to-body ratio.

### 1d. Gain is 440px wide to hold one knob

`DrawGainBody` (`src/main.cpp:3672-3678`) calls `DrawLevelMeterVisualizer`,
which is unconditionally `kAudioNodeWidth` wide (`src/main.cpp:3591`). §7 of the
spec explicitly says a one-param node should *not* be padded with chrome to look
complete — and then the visualizer does exactly that. Screenshot 3 is the spec
contradicting itself.

### 1e. Labels sit in a ragged right column

`ModSlider`/`ModSliderInt` use ImGui's default label placement: label to the
*right* of the widget. Two-per-row at `kAudioHalfWidth` produces
`● [slider 0] octave  ● [slider 0] semi` — four ragged text columns per row, the
value floating mid-track, and the label visually detached from its control.
`DropdownButton` does the same (`Sine    waveform`).

No plugin does this. Universal convention: **label under the knob, or label-left
/ value-right inside a horizontal slider's own track.**

### 1f. No visual hierarchy in the knob row

Every knob is `kKnobDiameter` = 52px. `freq`, `volume`, `width`, `unison`,
`detune` all read as equally important. Real oscillator panels size by
importance — one or two large knobs (level, the defining param), the rest
small. Uniform sizing is why the row reads as a spreadsheet of dials.

### 1g. The mod pin breaks the knob grid rhythm

Each knob is preceded by a 14px pin dot on its *baseline*, not its center, so
the row reads as `· ◯ · ◯ · ◯` with the dots floating above the knob caps
(visible in every screenshot). It also costs 18px × 6 = 108px of the row's
horizontal budget — a third of the overflow in §1c.

### 1h. Vertical misalignment inside the knob row

`DropdownButton` is a ~22px-tall button; `ModKnob` is 52 + 3 + textline ≈ 70px.
`ImGui::SameLine()` baseline-aligns them, so the dropdown floats near the top of
the row while knob captions sit at the bottom. Same problem for the step grid
row in Note Sequencer.

### 1i. Dead visualizers

`DrawWaveformVisualizer` (`src/main.cpp:3570`) draws nothing when
`scopeCacheCount <= 1` — a silent oscillator is a plain black rectangle with no
zero line, no grid, no waveform hint. Same for the Gain/Mixer level meter at
level 0 (`src/main.cpp:3596`). Screenshots 1 and 3 are both showing this. A
visualizer that is blank at rest looks broken, not idle.

### 1j. Note Sequencer specifics

`DrawNoteSequencerBody` (`src/main.cpp:3740-3780`):
- The step grid is 14px cells at 16 steps ≈ 256px, in a node that is ~550px
  wide — it occupies less than half the width it has, hugging the left edge.
- `ImGui::TextDisabled("%d steps - %s")` duplicates information the grid and
  the `pattern` dropdown both already show.
- `gate length` / `step count` are full-width `ModSlider`s at
  `kAudioNodeWidth` while the section rule above them is 190px — the worst of
  the §1b mismatch, and clearly visible in screenshots 4 and 5.
- Layout *changes shape* between Grid / Euclidean / Polyrhythm (different
  numbers of rows), so the node resizes when you change pattern.
- Nothing shows the playhead. A sequencer whose grid doesn't show which step is
  currently firing is missing its single most useful piece of feedback.

### 1k. Patch-level: audio nodes don't read as a family

Screenshot 5: Note Sequencer (green), Oscillator (blue), Gain (grey), Mixer
(grey) are four different widths, four different heights, four different
internal rhythms. The category tint (`CategoryColors.cpp:42-45`) is the only
thing tying them together, and it's applied to the title text only. Compare an
actual rack: modules differ in width but share a **baseline grid** — same row
height, same knob sizes, same label typography.

---

## 2. What the benchmarks actually do

Grounded in the visible UI of Vital, Serum, Phase Plant, Bitwig Polymer and
Ableton Operator (product UI and public documentation only — the clean-room
rule against reading GPLv3 source, including Vital's, still stands).

| Problem | What they do |
|---|---|
| Density | **Fixed-size panel**, params grouped into labelled sub-panels laid out in 2–3 **columns**, not one column. Height is bounded and constant. |
| Which knob matters | **Two or three knob sizes.** Vital's oscillator: one large level knob, small satellites for tune/phase/pan. |
| Labels | Always **under** the knob, small caps, dim. Never to the right, never ragged. |
| Live values | **One fixed readout location** per panel that shows the hovered/dragged param's name + value. Never a floating tooltip that follows the cursor. |
| Sections | A tinted/inset **sub-panel with a header**, spanning the full panel width. Not a hairline rule that stops a third of the way across. |
| Scope | Spans the **full panel width**, edge to edge, always shows a zero line and a static hint of the current waveform even when silent. |
| Horizontal sliders | Label left, value right, both *inside* the track's row. Track fills the panel width. |
| Sequencer grid | Steps stretch to fill the panel width; the current step is highlighted as it plays. |

The one thing v2 got right and should keep: **knob caption = param name, value
on demand** (`src/main.cpp:881-889`). That matches hardware and every plugin.
The mistake was choosing a cursor tooltip as the "on demand" mechanism.

---

## 3. Proposed v3 grammar

Six changes. They are ordered so each is independently shippable.

### 3a. One enforced body width — the load-bearing fix

Introduce an explicit audio body scope:

```
BeginAudioBody(width)  →  pushes gAudioBodyWidth, ImGui::PushItemWidth
EndAudioBody()
```

- Every audio-node drawing helper (`NodeSeparator`, visualizers, slider rows,
  the step grid) takes its width from `gAudioBodyWidth`, never from
  `kPreviewSize` or a literal.
- `NodeSeparator` gains a `width` parameter defaulting to `kPreviewSize` so
  visual nodes are untouched (the spec's own "do not restyle visual nodes"
  rule).
- The knob row is **laid out to fit** the body width rather than allowed to set
  it — see 3b.
- Two sanctioned widths, nothing else: `kAudioNodeWidth` (440) for a full node,
  `kAudioNarrowWidth` (200) for a node with ≤2 params (Gain, Splitter, Audio
  Out). §7's "a one-param node is visually smaller" becomes true instead of
  aspirational.

Success test: every horizontal element in an audio node starts at the same x and
ends at the same x. Today none of them do.

### 3b. A real knob row helper

```
AudioKnobRow row(bodyWidth, count);   // computes pitch = bodyWidth / count
row.Knob("freq",   &n->frequency, ...,  kKnobLarge);
row.Knob("volume", &n->volume,    ...,  kKnobLarge);
row.Knob("detune", &n->detune,    ...,  kKnobSmall);
```

- Each cell is `bodyWidth / count` wide; the knob is **centered in its cell**
  and the caption centered under it. Row width is exactly `bodyWidth` by
  construction — §1c overflow becomes structurally impossible.
- Two sizes: `kKnobLarge` (56) for the 1–2 params that define the node's sound,
  `kKnobSmall` (40) for the rest. Vertically **bottom-aligned** so all captions
  share a baseline (fixes §1f, §1h).
- The mod pin moves **onto the knob** — a 5px nub at the knob's lower-left on
  its own circle, still `ed::BeginPin` with the identical pin id, so
  `Modulation` plumbing is untouched. Recovers 108px/row and fixes the floating
  dots (§1g).
- An enum param gets a `DropdownCell` of the same pitch: button on top, caption
  underneath in the same style as a knob caption, so it reads as one of the row
  rather than a foreign widget (fixes §1e/§1h for dropdowns).

### 3c. Sections become panels, not hairlines

Replace `NodeSeparator("tuning")` in audio bodies with a full-width inset
sub-panel: a rounded rect at ~4% lighter than the node body, a small-caps header
in the category tint, its content inset 8px. This is the single highest-leverage
*visual* change — it is what makes a stack of 17 rows read as four groups
instead of a wall.

### 3d. Kill the knob tooltip; add a fixed readout

- Wrap the two existing `SetTooltip` calls in `ed::Suspend()`/`ed::Resume()`
  (correctness — §1a).
- Then stop using a tooltip for knob values. Add a **readout strip** — one text
  line at the top-right of the body, above the visualizer — that shows
  `detune  0.72 c` while any control in that node is hovered or being dragged,
  and the node's stat line otherwise (voice count, `%d steps - Grid`, etc.).
  Fixed position, never occludes the control you are adjusting, and it gives
  Note Sequencer's redundant `TextDisabled` line (§1j) a real job.

### 3e. Horizontal sliders get plugin layout

A `AudioSlider` variant of `ModSlider` that draws **label left, value right,
both inside the track row**, at either full body width or exactly half. Keeps
every line of `ModSlider`'s pin/typing/expression/undo logic — this is a
`SliderFloat` → custom-draw swap in the same shape `KnobFloat` already proved
works (`src/main.cpp:900-906`). Fixes §1e for the ~20 Tier 2 params.

### 3f. Visualizers are never blank

- Waveform scope: always draw a centre zero line and a faint vertical grid.
  When `scopeCacheCount <= 1`, draw **one cycle of the currently selected
  waveform**, computed on the main thread from `DspMath` — dim, so it reads as
  "idle, this is the shape" rather than "broken". Zero RT-safety implication:
  it's a main-thread recomputation from a published param, exactly what §3 of
  the spec already permits for the Envelope.
- Level meter: always draw the track, plus tick marks at −12/−6/−3/0 dB and a
  peak-hold line. A dB meter with no scale is decoration.
- Both span the full body width via 3a.

---

## 4. Per-node redesign under v3

**Oscillator** (440 wide, ~4 sections). Scope full width with idle waveform +
readout strip. Row 1 (4 cells): `waveform` dropdown, `freq` large, `volume`
large, `detune` small. Row 2 (4 cells, small): `unison`, `width`, `octave`,
`semi`. Panel `tuning`: `fine`, `glide`. Panel `phase / stereo`: `phase`,
`phase random`, `stereo width`, `sync`. Panel `fm (2-op)`: `ratio`, `index`,
`feedback`. Panel `velocity`: two sliders, and it should say *why* it's inert
rather than `(needs a note cable)` in a section header. Net: same param count,
roughly 40% less height, four visual groups instead of one wall.

**Gain** (200 wide). Meter with dB ticks at 200px, one large knob centered,
value in the readout strip. No 440px anything.

**Mixer** (440). 8 knobs as two rows of 4 via `AudioKnobRow`, each cell
110px, per-slot meters under each knob rather than one summed meter at the top
— a mixer's whole job is comparing slots.

**Note Sequencer** (440). Step grid **stretched to body width** (16 steps ×
25px), current step highlighted from the transport, click to toggle. Row: same
4-cell layout. Pattern-specific params in a fixed-height panel so the node does
not resize when the pattern changes (§1j).

**Envelope** (440). Already the closest to right — it just needs 3a/3b/3f.

---

## 5. Recommended order

1. **3a** (enforced body width) + **1b** (`NodeSeparator` width). Nothing else
   is worth doing first; every other fix inherits from this.
2. **3d** — the `ed::Suspend` fix is a two-line correctness bug and should not
   wait for a design pass.
3. **3b** (knob row + pin-on-knob) and **3f** (non-blank visualizers). Biggest
   perceived-quality jump per line changed.
4. **3c** (section panels) and **3e** (slider layout).
5. Per-node passes from §4.

Steps 1–4 are all in `src/main.cpp`'s widget layer plus `DrawAudioNodeBody`'s
helpers — no audio-thread, `ParamMailbox`, `MeterRing`, `Modulation` or patch-
format surface is touched, so `AUDIOGRAPHTEST` / `ROUNDTRIPTEST` / `PATCHTEST` /
`DSPTEST` should be unaffected. Verify anyway.

Once §3 lands, `audio-node-ui-system.md` should be revised to v3: §1's layout
grammar and §2's widget table are the parts that change; §3 (visualizer
catalogue), §5 (density tiers), §6 (cable matrix) and §8 (Oscillator inventory)
all survive intact.

---

## 6. Open question for the user

§1's "nothing hidden, ever" is the one call worth re-making deliberately. v3 as
proposed keeps it — everything stays visible, height is reduced ~40% by grouping
and by two knob sizes. The alternative, which is what Vital/Serum actually do,
is a **fixed-height body with tabbed sections** (`TUNE | PHASE | FM | VEL`),
which would cap an audio node at roughly the height of a 3D node forever, at the
cost of one click to reach a Tier 2 param. That is a taste call about canvas
density, not a correctness one, and it is worth deciding before P3b rather than
after 30 nodes exist.
