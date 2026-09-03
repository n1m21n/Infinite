# Field build step 16 — "[Unpack to Canvas]" for FieldGraphNode

You are implementing **build step 16 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). Self-
contained brief; no prior context assumed beyond "Files to read first".
Line numbers are from `src/` at the commit this was written against
(`feature/field-step-14-dynamic-pins-graph-node` tip, commit `18ba50e`) —
re-grep the symbol if a number has drifted; the symbol wins.

**Prerequisite:** `feature/field-step-15-fieldgraph-encapsulation` merged,
and its exit criterion green. This step is meaningless without it — there
is nothing to "unpack" from a `FieldGraphNode` that spawns its children
directly onto the canvas already (today's/pre-step-15 behavior). Branch
`git checkout feature/field-step-15-fieldgraph-encapsulation && git checkout -b feature/field-step-16-fieldgraph-unpack-to-canvas`.

---

## 1. Goal

A `[Unpack to Canvas]` button in `FieldGraphNode`'s header/body. Clicking it:

1. Flips `encapsulated = false` (step 15 §3.1) for this instance — its
   mounted children stop being hidden.
2. Auto-wraps all of them into a newly spawned `GroupNode`, named after the
   script/field node.
3. Lays them out on a grid, no overlap, using topological depth — replacing
   `FieldGraphNode::Regenerate()`'s existing call-site-based auto-placement
   for this one operation only (§3).
4. Reconnects any outer cable that was plugged into a boundary pin (step 15
   §5) onto the corresponding newly-visible boundary node directly, so the
   patch keeps working with zero dangling cables.

This is a one-shot, one-way conversion for a given `FieldGraphNode`
instance: after unpacking, that node behaves exactly like it did before
step 15 existed (`MainGraphHost`-driven, children on canvas, call-site
auto-placement on future regenerates) — see §5 for what future regenerates
do post-unpack.

---

## 2. Files to read first

| File | Why |
|---|---|
| `docs/plans/field/step-15-fieldgraph-encapsulation.md`, whole doc | this step's prerequisite — `encapsulated`, `mMountedIndices`, `hiddenFromCanvas`/equivalent, `VirtualGraphHost`, and the boundary-pin derivation (§5 there) are all reused here as-is |
| `src/nodes/UtilityNodes.h` | `GroupNode` (`:53-81` at this doc's baseline — re-verify, step 15 doesn't touch this file) — a resizable backdrop with `label`/`width`/`height`/`color`; membership is **not** stored on the node itself, see next row |
| `src/main.cpp` | `gGroupMembers` (`std::map<GroupNode*, std::set<int>>`, `:489`) — the actual membership store, keyed by `GroupNode*` pointer, deliberately kept out of `GroupNode` itself because it's addressed by `GraphNode::index`, which `UtilityNodes.h` knows nothing about (comment at `:485-489`) |
| `src/main.cpp` | `AutoFitGroupToMembers` (`:20926`-ish, re-verify) — sizes a group's box to its members' bounding box every frame; this step spawns members first, then a `GroupNode`, and lets this existing per-frame fit take over sizing rather than computing a box size itself |
| `src/main.cpp` | the "wrap selection into a group" keyboard-shortcut handler (`grep -n 'SpawnNode("Group", "Compositing"' src/main.cpp`, the occurrence inside a selection-bounding-box block, not the test-fixture occurrences) — the exact existing pattern for "spawn a `GroupNode` sized/padded around a computed bounding box, then `gGroupMembers[grp] = picked`" this step's wrap step (§4) copies |
| `src/nodes/FieldGraphNode.cpp` | the auto-placement block inside `Regenerate()` (`:86-147`) — **do not reuse this for unpack** (§3 explains why: its constants assume the *narrow* per-call-site column case, not a full topological grid) but its `idByKey`/`callSiteX`/clone-index bookkeeping shape is the right model to adapt |
| `src/main.cpp` | `PerformCopyPaste` (`:27687`-ish) — the existing pattern for "spawn N new nodes, remap an index-keyed structure (`newIndexByOrig`), rewire cluster links via `CaptureClusterLinks`/`ApplyClusterLinks`" — §4.3's boundary-cable reconnection is structurally the same problem (old logical target -> new real node) and should follow this shape rather than inventing a new remap idiom |
| `src/core/field/FieldGraphHost.h` | `IFieldGraphHost` — this step's unpack host is a **third** implementation (`MainGraphHost`, `VirtualGraphHost` from step 15, and this step's transient one) — per step 15 §8 trap 4, still don't share a base class, copy again |

