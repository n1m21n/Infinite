---
name: node-ui-pillars
description: The non-negotiable symmetry, alignment and theming pillars for every Infinite node body — knob-row grid discipline, where a dropdown/checkbox/mod-dot is allowed to sit, the standard bottom-right `mix` slot, filter-mode naming, and the light/dark contrast budget for checkboxes and dropdowns. Use BEFORE editing any Draw*Body / Draw*Params function, before adding a knob, dropdown, checkbox or toggle to a node, before changing PushCheckboxStyle / PushDropdownStyle / DrawDiscreteParamPin / AudioKnobRow, and as the acceptance checklist after any node UI change. Complements audio-node-ui (which covers layout grammar and widget choice); this one is the regression contract that must survive every future edit.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

This skill exists because a full-app UI pass in 2026 found the same handful of
defects repeated across a dozen node bodies: knob rows that didn't line up,
trailing checkboxes hanging off the grid, modulation dots floating above the
control they belong to, four different names for the same filter mode, and a
checkbox/dropdown restyle that read fine in light mode and shouted in dark.
Every rule below is one of those defects, written as a rule so it cannot
come back.

Read `.claude/skills/audio-node-ui/SKILL.md` first for the layout *grammar*.
This skill is the *contract* — the things that must still be true after you
touch anything.

---

## P1 — Every control sits on the row grid. There are no free-floating widgets.

`AudioKnobRow` divides `gAudioContentW` into N equal cells and centres one
control per cell. Any control drawn between `BeginAudioBody` and
`EndAudioBody` that is **not** inside an `AudioKnobRow`, an `AudioSlider`, a
`BeginAudioSection` panel, or a deliberate full-width button strip is a bug —
it will not share a column edge with anything above it.

The specific pattern to never write again:

```cpp
row.End();
DrawSyncToggle(n, "sync##fooSync");   // <-- free-floating
ImGui::SameLine();
ModCheckbox("analog##fooAnalog", &a);  // <-- free-floating
```

Write instead:

```cpp
{
   AudioKnobRow row(3);                 // same N as the knob row above it
   row.Checkbox("sync to tempo##fooSync", &sync);
   row.Checkbox("3 taps##fooTaps", &taps3);
   row.Checkbox("analog##fooAnalog", &analogBool);
   row.End();
}
```

`AudioKnobRow::Checkbox` is the only checkbox call that is grid-aligned and
that vertically centres its modulation pin against the checkbox frame
(`checkY + (ImGui::GetFrameHeight() - 12.0f) * 0.5f`). A bare `ModCheckbox`
in a body does neither.

**Corollary — cell count must match.** If the knob row above has 3 cells, the
toggle row below has 3 cells (use `row.Skip()` to leave one empty). A 4-cell
knob row over a 3-cell toggle row is the misalignment users see immediately.

## P2 — The modulation dot is vertically centred on its control, always.

`DrawDiscreteParamPin` draws a 12 px box at the *current cursor Y* and then
`SameLine`. A checkbox frame and a dropdown button are both taller than 12 px,
so a pin drawn at the raw cursor Y sits high and reads as belonging to the row
above. Every call site must offset by `(controlHeight - 12.0f) * 0.5f` before
drawing the pin. `AudioKnobRow::Checkbox` and `::Dropdown` already do; if you
add a new pinned-control helper, do it there too — do not fix it per node.

For a knob, the dot sits at the knob's vertical centre, on the cell's left
edge. For a caption-bearing cell, the dot never moves to accommodate the
caption.

## P3 — Row shape: selector left, knobs right.

Within a row, a dropdown (mode / scale / shape / wave / rate-division) goes in
the **leftmost** cell, knobs fill the cells to its right. A dropdown wedged
between two knobs breaks the row's rhythm — the button's frame height differs
from a knob's diameter, so the captions no longer share a baseline visually
even though they technically bottom-align.

When a node has two selectors and a sync mode, stack the selectors in the left
column of two consecutive rows (row 1 left = shape, row 2 left = sync/rate) so
the left edge reads as one selector column and the right side reads as a knob
block.

## P4 — `mix` is always the last knob of the last knob row.

Every `AudioEffects` node ends on `mix` in the bottom-right knob cell. This is
the one positional convention a user can rely on across all 16 effects. If a
layout change would move `mix`, change the layout, not `mix`.

