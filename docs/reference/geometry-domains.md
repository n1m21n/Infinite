# Geometry domains — the 3D node contract map

Phase 1 deliverable of `docs/plans/geometry/domain-audit-prompt.md`. This is a
reference document, not a design doc: it states what the code does today, per
node, per mode, with file:line citations and an explicit verified/inferred
mark on every row. Phase 2 builds the legality matrix on top of this. Phase 3
audits names against this. No enforcement code exists yet.

`IGeometrySource` is defined at `src/nodes/Geometry3DNodes.h:118-208`. It is a
union interface: `GetMesh()` is pure virtual (every producer returns
*something*), `GetPointCloud()`/`GetCurve()` default to `nullptr`, and seven
side-channel accessors (material, vertex/particle colour, per-map texture,
mapping transform, instance colour, instance selection, instance transform
override) default to empty/identity so an implementer only overrides what it
actually has.

**Emission tags:**

| tag | meaning | real geometry? |
|---|---|---|
| `mesh:surface` | vertices + faces | yes |
| `mesh:verts` | vertices, no indices | yes |
| `mesh:standin` | fabricated billboard-quad mesh, thumbnail only | **no** |
| `mesh:none` | permanently-empty stub | **no** |
| `cloud` | `GetPointCloud()` non-null | yes |
| `curve` | `GetCurve()` non-null | yes |

**Side channels A–G:**

| # | channel | carrier | revision stamp |
|---|---|---|---|
| A | Material | `GetMaterial()` | **none** |
| B | `Mesh::vertexColor` | inside mesh | piggybacks `MeshRevision` |
| C | `Particle::r/g/b` | inside cloud | `PointCloudRevision` |
| D | `InstanceColors()` | side, chain walk | via instancer |
| E | textures (`GetMaterialTexture`) | side | `SurfaceTextureRevision` |
| F | `GetMappingTransform()` | side | **none** |
| G | `InstanceSelection()` | side | `InstanceSelectionRevision` |

---

## Node count: 28, not 29 — resolved

Grepping `class .* : public.*IGeometrySource` across all of `src/` (not just
`src/nodes/`) yields exactly 28 distinct node classes (listed below), plus 5
`IGeometrySource`-implementing structs in `src/main.cpp` that are self-test
probes/fixtures (`DummyGeo` ×2, `CloudProbe`, `TransformProbeSource`,
`MappingProbeSource`) — not user-facing nodes, correctly excluded from the
node catalog. No 29th user-facing implementer exists anywhere in the
codebase. `PathNode` and `GeometryTableNode` were double-checked directly
(`src/nodes/PathNode.h:19`, `src/nodes/GeometryTableNode.h:17`) and both
implement `IModulator`, not `IGeometrySource` — they are geometry
*consumers* only, as the established-findings table already had them.
`git log --all --diff-filter=D` over `src/nodes/*.h`/`*.cpp` shows no
deleted geometry-node file that could explain a since-removed 29th class.
**Conclusion: the audit prompt's count of 29 was simply incorrect; 28 is the
authoritative roster for phase 2.**

---

## Emission per node, per mode

All entries below were verified by reading the actual accessor body unless
marked `inferred`. "Terminal" = no geometry input pin.

