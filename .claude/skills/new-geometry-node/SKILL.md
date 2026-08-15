---
name: new-geometry-node
description: The standard procedure for adding a new 3D node (geometry source, mesh operator, or anything else that implements IGeometrySource) to Infinite — the IGeometrySource contract, the passthrough-field forwarding trap that has already caused a real bug (env light cache invalidation), mesh caching/revision rules, and the machine-checkable exit criterion. Use when implementing a new geometry primitive, mesh operator, or 3D utility node; when writing the prompt for a fresh session that will implement one; or when a 3D node's material/texture/mapping silently doesn't pass through a chain, its render freezes when an upstream param changes, or moving/transforming an upstream source has no visible effect downstream.
---

Paths are relative to the repo root (`/Users/namansoni/infinite`).

This is the 3D sibling of `new-audio-node` and `new-effect-node`. The
regression net for this category is `geometry-transform-sweep` — run it
after building, it's not a substitute for reading this first.

---

## 0. The shape: everything downstream of IGeometrySource is one interface

Unlike Effects (table-driven through `FilterDef`) or Audio (two-object
main/audio-thread split), every 3D node — primitive source, mesh operator,
point distributor, instancer — implements the same interface,
`IGeometrySource` (`src/nodes/Geometry3DNodes.h` ~94). A `Render3D` node
downstream doesn't know or care whether it's looking at a `GeometryNode`
primitive or five `GeometryOpNode`s chained together — it just calls
`GetMesh()`/`MeshRevision()`/`GetModelMatrix()`/etc. This is what makes an
86-file node system tractable, but it means **every field on the interface
is a contract you must honor**, not an optional override — a wrapper node
that forgets to forward one silently breaks every chain it's placed in,
not just its own behavior.

---

## 1. Read these before writing code

| File | Why |
|---|---|
| `src/nodes/Geometry3DNodes.h` (~94-330) | the full `IGeometrySource` interface — read every doc comment on every virtual, they explain the defaults and who's responsible for what |
| `src/nodes/GeometryOpNodes.h`/`.cpp` | the reference *wrapper* node — one class, ten-plus operators, shows exactly which fields get forwarded vs recomputed |
| `src/nodes/PointDistributionNodes.h` | reference for a node that also carries a point cloud (`GetPointCloud`/`PointCloudRevision`) alongside or instead of a mesh |
| `docs/plans/phase2-one-geometry-interface.md` | why this consolidated into one interface, if you want the history |
| `docs/plans/phase4-selection-as-input.md` | the `selectionOnly`/face-mask pattern, if your node needs to respect an upstream Select |

---

## 2. Pick the node's shape first

| Shape | Example | What it must get right |
|---|---|---|
| **Terminal source** (owns its own mesh) | `GeometryNode`, `ModelSourceNode`, `OceanNode` | `GetMesh()` builds it; `MeshRevision()` bumps only when the mesh content actually changed; no upstream to forward from — every `IGeometrySource` field returns a real default, not a forwarded one |
| **Wrapper / operator** (mesh in, mesh out) | `GeometryOpNode`, `MappingNode`, `WrapNode` | Must **forward, not default**, every passthrough field it doesn't intentionally change — see §3 |
| **Point-cloud / curve producer** | `ParticleSystemNode`, `PathNode`, `PointDistributionNodes` | Implements `GetPointCloud()`/`PointCloudRevision()` (and/or `GetCurve()`/`CurveStamp()`) in addition to or instead of a real mesh |
| **Consumer / terminal** (reads geometry, produces something else) | `Render3DNode`, `MeshToPointsNode` | No `IGeometrySource` to implement — just a `GeometryInputSlot`, and its own cache signature must include everything it reads off the upstream source (§4) |

---

## 3. The passthrough-forwarding trap — the most common way this goes wrong

A wrapper node (anything with an `input` field) inherits **every**
`IGeometrySource` virtual with a default implementation. Those defaults
exist so an old wrapper that predates a newer field (e.g.
`GetMaterialTexture`, added after `GetSurfaceTexture`) doesn't have to be
touched — but a *new* wrapper you're writing must consciously decide, field
by field, whether to forward the input's value or supply its own:

- `GetModelMatrix()` — forward unless your node genuinely repositions the
  mesh. `GeometryOpNode` forwards this for every operator except Transform,
  because "an operator changes the shape, not where it sits" — dropping
  this forward once made moving an upstream `Geometry` node (or animating
  it with a `Path`) have no visible effect on anything chained after the
  operator. This is exactly the bug class `geometry-transform-sweep`
  exists to catch.
- `GetMaterial()` / `GetSurfaceTexture()` / `GetMaterialTexture(map)` /
  `SurfaceTextureRevision()` — forward unless your node changes the
  material (see `GeometryOpNode`'s `inheritMaterial` flag for the
  user-facing toggle version of this).
