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
| FieldElement | `FieldElementNode.h:16,146-163`, `.cpp:535-659`, `ElementStore.cpp:93-188` | **conditional.** With `input` wired: propagates input's tag — `ElementStore::ToMesh` rebuilds `mOutMesh` from `inMesh` verbatim when `boundCount == inCount` (`ElementStore.cpp:155-160`), or safely truncates (drops any triangle referencing a cut vertex, never leaves a dangling index) when `maxElements < inCount` (`:161-187`), which can turn a `mesh:surface` input into an effectively-`mesh:verts` output if truncation removes every surviving triangle. With no `input`: **originates fresh** as a generator — `n = max(1, generateCount)` bare vertices, no indices, i.e. always `mesh:verts` (`.cpp:584-593`) | none (kernel/topology-driven, not a UI mode) | **verified** |

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
| PointsToVertices `.input` | `cloud`, preferred, wins over mesh when present; degrades gracefully to a `mesh:verts`-shaped vertex/vertexColor copy when input has no cloud (does not require `.indices` either way) | `PointDistributionNodes.cpp:244-316` | **verified** — cloud path (`:274-289`) reads `px/py/pz`, `nx/ny/nz`, `r/g/b`, and `alive` (if `aliveOnly`); no-cloud fallback (`:297-305`) copies `input->GetMesh()`'s vertices/vertexColor only, never touches `.indices` |
| DistributeOnFaces `.input` | `mesh:surface` — needs face topology (uses `MeshOps::DistributeOnFaces`, area-weighted) | — | verified vs. MeshToPoints' index-order method |
| Cloth `.input` | `mesh:surface` for a physically meaningful result; degrades gracefully (no crash) on `mesh:verts` | `RebuildFromInput`, `SimulationNodes.cpp:271-402`, constraint loop `:338-356` | **verified** — one PBD distance-constraint kind only (no separate shear/bend, contra earlier phase-1 note), built by walking `src.indices` in triples; on an index-less input the loop never executes so `mConstraints` stays empty and the mesh simulates as an unconstrained particle set (gravity/wind/pin still apply) rather than refusing |
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
| SetColor | fwd | **orig** (`.cpp:1156-1184`) | **orig** (`.cpp:1190-1217`) | fwd | fwd | fwd | fwd — **but no `GetCurve()` override exists anywhere in the class (`.h:694-826`, full body scanned); a curve routed through SetColor is dropped, same defect as Null3D/Material/Mapping/Switcher3D (see anomalies)** |
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
| **SetColor** | `GetCurve()` only (it does forward `GetPointCloud()`) | **Oversight.** Forwards mesh, cloud, and every side channel correctly; curve alone is dropped with no override anywhere in the class. |

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
  auditing channel B specifically. `kDelete` was confirmed separately (see
  anomalies follow-up) to collapse straight to a fully-empty mesh rather than
  ever leaving vertices with no indices — it cannot produce a `mesh:verts`
  result by stripping indices.
- Whether `GeometryOpNode`'s `kSubdivide`/`kSelect` ops can change a mesh's
  emission tag (e.g. strip all indices while leaving vertices) — `kDelete` was
  checked and cannot (`Mesh.cpp:5004-5039`, collapses to fully empty instead);
  `kSubdivide`/`kSelect` were not independently traced this pass.
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

---
---

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
