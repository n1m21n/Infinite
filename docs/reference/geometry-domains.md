# Geometry domains — the 3D node contract map

Phase 1 deliverable of `docs/plans/geometry/domain-audit-prompt.md`, **re-verified
2026-09-30 against current source** (branch `feature/geometry-domain-map`, after
the Phase 4/5 fixes landed). Every row below was checked by reading the body
named in the citation. Status column: `V` = body read, no row is inferred.
Line numbers are current; the older revision of this file (2026-09-09) had
drifted throughout. **Phase 2 and Phase 3 sections at the bottom are the
original 2026-09-09 text and are partly stale** (see the banner there).

Interface: `IGeometrySource`, `src/nodes/Geometry3DNodes.h:174-272`. `GetMesh()` and
`MeshRevision()` pure virtual (`:188,194`); `GetPointCloud()`/`GetCurve()` default
`nullptr` (`:261,270`); `GetMaterialTexture(0)` defaults to `GetSurfaceTexture()`
(`:208-211`); all other channels default empty/identity/0.
Warning channel: `ICookWarningSource::CookWarning()` + `DescribeGeometryMismatch`
(`:291-340`).

**Predicates that decide everything** (`src/core/Mesh.h`): `Mesh::Empty()` =
`vertices.empty() || indices.empty()` (`:47`); `HasGeometry()` = `!vertices.empty()`
(`:51`); `FaceCount()` = `indices.size()/3` (`:52`). So a **verts-only mesh is
`Empty()`**.

## Tags and channels

| tag | meaning |
|---|---|
| `mesh:surface` | vertices + indices |
| `mesh:verts` | vertices, no indices (counts as `Empty()`) |
| `mesh:standin` | fabricated quad mesh. **Now zero producers** (D5 fixed) |
| `mesh:none` | honest empty mesh |
| `cloud` / `curve` | `GetPointCloud()` / `GetCurve()` non-null |

| # | channel | carrier | stamp |
|---|---|---|---|
| A | Material | `GetMaterial()` | `MaterialRevision()` (content hash, `:146`) - exists now |
| B | `Mesh::vertexColor` | in mesh (valid iff size == verts*3, `HasVertexColor`) | `MeshRevision` |
| C | `Particle::r/g/b` + `hasColor` | in cloud | `PointCloudRevision` |
| D | `InstanceColors()` | concrete on `InstanceOnPointsNode` only, not in interface | via instancer |
| E | textures | `GetSurfaceTexture`/`GetMaterialTexture` | `SurfaceTextureRevision` |
| F | `GetMappingTransform()` | side | `MappingRevision()` - exists now |
| G | `InstanceSelection()` + `InstanceTransformOverride()` | side | `InstanceSelectionRevision` |

Shader product: `base = toLinear(uBaseColor) * vInstanceColor * vVertexColor`, then
`*= texture` when `uHasTexture` (`Geometry3DNodes.cpp:643-644`). Cloud draws feed
`p.r/g/b` into `vInstanceColor` and still multiply `material.color` and the surface
texture (`:1936-1945, 2016`).

## Roster: 30 implementers

Base-class grep over `src/`: 30 user-facing node classes (the old file said
"28" over a 29-row table; the prompt said 29). New since then: `DepthProjectionNode`
(`DepthProjectionNode.h:26`). Excluded: self-test probes in `main.cpp`; `PathNode`,
`GeometryTableNode` are consumers only (IModulator).

## 1. Emission per node, per mode

| Node | Cite | Emission | Mode dependence | Fwd cloud/curve from input | S |
|---|---|---|---|---|---|
| Geometry | `Geometry3DNodes.h:343`, `.cpp` | mesh:surface | shape param only | no input | V |
| Text3D | `Text3DNode.h:25` | mesh:surface | none | no input | V |
| Ocean | `OceanNode.h:24` | mesh:surface | none | no input | V |
| ModelSource | `ModelSourceNode.h:27` | mesh:surface (empty if bypassed/unloaded) | none | no input | V |
| AudioRibbon | `AudioRibbonNode.h:33` | mesh:surface | none | no input | V |
| Curve | `CurveNode.h:28,36` | mesh:surface (tube) **+ curve** | none | no input | V |
| FieldPrimitive | `FieldPrimitiveNode.h:106,119,122`; `.cpp:877-1121` | **mesh only.** topology Points(0) = mesh:verts (`.cpp:1106-1121`); Plane/Sphere/Cylinder/Torus/Disc = mesh:surface. cloud/curve hardcoded `nullptr` | topology only | no input | V |
| FieldElement | `FieldElementNode.h:146-175`; `.cpp:541-666` | input wired: input's mesh with vertices rewritten (indices copied verbatim, or triangles referencing cut verts dropped when `maxElements` truncates, `ElementStore.cpp:155-187`). No input: **originates** `generateCount` verts, no indices = mesh:verts (`.cpp:584-597`). Input with `vertices.empty()` (cloud/curve-only) clears own mesh (`.cpp:569-581`) | wired vs unwired | **cloud + curve forwarded live and unmodified** (`.h:171-175`); kernel never touches the cloud | V |
| GeometryOp | `GeometryOpNodes.h:26`; `.cpp:200-270,523-577` | mesh (op result; kTransform on instancer input leaves stamp mesh, moves via group matrix) | 13 ops | cloud: kTransform transforms it (`.cpp:530-564`), other ops forward as-is; **curve forwarded UNTRANSFORMED always** (`.h:112`) | V |
| Displacement | `.cpp:611-630` | mesh (same topology, normals recomputed) | none | **neither** (no override) | V |
| AudioDisplacement | `.cpp:260-476` | mesh (subdivided, displaced) | 5 modes | **neither** (`.h` has none) | V |
| InstanceOnPoints | `GeometryOpNodes.cpp:730-733` | mesh = the **stamp** (`instanceShape->GetMesh()`), N placements carried separately | none | n/a (consumes cloud) | V |
| SetColor ("Set Vertex Color", `main.cpp:6119`) | `GeometryOpNodes.cpp:1226-1334` | mesh or cloud (whichever the input has, recoloured) | none | cloud yes (`.h:763`); **curve NO** | V |
| Wrap | `.cpp:995-1034` | mesh (source, world-space baked) | 3 modes | **neither** | V |
| Null3D | `UtilityNodes.h:187,195` | passthrough mesh+cloud+curve | none | **both (fixed)** | V |
| Material | `UtilityNodes.h:290,298` | passthrough | none | **both (fixed)** | V |
| Mapping | `UtilityNodes.h:468,476` | passthrough | none | **both (fixed)** | V |
| Switcher3D | `Switcher3DNode.h/.cpp` (full read) | active input's mesh | 4 slots | **NEITHER** (no `GetPointCloud`/`GetCurve` anywhere in the class) | V |
| Join | `UtilityNodes.cpp:257-387` | mesh:surface (inputs with `src.Empty()` skipped, `:266,316`) | merge / other modes | neither | V |
| MetaBall | `Mesh.cpp:4036-4110` | mesh:surface; UVs are per-triangle constants (0,0)/(1,0)/(0,1) (`:4001-4003`) | none | neither | V |
| MeshToPoints | `UtilityNodes.h:809`; `.cpp:653-668` | **cloud + mesh:none** (D5 fixed; GetMesh returns empty unless bypassed) | 3 sample modes | neither | V |
| MergeByDistance | `PointDistributionNodes.cpp:531-541` | mesh | none | **neither** | V |
| DistributeOnFaces | `PointDistributionNodes.h:39`; `.cpp:171-178` | cloud + mesh:none | none | neither | V |
| PointsToVertices | `.cpp:268-343` | mesh:verts (cloud path) or copy of input verts | cloud vs mesh input | neither | V |
| DistributeInGrid | `.h:264`; `.cpp:453` | cloud + mesh:none; terminal | none | no input | V |
| ParticleSystem | `SimulationNodes.h:26,48` | cloud + mesh:none; terminal | none | no input | V |
| Cloth | `SimulationNodes.h:125-176` | mesh (simulated); returns early with **no mesh** if input `Empty()` (`.cpp:294`) | none | **cloud + curve forwarded** (`.h:163-171`) | V |
| MeshResynth | `GenerativeNodes.h:47-111` | mesh (generational copy) | none | **neither** | V |
| ImageToPoints | `GenerativeNodes.h:186-219`, `.cpp:338-359` | cloud + mesh:none (`MeshRevision()` constant 0); terminal (ImageCable) | none | no input | V |
| DepthProjection (new) | `DepthProjectionNode.h:82-103`, `.cpp:141-156, 478-601` | outputType 0 = cloud + mesh:none; outputType 1 = mesh:surface (edge-torn grid, may be verts-only if all triangles torn) + cloud null. Bypass or no depth input = nothing | **outputType** | no input | V |

