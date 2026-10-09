# Infinite design system: iconography, controls and chrome

Status: planned (2026-10-08). Owner: "dive deep into our own iconography foundations... make our own icons from scratch that fit the branding."
Reference: Logic Pro screenshots (top bar, timeline header, sliders, browser table, accent picker). Borrow the *grammar*, never the glyphs.
Skills to load before building: `infinite-design-system`, `node-ui-pillars`, `codebase-navigation`, `windows-parity`, `linux-parity`, `node-ui-sweep`, `panels-sweep`.

## STATUS (single tracker, updated 2026-10-08)

How we work: one surface at a time. Per surface: real-app before crop (`tools/design/context_shot.py`) -> change -> after crop -> owner approves -> commit. Flat mockups are not review material. Sections 0-8 below are reference spec; this table is the only place that says what is done.

**Approved 2026-10-08:** the UI system analysis (`docs/plans/ui-system/README.md`) is signed off. This table now tracks its three blocks; the old steps 1-5 are folded into them (step 1 metrics layer = Block 1 type + layout engine, steps 2-3 = Block 2 top bar / Arrange header, step 4 = Block 3, step 5 = Block 2 + 3).

| Block | Branch | Step | State |
|---|---|---|---|
| 0 | `feature/design-foundations` | Tokens, ratchet, glyph pipeline, 56 glyphs, `UiAnim`, panels on own glyphs, main merged in | done (merge into `main` pending: `main` is checked out in a Codex worktree) |
| 1 Engine | `feature/ui-engine` | 1a ImGui 1.90.9 -> 1.92.9 (dynamic fonts, 6 local patches re-applied, `imgui-patches.md`) | done, 47/47 smoke + ui group green; Windows/Linux CI not yet run |
| 1 Engine | `feature/ui-engine` | 1b type engine: `UiType` (token sizes x Regular/Medium/Semibold), `UITYPETEST` | done; icon/text optical match comes with 1e (IconTile) |
| 1 Engine | `feature/ui-engine` | 1c layout engine `UiLayout` (done: Row/Column/Split, Fixed/Flex, Snap, `UILAYOUTTEST`); 1d interaction `UiInteract` (done: stable-key IDs, state, per-frame semantic tree, `UIINTERACTTEST`); 1e first components (done: TextButton, PillGroup, Readout with tabular digits, Divider, `UICOMPTEST`; IconTile moves onto UiInteract in 1h); 1f gallery + goldens (done: `UiGallery`, `tools/design/golden.py`, macOS goldens light + dark in `tests/ui-golden/macos`; Windows/Linux goldens need their own first run); 1g ratchet on raw widgets (done: `ratchet.py` now also counts `ImGui::Button/SmallButton/Separator/SameLine/PushStyleColor/PushStyleVar` per file; baseline 959 in 32 files, may only fall); 1h top bar (done 2026-10-08, owner-approved from real-app shots: 40 pt bar, 28 pt tiles, one centre line, 24 pt section breaks, Library button + glyph, tabular readouts; ratchet 954; B6 re-run within noise); remaining for Block 1: 3-platform CI green, merge. 1d interaction + stable IDs, 1e first components, 1f gallery + goldens, 1g ratchet, 1h top bar proof surface | not started |
| 2 Chrome | `feature/ui-chrome` | Top bar, display box, rail, library panel | done (owner-approved 2026-10-08). Arrange header, menus, tables, Settings, mod matrix, perf mode still raw: see coverage table |
| 3 Canvas + nodes | `feature/ui-canvas` | Node frame/pins/cables, controls C1-C12, visualizers, zoom LOD, node icons | in progress, see coverage table below |

### Block 3 coverage (2026-10-08, from the app, light + dark checked on Chorus and Wavetable only)

