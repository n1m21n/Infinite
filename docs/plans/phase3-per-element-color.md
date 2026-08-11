# Infinite — Phase 3: per-element colour attributes

Give geometry per-element colour: a value that varies per vertex on a mesh and
per point in a cloud, instead of one flat `Material::color` for the whole
surface. Then ship one node that writes it from a concrete set of sources —
including the existing Palette — so the feature is usable the day it lands.

Verified against current HEAD before writing. Phase 0 (`e3ae030`) and Phase 1
(`f47a942`) are already committed; this prompt assumes both.

## A design decision already made — do not "improve" it

An earlier draft of this plan called for **generic string-keyed named
attributes** with a domain tag, mirroring Blender's `Store Named Attribute` /
`Named Attribute` pair. **That was rejected deliberately.**

Blender's named attributes are only useful because it has a *field evaluator* —
a lazy per-element function graph — that can read an arbitrary name and compute
with it. Infinite has no field system and is not getting one: its cook model is
eager with revision-stamp caching, which is structurally incompatible. Without
an evaluator, a `Store Named Attribute` node would write into a void, because
nothing could consume a name it wasn't compiled to know.

So this phase ships **a fixed, named set of per-element attributes** whose
storage shape makes adding the next one a field plus a propagation line, not a
new mechanism. If you find yourself building a `std::map<std::string, ...>` or
an attribute-name text field in the UI, you have left this phase.

## Existing precedent to follow

`Mesh` (`src/core/Mesh.h:18`) already carries two optional per-element arrays:

- `faceMask` — per triangle, `unsigned char`, **empty means "all selected"**
- `selectionGroup` — per triangle, `unsigned int`, **empty means "each face is
  its own unit"**

That "empty means the default, so nothing that doesn't care pays a cost" 
convention is exactly right and is what this phase extends. Read the comments at
`src/core/Mesh.h:22-37` before designing the new field — match that style and
that reasoning.

`Particle` (`src/core/Mesh.h` — the point-cloud element) **already has
`r, g, b` and `scale`**. The point-cloud half of this feature is therefore
mostly done; the work is the mesh half plus the node that writes both.

## Step 3a — storage, one writer node, and the renderer

### Storage

Add to `Mesh`, following the `faceMask` convention:

    // Per-vertex linear RGB, three floats per vertex, parallel to `vertices`.
    // Empty means "no per-vertex colour" - the material's flat colour is used
    // instead, so no existing mesh grew a cost.
    std::vector<float> vertexColor;

Deliberately a flat `vector<float>` of rgb triples rather than a
`vector<Color>` struct, matching how `InstanceOnPointsNode::mColors`
(`src/nodes/GeometryOpNodes.cpp:474-476`) already packs per-instance colour for
upload — same shape means the same upload path works.

Add a validity helper alongside `Mesh::FaceSelected`:
`bool HasVertexColor() const { return vertexColor.size() == vertices.size() * 3; }`
Size-mismatched data must be treated as absent, never indexed into — a mesh
operator that resizes `vertices` and forgets `vertexColor` would otherwise read
out of bounds.

### The writer node — "Set Color"

One new node type, geometry in / geometry out, with a **dropdown of sources**.
This mirrors the codebase's established pattern of one class plus a dropdown
(`FilterNode` over `src/core/FilterDefs.h`, `GeometryOpNode` over its `Op`
enum) — see `ARCHITECTURE.md`, "Node Library".

Sources to ship:

- **Flat** — one colour (the trivial case; makes the node self-explanatory)
- **Position** — XYZ mapped to RGB over the mesh's own bounding box
- **Normal** — normal direction to RGB
- **Index** — element id ramped between two colours
- **Random** — per element, seeded, reproducible from the seed
- **Palette** — via `IPaletteSource` (`src/core/Palette.h:23`), which exposes
  `SwatchCount()` and `GetSwatch(index, outRgb)`. Pick the swatch by element id
  modulo the count. **This is the differentiating source** — it makes a
  palette extracted from a reference image colour a point cloud directly, which
  no other tool does.
- **Texture** — sample an image input at each vertex's UV

Write both domains from the same node: `vertexColor` when the input carries a
mesh, and `Particle.r/g/b` when it carries a point cloud. `MeshToPointsNode`
implements both interfaces as of `f47a942`, so a Set Color between it and
Render 3D must colour whichever representation is actually drawn.

**Palette note:** `PaletteBinding` (`src/core/Palette.h`) binds
`(node, colourIndex) -> (paletteNode, swatch)` — it is per-*parameter*, not
per-element, so do **not** try to route this through `PaletteBinding`. Read the
`IPaletteSource` directly from a geometry-side input pin instead.

**Modulation note:** modulators emit a single scalar, so they can drive this
node's *parameters* (ramp endpoints, seed, palette offset) but cannot drive a
per-element value directly. That is the correct and only honest scope here —
do not attempt per-element modulation.

### The renderer — cheaper than it looks

The mesh vertex attributes are pos/normal/uv at locations **0, 1, 2**
(`src/nodes/Geometry3DNodes.cpp:1904-1909`). Locations **3-6** are the
per-instance `Mat4` and **7** is the per-instance colour
(`:2023-2040`). So per-vertex colour goes at **location 8** — confirm 8 is free
before using it.

The fragment shader already reads:

    vec3 base = toLinear(uBaseColor) * vInstanceColor;   // Geometry3DNodes.cpp:575

