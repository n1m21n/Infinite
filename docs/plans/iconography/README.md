# Iconography and chrome redesign

Status: planned (2026-10-08). Owner: "dive deep into our own iconography foundations... make our own icons from scratch that fit the branding."
Reference: Logic Pro screenshots (top bar, timeline header, sliders, browser table, accent picker). Borrow the *grammar*, never the glyphs.
Skills to load before building: `node-ui-pillars`, `codebase-navigation`, `windows-parity`, `linux-parity`, `node-ui-sweep`, `panels-sweep`.

## Where we are

| Area | Today | Logic's version |
|---|---|---|
| Icons | 39 Tabler ports hand-drawn as ImDrawList strokes (`src/core/TablerIcons.h`, 24 grid), 52 call sites in 9 files; plus one Lucide font glyph (`src/IconsLucide.h`) | One family, one stroke weight, outline when off, filled tile when on |
| Top bar | Menus, then loose 38px icon toggles placed right to left (`src/app/frame/StageMenuBar.cpp:883`) | Grouped pill clusters, a dark display box in the centre |
| Sliders | Mostly stock ImGui grabs; only `AudioSliderFloat` draws its own (`src/app/ui/ParamWidgets.cpp:284`) | Dark rounded track, round light dot as the handle |
| Tables | ImGui tables with grid lines | No grid lines, faint striped rows, tall rows, a full-width row highlight, expand arrows |
| Accent | Fixed per theme (`t.accent`, `src/app/ui/Theme.cpp`) | User picks one of nine swatch dots, plus "Multicolor" |

## 1. Foundations: what the professional systems agree on

| Source | Rule we take |
|---|---|
| Apple SF Symbols + Human Interface Guidelines | Icons behave like type: weights, scales, an outline/fill pair; balanced by eye, not by measurement |
| Material Symbols keyline shapes | Every icon fits one of four base shapes (circle, square, portrait, landscape), so mixed icons read the same size |
| IBM Carbon icon grid | Fixed padding around the live area; a separate drawing per size, never a scaled one |
| Lucide / Phosphor design guides | One stroke width, round caps and joins, minimal detail, no text inside icons |
| Microsoft Fluent | Redraw at 16 and 20; detail that works at 24 falls apart at 16 |

### Principles

1. **Metaphor first.** Each icon names one action or object. If it needs a tooltip to be understood, redraw it before shipping.
2. **Optical, not mathematical.** A circle sits slightly larger than a square; a play triangle sits right of centre.
3. **Consistency beats beauty.** One stroke, one corner radius, one end cap across the set.
4. **States by fill, colour by meaning.** Outline = off, filled tile = on. Colour only where it carries meaning (record red, active accent, warning).
5. **No borrowed glyphs.** Every icon is drawn by us on our grid. Tabler/Lucide references are retired as icons are replaced.

## 2. Infinite Glyphs spec

```
 canvas 20×20 ─┐   ┌─ 2px padding
               ▼   ▼
   ┌────────────────────┐
   │  ┌──────────────┐  │   stroke   1.5px, round caps + joins
   │  │  live 16×16  │  │   corners  r = 2 (outer), r = 1 (inner)
   │  │   ○ □ ▯ ▭    │  │   keylines circle 16 · square 14 · portrait 12×16 · landscape 16×12
   │  └──────────────┘  │   sizes    16 (tables, nodes) · 20 (toolbars) · 24 (top bar)
   └────────────────────┘   pixel    stroke centres on half-pixels at 1x so lines stay crisp
```

| Token | Value |
|---|---|
| Grid | 20 × 20, 2px padding, live area 16 × 16 |
| Stroke | 1.5px at 20; 1.25px at 16 (separate master); 1.75px at 24 |
| Caps / joins | Round / round |
| Corner radius | 2 outer, 1 inner |
| Fill state | Solid shape, inner details knocked out to background |
| Min gap between strokes | 2px |
| Colour | Single colour (`ImGuiCol_Text`); state colours from the theme only |

### Brand motif: the dot

In Infinite a dot already means a pin, a cable end, a modulation point. The same filled dot appears in three places so they read as one language:

```
 icons          slider handle          node pin
   ●─╮              ──●──                ●──── cable
     ╰●
```

- Icons that show signal, routing or modulation end their strokes in a 3px dot.
- Slider handles are a round dot (section 4).
- Node pins keep their dot; size and colour rules shared with the icon set.

### Icon tile (toolbar button)

```
  off            hover          on
 ┌──────┐      ┌──────┐      ┌──────┐
 │  ◯   │      │░ ◯ ░│      │██◉██│     tile 28×28, radius 6
 └──────┘      └──────┘      └──────┘     on = filled tile (accent or neutral), glyph switches to its fill variant
```