## P5 — A row that changes shape must not change the grid.

Tempo-sync toggles swap a `rate` knob for a `rate` dropdown. The cell index
must not move when it swaps — the same cell holds either widget. Never
conditionally add or remove a cell.

## P6 — Prefer a filled grid over a ragged one.

A 3-knob row under a 4-knob row with the fourth cell empty reads as unfinished.
Either use `row.Skip()` deliberately in a position that reads as intentional
(centre or far edge, never a random middle cell), or add the missing control —
but only if that control is a real, wanted parameter for that DSP. **Never add
a knob purely to fill a hole.** If nothing real is missing, use a narrower row
with fewer, larger cells.

## P7 — One naming convention for filter modes, app-wide.

`SynthModes::FilterNames()` (`src/audio/SynthModes.h`) is the canonical list
and the canonical spelling: lowercase, shape abbreviation + slope —
`off`, `lp 12`, `lp 24`, `lp 36`, `hp 12`, `hp 24`, `hp 36`, `bp 12`, `bp 24`,
`notch 12`, `notch 24`. The Wavetable and Oscillator synths use it.

Divergent lists that exist for historical reasons and must be migrated, never
extended:

| Node | List | File |
|---|---|---|
| Wave Terrain | `Bypass, Lowpass 12dB, …` | `src/nodes/WaveTerrainNode.cpp:37` |
| Equation | `Bypass, Lowpass 12dB, …` | `src/nodes/EquationNode.cpp:41` |
| Image Spectral Synth | `Off, LP 12dB, …` | `src/nodes/ImageSpectralSynthNode.cpp:47` |
| Metallic | `FilterModeNames()` | `src/audio/dsp/MetallicResonator.h:86` |

A node whose DSP genuinely offers fewer modes exposes a **subset** of the
canonical names in canonical order — it does not invent its own spellings.
Saved patches store an integer index, so re-ordering or inserting an entry in
any of these lists silently rewrites every saved patch's filter mode. Migrate
by remapping on load, or by keeping index order and changing only the strings.

## P8 — A node that quantizes to a scale carries the global-scale toggle.

Any note node with a `scale`/`root` pair, or that snaps pitches on output,
registers a `useGlobalScale` bool and appears in `GetNodeGlobalScaleFlag`
(`src/main.cpp:3613`). The title-bar music-note glyph is drawn generically from
that function (`src/main.cpp:47317`) — a node missing from the list silently
ignores the transport key/scale, which is a functional bug, not a cosmetic one.
When `useGlobalScale` is on, the node's own scale/root dropdowns render inside
`ImGui::BeginDisabled()`.

## P9 — A pitch visualizer must be truthful about the scale it draws.

A keyboard/pitch strip highlights a key iff the key's pitch would actually pass
the node's logic. Two traps that have both shipped:

- **Layout vs. content.** A piano's white/black key *layout* is a property of C,
  not of the node's root. Offsetting the strip's low note by `root`
  (`lowNote = 48 + n->root`) draws the wrong instrument — E♭ rendered in a white
  key's slot. Keep the strip anchored to a C, and express the root by which
  pitch classes light up.
- **Window vs. range.** A fixed 2-octave window with a `rangeLow..rangeHigh`
  that can be anywhere in 0..127 shows a range that isn't there. Either scroll
  the window to contain the range, or widen it, or draw an explicit
  out-of-window indicator.

Anything the visualizer reads from the audio thread comes through the existing
two-atomic publish (`LastNote()`); never add a new cross-thread read.

## P10 — The dark-mode contrast budget.

Light mode is not the check. Dark mode is, because the node body background is
already dark and a light frame on a checkbox or dropdown becomes the loudest
thing on the node — louder than the value it is next to. On a dense params
panel (Render 3D is the worst case: ~25 stacked rows), that inverts the
hierarchy: the user sees a column of bright chips and cannot tell where one
parameter ends and the next begins.

The budget, dark theme:

- A checkbox frame or dropdown button fill sits **within ~0.06 luminance** of
  the node body background of the shipped default ("Infinite") theme. It is a
  recess, not a chip. The node body itself is `panelBg` blended with a
  per-category tint (`DrawNodes`' `ed::StyleColor_NodeBg` push, `src/main.cpp`
  ~47347) — its luminance varies by category (roughly 0.15–0.22 across the 10
  categories at the default tint weight), so treat the budget as "close to the
  quietest, most common case," not an exact match against every category.
  Some other theme presets (Nord in particular) have a brighter dark `panelBg`
  than "Infinite" — the fixed dark fill below will then sit *more* recessed
  than the 0.06 budget on those presets, which is the safe direction to miss
  in (a control can be darker than intended without ever becoming a chip; the
  defect this rule exists to prevent is the opposite).
- Border is switched off in dark (`FrameBorderSize 0`) rather than merely
  dimmed — at 1px even a hairline color reads as the thing that makes the
  control findable, which is itself the failure mode. Light mode keeps
  `FrameBorderSize 1`, because there the panel is bright enough that a recess
  needs a real edge to read as a control. If a border color is still set for
  the (now-invisible) dark border, keep its color no brighter than the
  row-separator tone (`t.border`) so a future re-enable doesn't reintroduce a
  chip.
- The **check mark / active state** is the only element allowed full accent
  brightness — brighter than the frame ever gets. An unchecked checkbox should
  be nearly invisible until scanned for; a checked one should be obvious.
- The dropdown's caption text carries the identity, at near-full contrast; the
  frame does not need to and should not compete with it.
- Hover/active states are free — they cost nothing at rest, so they can jump
  noticeably above the quiet resting fill to signal "interactive" without
  fighting rule 1.
- Vertical rhythm does the separating, not per-widget chrome: consistent row
  height and one gutter between the label column and the value column beat any
  amount of framing. Render 3D's `NodeSeparator` group headers (`output`,
  `camera`, `raster`, `light`, `shadows`, `environment`, `points`) are the
  highest-return separation tool already in the system — reach for zebra
  banding or a hairline group separator only if grouping alone still isn't
  enough, and keep it low-alpha (~2–3%) if you do.

Both themes are defined in exactly two places —
`PushCheckboxStyle()` / `PushDropdownStyle()` (`src/main.cpp` ~2040 and ~1960).
Never hand-roll a one-off checkbox or dropdown colour in a node body.

## P11 — Node-local strings never leak layout.

`snprintf` into the readout strip, not into a caption. A knob caption is the
param name. The strip is never empty. (Inherited from `audio-node-ui`; repeated
here because layout changes are where it gets broken.)

---

## Acceptance checklist — run after any node UI change

1. Screenshot the node in **both** the light and the default dark theme, and in
   at least one high-contrast theme. Compare the checkbox/dropdown chrome to
   the body background — P10.
2. Screenshot with a modulation cable attached to a checkbox and to a dropdown.
   The dot must be centred on the control, and the driven state must read — P2.
3. Toggle every sync/mode switch the node has and confirm no cell moves — P5.
4. Confirm `mix` is bottom-right — P4.
5. `./scripts/…` build, then run the hygiene harness
   (`.claude/skills/run-infinite-hygiene/SKILL.md`) — a body-layout change can
   still break save/load if a param was added, and `AUDIOPARAMSWEEPTEST`
   catches that.
6. If you added a param: `.claude/skills/node-param-audit/SKILL.md` to confirm
   it is modulatable and appears in the performance matrix.
7. If you touched a shared widget (`ModCheckbox`, `AudioKnobRow`,
   `DrawDiscreteParamPin`, `Push*Style`), you changed **every** node. Spot-check
   at least one node from each of: AudioEffects, Synths, Notes, 3D (Render 3D),
   Source, Modulators.

## Where the rules live in code

| Rule | Enforcement point |
|---|---|
| P1, P3, P5, P6 | `AudioKnobRow` (`src/main.cpp` ~7511) and each `Draw*Body` |
| P2 | `DrawDiscreteParamPin` (~1945) + `AudioKnobRow::Checkbox` (~7797) |
| P4 | convention only — check by eye |
| P7 | `SynthModes::FilterNames()` (`src/audio/SynthModes.h:137`) |
| P8 | `GetNodeGlobalScaleFlag` (~3613), draw site (~47317) |
| P9 | each `Draw*Visualizer` |
| P10 | `PushCheckboxStyle` / `PushDropdownStyle` |