FieldPrimitive resolved: not mode-dependent across domains. `GetPointCloud()`/
`GetCurve()` always null (`.h:119,122`); topology toggles only mesh:verts vs mesh:surface.

## 2. Pin requirements

"Gate" = the code that actually rejects/degrades. "Warn" = `CookWarning` wiring.

| Pin | Requires | Gate (verified) | verts-only input | cloud/curve input | Warn |
|---|---|---|---|---|---|
| MeshResynth.input | surface | `GenerativeNodes.cpp:190` warn; `ApplyGeneration` returns on `vertices.empty()` (`:72`) | runs; Smooth/Extrude/RecalculateNormals see no faces, normals zeroed (`Mesh.cpp:1782`) | mesh copy empty, nothing | kMeshSurface (`:190`) |
| MetaBall.cloudSource | cloud, reads `px/py/pz/scale` only, not colour | `UtilityNodes.cpp:755` | n/a | curve: nothing | yes (per earlier read) |
| InstanceOnPoints.cloudSource | cloud; **wins over pointSource** | `GeometryOpNodes.cpp:802-820` | n/a | - | yes (cloud) |
| InstanceOnPoints.pointSource | mesh; **mode 0 needs only vertices, modes 1/2 need faces** | gate `!HasGeometry()` (`:830`); `ToPoints` mode 0 `Mesh.cpp:2470-2540`, edges `:2541-2652`, faces `:2653-2693` | mode 0 works; modes 1/2 yield 0 instances, silently | 0 instances | **none** |
| InstanceOnPoints.instanceShape | any mesh | `GeometryOpNodes.cpp:730-746` | draws verts-only stamp as nothing (Render needs faces) | empty stamp | none |
| Cloth.input | **surface** | `SimulationNodes.cpp:294` `srcLocal.Empty()` returns, no output; constraints from `src.indices` | **no output at all** (old doc said "unconstrained particles": wrong) | no mesh output (cloud/curve forwarded) | **none** |
| Displacement.input | surface (no gate) | `GeometryOpNodes.cpp:620-624` -> `MeshOps::Displace` (`Mesh.cpp:2369-2426`) always ends in `RecalculateNormals` | smooth: all normals zeroed (`Mesh.cpp:1782`); **flatShade: result is an empty mesh** (`:1704-1749` builds from indices) | mesh empty | **none** |
| AudioDisplacement.input | surface | `.cpp:268` warn, `:275` gate on `vertices.empty()` | runs, ends in `RecalculateNormals` (`:470`): normals zeroed / flat = empty | empty | kMeshSurface (`:268`) |
| Wrap.sourceInput | surface | `Mesh.cpp:3043-3045` `worldSource.Empty()` returns source unchanged | **returned un-wrapped** (but world-baked) | empty | none on source |
| Wrap.targetInput | surface (nearest); optional in bend modes | `Mesh.cpp:3048` `target.Empty()` -> unchanged unless bend + `radiusOverride>0`; nearest loop uses `FaceCount()` (`:3133`) | treated as absent (`:3061` `worldTarget.Empty()` -> uses override) | treated as absent | kMeshSurface (`.cpp:1003`); **misclassifies verts-only as "nothing"** |
| Join inputs 1-4 | surface to contribute | `UtilityNodes.cpp:266,316` `Empty()` skip | **silently skipped** | skipped | none |
| GeometryOp.input | any (no gate at `:200-211`); kTransform/kArray are pure vertex ops | no per-op gate at pin | kTransform keeps verts-only intact | mesh empty; cloud handled `:523` | none |
| MergeByDistance.input | surface per warning, but op body only needs vertices | `Mesh.cpp:612-663` (`vertices.empty()` gate; faces loop optional) | works (welds verts) but node warns anyway | mesh empty | kMeshSurface |
| MeshToPoints.input | mode 0 vertices; modes 1/2 faces | `UtilityNodes.cpp:550-573`, `ToPoints` as above | mode 0 works, 1/2 emit 0 points | 0 points | **none** |
| DistributeOnFaces.input | surface | `Mesh.cpp:2760` `faceCount==0` returns empty | 0 points | 0 points | kMeshSurface |
| PointsToVertices.input | cloud preferred, else mesh vertices | `PointDistributionNodes.cpp:268-305` | works (copies verts) | cloud path | none |
| Path.curve | curve | `PathNode.cpp:25-37` | - | - | kCurve |
| Path.geometry (mesh-follow) | surface | `BoundaryLoops`/`SliceContours` over indices (`PathNode.cpp:96-101`) | no loops, no follow | no follow | none |
| GeometryTable.geo | any | cloud `:56,82`, curve `:64,170`, mesh `:103-109,190-196` | mesh mode reads `FaceCount()` (`:132`) | supported | n/a |
| SetColor.input | any | `GeometryOpNodes.cpp:1226-1227` | passes | cloud recoloured; curve **dropped** | none |
| Material / Mapping / Null3D | any | passthrough | passes | passes (fixed) | n/a |
| Switcher3D.geoA-D | any | active slot | passes | **cloud/curve dropped** | n/a |
| FieldElement.input | any with `vertices` (else clears) | `.cpp:566-581` | runs, indices copied | kernel skips cloud, cloud forwarded | none |

