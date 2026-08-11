# Infinite — Phase 6: real point distribution

Four point-domain nodes, of which one is the algorithm that most changes how
scattered geometry looks, and three are cheap.

Verified against HEAD after `e3ae030` (Phase 0) and `f47a942` (Phase 1).

## Node 1 — Distribute Points on Faces (the one that matters)

### The problem

`MeshOps::ToPoints` (`src/core/Mesh.cpp:1894`) samples by **index stride**:

    const int stride = std::max(1, n / cap + (n > cap ? 1 : 0));

That walks the vertex/edge/face array in index order. It is *not* a spatial
distribution — dense regions of the mesh get proportionally more points. On a UV
sphere, where `Primitives::Sphere` bunches its rings toward the poles
(`src/core/Mesh.cpp:135-148`), this produces visible dark caps at both poles.
Blender's `Distribute Points on Faces` instead works in **points per square
metre**, so coverage is uniform regardless of topology.

### The fix

A new `MeshOps` function — this is the one genuinely new algorithm in the phase:

    // Area-weighted scatter. `density` is points per square unit of surface
    // area, so coverage is uniform regardless of how the mesh is tessellated -
    // unlike ToPoints, which samples in index order and therefore clumps
    // wherever the topology is dense.
    std::vector<MeshPoint> DistributeOnFaces(const Mesh& in, float density,
                                             float seed, int method,
                                             float minDistance);

- **Random** — build a prefix sum of triangle areas, pick a triangle by binary
  search on a uniform random draw, then a uniform point inside it via the
  standard sqrt barycentric warp. Honour `faceMask` via `Mesh::FaceSelected`,
  matching what `ToPoints` already does at `src/core/Mesh.cpp:1901`.
- **Poisson disk** — generate as Random, then reject any point within
  `minDistance` of an accepted one, using a spatial hash grid (cell size
  `minDistance`) so rejection is O(1) per candidate rather than O(n). Blender
  caps density by `minDistance`: once the minimum spacing is saturated, no
  further points are added no matter how high the density. Match that.

Interpolate the normal across the triangle from its three vertex normals, and
normalise — do not just copy the face normal, or a smooth-shaded surface will
scatter with faceted orientation.

**Keep `ToPoints` and its index-stride mode exactly as it is.** Topology-revealing
clumping is sometimes the desired effect, and `MeshToPointsNode` and
`InstanceOnPointsNode` both depend on the current behaviour. This is a new node,
not a replacement.

Seed must be reproducible — a patch reopened must scatter identically. Follow the
existing convention: `MeshOps::Select`'s random mode is already seed-reproducible
and `INFINITE_SELECTTEST` asserts it.

Emit a point cloud (`Particle`s), so `Render 3D` draws sprites for free via the
`drawCloudSlot` path from `f47a942`.

## Node 2 — Points to Vertices

Point cloud in, mesh out: one mesh vertex per point, no edges, no faces. Lets
cloud output re-enter mesh operators.

**There is a trap here you must handle.** `Mesh::Empty()` is:

    bool Empty() const { return vertices.empty() || indices.empty(); }
                                                 // ^^ note the OR

A vertices-only mesh has `indices.empty() == true`, so **`Empty()` returns true**
and every consumer that guards on `if (mesh.Empty()) return;` will treat a valid
points-to-vertices result as nothing at all. Check the guard sites — e.g.
`InstanceOnPointsNode::Rebuild` (`src/nodes/GeometryOpNodes.cpp:485`) does exactly
this.

Resolve it deliberately, and say which you chose:

- **Recommended:** add `bool HasGeometry() const { return !vertices.empty(); }`
  and change only the guards where a vertices-only mesh is meaningful. Leave
  `Empty()` alone — it is used widely and its current meaning ("has drawable
  triangles") is correct for the renderer.
- **Do not** change `Empty()` to `&&`. That would make every renderer and
  operator guard silently accept meshes they cannot draw.

## Node 3 — Distribute Points in Grid

The honest version of "a grid": a rectangular lattice of points, not a new data
type. Params: counts per axis, spacing, and a jitter amount with a seed.

`ImageToPointsNode` already builds a grid of points and keeps per-point grid-cell
UV (`src/nodes/GenerativeNodes.h:196`) — read it before writing this, and match
its ordering convention so a Distribute in Grid and an Image to Points of the
same dimensions produce points in the same index order. That correspondence is
what lets one drive the other.

## Node 4 — Merge by Distance

Geometry in, geometry out: welds vertices closer than a threshold.

`MeshOps::BuildWeldMap` (`src/core/Mesh.h:340`, implemented at
`src/core/Mesh.cpp:474`) already does exactly this, but at a **hardcoded**
quantum:

    const double kQuantum = 1e5; // ~0.00001 units

So this node is `BuildWeldMap` with the quantum lifted into a parameter, plus
index remapping and dropping degenerate triangles (any triangle whose three
indices are no longer distinct after welding). Add a threshold overload rather
than changing the existing signature — five operators call the current one
(`Mesh.cpp:550, 728, 845, 1371, 1839, 3655`) and none of them want a tunable
epsilon.

This is the cleanup node that makes every other conversion usable: scatter,
convert, merge.

## Plumbing every new node type needs

Per `ARCHITECTURE.md`, "Node Library": `RegisterNodes()`, `InputCountFor`,
`ConnectGeometrySlot`, `QueryNewLink` (validation **and** its separate assignment
block), a `DrawXxxParams` panel, the per-frame dispatch, and `DisconnectAllTo`
(`src/main.cpp:5255`) — that last one prevents a use-after-free when an upstream
node is deleted. Phase 2 makes it automatic; without Phase 2, add each branch by
hand.

Both `IGeometrySource` invariants apply to all four nodes: forward every
side-channel you do not explicitly change, and only bump your revision stamp
when your output actually changed. Both have caused shipped bugs.

Cache on `(input pointer, upstream revision, own params)` — mirror
`MeshToPointsNode::RebuildIfNeeded` (`src/nodes/UtilityNodes.cpp`). For Poisson
in particular this matters: re-running rejection sampling every frame on a dense
mesh will cost more than the render.

## Suggested split

Four nodes is too much for one session. In order of payoff:

1. **Distribute Points on Faces** alone — the algorithm, the visible win, and
   the largest single improvement to scattered geometry. Ship it by itself.
2. **Merge by Distance + Points to Vertices** — both small, and they pair
   naturally as the "clean up after a conversion" step.
3. **Distribute Points in Grid** — smallest, do it whenever.

## Out of scope

- **Points to Curves.** Needs a per-element attribute to sort and group by;
  waits on attribute work.
- **Volume / SDF distribution** (`Distribute Points in Volume`,
  `Points to SDF Grid`). Blender has 30 volume nodes; that is a different
  product.
- **Convex hull, bounding box, separate geometry, sort elements,
  duplicate elements.** Real candidates for a later phase, not this one.
- **Replacing `ToPoints`.** Index-stride sampling stays.
- **A "Grid" data type.** Node 3 emits points. Do not add a fifth component.

## Done means

1. Builds clean:

       cmake --build build -j"$(sysctl -n hw.ncpu)"

2. Geometry sweep passes:

       .claude/skills/geometry-transform-sweep/driver.sh

3. Full hygiene suite passes, including the 163-node round trip:
   `.claude/skills/run-infinite-hygiene/`
4. Verified by hand in the app:
   - `UV Sphere → Distribute Points on Faces → Render 3D` — **no dark caps at
     the poles.** Compare side by side against `Mesh to Points` on the same
     sphere; the difference is the entire point of Node 1.
   - Poisson mode with a raised `minDistance` visibly stops adding points rather
     than packing them tighter.
   - Reopening a saved patch scatters **identically** — same seed, same points.
   - `Particle System → Points to Vertices → Transform → Render 3D` draws
     something. If it draws nothing, the `Mesh::Empty()` trap above was not
     handled.
   - `Cube → Subdivide → Merge by Distance` at a large threshold visibly
     collapses geometry, and at zero is a no-op.
   - Deleting the upstream node in each chain does not crash on the next cook.
5. A patch with all four nodes saves, closes, and reloads intact.