| Node | File | Emission | Mode dependence | Status |
|---|---|---|---|---|
| Geometry | `Geometry3DNodes.h:211`, `.cpp:796` | `mesh:surface` | none (shape param, not domain-changing) | verified |
| Text3D | `Text3DNode.h:13`, `.cpp:52-61` | `mesh:surface` | none | verified |
| Ocean | `OceanNode.h:14`, `.cpp:35-44` | `mesh:surface` | none | verified |
| ModelSource | `ModelSourceNode.h:27-35` | `mesh:surface` | none | verified |
| AudioRibbon | `AudioRibbonNode.h:16` | `mesh:surface` | none | verified |
| CurveNode | `CurveNode.h:14,35` | `mesh:surface` **+** `curve` | none | verified |
| GeometryOp | `GeometryOpNodes.h:26`, `.cpp:157-460` | `mesh:surface` (or instance-domain redirect, see below) | 10 ops (`kTransform`…`kScrew`, `kSelect`, `kDelete`); none change the emission tag itself | verified |
| Displacement | `GeometryOpNodes.h:371`, `.cpp:550-662` | `mesh:surface` | none | verified |
| AudioDisplacement | `AudioDisplacementNode.h:17`, `.cpp:514-516` | `mesh:surface` | none | verified |
| InstanceOnPoints | `GeometryOpNodes.h:541`, `.cpp:666-880` | `mesh:surface` (the stamp mesh, not realized copies) | none | verified |
| SetColor | `GeometryOpNodes.h:694`, `.cpp:1149-1258` | passthrough (mesh or cloud, whichever input has) | none | verified |
| Wrap | `GeometryOpNodes.h:834`, `.cpp:926-1017` | `mesh:surface` | none | verified |
| Null3D | `UtilityNodes.h:134`, `.cpp:17-36` | passthrough for mesh only — **cloud/curve NOT forwarded** (see anomalies) | none | verified |
| Material | `UtilityNodes.h:211`, `.cpp:40-107` | passthrough for mesh only — **cloud/curve NOT forwarded** | none | verified |
| Mapping | `UtilityNodes.h:354`, `.cpp:149-168` | passthrough for mesh only — **cloud/curve NOT forwarded** | none | verified |
| Join (×4-input) | `UtilityNodes.h:441`, `.cpp:183-428` | `mesh:surface` | none (mesh-only op; never reads cloud/curve) | verified |
| MetaBall | `UtilityNodes.h:582`, `.cpp:645-708` | `mesh:surface` (marching cubes) | none | verified |
| MeshToPoints | `UtilityNodes.h:689`, `.cpp:434-641` | `mesh:standin` **+** `cloud` | none | verified |
| MergeByDistance | `PointDistributionNodes.h:288`, `.cpp:504-509` | `mesh:surface` | none | verified |
| DistributeOnFaces | `PointDistributionNodes.h:20` | `mesh:standin` **+** `cloud` | none | verified |
| PointsToVertices | `PointDistributionNodes.h:137` | `mesh:verts` | none | verified (header only, `.cpp` build logic not independently re-read — see gaps) |
| DistributeInGrid | `PointDistributionNodes.h:233,254` | `mesh:standin` **+** `cloud` | none; terminal, no geometry input | verified |
| ParticleSystem | `SimulationNodes.h:26,41,48-53` | `mesh:none` **+** `cloud` | none; terminal | verified |
| Cloth | `SimulationNodes.h:125`, `.cpp:271,285-287,527-616` | `mesh:surface` (simulated, instancer realized at rest-build) | none | verified |
| MeshResynth | `GenerativeNodes.h:20,66-102` | `mesh:surface` | none | verified |
| ImageToPoints | `GenerativeNodes.h:158,174-204` | `mesh:standin` **+** `cloud` | none; terminal (ImageCable input, not geometry) | verified |
| Switcher3D | `Switcher3DNode.h:27`, `.cpp:56-119` | mesh = active input's tag — **cloud/curve NOT forwarded regardless of active slot** | 4-way input switch; only changes *which* mesh, not cloud/curve behaviour | verified |
| FieldPrimitive | `FieldPrimitiveNode.h:16,119,122`, `.cpp:884-1119` | **mesh only, always.** `topology=Points` → `mesh:verts` (`.cpp:1105-1119`); `topology`∈{Plane,Sphere,Cylinder,Torus,Disc} → `mesh:surface` (`.cpp:884-1090`). `GetPointCloud()`/`GetCurve()` hardcoded `nullptr` unconditionally | yes — `mesh:verts` vs `mesh:surface` only | **verified — resolves the open question below** |
| FieldElement | `FieldElementNode.h:16,146-163` | own `mOutMesh`; side channels + cloud/curve genuinely all forward from `input` | none | verified for side channels; **mesh-build relation to `input`'s mesh not independently re-read** |

