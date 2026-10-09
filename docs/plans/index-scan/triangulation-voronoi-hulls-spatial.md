# Index scan: triangulation, Voronoi, convex hulls, spatial indexes

Candidates were chosen from my own knowledge plus docs/plans/index-opensource-scan.md:18-19. They were NOT cross-checked against the indexopensource.com list, because that site is blocked (403).

## Projects

Read (licence fetched from raw LICENSE before reading source):
- mapbox/delaunator, branch main. LICENSE is ISC (Copyright 2026 Mapbox). Read `index.js`: `constructor`/`update` seed + sweep-hull, `_hashKey`, `_legalize`, `pseudoAngle`.
- mapbox/earcut, branch main. LICENSE is ISC. Read `src/earcut.js`: `earcut`, `earcutLinked`, `isEarHashed`, `indexCurve`, `zOrder`.
- kchapelier/fast-2d-poisson-disk-sampling, master. LICENSE is MIT. Licence only, source not read. Infinite already has a working Poisson sampler, so I did not read it.

Skipped or not read:
- akuukka/quickhull (the listed "QuickHull, public domain"). The repo has no LICENSE file (LICENSE, LICENSE.md, UNLICENSE and COPYING all return 404). Only the README says "100% Public Domain". I fetched `QuickHull.cpp` but did NOT use it. A README-only claim is unverifiable under the brief's rule. Either confirm the index entry, or write a 3D hull from the textbook algorithm.
- mourner/delaunator, mourner/earcut, mourner/rbush: these raw paths 404 because the repos moved. rbush, flatbush and kdbush LICENSE files were fetched (MIT, ISC, ISC) but the source was not read. A 2D R-tree is not needed by anything in Infinite today.
- nanoflann: COPYING fetched but not read. It is BSD, but a kd-tree has no caller in Infinite (see the grep below).

## What Infinite does today

- Voronoi exists only as a procedural texture, never as geometry.
  - 2D shader Voronoi: `src/nodes/TextureNode.cpp:120-160`. `voronoiCalc` scans a 5x5 grid of jittered cells and returns F1, F2 and cell id. A second pass computes distance-to-edge. Metrics are Euclidean, Manhattan, Chebyshev and Minkowski (`:8-10`).
  - Another Voronoi in `NoiseNode.cpp:60-86`, and a Field preset "Voronoi cells" (`FormulaNode.cpp:169`).
- Poisson disk on mesh faces is `MeshOps::DistributeOnFaces` (`src/core/Mesh.cpp:2849-2947`).
  - It is dart-throwing on a flat open-addressed 3D spatial hash with cell size equal to minDistance (`:2874-2897`) and a 27-cell neighbour check (`:2917-2931`).
  - It caps candidates at 15x with a 300k ceiling, and gives up after 150 consecutive rejects (`:2907-2908`).
  - Wired through `DistributePointsOnFacesNode` (`src/nodes/PointDistributionNodes.h:20`, method at `:77`) and registered at `src/app/graph/NodeRegistry.cpp:95`.
  - Grid scatter: `DistributePointsInGridNode` (`PointDistributionNodes.h:253`, `NodeRegistry.cpp:97`).
- Ear clipping is O(n^2) (`Mesh.cpp:1201-1260`, `EarClip`), used by Text3D glyphs.
  - It has a bridge-based hole merge (`MergeHoles`, `:1298-1330`), called at `:1446` and `:1483`.
  - Its own comment says "fine for glyph outlines - a few hundred points" (`:1201`). It has a guard of n*n+16 iterations.
- The "Triangulate" geometry op (`GeometryOpNodes.cpp:292`, `Mesh.cpp:2094`) only flattens normals and adds jitter. It is not a point triangulator.
- There is no Delaunay, convex hull, k-d tree, BVH or R-tree.
  - Grep over src/ (*.h, *.cpp), case-insensitive: `voronoi|delaunay|convex ?hull|poisson|kd-?tree|quickhull|triangulat|earcut|ear.clip`.
  - Result: the only hits are the files above, and none is a Delaunay, hull or tree. `hull|bvh|spatial.?hash|kdtree|uniform grid` hits only the Poisson grid and the unrelated Spatial Mixer (audio).
  - The 3D-node list in `NodeRegistry.cpp:64-97` has Image to Points, Mesh to Points, Distribute Points on Faces, Points to Vertices and Distribute Points in Grid. It has nothing that turns a point cloud into a mesh.

## What the peers do differently

- Delaunator (`index.js`) builds a Delaunay triangulation in about O(n log n), with flat typed arrays:
  - Seed triangle from the point nearest the bounding-box centre. Sort the other points by distance from the seed circumcentre (`quicksort`, near `:150-185`). Sweep them in, attaching each to the convex hull.
  - A hash of `pseudoAngle` around the circumcentre (`_hashKey` `:302`, `pseudoAngle` `:442`) finds a visible hull edge in O(1) amortised.
  - `_legalize` (`:312`) flips edges with a fixed-size stack (no recursion). It uses the `inCircle` predicate, and `orient2d` is used for hull visibility.
  - Output is `triangles` and `halfedges` plus `hull`. A degenerate collinear input returns just a hull (the `minRadius === Infinity` branch, around `:143`). Near-duplicate points are skipped (`EPSILON` check).
  - Voronoi cells come free from the halfedge array: circumcenters of triangles are the Voronoi vertices (`circumcenter` `:526`).
