# Geometry domain audit — 3D node contracts, cables, and side-channels

Executable prompt. Self-contained: do not assume the originating conversation.

Repo root is `/Users/namansoni/infinte` (spelled `infinte`). All paths relative
to it.

---

## Why this exists

A user merged a red-material branch and a white-material branch into one Join
Geometry and got white. The mechanism turned out to be that Join's merge
**manufactured** `Mesh::vertexColor` from material albedo unconditionally, so a
nested Join read that filler as authored colour and ignored the downstream
Material. Fixed on `bugfix/join-geometry-manufactured-vertex-colour`.

The fix is not the point. The point is the user's reaction:

> "we'll get caught up so bad because we didnt consider what nodes can be
> connected and what data might not be passed by"

That is correct, and a targeted fix does not answer it. Nobody has ever written
down what each 3D node emits, what each pin requires, or which of the seven
material/colour/texture side-channels each node forwards. Bugs of this shape
will keep landing until that map exists.

## Strict goals

1. **No misleading the user.** A node, pin, or mode must do exactly what its
   name says. Where it does not, the answer is to **rename it or remove the
   functionality** — not to document the discrepancy.
2. **Matrix-level understanding** of what connects to what and how data moves
   between 3D domains.
3. **Reason about chains**, not single nodes. Users wire permutations; the bug
   above needed three nodes in a specific order. Behaviour should match what a
   Houdini or Blender Geometry Nodes user would predict.

## Non-goals

- Do not implement connection enforcement in phase 1. Mapping comes first.
- Do not "fix" a node whose behaviour merely surprises you until the name
  contract in phase 3 says which of the two is wrong.

---

## Read these first

| Skill | Why |
|---|---|
| `.claude/skills/bug-blast-radius` | mandatory nine-question analysis before any fix |
| `.claude/skills/geometry-transform-sweep` | the probe-into-every-node-type sweep rig and its five invariants |
| `.claude/skills/run-infinite-hygiene` | the env-var self-test harness and its gotchas |
| `.claude/skills/git-branch-workflow` | branch per unit of work, merges to main, no PR |
| `.claude/skills/codebase-navigation` | where node bodies vs engines live |

The `cartographer` agent is appropriate for phase 1 and 2 reads — it reads whole
files and states what it did **not** find. Use it rather than ad-hoc grep when
the answer must be linked together.

---

## Established findings — verified, do not re-derive

Each claim below was checked against source in the originating session. Cited
lines are from that session; re-confirm a line number if an edit has moved it,
but do not re-derive the conclusion.

### The interface

`IGeometrySource` (`src/nodes/Geometry3DNodes.h:118`) is a **union**, not a type:

- `GetMesh()` is **pure virtual** — every source must return *something*
- `GetPointCloud()` / `GetCurve()` default to `nullptr`
- comment at `:193` states callers treat nullptr *"identically to not connected
  to the right kind of thing"* — **absent and empty are the same value**

Because `GetMesh()` is pure virtual, cloud nodes **fabricate** a mesh rather
than having none. This is the root disease and it is the same disease as the
reported bug: manufacturing data to fill a slot that should read as absent.

### 29 geometry sources, 22 with geometry input pins

Emission tags — six, because "emits mesh" hides four different things:

| tag | meaning | real geometry? |
|---|---|---|
| `mesh:surface` | vertices + faces, the node's actual content | yes |
| `mesh:verts` | vertices, no indices (`PointsToVertices`) | yes |
| `mesh:standin` | billboard quads synthesised for the thumbnail | **no** |
| `mesh:none` | permanently-empty stub (`ParticleSystem`) | **no** |
| `cloud` | `GetPointCloud()` non-null | yes |
| `curve` | `GetCurve()` non-null | yes |

Producers:

