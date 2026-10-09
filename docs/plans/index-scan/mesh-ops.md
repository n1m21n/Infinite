# Cluster: mesh ops (simplify, smooth, boolean, voxel, marching cubes / dual contouring)

Note: candidates were NOT cross-checked against the indexopensource.com list (site blocked, 403). Picked from own knowledge.

## Projects read
- zeux/meshoptimizer @ master, LICENSE.md = MIT (verified). Read src/simplifier.cpp (2947 lines): VertexKind enum + kCanCollapse table (~296-330), quadric* (~704-1030), fillFaceQuadrics/fillEdgeQuadrics (~1034-1170), hasTriangleFlips (~1249), rankEdgeCollapses (~1417), performEdgeCollapses (~1527), meshopt_simplifySloppy (~2646), meshopt_simplifyPrune (~2779).
- elalish/manifold @ master, LICENSE = Apache-2.0 (verified). Read src/boolean3.cpp (Shadow01 ~54, Intersect12 ~338, Winding03 ~388, Boolean3 ctor ~473) and src/sdf.cpp (LevelSet ~473, CreateLevelSet ~485, GridVert/NearSurface/BuildTris ~197-400; header at ~447 says marching tetrahedra on a BCC-style grid with a tolerance).
## Skipped
- Dual contouring / marching cubes reference impls: none fetched; Manifold LevelSet is the permissive reference used instead. Not looked at: OpenVDB (MPL-2.0, weak copyleft, skipped as unclear for our rule), CGAL (GPL/LGPL, skipped), libigl (MPL-2.0, skipped).

## What Infinite does today
- One GeometryOpNode with an Op enum (src/nodes/GeometryOpNodes.h:31-48): Transform, Array, Subdivide, Solidify, Extrude, Wireframe, Triangulate, Normals, Explode, Twist, Smooth, Mirror, Screw, Select, Delete. Dispatch in src/nodes/GeometryOpNodes.cpp:274-320.
- Smooth: MeshOps::Smooth is Taubin (lambda/mu) over a weld-map adjacency, src/core/Mesh.cpp:918-929. Subdivide is Loop with blend, Mesh.cpp:719.
- Boolean: MeshOps::Boolean, BSP-tree CSG in the csg.js style, Mesh.cpp:4532 (namespace csg), 4822 (Boolean). Header notes it needs closed, non-self-intersecting input (Mesh.h:~400-406). Wired as a node mode at src/nodes/UtilityNodes.cpp:386-389.
- Marching: MetaBalls, Mesh.cpp:4036, resolution capped at 96^3; uses a marching-TETRAHEDRA decomposition (6 tets per cube) with no case table, Mesh.cpp:3905-3925. Registered at src/app/graph/NodeRegistry.cpp:62.
- Weld/merge: BuildWeldMap, MergeByDistance (Mesh.h:339-353). Bevel, Wrap, Displace, SliceContours exist.
- Searched src/ for simplif/decimat/quadric/remesh/isosurface/voxel (case-insensitive): no mesh decimation, no remesh, no voxel/SDF-to-mesh path. (Hits in audio/platform files were substring noise like "sdf" in other words; the only "decimated" hits are scope traces.) The "dissolve" at Mesh.cpp:2594 is Wireframe edge filtering, not simplification.

## What the peers do differently
- meshoptimizer simplify: edge-collapse with quadric error metrics; per-vertex classification (manifold/border/seam/complex/fringe/locked) governs which collapses are legal (kCanCollapse), triangle-flip rejection (hasTriangleFlips), error-bounded termination (target_error, relative or absolute), optional vertex_lock, attribute-aware quadrics (UV/colour/normal), simplifySloppy (grid-cell clustering, fast, topology-agnostic), simplifyPrune (drop small disconnected islands).
- Manifold boolean: exact-ish via symbolic perturbation (Shadow01), edge-vs-face intersection then winding numbers (Winding03), so result is a manifold with consistent topology; handles coplanar/touching cases that BSP CSG gets wrong or fragments. Output keeps face/vertex provenance.
- Manifold LevelSet: SDF callback + bounds + edgeLength + level + tolerance -> mesh; sparse (only near-surface cells evaluated, NearSurface), parallel, vertices snapped to the surface within tolerance.

## Concrete improvements
Existing nodes first:
1. Boolean robustness: BSP CSG fragments faces (many sliver triangles after each op) and is fragile on coplanar faces. Cheap win: run MeshOps::MergeByDistance + a coplanar-merge pass after Boolean; real win is porting the Manifold approach (winding + symbolic perturbation). Effort M for cleanup, L for the exact port. Apache-2.0: attribution/NOTICE if near-port.
2. MetaBalls: switch from dense resolution^3 tet sampling to a sparse near-surface evaluation with a tolerance-driven vertex snap (LevelSet idea), lifting the 96 cap; also lets metaballs mesh finer. Effort M. Idea only, no attribution needed unless code is copied.
3. Smooth: add feature-preserving option (lock border/crease vertices, a la VertexKind locks) so Smooth does not melt open edges. Effort S. Idea only.

New nodes:
4. Simplify (Decimate) node: edge-collapse QEM with border/seam locking, flip rejection, and a "ratio / error" control; as an extra GeometryOpNode Op (append before kOpCount, do not reorder: saved ints). Benefit: LOD for imported models, heavy Instance chains, Array performance. Not present today (grep above). Effort M (own implementation from the QEM paper, using meshoptimizer as a design reference; MIT requires notice only if code is copied). Skill: new-geometry-node.
5. SDF-to-Mesh / Voxel remesh node (Level Set): a small SDF graph (sphere/box/torus, smooth-union, offset) or Field-driven SDF meshed via marching tets; combined with Boolean this gives robust "voxel boolean" and remesh-to-uniform. Effort L. Skills: new-geometry-node, field-integration if driven by Field. Apache-2.0 reference.
6. Prune-islands op (simplifyPrune idea): remove tiny disconnected components after Boolean. Effort S.

## Nothing to learn
Dual contouring: no permissive reference read; not recommended since tet-based MC already avoids ambiguity.

## Candidates
Simplify/Decimate node (QEM edge collapse) | LOD and perf for heavy geometry, fills a real gap | M | MIT idea (meshoptimizer), own implementation
Boolean cleanup then robust winding-based Boolean | fewer artifacts and fragments on CSG | M then L | Apache-2.0 (Manifold), attribution if ported
SDF level-set mesher (sparse, tolerance) | voxel remesh, higher-res metaballs | L | Apache-2.0 (Manifold) idea
