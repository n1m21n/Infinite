# Infinite — Phase 5: make curves first-class

Three new nodes that turn the curve component from a dead end into something
you can route through. **All three are thin wrappers over `MeshOps` functions
that already exist** — this phase is mostly plumbing, not algorithms.

Verified against HEAD after `e3ae030` and `f47a942`.

## Why curves are currently a dead end

`Polyline` (`src/core/Mesh.h:263`) is a real component with real machinery
behind it, but almost nothing can reach it:

- **One producer:** `CurveNode` (`src/nodes/CurveNode.h:14`), the only
  `ICurveSource`.
- **One consumer:** `PathNode`.

Meanwhile these already exist in `MeshOps` and are **not exposed as nodes**:

| Function | Location | What it does |
|---|---|---|
| `BoundaryLoops(const Mesh&)` | `src/core/Mesh.h:345` | open boundary edges → closed polylines |
| `SliceContours(const Mesh&, int axis, float position)` | `src/core/Mesh.h:380` | plane cross-section → contours |
| `TubeAlong(const Polyline&, radius, sides, taper)` | `src/core/Mesh.h:341` | sweeps a curve into a tube mesh |
| `SamplePolyline(const Polyline&, t, outPos, outTangent)` | `src/core/Mesh.h:329` | arc-length sampling — constant speed regardless of control-point bunching |

`BoundaryLoops` and `SliceContours` are currently reachable **only** from inside
`PathNode` (`src/nodes/PathNode.cpp:65-75`), and `TubeAlong` only from inside
`CurveNode::GetMesh()`. Three genuinely useful operations are locked inside two
nodes. This phase lets them out.

## Node 1 — Mesh to Curve

Geometry in, curve out. Dropdown picks the extraction method, mapping directly
onto the two existing functions:

- **Boundary** → `MeshOps::BoundaryLoops(mesh)`
- **Plane slice** → `MeshOps::SliceContours(mesh, axis, position)` with `axis`
  and `position` params

Both return `std::vector<Polyline>`, but `ICurveSource::GetPolyline()` returns a
single `const Polyline&`. Resolve this explicitly — it is the one real design
decision in this node:

- Add a `loop index` param selecting which polyline to emit, **and** an
  `all` mode that concatenates. Concatenating disjoint loops into one polyline
  is wrong for anything that follows a path (a Path node would teleport between
  loops), so default to loop index 0 and document why.
- Mirror how `PathNode` already handles the multi-loop case
  (`src/nodes/PathNode.cpp:65-75`) rather than inventing a second convention —
  read that code first and match it.

## Node 2 — Curve to Mesh

Curve in, geometry out. Wraps `MeshOps::TubeAlong(line, radius, sides, taper)`.

`CurveNode::GetMesh()` already calls this, so the node is close to an extraction
of existing behaviour. Params: `radius`, `sides`, `taper`.

This is what finally makes curves productive: `Mesh to Curve → Curve to Mesh`
gives you pipes tracing a silhouette or a cross-section, which is a genuinely
new visual you cannot get today.

## Node 3 — Curve to Points

Curve in, point cloud out. Wraps `MeshOps::SamplePolyline` in a loop over N
evenly spaced `t` values, emitting `Particle`s.

`SamplePolyline` samples **by arc length**, so points are evenly spaced along
the curve rather than bunched where control points are dense — see the comment
at `src/core/Mesh.h:326-328`. That is exactly the right behaviour and is why
this node is three lines of real logic.

Emit `IPointCloudSource` (or the folded interface, if Phase 2 has landed), which
means `Render 3D` draws it as camera-facing sprites for free via the
`drawCloudSlot` path added in `f47a942`. Set `Particle.nx/ny/nz` from the
tangent that `SamplePolyline` returns in `outTangent` — that gives downstream
`Instance on Points` a meaningful orientation to align to.

## Plumbing every new node type needs

Per `ARCHITECTURE.md`, "Node Library" — missing any of these is why "I added the
class but it doesn't show up / doesn't connect / has no UI" happens:

- `RegisterNodes()` — with a sensible category (`"3D"`)
- `InputCountFor` — pin count
- `ConnectGeometrySlot` — geometry/curve pin wiring
- `QueryNewLink` — connection validation **and** its separate assignment block
- a `DrawXxxParams` function for the params panel
- the per-frame dispatch that routes to it
- `DisconnectAllTo` (`src/main.cpp:5255`) — **null the pointer out when an
  upstream node dies, or it crashes on the next cook.** If Phase 2 has landed
  this is automatic; if not, add the branch by hand.

Each node consumes an upstream source, so both invariants from
`ARCHITECTURE.md`'s "Invariants for `IGeometrySource`-consuming nodes" apply:

1. **Forward every side-channel you do not explicitly change** —
   `GetModelMatrix()`, `GetMaterial()`, `GetSurfaceTexture()`,
   `GetMaterialTexture()`, `SurfaceTextureRevision()`,
   `GetMappingTransform()`. This has silently broken three times.
2. **Only bump your revision stamp when your output actually changed** — not on
   every `CookIfNeeded`. A stamp that moves every cook makes any downstream
   stateful node (`ClothNode`) reset every frame instead of settling.

Cache each node's output on `(input pointer, upstream revision, own params)`,
mirroring `MeshToPointsNode::RebuildIfNeeded` (`src/nodes/UtilityNodes.cpp`) —
it is the closest existing template.

## Ordering and dependencies

- **Phase 2 first is preferable but not required.** Phase 2 folds
  `ICurveSource` into `IGeometrySource`; building these three nodes first means
  three more classes for Phase 2 to migrate. If Phase 2 has landed, these nodes
  expose `GetCurve()` / `GetPointCloud()` instead of separate interfaces.
- **Independent of Phase 3 and Phase 4.**
- Suggested split if you want smaller sessions: Node 1 + Node 2 together (they
  are the useful pair), then Node 3 separately.

## Out of scope

- **Points to Curves** (ordering a cloud into a chain by group id and weight).
  Needs a per-element attribute to sort by; deferred.
- **Fillet, trim, resample, reverse, subdivide curve** — Blender has 11 curve
  operators. Ship the three conversions first; operators only earn their place
  once curves are actually reachable.
- **Curve tilt, radius, handle types, cyclic flags.** `Polyline` is
  deliberately just points plus a `closed` flag.
- **Grease pencil / 2D fill.** No.
- **N-gon faces, area-weighted distribution, attributes.** Other phases.

## Done means

1. Builds clean:

       cmake --build build -j"$(sysctl -n hw.ncpu)"

2. Geometry sweep passes — all three nodes have geometry/curve inputs:

       .claude/skills/geometry-transform-sweep/driver.sh

3. Full hygiene suite passes, including the 163-node round trip, which every new
   node type must survive: `.claude/skills/run-infinite-hygiene/`
4. Verified by hand in the app:
   - `Sphere → Mesh to Curve (plane slice) → Curve to Mesh → Render 3D` — a ring
     of tube tracing the sphere's cross-section. Moving the slice position
     animates it. **This is the headline chain.**
   - `Plane → Mesh to Curve (boundary) → Curve to Mesh` — a frame around the
     plane's border.
   - `Curve → Curve to Points → Render 3D` — evenly spaced sprites along the
     curve, evenly spaced even where control points bunch.
   - `Curve → Curve to Points → Instance on Points` — instances oriented along
     the curve's tangent.
   - Deleting the upstream node in each chain does not crash on the next cook.
5. A patch containing all three new nodes saves, closes, and reloads with the
   links and params intact.
