# Field build step 4 — the `element` domain

You are implementing **build step 4 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). This is a
self-contained brief; you have no prior context on Field and do not need any
beyond what is listed under "Files to read first".

**Prerequisite steps that must already be finished and merged before you start:**

| Step | What it delivered | How to confirm it is done |
|---|---|---|
| 1 | `src/core/Expression.cpp` split into lexer → AST → typed IR → bytecode, with `Expression::Evaluate`'s signature and its three call sites unchanged | a lexer/AST/IR exists under `src/core/` or `src/core/field/`; `INFINITE_FIELDTEST` sections A–C pass |
| 2 | `rand`/`noise`/`sh` are pure functions of `(t, seed)` | `INFINITE_FIELDTEST` random-set baseline recorded |
| 3 | `vec2`/`vec3`/`vec4` + scalar→vector rank polymorphism in the type checker | `INFINITE_FIELDTEST` section D passes |

If any of those is missing, **stop and say so**. Step 4 is built on step 3's
vector types (`P` is a `vec3`); there is no useful element domain without them.

---

## 1. Invariants — restated verbatim, they override anything you infer

1. **Clean room.** Infinite is MIT. **Never** open, read, grep, or reference
   GPL sources: Kronos (`bitbucket.org/vnorilo/k3`), Cmajor, SuperCollider, or
   BespokeSynth (also at `/Users/namansoni/BespokeSynth` on this machine — do
   not open it). The *Kronos paper* (Norilo, "Kronos: A Declarative
   Metaprogramming Language for Digital Signal Processing", Computer Music
   Journal 39:4, 2015) is a published paper and may be cited freely; its code
   may not be read. Safe to read for reference: Faust (LGPL), ChucK (dual
   MIT/GPL), Houdini VEX documentation, TidalCycles docs/papers.

2. **Bare names. No sigils. Ever.** An earlier draft used a VEX-style `@`
   sigil. The owner **removed it**. Plain ASCII, no special characters, in
   every example, every doc, every test fixture, every error message.

   | Wrong | Right |
   |---|---|
   | `@P.y += bass * 2` | `P.y += bass * 2` |
   | `@Cd = vec3(1,0,0)` | `Cd = vec3(1,0,0)` |
   | `v@P` / `f@heat` | `P` / `heat` |

3. **Rate is inferred, never declared.** There is no `@rate` keyword, no
   `krate` parameter, no domain annotation on a binding. If you find yourself
   adding syntax so the user can say "this is element-domain", you have taken a
   wrong turn.

4. **Field is one primitive, not a pile of features.** Every construct must be
   explainable as "a body of code run once per element of a domain". If a
   proposed feature cannot be explained that way, it does not go in.

5. **Only build step 1 touches existing code.** Steps 2–10 are additive: new
   files, new node types. **If your diff for step 4 edits an existing node,
   something is wrong** — with the single, deliberate exception discussed in
   §5.2 (the SoA store), which you must get an explicit decision on before
   writing.

6. **A failing compile never blanks the graph.** The last working program keeps
   running and the error text is surfaced. This is already how
   `FormulaNode::Apply()` behaves and it is the model for every Field backend.

7. **Real-time safety is non-negotiable:** no heap allocation in generated or
   per-cook code past init, no recursion, no unbounded loops, no strings /
   pointers / dynamic arrays / structs in the v1 language surface, every
   value's size known at compile time, element counts bounded and declared up
   front.

---

## 2. Goal

Build the **`element` domain**: a kernel body that runs once per point or
vertex of a geometry stream, with the reserved bare-name attributes
`P N uv Cd i count`, user-declared `attrib` storage, a
**structure-of-arrays** attribute store, and a compile-time-bounded element
count. This domain **does not exist in Infinite today** — there is no per-vertex
scripting surface anywhere in the codebase — so this is the largest new surface
of the whole Field project. The deliverable is: a new Field element node that
implements `INode` + `IGeometrySource`, takes a geometry input, runs a
user-typed kernel once per vertex, and hands the modified mesh downstream;
plus the element half of the bytecode backend, the SoA attribute store and its
`Mesh` ⇄ store conversion, the `attrib` declaration path, and the element
conformance fixtures. `param` and `state` are **out of scope** — they are steps
5 and 6.