| Class | State |
|---|---|
| C1 knob | kept as is (owner decision: existing knobs stay; textured/3D later); mod pin centred (P2) |
| C2 slider | outline removed both themes, radius 4. Hover/pressed easing open |
| C4 checkbox | done (2026-10-09): outline removed, P10 budget met, own tick with 120 ms draw-on |
| C6 dropdown | borderless recess, radius 4. Chevron tried and rejected by owner |
| C7 button | done (2026-10-09): `ActionButton` (plain / primary + semantic Selected, Record, Learn, Go, Solo kinds); all `ImGui::Button`/`SmallButton` in `src/app` converted; icon-only `##` buttons go to IconTile in C8. Decision: semantic fills are tokens `action.*`; Perf panel buttons use the accent, no per-node tint |
| C3 vertical fader | done (2026-10-09): `VFader.h`, pill handle, one tone path for both themes |
| C5 switch | done (2026-10-09): `Switch.h`, `FormParts::Switch`; Settings only (decision: node bodies keep CheckBox) |
| C8 toggle icons | done (2026-10-09): `GlyphToggle.h` (eye, viewport, scale snap, bypass) |
| C9 text entry | done (2026-10-09): `FieldWell::InputText/WithHint/Multiline/InputInt`; typed-value edit of controls keeps `PushTypedEditStyle` |
| C10 swatch, C12 badges | done (2026-10-09): `ColourChip.h` rounded colour field; favourite star on token `badge.favorite`; palette pin stays the square `PinDot::Swatch` (decision: square = palette-cable target, dot = cable) |
| C11 pin/dot | **done 2026-10-09**: `PinDot` component (Cable r7 / small r5, Param ring r4.5 -> r4 + centre dot when driven, Swatch square r4), one 12 pt box, sizes in `tokens.json` `pin`, colours in `pin.*` role pairs (idle grey, modulation amber, expression violet, prediction green), 0.72-1.0 slow pulse while driven (off under reduce-motion). Slider, knob, discrete, patch, lane, colour pins all draw through it. Decision: the idle ring stays grey, never accent |
| Old popups | done 2026-10-09: `##colorpick`, `##nodehelp` on `MenuParts::BeginPopup`; `##arrgridpopup` and Render Timeline already on MenuParts / SectionCard; `##commentedit` stays a transparent in-place editor on `FieldWell::InputTextMultiline` (decision: it is the note itself, not a popup card). Sweep: mod matrix rows on `MenuParts::Choice`, MIDI learn on `MenuParts::Item`. Raw widgets 470 |
| G1 segmented control | done: `PillGroup` already in Arrange unit toggle and the Library mode switch; no paired toggle buttons left |
| G11 empty states | partly done 2026-10-09: `EmptyState.h` on mod matrix, Samples, Plugins. Open: Perf panel, Viewport, Arrange (has its own prompt) |
| A8 tooltips | checked 2026-10-09: all 38 tooltip sites go through ImGui popup styling (PopupBg, `PopupRounding` 12, no border), so they match menus in both themes; help tips gated by Settings via `HelpTip`. Toasts: none exist yet (G22, not started) |
| G12 node icons | decided and built 2026-10-09: **family icons** (11 categories, not ~73 per-type; per-type waits until a browser/collapsed-node need exists). `art/icons/src/family-*-20.svg`, shown before the category in the node header via `NodeHeader::FamilyGlyph`. Prediction glyph is the weakest; redraw if it reads poorly at 14 px |
| G4/G5 status dot | done 2026-10-09 (not seen running: headless has no audio device): `StatusDot.h`, green/red on the Start Audio chip using the action role tokens (G14: go = ok, record = error, learn = warn). Warn (xruns) not wired to the chip yet; xruns stay in the readout tooltip |
| V2–V7 visualizers | done 2026-10-09 (to the extent the code allowed): meters on `radius_field` with r=2 fill, step/gate cells r=2 (Stutter, Arp), sparkline 1 px with end dot, keyboard keys and XY dot cursor already matched. Open: meter peak-hold dot, XY soft trail (not built: no grounded need) |
| G7 search scope | decided 2026-10-09: **the browser's mode tab is the scope**; each mode's field names it in its hint ("search plugins...", "search modules..."). A scope chevron would duplicate the tab row, so none is added |
| G8 filter chips | decided 2026-10-09: **`FilterBar` (sort + type dropdowns, direction arrow) is the filter row**; chips would take a second row in a narrow dock. Favourites are not in the browser, so no heart chip |
| G11 Perf + Viewport empty states | done 2026-10-09: Perf shows `EmptyState` on a page with no controls (hint differs in Edit/Perform). Viewport panel closes itself when its last node is removed, so it has no empty state to show. Not seen on screen (headless cannot open the panel) |
| G9 inline cell glyphs | decided 2026-10-09: **allowed, in use**: Samples rows already carry a play/pause glyph (`glyph::DrawPlayerPlay/Pause`). No lock/anchor/thumbnail data exists in any table, so none is added |
| G10 panel footer | decided 2026-10-09: **not built**. The Samples preview has no loop or volume control to host, and an item-count footer alone would be decoration. Revisit if preview loop/volume is added |
| G3 display modes | decided 2026-10-09: **not applicable now**. The top bar has no position readout (bar/beat display was removed), so there is no display box to switch. Revisit if a readout returns |
| G6 disabled shown | decided 2026-10-09: **no change**. No hidden-when-unavailable control found by search; params already use `BeginDisabled` (node-ui-pillars: greyed, not hidden) |
| G13 rich tooltips | decided 2026-10-09: **left as is**. Tooltips share the popup style (A8); name + value + shortcut is applied where a shortcut exists, no blanket rewrite |
| G14 colour roles | decided 2026-10-09: roles table written in `infinite-design-system` SKILL.md. Collisions flagged, not changed: learn vs modulation (both amber), solo vs favourite (both yellow) |
| Tab hover easing (A4-A7) | decided 2026-10-09: **left as is**. Only Settings uses a stock `BeginTabBar`, which ImGui cannot ease; docked panel tabs are ImGui's own |
| Light-theme review | `INFINITE_FORCELIGHT=1` with `INFINITE_AUDIOUITEST=1` (debug builds only) renders the canvas in the first light preset. Canvas, node frames, family glyphs and text read correctly in it |
| G15 contrast, G16 hit targets | partly done 2026-10-09. Measured text/panel and dim/panel for all 9 presets: default "Infinite" 15.1 / 6.0 passes; ported presets had dim text as low as 2.1:1. Decision: `Theme.cpp` pulls `TextDisabled` toward text colour until 4.5:1 (no-op for themes that pass). Search clear button hit area 20 -> 24; pins already expand via `ExpandPinHit`. Open: controls 3:1 audit, colour-blind check, other small hit areas |
| N1 node header | done 2026-10-09: one row (title Title/Medium, instance number, category dimmed) via `NodeHeader::Category` |
| N1 frame | 1 px hairline border done |
| N2 separators | done 2026-10-09: `NodeSeparator` hairline and label use themed Border / TextDisabled (were fixed dark-only palette colours) |
| N3 cables, N4 groups, N5 marquee | partly done 2026-10-09: link hover/selected border, group radius (`radius_group` 10) and marquee fill/border (accent 8% / 60%) set in `Theme.cpp` editor style. Open: cable draw order over nodes, ends on dot centre, dot grid (editor draws lines only; decision: keep the faint line grid), minimap |
| V1 scope / waveform | done 2026-10-09: every `ScopeBgCol()` well uses `tok::radius_field`; traces 1.5 px. V2-V7: share the same scope tokens already; per-class rounding (segments, cells, keys) not audited |
| Font sizes inside nodes | done 2026-10-09: no `SetWindowFontScale` left in node bodies except the NoteStack row (set to `type_body`); CV to Pitch readout on the Display size; global-scale tooltip at default size |
| Clip Settings, Render popup + progress/overwrite/fail dialogs, Viewport panel cards, node picker nav ring | done 2026-10-08 (`SectionCard` component; dark checked, light on Viewport only; dialogs not yet shot) |
| A3 File / Edit / main menus | **done 2026-10-09** (ebae9c06, c892a7f6, d821b8e9): 28 pt rows, rounded wash, inset hairlines (MenuParts); toggles use the app CheckBox; sliders use FieldWell::Slider; dropdown lists use MenuParts::Choice. Open: Modulation matrix / Performance / Arrangement submenus not re-screenshotted |
| A7 Settings window + every tab | **done 2026-10-09** (ebae9c06, 977377b3, 8790f748, c892a7f6): FormParts rows, section cards, hairline edges, round colour dots, reference style for Field Language / Expression Globals / Shortcuts / Help, 16 pt window padding, shared slider (type-to-edit, eased hover), slim scrollbars. Open: preset swatch strip still squares; tab hover not eased; Clip Settings and Render dialogs not re-verified |
| Node search popup | search field now uses `LibraryParts::SearchField` (magnifier was missing: Lucide font glyph not loaded). Done 2026-10-08 |
| Top bar metronome | pendulum swings per beat again (eased 140 ms) via `IconTile` swing. Done 2026-10-08 |