## 3. Pipeline

| Step | How |
|---|---|
| Author | SVG per icon per size in `art/icons/src/<name>-20.svg` (and `-16`), on the grid above |
| Lint | `tools/icons/lint.py`: canvas size, stroke width, no transforms, no text, padding respected |
| Build | `tools/icons/build.py` compiles the SVGs to one font `external/icons/Infinite/infinite-glyphs.ttf` plus a generated `src/IconsInfinite.h` (name → codepoint) |
| Load | Merged into the ImGui font atlas next to the existing Lucide merge; FreeType hinting on |
| Review | `tools/icons/sheet.py` renders a contact sheet: every icon at 16/20/24, off and on, light and dark |
| Retire | Each replaced `Tabler::Draw*` call site moves to the glyph; `TablerIcons.h` and `IconsLucide.h` go when empty |

Why a font: SF Symbols and Material ship as fonts; one atlas, crisp at every size, no per-frame path tessellation, same path on macOS/Windows/Linux.

## 4. Controls

### Dot slider

```
 ╭──────────────────●───────╮    track: 4px tall, fully rounded, recess colour
 ╰──────────────────────────╯    fill: left of handle, accent at 0.60
                                  handle: 12px circle, light, 1px soft shadow; 14px on hover/drag
```

- Generalise `AudioSliderFloat`'s custom draw into one shared `DotSlider`; route every plain `ImGui::SliderFloat` in chrome and panels through it.
- Node-body knobs stay governed by `node-ui-pillars`; this spec covers slider widgets only.

### Clean table

| Rule | Value |
|---|---|
| Borders | No inner vertical or horizontal lines; one hairline under the header |
| Rows | 24px tall, alternate stripe at 3% contrast |
| Selection | Full-width accent fill, text inverts |
| Hierarchy | Chevron disclosure, child rows indented 16 |
| Numbers | Right-aligned, tabular figures, muted units (`48 kHz`, `24-bit`) |

One shared table style helper applied to Browser, Mod Matrix, Perf panel and the Arrange track list.

### Accent picker

Settings → Theme → Color: a row of 9 dot swatches (the dot motif again), selected one ringed. "Multicolor" = category colours drive accents per panel. Accent feeds `t.accent` only; every derived colour already composites from it (`Theme.cpp:95`).

## 4b. Interaction feel (the small details)

Today every ImGui state change is instant: hover, press and toggle colours snap in one frame. No chrome widget animates. These details are what make Logic feel calm, so they are part of the spec, not polish for later.

### States every control has

```
 rest ──hover──▶ hover ──press──▶ pressed ──release──▶ on / off
   ◀──leave───         ◀──drag-out──            (focus ring on keyboard nav)
```

| State | Look | Timing |
|---|---|---|
| Hover in | Tile fades to 6% text-colour overlay | 120 ms ease-out |
| Hover out | Fades back | 180 ms ease-out (leaving is slower than arriving) |
| Press | Tile darkens to 12%, glyph scales to 0.94 | Instant on mouse-down (no delay on the action) |
| Release → on | Accent tile grows from the centre (radius 0 → full), glyph swaps to fill variant | 160 ms ease-out, slight overshoot (1.04) |
| On → off | Tile fades out, glyph back to outline | 140 ms ease-in |
| Disabled | 35% opacity, no hover | — |
| Keyboard focus | 2px accent ring, 2px outside the tile | 100 ms fade |
| Tooltip | Appears after 500 ms hover, fades in 100 ms; instant for neighbours once one is showing | — |

### Same rules on other controls

| Control | Detail |
|---|---|
| Dot slider | Handle grows 12 → 14px on hover, 15px while dragging; fill follows the value with no lag (values never animate, only chrome) |
| Table row | Hover row tint fades in 80 ms; selection fill is instant (it is data, not decoration) |
| Group pill | Pressing one tile never moves its neighbours; tiles keep fixed widths |
| Display box | Digits change without animation (it is a clock); edit mode highlights the dragged field |
| Accent swatch | Selected ring scales in 160 ms; the whole UI accent cross-fades 200 ms |

### Selected must not look like hover (owner report, 2026-10-08)

A button that is already on/selected (accent fill) changes colour when hovered today, which reads as "something just changed" when nothing did. App-wide: every inline `PushStyleColor(ImGuiCol_Button, AccentEmphasisSelected())` site, `AudioToggleButton`, the segment strips and `LaneToggle` (Spatial Mixer).

| State | Rule |
|---|---|
| Selected, hover | Keep the accent hue; brighten at most ~4% (a hint of life, no colour change) |
| Unselected, hover | Neutral fill lifts as specified in the table above |
| Selected, press | Darken as in the table; release returns to the selected look |

