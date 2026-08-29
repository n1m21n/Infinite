# Performance Matrix — implementation plan

A dockable, resizable panel where a user assembles a custom performance
surface out of controls pulled from an already-built patch: knobs, faders,
sliders, toggles, XY pads and mixer strips, freely placed, renamed and
coloured. The patch stays the instrument; this panel is its front panel.

Everything below was verified against the tree at the time of writing.
Line numbers are from `src/main.cpp` (44,232 lines) unless stated otherwise.
**`ARCHITECTURE.md`'s Editor-UI line table is badly stale** (it stops around
line 9,124) — do not navigate by it; the citations here are current.

---

## 0. What already exists that this is built on

Read these before writing anything. The panel is mostly an assembly of
mechanisms that are already in the codebase; almost nothing here is new
infrastructure.

### 0.1 Parameters are addressed two different ways

This is the single most important fact for this feature, and it constrains
the whole element catalogue.

**Scheme A — `(nodeIndex, paramIndex)`, positional.** Every modulatable
float. `ModSlider` (1319), `ModKnob` (2101), `ModSliderInt` (1629),
`ModKnobInt` (2352) each do:

```cpp
const int nodeIndex  = gCurrentNodeIndex;
const int paramIndex = gParamCounter++;      // ordinal, by draw order
...
Modulation::Instance().RegisterParam(ref);   // ParamRef: value*, min, max, step, name
```