Root cause of the sizing complaints (found 2026-10-08): each call site derives its own icon size from row height (0.65 flag/track height/edit, 0.72 top-bar transport, 0.88 top-bar toggles, 0.9 search). No shared size token exists. Step 1 fixes that before any more icon art is judged.

Open decisions (one per message): switches only in Settings/panels (C5); node-family icons first (G12); Multicolor meaning; display box timecode mode; knob direction.


## Where we are

| Area | Today | Logic's version |
|---|---|---|
| Icons | 39 Tabler ports hand-drawn as ImDrawList strokes (`src/core/TablerIcons.h`, 24 grid), 52 call sites in 9 files; plus one Lucide font glyph (`src/IconsLucide.h`) | One family, one stroke weight, outline when off, filled tile when on |
| Top bar | Menus, then loose 38px icon toggles placed right to left (`src/app/frame/StageMenuBar.cpp:883`) | Grouped pill clusters, a dark display box in the centre |
| Sliders | Mostly stock ImGui grabs; only `AudioSliderFloat` draws its own (`src/app/ui/ParamWidgets.cpp:284`) | Dark rounded track, round light dot as the handle |
| Tables | ImGui tables with grid lines | No grid lines, faint striped rows, tall rows, a full-width row highlight, expand arrows |
| Accent | Fixed per theme (`t.accent`, `src/app/ui/Theme.cpp`) | User picks one of nine swatch dots, plus "Multicolor" |

