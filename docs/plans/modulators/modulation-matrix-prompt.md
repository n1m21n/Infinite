# Implement the Modulation Matrix panel (Infinite)

Repo: `/Users/namansoni/infinite`. macOS ImGui + imgui-node-editor app. Everything
below was verified against the tree at the time of writing; line numbers are from
`src/main.cpp` (37,555 lines) and will drift — the function/symbol names won't.

## 0. Branch

The lo/hi modulation-range work this feature builds on is **uncommitted** on
`feature/mod-range-bounding` (`Modulation.{h,cpp}`, `Patch.{h,cpp}`,
`PatchJson.cpp`, `main.cpp`). `Modulation::Source::lo/hi`, `SetRange()` and
`ResolvedSourceFor()` — which this panel is built on — exist only in that
working tree, not in any commit. So:

```bash
git add -A && git commit -m "Bound modulation writes to a per-binding lo/hi range"
git checkout -b feature/modulation-matrix
```

Commit the range work on its own branch first, then branch off it. Do **not**
branch off `main` — you'd lose lo/hi and nothing below would compile.

## What you are building

A dockable "Modulation Matrix" panel: a spreadsheet-style table of every active
modulation binding, editable in place. It replaces nothing — the per-param
`##modbind` popup stays — it's the overview that popup can't be.

Columns (left to right):

| col | content | interaction |
|---|---|---|
| ● | enable dot | click toggles `Source::enabled` |
| Source | modulator node title + output index | click selects/frames that node on the canvas |
| Destination | destination node title | click selects/frames it |
| Parameter | `ParamRef::name` | read-only |
| Value | live current value of the destination param | read-only, updates per frame |
| Lo | `Source::lo` in destination units | `DragFloat`, editable |
| Hi | `Source::hi` in destination units | `DragFloat`, editable |
| ⇅ | invert | button, swaps lo/hi |
| ✕ | unbind | button |

## 1. `src/core/Modulation.h/.cpp` — three additions

**(a) `bool enabled = true;`** on `Modulation::Source`. Document it as: the
binding still exists and still shows in the matrix, it just stops being written
by the apply loop, leaving the destination param at whatever value it last held
(the param field stays read-only-locked, because the cable is still patched —
matching how `HasExpression` behaves under a live cable).

**(b) A durable param-metadata cache.** This is the one non-obvious problem in
the whole feature and you must solve it before drawing anything.

`Modulation::FrameParams()` is the only source of a destination's `name`,
`minValue`, `maxValue`, `step` and live `value` pointer — and it is rebuilt from
scratch every frame. Two things break a naive `for (ParamRef& r : FrameParams())`
lookup in the panel:

- **A node with `showParams == false` never registers its params at all.** The
  existing `##modbind` popup already hits this and degrades to
  `"(parameter not visible)"` (see the `destRef == nullptr` branch near
  `main.cpp:35673`). A matrix that dropped those rows would silently hide
  bindings, which is the opposite of the point.
- **`ClearFrameParams()` is called at `main.cpp:27752`, *before* the node editor
  runs.** A panel docked **top or left** draws at ~`main.cpp:28433`, i.e. after
  the clear but before any node has drawn — so from a top/left dock,
  `FrameParams()` is *empty*. A right/bottom-docked panel draws at ~`36450`,
  after the canvas, and sees a full list. If you don't fix this, the panel will
  work perfectly on the right and render an empty table on the top. Do not
  "solve" it by only allowing right/bottom docks.

Fix both with one mechanism in `Modulation`:

```cpp
// Sticky per-parameter metadata, accumulated from RegisterParam and never
// cleared per-frame. The matrix panel needs a destination's name/min/max/step
// even on frames where that param didn't draw - a collapsed node (showParams
// == false) registers nothing, and a top/left-docked panel draws before the
// canvas has registered anything at all this frame. Cleared only by Clear().
// The `value` pointer is deliberately NULLED in the stored copy: the raw
// float* is only valid within the frame that registered it.
const ParamRef* KnownParam(int nodeIndex, int paramIndex) const;
```

Populate it inside the existing `RegisterParam()` (copy the ref, set
`.value = nullptr`, overwrite the map entry), and wipe it in `Clear()`.

For the **Value** column, resolve the *live* `float*` from this frame's
`FrameParams()` when present and print `"--"` when it isn't — a collapsed node
genuinely has no live value to show, and a stale pointer is a use-after-free.
Never store or dereference a cached `value` pointer.

