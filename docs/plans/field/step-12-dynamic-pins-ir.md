# Field build step 12 — dynamic pins, Phase 2a: declaration syntax and the compiler layer

You are implementing **build step 12 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). This is a
self-contained brief; you have no prior context on Field and do not need any
beyond what is listed under "Files to read first".

Line numbers are from `src/` at the commit this was written against — re-grep
the symbol if a number has drifted. **The symbol is authoritative, not the
number, and the code is authoritative over any doc, including this one and
including the `field-compiler`/`field-domains` skills where they conflict
with what is actually in the tree — §1.6 records one such conflict found
while writing this doc.**

**Prerequisite:** `feature/field-step-11-dynamic-pins-phase1` merged, and its
exit criterion (`docs/plans/field/step-11-dynamic-pins-phase1.md` §7) green.
Step 11 proved the node/UI/save-load plumbing with a hardcoded menu; this
step is the compiler-side work that makes pin declarations come from kernel
source text instead. If step 11's `INFINITE_FIELDPINSTEST` does not exist and
pass, stop and say so.

This step is **compiler and IR work only** — `src/core/field/`. It produces
no user-visible pin yet; nothing in `src/nodes/` or `src/main.cpp` changes.
Step 13 consumes what this step builds and wires it into
`FieldElementNode`/`FieldSampleNode`/`FieldPixelNode`. Step 14 does the same
for `FieldGraphNode`, plus the emit()-vs-pins distinction restated at higher
stakes than step 11 §5.5.

---

## 1. Invariants, and one settled design decision per open question

### 1.1 Clean room (non-negotiable)

Infinite is **MIT**. **Never** open, read, grep, or reference GPL sources:
Kronos (`bitbucket.org/vnorilo/k3`), Cmajor, SuperCollider, or BespokeSynth
(also at `/Users/namansoni/BespokeSynth` — do not open it). The Kronos
*paper* (Norilo, CMJ 39:4, 2015) is citable freely; its code is not. Safe:
Faust (LGPL), ChucK, Houdini VEX docs, TidalCycles docs.

### 1.2 No sigils, bare names

`output float bass = reduce.rms(in, 20, 200)`, never `@bass` or `out@bass`.

### 1.3 Rate is inferred for the *value*; domain is *declared* for the *pin*

This sounds like a contradiction with `field-language` §3 ("rate is inferred,
never declared") and it is not — read this carefully, because it is the one
new idea in this step's syntax.

- **The value a `output`/`input` statement carries still gets its domain by
  inference**, exactly as every other expression does — nothing about
  inference itself changes.
- **The domain word in `output <domain> <type> <name> = <expr>` is not a rate
  annotation on the expression.** It is the domain **of the pin itself** —
  i.e. which of the node's rate contexts (Frame or the kernel's own ambient
  domain) the value crosses into on its way out to another node. It plays the
  same role `reduce`'s target domain already plays (`field-domains` §3): it
  is where the operator is going, not a wish about where the computation
  should run.
- The inferred domain of `<expr>` must be **no finer than** the declared pin
  domain (§5.3). This is a **join check against a stated target**, the same
  shape as every existing domain-join check in the compiler
  (`field-compiler` §5 step 2) — it is not a new inference mechanism, and it
  is not a way to force a value into a coarser domain than it actually is.

### 1.4 Both phases ship; this step is the first half of Phase 2

Per the owner's decisions (recorded in full in
`docs/plans/field/design-brief-dynamic-pins.md` and step 11 §0): the target
is universal, kernel-declared, arbitrary in/out pins of any domain on all
four Field node types, e.g. a `FieldElementNode` with two geometry inputs and
one audio input whose kernel makes geometry react to audio. This step does
not build that combination directly — it builds the compiler primitive
(`output`/`input` declarations, collected per-program, diffable by name)
that steps 13–14 assemble into it, one node type at a time.

### 1.5 Cable-orphaning: refuse, don't drop — this step's half of the contract

Per decision 4: an `Apply()` that would remove or retype a pin with a live
cable must be refused, keeping the last successfully compiled program
running, exactly like `FormulaNode::Apply()` (`src/nodes/FormulaNode.cpp:390`)
already does for a syntax error. **This step's job is to make that decision
possible, not to make it** — the compiler cannot see cables (it has no
`INode*`, no `gNodes`). What this step delivers is a **name-keyed diff**
between the previous program's declared pins and the new one's (§5.5); step
13/14 turn that diff into an accept/refuse decision by checking cables and
call `Apply()`'s existing keep-last-working-program path when refusing.