---

## 3. Files to read first, and why

### Skills — the authoritative contract (read in this order)

| File | Why |
|---|---|
| `.claude/skills/field-language/SKILL.md` | §1 the one primitive, §2 the domain table, §3 rate inference, §4 the no-sigil rule, §5 reserved words per domain, §6 `attrib`, §9 types, §14 the wrong/right table. This is the surface you are implementing. |
| `.claude/skills/field-compiler/SKILL.md` | §4 the AST node set, §5 domain inference as a dataflow fixpoint (the hoisting rule is the whole performance payoff), §6.1 the bytecode VM for frame/element, **§9 memory layout — read this twice**, §8 what the front end must refuse |
| `.claude/skills/field-realtime/SKILL.md` | §1 the diff checklist, §2 the bounded-size rules, §4 the branching cost model for `element` (a real branch, but it costs the batch its vectorization), §5 the element budget: ~6 ns per element |
| `.claude/skills/field-integration/SKILL.md` | §2 the `INode` contract, §6 pins and slots share ONE index space, **§7 `IGeometrySource` and the passthrough-forwarding trap**, §9 the wiring checklist |
| `.claude/skills/field-testing/SKILL.md` | §1 how testing works in this repo (there is no test binary), §5 the `element` conformance table — that table is your acceptance list |
| `.claude/skills/new-geometry-node/SKILL.md` | Field does not exempt a node from the existing procedure. Read it before writing the node. |
| `.claude/skills/node-ui-pillars/SKILL.md` | **before** you write any body-draw code. Symmetry and dark-mode contrast are non-negotiable in this repo. |

### Real source — the code wins over any skill

| File | Why |
|---|---|
| `src/core/Mesh.h` | the ground truth on AoS vs SoA. §5.1 below records what it actually does; **re-verify** rather than trusting the summary. |
| `src/nodes/Geometry3DNodes.h` (`IGeometrySource` at line 118) | every virtual your node must answer or forward |
| `src/core/INode.h` | `ParamVisitor` has exactly five methods; `TextureRevision()`; the slot-index space |
| `src/core/Expression.h` / `.cpp` | what step 1 restructured; the `double`-throughout precision rule |
| `src/core/Patch.h` | the line grammar; free-text fields are always last on their line |
| `.claude/skills/run-infinite-hygiene/driver.sh` | `FULL_TESTS=(...)` begins at **line 173** (not 49 — an older doc says 49; the code wins) |

---

## 4. Files to create / modify

### Create

| Path | Contents |
|---|---|
| `src/core/field/ElementStore.h` / `.cpp` | the structure-of-arrays attribute store, and `Mesh` ⇄ store conversion (§5.2) |
| `src/core/field/ElementBackend.h` / `.cpp` | element-domain bytecode emission + the per-element VM loop, sharing the register VM step 1 built for `frame` |
| `src/nodes/FieldElementNode.h` / `.cpp` | the node: `INode` + `IGeometrySource`, one geometry input, one geometry output |

(If step 1 put the compiler somewhere other than `src/core/field/`, put these
alongside it and keep the naming consistent. Do not create a second location.)

### Modify