**(c) `void SetEnabled(int nodeIndex, int paramIndex, bool on);`** — no-op if
nothing is bound there, mirroring `SetRange()`'s shape (`Modulation.cpp`).

## 2. Persist `enabled`

- `src/core/Patch.h`: add `bool enabled = true;` to `ModRecord`, and extend the
  format comment at the top of the file. The `mod` line is already
  `mod <dstIndex> <dstParam> <srcIndex> <srcOutput> <polarity> <depth> <centre> [<lo> <hi>]`
  — append `[<enabled>]` as one more optional trailing int, exactly the way
  `lo`/`hi` were appended (see the existing comment block, which explains why
  `>>`'s failed-extraction leaves defaults intact on older patches).
- `src/core/Patch.cpp`: write it after lo/hi at ~line 222; read it at ~line 373
  after the `in >> m.lo >> m.hi` extraction. Because it's the *last* token, an
  old patch simply leaves it `true`, which is the correct legacy behaviour.
  **Only write the `enabled` token when lo/hi were also written** — the tokens
  are positional, so a line with `enabled` but no lo/hi would decode as lo.
  Simplest correct rule: write `enabled` inside the same `if (m.hasRange)`
  branch, and when `!enabled && !hasRange` force `hasRange` true first.
- `src/core/PatchJson.cpp:45`: add `{"enabled", m.enabled}` to the modulation
  object, and read it back with a presence check defaulting to `true`.
- Wherever `main.cpp` converts between `Patch::ModRecord` and
  `Modulation::Source` (grep `RestoreLink` and the save-side `ModRecord`
  construction), carry the field across in both directions. Missing either half
  is how "the toggle works until you save" happens.

## 3. Apply loop honours `enabled`

`main.cpp:~36789`, inside `for (const ParamRef& ref : modulation.FrameParams())`:

```cpp
const Modulation::Source src = modulation.ResolvedSourceFor(ref);
if (src.nodeIndex >= 0)
{
   if (!src.enabled)
      continue;   // binding intact, just not written this frame
   ...
```

`continue`, not a fallthrough to the expression branch — a disabled *cable*
must not silently hand control to a stored expression the user can't see.

## 4. Panel state + layout reservation (the part that has to be right)

The layout budget currently assumes at most two panels: the fixed-width module
browser (always right) and the viewport panel (one of four sides). You are adding
a third four-sided panel, so the ad-hoc reservation has to become a small
accumulator. Get this wrong and you get either overlapping panels or — because
the shell window is `NoScrollbar` on purpose (read the comment on its `Begin()`
at ~`main.cpp:27790`) — content clipped out of sight with no scrollbar to reveal it.

**New globals**, next to `gViewportPanelDock` at `main.cpp:588`:

```cpp
bool  gModMatrixOpen = false;
int   gModMatrixDock = 0;              // 0 = bottom, 1 = right, 2 = left, 3 = top
float gModMatrixWidth = 420.0f;
float gModMatrixHeight = 240.0f;
const float kModMatrixMinWidth = 300.0f;   // eight columns need real room
const float kModMatrixMinHeight = 140.0f;
```

**Rewrite the reservation block at `main.cpp:28378–28448`** into four
accumulators. Current code (read it first) computes `topBottomGap`,
`graphHeight`, then `rightReserved`. Generalise to:

```cpp
// Each top/bottom-docked panel is its own row, so each costs one extra
// ItemSpacing.y that a SameLine'd left/right dock never does. Undercounting
// this is exactly what grows the shell window's own scrollbar.
float topBottom = 0.0f;
int   rows = 0;
if (viewportTop || viewportBottom) { topBottom += gViewportPanelHeight; rows++; }
if (matrixTop  || matrixBottom)    { topBottom += gModMatrixHeight;     rows++; }
const float graphHeight = std::max(150.0f,
   ImGui::GetContentRegionAvail().y - topBottom - rows * ImGui::GetStyle().ItemSpacing.y);
```

and `rightReserved += gModMatrixWidth` when `gModMatrixOpen && gModMatrixDock == 1`.

Draw order, preserving the existing "top and left draw before the canvas so the
cursor and `gGraphScreenTL` already reflect them" rule (the comments at 28430 and
28455 explain why the minimap depends on this):