---

## 3. Auto-layout — topological depth, not call-site order; owner's spacing numbers corrected

### 3.1 Why call-site order (the existing mechanism) is the wrong fit here

`FieldGraphNode::Regenerate()`'s existing auto-placement
(`FieldGraphNode.cpp:86-147`) lays out **unpositioned** nodes left-to-right
by call-site and top-to-bottom by clone index within a call site — it does
not look at `connect()` edges at all, so two nodes from different call
sites that are wired in series can land side-by-side instead of in
dependency order. That's an acceptable default for the *ordinary,
still-encapsulated or never-encapsulated* case (cheap, deterministic,
already shipped, untouched by step 15) but is exactly the "no overlap, no
spaghetti" requirement's opposite for a deliberate "make this readable"
unpack operation. Build a second, topological version for this step; do not
generalize the existing one to also do topological layout — the existing
one has callers (every ordinary `Regenerate()`, step 15's `VirtualGraphHost`
path included) that must keep their current behavior unchanged.

### 3.2 Topological depth from `GraphPlan::connects`

`plan.connects` (`GraphPlan::connects`, `FieldGraphKernel.h:61`) is a flat
`std::vector<ConnectSpec>`, each with `srcKey`/`dstKey`. Build a depth map:

```cpp
// depth[key] = 0 for a key that is never a dstKey (a root/source);
// otherwise 1 + max(depth[src] for every connect where key is dstKey).
// Cycles cannot occur here: emit()'s handles are only ever bound once
// (step 10's design - a handle name can't be reassigned to reference
// itself), and connect() only wires already-bound handles, so this is a
// DAG by construction - no cycle-breaking logic needed, unlike the
// audio-chain topological sort main.cpp already has elsewhere (main.cpp
// :4628, :4667 - that one needs cycle handling because live audio cables
// can form a loop; a graph-domain emit/connect plan cannot).
std::map<std::string, int> ComputeEmitDepths(const Field::GraphPlan& plan);
```

Confirmed no reusable topological-sort utility exists elsewhere in this
codebase for this shape of problem (`codebase-navigation` living map:
`AudioEngine.h`'s topological ordering is audio-thread-specific, keyed by
pooled buffer index, not by a `std::string` key — not reusable here without
more adaptation than writing the ~15-line depth walk directly). Write it
local to this step, in `FieldGraphKernel.cpp` or a step-16-specific helper —
your call on which file, but it belongs near `InterpretGraphProgram`, not in
`main.cpp`, since it's pure data (no `gNodes`/`ed::` dependency), matching
the existing separation between `FieldGraphKernel.h` (pure) and
`FieldGraphHost.h`/`main.cpp` (real graph).

### 3.3 Column/row spacing — the owner's 320/160 numbers do not fit this codebase's node widths; use depth-scaled spacing instead

**Judgment call, flag for owner sign-off:** the brief's suggested
`Δx=320px` column spacing is narrower than a single audio node's own width
in this codebase — `FieldGraphNode.cpp:105-106`'s existing constants
(`kAutoPlaceDX = 580`, derived from `kAudioNodeWidth (440) + margin`;
`kAutoPlaceDXWide = 1080`, from `kAudioWideWidth (960) + margin` for
Wavetable/Drum Sequencer) already had to solve exactly this problem and
landed on spacing proportional to real rendered node width, not a flat
constant. A flat 320px column would visually overlap any ordinary
audio-node-width column and every wide node entirely. **Recommendation:
reuse `FieldGraphNode.cpp`'s existing width classification (`isWide`,
`:113-115`) and its existing `kAutoPlaceDX`/`kAutoPlaceDXWide` constants for
column spacing** (by topological depth instead of call-site index — same
numbers, different axis-key), and keep the owner's `Δy=160px` **only** as a
floor — use `std::max(160.0f, <node's rendered height> + margin)` per row,
since row height also needs to fit whatever the tallest node at that depth
actually renders at (an audio-wide node is far taller than 160px once
drawn). State this correction explicitly to the owner rather than silently
substituting numbers — the 320/160 figures may have been written without
checking this codebase's actual node widths, and shipping them as literally
specified would produce visibly overlapping nodes on the very first
non-trivial unpack.

```cpp
constexpr float kUnpackOriginX = 60.0f;
constexpr float kUnpackOriginY = 60.0f;
constexpr float kUnpackDX = 580.0f;       // = FieldGraphNode.cpp's kAutoPlaceDX
constexpr float kUnpackDXWide = 1080.0f;  // = kAutoPlaceDXWide
constexpr float kUnpackDYMin = 160.0f;    // owner's number, as a floor
constexpr float kUnpackRowMargin = 40.0f;
```