## 0. Scope: every UI class in Infinite

This plan is the app's **design system**: it covers every element on screen, node bodies included, not just the top bar.

Ownership split, so nothing conflicts:

| Owner | Decides |
|---|---|
| `node-ui-pillars` + `audio-node-ui` | **Where** a control sits in a node (row grid, mod-dot alignment, `mix` last, selector left). Unchanged by this plan |
| This plan | **How** every element looks, moves and responds (glyphs, shape, colour tokens, states, motion) |

Inventory below is from the code (helper names, call counts from `src/app`).

### A. Foundations (6), applied to everything below

| # | Foundation | Section |
|---|---|---|
| F1 | Iconography (Infinite Glyphs) | 1–3 |
| F2 | Colour tokens + accent | 4 (accent picker), `Theme.cpp` |
| F3 | Typography (sizes, tabular digits, label case) | 4c |
| F4 | Spacing + shape (grid unit 4, radii 2/4/6/8) | 4c |
| F5 | States (rest, hover, pressed, on, disabled, focus, modulated) | 4b |
| F6 | Motion (`UiAnim`, reduce motion) | 4b |

### B. Controls inside nodes (12)

| # | Class | Today (helper, uses) | Redesign |
|---|---|---|---|
| C1 | Knob | `KnobFloat`/`ModKnob`/`KnobInt`/`BipolarKnobFloat`, ~280 | Recessed ring, value arc in accent, **dot** at the arc tip; bipolar arc grows from 12 o'clock; modulation shown as a thin outer arc |
| C2 | Horizontal slider | `ModSlider`, `AudioSliderFloat`, `SliderInt` | Dot slider (4) |
| C3 | Vertical fader | `VFaderFloat` | Dot slider rotated; handle is a pill (Logic mixer style) |
| C4 | Checkbox | `ModCheckbox`, `AudioKnobRow::Checkbox`, ~30 | Rounded square r=4, check glyph draws on in 120 ms; contrast budget per `node-ui-pillars` |
| C5 | Switch | (none yet) | iOS-style pill switch, only in Settings and panels, never in a node grid cell |
| C6 | Dropdown | `DropdownButton`, `PushDropdownStyle` | Pill button, value left, chevron glyph right; menu opens with 100 ms fade |
| C7 | Button | primary / plain / icon tile | Three kinds only: primary (accent fill), plain (faint fill), icon tile (2) |
| C8 | Toggle icon (bypass, eye, viewport, global scale) | `BypassToggle`, `EyeToggle`, `ViewportToggle`, `GlobalScaleToggle` | Icon tiles with outline/fill glyph pairs |
| C9 | Text / number entry | `InputText`, typed edit on knobs | Recessed field, accent focus ring, tabular digits |
| C10 | Colour swatch | `ColorSwatch` | Dot swatch (same as accent picker) |
| C11 | Modulation dot / pin | `DrawDiscreteParamPin`, `DrawPin` | The brand dot: one size table, one colour rule, pulses gently while a source drives it |
| C12 | Badge / decor | `DrawFavoriteBadge`, `DrawPredictorDecor` | Glyph badges from the icon font |

### C. Visualizers inside nodes (7)