`paramIndex` is a **draw-order ordinal**, not a name. This is what
`Modulation` keys its bindings on, what a `mod` line in a patch file stores,
and therefore what the performance matrix must store too. It has a known
fragility — inserting a param into a node's draw function shifts every later
ordinal — which the codebase already lives with and works around explicitly
(see the reserved-ordinal trick at 11440-11460, where a knob hidden by a mode
switch still burns its ordinal so bindings don't silently repoint). **The
panel inherits exactly this fragility and no more.** Do not invent a second,
"safer" addressing scheme; a panel binding that survives differently from a
modulation binding on the same param is worse than one that breaks the same way.

**Scheme B — `name` string, via `ParamVisitor`.** `src/core/INode.h:79-87`.
This is what save/load uses (`Patch::SaveParams`/`LoadParams`), and it is the
*only* address a non-float param has. Bools (`MixerNode::mute[i]`), enums,
text and colours are visited by name and have **no `paramIndex` at all**.

Consequence: a knob/fader element addresses Scheme A. A toggle element must
address Scheme B. They are genuinely different bindings and the plan below
keeps them as separate record types rather than pretending otherwise.

### 0.2 The raw widgets are already panel-safe

`AudioSliderFloat` (1224), `VFaderFloat` (1774) and `KnobFloat` (1938) contain
**no `ed::` calls** — verified. They are pure ImGui + draw-list and can be
called from any window. `ModSlider`/`ModKnob` are the node-editor wrappers
that add the pin, the modulation branch, the typed-edit field and undo; those
call `ed::BeginPin` and **cannot** be used in the panel.

So: the panel calls the raw widgets directly, and re-implements only the thin
interaction layer around them.

`AudioKnobRow` (6053) is **not** reusable — it reads the canvas globals
`gAudioContentX`/`gAudioContentW` and calls `ModKnob` internally.

Signatures to call:

```cpp
bool KnobFloat  (const char* label, float* v, float lo, float hi, const char* fmt,
                 float diameter, ImU32 fillColor, bool readOnly, float cellW = 0.0f,
                 FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr,
                 bool hasRange = false, float rangeLo = 0.0f, float rangeHi = 0.0f);
bool VFaderFloat(/* same, `height` in place of `diameter` */);
bool AudioSliderFloat(const char* label, float* v, float lo, float hi, const char* fmt,
                      float width, ImU32 fillColor, bool readOnly);
```

The taper hooks matter: pass `ConsoleFaderTaper::PosToValue/ValueToPos` for a
dB range and `FrequencyTaper::` for a Hz range, so a fader in the panel feels
identical to the same fader on the node. `AudioWidgetStyle` (1669) is the
enum that records which.

### 0.3 Params are only writable during the frame they registered

`ParamRef::value` is a raw `float*` valid **only** within its registering
frame — `Modulation.h` says so twice, and `KnownParam` deliberately nulls the
pointer in its stored copy. Per-frame order in `main.cpp`:

| Where | What |
|---|---|
| 32826 | `ImGui::Begin("Infinite")` |
| 33545-33594 | dock flags + panel size budget |
| 33622-33635 | **top- and left-docked panels draw** |
| 33644 | `Modulation::ClearFrameParams()` |
| ~33660-42760 | canvas: `ed::Begin` → every node draws → params register |
| 42771-42789 | **right- and bottom-docked panels draw** |
| 43189-43294 | modulation + expression apply pass writes through `FrameParams()` |

A top/left-docked panel therefore has **no live pointers at all** when it
draws. The modulation matrix already hits this and solves the *display* half
with `Modulation::KnownParam()` — sticky per-param metadata (name, min, max,
step) that survives frames where the param didn't draw.

For the *write* half there is no existing precedent, so this plan adds one:
a **deferred write queue** (§3.2). This is not optional and not a nicety —
a panel that writes directly would work when docked right and silently do
nothing when docked top.

### 0.4 A collapsed node stops registering unless something is bound to it

39958:

```cpp
const bool registerOnlyParams = !isAudioBody && !gn.showParams &&
                                (gn.hasModulatedParams || gn.hasPaletteColors ||
                                 gn.hasExpressionParams);
```

This gate (shipped in `5c0efe0`, "Keep modulation live on a node with its
params collapsed") is why a modulator keeps driving a collapsed node. **A
performance-matrix binding is a fourth reason a collapsed node must keep
registering** — and collapsing every controlled node is exactly what a VJ
will do once the panel exists. Missing this produces the precise bug that
commit fixed: the control moves, nothing happens.

### 0.5 Modulation wins over everything

The apply pass (43231-43293) writes modulator → else expression, and
`ModSlider` locks its field read-only whenever a param is modulated. The
panel must follow the same precedence and the same visual rule: a panel
control on a modulated param is **read-only, and displays the live modulated
value**. Anything else fights the apply pass and loses one frame later.

### 0.6 The docked-panel pattern is fully worked out

`DrawModMatrixDocked` (19354-19430) is the template to copy: a 6px
`InvisibleButton` grip whose drag mutates the panel's width/height global, a
content child sized around it, and a single border line on the edge facing
the canvas. `DrawViewportPanelDocked` (18907) is the same structure. Both are
driven by globals declared at 214-221 and 672-687, dock-flag booleans at
33545-33548, a size budget at 33576-33594, and draw sites at 33622-33635 /
42771-42789.

### 0.7 Patch records degrade gracefully

`src/core/Patch.h:9-50` documents the line-based format; `Patch.cpp:418`
ends the read loop with *"Anything else is from a newer version and is
deliberately ignored."* A new `perf`/`perfui` tag is therefore forward- and
backward-safe with no versioning work.

Because undo/redo snapshots the same `Patch::Data` (`BuildPatchData` 20834,
`ApplyPatchData` 21242, `PushUndoCheckpoint` 21425), **putting the panel
layout in `Patch::Data` makes the whole panel undoable for free.** Do it that
way; do not invent a side-channel store.

---

## 1. The element catalogue — decided

The question asked was "what UI elements can be dragged". This is the answer,
and it is deliberately a closed set of **seven**. The repo's standing
guidance is that audio surfaces stay plugin-simple rather than exhaustive;
a performance matrix with twenty widget types is a worse instrument than one
with seven that all feel the same.

| # | Element | Binds to | Cell size | Notes |
|---|---|---|---|---|
| 1 | **Knob** | one float param (Scheme A) | 1×1 | `KnobFloat`; inherits the source param's taper |
| 2 | **Vertical fader** | one float param (Scheme A) | 1×2 | `VFaderFloat`; the default for anything in dB |
| 3 | **Horizontal slider** | one float param (Scheme A) | 2×1 | `AudioSliderFloat`; for wide/coarse controls |
| 4 | **Toggle** | one bool (Scheme B, by name) | 1×1 | mutes, bypass, enables |
| 5 | **XY pad** | two float params (Scheme A ×2) | 2×2 | reuse `DrawFxPad`'s interaction, not its state |
| 6 | **Mixer strip** | a `MixerNode` + channel index | 1×3 | composite: fader + mute + pan + meter |
| 7 | **Label** | nothing | N×1 | section headings; layout only |

Explicitly **not** in the set, with reasons:

- *Free-floating meter* — folded into the mixer strip, where a level readout
  actually belongs. A meter with nothing to meter is decoration.
- *Colour swatch* — colours are Scheme B and already have a palette-cable
  path; a performance matrix is not where you grade.
- *Dropdown / enum selector* — mode switches change a node's whole layout
  mid-set. Deliberately excluded; if one is genuinely needed later it is a
  separate decision, not an oversight.
- *Video thumbnail* — that is the viewport panel, which already exists and
  already docks. Do not duplicate it.

**Mixer strip is the only composite**, and it is worth the special case: an
8-channel mixer is the single most common thing a VJ wants on the surface,
and building it out of eight separate fader elements plus eight toggles is
eight times the drag work for a worse-aligned result. It binds to
`MixerNode` + channel index and internally reads `gainDb[i]` (Scheme A),
`mute[i]` (Scheme B), `pan[i]` (Scheme A) and `ChannelLevel(i)` (read-only) —
see `DrawMixerBody` (11082-11150) for the exact fields and for
`DrawStripMeter`'s signature.

---

## 2. Two design decisions that need to be settled before coding

### 2.1 Does a panel control write the param directly, or through a hidden modulator node?

**Model A — direct write (recommended, and what this plan specifies).**
The element stores `(nodeIndex, paramIndex)` and writes the param's value
straight through the deferred queue. No graph mutation, no nodes created, no
cables at all.

**Model B — each element is backed by a hidden `MacroKnobNode` with a real
modulation binding.** Reuses the entire modulation stack: per-binding range
(`lo`/`hi`), enable/disable, undo, save/load, visibility in the mod matrix.
The element could itself be modulated or automated.

Recommend **A**, for three reasons: it needs no graph mutation (so no node
churn, no index-renumbering hazard, no orphan cleanup on undo); it is the
literal answer to *"hide the cables"* — Model A creates no cables to hide;
and it is strictly less code. The cost is real and should be stated plainly:
a Model-A element **cannot itself be modulated or recorded**. If per-element
automation recording is wanted later, that is the moment to revisit Model B —
and the record format in §3.1 is designed so a `perf` record can gain a
`srcIndex` field later without breaking existing files.

### 2.2 What "hide the cables" means

Given Model A, panel bindings produce no cables, so the request is already
satisfied for the panel itself. What remains worth building is the *general*
affordance the request implies: a **canvas-wide cable-visibility toggle**, so
that once a set is built the graph can be read without spaghetti.

The insertion point is exact: the link-emit loop at 40569-40605. Every
`ed::Link()` call is inside it, split by cable class (modulation orange at
40574, audio blue at 40590, note green at 40596, image at 40603). Gate the
loop on a small bitmask global — modulation / audio+note / image — and expose
it in the View menu next to the existing panel toggles.

Keep this as **Phase 5**. It is genuinely independent of the panel and should
not be allowed to hold it up.

---

## 3. Data model

### 3.1 New patch records

Add to `src/core/Patch.h`, alongside `ModRecord`/`ExprRecord`, and document
them in the format comment at the top of the file the way every other record
is documented:

```cpp
// One control on the performance matrix. `kind` is PerfElement::Kind.
// dstIndex/dstParam address a modulatable float exactly as ModRecord does
// (positional paramIndex - see the format comment on "mod"). For a toggle,
// dstParam is unused and `boolName` carries the ParamVisitor name instead;
// for a mixer strip, dstParam carries the channel index. dstParam2 is used
// only by the XY pad, for its Y axis.
struct PerfRecord
{
   int   kind      = 0;
   int   dstIndex  = 0;
   int   dstParam  = 0;
   int   dstParam2 = -1;
   int   cellX = 0, cellY = 0;
   int   page  = 0;
   float colorR = 0.0f, colorG = 0.0f, colorB = 0.0f;
   std::string boolName;   // toggle only
   std::string label;      // empty = inherit the source param's own name
};
```

Plus `std::vector<PerfRecord> performance;` on `Patch::Data`, and a small
`PerfLayoutRecord` for panel-level state (grid cell size, page count, page
names) — one `perfui` line rather than a field on every element.

Write site: `Patch.cpp:204-240` (follow the `mod` writer's escaping
discipline — `label` is free-form and must be last on its line, escaped with
`EscapeLine`, exactly as `expr` does at 236). Read site: `Patch.cpp:355-418`,
as a new `else if (tag == "perf")` branch before the "ignore unknown" comment.

`src/core/PatchJson.cpp` is a 67-line write-only mirror; add a
`out["performance"]` array beside the existing `out["modulation"]` at line 43
so the remote-control/MCP surface stays complete.

### 3.2 The deferred write queue

New, in the anonymous namespace near the other panel globals (~687):

```cpp
// Values the performance matrix wants written into params this frame.
// Applied at the top of the modulation apply pass, BEFORE modulators and
// expressions, so a modulated param stays owned by its modulator (see the
// precedence rule in Modulation::SetExpression's comment).
// Keyed the same way modulation is: (nodeIndex, paramIndex).
std::map<std::pair<int,int>, float> gPerfPendingWrites;
```

Applied at 43189, immediately before the `paramSnapshot` loop, so the
snapshot an expression reads already reflects the performer's move:

```cpp
for (const ParamRef& ref : modulation.FrameParams())
{
   if (ref.value == nullptr) continue;
   auto it = gPerfPendingWrites.find({ref.nodeIndex, ref.paramIndex});
   if (it != gPerfPendingWrites.end())
      *ref.value = ShapeToParam(ref, it->second);
}
gPerfPendingWrites.clear();
```

`ShapeToParam` (30201) is mandatory, not optional — it is what snaps an
integer destination to its grid and clamps to range. Skipping it is how a
panel knob drives an int param to 3.7.

Scheme-B (toggle) writes cannot go through this queue — there is no
`ParamRef` for a bool. Apply those directly at the panel's draw site through
a resolved node pointer; a bool write has no ordering hazard because nothing
else in the frame contends for it.

---

## 4. Phases

Each phase should compile and be usable on its own. Do not merge them into
one change; §4.1 alone is a reviewable increment.

### Phase 1 — the empty dockable panel

Mirror the modulation matrix exactly.

1. Globals beside 680-687: `gPerfPanelOpen`, `gPerfPanelDock`,
   `gPerfPanelWidth/Height`, `kPerfPanelMinWidth/MinHeight`.
2. `PerfPanelDockCombo()` — copy `ModMatrixDockCombo` (18983).
3. `DrawPerfPanelDocked(const char* id, ImVec2 size)` — copy
   `DrawModMatrixDocked` (19354-19430) verbatim, swapping the globals. Keep
   its grip/border/`gripFirst` structure; it is load-bearing, and the
   fixed-height comment at 19393-19402 explains a real bug that was fixed
   there.
4. Layout budget: extend 33576-33594. Both existing panels subtract each
   other's reservation from their own max; a third panel means each of the
   three must now subtract the other two. **This arithmetic is the most
   error-prone part of Phase 1** — the comment at 33597-33610 explains that
   undercounting it grows the shell window's own scrollbar, which is the
   symptom to watch for.
5. Dock flags at 33545-33548; `topBottom`/`topBottomRows` at 33611-33614;
   `rightReserved` at 33649-33652; draw sites at 33622-33635 and 42771-42789.
6. View menu: a `Performance surface` submenu after the `Modulation matrix`
   block (32979-32997), copying its checkbox + dock combo + size slider shape.
   **Do not name the menu item "Performance"** — that name is already taken at
   32999 by the FPS/vsync menu.
7. Shortcut: `Shift+P`, beside the `Shift+M` matrix toggle at 40975. Verify
   `Shift+P` is unclaimed before wiring it.

### Phase 2 — elements, placement, the grid

1. `PerfElement` struct + `enum class Kind` mirroring `PerfRecord`.
   `std::vector<PerfElement> gPerfElements`.
2. A cell grid. Fixed cell size (start at 76×76 px, adjustable from the
   panel's own context menu), elements occupying the spans in §1's table.
   Grid beats free placement here: it is what a hardware controller looks
   like, it makes alignment automatic, and it makes reflow on panel resize
   well-defined.
3. Placement/drag: a manually-tracked mouse drag against cell rects, **not**
   `BeginDragDropSource`/`SetDragDropPayload`. The codebase has no ImGui
   drag-drop anywhere, and 8990-8991 records a deliberate decision to
   hand-track instead. Follow that.
4. Draw each element with the raw widget from §0.2, carrying the source
   param's `min`/`max`/`step`/`name` from `Modulation::KnownParam()` and its
   taper from the recorded `AudioWidgetStyle`.
5. Writes go to `gPerfPendingWrites`. Display value: prefer this frame's
   `FrameParams()` entry when present, else the last value the panel wrote,
   else `KnownParam`'s. A top/left dock will always be one frame stale — this
   is correct and matches what the matrix already does (19153-19163).
6. Read-only when `Modulation::IsModulated(nodeIndex, paramIndex)`, drawn in
   the same orange the pin uses (`IM_COL32(255,190,90,255)`, 1366) so it reads
   as "driven, not broken."

### Phase 3 — binding controls to params

This is the interaction that decides whether the feature is pleasant.

**Primary path — right-click a param on a node → "Add to performance
surface".** `DrawModulationBindingMenu` (1303-1312) already captures a
right-click on a param and defers the popup; extend that popup rather than
adding a second right-click path. It already has `nodeIndex`/`paramIndex` in
hand, which is exactly the binding.

**Secondary path — a picker inside the panel.** An "Add control" button
listing every param currently in `Modulation::KnownParam`, grouped by node,
filtered by a text field. This is the path that works when the node is
collapsed or off-screen, which during a set it usually is.

**Then, and only then, consider drag-out-of-node.** Dragging a live param off
a node and onto the panel is the nicest of the three and by far the most
fragile: the drag starts inside `ed::Begin`/`ed::End` where the node editor
owns the mouse, and ends outside it. Build it third, on top of two paths that
already work, and be willing to drop it.

**Registration gate (do not skip):** extend 39958 to
`|| gn.hasPerfPanelParams`, and recompute `hasPerfPanelParams` per frame
wherever `hasModulatedParams` is recomputed. Without this, every control
bound to a collapsed node silently stops working — §0.4.

### Phase 4 — naming, colour, pages

1. Per-element rename (double-click the caption; empty label falls back to the
   source param's own name) and colour. For colour, offer the current theme's
   category tints from `CategoryColors` (`src/core/CategoryColors.h`) rather
   than a raw picker — the repo's stated reason for a fixed palette
   ("a fixed vocabulary … means every preset looks deliberate rather than
   user-mixed") applies here at least as strongly.
2. Pages: a tab strip. `PerfRecord::page` already carries it. 4-8 pages is
   plenty; this is a performance matrix, not a document.
3. An **edit/perform mode toggle**. In perform mode elements cannot be moved,
   renamed or deleted, only operated. This is not polish — it is the
   difference between a surface you can use live and one where a slightly-off
   click during a set drags a fader across the panel.

### Phase 5 — cable visibility (independent)

Per §2.2: a bitmask global, the gate inside the link loop at 40569-40605, and
a View-menu entry. Ship it separately from the panel.

---

## 5. Quality requirements

- **Undo.** Every structural change — add, delete, move, rename, recolour —
  calls `PushUndoCheckpoint()` (21425) *before* mutating, matching the
  pattern at 11128 and 19177. Dragging an element calls it once at drag
  start, not per-frame; `gDragStartSnapshot` (21437) is the existing
  precedent for that.
- **Live value changes are not undo events.** Turning a knob during a set
  must not fill the undo stack — same as `ModSlider`, which checkpoints on
  edit-begin rather than per-frame.
- **Stale bindings never crash.** A deleted node leaves elements pointing at a
  dead `nodeIndex`. `FindNodeByIndex` returning null must draw the element
  greyed and inert, not skip it and not crash — the matrix's
  `dstNode == nullptr` guard at 19140 is the precedent. Do **not**
  auto-delete them: undo can rewind past a deletion, and an element silently
  removed does not come back.
- **Threading.** The panel is main-thread ImGui only, and writes params the
  same way the apply pass does. No new audio-thread contact of any kind.
- **Per-frame cost.** The panel draws every frame while open. Resolve
  bindings through `KnownParam` (a `std::map` lookup) — do not linear-scan
  `FrameParams()` per element per frame the way the matrix does at
  19153-19163; the matrix gets away with it because it draws a bounded
  handful of rows, and a performance matrix will not be bounded.
- **Both themes.** Every colour goes through `IsThemeLight()` like the
  widgets it wraps. The raw widgets already handle their own theming; the
  panel chrome around them must too.
- **Windows.** Nothing here touches `src/platform/`, so there is no
  platform work — but keep `_WIN32` out of it entirely, per the
  `windows-parity` skill's one-abstraction rule.

## 6. Verification

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Must compile clean, not merely "look right".

Then run the existing harness — this change touches patch save/load, undo and
the modulation apply pass, all three of which are covered:

```bash
.claude/skills/run-infinite-hygiene/driver.sh
```

`PATCHTEST` (round-trip) and `UNDOTEST` are the two that matter most; a
`perf` record that does not survive a round trip will show up there rather
than in a set.

Add one new fixture in the same style as the existing ones (they live
interleaved in `main.cpp` — see `RunGrainMolderFixture` at 25655 and the
modulation fixture at 43690-43730 for the frame-stepped shape). It should
assert, across frames:

1. a `PerfRecord` survives `BuildPatchData` → `Patch::Write` →
   `Patch::Read` → `ApplyPatchData` unchanged;
2. a panel write reaches the param — set a pending write, step a frame,
   confirm the destination float changed and was shaped by `ShapeToParam`;
3. **the collapsed-node case** — collapse a node that has a panel binding and
   no modulation, step a frame, confirm its params still appear in
   `FrameParams()`. This is the §0.4 regression and it is the one most likely
   to be broken by a later change.

## 7. Out of scope

Deliberately excluded, having been considered:

- **MIDI-learn on panel elements.** Obviously wanted for a live surface, and
  the natural next feature — but it is its own design (learn mode, CC
  storage, conflict handling) and would double this plan. `MidiCCNode`
  already exists as the interim path.
- **Automation recording / playback of panel moves.** Needs Model B (§2.1) or
  an equivalent, and should be designed with the transport rather than bolted
  on here.
- **Ableton Link, MIDI clock, NDI, clip launching.** Separate features from
  the same live-performance direction; none of them is a prerequisite.
- **Touching the viewport panel or modulation matrix.** The layout-budget
  arithmetic in Phase 1 necessarily edits shared lines at 33576-33594 — that
  is expected. Nothing else in either panel should change.