Row `y` for the `n`-th node at a given depth: accumulate actual node height
(query `ed::GetNodeSize` **one frame after** spawning — codebase-navigation
living map's documented gotcha: a freshly spawned node's `ed::` size reads
stale/zero on the same frame it's spawned, so this step's layout pass must
be a two-frame operation, not one: frame 1 spawns everything at a
provisional position, frame 2 reads real sizes and repositions via
`host.Place`) plus `kUnpackRowMargin`, falling back to
`kUnpackDYMin` for the first node at a depth (nothing to measure yet).

### 3.4 No overlap, no spaghetti — what this actually guarantees

"No overlap": guaranteed within a depth column (rows stack by measured
height, §3.3) and across columns (column x-spacing exceeds every node's own
rendered width, same guarantee `FieldGraphNode.cpp`'s existing constants
already provide for the call-site case). "No spaghetti": guaranteed only in
the sense that a cable's source is always in a strictly lower-numbered
column than its destination (topological depth order) — this does not
prevent a long cable jumping several columns, and does not do any
crossing-minimization within a row (e.g. ordering nodes at the same depth
to reduce edge crossings, a real graph-drawing problem this step
deliberately does not solve). State this limitation in the exit criterion
rather than overclaiming "no spaghetti" as a hard guarantee — it is a
left-to-right dependency-order guarantee, not a crossing-minimized layout.

---

## 4. The unpack operation, step by step

### 4.1 Precondition and undo

Button only enabled when `encapsulated == true` and `mMountedIndices` is
non-empty (an empty/never-regenerated graph has nothing to unpack — grey
out or hide the button). Wrapped in exactly one `PushUndoCheckpoint()` +
`gSuppressUndoCheckpoints` pair, same discipline as
`RunFieldGraphRegenerate` (step 15 §3.3) and `PerformCopyPaste`
(`main.cpp:27733-27734`) already use for a multi-mutation operation that
must undo as one step.

### 4.2 Reveal, layout, group

1. Set `fgn->encapsulated = false`.
2. Clear `hiddenFromCanvas` (step 15 §3.2's flag) on every index in
   `fgn->mMountedIndices`.
3. Run §3's topological-depth layout, two-frame as described (§3.3) —
   this step's own per-frame tick (parallel to step 15's
   `gFieldGraphPendingRegenerate` drain, likely a new
   `gFieldGraphPendingUnpack` flag drained the same place, after
   `ed::End()`, for the same trap-T14 reason: this mutates `gNodes`
   indirectly via `host.Place` and directly via the `GroupNode` spawn
   below).
4. Spawn a `GroupNode` (`SpawnNode("Group", "Compositing", x, y)`, same call
   shape as the existing selection-wrap handler,
   `main.cpp`'s `SpawnNode("Group", "Compositing", gx, gy)` occurrence
   inside the selection-bounding-box block) at the unpacked cluster's
   computed bounding box (union of every member's post-layout position/size
   — same bounding-box accumulation shape as that same handler already
   does, `bmin`/`bmax` over `ed::GetNodePosition`/`ed::GetNodeSize`).
5. `label = fgn->code`-derived name — **not** the raw source text (too
   long/unreadable as a group title). Use the `FieldGraphNode`'s own
   `GraphNode::category`-adjacent display name if this codebase already
   has one (check whether `FieldGraphNode` instances carry a user-assigned
   name/title anywhere, e.g. a rename-in-place field like `GroupNode`'s own
   `renaming`/`renameJustStarted`, or the generic node-title system every
   node already has) and reuse that; otherwise default to `"Unpacked: " +
   fgn->Uid().substr(0, 8)` — short, unique, not pretty, and say so plainly
   rather than inventing a code-parsing name-extraction heuristic that will
   misfire on real scripts.