### FieldPrimitive — resolved

The established-findings table in the audit prompt listed FieldPrimitive as
"`mesh` + `cloud` + `curve`, mode-dependent, unverified." That premise is
**false**. `GetPointCloud()` and `GetCurve()` are inline `override` bodies
that unconditionally `return nullptr` (`FieldPrimitiveNode.h:119,122`) — no
`topology` value ever produces a cloud or curve. The `topology` dropdown only
toggles between `mesh:verts` (Points) and `mesh:surface` (everything else).
This closes the phase-1 exit requirement for this node.

---

## Consumer pin requirements

| Pin | Requires | Cites | Status |
|---|---|---|---|
| MeshResynth `.input` | `mesh:surface` | forwards A/E/F/PassthroughSource/instance-* (`.h:66-102`) | verified |
| MetaBall `.cloudSource` | `cloud` — reads `px/py/pz/scale` only, **not** `r/g/b` (`.cpp:668-679`) | — | verified — colour from an upstream Set Color has no effect on MetaBall |
| InstanceOnPoints `.cloudSource` | `cloud`, wins over `.pointSource` when both connected (`.cpp:733-751`) | — | verified |
| InstanceOnPoints `.pointSource` | `mesh:surface`-shaped (reads `->GetMesh()`, samples via `MeshOps::ToPoints`, needs `HasGeometry()` not full topology) (`.cpp:756-770`) | — | verified |
| InstanceOnPoints `.instanceShape` | `mesh:any` — reads `->GetMesh()`/`->GetModelMatrix()` (`.cpp:552,671,716-718,729`) | — | verified |
| Path `.curve` | `curve` | established findings | verified (prior session) |
| PointsToVertices `.input` | `cloud` (per class comment, `.h:131-136`) | — | **inferred** — `.cpp` `RebuildIfNeeded()` body not independently re-read this pass |
| DistributeOnFaces `.input` | `mesh:surface` — needs face topology (uses `MeshOps::DistributeOnFaces`, area-weighted) | — | verified vs. MeshToPoints' index-order method |
| Cloth `.input` | `mesh:surface`, faces needed for PBD constraint building | — | **inferred** — constraint-building body not re-read this pass |
| Displacement `.input` | `mesh:surface`, vertex-level only — does not require `->indices` beyond what `MeshOps::Displace` needs | `.cpp:561` | verified |
| AudioDisplacement `.input` | `mesh:surface` | `.cpp:514-516` | verified |
| Wrap `.sourceInput` | `mesh:any` — struct-copied into output, `vertexColor` rides along (`Mesh.cpp:3020`) | — | verified |
| Wrap `.targetInput` | `mesh:surface` — needs real triangles for nearest-surface search (`Mesh.cpp:2965+`) | — | verified |
| Join ×4 inputs | `mesh:any`; realizes any upstream instancer via `MeshOps::RealizeInstances` before merging (`.cpp:239-246`) | — | verified |
| GeometryOp `.input` | `mesh:any`; redirects `kTransform`/`kSelect`/`kDelete` to instance-domain ops when wrapping an instancer (`.cpp:132-144,246-258,318-421`) | — | verified |
| MergeByDistance `.input` | `mesh:surface` | forwards all 7 channels + PassthroughSource unchanged | verified |
| GeometryTable `.geo` | any (reads mesh, cloud, and curve) | established findings; **not an `IGeometrySource` implementer itself** — `IModulator`, holds `IGeometrySource*` as consumer only (`GeometryTableNode.h:17,34-35`) | verified this pass |
| Material, Mapping, Null3D, SetColor, Switcher3D, Render3D, FieldElement | any (passthrough/terminal) | — | verified for mesh; **cloud/curve pins on Null3D/Material/Mapping/Switcher3D silently starve downstream consumers — see anomalies** |