| # | Class | Examples in code | Redesign |
|---|---|---|---|
| V1 | Scope / waveform | ~20 `Draw*Scope` / `Draw*Waveform` | Shared tokens already exist (`ScopeBgCol`, `ScopeGridCol`, ...): set them once, rounded r=4 well, 1.5px trace |
| V2 | Meter | `DrawStripMeter`, `DrawModulatorMeter`, `DrawAudioMeterBody` | Rounded segments, peak hold dot |
| V3 | Step / gate grid | `DrawPatternStepGrid`, `DrawArpGateGrid`, `DrawStutterGateGrid` | Rounded cells r=2, playhead column tint, on cells filled |
| V4 | Curve editor | `DrawMiniCurveWidget`, `DrawVelocityCurveChart`, `DrawModCurveParams` | Curve 1.5px, handles are dots |
| V5 | Keyboard | `DrawMidiKeyboard`, `DrawInteractiveKeyboard` | Rounded key bottoms, accent for held keys |
| V6 | XY pad | `DrawFxPad` | Dot cursor with soft trail |
| V7 | Sparkline | `DrawSparklineMiniGraph` | 1px line, end dot |

### D. Canvas (5)

| # | Class | Redesign |
|---|---|---|
| N1 | Node frame + header | Radius 8, category colour as a thin top band, title in medium weight, header glyphs from the icon font |
| N2 | Node sections / separators | `BeginAudioSection`, `NodeSeparator`: hairline + small caps label |
| N3 | Pins + cables | Dot pins (C11 sizes), cable ends land on the dot centre, hover thickens the cable |
| N4 | Groups / backdrops | Radius 10, faint tint, header like N1 |
| N5 | Canvas grid, marquee, minimap | Dot grid instead of lines, marquee in accent at 8% |

### E. App chrome (8)

| # | Class | Section |
|---|---|---|
| A1 | Top bar | 5 |
| A2 | Display box (bar/beat/tempo) | 5 |
| A3 | Menus + context menus | 4c |
| A4 | Docked panels + seams | 4c |
| A5 | Tables / lists (browser, mod matrix) | 4 |
| A6 | Timeline (ruler, track header, clips) | 6 |
| A7 | Dialogs, Settings, help windows | 4c |
| A8 | Tooltips + toasts | 4b |

**Total: 6 foundations + 32 component classes** (12 controls, 7 visualizers, 5 canvas, 8 chrome), **plus 15 classes added in 0b** (segmented control, customisable bars, display modes, notification dot, status light, search with scope, filter chips, inline cell glyphs, panel footer, empty state, node-type icons, rich tooltip, cursors, drag feedback, toast) → **47 component classes**.

## 0b. Critical review: what the first draft missed

Judged three ways: Logic references, an experienced product designer, current design-system practice.

### Missed in the Logic references

| # | Element | Where in Logic | Infinite version |
|---|---|---|---|
| G1 | **Segmented control** | Grid/list view toggle, browser view switch | Joined pill, one segment filled; replaces paired toggle buttons |
| G2 | **Customisable bars** | Right-click → Track Header Components, Configure, Store as User Defaults | Right-click any bar (top bar, track header, node header) → show/hide items, store as default |
| G3 | **Display modes** | Chevron on the LCD switches Beats / Time / Custom | Chevron on the display box: bars.beats, timecode, CPU/fps |
| G4 | **Notification dot** | Red dot on browser / chat icons | Small red dot on a glyph = something new or needs attention |
| G5 | **Status light** | Orange dot top-right (activity) | One status dot for audio engine / recording / projector live |
| G6 | **Disabled state shown, not hidden** | Greyed tuner icon | Unavailable tools stay in place at 35% with a tooltip saying why |
| G7 | **Search field with scope** | Magnifier + chevron, placeholder text | Search with scope menu (nodes / presets / files) |
| G8 | **Filter chips** | Instrument / Genre / Descriptors, heart | Chip row in Browser: category, tag, favourite |
| G9 | **Inline cell glyphs** | Lock, anchor, thumbnail in table rows | 16px glyphs and mini thumbnails allowed in table cells |
| G10 | **Panel footer bar** | Preview speaker, loop, volume slider, item count | Browser footer: preview, loop, volume, count |
| G11 | **Empty state** | "0 items" + "Get More Sounds" | Every list/panel has an empty state with one next action |
| G12 | **Object icons** | Track icons (green line-art), plugin slot card with power colour | **Node-type icons**: one glyph per node family/type (~73 node types) for browser, headers, collapsed nodes |
| G13 | **Rich tooltips** | `Track 15 "P15"` | Tooltip = name + value/identity + shortcut |

### Missed by a designer's critique

