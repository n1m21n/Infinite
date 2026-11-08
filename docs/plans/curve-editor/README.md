# Curve Editor — plan

Status: **APPROVED 2026-11-08** (Naman: "let's start executing this"). Block 1 in progress on `curve-editor/block-1`.
Owner idea 2026-11-08. All 7 points from the idea are in scope; nothing dropped.

```
 Block tracker (the only tracker for this work)
 ┌───────┬──────────────────────────────────────────────┬──────────┐
 │ Block │ What lands on main                           │ Status   │
 ├───────┼──────────────────────────────────────────────┼──────────┤
 │ 1     │ Curve model + panel + core tools + time/loop │ building │
 │       │ + click-to-assign                            │          │
 │ 2     │ Shape library + shape/transform tools        │ proposed │
 │ 3     │ Expression pages + recording (knob, CC learn)│ proposed │
 │ 4     │ Motion grade: frames/seconds, bezier handles,│ proposed │
 │       │ easing standards, channels, 3D driving       │ proposed │
 └───────┴──────────────────────────────────────────────┴──────────┘
 one block = one branch = one session; one commit per step inside
```

---

## 1. Goal

A docked **Curve Editor** panel, the sibling of 3D View (objects), Piano Roll
(notes) and Sample Studio (samples). In it you **draw, record, type or pick a
curve and assign it as a modulator** to any knob in the patch: sound, picture or
3D. It should be as good as DAW automation for music and as good as a graph
editor (Blender F-curves, After Effects) for motion.

## 2. Problem