---

## Side-channel behaviour per node (A–G)

`fwd` = forwards from input unchanged. `orig` = originates fresh (overrides
the input's value). `drop` = silently not forwarded, falls to base-class
default. `n/a` = channel doesn't apply (node has no relevant input/output).

| Node | A Material | B vertexColor | C particle colour | D InstanceColors | E texture | F MappingTransform | G InstanceSelection |
|---|---|---|---|---|---|---|---|
| Geometry | orig (own fields) | n/a | n/a | n/a | orig (own `mTextureInput`, albedo-only) | **drop** (no override, base identity) | n/a |
| AudioRibbon | n/a (no override found) | n/a | n/a | n/a | **drop** — hardcoded `return 0` (`.h:37`) | n/a | n/a |
| AudioDisplacement | fwd | fwd (implicit, struct-level) | n/a | fwd | fwd (`.cpp:514-516`) | fwd | fwd |
| GeometryOp | **orig when `inheritMaterial==false`**, else fwd (`.cpp:484-511`) | implicit via `MeshOps::*`; **not individually verified per-op** (see gaps) | n/a | fwd | fwd | fwd | fwd |
| Displacement | fwd | fwd | n/a | fwd | fwd | fwd | fwd — deliberately preserved so instancer stays visible through it (`.h:407-409`) |
| InstanceOnPoints | fwd (from `instanceShape`, `.cpp:687-714`) | flows into D via point-sampling of upstream `vertexColor` | flows into D via cloud `p.r/g/b` | **originates** (`mColors`, `.cpp:746-748,815-817`) | fwd (from `instanceShape`, `.h:559-566`) | **drop — no override at all in the class.** Forwards A and E from the same input but not F. Flagged as an oversight, not a design choice (see anomalies) | n/a — root of the instancer chain, not a downstream consumer of selection |
| SetColor | fwd | **orig** (`.cpp:1156-1184`) | **orig** (`.cpp:1190-1217`) | fwd | fwd | fwd | fwd |
| Wrap | fwd (from `sourceInput` only) | fwd, incidental via struct copy (`Mesh.cpp:3020`) | n/a | fwd (from `sourceInput`) | fwd (from `sourceInput`) | fwd (from `sourceInput`) | fwd (from `sourceInput`) |
| Null3D | fwd | fwd | n/a | fwd | fwd | fwd | fwd — **but `GetPointCloud()`/`GetCurve()` are not overridden at all, contradicting the class's own "everything is forwarded" comment (`.h:131-133`)** |
| Material | **orig** (unless bypassed, `.cpp:52-91`) | fwd (mesh passthrough) | n/a | fwd | orig, per-`MaterialMap` channel (`.cpp:98-107`) — own `mMaps[map]` cable if connected, else forwards input | fwd | fwd — **same cloud/curve gap as Null3D** |
| Mapping | fwd | fwd | n/a | fwd | fwd | **orig** (own `space`/`translate`/`rotate`/`scale`/`triplanarBlend`, `.cpp:149-159`) | fwd — **same cloud/curve gap as Null3D** |
| Join | picks one input via `materialFrom` index (`.cpp:341-417`) | **historically manufactured from albedo when absent — fixed on `bugfix/join-geometry-manufactured-vertex-colour`; now preserves correctly per-input** | n/a | n/a (realizes instances before merge) | picks same `materialFrom` slot as A, consistent (`.cpp:341-417`) | picks same `materialFrom` slot as A/E | n/a |
| MetaBall | orig (own fields, inferred) | n/a | **drop** — reads only `px/py/pz/scale` from cloud, never `r/g/b` (`.cpp:668-679`) | n/a | **drop — no override anywhere in class** | **drop — no override anywhere in class** | n/a |
| MeshToPoints | n/a (mesh:standin only) | flows into emitted cloud's `r/g/b` when instancer realized, weighted by D (`.cpp:496-501`) | **originates** the emitted cloud's colour from sampled mesh points | consumed (multiplies into emitted per-particle colour) | n/a | n/a | n/a — sampling collapses the instancer, nothing left to select |
| MergeByDistance | fwd | fwd | n/a | fwd | fwd | fwd | fwd — clean, complete passthrough, no gaps found |
| DistributeOnFaces | fwd | n/a (emits standin+cloud) | originates (sampled) | n/a | fwd | fwd | n/a |
| ParticleSystem | n/a | n/a | originates own | n/a | n/a | n/a | n/a |
| Cloth | fwd (`.cpp:535-541`, checks `bypassed`/`inheritMaterial`) | implicit, simulated mesh carries it forward | n/a | **drop — deliberate.** Instancer realized into concrete mesh at rest-build (`.cpp:285-287`); nothing left to select/transform as a group afterward | fwd (`.cpp:566-569`) | fwd (`.h:155-158`) | **drop — deliberate, same reasoning as D** |
| MeshResynth | fwd | implicit | n/a | fwd | fwd | fwd | fwd — clean operator, matches GeometryOp's pattern |
| ImageToPoints | n/a | n/a | originates from image sample | n/a | **drop — deliberately hardcoded to avoid double-applying colour via both `r/g/b` and a surface texture sample (`.h:198-204`, documented, not a bug)** | n/a | n/a |
| Switcher3D | fwd (from `Active()` input, `.cpp:56-119`) | fwd (mesh passthrough) | n/a | fwd | fwd | fwd | fwd — **but `GetPointCloud()`/`GetCurve()` never overridden; every other accessor correctly routes through `Active()`, cloud/curve simply weren't added** |
| FieldElement | fwd | fwd | n/a | fwd | fwd | fwd | fwd — most complete forwarding wrapper found; the only one that also forwards cloud/curve (`.h:146-163`) |

---

## Anomalies — resolved

The audit prompt flagged three anomalies as needing investigation. Resolved:

| Node | Missing channel | Verdict | Reasoning |
|---|---|---|---|
| **InstanceOnPoints** | F (`GetMappingTransform`) | **Oversight — real gap.** Forwards A and E from the same `instanceShape` input but drops F with no comment. The stamp mesh has real UVs; nothing topologically prevents forwarding Mapping. Also absent from `MAPPINGSWEEPTEST`. | recommend fixing in phase 4 |
| **MetaBall** | F, E (`GetMappingTransform`, `GetMaterialTexture`) | **Deliberate — defensible.** Marching-cubes output has no meaningful UVs (no UV-writing code found in the region of `Primitives::MetaBalls`, not independently confirmed by reading that function body — see gaps). Texture/mapping would have nothing coherent to sample onto. | no fix needed; consider documenting in-code |
| **Cloth** | G/D (instance selection, group transform / "PASS") | **Deliberate — not a bug.** `RebuildFromInput()` bakes (`MeshOps::RealizeInstances`) the upstream instancer into one concrete simulated mesh at rest-build time (`.cpp:285-287`). A PBD solve over one shared point set cannot represent "N independent instances," so there is nothing left to select or group-transform downstream. | **remove from any fix list** — this was misclassified as an anomaly in the original audit prompt |

## Anomalies — newly found this pass (not in the original prompt)

| Node | Missing channel | Verdict |
|---|---|---|
| **Switcher3D** | `GetPointCloud()`, `GetCurve()` | **Oversight.** Every one of the other 7 side-channel accessors + `PassthroughSource()` correctly routes through `Active()`. Cloud/curve were never added. A cloud or curve source patched into any of the 4 slots is invisible downstream regardless of which slot is active. Directly contradicts the audit prompt's own classification of Switcher3D as a clean tag-of-input passthrough. |
| **Null3D** | `GetPointCloud()`, `GetCurve()` | **Oversight.** Class comment states "everything is forwarded" (`.h:131-133`); false for cloud/curve. |
| **Material** | `GetPointCloud()`, `GetCurve()` | **Oversight.** Header frames the node as "a pass-through in the geometry chain" (`.h:207-210`); true for mesh, false for cloud/curve. |
| **Mapping** | `GetPointCloud()`, `GetCurve()` | **Oversight.** Same shape as Null3D/Material. |

All four share one fix shape — this is a single sweep, not four separate
investigations. Recommend adding to `codebase-navigation`'s living map (see
below) and fixing together in phase 4.