6. `gGroupMembers[grp] = fgn->mMountedIndices` — direct set assignment,
   exactly like the selection-wrap handler's `gGroupMembers[grp] = picked`.
   `AutoFitGroupToMembers` (already runs every frame, step 15 §2's file-read
   list) takes over sizing from the very next frame; this step does not
   need to compute the group's final width/height itself, only its
   spawn-time position (padding around the just-computed bounding box, same
   `kPad`/`kHeader` constants the existing selection-wrap handler already
   uses — reuse them, don't invent new padding numbers).

### 4.3 Boundary cable reconnection

Per step 15 §5.4, an outer cable plugged into `FieldGraphNode`'s derived
boundary pin currently resolves against a terminal `INode*` re-resolved via
`mOwnership` every frame — the cable's actual graph-level source is already
that terminal's mounted node, step 15 never routed it through
`FieldGraphNode` itself as an intermediary hop. **This means unpacking
requires no cable rewiring at all for the already-correct case**: the
outer cable's `ImageCable`/`AudioCable`/`NoteCable` source pointer already
*is* the terminal `INode*`, and that `INode*` does not change identity or
address when `hiddenFromCanvas` clears — only its visibility does. Verify
this against step 15's actual implementation before assuming it: if step 15
instead resolves boundary output through some `FieldGraphNode`-owned
forwarding pin (which §5.4 as written does not describe — it describes
direct terminal resolution), reconnection work is needed here; if step 15
shipped as written, **§4.3 in that case is a no-op, and this step's exit
criterion (§7) should assert exactly that — zero cables detached, zero
reconnected, because none needed to move.** State the finding either way in
the PR/summary for this step rather than assuming it silently.

Boundary **input** pins (`param`-backed, step 15 §4) similarly need no
reconnection: `PushLiveParams`/`Regenerate()`'s `host.SetParam` calls
already write into the same mounted child field a direct cable-to-the-
child's-own-param would target once visible — unpacking does not change
which `INode`/field a modulation cable plugged into `FieldGraphNode`'s
own param pin ultimately reaches, because it never routed through the
child's own `ParamRef` in the first place (step 15 §4.2's stated gap). This
step does not fix that gap either — flag again, don't silently absorb it
into this step's scope.

---

## 5. What happens on the next `Regenerate()` after unpacking

`fgn->encapsulated` is now `false`, so per step 15 §3.3, the next
`Regenerate()` on this instance uses `MainGraphHost`, not
`VirtualGraphHost` — mounts/updates/unmounts go straight onto the visible
canvas, exactly as `Regenerate()` already behaves today (pre-step-15). Any
**newly** emitted node (the script grew a new `emit()` since unpacking)
lands via `FieldGraphNode.cpp`'s existing call-site auto-placement
(§3.1's "unchanged for the ordinary case"), not this step's topological
layout — topological layout is a one-shot unpack-time operation, not a
standing behavior. A newly emitted node is **not** automatically added to
the `GroupNode` spawned in §4.2 (group membership is purely geometric,
`AutoFitGroupToMembers`, step 15's living-map entry doesn't change this) —
if it lands inside the group's current bounding box, it's adopted next
frame by the existing auto-fit logic; if it lands elsewhere (a new call
site, positioned by the unrelated call-site auto-placement math), it does
not join the group. This is existing `GroupNode` behavior, not a gap this
step needs to close — note it in the exit criterion as expected, not
surprising.

---

## 6. Open question — dotted/dashed modulation-cable style, investigated not decided

**The owner's idea:** visually distinguish a `FieldGraphNode` param's
modulation cable from an ordinary data cable with a dashed/dotted line
style.

**Investigated against the actual vendored library**
(`external/imgui-node-editor/imgui_node_editor.h`): `ed::Link(LinkId,
PinId, PinId, ImColor color, float thickness)` takes exactly those four
arguments — no line-pattern/dash parameter exists anywhere in the public
API. `Style` (`imgui_node_editor.h:196`+) exposes `LinkStrength` (bezier
curvature) and a `StyleColor_Flow`/`StyleColor_FlowMarker` pair plus
`Flow(LinkId, FlowDirection)` (`:334`) — an animated particle traveling
along an existing link, already built into the library, zero custom
drawing required.

**Verdict for the owner to choose between, not decided here:**

| Option | Feasibility | Cost |
|---|---|---|
| True dashed/dotted line | **blocked on custom drawing** — the library's `Link()` always renders a solid bezier; achieving a dash pattern means bypassing `ed::Link` for these specific links and hand-drawing a dashed bezier via `ImDrawList` at the pin positions `ed::` itself would have used, which means duplicating (or extracting) the library's own pin-to-pin bezier math — non-trivial, and risks visually diverging from every other cable's curve shape if the duplication drifts | high — new custom draw path, ongoing maintenance to keep visually consistent with the library's own link rendering |
| Distinct color only | **trivial, already-supported** — `ed::Link`'s existing `color` argument (already varied per-cable-kind elsewhere in this codebase, e.g. `main.cpp:54299`+'s `ImColor(c.r, c.g, c.b, 1.0f)` per-link color) already does this; no library change needed | near-zero |
| Animated flow marker (`ed::Flow`) for modulation cables specifically | **feasible, native, cosmetic** — call `ed::Flow(linkId)` once when a param-modulation cable is drawn, distinct from an ordinary data cable which never calls it; reads as "this cable is actively carrying a live signal," arguably a better fit for "modulation" specifically than a static dash pattern would be | low — one call per qualifying link, uses the library's existing particle animation |