| Today | Effect on the user |
|---|---|
| MSEG node exists, but its editor is a small graph inside a node card, 32 points max | Drawing a real curve breaks the flow: zoom the canvas, squint, tiny handles |
| MSEG appears in 3 of 106 readable patches; LFO in 12 | People modulate a lot (76/106) but almost never draw. The drawing tool can't be reached |
| CV Recorder records a raw take (16 samples/beat, 64 beats) that can't be edited | A recorded gesture can't be fixed, only re-recorded |
| GestureRecorder (Shift-drag) loops a knob move, but it's invisible | Users don't find it; it can't be edited |
| Typed expressions live per param field (`Modulation`, `ExprGlobals`) | A formula can't be seen as a curve or reused on another knob |
| No keyframes or easings anywhere in 3D; motion = modulators on params | Motion designers can't make a precise "ease-in-out over 48 frames" |
| Assigning = wire a pin, or the per-knob `##modbind` popup (issue #48: no auto-connect) | Assigning is slower than in a DAW |
| No automation lanes in Arrange | DAW users have nowhere to "draw automation" |

## 3. Scope

**In (all owner points):**

| # | Owner point | What it becomes |
|---|---|---|
| 0 | Panel, symmetric, all dock positions, no floating, pages, tools as icons | Docked panel (bottom / top / left / right); page strip; icon tool strip |
| 1 | Expression formula | Expression page: type `f(t)`, see it live as a curve, assign; bake to points |
| 2 | Recording with MIDI CC learn | Record page: arm, learn CC or touch a knob, record (Overwrite / Touch / Latch), lands as editable points |
| 3 | Built-in curve collection | Library: basic waves, steps/rhythms, envelopes, musical moves, random, motion easings, user presets |
| 4 | Assign like Performance Matrix | Assign button → click any knob on the canvas → bound; many targets per curve, each with its own range |
| 5 | Deep tools | Full tool strip (§8.4): pen, draw, line, bend, eraser, transform, smooth, simplify, humanize, mirror… |
| 6 | Time / quantise / loop | Length in bars/beats or seconds/frames; grid + snap + quantise; loop brace; one-shot/loop/ping-pong; crop to section |
| 7 | 3D motion, industry curves | Bezier keys with Blender handle types, Penner + CSS cubic-bezier easings, frame-exact export, XYZ channel overlay |

**Out (and why):**

| Not in | Why |
|---|---|
| Arrange-timeline automation lanes | A different object (song-position data). The curve model built here is reusable for it later |
| Floating window | Owner rule: dock only |
| A second modulation engine | Everything goes through the existing `IModulator` + `Modulation` bindings |

## 4. Existing constraints

| Constraint | Source | What it forces |
|---|---|---|
| Panels edit nodes; nodes own data (Piano Roll pages = MIDI lanes + linked node) | `PianoRollPanel.cpp` header | Page = one curve node on the canvas |
| Extend an existing node over adding a duplicate | new-features rule | Upgrade **MSEGNode**; no new "Curve" node |
| Shape maths shared by main thread and per-voice audio kernel | `core/ModShapes.h` | New segment types go into `ModShapes`, nothing else evaluates curves |
| Audio thread: no allocation, no locks | ModShapes header | Fixed-capacity point arrays; edits swap a snapshot |
| Dock codes 0 bottom, 1 right, 2 left, 3 top | `gTerminalDock` | Same codes, `gCurveEditorDock` |
| Design system: PanelFrame, PanelRail, ToolCursor, TimeRuler, ChipButton, FieldWell, MenuParts, EmptyState | `ui/design/components` | Build only from these; no invented look |
| No chevrons on dropdowns; short labels, no hint lines | UI memories | Inspector is labels + fields only |
| Bypass rule: 1-input modulators may bypass; bypassed nodes don't cook | bypass rule | MSEG bypass → outputs its trigger input / rest value, as today |
| Patches evolve; few users | patch rules | New MSEG encode with a version tag; old strings decode; no legacy flags |
| Export must be exact per frame (`Transport` timestamps k / fps) | `Transport.h` | Motion curves are evaluated at the frame time, never at the UI tick |
| Help = 4 hand-kept tables + per-OS `#if` branches | node help memory | Update all four for MSEG's new params |
| macOS, Windows, Linux | release | No OS-specific code in the panel |
| 8 GB build machine | build rule | All builds via `scripts/ibuild.py`, one at a time |
| Rhythmic divisions from one table | `rhythmic-quantization-standard` skill, `MusicTime.h` | Grid, length and quantise lists reuse `MusicTime::RateDivision` |
| Modulator procedure | `new-modulator-node`, `modulation-sweep` skills | MSEG upgrade follows them; sweep runs at the end of each block |

## 5. Hypothesis

If drawing, recording, typing and picking curves all happen in one big docked
editor, and assigning is one click to a knob, then:

- curve modulators (MSEG) get used about as often as LFOs;
- one curve can drive sound and picture in sync, which no DAW or 3D tool does live;
- 3D users get keyframe-grade motion without a keyframe system, because a
  frame-time curve bound to a param **is** an F-curve.

## 6. Baseline (recorded once, 2026-11-08; not re-measured later)

| Measure | Value today |
|---|---|
| MSEG in readable patches | 3 / 106 (LFO 12, Mod Curve 6, CV Recorder 5) |
| MSEG max points | 32, x and y in 0..1, one bend per segment |
| Segment types | 1 (power bend) |
| Time bases | Beats (synced), Hz — no seconds-length, no frames |
| Easing presets | 0 |
| Shape presets for a drawn curve | 0 (LFO has fixed shapes) |
| Record → editable curve | not possible (CV Recorder take is raw bytes) |
| Expression visible as curve | no |
| Steps to draw a 4-bar filter sweep and assign it | ~8: add MSEG, zoom canvas, add/drag points in card, set rate, drag wire or open modbind, set range |
| 3D: ease a camera over 48 frames | not possible exactly |

## 7. Success metric

| # | Metric | Target |
|---|---|---|
| M1 | Draw a 4-bar sweep and assign it | ≤ 3 actions, ≤ 10 s |
| M2 | Preset → assigned | ≤ 2 clicks after opening the panel |
| M3 | CC move → editable curve | 1 arm + the move; points ≤ 64 for a 4-bar take at default thinning |
| M4 | Expression → curve on screen | updates in the same frame as the typing |
| M5 | Easing exactness | every easing preset within 1e-4 of its reference formula; bezier keys within 1e-3 of Blender's F-curve at every frame (headless Blender A/B) |
| M6 | Frame exactness | export at 24/30/60 fps gives the same value per frame on every run and every OS |
| M7 | Audio cost | per-voice MSEG cost ≤ today +10 % at 256 points; no new xruns in the audio bench |
| M8 | UI cost | panel open with 8 pages: canvas frame time +≤ 1 ms on the M2 8 GB |
| M9 | Old patches | the 3 MSEG patches open, play and look right (not bit-exact) |
| M10 | Coverage | each of the 7 owner points has a working test that drives it |

---

## 8. Methodology

### 8.1 One model: the upgraded MSEG node

```
            ┌──────────── MSEG node (on canvas, saved in .inf) ───────────┐
            │ source:  Points │ Expression │ (Record writes into Points)  │
  trigger ─►│ time:    Beats  │ Seconds    │ Frames                       │──► IModulator
            │ length, loop mode, loop start/end, grid                     │    Value01()
            │ points[≤256]: x, y, interp, handles, easing                 │    + VoiceSpec
            │ channel tag (X/Y/Z/…), colour                               │
            └──────────────────────────▲──────────────────────────────────┘
                                       │ edits (snapshot swap, undo step)
                         ┌─────────────┴─────────────┐
                         │   Curve Editor panel      │  owns nothing
                         │   page = one MSEG node    │
                         └───────────────────────────┘
```

Settled decisions:

| # | Decision | Principle |
|---|---|---|
| D1 | Page = one MSEG node; the panel stores no curve data | Panels edit nodes (Piano Roll); save/undo/bypass/agents/mod matrix work for free |
| D2 | Extend MSEG, keep its id; panel title "Curve Editor" | Extend over duplicate; old patches keep working |
| D3 | Points 32 → 256, fixed array | Recording and detailed motion need more; fixed size stays audio-safe |
| D4 | Per-point interpolation: Constant, Linear, Bend (today's), Bezier, Easing preset | One curve can mix stepped gates and smooth motion, like Blender |
| D5 | Bezier handles with Blender's types: Auto Clamped (default), Auto, Vector, Aligned, Free | The industry standard; Auto Clamped never overshoots, which is right for modulators |
| D6 | y stays 0..1 inside the curve; real units come from each binding's lo/hi | One curve, many targets with different ranges; the inspector shows the first target's units on the y axis |
| D7 | Time base Beats / Seconds / Frames per node; Frames uses the export fps | Music and motion in one model; frame-exact export |
| D8 | Expression is a source mode of the same node, with "Bake to points" | One node, three ways to make it; no separate expression node |
| D9 | Recording writes points into the node (thinned with RDP), never raw samples | Everything recorded can be edited; CV Recorder stays for raw takes |
| D10 | Channels: XYZ = three MSEG nodes created as a group, overlaid on one page | Keeps one `Value01` per node; the overlay gives the graph-editor view |
| D11 | Evaluation only in `ModShapes` | Main thread and per-voice kernel can't drift |

### 8.2 Panel layout (symmetric)

Bottom/top dock:

```
┌─ Curve Editor ──────────────────────────────────────────────────────── ⋯ ┐
│ [Filter sweep] [Cam X|Y|Z] [ƒ wobble] [● CC 74] [+]          Mod │ Motion │  page strip
├────┬──────────────────────────────────────────────────────────────┬─────┤
│ ↖  │ 1        2        3        4        5          ← TimeRuler    │Inspe│
│ ✎  │ ┌──────────────────────────────────────────────────────────┐ │ctor │
│ ∿  │ │            ●──╮                    loop ├────────┤        │ │     │
│ ╱  │ │          ╱    ╰──●               ●                        │ │Time │
│ ⌒  │ │        ●           ╲           ╱  ╲                       │ │Len  │
│ ⌫  │ │  ●───╯               ●───────●     ●──                    │ │Loop │
│ ⬚  │ └──────────────────────────────────────────────────────────┘ │Grid │
│ ⊹  │ value axis (units of first target)                           │Targ.│
│ ⌗  │                                                              │Lib  │
├────┴──────────────────────────────────────────────────────────────┴─────┤
│ snap ⌗  grid 1/16  ·  ▶ ● rec  ·  Assign ⊕  ·  3 targets                 │  footer
└─────────────────────────────────────────────────────────────────────────┘
  tool rail (left)            graph (centre)                    inspector (right)
```

Left/right dock: the same three columns turn into three rows (page strip top,
tool rail as a horizontal row, graph, inspector folded under the graph).
Mirror rule: the tool rail is always on the canvas side, the inspector on the
outer edge, so left and right docks are mirror images.

- Widths reuse Piano Roll's: icon rail 30 px, page chip 140 px, inspector 268 px, ruler 16 px.
- The **Mod / Motion** switch filters pages and sets new-page defaults (Beats + Bend vs Frames + Bezier + easing library). Same model, two front doors.
- Page chips show a kind glyph: curve, ƒ expression, ● recorded, XYZ group.
- Empty state: one EmptyState card, "New curve", with the library underneath.

### 8.3 Pages

| Action | Result |
|---|---|
| `+` | New MSEG node on the canvas next to the selection, opened as a page |
| Select an MSEG node on the canvas | Its page comes to the front (follow selection, like 3D View) |
| Close page | Hides the page; the node stays |
| Delete page (menu) | Deletes the node; one patch undo step |
| Drag a chip | Reorder; order saved with the patch |
| Ghost | Other pages' curves drawn faint behind the current one (toggle) |

### 8.4 Tools (icon rail, Lucide/Tabler glyphs, keys match Piano Roll where they overlap)

| Group | Tool | Key | Does |
|---|---|---|---|
| Pick | Select | A | Click / marquee points, drag to move, Shift adds |
| | Hand | H | Pan; wheel zooms time, Alt-wheel zooms value |
| Make | Pen | P | Click adds a point, drag pulls out bezier handles |
| | Draw | D | Freehand stroke → simplified points (tolerance in footer) |
| | Line | L | Drag a straight ramp across a range, replacing what's under it |
| | Stamp | S | Paint the chosen library shape across a range, N cycles |
| Edit | Bend | B | Drag a segment up/down to change its curvature |
| | Eraser | E | Rub to delete points |
| | Transform | T | Box around the selection: scale time/value, skew, flip, around a pivot |
| | Smooth brush | M | Rub to relax points |
| Change (commands, also in the right-click menu) | Reverse, Invert, Mirror (make symmetric), Simplify, Humanize (seeded), Quantise to grid, Duplicate to fill length, Normalise | | One undo step each |
| Keys | Interpolation | 1–5 | Constant / Linear / Bend / Bezier / Easing on the selection |
| | Handle type | Shift+1–5 | Auto Clamped / Auto / Vector / Aligned / Free |
| Snap | Snap toggle | ⌗ / G | Time to grid; Ctrl snaps value to 1/8 steps |

The ToolCursor badge shows the active tool at the pointer, as in Piano Roll.

### 8.5 Time, quantise and loop (point 6)

| Control | Options |
|---|---|
| Time base | Beats · Seconds · Frames |
| Length | 1/16 beat … 64 bars, or 0.01 s … 10 min, or 1 … 100 000 frames |
| Grid | Beats: the canonical `MusicTime::RateDivision` table (4 bars … 1/32, dotted, triplets, 1/64), never a local list; seconds 0.1/0.25/1; frames 1/5/10/fps |
| Play mode | One-shot (hold last value) · Loop · Ping-pong · Loop region (sustain, today's loopStart/End) |
| Start | Transport-synced · Free-running · Retrigger (trigger input, per voice when `voice` is on) |
| Keep section | Drag the loop brace, then **Crop** keeps only that part and sets the length to it |
| Speed | ×0.25 … ×4 time scale, without moving points |

### 8.6 Shape library (point 3)

| Shelf | Shapes |
|---|---|
| Waves | Sine, Triangle, Saw up, Saw down, Square, Pulse 25 %, Half-sine, Exp rise, Exp fall |
| Steps & rhythm | Gates 1/4, 1/8, 1/16, dotted 1/8, triplet, Euclid (k, n), Trance gate patterns |
| Envelopes | AR, ADSR, Swell, Pluck, Reverse swell |
| Musical moves | Riser, Drop, Sidechain pump, Wobble, Filter sweep, Tape stop, Fade in/out |
| Random | Smooth random, Stepped S&H (seeded), Drift |
| Motion easings | Penner set: In/Out/InOut × Sine, Quad, Cubic, Quart, Quint, Expo, Circ, Back, Elastic, Bounce (30); CSS `ease`, `ease-in`, `ease-out`, `ease-in-out`, `linear`; AE Easy Ease (33 % influence); Material standard / emphasized |
| Mine | User saves (stored as files in the library folder, same as other presets) |

Apply = replace the page, or stamp across a selection (Stamp tool).
Shapes are data files plus formulas in `ModShapes`, so agents and CLI can use them too.

### 8.7 Assign (point 4)

```
  [Assign ⊕] ──► canvas enters pick mode (same as Perf Matrix gPerfAssigning…)
        │         knobs glow; Esc cancels
        ▼
  click knob ──► Modulation binding (source = this MSEG, lo/hi = knob's range)
        │         stays armed while Shift is held → click more knobs
        ▼
  Targets list in inspector: name · lo · hi · polarity · remove
```

- Also: drag the page chip onto a knob.
- Fixes issue #48 along the way: a new binding always wires up.
- 3D params (camera orbit, transform rot/move, armature pose) are ordinary knobs, so they assign the same way.

### 8.8 Expression page (point 1)

- Field: `f(t)` with `t` = 0..1 along the length, plus `beat`, `sec`, `frame`, `phase` and every `ExprGlobals` name; the same parser as param expressions.
- The graph draws the formula live (sampled per pixel column); parse errors show in the field well, curve keeps its last good shape.
- Presets in the library carry formulas too, so a user can open a preset as an expression to learn.
- **Bake** → turns the formula into points (adaptive sampling + simplify), the page becomes a points page; one undo step.
- Audio: evaluated on the main thread into a 1024-point table each change; the per-voice kernel reads the table (audio-safe).

### 8.9 Record page (point 2)

| Control | Options |
|---|---|
| Source | Learn CC (next CC moved wins, shows "CC 74 · ch 1") · Any knob (next knob touched) · Wired input |
| Mode | Overwrite (whole take) · Touch (writes while moving, returns after) · Latch (writes from first move to stop) |
| Start | Now · Next bar · On transport play; count-in follows the transport |
| Length | Fixed (current length) or Auto (stop sets the length, rounded to bars) |
| Thinning | RDP tolerance (default keeps a 4-bar take ≤ 64 points), optional quantise on stop |
| Overdub | Record again over a range; outside the range stays |

Built on `MidiCCNode` learn and `GestureRecorder`'s trace; the output is points (D9).

### 8.10 Motion grade (point 7)

- Frames time base uses the export fps; the ruler shows frames and timecode.
- Bezier keys + handle types (D5); the easing interpolation uses the presets in §8.6 between two keys.
- XYZ groups (D10): `+ Motion → Position / Rotation / Scale` makes three linked pages coloured X red, Y green, Z blue (Blender's colours), auto-assigned to the picked 3D node's three params.
- Value axis shows the target's units (degrees, metres) from the binding range.
- One-shot hold by default in Motion mode, so a move ends and stays.
- Exactness proved by headless Blender A/B (M5): same keys, same handles, compare values at every frame.

### 8.11 The five things Naman judges

| Check | Answer |
|---|---|
| Params / pins | MSEG gains: source, time base, length, play mode, start, speed, channel; trigger input unchanged |
| UI/UX | Built only from existing design components; symmetric; four docks |
| Intuitiveness | DAW users find draw + record; motion users find keys + easings; Mod/Motion front doors |
| Works on all OS | Pure ImGui + core code; no OS branches |
| Overlap | Upgrades MSEG instead of a new node; uses existing bindings, expressions, CC learn, GestureRecorder |

---

## 9. Solution trajectories

| Trajectory | Pros | Cons | Verdict |
|---|---|---|---|
| **A. Panel edits an upgraded MSEG (chosen)** | No new node; old patches work; per-voice already works; mod matrix / undo / save free | MSEG encode grows; 256-point evaluation must stay cheap | **Chosen** |
| B. New "Curve" node + panel | Clean slate, can drop MSEG's old bend maths | Duplicates MSEG; two curve nodes for users to tell apart; violates extend-first | Rejected |
| C. Curves stored in the panel / patch side data (like Arrange) | Could also become arrangement automation | Not a node: no wires, bypass, agents; second modulation path | Rejected (reuse the model for Arrange later) |
| D. One node with many output channels | XYZ in one node | `IModulator` is single-output; touches every binding | Rejected → D10 groups |

What-ifs:

| What if… | Then |
|---|---|
| 256-point per-voice evaluation is too slow (M7 fails) | Cache the segment index per voice (curves are read left→right); fallback: bake to a 1024 table like expressions |
| Bezier vs Blender doesn't match to 1e-3 | Port Blender's exact handle solve (`BKE_fcurve` maths, re-implemented, not copied) |
| Users never open the Mod/Motion switch | Drop it; follow the selected target's type instead |
| Recording CC jitter makes too many points | Raise the default tolerance; add a smoothing pass before RDP |
| Side docks feel cramped | Inspector folds to a popover in side docks |
| Arrange automation is asked for later | The same MSEG curve model + panel graph widget become a lane renderer |

## 10. Benchmarking against the baseline

Filled in per block when it lands; each row links to §6 and §7.

| Metric | Baseline (§6) | Target (§7) | Result |
|---|---|---|---|
| M1 draw + assign | ~8 steps | ≤ 3 / ≤ 10 s | — |
| M2 preset → assigned | n/a | ≤ 2 clicks | — |
| M3 CC → editable | impossible | 1 arm, ≤ 64 pts | — |
| M4 expression live | no | same frame | — |
| M5 easing / Blender A/B | no easings | 1e-4 / 1e-3 | — |
| M6 frame exact | n/a | identical per run/OS | — |
| M7 audio cost | MSEG 32 pts | ≤ +10 % at 256 | — |
| M8 UI cost | — | +≤ 1 ms | — |
| M9 old patches | 3 patches | open/play/look right | — |
| M10 coverage | — | 7/7 points tested | — |

## 11. Critique

| Of | Critique | Answer in the plan |
|---|---|---|
| Problem | Low MSEG use may mean people don't want drawn curves, not that the editor is hidden | Both readings lead to the same test: M1/M2 make drawing cheap; usage after release says which was true |
| Problem | No user has asked for it (0 of 23 issues) | Value is in the AV-sync pitch and DAW-familiarity; it also upgrades hidden features (CV Recorder, GestureRecorder, expressions) |
| Scope | Four blocks is large for a small user base | Each block ships something usable alone; Block 1 already fixes the flow break |
| Solution | Keeping y in 0..1 may confuse motion users who want "90°" | D6: the axis shows the target's units; the data stays normalised |
| Solution | XYZ as three nodes clutters the canvas | Grouped and collapsed on the canvas as one stack; page shows them as one |
| Solution | Expression + Points + Record in one node makes MSEG's card busy | The card stays small (curve preview + source chip); the panel holds the depth |
| Results | M5 depends on Blender being installed | Blender A/B runs where Blender exists; formula checks (1e-4) run everywhere |

## 12. Next steps

1. **Naman approves or changes this proposal** (new panel rule). Nothing is built before that.
2. Block 1 on its own branch: ModShapes curve model + MSEG upgrade/encode → panel shell (4 docks, pages, rail, ruler, inspector) → Select/Hand/Pen/Draw/Line/Bend/Eraser → snap/grid/quantise → time & loop (§8.5) → Assign pick mode → help tables → tests (M1, M6, M7, M8, M9).
3. Block 2: library (§8.6), Stamp, Transform, Smooth, change commands, user presets → tests (M2, M5 formula half).
4. Block 3: Expression page (§8.8), Record page (§8.9) → tests (M3, M4).
5. Block 4: motion grade (§8.10) → Blender A/B (M5) → M10 coverage sweep.
6. After Block 4: fill §10, decide ship / iterate / abandon per block, then judge whether Arrange automation should reuse this.
