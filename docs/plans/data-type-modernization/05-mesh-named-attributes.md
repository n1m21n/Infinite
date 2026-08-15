In /Users/namansoni/infinte, there's a request for a Blender-Geometry-
Nodes-style generic attribute system on Mesh. DO NOT build this as a
generic string-keyed/typed attribute system — that exact idea was already
proposed and explicitly rejected in this codebase's own planning docs:
docs/plans/phase3-per-element-color.md (lines 12-27): "An earlier draft
of this plan called for generic string-keyed named attributes with a
domain tag, mirroring Blender's Store Named Attribute / Named Attribute
pair. That was rejected deliberately... If you find yourself building a
std::map<std::string,...> or an attribute-name text field in the UI, you
have left this phase." The stated architectural reason: Infinite's cook
model is eager with revision-stamp caching (src/core/Mesh.h's
NextMeshRevision()), which has no lazy field evaluator — that's what
makes Blender's generic version actually work, and it's structurally
absent here. The same rejection is repeated in docs/plans/README.md,
phase2-one-geometry-interface.md, and phase4-selection-as-input.md, plus
a code-level comment on SetColorNode (src/nodes/GeometryOpNodes.h:
601-602: "Deliberately NOT a generic named-attribute writer").

What the docs prescribe instead, and what this prompt actually asks for:
a fixed, named per-element field added one at a time, following the
exact existing convention `Mesh::vertexColor` already uses
(src/core/Mesh.h:39-45,61) — a parallel `std::vector<float>`, empty
means "not present" (no cost to meshes that don't use it), with a
`HasX()` guard that treats a size mismatch as absent rather than
indexing out of bounds (mirroring `HasVertexColor()` at line 61).

Before implementing anything: this prompt does NOT know what the next
concrete per-element property should be — that's a real product
decision, not something to invent speculatively. Stop and ask the user
which one is actually needed before writing code. Candidates that came
up in earlier discussion (none confirmed as actually wanted yet):
per-vertex velocity (for simulation state round-tripping through a mesh),
per-face material index (for multi-material meshes — note Material is
currently one-per-mesh, src/nodes/Geometry3DNodes.h:16-51), or per-point
custom scalar (for instancing/distribution nodes to carry, e.g., a
per-instance random seed).

Once a specific property is confirmed:
1. Add it to `Mesh` (src/core/Mesh.h) as a parallel array following the
   vertexColor pattern exactly: empty = absent, a `HasX()` size-match
   guard, a comment stating the domain (per-vertex vs per-face) and the
   upload-shape rationale if it's consumed by a shader.
2. Grep for every place that resizes `vertices` or `indices` without
   also touching `vertexColor` today (src/core/Mesh.cpp, the ~8 node
   files that touch Mesh fields directly: GeometryOpNodes.cpp,
   GenerativeNodes.cpp, Geometry3DNodes.cpp, PointDistributionNodes.cpp,
   UtilityNodes.cpp, SimulationNodes.cpp, ModelSourceNode.cpp,
   Text3DNode.cpp) and confirm the new field degrades safely (stays
   empty/absent) rather than getting silently resized incorrectly.
3. Do not add a name/domain-tag field, a generic reader/writer node, or
   any attribute-name UI text field — if you find yourself doing any of
   those three things, stop, you've left the scope the project's own
   docs already settled on.

Build with:
  cmake --build build -j"$(sysctl -n hw.ncpu)"
and run .claude/skills/geometry-transform-sweep/driver.sh before
considering it done.
