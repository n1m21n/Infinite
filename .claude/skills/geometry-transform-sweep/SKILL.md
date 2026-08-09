---
name: geometry-transform-sweep
description: Generic sweeps across every Infinite 3D node type that consumes an IGeometrySource, checking three invariants at once — that moving/rotating/scaling an upstream source propagates to a node's final output, that a Mapping node's UV/offset/rotate/scale propagates the same way, and that a node's revision/generation stamp doesn't change when nothing actually did. Use when asked to "check for transform bugs", "test node combinations", "sweep the geometry nodes", "does moving a source actually update the render", "why doesn't my Mapping node do anything", "why did my simulation reset", or after adding/touching any node with a geometry input.
---

Paths below are relative to the repo root (`/Users/namansoni/infinite`), not
this skill directory.

## Run this first

```bash
.claude/skills/geometry-transform-sweep/driver.sh
```

Add `--skip-build` to reuse the existing `build/` tree. Runs all three
sweeps below in one pass.

## What this catches

Three real bug classes found in this codebase, each generalized into a sweep
that covers every node type at once instead of a fixture per node someone
remembered to write:

1. **Dropped transform** (`TRANSFORMSWEEPTEST`) — a node reads
   `input->GetMesh()` and bakes it into its own cached mesh/points/simulation
   state, but never applies (or even checks) `input->GetModelMatrix()`.
   Result: moving/rotating/scaling the upstream source has **zero** visible
   effect on the render. `InstanceOnPointsNode`, `ClothNode`, and `PathNode`'s
   mesh-follow had exactly this bug before being fixed — see
   `src/main.cpp`'s `INFINITE_BUGTEST` block for the fixed-fixture regression
   checks on those specific cases.

2. **Dropped mapping** (`MAPPINGSWEEPTEST`) — a single-input node forwards
   every *other* side-channel from its input (material, textures, model
   matrix) but not `GetMappingTransform()`. Result: a Mapping node patched
   upstream has its space/translate/rotate/scale silently discarded before
   Render 3D ever sees it — the UI lets you wire it, nothing errors, it just
   does nothing. `ClothNode`, `MeshResynthNode`, and `MeshToPointsNode` had
   this bug; `JoinGeometryNode` had a milder version (always identity, no
   "which input" selection the way it already has for material).

3. **Spurious revision bump** (`REVISIONSWEEPTEST`) — a node bumps its
   `MeshRevision()`/generation stamp every cook regardless of whether its
   content actually changed. Result: any stateful node downstream (a
   simulation like Cloth) sees "the input changed" every single frame and
   resets itself continuously instead of ever settling — cloth patched
   downstream of a `DisplacementNode` with a *static* texture connected never
   drapes, it just snaps back to rest pose forever. `DisplacementNode` had
   this bug (`mTexGeneration` bumped unconditionally on every cook while a
   texture was connected, whether or not the pixels changed).

See `ARCHITECTURE.md`, "Node Library" → "Invariants for
`IGeometrySource`-consuming nodes" for the two rules #2 and #3 exist to
enforce, spelled out as ongoing rules for anyone adding a new node type.

## How it works

`src/main.cpp` (search `TRANSFORMSWEEPTEST`, `MAPPINGSWEEPTEST`,
`REVISIONSWEEPTEST`) defines a small fake `IGeometrySource` probe per sweep
that forwards everything to a real mesh except the one field under test, and
wires that probe into every node type that takes a geometry input. That
decouples "does this consumer correctly use its input's X" from needing to
know that input's own field names — the probe can stand in for *any*
producer without per-node wiring.

**Transform sweep**: sets the probe's `GetModelMatrix()` to Identity, cooks,
reads `MeshOps::Transform(node->GetMesh(), node->GetModelMatrix())`; sets it
to a translation that's a **different** amount per axis (5, 7, 3), re-cooks,
re-reads; asserts the shift matches exactly (so an axis swap or dropped
component fails too, not just a fully-ignored transform). `InstanceOnPointsNode`
and `PathNode`'s mesh-follow don't expose a plain `GetMesh()`+`GetModelMatrix()`
pair, so they get their own small variants reading `InstanceTransforms()[0]` /
`CurrentPoint()` instead.

Covered: `GeometryOpNode`, `DisplacementNode`, `MeshResynthNode`,
`MeshToPointsNode`, `Null3DNode`, `MaterialNode`, `MappingNode`,
`JoinGeometryNode`, `WrapNode`, `ClothNode`, `InstanceOnPointsNode` (both the
`pointSource` and `instanceShape` slots), `PathNode` (mesh-follow).

**Mapping sweep**: same shape, but the probe's `GetMappingTransform()` returns
a fixed, non-identity, non-uniform-per-field value (distinct space + distinct
translate/rotate/scale per axis) instead of forwarding the wrapped mesh's.
Cooks the node once and asserts `node->GetMappingTransform()` matches the
probe's value field-for-field. `MappingNode` itself is excluded on purpose —
it *sets* the mapping transform from its own params rather than forwarding
one, so the invariant doesn't apply to it.