---

## What this pass did NOT verify (explicit negatives)

- `Primitives::MetaBalls()`'s body in `Mesh.cpp` — not read directly to
  confirm marching-cubes vertices leave `u/v` at 0; inferred from absence of
  UV-writing code in the surrounding grep sweep.
- `MeshOps::RemapVertexColor` call sites inside each of `GeometryOpNode`'s 13
  individual ops (`kSubdivide`, `kMirror`, `kBevel`, etc.) — the shared helper
  exists (`Mesh.h:360`) and Wrap/Transform are confirmed to preserve colour via
  struct-copy, but not every op's `MeshOps::*` implementation in `Mesh.cpp` was
  individually traced for vertexColor correctness. Real gap for anyone
  auditing channel B specifically.
- `PointsToVerticesNode::RebuildIfNeeded()` body (`PointDistributionNodes.cpp`)
  — only the header comment was checked; whether it needs `alive` filtering
  only, or also normals/scale, is not independently confirmed.
- `FieldElementNode.cpp`'s mesh-build logic — whether `mOutMesh` is a full
  replace or a per-element modification of `input`'s mesh was not read; only
  the header's side-channel accessor bodies were verified.
- `InstanceColors()` (channel D) has no revision stamp and is not part of the
  `IGeometrySource` interface at all — it's a concrete accessor on
  `InstanceOnPointsNode` (`GeometryOpNodes.h:591`). Every call site does an
  explicit `FindInstancer`/`dynamic_cast` first; a plain `IGeometrySource*`
  cannot read it. This matters for phase 2's satisfaction rule: "does this
  chain preserve instance colour" is not answerable from the interface alone.