- `GetMappingTransform()` — forward unless your node *is* a `MappingNode`.
- `PassthroughSource()` — return the input. `Render3DNode` walks this chain
  to find an `InstanceOnPoints` further upstream even through several
  wrapper nodes; returning `nullptr` here (the default, meaning "mesh
  originates here") on a node that isn't actually the origin breaks
  instancing through that wrapper.
- `GetInstanceGroupMatrix()` — only relevant if your wrapper can sit
  between an `InstanceOnPoints` and whatever draws it; see
  `GeometryOpNode::GetInstanceGroupMatrix()` for the one case (Transform)
  that needs to return something other than identity.
- `GetPointCloud()`/`GetCurve()` and their revisions — forward if your
  wrapper is generic (should pass a point cloud through untouched), leave
  as the `nullptr`/`0` default if your node only ever deals in meshes.

**When adding a new field to `IGeometrySource` in the future** (not just
when consuming an existing one): grep every class implementing the
interface and decide forward-vs-default for each, the same way this file
lists it — don't just add the virtual with a convenient default and move
on, that's how the env-light bug below happened on the consumer side of
this same principle.

---

## 4. Mesh caching and revision — the second most common trap

Terminal sources and operators both cache their built mesh and skip
rebuilding when nothing changed (`GeometryOpNode`'s comment: "a Subdivide
feeding an Array can otherwise re-run hundreds of thousands of triangles
every single frame"). The cache is a signature struct compared cook to
cook — same shape as `FilterNode`'s (see `new-effect-node` §3), same trap:

**Anything your `GetMesh()`/`CookIfNeeded()` reads that isn't in the cache
signature will silently freeze when it changes.** This already happened for
real: `Render3DNode`'s scene cache didn't invalidate when an `EnvironmentNode`
node's `intensity`/`rotation` changed, because those were read directly as
uniforms in `CookIfNeeded()` while the signature only tracked
`envInput.Revision()` (which bumps on texture re-upload, not on those two
scalar fields) — fixed in `08dd3ec` by folding `env->rotation`/
`env->intensity` into `SceneSignature` explicitly. `GeometryOpNode`'s
`spin` field has the same shape of fix already built in: it's a per-beat
rotation baked into the mesh cache, so `CurrentSignature()` has to fold in
the live beat count whenever `spin != 0`, or the baked rotation would
freeze at whatever beat the mesh happened to be built on.

The rule for any new node: **if `GetMesh()`/`CookIfNeeded()` reads a field,
that field (or something equivalent that changes exactly when it does)
must be in the signature compared to decide whether to rebuild.** `Revision()`
counters only cover upstream mesh/texture content — your own node's plain
fields need to be in the signature by hand.

---

## 5. The wiring checklist

1. **`src/nodes/XxxNode.h` / `.cpp`** (or add to an existing family header
   like `GeometryOpNodes.h` if it's another operator in that shared class,
   the same table-esque pattern `GeometryOpNode`'s `Op` enum uses — one
   class, many named registrations, see §6).
2. **`CMakeLists.txt`** — add the `.cpp` to `src/nodes/` (~line 96-107,
   next to `Geometry3DNodes.cpp`/`GeometryOpNodes.cpp`/
   `PointDistributionNodes.cpp`).
3. **`src/main.cpp` include** — with the other node headers.
4. **`RegisterNodes()`** — `REGISTER_NODE(XxxNode, Display Name, "3D")` for
   a standalone class, or a named loop like `GeometryOpNode`'s (`main.cpp`
   ~1918-1924) if several node names share one class via a `CreateFor(int)`
   pattern.
   **Category string must be one whitespace-free token** — same
   `Patch.cpp` `>>`-parsing trap as every other node family
   (`node <index> <category> <typeName>` on one line).
5. **Name collision check** — grep existing `REGISTER_NODE(..., "3D")`
   calls and `FilterDefs.cpp`/audio categories; `Noise`, `Curve`, `Curves`,
   `Shape`, `Pattern`, `Transform` are already claimed elsewhere (see
   `new-audio-node`'s collision table).
6. **`GeometryInputSlot(slot)`** — the one required override for a wrapper;
   `slot == 0 ? &input : nullptr` for a single-input node.
7. **`InputLabel(slot)`** — without it the pin is unlabelled.
8. **`IGeometrySource` overrides** — work through §3's field list
   deliberately, not by leaving defaults.
9. **Viewport preview.** If the node is a pure point-cloud/curve source
   with no real mesh, it needs the `GL_POINTS`/curve preview path in
   `NodeViewport` (`src/core/NodeViewport.cpp`, added in `08dd3ec` for this
   exact case) rather than falling back to an empty mesh preview.
10. **Node help table** — `Geometry3DHelpText` (`main.cpp` ~10277); one
    sentence, existing voice.
11. **Params panel dispatch** if the node needs a non-generic layout (see
    `DrawGeometryOpParams` for the dropdown-plus-conditional-fields
    pattern operators use).

---

## 6. Bug traps, each of which has already happened here

- **Forgot to forward a passthrough field on a wrapper** (§3) — the single
  most common bug class in this node family; `geometry-transform-sweep`
  exists specifically to catch the transform/model-matrix instance of it,
  but it doesn't cover every field, so check the others by hand.
- **Cache signature omits a field the node's own `CookIfNeeded` reads**
  (§4) — same shape as the Effects-node caching trap, already caused the
  env-light bug for real.
- **Space in a category name corrupts the save file** (§5.4).
- **Deprecated enum indices reused or reordered.** `GeometryOpNode`'s `Op`
  enum keeps three deprecated `*Selected` values at their original indices
  purely so old saved patches' integer `op` fields keep meaning something
  — see the enum's own comment and `docs/plans/phase4-selection-as-input.md`
  before ever touching an existing operator's numeric position.
- **Instancing broken through a wrapper.** If `PassthroughSource()` isn't
  wired correctly, `Render3DNode` can't walk past your wrapper to find an
  upstream `InstanceOnPoints`, and instancing silently falls back to a
  single un-instanced copy.
- **Point-cloud-only source with no mesh preview.** Falls back to a blank
  viewport instead of the `GL_POINTS` path — check `NodeViewport.cpp`'s
  dispatch before assuming "no mesh" means "no preview is possible."

---

## 7. Tests — write them with the node, not after

- Confirm the node is picked up by `geometry-transform-sweep`
  (`.claude/skills/geometry-transform-sweep/driver.sh`) rather than writing
  a per-node version: it checks (1) moving/rotating/scaling an upstream
  source propagates through to final output, (2) a `Mapping` node's
  UV/offset/rotate/scale propagates the same way, (3) the revision/
  generation stamp doesn't change when nothing actually did.
- `INFINITE_ROUNDTRIPTEST` (registry-driven, `main.cpp` ~17083) picks up
  every registered node automatically for save/load and copy/paste
  round-tripping — no per-node work needed there.
- Manually verify the caching trap (§4): change every field your
  `CookIfNeeded` reads, one at a time, and confirm the render updates each
  time.
- Then run `/run-infinite-hygiene` before committing.

---

## 8. Exit criterion — state it machine-checkably in every prompt

A node is done when all of these hold:

1. It builds clean.
2. Spawned from the palette it shows the right pin(s) and label(s), and has
   a working viewport preview (mesh, point-cloud, or curve as appropriate).
3. `geometry-transform-sweep` passes for it — transform propagation,
   Mapping propagation (if applicable), and revision-stamp stability all
   check out.
4. Every `IGeometrySource` field it should forward, forwards correctly
   through a real chain (verify at least one: material, or instancing
   through it, or mapping).
5. Its params survive save → load → undo → copy/paste → delete unchanged.
6. Its own cache correctly invalidates on every field its `CookIfNeeded`
   reads — no stale/frozen render after a param change.
7. `/run-infinite-hygiene` passes.
8. `README.md`'s 3D node table and `ARCHITECTURE.md` (if relevant) are
   updated.

---

## 9. Prompt template for a fresh session

```
Implement the <NAME> node in Infinite (/Users/namansoni/infinite).

Shape: <terminal source | wrapper/operator | point-cloud producer |
consumer>. What it does: <...>. Params: <...>.

Follow .claude/skills/new-geometry-node/SKILL.md for the procedure. It's
prescriptive — do not re-derive it.

Two rules that override anything you infer:
1. If this is a wrapper (has a geometry input), go through every
   IGeometrySource virtual by hand and decide forward-vs-default —
   GetModelMatrix, GetMaterial/GetSurfaceTexture/GetMaterialTexture/
   SurfaceTextureRevision, GetMappingTransform, PassthroughSource,
   GetInstanceGroupMatrix, GetPointCloud/GetCurve. Silently defaulting one
   that should forward breaks every chain the node is placed in, not just
   this node — this already happened for real (see SKILL.md §3).
2. Anything CookIfNeeded()/GetMesh() reads that isn't already covered by an
   upstream Revision() must be folded into this node's own cache signature
   by hand, or the render will freeze on that field's changes instead of
   updating — this already happened for real (env-light cache bug, 08dd3ec,
   see SKILL.md §4).

Reference nodes: GeometryOpNode (the canonical wrapper, shows correct
forwarding for ~13 operators in one class), GeometryNode (canonical
terminal source).

Done when SKILL.md §8's eight criteria all hold. Report each one.
```
