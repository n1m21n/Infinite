# Node polish brief: every node, every detail

Paste the **Prompt** section into a fresh session. Everything else is the spec it points to.
Written 2026-10-09 after a one-by-one review of all 307 nodes (params closed and open), on `feature/ui-canvas` at `8a3d27f2`.

---

## Prompt

Continue the node polish pass on `feature/ui-canvas` of Infinite. Spec: `docs/plans/ui-system/node-polish-brief.md` (this file). Load first: `node-ui-pillars`, `infinite-design-system`, `audio-node-ui`, `run-infinite-hygiene`, `bug-blast-radius` (for the shared-component changes).

Work the **Order of work** below, top to bottom. One commit per step, gates green before each commit, a contact-sheet before/after sent to the owner with SendUserFile after each step. Decide yourself; never hand a design decision back to the owner. A step is done only when its **Acceptance** line holds on screenshots in dark and light.

Rules: no Claude attribution anywhere (no "Generated with Claude Code", no Co-Authored-By). Commit by explicit path; never stage `.mcp.json`, `.gitignore`, `art/*`, `glyph-sheet.png`, `docs/plans/iconography/inventory.md`, `external/vst3sdk`. Never UI-script the canvas: shoot with `tools/design/gallery/`. Fix in shared components, never per call site. No dropdown chevrons. Never add a control to fill a hole (P6). Every param row keeps all its interactions (section 2b: double-click to type, hover-and-type, the right-click menus for plain / modulated / expression / recording values, drag-to-bind, prediction, Tab, MIDI learn, undo); a new control type ships only if it carries them all. Functionality must not change: the full self-test suite (`driver.sh --auto`) passes at the end, as it did at the start (116 pass).

Finish with: fixed, decisions made, not visually verified, remaining.

---

## 1. The design language, as checkable rules

The owner's words are symmetry, clarity, breathing space, user experience and the feel of the buttons. Each becomes a rule that can be checked on a screenshot.

| Principle | Rule | Check on the shot |
|---|---|---|
| **Symmetry** | The body fills the node. Content is centred on the node's axis or spans edge to edge; never left-hugging with a dead right strip. `out` pins sit on the right edge. | Left and right inner margins equal (±2 px). No strip wider than one gutter. |
| | Opening params never changes node width. | `widen.py` reports 0 nodes. Baseline: 135 of 307. |
| | Rows of equal things are equal: same width buttons in a row, same size swatches, same knob size in a row. | No row with mixed widths unless one is a primary action. |
| **Clarity** | One way to show one value. A value is shown once (Null Modulator shows it four times). | Count representations per value: 1, or 2 if one is a graph. |
| | Every pin has a name. Every control has a caption, and the caption sits *inside* its field (dim label left, value right), like sliders already do. | No bare pin dots. No caption to the right of a field. |
| | The control matches the data: on/off is a checkbox, 2–5 choices are a segmented `PillGroup`, a beat length is a division (1/16), an int never prints decimals, angles print °, Hz prints Hz. | No "0=X 1=Y 2=Z" sliders, no "16.000", no "2800.000". |
| | Words: lowercase captions, one spelling (`colour` in UI text is fine but pick one app-wide; code names never leak, e.g. `constantIn`), real arrows (→), "-inf" not "-100.0 dB". | grep the strings. |
| **Breathing space** | One gutter token between groups, one row token between rows. Sections are `SectionCard`s (Metallic is the reference). | Row pitch constant within a node. |
| | Empty states are one centred `EmptyState` line (+ optional hint) in the middle of the area they replace. Never top-left, never plain wrapped help text in body size. | "no input", "no sample loaded", "no loop", Syphon/NDI messages all centred, caption size. |
| | Dense nodes earn density with structure, not with smaller text. Nothing is drawn below the body text size. | Drum Sequencer text currently smaller than every other node. |
| **User experience** | Idle never lies: no playhead while stopped, no "running" while idle, no lit button that means the opposite state. | Note Sequencer and Drum Sequencer at rest show no playhead. |
| | Status line format is the same everywhere: `<what> · <state>`, middle dot, tabular digits, no stray separators. | Gain "+0.0 dB  –", Wavetable "Basic Shapes + – –" gone. |
| | Truncation always ends in "…" and has a tooltip with the full text. | Output paths, Text font, Remove BG engine, Predictive Modulator hint. |
| **Feel of the buttons** | One button family (`ActionButton`): one height, label-sized width with a shared minimum, aligned to the slider column, in rows of equal widths. Primary action uses the accent; destructive uses the record role; letters are never icons ("x" → the close glyph). | Line up every button row against the slider column edge. |
| | Pressed, hover and active states exist and read the same everywhere; toggles that latch (loop, freeze, rev) are `GlyphToggle`/`PillGroup`, not plain buttons. | Sampler "loop/rev/p-p", Granular "freeze/loop". |