1. `if (viewportTop) DrawViewportPanelDocked(...)`
2. `if (matrixTop) DrawModMatrixDocked("##modmatrix_top", ImVec2(0, gModMatrixHeight));`
3. `if (viewportLeft) { ...; ImGui::SameLine(); }`
4. `if (matrixLeft) { DrawModMatrixDocked("##modmatrix_left", ImVec2(gModMatrixWidth, graphHeight)); ImGui::SameLine(); }`
5. capture `gGraphScreenTL`, `ed::Begin(...)`, canvas
6. after `ed::End()`: node browser (`SameLine`), viewport right (`SameLine`),
   then matrix right (`SameLine`) — the chain at `main.cpp:36341–36450`
7. `if (viewportBottom) ...` then `if (matrixBottom) DrawModMatrixDocked("##modmatrix_bottom", ImVec2(0, gModMatrixHeight));`

Also extend the **clamp block at `main.cpp:28398`** so `maxWidth` subtracts the
matrix's width too when it's left/right docked, and clamp
`gModMatrixWidth`/`gModMatrixHeight` against their own floors there. That block
runs unconditionally every frame on purpose — read its comment before touching it.

## 5. `DrawModMatrixDocked` — mirror `DrawViewportPanelDocked`

Copy the structure of `DrawViewportPanelDocked` (`main.cpp:16946–17021`)
verbatim in shape: outer borderless `BeginChild`, a 6px `InvisibleButton` grip on
the canvas-facing edge (`gripFirst` for docks 0/1, `vertical` for docks 1/2, sign
of the mouse delta flips per dock), an inner content child sized
`inner - kGrip - ItemSpacing`, and a single hairline `AddLine` on the
canvas-facing edge with the same `IM_COL32(70, 74, 90, 255)`. The comments there
explain each of those choices — keep them adapted rather than dropped.

**Right-click menu**: mirror `main.cpp:16916–16935` exactly, including the reason
it uses a manual mouse-rect test rather than `BeginPopupContextWindow` (nested
child windows swallow the window-hover test). Menu contents:
`Bottom / Right / Left / Top` radio items writing `gModMatrixDock`, separator,
`Close panel` → `gModMatrixOpen = false`.

## 6. Table body

`ImGui::BeginTable` is available (bundled ImGui is 1.90.9; `main.cpp:17514`
already uses it). Use:

```cpp
ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp
```

with `TableSetupScrollFreeze(0, 1)` so headers stay put.

Iterate `Modulation::Instance().Links()` (a `std::map<Key, Source>`, so rows come
out sorted by destination node then param — stable ordering for free). For each:

- `FindNodeByIndex(link.first.first)` / `(src.nodeIndex)` → `NodeTitle(*gn)`
  (`main.cpp:352`) for both name cells. Do **not** use `gn.typeName` — nodes whose
  title tracks a live dropdown (`GeometryOpNode`, `GeometryNode`, `ShapeNode`)
  would show the wrong name; `NodeTitle` exists precisely for that.
- If either node lookup returns null, skip the row (a stale link mid-delete).
- Param name / min / max / step from `KnownParam()`; fall back to
  `"param %d"` if even the sticky cache has never seen it.
- Live value from this frame's `FrameParams()` only, else `"--"`.
- Lo/Hi use `Modulation::ResolvedSourceFor()` when a `ParamRef` is in hand this
  frame (it performs the one-time legacy polarity→range conversion), and plain
  `ModulatorFor()` otherwise. Read the comments on both in `Modulation.h` — using
  the wrong one is how a legacy patch shows lo=0/hi=0 in the matrix.
- Format Lo/Hi as `%.0f` when `step > 0.0f` (integer destination), else `%.3f`;
  clamp to `[minValue, maxValue]` and `std::round` when integral before calling
  `SetRange` — the same shaping the `##modbind` popup does at `main.cpp:35755`.
- Give every widget in a row an id suffixed with the key
  (`ImGui::PushID(nodeIndex * 1000 + paramIndex)`) or every row after the first
  will share state.

Empty state: `ImGui::TextDisabled("No active modulations.")` plus one line
explaining that dragging a modulator's `out` pin onto a parameter's dot creates
one. An empty grid with headers reads as broken.

**Row actions**
- Enable dot: `SetEnabled(..., !src.enabled)`. Draw disabled rows with dimmed
  text so the state is legible without hunting for the dot.
- Invert: `SetRange(node, param, src.hi, src.lo)` — same as the popup's "Invert".
- Unbind: `PushUndoCheckpoint(); mod.Unbind(node, param);` — note the popup at
  `main.cpp:35780` checkpoints before unbinding; match that. **Break out of the
  loop immediately after** — you just mutated the map you're iterating.