### 1.6 Discrepancy found while writing this step — the sample backend does not share the typed IR

`field-compiler` §6.1/§6.3 and `field-domains` describe "both backends lower
the same typed IR" as a general property. **It is not true for `sample`.**
`src/core/field/BackendRegister.cpp`'s own header comment (`:9-19`) states it
explicitly:

> "A dedicated compiler for the sample domain rather than a shared pass with
> `FieldIR.cpp`'s typed-IR Element/Pixel lowering: the sample domain's target
> is straight-line register bytecode with no runtime branches … which is a
> different enough code-generation problem … that sharing IRNode/IRStmt would
> mean bolting a second, mismatched codegen target onto a data structure
> designed for the GLSL-shaped Element/Pixel backends."

`CompileSampleProgram` (`BackendRegister.h:16-19`) goes straight from
`FieldAst`/`FieldLex`/`FieldParse` to `SampleProgram` — it never touches
`FieldIR.h`'s `IRStmt`/`ElementIRProgram`/`PixelIRProgram` types at all.

**Consequence for this step:** declared-output/input collection cannot be
added once, centrally, in `FieldIR.h`, and expected to cover `sample`. It
must be added **twice**, in parallel, with the same *name*, *type*, *domain*
shape but two separate implementations:

1. In `FieldIR.h`/`FieldParse.cpp`/`FieldIR.cpp` — for `Element`, `Pixel`,
   and (where applicable, see §5.7) `Graph`.
2. Directly in `BackendRegister.h`/`.cpp` against `SampleProgram` — for
   `Sample`.

Do not try to unify these into one pass as part of this step; that is a much
larger refactor than dynamic pins and is explicitly out of scope (§8).
Keep the *declaration grammar* (§5.1) and the *diff contract* (§5.5)
identical between the two implementations so step 13 has one API to consume,
even though the lowering code is duplicated by necessity.

### 1.7 Only additive changes to existing programs

Every existing Field kernel — every preset, every corpus fixture, every
saved patch — compiles today with **zero** `output`/`input` declarations.
Nothing in this step may change what those compile to. `INFINITE_FIELDTEST`
section A must stay at 170/170 (`step-09-sample-domain.md` §0.2's rule,
restated because it applies here too).

---

## 2. Goal

Give Field two new declarations — `output` and `input` — whose presence in a
kernel's source text is what Phase 2 (steps 13–14) reads to decide a Field
node's pin shape, instead of the C++-hardcoded menu step 11 used. This step
delivers:

1. Parsing and AST nodes for both declarations (§5.1).
2. IR collection (`declaredOutputs`, `declaredInputs`) on `ElementIRProgram`
   and `PixelIRProgram`, and the parallel, separately-implemented equivalent
   directly on `SampleProgram` (§1.6, §5.2).
3. Domain-inference extension: each declared pin's expression is checked
   against its declared domain by the existing join machinery, with no new
   fixpoint algorithm (§5.3).
4. A stable, name-keyed, append-only identity scheme for declared pins,
   modelled directly on the `ParamTable` class Field already ships (§5.4) —
   this is what makes save-format addressing (step 13) and the
   cable-orphaning refusal (§5.5) possible at all.
5. A `PinDiff` value — added/removed/retyped/redomained declared pins,
   computed by comparing the previous program's stable-id-keyed pin list to
   the new one's — that a node's `Apply()` reads to decide accept-or-refuse.

No node changes. No UI changes. No `main.cpp` changes.

---

## 3. Files to read first, and why

### Docs