### Borrowed from Infinite's brand (website/film identity)

| Brand element | Use in nodes |
|---|---|
| Calm premium ground, one loud thing | A node has one accent colour (its category) plus amber for "live/now" (playhead, current step, active point). Nothing else is saturated. Audio Color Ramp's primaries go. |
| Everything rounded: 10 / 16 / 24 / pill | Fields, buttons, swatches and step cells use the radius tokens; no square swatches or square pins. |
| Port dot | Pins are the one round dot shape everywhere (colour rows' square grey pins go). |
| Geist Mono HUD, tabular digits | All live numbers go through `Readout` with tabular digits so they do not jitter. |
| Node cards and category colours | Already the canvas grammar; keep the category tint as the only body tint. |

---

## 2. Shared fixes (each one fixes many nodes)

| # | Problem | Where it lives | Fix | Acceptance |
|---|---|---|---|---|
| R1 | Dropdown caption drawn outside-right (`SameLine(); TextDisabled(StripParamLabel(label))`), so the node widens and the caption sits off the value's baseline | `src/app/ui/ParamWidgets.cpp:657-659` (`DropdownButton`) and the dropdown face `DropdownField.h` | Caption inside the field: dim label left, value right, same as `SliderFloat` faces. Field width = `kParamWidth` (`AppShared.h:32`). | Keeps the hit rect, right-click, Tab, hover-type and the `gParamPinScreenList` drop rect over caption + value (section 2b). `widen.py` count drops sharply; Polygon, Projection, Output, Metallic, Slicer show no outside captions. |
| R2 | Colour rows: square grey pin, raised; swatch size varies; caption outside | colour param row in `ParamWidgets.cpp` + `ColourChip.h` | One row: round pin, fixed-size rounded swatch (one row tall), caption inside the row. | Set Vertex Color, Outer Glow, Light, Color Ramp stops identical rows. |
| R3 | Params column (168) does not fill the node; multi-input headers set the width; `out` not at the right edge | node body layout in `src/app/frame/StageNodeBodies.cpp` | Body width = node width. Columns stretch (one column: full width; two: equal halves with one gutter). Wrap long pin headers into rows so they never set width. `out` row right-aligned to the node edge. | Join, Union, Group 3D, Switcher 3D, Render 3D, Layer Stack, LFO/Random scope, Predictive Modulator scope: equal margins. |
| R4 | Buttons: random widths, not on the slider column | every `ActionButton::Draw` call with ad-hoc sizes | Add `ActionButton::Row(labels…)` that lays out N equal buttons across the column width. Single buttons span the column. Inline buttons next to checkboxes ("clear" in Macro XY, Resynthesize) move into the row. | Every button row's edges line up with the slider column. |
| R5 | Empty states left-aligned / top-left / plain help text | 29 literal sites (`grep -rn '"no input"\|"no geometry"\|"no sample loaded"' src`), plus Syphon/NDI/Slideshow/Looper/MIDI File/Predictive hints | All through `EmptyState::Draw(min, max, msg, hint)`. | Every preview's empty message centred, caption size. |
| R6 | Unnamed input pins | FilterDef nodes (all Compositing/Effects filters), Audio Ribbon, Image to Points, Image Analyze, Predictive Coloring; Note Router's 4 grey dots | FilterDef input default name `in`; name the rest. Note Router's dots get labels 1–4 and light up on the routed voice. | No bare pin dot on any sheet. |
| R7 | Label casing/spelling | FilterDef labels are Title Case ("Radius", "Translate X", "LUT Size"); Color/colour mixed; `geo` vs `out`; ASCII `->`; `constantIn` | Lowercase FilterDef labels at the definition; one spelling; output pin `out` everywhere; `→`. | grep clean; P11 strings rule holds. |
| R8 | Icon row gap where 2+ input nodes have no power icon | header icon row | Icons pack left with no hole; nodes without a monitor icon do the same. | Join, Union, Material, Wrap, Camera, Light: no gaps. |
| R9 | Wrong control for the data | Select/Wrap `axis` slider; LUT Size; Halftone angle; Luma Key / Edge Sobel `invert` dropdown; Convolve k11..k33; seeds shown as `1.0` vs `0.000`; Macro Step Gate `rate (beats) 0.250`; Field Synth cutoff `2800.000` | axis → `PillGroup` X/Y/Z; ints print as ints; angle prints °; invert → checkbox; Convolve → 3×3 grid of number cells; seeds are ints; beat rates use the rhythmic table (`rhythmic-quantization-standard`); units on every value. | None of the listed strings on any sheet. Each new control carries every behaviour in section 2b; a slider that becomes discrete is checked against saved prediction bindings (I8). |
| R10 | Status line formats differ | node status well (`FieldWell`) text per node | One formatter: `<what> · <state>`, tabular digits, `-inf dB`, no trailing separators. Stat lines (triangles, alive) either everywhere a count is meaningful, or nowhere. | Gain, Mixer, Wavetable, Cube/Ocean/Particle consistent. |
| R11 | Placeholder previews | Camera / Light / Particle System "scene node" black box; HDRI help text wraps wide | Draw a small gizmo preview (camera frustum, light icon with rays, particle dots), or the shared `EmptyState` with the node glyph. HDRI help text wraps inside the column. | No black text boxes in 3D sheet. |
| R12 | Scope/graph narrower than the node, or values clipped at the border | LFO/Random scope, MIDI CC/Trigger dot clipped, Delay first echo bar touching the top, Color Ramp/Curves handles overhanging | Graphs span the body width with an inner inset of one radius; plotted values clamp inside the inset. | Nothing touches or crosses a frame edge. |

---

## 2b. Param interactions that must survive every redesign

A param row is not only a look. Each one carries the behaviours below. R1, R2, R3, R4 and R9 all change param rows, so before any of them ships, every behaviour here has to work on the new row exactly as before. If a new control type replaces an old one (dropdown → `PillGroup`, slider → checkbox, 9 sliders → 3×3 grid), the new control has to carry all of these too, or that replacement does not ship.

| # | Behaviour | Where it lives today | What must hold after the change |
|---|---|---|---|
| I1 | **Double-click the value** opens a text field seeded with the current value, or with `=expr` when the param has an expression | `BeginTypedEditFromCurrent` (`ParamWidgets.cpp:71`), double-click checks in `ModWidgets.cpp` (~`:413`, `:485`, `:571`, `:1428`) | Double-click on the *value area* of the new face still opens typing. The field fills the same rect as the control (caption-inside faces: the field covers caption + value). |
| I2 | **Hover and type** a digit, `-`, `.` or `=` jumps straight into typing; `=` always starts a blank formula | `HandleParamTypeHotkeys` (`ParamWidgets.cpp:97`), guarded by `TextFocusClaimed()` | Works on the whole face including the inside caption, not only the old value part. A field already open (double-click) keeps every keystroke. |
| I3 | **Right-click a plain value**: Enter Value, Enter Expression, Start Recording, Add to Performance Matrix (bool params: Performance Matrix only) | `DrawModulationBindingMenu`, popup in `StagePopupsA.cpp` (~`:650-680`) | Right-click anywhere on the face opens it and consumes the click so the node menu does not also open. |
| I4 | **Right-click a modulated value**: range lo/hi, Full range, Around current, Invert, Unbind | `StagePopupsA.cpp` (~`:505-545`) | Same menu on the new face. Modulated faces stay read-only to drags but still open this menu. A modulated dropdown must still be right-clickable to unbind (comment at `ParamWidgets.cpp:633`). |
| I5 | **Right-click an expression value**: Edit Expression, Performance Matrix, range, Full range, Invert, Unbind. A broken expression shows as a plain editable slider but keeps the fx/x controls | `StagePopupsA.cpp` (~`:548-590`), `ExpressionErrorFor` (`ModWidgets.cpp:137`) | Same. |
| I6 | **Recording**: Shift-drag records a gesture, an armed param records on the next drag, playback locks the drag (Shift overrides), red recording look, right-click gives Cancel / speed / range / Invert / Record Again / Unbind | `GestureRecorder` calls through `ModWidgets.cpp` (`:143`, `:480-505`, `:705`, `:1020`), `StagePopupsA.cpp` (~`:592-650`) | Every new continuous face calls the same `GestureRecorder` hooks and shows the same states. |
| I7 | **Modulation**: the pin dot shows Idle / Modulated / Expression / Prediction (green); dropping a cable or dragging a modulator node onto the param binds it; the bound range draws as a band on the control | `PinDot::Param` (`ModWidgets.cpp:163`), `gParamPinScreenList` hit rects (`ModWidgets.cpp:173`), range band (`ModWidgets.cpp:831`) | Pin dot vertically centred on the new face (P2). `gParamPinScreenList` rect covers the new face exactly, so drag-to-bind still hits it. Range band visible on sliders, knobs and the new controls. |
| I8 | **Prediction**: green sources may only drive continuous params; discrete targets refuse with a reason; predictor destinations get the grab/ghost view; Shift makes them editable | `PredictorBindRefusal`, `BeginPredictorGrab`/`EndPredictorGrab` (`ParamWidgets.cpp:773-866`) | A param that turns from slider to `PillGroup`/checkbox becomes discrete. Check that this does not break an existing prediction binding in a saved patch; if it would, keep it continuous. |
| I9 | **Keyboard**: Tab walks params in draw order with a focus ring; Left/Right nudges a slider or steps a dropdown; digits start typing on the focused one | `KbParamHook` (`ParamWidgets.cpp:146`), `KbDiscreteHook` (`:455`) | Draw order (Tab order) unchanged by the new layout, or deliberately left-to-right/top-to-bottom. New discrete controls step with Left/Right. |
| I10 | **MIDI Learn** draws a learn ring on the param while armed | `StateRing::Draw(... Learn ...)` (`ModWidgets.cpp:588`) | Ring hugs the new face rect. |
| I11 | **Tooltip** shows the range and "Double-click, or hover and type, to enter a value" | `KbParamHook` tooltip | Still shown on the new face. |
| I12 | **Undo**: every typed value, drag, step and binding change is one undo step | `PushUndoCheckpoint` calls | Unchanged. |
| I13 | **Save / load**: param indices, names and types are what patches store | `ParamRef`, node `Save`/`Load` | No param changes index, name or int/float type because of a UI change. Renaming a *label* is fine; renaming the stored name is not. |

**Tests that cover these** (must stay green; add a case when a new control type appears): `UITYPETEST` (typing), `KBCURSORTEST`, `KBDISCRETETEST`, `MODTEST`, `MODBOUNDSTEST`, `MODCURVETEST`, `MODDROPDOWNUNDOTEST`, `MODMATRIXTEST`, `PREDBINDTEST`, `RECTEST`, `GESTUREUNDOTEST`, `MIDILEARNTEST`, `UNDOTEST`, `MPCMODTEST`, `AUDIOPARAMSWEEPTEST`. Run `invariant-interaction-audit` on the R1 change before it ships: it is a new guarantee ("no outside captions") that a sibling call site could undo.

**Per-step check**: after steps 1 and 3, on one node of each control type (slider, knob, dropdown, checkbox, new PillGroup, Convolve grid), go through I1–I12 with the self-tests above. No UI scripting.

---

## 3. Per-family details (after the shared fixes land)

### Source
- Image Source / Video idle checkerboard gets an `EmptyState` ("drop an image" / "drop a video").
- Text: the text row gets its pin; font dropdown truncates with "…"; default font is the UI sans, not serif.
- Video In: no red text for "waiting for camera permission" (use the warn role + `StatusDot`); paired checkboxes on one row grid.
- Video: remove the stray separator above the out pins. Draw: "animation" sub-header becomes a `SectionCard` title.
- Long checkbox labels ("expose state texture output") shorten to two words.

### 3D
- Material and Render 3D: one column grammar like every other node; preview centred; no dead area.
- Audio Ribbon: the 10 transform sliders collapse into a `SectionCard` "transform" (closed by default).
- Cloth: "wind turbulence" shortens ("turbulence") so the value has room.
- Resynthesize 3D: three inline buttons → one `ActionButton` row.
- Depth Projection: split into sections.

### Compositing / Effects
- Curves: editor = column width; endpoint handles inside the frame; remove the anti-diagonal guide.
- Color Ramp: gradient and stop markers inside the frame; stop delete uses the close glyph.
- Audio Color Ramp: band meters centred; category accent only.
- Reaction Diffusion: "with an input connected:" becomes a `SectionCard` title.
- Remove BG: engine line shortened, "(macOS 14+)" moves to the tooltip.
- Feedback: params panel is empty when open; check whether that is real, fix if it is.
- Color Adjustments: group into sections (tone, colour, detail).
- Resynthesize: XY pad corner labels inside the pad; the unique "pad corners" collapsible becomes a `SectionCard`.

### Modulators
- One body grammar for all modulators: scope on top (full width), value readout, then controls. Envelope and Audio to CV move to it; their readout must not look like an editable field.
- Envelope: the knob labelled "in" is renamed (it duplicates the pin).
- MIDI CC / MIDI Trigger: the empty dark box gets an `EmptyState` ("move a control to learn"); "bound: unbound" → "not learned".
- CV to Pitch: readout through `Readout`, same size as other values.
- Null Modulator: show the value once (scope + one readout).
- Image Analyze: real preview of the input; "Result" swatch only when live.
- Pattern: value chips get one row of air below the header.
- Audio Analyze: "idle" box → `EmptyState`; the 13 output pins in a tidy two-row grid with names.

### Utility
- Output: paths truncate with "…" + tooltip; "Offline Render" is a `SectionCard`; render fps / duration / preroll are sliders with units like everywhere else; the 15/30/45/60 s chips are a `PillGroup`; buttons on one row grid; the fps field must not render as selected text.
- Projection: mode/pattern captions inside (R1); the four buttons as one equal row (already close).
- Syphon / NDI In and Out: message centred as `EmptyState`, caption size; Refresh is a column-width button.
- Mixer: channels control as a `PillGroup` 1–8 or a full-width slider; "sum -inf dB"; "8 in → 1 out".
- Splitter: show one `out` pin per active output (or say "1 out" if that is what it does); remove the dead space.
- Gain: drop the stray "–" in the status.

### Notes
- Note Sequencer: no playhead when stopped; Synced and 1/16 dropdowns get inside captions ("mode", "rate"); "8 steps, looped" goes into the status line; columns and playhead on `StepCell`.
- Note Router: the four voice dots labelled and lit by activity; the dropdown is not duplicated in the status line.
- Keyboard, Note Capturer: fine; check the numpad/loop row sits on the grid with the knobs.
- MIDI File: idle area shows one centred `EmptyState`; the "Load…" button spans the column; help line removed (duplicate of the empty state).
- Gate: fine.

### Synths
- **Drum Sequencer** (biggest single item): body text at normal size (no shrunken text); step grid with visible cells (empty = recess, on = accent, accent step = brighter, current = amber ring), no playhead when stopped; lane cards as `SectionCard`s with titled lanes; "choke" dropdown with an inside caption; lane close uses the glyph; the bottom knobs show values on hover/drag like every other knob; Stop/Randomise/Clear as one `ActionButton` row with Stop as a transport toggle, not a lit accent bar.
- Sampler / Slicer / Granular / Looper: empty waveform areas use `EmptyState` centred; "auto · running" moves into the status line; latching toggles (loop, rev, p-p, freeze) become `GlyphToggle`s; Slicer's two "onsets" controls renamed ("slice by" / "count").
- Wavetable: status "Basic Shapes + – –" fixed; consider `SectionCard`s per oscillator so A and B read as two columns of the same thing.
- Field Synth: synth grammar (no eye icon, no image-node layout), cutoff in Hz.
- Metallic: the reference node; leave it.
- MPC: hide the "shot" corner label when it is the default.

### Audio effects
- EQ: axis labels must not collide ("-18" over "20"); inset the dB labels.
- Compressor: label or remove the white dot on the curve; label the side meter (GR) or hide it when idle.
- Delay: first echo bar inside the frame.
- Reverb, Chorus, Phaser, Flanger, Drive, Stereo, Limiter, Pitch Shifter: check against the R rules only.

### Macros / Prediction
- Macro XY: "clear" into the button row; x/y readout through `Readout`.
- Macro Step Gate: the title shows once (not under the cells and again in the field); rate as a division.
- Drift / Moves: node title matches the library name (one name each).
- Predictive Modulator: the hint does not truncate (move it into the `EmptyState` of the scope); `constantIn` → "constant".
- Predictive Coloring: name the input pin; "20% conf" through the status line, not amber free text.

---

## 4. Order of work

| Step | Scope | Commit message stem |
|---|---|---|
| 1 | R1 + R3 + R4 (params row, body width, button rows). Re-run `widen.py`; target 0. | "Params rows: captions inside, body fills node, button rows" |
| 2 | R5 + R6 + R7 + R8 (empty states, pin names, labels, icon row) | "Empty states centred, every pin named, one label style" |
| 3 | R2 + R9 + R10 + R11 + R12 | "Right control for the data, one status format, graphs inside frames" |
| 4 | Drum Sequencer + Note Sequencer | "Sequencers: readable grid, no idle playhead" |
| 5 | Per-family lists above, one commit per family | "<Family>: polish pass" |
| 6 | Live-state pass (below) | "Live states: waveforms, playheads, visualisers" |

After each step: build, `python3 tools/design/inventory.py && python3 tools/design/ratchet.py`, `driver.sh --skip-build --group ui`, shoot the affected families with `tools/design/gallery/shoot.py <Cat…>` + `sheet.py`, compare against the baseline in `~/infinite-node-gallery/`, send the before/after sheet. Deploy `build/Infinite.app` to `~/Desktop/Infinite.app`. Last step: `driver.sh --auto`.

## 5. Not yet seen (do in step 6)

All 2026-10-09 shots are idle. Needed: a sample loaded in Sampler / Slicer / Granular / MPC / Looper (waveform + playhead + slice markers), audio running through every audio effect visualiser and meter, the Drum and Note Sequencers playing, MIDI File with a file. Add a fixture env hook (like `INFINITE_NODEGALLERY`) that loads a bundled test sample and starts the transport, then shoot. Light theme for every family is also unshot.

## 6. Baseline and tools

- Shots, sheets and sizes before this pass: `~/infinite-node-gallery/baseline-2026-10-09/`, `…-sheets/`, `sizes-baseline-2026-10-09.txt`.
- Tools: `tools/design/gallery/README.md`.