| Node | emits | status |
|---|---|---|
| Geometry, Text3D, Ocean, ModelSource, AudioRibbon | `mesh:surface` | inferred |
| GeometryOp, Displacement, AudioDisplacement, Join, MergeByDistance, MeshResynth, Wrap, Cloth, InstanceOnPoints, MetaBall | `mesh:surface` | inferred |
| CurveNode | `mesh:surface` + `curve` | verified |
| FieldPrimitive | `mesh` + `cloud` + `curve` | **unverified, mode-dependent** |
| PointsToVertices | `mesh:verts` | verified |
| MeshToPoints, DistributeOnFaces, DistributeInGrid, ImageToPoints | `mesh:standin` + `cloud` | verified |
| ParticleSystem | `mesh:none` + `cloud` | verified — `static Mesh empty` |
| Material, Mapping, Null3D, SetColor, Switcher3D, FieldElement | **= their input's tag** | verified (passthrough) |

The last row is load-bearing. Without **tag propagation through passthroughs**,
`MeshToPoints → Null3D → Cloth` launders a stand-in into a surface and any rule
set is defeated by one utility node.

Consumer pins — every geometry input in the codebase:

| Pin | requires | status |
|---|---|---|
| MeshResynth `.input` | `mesh:surface` | **verified** — `Subdivide`/`ExtrudeSelected`, budget from `indices.size()/3` |
| MetaBall `.cloud` | `cloud` | **verified** — pin literally named "cloud" |
| InstanceOnPoints `.cloudSource` | `cloud` | verified |
| Path `.curve` | `curve` | verified |
| PointsToVertices `.input` | `cloud` | verified |
| DistributeOnFaces `.input` | `mesh:surface` | read verified, faces inferred |
| Cloth `.input` | `mesh:surface` | read verified, **faces inferred** |
| Displacement, AudioDisplacement `.input` | `mesh:surface` | read verified, **faces inferred** |
| InstanceOnPoints `.pointSource` | `mesh:surface` | **inferred** |
| InstanceOnPoints `.instanceShape` | `mesh:any` | inferred |
| Wrap `.source` / `.target` | `mesh:any` / `mesh:surface` | reads verified, target-faces inferred |
| Path `.geo` | `mesh:any` | verified |
| GeometryOp, MergeByDistance, Join×4 | `mesh:any` | inferred |
| GeometryTable `.geo` | **any** | **verified** — reads mesh, cloud and curve |
| Material, Mapping, Null3D, SetColor, Switcher3D×4, Render3D×N, FieldElement | any | passthrough/terminal |

**A false rejection is worse than the silent bug.** Every `inferred` above must
be verified before it is allowed to block a connection or raise an error.

### The fabrication is load-bearing but only cosmetically

| Consumer | prefers | so the billboard mesh is |
|---|---|---|
| Render3D (`Geometry3DNodes.cpp:1966`) | **cloud** — `if (GetPointCloud()) drawCloudSlot()` | never drawn |
| NodeViewport (`NodeViewport.cpp:515`) | **mesh** — `hasMesh ? nullptr : GetPointCloud()` | **drawn — the node thumbnail** |

It cannot simply be deleted. Splitting the preview accessor from the data
accessor is the fix (decision D5 below).

### Seven side-channels, two with no invalidation

| # | channel | carrier | revision stamp |
|---|---|---|---|
| A | **Material** (albedo, roughness, transmission) | `GetMaterial()` | **NONE** |
| B | `Mesh::vertexColor` | inside the mesh | piggybacks `MeshRevision` |
| C | `Particle::r/g/b` | inside the cloud | `PointCloudRevision` |
| D | `InstanceColors()` | side channel, chain walk | via instancer |
| E | textures (`GetMaterialTexture`) | side | `SurfaceTextureRevision` |
| F | **`GetMappingTransform()`** | side | **NONE** |
| G | `InstanceSelection()` | side | `InstanceSelectionRevision` |

A caused the reported bug (`MaterialNode::MeshRevision()` forwards its input's
stamp verbatim, so a colour change upstream of a cache is invisible). **F is the
identical hole, not yet triggered.**

### Colour lives in four places and multiplies

`src/nodes/Geometry3DNodes.cpp:577`:

```
base = toLinear(uBaseColor) * vInstanceColor * vVertexColor
         ^ A (Material)      ^ D             ^ B / C
```

Consequence: **Set Color red + Material red = dark red.** Two nodes claiming the
same noun, neither winning.

Manufactured **white is invisible to the shader** (white is the multiplicative
identity). It is toxic only where code branches on `HasVertexColor()`. Those
sites are:

- `src/nodes/UtilityNodes.cpp:260, 347` — Join (the reported bug)
- `src/core/Mesh.cpp:4966` — `AppendMesh`, a **shared helper**
- `src/core/Mesh.cpp:885, 2419, 2461, 2572, 2611, 2766` — mesh ops
- `src/core/field/ElementStore.cpp:64, 142`
- `src/nodes/Geometry3DNodes.cpp:2022` — render (benign)

### Domain crossings manufacture colour in both directions

| crossing | code | behaviour |
|---|---|---|
| mesh → cloud (`MeshToPoints`) | `src/core/Mesh.h:296` | *"carried from `vertexColor` when the source had it, **1,1,1 otherwise**"* |
| cloud → mesh (`PointsToVertices`) | `src/nodes/PointDistributionNodes.cpp:286-288` | pushes `p.r/g/b` **unconditionally**; `Particle` defaults to `1,1,1` |