- Earcut (`src/earcut.js`) is a circular doubly-linked list with z-order (Morton) hashing for ear tests.
  - `zOrder` (`:614`) interleaves bits. `indexCurve` (`:539`) sorts nodes by z (insertion sort for small n, else 4-pass radix).
  - `isEarHashed` (`:209`) tests only points within the ear's z-range, instead of all remaining points.
  - Fallbacks when no ear is found (`earcutLinked` `:127`): filter collinear points, `cureLocalIntersections`, then `splitEarcut`.
  - Holes go through `eliminateHoles` / `findHoleBridge` (`:293`, `:440`), with the bridge chosen by a ray test and sector check.

## Concrete improvements

### Existing nodes

1. Text3D and any other ear-clip user: replace `EarClip` (`Mesh.cpp:1203`) inner loop with the z-order hashed ear test. Benefit: no hang or "partially triangulated glyph" on dense outlines such as SVG or large fonts, and fewer failures on near-degenerate contours. Effort M. Licence: ISC. The idea (z-order bucketing) is generic; a near-port needs the ISC notice in the file header. Skill: new-geometry-node (mesh caching and revision rules). Do the cheap part first: the `isConvex` skip and collinear-point filtering from `filterPoints`/`earcutLinked`. Effort S.
2. Hole bridging in `MergeHoles` (`Mesh.cpp:1301`): current logic sorts every outline vertex by distance and tests each bridge for crossings, which is O(n) per hole plus sort. Earcut's `findHoleBridge` is a ray cast to the leftmost point, which is cheaper and a known-good heuristic. Effort S-M. ISC.
3. Poisson (`Mesh.cpp:2849`): works and is tested (`FrameTests1.cpp:998-1012`). Nothing to learn from earcut/Delaunator. A Bridson-style active list with annulus candidates would fill tighter than dart throwing, but this repo's 150-reject saturation exit already bounds cost. Effort M, low value. Not recommended unless tight packing is a requested complaint.
4. Voronoi texture (`TextureNode.cpp:120`): a 5x5 scan per pixel (25 hash2 calls, twice). Delaunator offers nothing for GPU noise. The standard speed-up is a 3x3 scan with a jitter cap of 1 (no 5x5), which is not from these projects. Left as an open note, not scored.

### New nodes

1. Delaunay Mesh (3D category, new-geometry-node). Input: a point cloud (`IGeometrySource::GetPointCloud`, `Geometry3DNodes.h:280`) from Distribute Points / Image to Points / Mesh to Points; project to a chosen plane (xy/xz/yz), run a Delaunator-style sweep, output a triangle mesh or wireframe/edge curves. Benefit: the first way to make a surface or low-poly "crystal" look from scattered points; Image to Points then Delaunay gives a classic low-poly portrait. Effort M (about 400 lines of C++ for the sweep, `orient2d`/`inCircle`, `_legalize`). Licence: ISC, attribution if near-port; algorithm is also in the published Delaunator paper/blog. Remember the passthrough/mesh-revision traps in `codebase-navigation` (forward cloud/curve in any wrapper).
2. Voronoi Cells geometry (from the Delaunay halfedges: circumcentres to cell polygons, clipped to a bounding box). Output cell polygons, shatter pieces, or an edge wireframe; the shatter-to-instances use case. Effort M on top of node 1. ISC.
3. Convex Hull (3D, hull of a point cloud or mesh). Benefit: bounding blobs, collision proxies, "shrink-wrap" looks. Effort M. Blocked on a verified licence for the public-domain QuickHull (see Skipped); alternatively write it from textbook QuickHull. Not scored with a licence claim.
4. Optional: triangulate arbitrary polygon outlines as a node (Polygon to Mesh) on top of an Earcut-style routine. Effort S once improvement 1 lands.

Not needed: a general spatial index. Nothing in src/ does neighbour queries beyond the Poisson grid (grep above), so a kd-tree/R-tree dependency is premature. If a Delaunay node is added, its halfedges already give neighbour adjacency.

## Candidates

Delaunay Mesh from point cloud (Delaunator sweep + halfedges) | first point-cloud to surface/low-poly path | M | ISC (attribution if near-port)
Voronoi Cells / shatter geometry from Delaunay circumcentres | geometry Voronoi (today texture-only) | M | ISC
Earcut z-order hashing + hole bridge for Text3D EarClip | no hangs on dense outlines, faster text rebuild | S-M | ISC
Convex Hull node | hulls/proxies; licence of akuukka/quickhull unverified (README-only) so write from textbook | M | unverified, do not read source