- No per-map (per-`MaterialMap`) UV/mapping-transform concept exists —
  `GetMaterialTexture(int map)` always samples through the single
  `GetMappingTransform()`, consistent with the interface's comment
  (`Geometry3DNodes.h:144-149`) but not independently re-verified against the
  shader.

---

## Node count discrepancy — open question

28 confirmed `IGeometrySource` implementers found by base-class grep, not the
29 stated in the audit prompt. No 29th class was found in `src/nodes/`. Raise
to the user before phase 2 locks in a fixed roster: was a class renamed or
removed since the prompt was written, or was one class double-counted?

---

## Codebase-navigation map addition

Recommend adding to `.claude/skills/codebase-navigation`'s living map:

> **IGeometrySource passthrough wrappers silently drop `GetPointCloud()`/
> `GetCurve()`**: `Null3DNode`, `MaterialNode`, `MappingNode`
> (`src/nodes/UtilityNodes.h`), and `Switcher3DNode`
> (`src/nodes/Switcher3DNode.cpp`) all forward mesh + all 7 side-channels +
> `PassthroughSource()`/instance-* correctly, but none override
> `GetPointCloud()`/`GetCurve()` — a cloud or curve source routed through any
> of them becomes invisible downstream even though each node's own comments
> describe it as a complete no-op passthrough. Any new `IGeometrySource`
> passthrough wrapper needs to explicitly forward these two; the base class
> defaults to `nullptr` and nothing enforces the "if you forward
> `PassthroughSource`, forward cloud/curve too" pairing.