Covered: the same list as the transform sweep, minus `MappingNode`,
`InstanceOnPointsNode`, and `PathNode` (the latter two don't reduce to a
plain `GetMappingTransform()` a caller would read, same reasoning as the
transform sweep's exclusions there).

**Revision sweep**: cooks a node twice in a row with nothing about its inputs
changed, and asserts its revision/generation-derived stamp is identical both
times. `DisplacementNode` gets a real `NoiseNode` with `speed = 0` (no
transport-time animation) connected to its texture input for this one — a
static, genuinely-connected texture is the specific case that caught the
original bug; testing without a texture connected at all would not have.
`ClothNode` is checked differently: its own mesh revision legitimately moves
every physics tick even while draping correctly, so instead the sweep checks
that its constraint count stays put across repeated cooks of an unchanging
input — confirming a static input doesn't force a topology rebuild by proxy.

Covered: `GeometryOpNode`, `DisplacementNode` (with a texture connected),
`MeshResynthNode`, `ClothNode` (constraint-count variant).

## Adding a new node type to a sweep

Any new node class that takes an `IGeometrySource*` (or `ICurveSource*`,
`IPointCloudSource*`, etc.) input should get a line added to the relevant
fixture(s), not a bespoke one-off test — that is the entire point of this
being a sweep rather than a fixture library.

- **Transform / mapping sweeps, plain `GetMesh()` + `GetModelMatrix()` node**
  (true for almost everything): add one line —
  `checkGeneric("NewNode", &node)` / `checkForwarding("NewNode", &node)` —
  after wiring `node.input = &probe`.
- **Transform / mapping sweeps, node with its own draw path** (instancing,
  path-follow): copy the shape of `checkInstancing` / the `PathNode` block.
- **Revision sweep**: cook the node twice with nothing changed and compare
  its stamp (`MeshRevision()` for most nodes); if the node has a
  texture/image input, connect a real, static (non-animated) source the same
  way `DisplacementNode`'s check does — testing with no texture connected
  cannot catch this bug class.

## Interpreting results

- `[pass]` — the checked invariant held for that node.
- `[FAIL]` — real bug. For the transform/mapping sweeps: this node drops (or
  partially drops) the checked side-channel — check whether it *bakes* input
  geometry into a cache (needs to apply/track the input's live value, since
  neither `GetModelMatrix()` nor `GetMappingTransform()` bumps a revision
  counter on its own) or *forwards* it (its own accessor should return
  `input->Get...()` rather than identity/an unrelated value). For the
  revision sweep: this node's dirty-check is keyed on something that changes
  without the node's actual output changing — look for an unconditional
  `Generation++`/`Revision =` inside a per-frame `CookIfNeeded`.
- `[SKIP]` — the fixture's generic probe mesh/mode didn't suit that node (for
  example, boundary-follow needs an open mesh, so a closed cube produces
  nothing to compare). This is a **gap in test coverage**, not a clean bill of
  health — if you see a new skip, either adjust the fixture's probe for that
  node or manually verify it in the editor and report what you find, the same
  way you'd report a manual combination that misbehaves.

## Manually trying combinations these sweeps do not cover

These sweeps only prove *pairwise* propagation (one producer's transform/
mapping into one consumer's output) and *single-node* revision stability.
They say nothing about:
- Deeper chains (source → op → op → op → render) — the pairwise property
  composes in principle, but the interaction between two or more working
  nodes is worth spot-checking on request, not something the harness proves.
- Whether the actual *pixels* Render 3D draws end up in the right place, not
  just the CPU-side mesh/matrix data.
- Node types outside `IGeometrySource`-style geometry (e.g. modulators other
  than `PathNode`, image/texture nodes).
- Revision stability of a genuinely animated source (Noise with `speed > 0`,
  a live simulation) — the sweep only proves the *static* case doesn't fake
  a change; an animated source is expected to bump its revision every frame
  it actually changes, and that path isn't separately asserted here.

If you build a patch by hand and something looks like it isn't updating live,
or a simulation resets when it shouldn't, that's a sweep blind spot — report
which two (or more) node types you chained together and what you moved or
changed, so a fixture can be added here for it.

## Gotchas

- Like `run-infinite-hygiene`'s suite, the verdict is a printf line, not an
  exit code from the app itself — `main()` always returns 0 regardless of
  test outcome. `driver.sh` greps for each sweep's own `... OK` / `... FAIL`
  marker.
- The transform sweep's injected translation (5, 7, 3) and the mapping
  sweep's injected space/translate/rotate/scale values are arbitrary but
  fixed — don't read anything into the specific numbers, they're just
  distinct-per-field so a swap or partial forward can't accidentally pass.
- This is a different, narrower harness than `run-infinite-hygiene` — it does
  not build/screenshot/eyeball rendering, it only checks CPU-side
  propagation and revision stability. `run-infinite-hygiene`'s suite now
  runs all three of these sweeps too (as `TRANSFORMSWEEPTEST`,
  `MAPPINGSWEEPTEST`, `REVISIONSWEEPTEST`), so a plain pre-commit hygiene run
  already covers this; use this skill directly when you want the sweeps in
  isolation or want to add a new node type to them.