Warning wiring recap: warnings exist for MeshResynth, MetaBall.cloud, InstanceOnPoints.cloud,
Wrap.target, DistributeOnFaces, MergeByDistance, AudioDisplacement, Path.curve. Missing:
Cloth, Displacement, InstanceOnPoints.pointSource, Join, MeshToPoints, PointsToVertices,
GeometryOp, Path.geometry, Wrap.source.

## 3. Side channels A-G per node

`fwd` forwards, `orig` originates, `drop` not forwarded, `xform` transforms, `-` n/a.

| Node | A Material | B vertexColor | C particle colour | D InstanceColors | E textures | F Mapping | G Selection |
|---|---|---|---|---|---|---|---|
| Geometry / Text3D / Ocean / ModelSource | orig | never set (none write it) | - | - | orig, own `mTextureInput`, albedo only (default routing of `GetMaterialTexture`) | **drop** (default identity) | - |
| AudioRibbon | orig | cleared (`.cpp:132`) | - | - | `GetSurfaceTexture` hardcoded 0 (`.h:38`) | drop | - |
| Curve | orig | none | - | - | none (no override) | drop | - |
| FieldPrimitive | `Material()` default (`.h:109`) | ToMesh: only if kernel wrote `cd` | - | - | 0 | identity | nullptr |
| FieldElement | fwd | fwd if input has it, else empty; kernel-written `cd` overrides (`ElementStore.cpp:129-152`) | cloud forwarded untouched | fwd | fwd | fwd | fwd |
| GeometryOp | orig if `!inheritMaterial` else fwd | rides in MeshOps | xform for kTransform only | fwd | fwd | fwd | fwd; kTransform+selectionOnly writes transform override (`.cpp:240-258`) |
| Displacement | orig/fwd (`inheritMaterial`) | rides in `out = in` copy (flat: RemapVertexColor) | - | fwd | fwd | fwd | fwd |
| AudioDisplacement | orig/fwd | rides (`mCache = src`) | - | fwd | fwd | fwd | fwd |
| InstanceOnPoints | fwd from instanceShape | - | reads `p.r/g/b` into D | **orig** `mColors`; **always filled** (white default too) (`.cpp:815-817,884-886`) | fwd from instanceShape | **fwd now** (`.h:611-614`, Phase 5); **`MappingRevision` not overridden -> 0** | orig/fwd |
| SetColor | fwd | **orig** | **orig** | fwd | fwd | fwd | fwd |
| Wrap | fwd from source | rides (struct copy) | - | fwd source | fwd source | fwd source | fwd source |
| Null3D / Material / Mapping | fwd; Material **orig** | fwd | fwd (cloud) | fwd | fwd; Material orig per-map | fwd; Mapping **orig** | fwd |
| Switcher3D | fwd active | fwd | **drop** (no cloud) | fwd | fwd | fwd | fwd |
| Join | picks via `materialFrom` | **orig only when some input has authored colour or albedos differ and `keepInputColours`** (`.cpp:274-293`) | - | consumed (realized) | one input | one input | - |
| MetaBall | orig | none | reads xyz/scale only | - | **drop** (default); UVs would be per-triangle stamps so a texture is meaningless | drop | - |
| MeshToPoints | fwd tint (`mBuiltMaterialRev` in dirty check) | **-> C** | **orig** from mesh/instance colours | consumed | fwd | fwd | - |
| MergeByDistance | fwd | remapped (`Mesh.cpp:661`) | - | fwd | fwd | fwd | fwd |
| DistributeOnFaces | fwd | -> C | **orig**, bakes inherited tint | - | fwd | fwd | - |
| PointsToVertices | fwd | **orig** from cloud r/g/b | reads | - | fwd | fwd | - |
| DistributeInGrid | orig | - | orig | - | 0 | - | - |
| ParticleSystem | - | - | **orig** (`hasColor` true, `.cpp:135-136,198`) | - | - | - | - |
| Cloth | fwd | rides | forwarded | **drop, deliberate**: instancer baked by `RealizeInstances` at rest build (`.cpp:286-288`); nothing left to select/group-move | **`GetSurfaceTexture` only; per-map `GetMaterialTexture` not forwarded** | fwd | drop (baked) |
| MeshResynth | fwd | rides | - | fwd | fwd | fwd | fwd |
| ImageToPoints | orig tint | - | **orig** (image*tint) | - | 0 (deliberate) | - | - |
| DepthProjection | orig (tint, metallic, roughness, opacity) | **orig in mesh mode** (sample colour * tint) | **orig in cloud mode** | - | colour-input texture (`.cpp:183-188`) | identity | - |

### Deliberate vs oversight (prompt's three anomalies)