- Selecting a node from a name cell: `gPendingSelect` is already the mechanism
  the canvas consumes right after `ed::Begin` (`main.cpp:~28470`); push the node
  id there rather than calling `ed::SelectNode` from outside the editor.

**Undo**: checkpoint on unbind and on enable-toggle. Do **not** checkpoint on
every `DragFloat` frame — follow whatever the existing lo/hi popup does (it
currently doesn't checkpoint range edits at all; matching that is fine and
consistent, and a per-frame checkpoint would flood the undo stack).

## 7. Opening it

- **Menu → new `SeparatorText("Modulation matrix")` section**, right after the
  existing "Viewport panel" section at `main.cpp:~27905`: a `Checkbox("Show
  modulation matrix", &gModMatrixOpen)`, a dock combo mirroring
  `ViewportPanelDockCombo()` (`main.cpp:16655`), and — matching that section's
  deliberate choice — expose only the axis the current dock actually reserves
  (`Width` slider for docks 1/2, `Height` for 0/3). A dead slider reads as broken.
- Optional but consistent: a toolbar toggle next to the existing `search` button
  at `main.cpp:28336`, which flips `gNodePanelOpen` the same way.
- Session-only state, like `gNodePanelOpen` and `gViewportPanelNodes` — do **not**
  serialize the panel's open/dock state into the patch or the undo stack.

## 8. Test fixture — `INFINITE_MODMATRIXTEST`

Mirror `INFINITE_MODBOUNDSTEST`, which lives in this same branch's diff and is
the closest template (spawn block at `main.cpp:27086`, assertions at `37020`,
driver entry `"MODBOUNDSTEST:30"` at `.claude/skills/run-infinite-hygiene/driver.sh:57`).

Add the spawn block (a `Shape` + an `LFO`, `showParams = true` on both — params
must draw to register), then per-frame assertions:

1. After binding `lfo → shape.sides`, `Modulation::Instance().Links()` has
   exactly one entry with the expected key.
2. `KnownParam(shapeIdx, sidesParam)` returns non-null with
   `name == "sides"`, and **still** returns non-null after setting
   `gNodes[shapeIdx].showParams = false` for a frame — this is the whole point
   of the sticky cache and the one thing a screenshot cannot verify.
3. `SetEnabled(..., false)` → after two frames the destination stops changing
   while the LFO keeps running (sample `shape->sides` twice, several frames
   apart, with the LFO rate high enough that an enabled binding would visibly
   move it). Re-enable → it moves again.
4. Save to a temp patch and reload: the disabled binding comes back disabled and
   with its lo/hi intact. Round-tripping `enabled` through *both* the text and
   JSON writers is the regression most likely to slip.

Print `OK` / `- BUG` per check exactly as `MODBOUNDSTEST` does — `driver.sh`
greps verdict text, it does not read exit codes. Add `"MODMATRIXTEST:40"` to the
`TESTS=(` array at `driver.sh:44`.

## 9. Build and verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Must compile clean. Then:

```bash
.claude/skills/run-infinite-hygiene/driver.sh
```

`MODMATRIXTEST` must pass, and nothing that passed before may start failing —
the script prints a "known baseline" list of pre-existing failures; only those
are acceptable. Then launch the app and confirm by eye, at **all four docks**,
that the matrix never overlaps the module browser or the viewport panel and that
the shell window never grows a scrollbar. Check the top dock specifically — that
is the configuration the `ClearFrameParams` ordering breaks.

Per the repo convention, copy the built `Infinite.app` to `~/Desktop` when done.

## Out of scope

- Don't touch the `##modbind` popup (`main.cpp:35648–35785`). It stays as the
  per-param path; the matrix is additive.
- Don't add typed-expression rows to the matrix. `Modulation::Expressions()` is a
  separate map with different semantics (a wired cable wins over an expression —
  see `Modulation.h`), and mixing both into one table needs its own design pass.
- Don't refactor the module browser's fixed `kNodePanelWidth` into a resizable
  dockable panel. Tempting while you're in the layout code, but it's a separate
  change with its own risk.
- Don't generalise `DrawViewportPanelDocked` and the new panel into one shared
  dock framework yet. Two call sites isn't enough signal for the right
  abstraction, and `main.cpp` has no UI-layer test coverage to catch a bad one.