| File | Why |
|---|---|
| `docs/plans/field/design-brief-dynamic-pins.md` | the full brief this whole sequence executes — read in full |
| `docs/plans/field/step-11-dynamic-pins-phase1.md` | what Phase 1 proved, and the four node types' Phase-1 menu entries this step's syntax must be able to eventually subsume (not replace yet) |
| `docs/plans/field/step-05-param-declarations.md` §5.1, §5.3 | the *exact same identity problem* (`param`'s stable `paramIndex`) already solved once, by the `ParamTable` class this step reuses the shape of |
| `docs/plans/field/step-08-transfer-operators.md` | `reduce`'s domain-target argument — the precedent for "a declared target domain that the compiler checks the expression against" |

### Skills

| File | Why |
|---|---|
| `.claude/skills/field-compiler/SKILL.md` | §4 the AST node set (`AstDeclAttrib` is the closest existing precedent, §5 domain inference as a fixpoint — read this before touching the join check in §5.3 |
| `.claude/skills/field-domains/SKILL.md` | §1-3 the lattice and `reduce`'s domain-target shape; §10 cite it, but see §1.6 above for where it (and `field-compiler`) is wrong about shared IR |
| `.claude/skills/field-integration/SKILL.md` | §3's `param`-identity open question, already resolved by `ParamTable` — read it to see the shape you are about to copy |
| `.claude/skills/field-realtime/SKILL.md` | §8 (via `field-compiler`) what the front end must refuse — extend the same table, do not write a second one |

### Real source — the code wins over any doc or skill

| File | Lines that matter | Why |
|---|---|---|
| `src/core/field/FieldAst.h` | `AstDeclAttrib` `:153-161`, `AstDeclParam` `:163` onward | the AST shape `AstDeclOutput`/`AstDeclInput` are modelled on |
| `src/core/field/FieldParse.cpp` | the reserved-word checks `:544-567`; `AstDeclAttrib` parse returning at `:621`; the `attrib <type> <name> = <expr>` grammar around it | where `output`/`input` parsing is added, next to `attrib`'s |
| `src/core/field/FieldIR.h` | `enum class IRStmtKind` `:69-84`; `struct DeclaredState` `:89-97` (the exact shape `DeclaredOutput`/`DeclaredInput` are modelled on: `name`, `typeName`, `DataType type`, `Domain domain`, `SourceSpan span` — `DeclaredState` additionally carries `lanes`/`initialValues`, which a pin does not need); `GraphIRProgram` `:175-179`; `ElementIRProgram` `:181-190` (`declaredAttribs` `:186`, `declaredParams` `:187`, `declaredStates` `:188`); `PixelIRProgram` `:192-198` | where `declaredOutputs`/`declaredInputs` are added |
| `src/core/field/FieldIR.cpp` | `LowerElementProgramToIR` / `LowerPixelProgramToIR` (declared in `FieldIR.h:207`, `:210`) | where the new `IRStmtKind` cases are lowered and collected |
| `src/core/field/ParamTable.h` | the **entire file, 57 lines** — `DeclaredParam` `:10-16`, `ParamEntry` `:18-26` (`id`, `name`, `value`, `defaultValue`, `minValue`, `maxValue`, `isDeclared`), `class ParamTable` `:28-54` (`Reconcile(declared, nodeIndex, outNotice)`, `Find`/`FindById`, `NextParamId`/`SetNextParamId`, `SerializeParamMap`/`DeserializeParamMap`) | **this is the file to copy the shape of, almost verbatim, for pin identity** — see §5.4 |
| `src/core/field/ParamTable.cpp` | `Reconcile`'s body | the exact append-only, name-keyed, never-reused-id algorithm — a `param` declaration and a pin declaration have the identical identity problem |
| `src/core/field/BackendRegister.h` / `.cpp` | header comment `:9-19` (§1.6); `IsReservedName` `BackendRegister.cpp:24-28`; `struct Ctx` `:30` onward | the sample backend's separate compilation path — read before writing the parallel `declaredOutputs`/`declaredInputs` collection for it |
| `src/core/field/SampleProgram.h` | `struct SampleProgram` `:85` onward; `hasReduceRms`/`reduceLoHz`/`reduceHiHz` `:95-97` (the existing precedent for "a fact about the compiled program, exposed as a plain field, read by the node layer") | where `SampleProgram`'s own declared-pin lists live |
| `src/core/field/GlslBackend.cpp` | `fld_srcTex` uniform `:484`; `vec4 src = texture(fld_srcTex, vUv)` `:581` | **how the pixel domain's existing image input already works** — `src` is not a first-class Field type, it is a reserved name backed by a hardcoded `sampler2D` uniform. §5.6 extends this pattern rather than inventing a `image` `DataType` |
| `src/nodes/FieldSampleNode.h` | `ReadRmsLatest` `:64-68` | the exact value this step's `output` syntax should be able to express as `output frame float bass = reduce.rms(in, 20, 200)`, replacing step 11's hardcoded `exposeRmsOutput` toggle in step 13 |

---

## 4. Files to create / modify

### Create

| Path | Contents |
|---|---|
| `src/core/field/PinTable.h` / `.cpp` | `Field::PinTable` — the stable-id, name-keyed, append-only identity scheme for declared pins. A near-verbatim copy of `ParamTable`'s shape (§5.4), parameterised so it can hold either output or input declarations, one instance per direction per node. |

### Modify

| Path | Change |
|---|---|
| `src/core/field/FieldAst.h` | add `AstDeclOutput`, `AstDeclInput`, modelled on `AstDeclAttrib` (`:153-161`) |
| `src/core/field/FieldParse.cpp` | parse `output <domain> <type> <name> = <expr>` and `input <domain> <type> <name>`; extend the reserved-word checks at `:544-567` so a declared pin name cannot shadow a reserved attribute, an existing `attrib`/`param`/`state`, or another declared pin |
| `src/core/field/FieldIR.h` | add `DeclOutput`, `DeclInput` to `IRStmtKind` (`:69-84`); add `struct DeclaredOutput` / `struct DeclaredInput` next to `DeclaredState` (`:89-97`); add `declaredOutputs`/`declaredInputs` vectors to `ElementIRProgram` (`:181-190`) and `PixelIRProgram` (`:192-198`) |
| `src/core/field/FieldIR.cpp` | lower `DeclOutput`/`DeclInput` statements; run §5.3's domain-join check against each declared pin's target domain; collect into the new vectors |
| `src/core/field/SampleProgram.h` | add the sample-domain equivalent of `declaredOutputs`/`declaredInputs` — plain fixed-size arrays (no `std::string`, no `std::vector` — `SampleProgram` is a POD the audio thread's *identity* never reads, but keep it consistent with the rest of the struct's no-heap discipline since `FieldSampleNode`'s main-thread half copies it around) |
| `src/core/field/BackendRegister.cpp` | parse and collect the sample-domain `output`/`input` declarations, independently of `FieldIR.cpp`'s implementation (§1.6) |
| `CMakeLists.txt` | add `src/core/field/PinTable.cpp` (core sources, `:206` neighbourhood) |
| `.claude/skills/run-infinite-hygiene/driver.sh` | register this step's fixture (§7) in `TIER1_CHECKS` — re-grep the line number, it drifts every step |

### Must not be modified

- `src/nodes/FieldElementNode.*`, `FieldSampleNode.*`, `FieldPixelNode.*`,
  `FieldGraphNode.*` — step 13/14.
- `src/main.cpp` — nothing in this step is user-visible.
- `src/core/Patch.h` / `.cpp`, `src/core/Modulation.h`, `src/core/INode.h` —
  no save-format or pin-contract change belongs in the compiler layer.
- `Expression.h`/`.cpp`'s public API and its three call sites
  (`src/main.cpp:37506`, `src/core/ExprGlobals.cpp:72`,
  `src/nodes/AnalyzeNodes.cpp:179`).

---

## 5. Procedure

### 5.1 Declaration syntax — settling the brief's open question 1

The brief's open question 1 asked whether declaration syntax should be a
keyword mirroring `attrib`, or purely inferred from what the expression
touches. **Decision: an explicit keyword, symmetric for both directions.**
Pure inference cannot work for `input` at all (there is nothing to infer an
*input*'s domain/type *from* — it has no expression), so whatever syntax is
chosen for `input` has to be explicit; keeping `output` explicit too avoids
having two different declaration philosophies in one language.

```
output <domain> <type> <name> = <expr>
input  <domain> <type> <name>
```

| Piece | Meaning |
|---|---|
| `output` / `input` | new keywords, added to the `KEYWORD` token set next to `attrib`/`param`/`state` (`field-compiler` §2) |
| `<domain>` | one of `frame`, `element`, `pixel`, `sample` (not `graph` — see §5.7) — the domain of the **pin**, not a rate annotation (§1.3) |
| `<type>` | `float`, `int`, `bool`, `vec2`, `vec3`, `vec4` — the existing Field type set, **plus** the new pseudo-type `image` for a texture-backed pin (§5.6). `image` is legal only when `<domain>` is `pixel`. |
| `<name>` | the pin's identity, must be a valid Field identifier, must not shadow a reserved attribute name of any domain, an existing `attrib`/`param`/`state`, or another declared pin in the same program |
| `= <expr>` | **required for `output`**, absent for `input` — an input has no expression, it is a value the pin *provides to* the kernel |

Worked examples:

```
# FieldElementNode kernel with a second, frame-domain scalar output
output frame float publish = length(P) * heat

# FieldSampleNode kernel exposing its existing internal RMS meter as a real pin
output frame float bass = reduce.rms(in, 20, 200)

# FieldElementNode kernel with a declared SECOND geometry input and one audio input
# (the user's own example from the design brief)
input element geometry other
input sample  audio    sidechain
P += (other.P - P) * 0.1 * reduce.rms(sidechain, 20, 200)
```

(`geometry`/`audio` as `<type>` values are covered in §5.6 alongside
`image` — none of the four Field scalar/vector types can represent a whole
mesh or an audio buffer, so all three "structural" pin types — `geometry`,
`audio`, `image` — are handled the same way: a reserved name backed by a
node-level resource, not a value living in a register.)

| Wrong | Right | Error must say |
|---|---|---|
| `output publish = length(P)` | `output frame float publish = length(P)` | domain and type are both required, no inference from the expression |
| `output frame float t = ...` | pick another name | `t` is a reserved attribute of the `frame` domain |
| `output frame float bass = ...` twice | one declaration | duplicate output declaration, both spans |
| `input frame float k = 1.0` | `input frame float k` | an `input` never has an initializer — that is what `param` is for |
| `output graph float x = 1.0` | not legal — `graph` has no per-cook value to publish as a node pin | "a `graph`-domain value cannot be a node output pin; graph-domain constants stay inside the kernel — see step 14 if you meant to emit a node" |

### 5.2 IR collection

Add `IRStmtKind::DeclOutput` / `IRStmtKind::DeclInput` (`FieldIR.h:69-84`),
each carrying the same span/name/type/domain triple as `DeclaredState`
(`:89-97`) but with no `initialValues`/`lanes` (a pin has no memory — it is
recomputed every cook, unlike a `state` cell). Collect into new
`declaredOutputs`/`declaredInputs` vectors on `ElementIRProgram` (`:181-190`)
and `PixelIRProgram` (`:192-198`), exactly parallel to how `declaredAttribs`/
`declaredParams`/`declaredStates` are already collected.

For `sample`: per §1.6, this is a **second, separate implementation**
directly in `BackendRegister.cpp`/`SampleProgram.h`, parsing the identical
grammar (§5.1) but emitting into `SampleProgram`'s own fixed-size arrays
instead of a shared IR vector. Keep the field names (`declaredOutputs`/
`declaredInputs`) and per-entry shape (`name`, `typeName`, `Domain domain`,
`SourceSpan span`) identical in spirit even though the storage is a
different C++ type, so step 13's code reading both is not fighting two
incompatible shapes.

### 5.3 The domain-join check — no new fixpoint

Per §1.3: after the existing domain-inference fixpoint converges
(`field-compiler` §5), for every `output <domain> <type> <name> = <expr>`
statement, check:

```
if not (domain(expr) ⊑ declaredDomain):
    error at expr's span, naming both domains and the declared one
```

using the exact same `⊑` (join/comparability) relation the fixpoint already
computes elsewhere (`field-compiler` §5 step 2). This is a **single
post-fixpoint assertion per declared output**, the same shape `step-10`'s §5.2
rate-zero check already used for the graph domain ("after that fixpoint
converges, add one walk") — **no new pass, no new algorithm.** `input`
declarations need no join check at all: an input has no expression, its
declared domain is simply what domain the value arrives *at* when the
kernel reads its name (seeded into the fixpoint exactly the way `P`/`N`/
`in`/`out`/`uv` already are, per reserved-name seeding, `field-compiler` §5
step 1).

Error example:

```
FieldElement.field:4:16: error: `output frame float publish = ...` declares a
   frame-domain pin, but the expression on the right is element-domain (it
   reads `P`).
   Fix: reduce it first, e.g. `output frame float publish = reduce.mean(...)`,
   or declare the pin as `output element float publish = ...` instead.
```

### 5.4 Pin identity — `PinTable`, copied from `ParamTable`

This is the load-bearing piece of this step, and the brief's open question 4
(save-format migration) and half of open question 5 (cable-orphaning) both
reduce to getting this right.

**The problem is identical to `param`'s `paramIndex` problem, already solved.**
`Patch::CableRecord` addresses a pin by a bare integer
(`Patch.h:72-83`), but a kernel's declared-output list can be reordered,
renamed, added-to and removed-from on every edit. Reusing declaration
*ordinal* as the saved index is exactly the bug `ParamTable::Reconcile`
already exists to prevent for `param`s (`step-05-param-declarations.md`
§5.1 OPEN 1, resolved there by the `ParamTable` class this codebase already
ships).

**Decision: reuse the identical scheme, in a new class with the identical
shape.** `Field::PinTable` (`src/core/field/PinTable.h`, new):

```cpp
namespace Field
{
   struct DeclaredPin       // mirrors DeclaredOutput/DeclaredInput's shape
   {
      std::string name;
      std::string typeName;
      Domain domain = Domain::Frame;
   };

   struct PinEntry           // mirrors ParamTable::ParamEntry
   {
      int id = 0;
      std::string name;
      std::string typeName;
      Domain domain = Domain::Frame;
      bool isDeclared = false;   // false = this slot's cable, if any, is now orphaned
   };

   class PinTable
   {
   public:
      // Same contract as ParamTable::Reconcile: assigns a fresh id to a
      // name never seen before, keeps the existing id for a name that is
      // still declared with the same type+domain, and marks an entry
      // isDeclared=false (never removes it, never reuses its id) when its
      // name is no longer declared or its type/domain changed.
      void Reconcile(const std::vector<DeclaredPin>& declared, int nodeIndex, std::string& outNotice);

      const std::vector<PinEntry>& Pins() const;
      const PinEntry* Find(const std::string& name) const;
      const PinEntry* FindById(int id) const;

      int NextPinId() const;
      void SetNextPinId(int nextId);

      std::string SerializePinMap() const;
      void DeserializePinMap(const std::string& str);
   };
}
```

The one deliberate difference from `ParamTable::Reconcile`: **a name whose
type or domain changed between compiles is treated as removed, not updated
in place.** `ParamTable`'s params are all `float`, so a type change cannot
happen there; a pin's type/domain can change (`output frame float x = ...`
edited to `output element vec3 x = ...`), and that is a different pin as far
as any live cable is concerned — a `vec3` cable target reading a `float`
source is not the same connection, so treat it as `(old id retired,
isDeclared=false) + (new id allocated)`, exactly like a rename.

`isDeclared=false` is the field step 13/14's `Apply()` reads to build the
refusal decision (§5.5) — `PinTable` itself never looks at cables and never
refuses anything; it only ever **adds** facts, never removes an id.

### 5.5 The diff — what `Apply()` gets to decide on

After a successful compile, the node's `Apply()` (step 13/14) calls
`Reconcile()` and then asks, for every `PinEntry` where `isDeclared` just
flipped from `true` to `false`: **is this id's pin currently wired?** That
check needs `gNodes`/cable state, which the compiler cannot see (§1.5) —
this step's contribution stops at making the *retired* set discoverable by
id, deterministically, from a name-only diff of two `PinTable` snapshots.
Concretely, `PinTable::Reconcile` takes the *previous* table's state
implicitly (it is a method on the existing instance, not a static function)
so the node layer can simply compare `Pins()` before and after the call.

**What must NOT happen in this step:** `PinTable::Reconcile` must not
itself refuse anything or roll back. It always accepts the new declaration
list and always updates the table. The keep-last-working-program discipline
(decision 4) lives one layer up, in the node's `Apply()`
(`FieldElementNode.cpp:48` et al.), which — **after** seeing that a compile
succeeded and **before** committing to the new program/table — checks
whether any newly-retired pin id has a live cable, and if so, discards the
whole compile result (including the `PinTable` update) and keeps running the
previous program exactly as it did before the attempted `Apply()`. This
mirrors `FormulaNode::Apply()`'s "compile into a local, swap only on
success" shape (`field-compiler` §7) one level up: here the "success"
condition additionally includes "and no live cable was orphaned", not just
"and it compiled".

### 5.6 `geometry` / `audio` / `image` as reserved pin type-names, not `DataType`s

None of Field's four scalar/vector `DataType`s (`field-compiler` §4) can
represent a mesh, an audio buffer, or a texture — these are node-level
resources, not per-element register values. Extend the existing precedent
(§5.6 of this doc references `GlslBackend.cpp:484,581`'s `src` — a reserved
`vec4`-valued name backed by a hardcoded `sampler2D` uniform) to the general
case, rather than inventing a new `DataType`:

| Declared `<type>` | Legal `<domain>` | What the name resolves to inside the kernel |
|---|---|---|
| `geometry` | `element` | not a value at all — it identifies a *second mesh source* the node reads. A kernel referencing `other.P`/`other.N`/`other.<attrib>` (dotted access on the declared name) reads that second geometry's per-element attributes, aligned to the ambient element by index — out of range is a compile-time-bounded read, clamped, per `field-realtime`'s no-unbounded-anything rule |
| `audio` | `sample` | resolves like `in` does today — a `float`, one sample at a time, from a **second** audio input distinct from the node's native `in` |
| `image` | `pixel` | resolves like `src` does today (`GlslBackend.cpp:581`) — a `vec4` sampled at `vUv` from a second texture input |

This table is deliberately narrow: it covers exactly the combinations the
owner's own example requires (a `FieldElementNode` with two geometry inputs
and one audio input) plus the pixel case symmetry requires it to also
support. A `geometry`-typed pin read from `pixel` domain, or an
`image`-typed pin read from `element` domain, is a **cross-domain resample**
(`field-domains` §5) and is explicitly **out of scope for this step** — flag
it as a compile error naming `resample` as the concept that would be needed,
not a silent implicit conversion. Extending this table to more crossings is
future work, not this step's.

### 5.7 Why `output`/`input` are not legal in the `graph` domain

Per §5.1's refusal row: a `graph`-domain kernel has no per-cook value at all
(step 10 §5.2, "`graph` is a source, never a sink" — there is no transfer
*into* `graph`, and a graph kernel's "output" is the set of nodes it emits,
which is an entirely different mechanism, see step 14 §5.5's restatement of
step 11 §5.5's distinction). `output`/`input` inside a `FieldGraphNode`'s
kernel are refused at parse time with a message pointing at `emit`/`connect`/
`set`/`place` as the actual mechanism for that node type. **Step 14 adds a
different, additive mechanism (ordinary `INode` pins on the `FieldGraphNode`
itself, driven by declarations outside the `graph`-domain kernel body) — that
is not this syntax and does not reuse `GraphIRProgram::declaredParams`.**

### 5.8 What the front end must additionally refuse

Extend `field-compiler` §8's table, do not write a second one:

| Refuse | Message must say |
|---|---|
| `output`/`input` inside a `graph`-domain kernel | that `graph` has no per-cook value; points at `emit`/`connect`/`set`/`place` |
| a declared output's domain finer than its expression's inferred domain — wait, this direction is always legal via broadcast; refuse the **opposite**: expression domain finer than declared | names both domains, suggests `reduce` (§5.3) |
| two declared pins (same direction) with the same name | both spans |
| a declared pin's name shadowing a reserved attribute, an `attrib`, a `param`, a `state`, or a pin in the other direction | which domain/declaration owns the name |
| `image`/`geometry`/`audio` used with a `<domain>` other than the one row §5.6 allows | the one legal domain for that type |
| more than a stated ceiling of declared pins in one kernel (pick a defensible number, e.g. 16, and cite it in the message, the same way `ParamMailbox::kMaxParams` is cited for the 128 `param` ceiling) | the count and the ceiling |

---

## 6. Traps

| # | Trap | The bug it prevents |
|---|---|---|
| 1 | Adding `declaredOutputs`/`declaredInputs` only to `FieldIR.h` and assuming `sample` picks it up | it does not — `BackendRegister.cpp` never touches `FieldIR.h` (§1.6). A `FieldSampleNode` kernel with `output`/`input` declarations that silently compiles with them ignored is worse than a refusal |
| 2 | Making `PinTable::Reconcile` decide accept-or-refuse | it cannot see cables; that decision belongs one layer up, in the node's `Apply()` (§5.5) — keep the compiler layer pure |
| 3 | Treating a type/domain change on an existing pin name as an in-place update | it must retire the old id and mint a new one (§5.4) — a live cable on the old type is a different connection, not an upgraded one |
| 4 | Inventing an `image`/`geometry`/`audio` `DataType` alongside `float`/`int`/`bool`/`vecN` | these are node-level resources, not register values — §5.6 extends the existing reserved-name-backed-by-a-uniform/second-source pattern instead |
| 5 | Letting `output`/`input` parse inside a graph-domain kernel and only catching it in the domain-join check | refuse at parse time with a message pointing at the real mechanism (§5.7) — a late refusal after the user has half-written a kernel around it is a worse experience |
| 6 | Writing a second, ordinal-keyed identity scheme "just for pins" instead of reusing `ParamTable`'s exact algorithm | this is precisely the mistake `step-05`'s OPEN 1 already made and un-made once; reusing the shape verbatim is not laziness, it is the one already-correct answer in this codebase |
| 7 | Recomputing `PinTable` from scratch on every compile instead of reconciling against the live instance | destroys the append-only id guarantee (§5.4) the moment a temporarily-failed compile is followed by a successful one with a reordered declaration list |

---

## 7. Machine-checkable exit criterion

```bash
cd /Users/namansoni/infinte
cmake --build build -j"$(sysctl -n hw.ncpu)"

# every existing Field harness must be unaffected — this step is additive
for v in FIELDTEST FIELDELEMENTTEST FIELDPARAMTEST FIELDSTATETEST FIELDSAMPLETEST \
         FIELDGRAPHTEST FIELDGRAPHRATETEST FIELDGRAPHUNDOTEST FIELDGRAPHBLASTTEST FIELDPINSTEST; do
  env INFINITE_$v=1 ./build/Infinite.app/Contents/MacOS/Infinite | tee /tmp/$v.log | tail -5
  grep -c FAIL /tmp/$v.log
done

# this step's own fixture — headless, early-exit, no spawned node needed
# (this is pure compiler surface: parse, infer, refuse, PinTable.Reconcile)
env INFINITE_FIELDPINDECLTEST=1 ./build/Infinite.app/Contents/MacOS/Infinite | tee /tmp/fieldpindecl.log
grep -c FAIL /tmp/fieldpindecl.log

.claude/skills/run-infinite-hygiene/driver.sh
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

`INFINITE_FIELDPINDECLTEST` must assert:

1. every wrong/right row in §5.1 and every refusal row in §5.8, each error
   naming its span;
2. a valid `output frame float x = reduce.rms(in, 20, 200)` inside a sample
   kernel compiles via `BackendRegister.cpp`'s path and appears in
   `SampleProgram`'s declared-outputs list — proving §1.6's parallel
   implementation actually exists, not just the shared-IR one;
3. a valid `output`/`input` pair inside an element kernel appears in
   `ElementIRProgram::declaredOutputs`/`declaredInputs`;
4. `PinTable::Reconcile` called twice with the same declaration list produces
   the same ids both times (idempotence);
5. `PinTable::Reconcile` called with a renamed pin retires the old id
   (`isDeclared=false`, still present in `Pins()`) and mints a new one;
6. `PinTable::Reconcile` called with the same name but a changed type/domain
   also retires-and-mints, not updates-in-place (§5.4's deliberate
   difference from `ParamTable`);
7. `SerializePinMap`/`DeserializePinMap` round-trip a table with at least one
   retired entry.

---

## 8. Out of scope for this step

| Not in step 12 | Where it lands |
|---|---|
| Any change to `FieldElementNode`/`FieldSampleNode`/`FieldPixelNode`'s actual `OutputCount()`/`GeometryInputSlot`/`AudioInputSlot` | step 13 |
| `FieldGraphNode` gaining declared pins, and the emit()-vs-pins distinction restated for Phase 2 | step 14 |
| The four hand-maintained image-pin `dynamic_cast` chains in `main.cpp` | step 13 (for image-typed outputs/aux inputs) |
| `Patch::CableRecord` actually addressing a declared pin by its `PinTable` id | step 13 |
| Undo/redo/copy-paste behaviour for a kernel-driven pin count change | step 13 |
| Unifying the sample backend with the shared typed IR (§1.6) | not planned anywhere in this sequence — a separate, much larger refactor, out of scope for dynamic pins entirely |
| Any change to `Expression::Evaluate`'s signature or its three call sites | never |
