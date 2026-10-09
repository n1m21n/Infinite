# Index scan: curves, splines, strokes, path offsets, 2D polygon boolean/offset

Candidates were NOT cross-checked against the indexopensource.com list (site blocked, 403); chosen from memory plus docs/plans/index-opensource-scan.md:18 (Clipper2 row).

## Projects read (licence verified from raw LICENSE before reading)
- AngusJohnson/Clipper2 @ main: LICENSE is Boost Software License 1.0 (verified). Read CPP/Clipper2Lib/src/clipper.offset.cpp (661 lines: OffsetPoint, DoMiter, DoRound, DoGroupOffset).
- mattdesl/extrude-polyline @ master: LICENSE.md is MIT (verified). Read index.js (Stroke._seg, miter/bevel join, caps).
- mattdesl/polyline-normals @ master: MIT (verified). Read index.js (per-vertex miter normals).
## Skipped
- Bezier.js (Pomax): LICENSE fetch returned 404 on master, so licence unverifiable; skipped.
- Paper.js: not fetched (offsetting is in a separate lib); not verified, skipped.
- extrude-polyline's polyline-miter-util dependency: not fetched; the maths is standard.

## What Infinite does today
- Polyline is just xyz triples + closed flag: src/core/Mesh.h:281-288. Exposed through IGeometrySource::GetCurve() (src/nodes/Geometry3DNodes.h:289), stamp via CurveStamp.
- Producers: CurveNode (src/nodes/CurveNode.h, kind catmull-rom/bezier/b-spline/linear, max 8 control points, kMaxPoints at :17), mesh BoundaryLoops (Mesh.h:393) and SliceContours (Mesh.h:459; Mesh.cpp:4460,4487). Consumers: PathNode (follow, PathNode.h:46-56), GeometryTableNode contour sampling (GeometryTableNode.cpp:64,170).
- GeometryOpNode::GetCurve (GeometryOpNodes.cpp:567) only applies a Transform to a curve; every other op returns the curve untouched. There are no curve operators.
- MeshOps::BuildCurve (Mesh.cpp:4181): uniform Catmull-Rom (no centripetal/chordal parameterisation), B-spline approximated (comment Mesh.h:381-383 admits it), steps clamped to 1..64 per span, fixed subdivision count regardless of curvature.
- MeshOps::SamplePolyline (Mesh.cpp:4117): arc-length sampling, but allocates and rebuilds the cumulative-length vector on every call (O(n) per sample).
- MeshOps::TubeAlong (Mesh.cpp:4283): circular profile only, parallel-transport frame, linear taper (radius*(1-taper*along)); no flat ribbon/stroke, no caps (open tube ends are holes; grep of quad pushing shows no cap triangles), no per-point width.
- 2D: MeshOps::ExtrudeContours with a hand-rolled O(n^2) ear clip + hole bridging (Mesh.cpp:1201, Mesh.h:323-327) is the only 2D polygon routine, used by Text3DNode (Text3DNode.cpp:37-46).
- Searched src/nodes, src/core for clipper/offset polyline/resample/simplify/chaikin/boolean polygon: zero hits (only unrelated "limiter"/"Triangulate" matches). So no 2D polygon union/difference/intersection, no polygon inset/outset, no polyline resample/simplify/smooth/offset node exists. external/ has no clipper (ls external).
- ShapeNode (src/nodes/ShapeNode.h) is an SDF image shader: strokes/fill are pixel-domain, not geometry.

## What the peers do differently
- Clipper2 clipper.offset.cpp: one offsetter with join types Miter/Square/Bevel/Round and end types Polygon/Joined/Butt/Square/Round (DoGroupOffset :455, OffsetPoint :311). Round-join step count derives from an arc tolerance (steps_per_360 = pi/acos(1 - arcTol/delta), DoGroupOffset ~:468-480) so smoothness is tolerance-driven. Concave joins insert 3 points and rely on a final union to clean (OffsetPoint comment ~:337). Miter falls back to square beyond a miter limit. Per-vertex delta callback (deltaCallback64_) gives variable-width offset.
- extrude-polyline index.js: 2D stroke triangulation with miter joins, miterLimit falling back to bevel (Stroke._seg), square/butt caps, overridable mapThickness(point,i,points) giving per-point width.
- polyline-normals: per-vertex miter normal + miter length for GPU-side thickness expansion.

## Improvements

Existing nodes first
1. CurveNode / BuildCurve: add centripetal Catmull-Rom (alpha 0.5) option and adaptive subdivision by flatness instead of fixed steps (peer idea: Clipper2 arc-tolerance formula; centripetal CR is a published technique, Barry & Goldman / Yuksel 2011, no code needed). Benefit: no loops/overshoot on uneven control spacing. Skill: new-geometry-node (revision rules); effort S. Licence: none (formula only).
2. SamplePolyline: cache cumulative lengths in Polyline or per-consumer (speeds PathNode, GeometryTable). Effort S, own code.
3. TubeAlong: add end caps, per-point width profile (curve-shaped taper) and optional flat ribbon profile (extrude-polyline-style mapThickness + miter normals). Benefit: strokes/ribbons that read as lines, closed tube end holes. Effort M. MIT; near-port needs attribution, a reimplementation from the idea does not.
4. ExtrudeContours: replace/extend ear clip with hole-aware robust triangulation only if glyph failures show up; Clipper2's cleanup-by-union idea does not need a port. Effort L, low priority.

New nodes
5. Curve Offset / Stroke node (IGeometrySource curve in, curve out): inset/outset a closed Polyline in plane with Miter/Round/Bevel joins and arc-tolerance-driven round steps, using the Clipper2 OffsetPoint/DoRound structure. Needs 3D-to-plane handling (SliceContours output is planar, so a natural fit). Benefit: concentric contour rings from sliced meshes, outline/shadow shapes from text, parallel curves. Skill: new-geometry-node, plus the GetCurve passthrough trap in codebase-navigation (forward GetCurve in any wrapper). Effort M. Reimplement from the algorithm; if code is copied, BSL-1.0 requires keeping the licence notice with source distributions only.
6. Curve Ops node: resample (even arc length N points), simplify (Ramer-Douglas-Peucker), smooth (Chaikin), reverse, join. Benefit: PathNode/Table/tube get uniform input, fills the gap that curves can only be Transform-ed (GeometryOpNodes.cpp:567-600). Effort S-M, textbook algorithms.
7. 2D Polygon Boolean node (union/intersect/difference/xor on planar closed curves, then ExtrudeContours): a direct Clipper2 use case. Writing a robust Vatti/clip engine is large; vendoring Clipper2 (BSL-1.0, allowed to ship without attribution in binary form) under external/<name>/ matches the vendoring pattern in codebase-navigation. Effort L (engine) or M (vendor + glue). Requires CMake include line and the Field GetCurve passthrough audit.

## Candidates
Curve Offset/Stroke node (Clipper2 join/arc-tolerance model) | outline/inset rings from text and slices | M | BSL-1.0 (idea reimplemented, no attribution if no code copied)
Curve Ops node: resample/simplify/smooth | makes curves editable and path-following uniform | S-M | none (textbook)
TubeAlong caps + width profile + ribbon (extrude-polyline) | strokes that look like strokes, closed tube ends | M | MIT (attribution only if code copied)
Centripetal Catmull-Rom + adaptive subdivision | cleaner splines | S | none
2D Polygon Boolean via vendored Clipper2 | union/diff of text and shapes then extrude | M-L | BSL-1.0