Per-vertex colour multiplies into that same expression. `uBaseColor` is set from
`material.color` at `:1821` and `:2075`. Multiplying (rather than replacing)
means a white material leaves the attribute untouched and a tinted material
still tints — and a mesh with no `vertexColor` passes `vec3(1.0)`, so nothing
changes for existing patches.

Upload `vertexColor` into its own VBO in `GpuMesh`
(`src/nodes/Geometry3DNodes.h:420`) keyed on the same `meshRevision` the vertex
buffer already uses — not a separate stamp. When `HasVertexColor()` is false,
disable the attrib array and rely on a `vec3(1.0)` default rather than uploading
a buffer of ones.

**The sprite/cloud path needs no renderer change.** It already routes
`Particle.r/g/b` through `vInstanceColor`.

### What Step 3a deliberately does not do

Mesh operators will **drop** `vertexColor` in 3a. A Set Color feeding a
Subdivide loses its colour. That is an accepted, documented limitation for this
step — do not fix it here, and do **not** paper over it by silently keeping a
stale, wrong-sized array. Drop it explicitly (clear the vector) so
`HasVertexColor()` returns false rather than leaving a size mismatch.

## Step 3b — propagation through mesh operators

This is the real cost of the phase and the reason it is a separate step.

`ARCHITECTURE.md`'s "Invariants for `IGeometrySource`-consuming nodes" documents
that side-channel forwarding has silently broken **three times**
(`ClothNode`, `MeshResynthNode`, `MeshToPointsNode` all dropped
`GetMappingTransform()`). `vertexColor` is a fourth side-channel with the same
failure mode, made worse because operators change vertex *counts*.

Split the operators by difficulty and do them in this order:

1. **Trivially preserving** — vertex count and order unchanged: `Transform`,
   `Normals`, `Triangulate`, `Set Material`. Copy the array through.
2. **Count changes, mapping known** — `Array`, `Mirror`, `Screw`: each output
   vertex derives from one input vertex, so copy per repetition.
3. **Genuine interpolation needed** — `Subdivide`, `Smooth`, `Bevel`: new
   vertices sit between old ones. Subdivide's Loop weights already compute
   positional blends; reuse the same weights for colour. Smooth moves vertices
   without adding them, so colour passes through unchanged.
4. **Sampling** — `ToPoints` (`src/core/Mesh.cpp:1894`) should carry
   `vertexColor` into `MeshPoint`, and `PointsToFaces` (`:2117`) into the
   billboard vertices. Note `ToPoints` now takes `(in, mode, maxPoints, weld,
   dissolveAngleDegrees)` as of `e3ae030` — welding merges vertices, so colours
   of merged vertices must be averaged or first-wins, not left dangling. Pick
   first-wins and say so in a comment; averaging at a seam is not obviously
   more correct.

Write **one shared helper** that remaps a `vertexColor` array given an
index-mapping vector, and use it everywhere rather than open-coding the copy in
each operator. `BuildWeldMap` (`src/core/Mesh.cpp:474`) already returns exactly
that shape of mapping and is used by five operators — mirror its ergonomics.

## No patch-format change

`vertexColor` is derived data, rebuilt from the graph on every cook, exactly
like `Mesh` itself — it is never serialised. Only the Set Color node's own
parameters go through `VisitParams`. Adding the node type does require the
standard registration touch points (see `ARCHITECTURE.md`, "Node Library"):
`RegisterNodes()`, `InputCountFor`, `ConnectGeometrySlot`, its `DrawXxxParams`
panel, the per-frame dispatch, and `QueryNewLink`.

## Ordering against Phase 2

Not a hard dependency, but **Phase 2 first is better**: Phase 2 folds the three
geometry interfaces into one, and landing Set Color first means one more class
for Phase 2 to migrate. If Phase 2 has already landed, Set Color implements the
single folded interface and reads components via `GetPointCloud()`.

## Out of scope

- **Generic string-keyed named attributes**, a `Named Attribute` reader node,
  or any attribute-name UI. See the design decision at the top.
- **Any field / lazy per-element evaluator.**
- **Attributes other than colour.** `radius` already exists as
  `Particle.scale`; weight, mask and id are not part of this phase.
- **Per-face colour.** Vertex domain only. Per-face needs n-gon face identity,
  which is a separate deferred item.
- **Selection as attribute (Phase 4).** `faceMask` stays exactly as it is.
- **Conversion nodes (Phase 5).**

## Done means

1. Builds clean after each step, separately:

       cmake --build build -j"$(sysctl -n hw.ncpu)"

2. Set Color has a geometry input, so the sweep applies — it catches the
   dropped-side-channel and spurious-revision-bump classes that this phase is
   most likely to introduce:

       .claude/skills/geometry-transform-sweep/driver.sh

3. The full hygiene suite passes, including the 163-node round trip (a new node
   type must survive save/load): `.claude/skills/run-infinite-hygiene/`
4. Verified by hand in the app:
   - `Sphere → Set Color (Position) → Render 3D` — smooth XYZ-to-RGB gradient
     across the surface.
   - `Sphere → Mesh to Points → Set Color (Palette) ← Palette → Render 3D` —
     points take the palette's swatches. This is the headline chain; if it
     works, the phase delivered.
   - A geometry chain with **no** Set Color renders byte-identically to before
     this change. The `vec3(1.0)` default must be a true no-op.
   - After 3b: `Sphere → Set Color → Subdivide → Render 3D` keeps its colours,
     interpolated across the new vertices.
5. `Mesh::HasVertexColor()` is false — never a size mismatch — after every
   operator that does not yet propagate.
