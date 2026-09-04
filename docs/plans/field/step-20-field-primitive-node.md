# Step 20: Field Primitive node (pure geometry generation)

> **Handoff note:** this doc is written to be executable by an AI agent with
> no other context than this repository. It cites real file:line locations
> confirmed by direct investigation as of commit `58acad2` on branch
> `bugfix/field-device-ui-and-cable-fixes`. If line numbers have since
> drifted, re-locate by the named function/symbol, not the raw number.
>
> This is workstream 2 of 3 in a related set — see also
> [`step-19-sample-delay-line.md`](step-19-sample-delay-line.md) and
> [`step-21-field-synth-node.md`](step-21-field-synth-node.md). This one has
> no dependency on the other two and is the smallest/lowest-risk of the
> three — a reasonable step to implement first.

## Context

Field currently has "Field Modifier" (`FieldElementNode`) for geometry
modification. The user wants a separate **Field Primitive** node dedicated to
pure from-scratch geometry generation — no input pin, invented presets for
basic generation primitives (circles, spirals, grids, etc.) rather than
displacement of existing geometry.

## Why this is a small, mostly-mechanical extraction

`FieldElementNode` already contains a complete, working no-input generator
fallback mode — it's just undocumented and hidden behind "no mesh plugged
into `input`" rather than being its own node type. It lives entirely in
`CookIfNeeded()` (`src/nodes/FieldElementNode.cpp:223-284`): when
`input == nullptr`, it synthesizes a degenerate mesh of `generateCount`
(default 64) bare vertices along X in `[-1,1]`, no indices/faces/UVs — a pure
scaffold that `i`/`count`/`P` presets already write into via the normal
per-element kernel. Everything else in the node — `Apply()`, `VisitParams`,
the pin/`PinTable` machinery, preset loading — is domain-agnostic and already
shared identically between "modifier" and "generator" use.

Confirmed: the reserved element-domain identifiers `i` (per-element index,
read-only) and `count` (total element count, read-only) resolve identically
in `FieldIR.cpp`/`ElementBackend.cpp` whether the source mesh is real
upstream geometry or this synthesized scaffold mesh (both driven off
`Field::ElementStore::Count()`). This means primitive-generation presets
using `i`/`count`/`P` (e.g. a circular point layout, a spiral, a grid)
already work through the existing generator branch, completely unmodified —
this is proven-working logic, not new territory.

## Design: new node type, not a mode flag on Field Modifier

Create `FieldPrimitiveNode` (new `.cpp`/`.h` pair) as a genuinely separate
node type — do not keep it as a hidden mode of Field Modifier. Structure it
after `DistributePointsInGridNode`
(`src/nodes/PointDistributionNodes.h:227-282`) — the established codebase
template for "pure generator, no input pin, builds own vertex buffer from
params": no `input` member, no `GeometryInputSlot`/`BypassSource`
passthrough, self-contained `RebuildIfNeeded()` memoized on its own params
rather than upstream revision.

Concretely: `FieldPrimitiveNode` should own the exact generator logic
currently in `FieldElementNode::CookIfNeeded()`'s `input == nullptr` branch
(the synthesis of the base scaffold mesh, parameterized by `generateCount` or
similar), plus the shared kernel-compile/`Apply()`/pin machinery it needs
(most of this can be copied from `FieldElementNode`, since the kernel-running
part is identical — both compile the same element-domain language and run
the same `ElementBackend.cpp` VM).

Once `FieldPrimitiveNode` exists as its own type, **remove the no-input
fallback from `FieldElementNode`** — Field Modifier should require an input
mesh going forward, since it's a modifier, not a generator. **This removal
is an open decision** — the alternative is leaving Field Modifier's fallback
mode as dead-but-harmless code for backward compatibility with any existing
patches that rely on it running with no input connected. If there's any
uncertainty, ask the user before removing rather than silently dropping
behavior an existing patch might depend on.

## New presets to invent for Field Primitive

Design presets that make sense for *generation from nothing*, not
*modification of existing geometry* — e.g.:

- A circle/ring of points (`P` set from `i`/`count` via `cos`/`sin`)
- A spiral
- A grid/lattice pattern
- A Fibonacci/golden-angle point distribution
- A simple parametric surface (torus, helix)

These should live in `FieldPrimitiveNode::Presets()`, written fresh — none of
the existing Field Modifier presets (which assume an input mesh to displace)
transfer directly. The kernel language itself is identical to Field
Modifier's, so the general guidance on frame-domain outputs, `publish`, and
`P`/`N`/`Cd` semantics in
[`.claude/skills/field-modifier-presets/SKILL.md`](../../../.claude/skills/field-modifier-presets/SKILL.md)
still applies. Once this ships, consider authoring a
`field-primitive-presets` skill mirroring the existing
`field-pixel-presets`/`field-modifier-presets` skills.

## `main.cpp` sites a brand-new Field node type must be added to

Confirmed via direct investigation, these are the hand-maintained dispatch
points every existing Field node type appears in — a new type needs an entry
at each. Reference existing registrations for the pattern:
`main.cpp:3987` (`FieldPixelNode`/"FieldPixel"/"Source"), `:4056`
(`FieldElementNode`/"Field Modifier"/"3D"), `:4144` (`FieldSampleNode`/
"Field Effect"/"AudioEffects"), `:4149` (`FieldGraphNode`/"Field Graph"/
"Utility", denylisted from spawn via `IsUserSpawnable()` at `:421-429` — an
established precedent for "loadable but not spawnable" if ever needed, not
relevant here since Field Primitive should be spawnable).

- `REGISTER_NODE(FieldPrimitiveNode, "Field Primitive", "3D")` — via the
  `REGISTER_NODE` macro (`src/core/NodeFactory.h:50-51`).
- Node-body draw dispatch (~`main.cpp:57562`) — add a case for the new type.
- A new `DrawFieldPrimitiveParams` function analogous to
  `DrawFieldElementParams` (~`main.cpp:8649-8720`). Note:
  `DrawFieldDeviceControls<NodeT>` (~`main.cpp:5521-5524`) is already generic
  and needs **no** changes.
- Post-load `Apply()` dispatch (~`main.cpp:5286-5287`).
- `.infdev` drag-and-drop domain-string dispatch (~`main.cpp:47705-47808`,
  keyed on `Field::DeviceFile::domain` strings like `"element"`/`"pixel"`/
  `"sample"`/`"graph"`/`"formula"`) — the new node needs its **own** domain
  string (e.g. `"primitive"`) to avoid drop-target ambiguity with Field
  Modifier's `"element"`.
- Drop-target lookup list (`FindNodeUnderCanvasPoint<T>`,
  ~`main.cpp:47705-47708`).

## Files to touch

- New: `src/nodes/FieldPrimitiveNode.h`/`.cpp` (largely adapted from
  `FieldElementNode.h`/`.cpp`'s generator-mode code path + the shared
  kernel-compile/pin machinery).
- `src/nodes/FieldElementNode.cpp/.h` — remove (or explicitly keep, per the
  open decision above) the no-input fallback branch once Field Primitive
  exists.
- `src/main.cpp` — all dispatch sites listed above.
- `CMakeLists.txt` — add the new `.cpp` to the build (check how the other
  Field node `.cpp` files are listed there for the pattern to follow).

## Verification

1. Build (`cmake --build build -j 8`).
2. Spawn a new Field Primitive node — there is no input pin at all, so this
   is its only mode. Confirm each new preset renders a visibly correct shape
   (circle, spiral, grid, etc.), not the flat degenerate 64-points-on-a-line
   scaffold.
3. Confirm dragging a `.infdev` primitive-domain device file onto the canvas
   creates a Field Primitive node, not colliding with Field Modifier's drop
   handling.
4. Confirm an old patch containing a Field Modifier node with nothing wired
   to `input` still loads and behaves according to whatever was decided for
   the fallback-removal question above.
5. Deploy to `~/Desktop/Infinite.app` after a successful build and manual
   check, per this project's existing convention.