| # | Gap | Why it matters | Rule |
|---|---|---|---|
| G14 | **Semantic colour roles** | Accent, category colours (`CategoryColors.cpp`), state colours and "Multicolor" will collide | Fixed roles: record red, solo yellow, mute blue, modulation, prediction green, warning, error; accent never reuses a role colour |
| G15 | **Accessibility** | Pro users work in dark rooms and on stage | Text contrast ≥ 4.5:1, controls ≥ 3:1, never colour alone (add shape or glyph), colour-blind check on role colours, full keyboard path (`docs/plans/keyboard-nav`) |
| G16 | **Hit targets** | Small glyphs are hard to hit on a trackpad | Hit area ≥ 24×24 even when the glyph is 16; pins already use `ExpandPinHit` |
| G17 | **Scale + density** | `gUiScale` exists; a 20px grid must survive 1x, 2x and user scale | Glyph font baked per scale; compact / regular density setting |
| G18 | **Zoom level of detail** | Zoomed-out node canvases turn to mush | Below a zoom threshold nodes draw header + icon + pins only |
| G19 | **Cursors** | 45 `SetMouseCursor` calls, all stock | Cursor set is part of iconography: pencil, scissors, trim, hand, link, no-drop |
| G20 | **Drag and drop feedback** | Users can't see where a drop lands | Drop target highlight, insertion line, drag ghost with icon |
| G21 | **Selection model** | Canvas, timeline, tables each draw selection differently | One look for hover / selected / focused / multi-select everywhere |
| G22 | **Feedback surfaces** | Errors and saves are silent or modal | Toasts (non-blocking), inline errors (Field already keeps last working), progress for long jobs |
| G23 | **Text expansion** | i18n is planned (`docs/plans/i18n`); German runs ~30% longer | No fixed-width text buttons; labels truncate with tooltip |
| G24 | **Live performance mode** | Infinite is an instrument; the chrome must not steal attention or frames | Perf mode: no chrome animation, larger targets, output gets visual priority |
| G25 | **Three platforms** | Logic is Mac-only; Windows/Linux have no global menu bar | Top bar spec for each platform: macOS keeps native menus; Windows/Linux keep in-window menus |

### Missed by current design-system practice

| # | Gap | Rule |
|---|---|---|
| G26 | **Tokens as the single source** | `tokens.json` (colour, radius, spacing, type, motion) → generated C++ header; no literal colours or radii at call sites |
| G27 | **Component gallery** | In-app gallery window of every class in every state, light/dark. Debug builds only (`#ifndef NDEBUG`), per the no-debug-tools-in-release rule |
| G28 | **Visual regression tests** | Headless render of the gallery to PNGs; CI diffs against goldens on all three platforms |
| G29 | **UI performance budget** | ~280 knobs × arc segments adds vertices; budget per node and per frame, measured with the perf bench before/after |
| G30 | **Migration without a mixed UI** | Switch one class at a time across the whole app (all knobs at once), never node by node, so old and new never sit side by side |
| G31 | **Governance** | Icon request → draw on grid → lint → sheet review → merge; naming `verb-object` / `object` ; deprecation list |
| G32 | **Measure** | Before/after: time to find a node, time to patch a known chain, mis-click rate in the self-test harness |

## 0c. Inventory: proof that nothing is missed

`tools/design/inventory.py` scans `src/app` + `src/arrange` and writes `inventory.md` (this folder): every element class × 13 screen surfaces, plus every drawn icon and where it is used. It is the checklist; the plan is done when its numbers say so.

### What the scan found (2026-10-08)

| Finding | Number | What it means |
|---|---|---|
| Literal colours (`IM_COL32`, raw `ImVec4`) | **1551** (nodes 862, mod matrix 235, timeline 158) | Tokens can't restyle these. The biggest hidden job: every one moves to a token or a role colour |
| Hand-drawn shapes (`AddRect`, `AddCircle`, ...) | **749** (nodes 496, mod matrix 92, timeline 79, perf 39) | Custom UI the widget restyle won't touch: each must be reviewed by hand |
| Hand-drawn text | 120 | Must use the type tokens (size, tabular digits) |
| Drawn icons | 52 uses of 30 Tabler icons | All 30 get an Infinite Glyph; list in `inventory.md` |
| **Close / cross controls** | 9 drawn X + 2 text `"x"` buttons (`AudioNodeBodies.cpp:163`, `ParamBodies2.cpp:574`) | One close glyph, one size, one hover (circle fill), one hit area, everywhere |
| Text arrow buttons | `"<"` / `">"` in `SamplerBodies.cpp` | Chevron glyphs |
| Feedback | One status string (`gPatchStatus`), no toast system | G22 builds the feedback layer |
| Drag and drop | 7 sites (timeline, perf) | G20 drop/insert/ghost visuals |
| Cursors | 45 sites (timeline 21, nodes 15) | G19 cursor set |