| Path | Change |
|---|---|
| `CMakeLists.txt` | add the new `.cpp` files — node files sit with the others under `src/nodes/` (~line 98) |
| `src/main.cpp` | include with the other node includes (~line 92); `REGISTER_NODE(FieldElementNode, <Display Name>, "<OneToken Category>")` in `RegisterNodes()`; the body-draw function and its dispatch entry; the node help table (~line 7780), one sentence |
| `src/core/field/*` (step 1's files) | extend the domain lattice / reserved-word table with `element`. **Additive only** — no frame-domain behaviour changes. |
| `.claude/skills/run-infinite-hygiene/driver.sh` | add `"FIELDELEMENTTEST:10"` to `FULL_TESTS` (line 173) |

**Do not** modify `src/core/Mesh.h`, `src/nodes/Geometry3DNodes.h`, or any
existing geometry node without first getting §5.2's open question answered.

---

## 5. Step-by-step procedure

### 5.1 First: record what `Mesh.h` actually does (do this before writing code)

The design brief requires attributes to be **structure-of-arrays**; an AoS
layout costs a permanent 2–5×. What the code does today is **hybrid**, and it
defends the AoS parts on purpose. Verified against
`/Users/namansoni/infinte/src/core/Mesh.h`:

| Structure | Line | Layout | Note |
|---|---|---|---|
| `struct Vertex { px py pz nx ny nz u v }` inside `std::vector<Vertex> vertices` | `Mesh.h:10` (field at `:19`) | **AoS**, interleaved, 32 bytes/vertex | this is `P`, `N`, `uv` |
| `Mesh::faceMask` — `vector<unsigned char>`, 1 per triangle | `Mesh.h:29` | **SoA-shaped** | empty means "all faces selected" |
| `Mesh::selectionGroup` — `vector<unsigned int>`, 1 per triangle | `Mesh.h:37` | **SoA-shaped** | |
| `Mesh::vertexColor` — flat `vector<float>`, 3 per vertex | `Mesh.h:45` | **SoA-shaped** | this is `Cd`. The comment says it is a flat float vector *rather than* a `vector<Color>` "matching how InstanceOnPointsNode packs per-instance colour for upload — same shape means the same upload path works." |
| `struct Particle { px py pz vx vy vz nx ny nz scale r g b age life alive }` | `Mesh.h:265` | **AoS**, and defended | the comment at `:262` explicitly says keeping them apart "would mean walking two parallel arrays that must stay index-aligned" |
| `struct MeshPoint { px py pz nx ny nz scale index r g b }` | `Mesh.h:290` | **AoS** | what `MeshOps::ToPoints` / `DistributeOnFaces` return |
| `Polyline::points` — flat `vector<float>`, xyz triples | `Mesh.h:282` | **SoA-shaped** | |

**Summary to carry into your design: core P/N/uv is AoS; every attribute added
later is a parallel array.** The SoA precedent already exists in this file
(`vertexColor`), and `Mesh` already enforces a "size mismatch means absent"
convention (`HasVertexColor()` at `Mesh.h:60` returns
`vertexColor.size() == vertices.size() * 3`) that your store must not break.

> **OPEN — ask the owner before writing the store. Do not pick silently.**
>
> | Option | What it means | Cost |
> |---|---|---|
> | **(a)** Field owns its own SoA store and converts at the node boundary | one gather + one scatter per cook; **zero existing code changes** | the conversion may cost more than the AoS penalty at small N |
> | **(b)** add parallel SoA arrays to `Mesh` alongside `vertices`, migrate readers gradually | matches the `vertexColor` precedent | two sources of truth for P/N/uv until migration finishes |
> | **(c)** convert `Mesh::vertices` to SoA outright | correct end state | touches every 3D node and the Render3D upload path — very large diff |
>
> **Measure (a) at N = 5000 before anyone proposes (c).** Write the measurement
> into the commit message. (a) is the only option compatible with invariant 5
> ("steps 2–10 are additive"), so start there.

### 5.2 Build the SoA element store

```
   Mesh (AoS)                    ElementStore (SoA)
   ─────────                     ──────────────────
   vertices[i].px ─┐             px[0] px[1] px[2] ... px[N-1]
   vertices[i].py  ├─ gather ──▶ py[0] py[1] ...
   vertices[i].pz ─┘             pz[0] ...
   vertices[i].nx/ny/nz ───────▶ nx[] ny[] nz[]
   vertices[i].u/v      ───────▶ u[]  v[]
   vertexColor[3i..3i+2]───────▶ cr[] cg[] cb[]      (absent -> filled with 1,1,1)
   (declared `attrib float heat`)  heat[]            (fresh, initial value)
```

Requirements:

- One `std::vector<float>` per scalar lane. A `vec3` attribute is **three**
  parallel arrays, not one interleaved array of 3.
- **Allocate once, reuse across cooks.** `resize()` only when the element count
  actually changed. `field-realtime` §1 rule 8: no allocation in `CookIfNeeded`
  beyond what already existed.
- Round-trip fidelity is a hard requirement: `Mesh → store → Mesh` must be
  byte-identical for `vertices`, and must preserve `indices`, `faceMask`,
  `selectionGroup` and `vertexColor` **including their emptiness**. An empty
  `vertexColor` on the way in that comes back as an all-white array on the way
  out is a silent regression — it turns "use the material's flat colour" into
  "use per-vertex white" for every downstream node.
- Scatter back only the lanes the kernel actually wrote. Track written-ness at
  compile time, from the IR, not at runtime.

### 5.3 Add `element` to the reserved-word table and the domain lattice

| Domain | Reserved names |
|---|---|
| `frame` | `t` `dt` `frame` |
| `element` | `P` `N` `uv` `Cd` `i` `count` |

| Name | Type | Meaning |
|---|---|---|
| `P` | `vec3` | position, object space, read/write |
| `N` | `vec3` | normal, read/write |
| `uv` | `vec2` | texture coordinate, read/write |
| `Cd` | `vec3` | colour, read/write, backed by `Mesh::vertexColor` |
| `i` | `int` | this element's index, **read only** |
| `count` | `int` | total element count, **read only**, frame-domain-constant within a cook |

- `uv` appears in **two** domains (element and pixel). A kernel is only ever in
  one domain, so they never collide inside one body — but **the error message
  must name which domain it resolved in**, or the user cannot tell why a `uv`
  reference typed the way it did.
- A local variable may **not** shadow a reserved name. That is a **compile
  error, not a warning**, and the message names the domain the name belongs to.
  Precedent already in the codebase: `ExprGlobals::IsValidName`
  (`src/core/ExprGlobals.h:45`) already refuses `t`, `pi`, `lo`, `hi`.
- Lattice: `graph ⊑ frame ⊑ element`. `element`, `pixel` and `sample` are
  **mutually incomparable** — nothing joins them implicitly.

### 5.4 `attrib` declarations

```
attrib float heat = 0
heat += bass * 0.1
```

This is deliberately **stricter than VEX**, where `@heat` and `@heta` both
silently succeed and one of them is a typo you find three hours later.

| Wrong | Right | Error must say |
|---|---|---|
| `heat += 0.1` with no declaration | `attrib float heat = 0` first | the **use site's line and column**, and the undeclared name |
| `attrib float P` | pick another name | `P` is a reserved word of the `element` domain |
| `attrib heat = 0` | `attrib float heat = 0` | a type is required |
| two `attrib float heat` lines | one | duplicate declaration, with both spans |

An `attrib` is storage that lives on **every element of the domain**, for the
lifetime of the element. It is a lane in the SoA store, allocated at compile
time, initialised to its declared value when the store is (re)sized.

### 5.5 Domain inference and the hoist

This is the whole performance payoff. Implement it as the fixpoint
`field-compiler` §5 describes, then **prove the hoist happened**:

```
param-free worked example:

amount = 0.5 + 0.5 * sin(t)     # mentions t only  -> frame domain
P.y   += amount                  # mentions P       -> element domain
```

| IR node | Type | Domain | Placement |
|---|---|---|---|
| `t` | float | frame | prologue, once |
| `sin(t)` | float | frame | prologue, once |
| `amount` | float | frame | **hoisted out of the element loop** |
| `P.y` | float | element | inside the loop |
| the `+=` | — | element | inside the loop |

Emitted shape:

```
   [prologue]      frame-domain code, executed ONCE per cook
   for i in 0..count-1:
       [body]      element-domain code
```

The user writes no annotation to make that happen **and cannot write one to
prevent it**. Norilo Table 3 p.45 measured 2.25× from rate inference alone
(257 µs/1024 samples at krate=1 → 114 µs at krate=128, saturating near 32);
cite it, never claim novelty.

### 5.6 Bounded element counts

`field-realtime` §1 rule 6: element counts are **bounded and declared up
front**. Kronos accepts the same constraint for polyphony (Norilo p.46).

- The node carries a **maximum element count** as a parameter with a hard
  ceiling. Pick a defensible default (the codebase's own `ToPoints` /
  `RealizeInstances` use ceilings like `maxPoints` and `maxInstances = 256`;
  mirror that idiom).
- An incoming mesh larger than the ceiling is **truncated with a visible
  readout on the node**, never silently processed or silently dropped.
- `count` inside the kernel is the *actual* element count for this cook, which
  is ≤ the ceiling. It is a runtime value, so
  `for (i = 0; i < count; i++)` is **refused** — the bound must be a
  compile-time constant (`field-compiler` §8). It is also almost always a
  mistake, because a per-element kernel already runs once per element.

### 5.7 The node

Follow `.claude/skills/new-geometry-node/SKILL.md` and
`field-integration` §7 and §9. The specific things a fresh session gets wrong:

- **Forward every passthrough field.** An element-domain Field node derives its
  mesh from an upstream source, so it must forward `PassthroughSource()`,
  `SurfaceTextureRevision()`, `GetMappingTransform()`,
  `GetInstanceGroupMatrix()`, `InstanceSelection()`,
  `InstanceSelectionRevision()` and `InstanceTransformOverride()`. These are
  declared at `src/nodes/Geometry3DNodes.h:161`, `:149`, `:152`, `:168`,
  `:174`, `:175`, `:182`. **Forgetting one has already caused a real bug here**
  (env light cache invalidation).
- `MeshRevision()` is **pure virtual on purpose** — "a source that silently
  returned a constant would freeze its geometry at the first frame"
  (`Geometry3DNodes.h:130`). Bump it from `NextMeshRevision()`
  (`Mesh.h:259`) when and only when the mesh actually changed: upstream
  revision changed, or the kernel recompiled, or a `frame`-domain input to the
  hoisted prologue changed. **Do not bump every frame.**
- One geometry input and one geometry output. Input slots share **one index
  space** across kinds (`field-integration` §6) — a gap in the indices makes
  the generic pin count stop at the first gap, which shows up as a node with
  missing pins.
- Source text saves as a `Text` param via `VisitParams`, exactly the way
  `FormulaNode::formula` does (`src/nodes/FormulaNode.h:41`). `Patch.cpp`'s
  `EscapeLine`/`UnescapeLine` already turn an embedded newline into a literal
  `\n` and back. **Do not invent a second multi-line scheme.**
- Compile on the main thread, on edit — never inside `CookIfNeeded`. Copy
  `FormulaNode`'s retry policy: recompile only when there is no program **and**
  no recorded error (`src/nodes/FormulaNode.cpp:415`), so a broken program
  costs nothing per frame.
- Read `.claude/skills/node-ui-pillars/SKILL.md` before writing the body-draw
  function.

### 5.8 Fixtures

Add `INFINITE_FIELDELEMENTTEST` as an **early-exit, headless** fixture where
the checks are pure computation, in the shape of `INFINITE_DSPTEST` (gated at
`src/main.cpp:37619`, before `glfwInit()`), and an **in-frame** section in the
shape of `INFINITE_ROUNDTRIPTEST` (`src/main.cpp:43318`, fires at
`frameId == 4`) for anything needing a real spawned node. One verdict line per
section, ending `OK` or containing `FAIL` with the case name and both values.

Cases, from `field-testing` §5's element table:

| Case | Assert |
|---|---|
| N-element kernel | body ran exactly N times — **on a counter, not on the result** |
| hoisted subexpression | a frame-domain subexpression inside an element kernel ran exactly **once** — again on a counter; the result value is identical either way, which is exactly why a value assertion proves nothing |
| `P` writes land correctly | write `P.y = i` and read back a ramp |
| `count` | equals N |
| AoS/SoA round trip | `Mesh → store → Mesh` equality for `vertices`, `indices`, **and** the parallel `vertexColor` / `faceMask` / `selectionGroup` arrays, including empty-stays-empty |
| reserved-word shadowing | `float P = 1` errors, and the message names the `element` domain |
| undeclared `attrib` | `heat += 1` with no declaration errors at the **use site's line and column** |
| non-constant loop bound | `for (i = 0; i < count; i++)` is refused, naming which expression was not constant |
| element cap | a mesh above the ceiling truncates and the node shows it |

**A test that cannot fail is not a test.** For each case: introduce the bug
deliberately, watch the fixture print `FAIL`, then fix it.

---

## 6. Traps

| # | Trap | The bug it prevents |
|---|---|---|
| 1 | Emitting `@P` anywhere — code, docs, error messages, test fixtures | the sigil was removed by owner decision, permanently. A grep that finds one means the surface drifted from the contract. |
| 2 | Adding any syntax that lets the user declare a rate | rate is inferred; if the user can override inference, every optimisation guarantee evaporates |
| 3 | Building an **interleaved** element store (`struct { float x,y,z; }` in a vector) because it "looks like `Vertex`" | that is AoS again, and it is the 2–5× the whole SoA requirement exists to avoid |
| 4 | Storing a `vec3` attrib as one array of 3-float structs | same as trap 3 wearing a hat. One `vector<float>` per lane. |
| 5 | Round-tripping an empty `Mesh::vertexColor` back as all-white | flips every downstream node from "material flat colour" to "per-vertex white"; `Mesh::HasVertexColor()` (`Mesh.h:60`) is size-based and will now say yes |
| 6 | Resizing the store inside `CookIfNeeded` on every cook | allocation on the render hot path; `field-realtime` §1 rule 8 |
| 7 | Asserting the hoist worked by comparing result values | a hoisted and a non-hoisted expression give the **same answer**. Assert on an evaluation counter or the test proves nothing. |
| 8 | Not forwarding one of the seven `IGeometrySource` passthrough fields | the exact bug class that already shipped here (env light cache invalidation): material/texture/mapping/instances silently vanish two nodes downstream |
| 9 | `MeshRevision()` returning a fresh value every frame | forces a full GPU re-upload every frame for a mesh that did not change |
| 10 | `MeshRevision()` returning a constant | freezes the geometry at frame one. It is pure virtual precisely to stop you defaulting it. |
| 11 | Letting a local shadow `P`/`N`/`uv`/`Cd`/`i`/`count` with a warning | the contract says compile error. A warning in a text field nobody reads is not a diagnostic. |
| 12 | An error message that says "`uv` type error" without naming the domain | `uv` exists in both `element` and `pixel`; without the domain the user cannot tell why it typed the way it did |
| 13 | Accepting an undeclared attribute because "it is obviously a float" | this is precisely the VEX silent-typo failure the declaration requirement exists to delete |
| 14 | Compiling inside `CookIfNeeded`, or recompiling a failing program every frame | `FormulaNode.cpp:415` is the reference; a per-frame recompile is a visible stall |
| 15 | Making the element node non-additive by editing existing geometry nodes | invariant 5. If you need `Mesh` to change, that is §5.1's open question and it goes to the owner first. |
| 16 | Emitting a `for` whose trip count reads a runtime value (`count`, a param) | `field-realtime` §1 rule 3 — an unbounded loop cannot be unrolled or vectorised and cannot be budgeted |
| 17 | Assuming `element` and `pixel` can join implicitly because both are "spatial" | they are **incomparable** in the lattice. An implicit crossing must be a compile error with both spans, not a silent `map`. |
| 18 | Describing a branch in an element kernel as free | it is a real branch, but **the batch loses vectorization either way** (`field-realtime` §4). Any doc or tooltip showing branching syntax owes the reader the cost model. |

---

## 7. Machine-checkable exit criterion

Run all of these. Every one must pass.

```bash
cd /Users/namansoni/infinte

# 1. builds clean
cmake --build build -j"$(sysctl -n hw.ncpu)"

# 2. the element conformance fixture
INFINITE_FIELDELEMENTTEST=1 INFINITE_EXITAFTER=10 ./build/Infinite.app/Contents/MacOS/Infinite 2>&1 | tee /tmp/fieldelem.log
grep -c FAIL /tmp/fieldelem.log            # must print 0
grep -c BUG  /tmp/fieldelem.log            # must print 0

# 3. the language surface still has no sigils and declares no rates
grep -rn '@[A-Za-z]' .claude/skills/field-*/ docs/plans/field/ src/core/field/ src/nodes/FieldElementNode.* \
  | grep -v '@brief\|@param\|email' || echo "no sigils OK"
grep -rn 'krate\|@rate' src/core/field/ src/nodes/FieldElementNode.* && echo "RATE DECLARED - FAIL" || echo "no rate syntax OK"

# 4. no new cross-thread channel, no allocation shapes in the per-element path
git diff --stat main
git diff main -- src/core/field/ | grep -n 'std::atomic\|std::mutex\|std::function\|push_back\|make_shared\|new ' || echo "clean OK"

# 5. geometry passthrough is intact
.claude/skills/geometry-transform-sweep/driver.sh

# 6. the full gate
.claude/skills/run-infinite-hygiene/driver.sh

# 7. project convention
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

`GEOTEST`, `MESHOPTEST`, `TRANSFORMSWEEPTEST`, `MAPPINGSWEEPTEST`,
`REVISIONSWEEPTEST`, `ROUNDTRIPTEST` and `PATCHTEST` are the hygiene entries
that directly guard what step 4 touches; all must pass, and the
`AUDIOPARAMSWEEPTEST` xfail baseline must not grow.

---

## 8. Out of scope for this step

| Not in step 4 | Where it lands |
|---|---|
| `param float x = 0.5 [0, 2]` and any `ParamRef` registration | step 5 |
| `state` cells of any kind, in any domain | step 6 |
| The `pixel` domain, GLSL transpilation, `GLUtil::CompileProgram` | step 7 |
| `reduce`, `map`, `resample`, `downsample` — including "just a simple `reduce.rms` so audio can drive geometry" | step 8 |
| The `sample` domain, `ParamMailbox`, anything on the audio thread | step 9 |
| The `graph` domain | step 10 |
| Converting `Mesh::vertices` to SoA | §5.1 option (c) — a separate, owner-approved piece of work with its own brief |
| Changing `Expression::Evaluate`'s signature or its three call sites (`src/main.cpp:37506`, `src/core/ExprGlobals.cpp:72`, `src/nodes/AnalyzeNodes.cpp:179`) | never |
| Changing the patch file format or `Patch::NodeRecord` | never in this step |
| Touching `WouldCreateAudioCycle` / `WouldCreateNoteCycle` (the node-level cycle ban, `src/main.cpp` ~2511) | never — that is a different rule at a different level |

If you find a genuine bug in existing code while in here, **report it rather
than fixing it inline**.