This doc does not pick one — surface this table to the owner as this step's
open question, exactly as instructed. If the owner picks either of the
bottom two, it's a same-step addition (a handful of lines at the existing
cable-draw call sites, `main.cpp:54299` and its siblings, gating color/`Flow`
on "is this link's source pin one of `FieldGraphNode`'s derived param
pins"). If the owner picks true dashing, split it into its own step — it is
not "a handful of lines."

---

## 7. Machine-checkable exit criterion

```bash
cd /Users/namansoni/infinte
cmake --build build -j"$(sysctl -n hw.ncpu)"

for v in FIELDGRAPHTEST FIELDGRAPHENCAPTEST FIELDGRAPHLIVEPARAMTEST FIELDGRAPHUNPACKTEST; do
  env INFINITE_$v=1 ./build/Infinite.app/Contents/MacOS/Infinite | tee /tmp/$v.log | tail -5
  grep -c FAIL /tmp/$v.log
done

.claude/skills/cable-logic-sweep/driver.sh
.claude/skills/modulation-sweep/driver.sh
.claude/skills/run-infinite-hygiene/driver.sh
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

`INFINITE_FIELDGRAPHUNPACKTEST` (new) must assert:

1. an encapsulated `FieldGraphNode` with N mounted children, after
   `[Unpack to Canvas]`: `encapsulated == false`; every one of the N
   children has `hiddenFromCanvas == false`; exactly one new `GroupNode`
   exists whose `gGroupMembers` set equals the N children's indices exactly;
2. no two of the N children's post-layout bounding boxes overlap (query
   `ed::GetNodePosition`/`ed::GetNodeSize` two frames after the unpack
   operation, per §3.3's two-frame requirement);
3. a node at topological depth 2 sits at a strictly greater x than every
   node at depth 0 or 1 it depends on (transitively, via `plan.connects`);
4. an outer cable connected to a boundary output pin before unpacking is
   still connected, to the same `INode*`, after unpacking — zero detached
   count (§4.3's expected no-op, asserted rather than assumed);
5. undo after unpack restores exactly the pre-unpack state in one step —
   `encapsulated` back to `true`, the spawned `GroupNode` gone, every
   child's `hiddenFromCanvas` back to `true`;
6. a `FieldGraphNode` with `mMountedIndices` empty (never regenerated)
   cannot have `[Unpack to Canvas]` invoked (button disabled, or the
   operation is a documented no-op if driven programmatically by the test).

---

## 8. Traps

| # | Trap | The bug it prevents |
|---|---|---|
| 1 | Reusing `FieldGraphNode.cpp`'s existing call-site auto-placement math for the unpack layout | wrong axis (call-site order, not dependency order) and wrong intent (that code runs on every ordinary `Regenerate()`, this step's layout is a one-shot operation) — §3.1 |
| 2 | Shipping the owner's literal `Δx=320/Δy=160` without correction | overlaps this codebase's actual node widths (440-960px) — §3.3 states the correction and why |
| 3 | Reading `ed::GetNodePosition`/`ed::GetNodeSize` the same frame a node is spawned | stale/zero per the codebase-navigation living map's documented gotcha — layout must be a two-frame operation (§3.3) |
| 4 | Computing the spawned `GroupNode`'s width/height directly instead of letting `AutoFitGroupToMembers` size it from `gGroupMembers` | fights the existing per-frame auto-fit system, which will immediately override any explicit size on the next frame anyway — set position only, not size (§4.2) |
| 5 | Assuming boundary cables need explicit reconnection logic | per §4.3, they likely don't — verify against step 15's actual shipped implementation before writing reconnection code that may be entirely unnecessary |
| 6 | Building true dashed-line cable rendering without owner sign-off | high-cost, custom-draw-path decision the owner hasn't made yet — §6 is an open question, not a mandate |
| 7 | Making a newly emitted node (post-unpack, next `Regenerate()`) automatically join the unpack's `GroupNode` | not how `GroupNode` membership works anywhere else in this codebase (purely geometric, §5) — don't special-case `FieldGraphNode`'s spawned group to behave differently from every other group |