| Omission | Verdict | Evidence |
|---|---|---|
| InstanceOnPoints F | **Was oversight, now FIXED** (forwarded); residual: no `MappingRevision` override | `GeometryOpNodes.h:611-614` |
| MetaBall E/F | **Deliberate, defensible** | UVs are per-triangle constants (`Mesh.cpp:4001-4003`), no meaningful surface parameterisation |
| Cloth G/D (PASS) | **Deliberate** | `SimulationNodes.cpp:286-288` bakes instancer into the sim mesh |

## 4. Anomalies (real-bug candidates, current source)

| # | Where | Finding | Severity |
|---|---|---|---|
| 1 | Join `keepInputColours` (`UtilityNodes.h:576,615`, `.cpp:233,293,382`, `main.cpp:26360`) | The doc, D4 and commit df55bf1 said it was removed; merge 906acfa put it back. Default true. | Regression of a decided design |
| 2 | Join `anyAuthoredColour` (`.cpp:274-276`) | Counts any instancer input as authored colour, but `InstanceOnPoints` always fills `mColors` (white default), so instancer -> Join bakes colour and neutralises albedo: the original manufactured-colour bug via a new door | High, confirm against `RealizeInstances` (`Mesh.cpp:499-523`, it only multiplies/creates colour when `instanceColors` given) |
| 3 | GeometryOp kTransform (`.h:108-113`, `.cpp:229-269`) | Mesh and cloud are transformed, **curve is forwarded untransformed**. Also `selectionOnly` transforms the whole cloud (`.cpp:530-564` has no mask) | High |
| 4 | DistributeOnFaces (`PointDistributionNodes.cpp:56-62,149`) | Bakes inherited material tint into particle colour but dirty check omits `MaterialRevision`; goes stale on upstream colour change. MeshToPoints does include it | Medium |
| 5 | `DescribeGeometryMismatch` (`Geometry3DNodes.h:296-297`) | `hasVerts = !Empty()` == "has surface". mesh:verts reports "got nothing" for kMeshSurface, and would falsely fail kMeshVertices. `kMeshVertices` is unused so latent | Medium |
| 6 | Displacement/AudioDisplacement on verts-only | Normals zeroed; with flatShade the mesh vanishes. No warning on Displacement | Medium |
| 7 | DepthProjection (`.cpp:406-408,500-503,541-543`, `GetSurfaceTexture :183-188`, `GetMaterial :168`) | Colour texture is baked into `p.r/g/b`/`vertexColor` **and** returned as surface texture, and `tint` is baked **and** is `material.color`. Shader multiplies all (`Geometry3DNodes.cpp:643-644,2016`): texture applied twice, tint squared. ImageToPoints removed only the texture half (`GenerativeNodes.h:212-218`) and still squares tint (`.cpp:324-326` + `GetMaterial :365`) | Medium |
| 8 | Switcher3D | Still drops cloud/curve. The old "wrapper drops cloud/curve" finding is fixed for Null3D/Material/Mapping only | Medium |
| 9 | SetColor | Still no `GetCurve()` | Low |
| 10 | Displacement, AudioDisplacement, MergeByDistance, Wrap, MeshResynth, Join | None forward cloud/curve; contradicts the old doc's propagation table for Displacement/MergeByDistance | Low-Medium |
| 11 | Cloth | `GetMaterialTexture` not forwarded (per-map textures lost) | Low |
| 12 | InstanceOnPoints | `MappingRevision()` defaults to 0 | Low |
| 13 | FieldElement | Kernel is silently skipped on a cloud input (no warning) | Low |
| 14 | MergeByDistance | Warns kMeshSurface but op works on verts-only | Low |

## 5. Claims in the prompt / older doc that turned out wrong

| Claim | Reality |
|---|---|
| "29 implementers" / "28" | 30 (DepthProjection added); old doc listed 29 rows under a "28" heading |
| Null3D/Material/Mapping drop cloud/curve | Fixed (`UtilityNodes.h:187-195,290-298,468-476`). Switcher3D and SetColor.curve still open |
| mesh:standin producers (MeshToPoints, DistributeOnFaces, DistributeInGrid, ImageToPoints) | All return an honestly empty mesh now |
| Material and mapping have "no revision stamp" | Both exist (`MaterialRevision`, `MappingRevision`) |
| Cloth on verts-only degrades to unconstrained particles | Returns early, no output (`SimulationNodes.cpp:294`) |
| Displacement, MergeByDistance "forward all 7 channels" | Seven side channels yes; cloud/curve no |
| InstanceOnPoints drops MAP | Forwarded now |
| "Set Color" | Node is "Set Vertex Color" (`main.cpp:6119`) |
| `keepInputColours` removed | Present (anomaly 1) |
| FieldPrimitive "mesh+cloud+curve, mode-dependent" | Mesh only; verts vs surface |
| Wrap.target "needs real triangles" | True for nearest; bend modes work without a target (radius override), verts-only target is treated as absent |
| All cited line numbers | Drifted; replaced above |

## 6. What I did NOT find or verify

- No implementer emits `mesh:standin` (grep of GetMesh bodies; D5).
- No `kMeshVertices` caller (grep): the verts-only classification bug is latent.
- No warning wiring for the nine pins listed under "Missing".
- `GeometryOp` ops other than kTransform/kArray were not traced for verts-only input (the only channel-B-relevant claim is that kTransform is a pure vertex op).
- `MeshOps::Subdivide` on a face-less mesh not traced past the entry (`Mesh.cpp:719-750`); AudioDisplacement's verts-only result is bounded by `RecalculateNormals` only.
- Render3D / NodeViewport mesh-vs-cloud preference code not re-read; relied on `GenerativeNodes.cpp:340-349` comment plus the cloud draw path (`Geometry3DNodes.cpp:1936-2016`).
- Per-map UV/mapping (no per-map mapping concept found; one `GetMappingTransform()` for all maps).
- No Windows/Linux angle: this audit touches no `Platform::` code (`GeometryNode` model loading is unchanged).
- Did not run any sweep (`TRANSFORMSWEEPTEST` etc.); no code executed.

## 7. Living-map entry to add to `codebase-navigation` (recommended, not applied)

Replace the "IGeometrySource passthrough wrappers silently drop GetPointCloud()/GetCurve()"
entry: Null3D/Material/Mapping now forward both (ebf4f67); still open are Switcher3D
(all) and SetColor (`GetCurve`), and Displacement/AudioDisplacement/Wrap/MergeByDistance/
MeshResynth/Join never forward cloud/curve. Add: **`Mesh::Empty()` means "no surface", not
"no vertices"; use `HasGeometry()` for vertices**; and **Join's `keepInputColours` was
resurrected by merge 906acfa**.

