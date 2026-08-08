---
name: geometry-transform-sweep
description: Generic sweep across every Infinite 3D node type that consumes an IGeometrySource, checking that moving/rotating/scaling an upstream source actually propagates to that node's final world-space output. Use when asked to "check for transform bugs", "test node combinations", "sweep the geometry nodes", "does moving a source actually update the render", or after adding/touching any node with a geometry input.
---

Paths below are relative to the repo root (`/Users/namansoni/infinite`), not
this skill directory.

## Run this first

```bash
.claude/skills/geometry-transform-sweep/driver.sh
```

Add `--skip-build` to reuse the existing `build/` tree.

## What this catches

This exists because of a real bug class found in this codebase: a node reads
`input->GetMesh()` and bakes it into its own cached mesh/points/simulation
state, but never applies (or even checks) `input->GetModelMatrix()`. The
result: moving, rotating, or scaling the upstream source has **zero** visible
effect on the render, because the transform was silently dropped between the
source and whatever baked its geometry.

Four node types had exactly this bug (`InstanceOnPointsNode`, `ClothNode`,
`PathNode`'s mesh-follow) before being fixed — see `src/main.cpp`'s
`INFINITE_BUGTEST` block for the fixed-fixture regression checks on those
specific cases. This sweep is the generalization: instead of one hand-written
fixture per node type someone remembered to check, it mechanically wires a
probe into **every** node type that takes a geometry input and checks the
same invariant on all of them at once — including any new node type added
after this was written, automatically.

## How it works

`src/main.cpp` (search `TRANSFORMSWEEPTEST`) defines a `TransformProbeSource`
— a fake `IGeometrySource` that forwards `GetMesh()`/`GetMaterial()`/etc. to a
real mesh, but returns an arbitrary matrix from `GetModelMatrix()`. That
decouples "does this consumer correctly use its input's transform" from
needing to know that input's own field names (`posX` vs no transform at all)
— the probe can stand in for *any* producer without per-node wiring.

For each consumer node type, the fixture:
1. Sets the probe's matrix to Identity, cooks the node, reads its world-space
   output (`MeshOps::Transform(node->GetMesh(), node->GetModelMatrix())`).
2. Sets the probe's matrix to a translation that is a **different** amount on
   each axis (5, 7, 3) — not just "moved", so an axis swap or a dropped
   component fails too, not just a fully-ignored transform.
3. Re-cooks and re-reads the world-space output.
4. Asserts the shift matches the injected translation exactly.

`InstanceOnPointsNode` and `PathNode`'s mesh-follow don't expose a plain
`GetMesh()`+`GetModelMatrix()` pair (instancing uses per-instance transforms;
path-follow emits a modulator position), so they get their own small
variants of the same check reading `InstanceTransforms()[0]` /
`CurrentPoint()` instead — see the fixture for the exact shape.

Covered today: `GeometryOpNode`, `DisplacementNode`, `MeshResynthNode`,
`MeshToPointsNode`, `Null3DNode`, `MaterialNode`, `MappingNode`,
`JoinGeometryNode`, `ClothNode`, `InstanceOnPointsNode` (both the
`pointSource` and `instanceShape` slots), `PathNode` (mesh-follow).

## Adding a new node type to the sweep

Any new node class that takes an `IGeometrySource*` (or `ICurveSource*`,
`IPointCloudSource*`, etc.) input should get a line added to this fixture,
not a bespoke one-off test — that is the entire point of this being a sweep
rather than a fixture library. Two cases:

- **It exposes a plain `GetMesh()` + `GetModelMatrix()`** (true for almost
  everything): add one line — `checkGeneric("NewNode", &node)` — after
  wiring `node.input = &probe` (or whatever its input field is named).
- **It has its own draw path that doesn't reduce to mesh+matrix** (like
  instancing or path-follow): copy the shape of `checkInstancing` /
  the `PathNode` block — inject the probe, flip its matrix, read whatever
  that node's *actual* placement output is, and assert the same
  distinct-per-axis delta.

## Interpreting results

- `[pass]` — that node's final world-space output moved exactly as expected.
- `[FAIL]` — real bug: this node drops (or partially drops) an upstream
  transform. Check whether it *bakes* input geometry into a cache (needs to
  apply `input->GetModelMatrix()` before baking, and include that matrix in
  its own dirty-check — `GetModelMatrix()` is a live value, not something a
  revision counter bumps on its own) or *forwards* it (its own
  `GetModelMatrix()` should return `input->GetModelMatrix()` rather than
  identity or an unrelated matrix).
- `[SKIP]` — the fixture's generic probe mesh/mode didn't suit that node (for
  example, boundary-follow needs an open mesh, so a closed cube produces
  nothing to compare). This is a **gap in test coverage**, not a clean bill of
  health — if you see a new skip, either adjust the fixture's probe for that
  node or manually verify it in the editor and report what you find, the same
  way you'd report a manual combination that misbehaves.

## Manually trying combinations this sweep does not cover

This sweep only proves *pairwise* propagation (one producer's transform into
one consumer's output). It says nothing about:
- Deeper chains (source → op → op → op → render) — the pairwise property
  composes in principle, but the interaction between two working nodes is
  worth spot-checking on request, not something the harness proves.
- Whether the actual *pixels* Render 3D draws end up in the right place, not
  just the CPU-side mesh/matrix data.
- Node types outside `IGeometrySource`-style geometry (e.g. modulators other
  than `PathNode`, image/texture nodes).

If you build a patch by hand and something looks like it isn't updating live,
that's the sweep's blind spot — report which two (or more) node types you
chained together and what you moved, so a fixture can be added here for it.

## Gotchas

- Like `run-infinite-hygiene`'s suite, the verdict is a printf line, not an
  exit code from the app itself — `main()` always returns 0 regardless of
  test outcome. `driver.sh` greps `TRANSFORM SWEEP OK` / `FAIL`.
- The probe's injected translation (5, 7, 3) is arbitrary but fixed — don't
  read anything into the specific numbers, they're just distinct-per-axis so
  an axis swap can't accidentally pass.
- This is a different, narrower harness than `run-infinite-hygiene` — it does
  not build/screenshot/eyeball rendering, it only checks CPU-side transform
  propagation. Run both before a commit that touches any geometry node.