**Both violate the invariant the new `COLOURSWEEPTEST` asserts** ("a node fed a
colourless mesh must emit a colourless mesh") and neither is wired into that
sweep. Resolving this is a phase 4 deliverable, not an assumption.

### Side-channel forwarding — structural map (which overrides exist)

Derived mechanically. **Presence of an override is not correctness** —
`JoinGeometryNode` has the `MAT` override and still got material wrong.

Anomalies worth investigating first:

| Node | missing | consequence if unintentional |
|---|---|---|
| **InstanceOnPoints** | no `MAP` | upstream Mapping **dropped**; also absent from `MAPPINGSWEEPTEST` |
| **MetaBall** | no `MAP`, no `TEX` | upstream mapping and textures dropped |
| **Cloth** | no `PASS` | upstream InstanceOnPoints loses instancing — the class `INSTANCESWEEPTEST` exists for |

MetaBall and Cloth generate new surfaces, so "no upstream UVs to forward" is
defensible. **InstanceOnPoints forwards two of three side-channels from the same
input, which is the signature of an oversight rather than a decision.**

### Modes multiply the contract surface

```
kModeNames    81 entries
kShapeNames   36
kOpNames      10
```

A node's promise is **per-mode**. `GeometryOp` at op=Extrude and op=Delete are
different contracts. The audit is over **(node, mode)**, not node.

### Prior art — the design direction is settled

| Tool | geometry cable | validation point |
|---|---|---|
| Houdini SOPs | **one type** — `Geometry` = detail of points/prims/verts/attrs | **cook time**, red node |
| Blender Geometry Nodes | **one `Geometry` socket**, a container holding Mesh + Curves + PointCloud + Instances + Volume simultaneously | cook time + component passthrough |
| TouchDesigner | typed families (TOP/CHOP/SOP/DAT), refused | connect time, coarse |

Both DCCs that Infinite resembles **rejected connect-time typing for geometry**,
because a procedural node changes output type when a mode changes, and static
rules would sever an existing cable when the user touches a dropdown. Infinite
has this exact hazard (`FieldPrimitive`'s mode-dependent emission).

Blender's **component passthrough** is the important borrowed idea: a mesh-only
node leaves other components untouched and forwards them, so "cloth on a point
cloud" is a harmless no-op rather than swallowed data.

> Confidence note: these architecture claims are from knowledge, not verified.
> The load-bearing one — Blender's single `Geometry` socket carrying multiple
> components — should be confirmed before committing to the direction.

---

## Decisions taken

Confirm before relying on any of these; D3 and D4 remove or rename
user-visible things.

| # | Decision | Chosen |
|---|---|---|
| D1 | Where a bad connection is caught | **cook-time node error + message**, not connect-time refusal. Hard refusal reserved for pairs impossible in *any* mode (e.g. curve pin ← a source with no curve in any mode) |
| D2 | Saved patches with now-illegal cables | **load + flag the node.** Never silently drop a cable from saved work |
| D3 | Set Color vs Material both claim "colour" | **Rename.** `Material` owns albedo; Set Color writes *vertex colour* and its name must say so |
| D4 | `keepInputColours` param | **Remove.** Merge always preserves. A checkbox for "don't destroy my data" is the name failing |
| D5 | Billboard-quad fabrication | **Split** the preview accessor from the data accessor so `GetMesh()` can be honestly empty |
| D6 | `Particle` colour absence | **Add a has-colour flag** so absence is representable. Touches a core struct used by every cloud node — sequence it late |
| D7 | Delivery | **Phased.** Phase 1 lands before phase 2 starts |

Rationale for D1 is the prior art above: a red node reading *"expected a
surface, got a point cloud"* is strictly more informative than a cable that will
not drop, because it says what was wanted. It also serves goal 1 better than
either silence or refusal.

---

## Phases

Each phase is its own branch per `git-branch-workflow`. Phase N+1 does not start
until phase N is merged. Do not batch phases into one branch.

### Phase 1 — the reference document (no code changes)

Branch: `feature/geometry-domain-map`

Produce `docs/reference/geometry-domains.md`, the authoritative map. It must
contain, for **every one of the 29 sources and 22 consumers**:

1. **Emission tags per mode.** Where a mode changes the tag, list per mode.
   Every entry marked **verified** (a body was read) or **inferred**.
2. **Pin requirements per pin per mode.** Same verified/inferred marking. Every
   `inferred` in the established-findings table above must be resolved to
   verified here — especially the faces-requirements for Cloth, Displacement,
   Wrap `.target` and InstanceOnPoints `.pointSource`.
3. **Side-channel behaviour** for all seven channels A–G per node: forwards /
   drops / transforms / originates. Read the override body; do not report
   presence as correctness.
4. **What you did not find**, stated explicitly.

Resolve the two known unknowns: `FieldPrimitive`'s mode-dependent emission, and
whether the three anomalies (InstanceOnPoints `MAP`, MetaBall `MAP`/`TEX`, Cloth
`PASS`) are deliberate.

Exit criterion: zero `inferred` entries remain in the requirement column.

### Phase 2 — the rule matrix

Branch: `feature/geometry-rule-matrix`

Extend the phase 1 document with:

1. **Satisfaction rule.** Which emission tags satisfy which requirement tags.
   `mesh:standin` and `mesh:none` satisfy nothing in the mesh domain.
2. **Propagation rule** for passthrough nodes — a passthrough's tag is its
   input's tag, or the whole scheme is defeated by one Null3D.
3. **The derived illegal list**, generated from 1+2 rather than hand-written.
4. **The legal list that must not regress** — `MeshToPoints → Render3D`,
   `MeshToPoints → InstanceOnPoints.cloudSource`, `MeshToPoints →
   GeometryTable`, `PointsToVertices → GeometryOp`, every passthrough chain.
5. **Chain cases**, minimum depth 3, including the laundering case
   `MeshToPoints → Null3D → Cloth`.

Still no enforcement code. This phase's output is reviewed against goal 3: would
a Houdini or Blender user predict every verdict in the table? Any row where the
answer is no is a design finding, not a rule.

### Phase 3 — the name-contract audit

Branch: `feature/geometry-name-audit`

One line per **(node, mode)**: what the name promises, what it does, verdict.
This catches a different class than any sweep — Set Color × Material is
mechanically perfect (nothing dropped, every channel forwards) and still wrong
to a user who typed "red" twice.

The extracted rule:

> When a node needs a parameter to stop it destroying data, or a comment to
> explain why its output is not what its name says, **the name is wrong** — not
> the docs.

Known-honest examples that set the bar, all already in the repo: `Points to
Vertices` (emits a faceless mesh and says so), `Distribute Points on Faces`,
MetaBall's pin labelled `cloud`.

Known mismatches to start from: Set Color vs Material (D3), Join's
`keepInputColours` (D4), `Mesh to Points` also emitting a mesh (D5), Cloth
swallowing an upstream cloud, and `Material` promising an effect the graph has
no channel to observe (side-channel A).

Per goal 1, each mismatch resolves to **rename** or **remove**, never to a
documentation note.

### Phase 4 — code changes, smallest first

Separate branch per item, in this order:

1. **Revision stamps for A and F.** Add `MaterialRevision()` and a mapping
   stamp to `IGeometrySource`, defaulted so nothing breaks, and forward them
   through every passthrough. Closes one live hole and one latent one. Smallest
   real win; do it first.
2. **D5 — stop fabricating.** Split the preview accessor from the data
   accessor. Removes most illegal-connection rows without a single rule.
3. **D4 / D3** — remove `keepInputColours`; rename per the phase 3 verdicts.
   Both change saved patches: `VisitParams` migration required, and D2 applies.
4. **Cook-time error channel** (D1) driven by the phase 2 matrix.
5. **Component passthrough** (Blender's rule) so an unhandled cloud survives a
   mesh-only node.
6. **D6** — `Particle` has-colour flag, and the `COLOURSWEEPTEST` crossing
   exception resolved with it. Last: it touches a core struct.

### Phase 5 — sweeps

Extend `geometry-transform-sweep`'s rig, which already wires a probe into every
node type:

- a **material probe** and a **texture probe**, covering channels A, E, F
  mechanically — `MAPPINGSWEEPTEST` proves the pattern works for one channel
- widen `COLOURSWEEPTEST` from 8 node types to all 22 (`MAPPINGSWEEPTEST`
  already covers 15; the wiring pattern exists)
- add the missing types to `MAPPINGSWEEPTEST`, starting with InstanceOnPoints
- a **randomised chain fuzzer**: build N random 3–5 node chains from the 29
  types with random modes, assert colour-in/colour-out, no manufacturing, and
  determinism (same graph rebuilt twice is identical). This finds ordering bugs
  without anyone enumerating orderings, which is the only tractable answer to
  the permutation problem.

Expect the fuzzer to surface unrelated pre-existing bugs. That is the point, but
it makes phase 5 a poor thing to start immediately before a release.

---

## Standing constraints

- Every phase gets its own branch off `main` and merges directly back (no PR,
  solo repo).
- After building, copy `build/Infinite.app` to `~/Desktop/Infinite.app`.
- Do **not** UI-script Infinite's ImGui canvas to verify. Read code, or ask.
- Explain with diagrams, tables and bullets — not long prose, not Artifacts.
- Run the invariant-interaction audit before shipping invariant-establishing
  code. **This whole document exists because that step was skipped once**: the
  commit that caused the reported bug established "a merged mesh always carries
  `vertexColor`" and nobody asked who relied on that not being true. One grep
  for `HasVertexColor()` would have found Join's own read of it three lines
  away.
- A green suite that contains no assertion about the thing you changed is not
  evidence. The commit that caused the bug said "all sweeps pass" truthfully —
  no sweep covered colour.
- Debug tools must be `#ifndef NDEBUG`'d out of shipped builds.
- End commit messages with:
  `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`

## Open questions for the user

Raise these before the phase they gate; do not answer them by assumption.

1. **Phase 3**: for each rename, what is the new name? Renaming a node changes
   what loads from saved patches — a migration is needed either way.
2. **Phase 4.3**: is it acceptable that removing `keepInputColours` changes how
   an existing saved patch renders? It is the correct behaviour, but it is a
   visible change to already-saved work.
3. **Phase 4.5**: component passthrough changes what several nodes emit. Confirm
   before implementing.
4. **Phase 5**: run the fuzzer before or after the next release?