### Per-surface checklist

| Surface | Must cover |
|---|---|
| **Close / cross** | Every X: panel close, chip remove, binding remove, search clear, node delete, clip remove |
| **Modulation matrix** | Rows (table style), mod dots, curve menu, eye toggles, range bars (hand-drawn, 92), source colours as role colours, empty state |
| **Timeline / Arrange** | Header (section 6), ruler, track headers M/S, clips + waveforms + thumbnails, 13 tool glyphs (pointer, pencil, scissors, trim, range, hand, zoom, ...), cursors (21), loop band, playhead, drag/drop |
| **Performance mode** | Pads/cells (39 shapes), 60 menu items, drag/drop, live-safe rules (G24) |
| **Viewport panel** | Cards, toggles, close glyph, resize handles, empty state |
| **Canvas + nodes** | Node frame/header, pins, cables, groups, minimap, marquee, zoom LOD (G18), every body control (C1–C12), visualizers (V1–V7) |
| **Navigation** | Canvas pan/zoom feel (inertia, zoom to cursor), minimap, keyboard focus path (`docs/plans/keyboard-nav`), focus ring visibility, "jump to node" |
| **User feedback** | Toasts, inline errors, progress, save state, undo hint, hover/press/on states (4b), tooltips (G13), empty states (G11) |
| **Popups + menus** | 233 menu items + 82 popups: one menu style, shortcuts aligned, icons optional but consistent |
| **Settings + help** | Grouped sections, switches (C5), tables, tabs |

### Done means

| Check | Target |
|---|---|
| `Tabler::` call sites | 0 |
| Literal colours outside the token header | 0 (allow-list for data colours such as user clip colours) |
| Hand-drawn shapes | Each reviewed and either on tokens or replaced by a shared helper |
| Every class × surface cell in `inventory.md` | Appears in the gallery (G27) and in visual goldens (G28) |

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

Status: done. Shared `PushSelectedButtonColors` / `PopSelectedButtonColors` (Theme.cpp); every inline accent site uses them; `ChipButton` keeps the accent on hover by construction.

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

## 4c. Type, spacing, menus, panels

| Token | Value |
|---|---|
| Spacing unit | 4 (all gaps are 4, 8, 12, 16) |
| Radii | 2 (cells), 4 (fields, checkbox, wells), 6 (icon tiles, buttons), 8 (pills, nodes), 10 (groups) |
| Type sizes | 11 caption, 13 body, 15 title, 22 display digits |
| Digits | Tabular everywhere a number changes (knob readouts, display box, tables) |
| Labels | Lowercase in nodes (existing convention), Title Case in menus and Settings |
| Menus | Radius 8, 4px inset rows, hover row pill, shortcut text muted and right-aligned, separators inset |
| Panels | One elevation step per level (docked < elevated < popup); seams are hairlines, never gaps |
| Dialogs | Grouped rows in rounded sections (Settings → Theme style from the Apple screenshot) |

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
| 1 | **Foundations, no visible change first**: design folder + `tokens.json` + generator; move all 1551 literal colours and 749 hand-drawn shapes onto tokens/role colours (screenshots identical before/after); ratchet check in CI; gallery + goldens (G27–G28); glyph pipeline + 12 pilot glyphs; `UiAnim`; perf budget (G29) | `feature/design-foundations` | Literal colours = 0 outside allow-list; goldens unchanged; pilot sheet approved |
| 2 | Node interior: controls C1–C12, visualizer tokens V1–V7, canvas N1–N5, node-type icons (G12), zoom level of detail (G18), cursors + drag feedback (G19–G21) | `feature/node-ui-redesign` | `node-ui-pillars` checklist + `node-ui-sweep` pass; Windows/Linux CI green |
| 3 | Chrome: top bar, display box, timeline, tables, menus, panels, dialogs (A1–A8), G1–G11, G13, G22–G25; retire `TablerIcons.h` | `feature/chrome-redesign` | `panels-sweep` pass; no `Tabler::` call sites left |

### Pilot set (block 1)