---

> **Stale banner (2026-09-30):** Phase 2/3 below are the original 2026-09-09 text. Since then Phase 4/5 landed (D1 warnings, D5 empty stand-ins, passthrough cloud/curve for Null3D/Material/Mapping/Cloth, MaterialRevision/MappingRevision). Where they disagree with sections 1-6 above, the sections above win.

# Phase 2 — the rule matrix

Phase 2 deliverable of `docs/plans/geometry/domain-audit-prompt.md`. Builds a
legality decision procedure on top of phase 1's data — no enforcement code,
still no connect-time refusal (D1: cook-time only). Every illegal/legal
verdict below is a mechanical consequence of §1 (satisfaction) + §2
(propagation) applied to phase 1's emission/consumption tables, not a
separately hand-written opinion — where a verdict looked wrong against what a
Houdini/Blender user would predict, that is called out as a design finding in
§5, not silently special-cased into the rule.

A node's **tag-set** is every emission tag it simultaneously carries (a node
can emit `mesh:standin` *and* `cloud` at once — they are read through
different accessors, `GetMesh()` vs `GetPointCloud()`, and a pin only ever
calls one of them). Legality is evaluated per accessor-call, not per node.

## 1. Satisfaction rule

Which emission tags satisfy which pin-requirement categories, derived from
every `requires` cell in phase 1's consumer-pin table. Four requirement
categories cover all 22 known consumer pins:

| Requirement category | Example pins | Satisfied by | Reasoning |
|---|---|---|---|
| **`mesh:surface`** (needs real face topology) | MeshResynth.input, DistributeOnFaces.input, Wrap.targetInput, AudioDisplacement.input, MergeByDistance.input | `mesh:surface` only | The consuming code walks `.indices` (nearest-triangle search, area-weighted sampling, subdivision) — no indices, no meaningful result |
| **`mesh:vertices`** (needs positions only, faces optional) | Displacement.input (`.cpp:561`, vertex-level only) | `mesh:surface`, `mesh:verts` | `MeshOps::Displace` never reads `.indices` |
| **`mesh:any`** (terminal/passthrough/operator that degrades rather than refuses on bad input) | GeometryOp.input, Join ×4, Wrap.sourceInput, InstanceOnPoints.instanceShape, Cloth.input (degrades to unconstrained particles, phase 1 verified), PointsToVertices.input (degrades to vertex copy) | **meaningfully**: `mesh:surface`, `mesh:verts`. **Not meaningfully**: `mesh:standin`, `mesh:none` — the pin will not crash (every producer's `GetMesh()` is non-null by construction) but the data is fabricated, not real | This is where D5's fabrication disease actually bites: a "mesh:any" pin fed a `mesh:standin` producer *silently succeeds* today with garbage input, which is the exact shape of the reported Join bug |
| **`cloud`** | MetaBall.cloudSource, InstanceOnPoints.cloudSource, PointsToVertices.input (preferred over mesh) | `cloud` only | Reads `GetPointCloud()` specifically; a producer's mesh-side tag is irrelevant to this call |
| **`curve`** | Path.curve | `curve` only | Reads `GetCurve()` specifically |

`mesh:none` satisfies nothing in any category, including `mesh:any` — it is
`ParticleSystem`'s permanently-empty stub.

## 2. Propagation rule

For a passthrough/operator node, does its own tag-set equal its input's
tag-set? Verified per node in phase 1; the answer is **not uniform**, which
is the load-bearing finding of this whole audit:

| Forwards mesh tag correctly | Forwards cloud tag | Forwards curve tag |
|---|---|---|
| GeometryOp, Displacement, AudioDisplacement, InstanceOnPoints (n/a — root, not a passthrough), SetColor, Wrap, Null3D, Material, Mapping, Join (n/a — merges, doesn't pass one input's tag through), MetaBall (n/a — originates), MergeByDistance, MeshResynth, Switcher3D (of whichever input is active), FieldElement (conditional — see phase 1 row) | GeometryOp, Displacement, AudioDisplacement, MergeByDistance, MeshResynth, SetColor, FieldElement | GeometryOp, Displacement, AudioDisplacement, MergeByDistance, MeshResynth, FieldElement |
| **All passthrough/operator nodes forward the mesh tag correctly** — no exceptions found | **Null3D, Material, Mapping, Switcher3D drop it entirely** (no `GetPointCloud()` override at all) | **Null3D, Material, Mapping, Switcher3D, and SetColor drop it entirely** (no `GetCurve()` override) |

The practical rule: **every node that forwards mesh correctly also forwards
cloud and curve correctly, except five** — Null3D, Material, Mapping,
Switcher3D (cloud + curve), and SetColor (curve only, its cloud forwarding is
fine). This is not the propagation rule the audit prompt assumed ("a
passthrough's tag is its input's tag, or the whole scheme is defeated by one
Null3D") — that statement is **true for mesh, false for cloud/curve on 4 of
Infinite's most commonly inserted utility nodes**. A user who threads a
Particle System through a Null3D for cable tidiness (a completely ordinary
thing to do) silently loses the cloud.

## 3. Derived illegal list

Generated by applying §1 to every producer tag-set × every consumer
requirement category found in phase 1 — not independently re-imagined per
pair. Because legality only depends on (tag-set, requirement category), not
node identity, this collapses to one small decision table plus a
name-substitution step:

| Producer tag-set | → `mesh:surface` pin | → `mesh:vertices` pin | → `mesh:any` pin | → `cloud` pin | → `curve` pin |
|---|---|---|---|---|---|
| `mesh:surface` | legal | legal | legal | **illegal** | **illegal** |
| `mesh:verts` | **illegal** | legal | legal (degraded — see Cloth/PointsToVertices) | **illegal** | **illegal** |
| `mesh:standin` (+ `cloud`) | **illegal** | **illegal** | **illegal (fabrication — the reported-bug shape)** | legal (via the co-emitted `cloud` tag) | **illegal** |
| `mesh:none` (+ `cloud`) | **illegal** | **illegal** | **illegal** | legal (via the co-emitted `cloud` tag) | **illegal** |
| `cloud`-only accessor call | **illegal** | **illegal** | **illegal** | legal | **illegal** |
| `curve`-only accessor call | **illegal** | **illegal** | **illegal** | **illegal** | legal |

Instantiated against real node/pin pairs, the illegal connections that exist
in the graph today (all currently silent — no cook-time error yet, per D1
this is the backlog phase 4.4 must close):

- **MeshToPoints / DistributeOnFaces / DistributeInGrid / ImageToPoints
  (`mesh:standin`) → any `mesh:surface` pin** (MeshResynth.input,
  DistributeOnFaces.input, Wrap.targetInput, AudioDisplacement.input,
  MergeByDistance.input) — the fabricated billboard quads get subdivided,
  wrapped-onto, merged, etc. as if real.
- **MeshToPoints / DistributeOnFaces / DistributeInGrid / ImageToPoints
  (`mesh:standin`) → any `mesh:any` pin** (GeometryOp.input, Join ×4,
  Wrap.sourceInput, InstanceOnPoints.instanceShape) — this is the exact
  mechanism the reported bug's fix touched: Join reading a `mesh:standin` (or
  any mesh lacking real `HasVertexColor()`) as if it were authored geometry.
- **ParticleSystem (`mesh:none`) → any `mesh:surface`/`mesh:any` pin** — same
  shape, currently silent (produces an empty result rather than an error).
- **PointsToVertices (`mesh:verts`) → any `mesh:surface` pin** (MeshResynth,
  DistributeOnFaces, Wrap.targetInput, AudioDisplacement, MergeByDistance) —
  these pins need `.indices`; `mesh:verts` never has any.
- **Any mesh-only producer → MetaBall.cloudSource / InstanceOnPoints.cloudSource
  / Path.curve** — a `mesh:surface`/`mesh:verts` producer has no cloud/curve
  accessor to satisfy these at all (not merely fabricated data — genuinely
  absent).
- **Cloud/curve-through-broken-passthrough (newly found this phase, not in
  the original illegal list, and arguably should be *legal*):**
  `MeshToPoints → Null3D → InstanceOnPoints.cloudSource` (or `.cloudSource`
  on MetaBall, or `.input` on PointsToVertices) currently fails — not because
  the data is fabricated, but because `Null3D` silently drops the cloud tag
  in transit. Unlike the fabrication cases above, this chain is **not**
  reading garbage — the cloud data is real and just doesn't survive the
  passthrough. This belongs on the legal list once §2's Null3D/Material/
  Mapping/Switcher3D/SetColor gap is fixed (phase 4), not on the permanent
  illegal list.

## 4. Legal list that must not regress

Chains confirmed legal by §1+§2 today, to be protected by phase 5's fuzzer
and widened sweeps:

- `MeshToPoints → Render3D` — `cloud` satisfies Render3D's `any` terminal pin
  via the co-emitted tag; Render3D's own logic *prefers* cloud over mesh
  (phase 1: `Geometry3DNodes.cpp:1966`), so the standin mesh is correctly
  never drawn.
- `MeshToPoints → InstanceOnPoints.cloudSource` — `cloud` satisfies `cloud`
  directly, no passthrough involved.
- `MeshToPoints → GeometryTable` — `GeometryTable.geo` accepts any of
  mesh/cloud/curve (verified phase 1), so both the standin mesh and the real
  cloud are visible to it; not itself a bug since GeometryTable's contract is
  "any."
- `PointsToVertices → GeometryOp` — `PointsToVertices` emits `mesh:verts`;
  `GeometryOp.input` is a `mesh:any` pin, so this is legal today, though
  degraded (ops that need faces, e.g. subdivision variants, silently produce
  nothing changeable since there are no faces to subdivide — a `mesh:any`
  pin accepting `mesh:verts` "legally" does not mean every operation on it
  is meaningful; that distinction belongs to per-op contracts, out of scope
  for this phase).
- Every `mesh:surface → mesh:surface` passthrough chain (GeometryOp,
  Displacement, AudioDisplacement, MergeByDistance, MeshResynth, Wrap.source,
  Material, Mapping — mesh side only for the last two).
- `Displacement.input` accepting `mesh:verts` — Displacement is the one
  `mesh:surface`-labelled-in-the-original-prompt pin that actually only needs
  `mesh:vertices` per §1's category, so `FieldPrimitive(topology=Points) →
  Displacement` is legal and meaningful today (moves the points), which a
  strict-`mesh:surface` reading of the original prompt's consumer table would
  have wrongly flagged as illegal.

## 5. Chain cases (minimum depth 3)

| Chain | Verdict | Why |
|---|---|---|
| `MeshToPoints → Null3D → Cloth` (the prompt's named laundering case) | **Illegal, and correctly stays illegal** — not laundered. `Null3D` forwards the *mesh* tag faithfully (`mesh:standin` in, `mesh:standin` out); `Cloth.input` is a `mesh:any`-degrading pin per §1, but `mesh:standin` still satisfies nothing under the fabrication rule. §2 confirms Null3D does not defeat this — the mesh side of Null3D has no gap. Cloth would build zero PBD constraints from the fabricated quads (no shared topology) and simulate degenerate floating billboards. **This is the case D1's cook-time error should catch first**, since the failure mode is a silent, visually-confusing near-no-op rather than a crash. |
| `MeshToPoints → Null3D → InstanceOnPoints.cloudSource` | **Currently illegal but shouldn't be** — see §3's newly-found gap. This is the real laundering-adjacent bug: not fabricated-data-through-a-tag-preserving-passthrough (which correctly stays blocked), but real-data-silently-dropped-by-a-tag-*breaking*-passthrough that claims to forward everything. |
| `PointsToVertices → GeometryOp(kSubdivide) → MeshResynth.input` | **Illegal at the second hop.** `PointsToVertices` emits `mesh:verts`; `GeometryOp.input` accepts it (`mesh:any`) and forwards the mesh tag unchanged (§2) — `kSubdivide` on an index-less mesh is a no-op (confirmed phase 1: no `GeometryOpNode` op strips or adds indices to an empty index buffer in a way that would fix this). So the mesh reaching `MeshResynth.input` (`mesh:surface`-required) is still `mesh:verts` — illegal, and would currently fail silently rather than error. |
| `DistributeOnFaces → SetColor → InstanceOnPoints.pointSource` | **Legal.** `DistributeOnFaces` emits `mesh:standin`+`cloud`; `SetColor` forwards both tags correctly (§2, cloud is fine, only curve is broken) and originates fresh colour into both; `InstanceOnPoints.pointSource` accepts `mesh:surface`-shaped input per phase 1 (`HasGeometry()`, not full topology) — `mesh:standin` does have vertices, satisfying `pointSource`'s actual (looser than `mesh:any`) requirement. This is a case where the standin mesh, despite being fabricated for *rendering*, is legitimately usable as *point positions* — the fabrication rule in §1 is about the `mesh:any`/`mesh:surface` categories specifically, not a blanket ban; `pointSource` was independently verified in phase 1 to need `HasGeometry()` only. |
| `CurveNode → Material → Path.curve` | **Illegal at the second hop, and silently so.** `CurveNode` emits `mesh:surface` **and** `curve` simultaneously. `Material` forwards mesh correctly but has no `GetCurve()` override (§2) — so `Path.curve`, which needs `curve` specifically, sees `nullptr` even though the original `CurveNode` had a perfectly good curve two hops upstream. A user inserting a Material node to tint a curve's rendering (an entirely reasonable thing to want) silently breaks the Path chain feeding off the same curve elsewhere in the graph. |
| `ImageToPoints → Switcher3D → MetaBall.cloudSource` | **Illegal at the second hop, silently.** Same shape as the CurveNode/Material case — `Switcher3D` forwards mesh (of whichever input is active) but never cloud, so `MetaBall.cloudSource` sees nothing regardless of which Switcher3D slot is active. |

Would a Houdini/Blender user predict these verdicts? The two "correctly
illegal" rows (Cloth-laundering, PointsToVertices-into-Subdivide) match
expectation — cook-time error, not a dropped cable. The three
"illegal-because-of-a-forwarding-gap" rows (Null3D/cloud, Material/curve,
Switcher3D/cloud) would **not** be predicted by a Blender user, since
Blender's component-passthrough model (cited in phase 1's prior-art table)
guarantees exactly the property these five nodes violate: "a mesh-only node
leaves other components untouched." **These five rows are design findings,
not accepted rules** — phase 4.5 (component passthrough) is the fix, not a
cook-time error message that would just make the silent failure loud.

---
---

# Phase 3 — the name-contract audit

Phase 3 deliverable of `docs/plans/geometry/domain-audit-prompt.md`. Per
node (and mode, where a mode changes the promise), what the name tells a
user to expect, what the code actually does, and a verdict. Per goal 1, a
real mismatch resolves to **rename** or **remove** — never a documentation
note. A mismatch that is a *completeness bug* (the name is accurate, an
implementation gap makes the behavior fall short of it) is **not** a
naming verdict — it's tagged "name honest, code gap" and pointed at its
phase 4 item instead, so this table doesn't relitigate phase 1/2's findings
under a different heading.

**The rule, extracted from the plan:** when a node needs a parameter to stop
it destroying data, or a comment to explain why its output isn't what its
name says, the name is wrong — not the docs.

**Bar-setters already in the repo** (cited by the plan as the standard to
match): `Points to Vertices` (emits a faceless mesh and its own class
comment says so), `Distribute Points on Faces` (name matches its principal
behavior exactly), MetaBall's `cloud` pin (labelled for what it actually
requires, not a generic "geometry" label).

## Per-node verdicts

| Node (mode) | Name promises | Actual behavior | Verdict |
|---|---|---|---|
| Geometry | a primitive shape generator | matches | **honest** |
| Text3D | 3D text mesh | matches | **honest** |
| Ocean | wave-simulated surface | matches (Gerstner) | **honest** |
| Model Source | loads an external model file | matches | **honest** |
| Audio Ribbon | ribbon mesh driven by audio | matches | **honest** |
| Curve | a curve | emits a **tube mesh** (`mesh:surface`) as well as `curve` — but `radius`/`sides`/`taper`/`segments` are all real, visible `VisitParams` controls (`CurveNode.h:44,52,89-95`, verified this pass), matching the standard DCC convention of a curve object with an adjustable bevel/thickness (e.g. Blender's Curve `Bevel Depth`) | **honest** — the mesh is a controllable, advertised feature, not a silent surprise |
| GeometryOp (all 10 ops) | a generic multi-purpose mesh operator | `OpNames()` is a public accessor (`GeometryOpNodes.cpp:14,96`, verified) feeding the node body's own dropdown, so the active op's name is always visibly surfaced — "GeometryOp: Subdivide" is not the same promise as bare "GeometryOp" | **honest**, same pattern as any multi-mode utility node whose UI shows the live mode |
| Displacement | displaces a mesh via a texture | matches | **honest** |
| Audio Displacement | displaces a mesh via audio | matches | **honest** |
| InstanceOnPoints | instances a shape onto points | matches for what it does; silently drops upstream Mapping from `instanceShape` (§2/phase 1) | **name honest, code gap** — phase 4 item, not a rename (the node does instance shapes onto points; it just forgets one side-channel while doing it) |
| **Set Color** | sets "the" colour | **collides with Material**, which also claims to set "colour" (albedo) — the two combine multiplicatively (`base = uBaseColor * vInstanceColor * vVertexColor`, `Geometry3DNodes.cpp:577`) so "Set Color red + Material red = dark red," which nothing in either name warns about | **RENAME (D3, already decided).** Proposed name: **`Set Vertex Color`** — states which of the two "colour" concepts it actually writes (channel B, not channel A). Final name is an open question for the user (see below) — this is a proposal, not a decision. |
| Wrap | wraps source mesh onto target surface | matches | **honest** |
| Null3D | "everything is forwarded" (its own header comment, `UtilityNodes.h:131-133`) | drops `GetPointCloud()`/`GetCurve()` entirely (§2) — the comment makes an explicit false claim | **name honest ("Null" = do-nothing passthrough is a standard DCC term), comment is wrong and code has a gap** — phase 4 fix (forward cloud/curve), and delete or correct the false "everything is forwarded" comment as part of that fix, not a rename of the node |
| Material | sets material (albedo/roughness/etc.) | matches for what it sets; **has no revision stamp on channel A** (phase 1), so a downstream cache can silently show a stale material after this node changes it — "sets material" is true the instant it runs, unreliable afterward | **name honest, code gap (phase 4.1 — add `MaterialRevision()`)**, not a rename. This is the node the plan's "an effect the graph has no channel to observe" line refers to — the missing observability is the revision stamp, not the node's name. |
| Mapping | applies a UV/mapping transform | matches for mesh; drops cloud/curve passthrough (§2) | **name honest, code gap** — same shape as Null3D |
| **Join** ("Join Geometry") | combines multiple geometry inputs | matches; but the `keepInputColours` parameter defaults imply a mode where merging **destroys** input colour — the existence of an opt-in "don't destroy my data" checkbox is itself the tell | **REMOVE `keepInputColours` (D4, already decided).** A merge should always preserve; no rename of "Join" needed — the node's core promise (combine geometry) is accurate, only the lossy-by-default colour param violates it. |
| MetaBall | generates a metaball/blob surface from a point cloud | matches; pin is honestly labelled `cloud`, not generic `geometry` (bar-setter, cited above); deliberately drops mapping/texture (no meaningful UVs on marching-cubes output, phase 2 — defensible, not a naming issue) | **honest** |
| **Mesh to Points** | converts a mesh to points | `GetPointCloud()` matches; but `GetMesh()` **also** returns a fabricated billboard-quad mesh (`mesh:standin`) that a downstream `mesh:any`/`mesh:surface` pin will silently accept as if real (the mechanism of the reported bug) | **name honest for the cloud output; the mesh output is the actual defect.** Resolution is **D5 (already decided): split the preview accessor from the data accessor** so `GetMesh()` can be honestly empty — not a rename, since "Mesh to Points" never promised to also emit a usable mesh. |
| MergeByDistance | welds vertices within a distance threshold | matches; cleanest passthrough found in the whole audit (§side-channel table, no gaps) | **honest** |
| Distribute Points on Faces | scatters points across a mesh's faces | matches (bar-setter, cited above); shares the identical `mesh:standin` fabrication as Mesh to Points | **name honest; shares D5's fix** — the fabrication bug is uniform across all four `mesh:standin` producers (this node, Mesh to Points, Distribute Points in Grid, Image to Points), so D5's split-accessor fix applies to all four, not just the one the plan named |
| Points to Vertices | converts points to a faceless vertex mesh | matches, documents its own facelessness in-code (bar-setter, cited above) | **honest** |
| Distribute Points in Grid | generates a point grid | matches; same shared `mesh:standin` fabrication as above | **name honest; shares D5's fix** |
| Particle System | a particle simulation | `GetMesh()` returns a **permanently-empty stub** (`static Mesh empty`, phase 1, verified) rather than a fabricated standin — the most honest of the four/five cloud-emitting producers, since it doesn't fabricate anything at all | **honest — and arguably the template D5's fix should converge the other four toward** (empty, not billboard quads) |
| Cloth | cloth simulation | matches; realizes (bakes) any upstream instancer into one concrete simulated mesh before solving (phase 2, confirmed deliberate) — the same behavior a Houdini/Blender cloth solver has on instanced geometry, not a surprise to a DCC user | **honest** — recommend an in-code comment on `RebuildFromInput` stating this is why instancing doesn't survive Cloth (a comment justified by physics, not covering for a naming gap — consistent with the plan's rule since the *reason* is legitimate, only the *documentation* of it is currently missing) |
| Image to Points | converts image pixels to points | matches; deliberately hardcodes `GetSurfaceTexture()` to 0 specifically to avoid double-applying colour via both `r/g/b` and a texture sample — documented in-code (phase 1, `.h:198-204`) | **honest**; same shared `mesh:standin` fabrication as the other three, shares D5's fix |
| Switcher3D | switches between geometry inputs | matches for mesh (of whichever input is active); drops cloud/curve regardless of active slot (§2) | **name honest, code gap** — same shape as Null3D/Material/Mapping, not a rename |
| FieldPrimitive | a Field-kernel-driven primitive generator | matches — the plan's own premise that it emits cloud/curve depending on mode was the thing that turned out to be false (phase 1); the node itself never claimed that | **honest** |
| FieldElement | a per-element Field kernel modifier of connected geometry (or a standalone generator when nothing is wired in) | matches; `maxElements` is a visible, named budget control (`FieldElementNode.h:198`) and truncation state (`mWasTruncated`) exists to be surfaced in the UI | **honest**, assuming the UI actually surfaces `mWasTruncated` — not independently re-verified this pass whether the node body draws a truncation indicator; flag as a UI-completeness check for phase 5, not a naming verdict |
| MeshResynth | iterative mesh mutation/regeneration | clean forwarding operator (§2, no side-channel gaps found); `OpNames()` accessor mirrors GeometryOp's mode-visibility pattern (`GenerativeNodes.cpp:11,53`) | **honest** |

## Summary — rename/remove list (goal 1 compliant: no documentation-only resolutions)

| Node | Action | Status |
|---|---|---|
| Set Color | **Rename** to `Set Vertex Color` (proposed) | needs user confirmation — open question below |
| Join — `keepInputColours` param | **Remove** the parameter; merge always preserves | decided (D4); implementation is phase 4.3 |
| Mesh to Points / Distribute Points on Faces / Distribute Points in Grid / Image to Points | **No rename** — split `GetMesh()`'s preview accessor from its data accessor (D5) so the mesh output can be honestly empty, matching Particle System's already-honest pattern | decided (D5); implementation is phase 4.2, applies to all four producers uniformly, not just the plan's named example |

Everything else audited this pass is either genuinely honest, or a
completeness bug where the name is accurate and an implementation gap (not
the name) is at fault — those stay on the phase 4 list built in phase 1/2,
not this one.

## Open questions carried forward

1. **Set Color's new name.** `Set Vertex Color` is this pass's proposal —
   states the channel (B) it actually writes, distinct from Material's
   channel (A). Confirm before phase 4.3, since renaming changes what loads
   from saved patches (`VisitParams` migration required either way, per the
   plan's phase 4.3 note and D2: never silently drop a cable/param from
   saved work).
2. Everything else the plan flagged as a "known mismatch to start from" —
   Join's `keepInputColours`, Mesh to Points' dual emission, Material's
   missing revision stamp — resolved above without needing a new open
   question; all three already had a decision (D3/D4/D5) that this pass
   confirmed rather than had to invent.
