In /Users/namansoni/infinte, there is a request to add per-vertex tangent
vectors (tx,ty,tz + handedness sign) to the Vertex struct
(src/core/Mesh.h:10-15, currently `px,py,pz / nx,ny,nz / u,v`, no tangent
field) to support normal mapping.

IMPORTANT — read this before implementing: normal mapping already works
today via a different, deliberately-chosen method. src/nodes/
Geometry3DNodes.cpp:501-523 (`applyNormalMap`) derives the full
tangent/bitangent/normal frame per-pixel from screen-space derivatives
(dFdx/dFdy of world position and UV), and its own comment (lines 501-504)
states this was chosen *specifically* to avoid a vertex tangent field,
because adding one "would mean regenerating them in every primitive and
every mesh operator." Verified blast radius if you proceed: at least 12
primitive-construction functions in src/core/Mesh.cpp (Plane, Cube,
Sphere, Torus, Cylinder, Cone, Icosphere, TorusKnot, Capsule, Tube,
Pyramid, Prism, Helix, plus the platonic solids and Mobius/Klein/Gear/
Star/Disc/Arrow/Supershape), all MeshOps that rebuild vertices
(Subdivide, Mirror, Screw, ExtrudeContours, Solidify, Extrude, Wireframe,
PointsToFaces, Select, DeleteSelected, ExtrudeSelected), plus the
imported-model loader (src/nodes/ModelSourceNode.cpp:32) and point-cloud
converters (GenerativeNodes.cpp:380, PointDistributionNodes.cpp:183).
There is no existing `MeshOps::RecalculateTangents` alongside
`RecalculateNormals` — you would be writing that from scratch too.

Before writing any code: reproduce and screenshot an actual normal-mapped
surface with a visible seam or distortion artifact that the current
screen-space-derivative method produces (e.g. at a UV seam, or on a
low-poly mesh with sparse UV islands). If you cannot produce a concrete
visible artifact, stop here and report back that the existing method
appears adequate — do not add the tangent field speculatively.

If (and only if) you've confirmed a real artifact, then:
1. Add `float tx=1,ty=0,tz=0,tw=1;` to Vertex (tw carries handedness sign,
   the standard convention).
2. Add `MeshOps::RecalculateTangents(Mesh&)` mirroring the structure of
   the existing `RecalculateNormals` (src/core/Mesh.cpp:1562-1621),
   computing tangents from position+UV deltas per triangle, accumulated
   and normalized per vertex (standard Lengyel method).
3. Call it at the end of every primitive constructor and every MeshOps
   function that rebuilds `vertices` (see file list above) — or,
   preferably, call it once at the single `PushVertex`/`PushGrid`
   choke point (Mesh.cpp:21, :69) if that covers all primitives, and
   audit the remaining MeshOps functions individually since they don't
   all route through PushGrid (Ocean at line 1470/1477 constructs
   Vertex directly, for example).
4. For ModelSourceNode.cpp's imported-model path: check whether
   `Platform::ModelVertex` already carries tangents from the source file
   format; if not, call RecalculateTangents after load.
5. Update `applyNormalMap` (Geometry3DNodes.cpp:501-523) to use the
   native per-vertex tangent (interpolated by the vertex shader) instead
   of the screen-space-derivative fallback, but keep the derivative
   method as a fallback for any mesh that reaches the shader with a
   zero/degenerate tangent (e.g. old cached geometry), so nothing
   regresses silently.

Build with:
  cmake --build build -j"$(sysctl -n hw.ncpu)"
and run .claude/skills/geometry-transform-sweep/driver.sh (this touches
every geometry-producing node) before considering it done.