play · stop · record · capture · loop · metronome · count-in · browser · mixer · mod matrix · viewport · arrange

## 8. Organisation: built so any future UI change is one small edit

Owner rule (2026-10-08): future UI/UX updates must be very easy to execute. Every look-and-feel decision lives in exactly one place, and the tools refuse anything that bypasses it.

### Where things live

```
src/app/ui/design/
  tokens.json            ← THE source: colour roles, radii, spacing, type, motion, sizes
  Tokens.gen.h           ← generated from tokens.json, never edited
  Glyphs.gen.h           ← generated from art/icons, never edited
  UiAnim.h / .cpp        ← the only animation timing code
  components/            ← one file per component class (section 0 ids)
     IconTile  Knob  DotSlider  Fader  Checkbox  Switch  Dropdown  Segmented
     Chip  SearchField  Table  Menu  Tooltip  Toast  EmptyState  Pin  ...
art/icons/src/*.svg      ← icon masters (16 and 20 px)
tools/design/
  build_tokens.py        ← tokens.json → Tokens.gen.h
  build_glyphs.py        ← SVGs → font + Glyphs.gen.h
  lint_icons.py          ← grid, stroke, padding checks
  inventory.py           ← the coverage scan (section 0c)
  ratchet.json           ← allowed counts of literal colours / raw ImGui widgets per file; may only go down
docs/plans/iconography/  ← this spec + generated inventory.md
.claude/skills/infinite-design-system/SKILL.md  ← the runbook
```

`CategoryColors::UiTheme` (6 base colours per theme preset) stays the user-facing theme; `tokens.json` defines how every role is **derived** from those 6 plus fixed role colours (record, solo, mute, modulation, prediction, warning, error), so all presets keep working.

### The three layers

| Layer | Example | Who may use it |
|---|---|---|
| Base tokens | `color.accent`, `radius.8`, `space.4`, `motion.hover_in = 120ms` | Only role tokens |
| Role tokens | `control.knob.arc`, `surface.panel`, `state.record`, `text.muted` | Only components |
| Components | `Knob(...)`, `IconTile(...)`, `Toast::Show(...)` | Every call site (nodes, panels, chrome) |

A call site never names a colour, radius or duration. It names a component; the component names roles; roles name base tokens.

### Recipes (what a future change costs)

| Change | Edit |
|---|---|
| New accent / softer dark mode | `tokens.json`, one value |
| All knobs get a thicker arc | `tokens.json` (`control.knob.arc_width`) or `components/Knob` |
| Hover feels too slow | `tokens.json` (`motion.hover_in`) |
| New icon | Add SVG to `art/icons/src`, run `build_glyphs.py` |
| New kind of control | New file in `components/`, a gallery entry, a golden |
| Restyle one surface (e.g. mod matrix rows) | That surface's component (`Table`), never the panel file |

### Guards (so organisation can't rot)

| Guard | Fails when |
|---|---|
| Ratchet (`tools/design/ratchet.json`, CI) | A file gains a literal colour, a raw `IM_COL32`, or a raw `ImGui::Button`/`Checkbox`/`Combo` outside `components/` |
| Generated-file check | `Tokens.gen.h` / `Glyphs.gen.h` don't match their sources |
| Icon lint | An SVG breaks the grid/stroke/padding spec |
| Visual goldens | A component's pixels change without the golden being updated in the same commit |
| Inventory | A component class has no gallery entry |

## Decisions

| # | Question | Decision | Why |
|---|---|---|---|
| 1 | Grid and stroke | 20 grid, 1.5px stroke, round caps | Matches Logic/SF toolbar density; 1.5 stays crisp at 2x and readable at 1x |
| 2 | Delivery format | Icon font built from SVGs | One atlas, crisp, cross-platform, same as SF Symbols / Material |
| 3 | Brand motif | The dot (pin = handle = icon terminal) | Already Infinite's meaning for signal; gives an identity that isn't Logic's |
| 4 | Accent picker | 9 swatches + Multicolor | Accent already flows from one `t.accent` |
| 5 | Order | Move all literal colours to tokens first, no visible change (owner approved 2026-10-08) | After it, a redesign is edits to `tokens.json` + `components/` |
| 6 | Organisation | One design folder, three token layers, CI ratchet (section 8) | Owner: future UI changes must be very easy to execute |

## Open

- Multicolor: per-panel category colour, or per-node-category accent?
- Display box: does it also show timecode (h:m:s:f) on click, as Logic does?