Implement once in the shared toggle helpers by also pushing `ImGuiCol_ButtonHovered` and `ImGuiCol_ButtonActive` for the selected state; then sweep `grep -rn "AccentEmphasisSelected" src`. Push and Pop must read one local `on` captured before the click: a click flips the state between them, which leaked styles in the Spatial Mixer and Audio Out `live` buttons (fixed in 690b250d).

### Rules

1. **Feedback is instant, decoration eases.** The action fires on press/release with no wait; only the visual settles.
2. **Never animate values.** Transport position, meters, parameter values draw the true value every frame.
3. **Under 200 ms.** Nothing in the chrome animates longer.
4. **Reduce motion.** One setting (and the OS "reduce motion" flag on macOS) turns every ease into an instant change.
5. **Zero cost at rest.** An animation only redraws while it is moving; idle UI stays idle (perf rule: canvas cost must not rise).

### Implementation

- One small helper `UiAnim::Value(ImGuiID id, float target, float durationMs)`: per-ID float, eased by `io.DeltaTime`, stored in a flat map, entries dropped when not touched for a frame.
- `IconTile(...)` and `DotSlider(...)` read hover/press/on through it; no call site hand-rolls timing.
- The app redraws every frame today (no idle throttle found), so eases cost only the map lookup. If an idle throttle lands later (perf initiative), `UiAnim` must report "still moving" to keep frames coming.

## 5. Top bar

```
[▣ ⚙ ? ⤓] [◎ ≡ ✎]   [■ ▶ ● ◉ ⟲]  ┃ 001 1 │ 120 │ 4/4 ┃  [1234 ♩]  [▬cpu▬]  [☰ ▤ ◯ ⧉]
 panels    tools      transport   ┃ bar beat│tempo│sig ┃   modes     meter    side panels
```

| Group | Contents |
|---|---|
| Panels (left) | Browser, node search, help, import |
| Tools | Tempo/clock, mixer, edit |
| Transport | Stop, play, record, capture, loop |
| Display box | Dark rounded box: bar.beat in large tabular digits, tempo, signature/key; small labels under; click-drag to edit |
| Modes | Count-in, metronome as icon tiles |
| Meter | CPU / audio load bar |
| Side panels (right) | Arrange, perf, mod matrix, viewport |

Each group is a pill (radius 8, faint fill). Groups collapse from the outside in on narrow windows, keeping today's "drop least essential first" rule.

## 6. Timeline header

```
[Edit ▾ Functions ▾ View ▾] [▦ ☰] [⟋ ⋈] [tool ▾ +▾]      [snap] [↕ ──●──] [↔ ──●──]
 ruler:  ▶1━━━━ 9    17    25    33 …     bar numbers, shaded loop region
 tracks: [+] [⧉]                           [M] [S] small pill buttons per header
```

- Ruler: bar numbers only at zoom-appropriate intervals, loop region as a shaded band.
- Zoom sliders are dot sliders with ↕ / ↔ glyphs.
- Track header buttons: rounded pill, one letter, filled when on (M = neutral, S = yellow).
- Read `timeline-arrangement-architecture` before touching `src/arrange/`.

## 7. Plan

| Block | What | Branch | Done when |
|---|---|---|---|
| 1 | Spec sign-off; 12 pilot glyphs for the top bar; pipeline (lint, build, sheet) | `feature/iconography` | Contact sheet approved in light and dark |
| 2 | `UiAnim` helper, icon tile states, top bar groups, display box, dot slider, accent picker | `feature/topbar-redesign` | `node-ui-sweep` + `panels-sweep` pass; Windows/Linux CI green |
| 3 | Timeline header, clean table style, remaining glyphs; retire `TablerIcons.h` | `feature/timeline-header` | No `Tabler::` call sites left |

### Pilot set (block 1)

play · stop · record · capture · loop · metronome · count-in · browser · mixer · mod matrix · viewport · arrange

## Decisions

| # | Question | Decision | Why |
|---|---|---|---|
| 1 | Grid and stroke | 20 grid, 1.5px stroke, round caps | Matches Logic/SF toolbar density; 1.5 stays crisp at 2x and readable at 1x |
| 2 | Delivery format | Icon font built from SVGs | One atlas, crisp, cross-platform, same as SF Symbols / Material |
| 3 | Brand motif | The dot (pin = handle = icon terminal) | Already Infinite's meaning for signal; gives an identity that isn't Logic's |
| 4 | Accent picker | 9 swatches + Multicolor | Accent already flows from one `t.accent` |

## Open

- Multicolor: per-panel category colour, or per-node-category accent?
- Display box: does it also show timecode (h:m:s:f) on click, as Logic does?
