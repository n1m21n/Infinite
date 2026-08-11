# Infinite — Phase 4: selection as an input, not four extra operators

Stop having a separate `XSelected` operator for every operation that can be
restricted to part of a mesh. One node sets a face selection; every operator
optionally honours it.

Verified against HEAD after `e3ae030` (Phase 0) and `f47a942` (Phase 1).
Phase 2 and Phase 3 are independent of this and can land before or after.

## Why this is worth a phase of its own

`GeometryOpNode` (`src/nodes/GeometryOpNodes.h:15`) currently has 17 operators,
of which **four exist only because selection is stateful**:

    kSelect, kDeleteSelected, kTransformSelected, kExtrudeSelected

Backed by four functions in `MeshOps` (`src/core/Mesh.h:370-377`):

    Mesh Select(const Mesh& in, int mode, float a, float b, float c, int axis,
                float seed, ...);
    Mesh DeleteSelected(const Mesh& in, bool keepSelected);
    Mesh TransformSelected(const Mesh& in, const Mat4& m, bool alongNormals,
                           float normalAmount);
    Mesh ExtrudeSelected(const Mesh& in, float distance, float inset);

This pattern **grows combinatorially**. Every future operator worth restricting
to a selection — Solidify, Twist, Explode, Smooth, Subdivide, Bevel, Wireframe —
needs its own `XSelected` twin, its own enum entry, its own signature-cache
field, and its own params-panel branch. Blender needs zero of these: every
operation takes a `Selection` input and a `Domain`, and there is no "Select"
node at all.

Infinite already has the hard part. `Mesh::faceMask`
(`src/core/Mesh.h:29`) is a per-triangle array that **travels down the geometry
cable**, and `Mesh::FaceSelected(size_t)` (`:42`) already implements the
"empty means all selected" convention. `MeshOps::ToPoints` already honours it
(`src/core/Mesh.cpp:1901`, the `hasSelection` path). The mask mechanism works;
what is wrong is that only four operators consult it.

## The change

**Keep `kSelect` as the mask producer.** It is the closest thing this codebase
has to a field input, and removing it would leave no way to express *which*
faces. Do not delete it.

**Make every operator optionally honour `faceMask`,** via one new bool on
`GeometryOpNode`:

    // When set and the incoming mesh carries a selection, this operator only
    // affects selected faces and passes the rest through untouched. Ignored
    // when the input has no selection (Mesh::faceMask empty), so nothing
    // changes for a chain with no Select node in it.
    bool selectionOnly = false;

**Then retire the three redundant operators**, mapping each to its general form
plus `selectionOnly = true`:

| Retired | Becomes |
|---|---|
| `kDeleteSelected` | a new `kDelete` with `selectionOnly` |
| `kTransformSelected` | `kTransform` with `selectionOnly` |
| `kExtrudeSelected` | `kExtrude` with `selectionOnly` |

`kDeleteSelected` has no general twin today, so add `kDelete` (delete faces —
with `selectionOnly` off it deletes everything, which is useless but consistent;
consider requiring a selection for that op specifically).

## Which operators to convert, and in what order

Do **not** attempt all 17 in one pass. Order by whether "affects only these
faces" is even well defined:

1. **Per-face independent — do these first.** The operator acts on each face
   without needing neighbours: `Delete`, `Transform`, `Extrude`, `Explode`,
   `Triangulate`, `Normals`, `Solidify`, `Wireframe`. Restricting these is a
   filter in the face loop.
2. **Per-vertex, needs a vertex selection derived from faces.** `Twist`,
   `Displace`. Use the same "a vertex counts as selected if any face touching
   it is selected" rule `ToPoints` already implements at
   `src/core/Mesh.cpp:1904-1907` — reuse that, do not invent a second rule.
3. **Connectivity-dependent — leave alone this phase.** `Subdivide`, `Smooth`,
   `Bevel`, `Screw`, `Boolean`. Partially subdividing a mesh needs crease
   handling at the selection boundary to avoid cracks. Out of scope; say so in
   a comment at each site rather than silently ignoring `selectionOnly`.
4. **Meaningless — hard-ignore.** `Array`, `Mirror` duplicate whole meshes.
   Leave `selectionOnly` hidden in the UI for these rather than showing a
   toggle that does nothing.

**Hide the toggle for any operator that ignores it.** A visible control that
does nothing is worse than no control — the same reasoning the codebase already
applies in `QueryNewLink` for the HDRI pin (`src/main.cpp` env-slot branch:
reject rather than accept-and-ignore).

## Migration — the part most likely to break

Saved patches contain `op` as an integer index into the `Op` enum
(`src/nodes/GeometryOpNodes.h:19-25`), written through `VisitParams`. **Removing
or reordering enum entries silently reinterprets every saved patch.** A patch
with `kSmooth` (10) would load as whatever now sits at index 10.

Handle it one of two ways, and state which you chose in the commit message:

- **Safest:** keep the retired enum values as deprecated aliases at their
  current indices, mapped at load time to `(generalOp, selectionOnly = true)`,
  and simply omit them from the spawn menu and the dropdown. Nothing shifts.
- **Cleaner but riskier:** renumber, and add explicit remapping in
  `Patch::LoadParams` / `ReloadDerivedState`. Only do this if you also bump a
  patch version marker.

Recommendation: the deprecated-alias route. This codebase has no patch
versioning, and `RegisterNodes()` registers each operator under its own
searchable name (`src/main.cpp` — the `GeometryOpNode::OpNames()` loop), so a
retired name simply stops being registered.

`GeometryOpNode::Signature` (`src/nodes/GeometryOpNodes.h:223`, compared at
`:242`) must gain `selectionOnly`, or toggling it will serve a stale cached
mesh with no visible response. It must **also** already include the incoming
selection — check whether the signature currently keys on the input's
`MeshRevision()` only; a `Select` node upstream changing its mask must bump that
revision, or downstream operators will not re-run. Verify this rather than
assuming: `MeshOps::Select` returns a new `Mesh`, so the revision should move,
but confirm it.

## Out of scope

- **Vertex- and edge-domain selection.** Face domain only. Blender's `Domain`
  enum is a field-system feature.
- **Selection as a separate cable.** It rides on `Mesh::faceMask` down the
  existing geometry connection, which is the design already in place and the
  right one — see the comment at `src/core/Mesh.h:22-28`.
- **Connectivity-dependent operators** (`Subdivide`, `Smooth`, `Bevel`,
  `Boolean`). Listed above as group 3.
- **Named attributes / Phase 3 work.** `faceMask` stays a plain
  `vector<unsigned char>`, not an attribute.

## Done means

1. Builds clean:

       cmake --build build -j"$(sysctl -n hw.ncpu)"

2. `INFINITE_SELECTTEST` still passes — it is the existing regression test for
   this exact machinery (`src/main.cpp`, search `INFINITE_SELECTTEST`). It
   asserts the empty-mask-means-all convention, normal-based selection finding
   exactly the two top faces of `Primitives::Cube(1)`, seed reproducibility, and
   that point billboards are never torn in half. All four must still hold.
3. Geometry sweep passes:

       .claude/skills/geometry-transform-sweep/driver.sh

4. Full hygiene suite passes, including the 163-node round trip:
   `.claude/skills/run-infinite-hygiene/`
5. **A patch saved before this change, using Delete Selected / Transform
   Selected / Extrude Selected, loads and still does the same thing.** Save one
   on the current build first. This is the highest-risk check in the phase.
6. `Cube → Select (normal, +Y) → Transform (selectionOnly on)` moves only the
   top; with `selectionOnly` off it moves the whole cube.
