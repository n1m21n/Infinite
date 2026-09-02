---
name: field-compiler
description: How Field source text becomes running code in Infinite — the lexer/AST/typed-IR pipeline, the token grammar, the AST node set, what the typed IR carries (type AND domain per node), domain inference as a dataflow fixpoint, the three backends (bytecode VM for frame/element, GLSL text for pixel, register machine for sample), error reporting modelled on FormulaNode's keep-last-working-program behaviour, and how the IR stays retargetable to C++ and WASM. Use when implementing or reviewing any part of the Field compiler, when restructuring src/core/Expression.cpp behind its existing API (build step 1), when adding a backend, when a Field program types or infers the wrong domain, when deciding what the IR must carry, or when writing the prompt for a session that will build any compiler stage.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

Read [`field-language`](../field-language/SKILL.md) first — this skill assumes
its syntax and domain model. Cross-links:
[`field-state`](../field-state/SKILL.md) (delay lowering),
[`field-domains`](../field-domains/SKILL.md) (transfer operators),
[`field-realtime`](../field-realtime/SKILL.md) (what the backend must refuse),
[`field-integration`](../field-integration/SKILL.md) (where output lands),
[`field-testing`](../field-testing/SKILL.md) (how to prove it did not regress).

---

## 0. Invariants

1. **Clean room.** Never read Kronos, Cmajor, SuperCollider or BespokeSynth
   source. Cite the Kronos *paper* (Norilo, CMJ 39:4, 2015) freely. Faust
   (LGPL), ChucK, and Houdini VEX docs are safe to read.
2. **The typed IR is the durable asset, not the syntax.** Syntax is a
   front-end; backends are back-ends; the IR outlives both. Any change that
   makes the IR harder to retarget is a regression even if every test passes.
3. **Step 1 keeps an identical public API.** `Expression::Evaluate` keeps its
   exact signature. Existing `=` expressions in saved patches are the
   regression corpus.
4. **A failed compile never blanks the output.** The last working program keeps
   running. This is already how `FormulaNode::Apply()` behaves and it is the
   model for every Field backend.

---

## 1. The pipeline

```
   source text
        │
        ▼
   ┌──────────┐   tokens        (§2)
   │  lexer   │──────────────┐
   └──────────┘              │
        ▼                    │  spans (line, col, len) ride every token
   ┌──────────┐   AST         │  and every AST node, all the way to the IR —
   │  parser  │──────────────┤  this is what makes error messages point at
   └──────────┘              │  the character the user typed
        ▼                    │
   ┌─────────────────────┐   │
   │ type + domain infer │◀──┘   (§4, §5)
   └─────────────────────┘
        ▼
   ┌──────────────────────────────────────┐
   │   TYPED IR   — the durable asset     │   every node carries TYPE and DOMAIN
   └──────────────────────────────────────┘
        │
        ├──▶ bytecode         frame / element   register VM        (§6.1)
        ├──▶ GLSL text        pixel             GLUtil::CompileProgram (§6.2)
        ├──▶ register machine sample                               (§6.3)
        └──▶ (later) C++ source, WASM, and inbound Faust / GLSL front-ends
```

**Why not an interpreter in the sample domain.** Ertl & Gregg report 50–98%
branch misprediction for typical interpreters; on Sandy Bridge a miss costs
~18 cycles, in which the chip could have retired 144 floating-point operations
(Norilo p.31). Cite this whenever anyone proposes interpreting at 48 kHz.

## 2. Token grammar

| Token | Pattern | Notes |
|---|---|---|
| `NUMBER` | `[0-9]+ ( '.' [0-9]* )? ( [eE] [+-]? [0-9]+ )?` and `'.' [0-9]+` | lexed as **double**; see §3 |
| `IDENT` | `[A-Za-z_][A-Za-z0-9_]*` | keywords are recognised after lexing, not in the pattern |
| `KEYWORD` | `attrib` `param` `state` `if` `else` `for` `float` `int` `bool` `vec2` `vec3` `vec4` `true` `false` | plus the reserved attribute names of the current domain (`field-language` §5) |
| `OP` | `+ - * / % ^` `+= -= *= /=` `= == != < <= > >=` `&& \|\| !` `.` | maximal munch: `<=` before `<`, `==` before `=`, `!=` before `!` |
| `PUNCT` | `( ) { } , [ ] ;` | `[ ]` only inside a `param` range |
| `COMMENT` | `#` to end of line | discarded, but its span is kept for the editor |
| `NEWLINE` | significant as a statement terminator | `;` is also accepted |
| `EOF` | — | |

**Maximal-munch traps that have already bitten this codebase.**
`src/core/Expression.cpp:56` carries a comment about exactly this: a naive
`Consume('<')` matches the `<` of `<=` and leaves `=` to be misread as the
start of an operand. `src/core/Expression.cpp:362` repeats the note for `!=`.
The lexer must produce `<=` `>=` `==` `!=` `&&` `||` `+=` `-=` `*=` `/=` as
single tokens — this is precisely the class of bug the tokenizer exists to
delete, and the current parser only avoids it by ordering its `ConsumeStr`
calls by hand.

**Every token carries `{ offset, line, col, length }`.** Not optional. The
error UX in §7 is impossible without it, and today's evaluator has none — its
errors are prose with no position.

## 3. Numeric precision — a real regression risk

`src/core/Expression.cpp` evaluates entirely in **`double`** and narrows to
`float` only in the final assignment `outValue = (float)result;`
(Expression.cpp, end of `Evaluate`). A compiler that lexes to `float` or does
its constant folding in `float` will produce values that differ in the last
bits from every saved patch.

| Rule | |
|---|---|
| Lexing | literals to `double` |
| Constant folding in the IR | `double` |
| `frame` bytecode VM | `double` accumulators, narrow once at the boundary |
| `element` bytecode VM | `float` is acceptable — no saved-patch corpus depends on it |
| `pixel` GLSL | `float` — GLSL 150 `highp` float; a `double` result is not representable and this is a **documented, tested** divergence, not a bug |
| `sample` register machine | `float` unless a specific filter needs `double` state |

`field-testing` §3 requires the frame-domain golden values to match at
`double` precision and the pixel-domain ones to match within a stated epsilon.

## 4. The AST node set

Small on purpose. If a construct cannot be expressed with these, it probably
should not be in v1.

| Node | Children | Carries |
|---|---|---|
| `Program` | list of `Decl` and `Stmt` | source span |
| `DeclAttrib` | optional init `Expr` | name, type |
| `DeclParam` | optional init `Expr` | name, type, min, max |
| `DeclState` | optional init `Expr` | name, type |
| `Assign` | lvalue `Access`, rvalue `Expr` | op (`=` `+=` `-=` `*=` `/=`) |
| `If` | cond `Expr`, then `Block`, optional else `Block` | — |
| `For` | init, cond, step, `Block` | **bound must be a compile-time constant** (§8) |
| `Block` | list of `Stmt` | — |
| `Binary` | lhs, rhs | op |
| `Unary` | operand | op (`-` `!`) |
| `Call` | args | callee name |
| `Access` | base | field/swizzle string (`x`, `xz`, `rgb`) |
| `Ident` | — | name; resolves to reserved attribute, attrib, param, state, or local |
| `Literal` | — | double value or bool |

Two nodes that look like they belong here and do not:

- **`Kernel`** — there is no explicit kernel node. A `Program` *is* the kernel
  body; which domain it is a kernel of is inferred (§5), not written.
- **`Transfer`** — `reduce`, `map`, `broadcast`, `resample`, `downsample` are
  `Call` nodes whose callee is a transfer operator. Inference (§5) gives them
  their special domain behaviour; the parser does not need to know about them.
  See [`field-domains`](../field-domains/SKILL.md).

## 5. Domain inference as a dataflow fixpoint

Every IR node carries **two** facts: its type and its domain. Type inference is
ordinary bottom-up unification with the rank-polymorphism rule from
`field-language` §9. Domain inference is a fixpoint over the same graph.

Domains form a lattice, coarsest first:

```
   graph  ⊑  frame  ⊑  element        (spatial fan-out)
   graph  ⊑  frame  ⊑  pixel          (spatial fan-out)
   graph  ⊑  frame  ⊑  sample         (temporal fan-out)
```

`element`, `pixel` and `sample` are mutually incomparable — nothing joins them
implicitly. Crossing between them **requires an explicit transfer operator**;
an implicit crossing is a compile error, not a silent `map`.

The algorithm:

1. **Seed.** Every reserved attribute name gets its domain from
   `field-language` §5. Every literal and every `param` gets `graph`. Every
   `state` cell gets the domain of the kernel it is declared in.
2. **Propagate.** `domain(node) = join(domain(children))` under the lattice
   above, where `join` picks the finer of two comparable domains and **errors**
   on two incomparable ones.
3. **Transfer nodes override.** `reduce(x, …)` has domain
   `coarsen(domain(x))`; `resample(x, D)` has domain `D`; `downsample(x, k)`
   keeps its input's domain but records a divisor `k` used by the backend.
4. **Iterate to a fixpoint.** A `state` cell's read is a back-edge, so one
   bottom-up pass is not enough. Repeat step 2 until nothing changes.
   Termination is guaranteed: the lattice is finite and domains only ever move
   finer.
5. **Placement.** Every node whose domain is strictly coarser than the kernel's
   is hoisted out of the per-element loop and evaluated once. **That hoist is
   the entire performance payoff of rate inference** (Norilo Table 3, p.45:
   2.25× from this alone).

Worked example:

```
param float depth = 1.0 [0, 2]      # graph
amount = 0.5 + 0.5 * sin(t)         # sin(t): t is frame  -> amount is frame
P.y   += amount * depth             # P is element        -> statement is element
```

| Node | Type | Domain | Placement |
|---|---|---|---|
| `depth` | float | graph | uniform / mailbox slot |
| `t` | float | frame | per-frame |
| `sin(t)` | float | frame | per-frame |
| `amount` | float | frame | hoisted out of the element loop |
| `amount * depth` | float | frame | hoisted |
| `P.y` | float | element | inside the loop |
| the `+=` | — | element | inside the loop |

**The error message matters more than the algorithm.** When the join fails,
report both operands' domains, both spans, and the transfer operator that would
fix it — "`in` is sample-domain and `P` is element-domain; wrap it, e.g.
`reduce.rms(in, 20, 200)`". A bare "type error" here is useless, because the
user never wrote a domain anywhere.

## 6. The three backends lower the same IR

### 6.1 Bytecode VM — `frame` and `element`

- Register machine, not a stack machine (see §6.3's rationale — the same
  argument applies).
- The per-element loop is the outer structure; hoisted (§5.5) frame-domain code
  is emitted once as a prologue.
- **Element attributes are addressed as parallel arrays.** See §9 for what
  `src/core/Mesh.h` actually gives you today, which is not that.
- Bounded `for` loops are unrolled when the bound is small enough to be worth
  it, kept as a counted loop otherwise. Never a data-dependent trip count
  (`field-realtime` §2).
- Branches are real branches. The cost is a lost vectorization opportunity for
  the batch, not a correctness issue.

### 6.2 GLSL text — `pixel`

The target is `GLUtil::CompileProgram(const char* fragSrc, std::string* outError)`
(`src/core/GLUtil.h:34`), which supplies its own vertex shader with
`aPos`/`aUv` bound at locations 0 and 1 and hands the fragment stage a `vUv`.

**Emit `#version 150`, not 330.** Every one of the 57 `#version` directives in
`src/` is `#version 150` — the app targets an OpenGL 3.2 core profile. A
generated `#version 330` shader will compile on some drivers and not others and
will diverge between macOS and Windows. Match `FormulaNode`'s preamble shape
(`src/nodes/FormulaNode.cpp:11`):

```glsl
#version 150
in vec2 vUv;
out vec4 fragColor;
uniform float uTime;
// ... one uniform per `param`, one sampler per `state` cell's read texture
```

Lowering rules specific to this backend:

| IR construct | GLSL |
|---|---|
| `param` | a `uniform float` |
| `attrib` | a local in `main()` (pixel attributes do not persist between frames) |
| `state` | a **ping-pong texture pair**: read from sampler, write to `fragColor`'s target — see [`field-state`](../field-state/SKILL.md) §4 |
| `if` | **predication** — evaluate both sides, `mix()`/`?:` the result |
| bounded `for` | a literal-bound `for`; GLSL 150 requires a constant bound anyway |
| `%` on floats | `mod()` |
| `^` (if kept) | `pow()` — never emit `^`, which is XOR in GLSL |
| `reduce` | **not lowerable here.** A reduction is a coarser domain; it is computed on the CPU or in a prior pass and arrives as a uniform |

### 6.3 Register machine — `sample`

- **Register machine, not a stack VM, and not an interpreter over the AST.**
  The Ertl & Gregg / Norilo p.31 numbers in §1 are the justification: a
  dispatch-per-node interpreter at 48 kHz spends its budget on mispredicted
  indirect branches.
- Every `state` cell is a fixed register slot allocated at compile time. No
  allocation ever happens after `PrepareToPlay`.
- `param` values arrive through `ParamMailbox::SmoothedValue(paramId)`
  (`src/audio/ParamMailbox.h:39`) — one atomic slot per param, one-pole
  smoothed on the consumer side, **max 128 params**
  (`ParamMailbox::kMaxParams`). Exceeding that is a compile error with a clear
  message, not a silent truncation.
- The whole audio-thread prohibition list applies to generated code as much as
  to hand-written code: no allocation, no locks, no `dynamic_cast`, no
  `std::function`/`map`/`string`, no GL, no ImGui, no file I/O, no `printf`.
  See [`field-realtime`](../field-realtime/SKILL.md).

### 6.4 Retargetability — what keeps C++/WASM possible

The IR stays retargetable only if these hold. Check them on every IR change:

| Rule | Why |
|---|---|
| No backend-specific node types in the IR | the moment a `GlslMix` node exists, the C++ backend has to special-case it |
| Domain and type are **on the node**, never in a side table keyed by pointer | a side table does not survive serialization, and the IR must serialize for a standalone export |
| Every intrinsic has a **semantic** definition, not "whatever GLSL does" | `mod` differs between GLSL, C `fmod` and Haskell; pick one and make every backend match it |
| No node's meaning depends on evaluation order except through explicit `state` | the C++ and WASM backends will schedule differently |
| Sizes are compile-time constants in the IR itself | a WASM backend cannot ask the host for a size at codegen time |

Faust's standalone C++ export is the model for the shape of the C++ target,
and Faust is LGPL — its documentation and generated-code conventions are safe
to read.

## 7. Error reporting and recovery — model it on `FormulaNode`

`FormulaNode::Apply()` (`src/nodes/FormulaNode.cpp:390`) is the behaviour to
copy exactly:

```cpp
unsigned int program = GLUtil::CompileProgram(src.c_str(), &error);
if (program == 0) { mLastError = error; return false; }   // old mProgram untouched
if (mProgram != 0) glDeleteProgram(mProgram);
mProgram = program;
mLastError.clear();
```

The four properties that matter, in the order they matter:

1. **A failed compile keeps the last working program.** The graph never blanks
   and the app never crashes on a typo mid-edit.
2. **The error text is surfaced, not swallowed.** `LastError()` is read by the
   node body.
3. **It does not retry every frame.** `FormulaNode::CookIfNeeded` recompiles
   only when `mProgram == 0 && mLastError.empty()`
   (`src/nodes/FormulaNode.cpp:415`) — so a broken program costs nothing per
   frame. A Field node that recompiles a failing program 60 times a second is
   a bug, and at pixel-domain shader-compile cost it is a visible one.
4. **The same policy already exists on the expression side.** The per-frame
   apply loop at `src/main.cpp:37506` leaves the last good value in place when
   `Expression::Evaluate` fails, and records the error via
   `Modulation::SetExpressionError`. Field must not regress that: a failing
   Field param keeps its last good value.

**Parse-error recovery.** Recover at statement boundaries (newline or `;`), so
one bad line yields one error rather than a cascade. Report at most ~5 errors,
then stop — `GLUtil::CompileProgram` truncates the driver log at 1024 chars
(`src/core/GLUtil.cpp`, `char log[1024]`), so a long cascade loses the first,
most useful message anyway.

**Error record shape:**

| Field | |
|---|---|
| `severity` | error / warning |
| `span` | line, col, length — from the token (§2) |
| `message` | one sentence, names the identifier or type at fault |
| `hint` | optional; for a domain-join failure, the transfer operator that fixes it (§5) |

## 8. What the front end must refuse

These are compile errors, checked in the typed IR before any backend runs.
The rationale for each is in [`field-realtime`](../field-realtime/SKILL.md).

| Refuse | Message must say |
|---|---|
| any heap allocation construct | (v1 has none — this is a check on the *compiler's own* generated code) |
| recursion, direct or mutual | the cycle of names |
| a loop whose bound is not a compile-time constant | which expression was not constant |
| a value whose size is not known at compile time | which declaration |
| a local shadowing a reserved attribute name | which domain the name belongs to |
| an `attrib` used with no declaration | the use site's line and column |
| a dataflow cycle with no `state` on it | the cycle, node by node |
| more than 128 `param`s in a sample kernel | the limit and its source (`ParamMailbox::kMaxParams`) |

## 9. Memory layout — check this before building the element backend

The design brief requires attributes to be **structure-of-arrays**; an AoS
layout costs a permanent 2–5×. **What the code does today is AoS**, and it
says so on purpose:

| Structure | File | Layout |
|---|---|---|
| `Vertex { px py pz nx ny nz u v }` in `std::vector<Vertex>` | `src/core/Mesh.h:10` | **AoS**, interleaved, 32 bytes/vertex |
| `Particle { px py pz vx vy vz nx ny nz scale r g b age life alive }` | `src/core/Mesh.h:265` | **AoS**, and the comment at :260 explicitly defends it: "keeping them apart would mean walking two parallel arrays that must stay index-aligned" |
| `MeshPoint { px py pz nx ny nz scale index r g b }` | `src/core/Mesh.h:290` | **AoS** |
| `Mesh::vertexColor` — flat `vector<float>`, 3 per vertex | `src/core/Mesh.h:45` | **SoA-shaped**, parallel to `vertices` |
| `Mesh::faceMask` — `vector<unsigned char>`, 1 per triangle | `src/core/Mesh.h:29` | **SoA-shaped** |
| `Mesh::selectionGroup` — `vector<unsigned int>`, 1 per triangle | `src/core/Mesh.h:37` | **SoA-shaped** |
| `Polyline::points` — flat `vector<float>`, xyz triples | `src/core/Mesh.h:282` | **SoA-shaped** |

So the codebase is **hybrid**: core position/normal/uv is AoS; every attribute
added later is a parallel array. The precedent for SoA already exists — the
comment on `vertexColor` even notes it was made a flat float vector rather than
a `vector<Color>` to match the instancing upload path.

> **OPEN — how does the element domain get SoA without rewriting `Mesh`?**
> Options: **(a)** Field owns its own SoA attribute store and converts to/from
> `Mesh` at the node boundary (one gather + one scatter per cook; no existing
> code changes; the conversion may cost more than the AoS penalty for small N);
> **(b)** add parallel SoA arrays to `Mesh` alongside `vertices` and migrate
> readers gradually (matches the existing `vertexColor` precedent, but two
> sources of truth for P/N/uv until migration finishes);
> **(c)** convert `Mesh::vertices` to SoA outright (correct end state, touches
> every 3D node and the `Render3D` upload path — very large diff).
> Do not pick silently. Measure (a) at N = 5000 before proposing (c).

## 10. Build order and what each step must not break

Only step 1 touches existing code. Steps 2–10 are additive — new files, new
node types.

| Step | What | Touches existing code? |
|---|---|---|
| 1 | split `Expression.cpp` into lexer → AST → typed IR → bytecode, **identical public API** | **yes** — the only step that does |
| 2 | `rand`/`noise`/`sh` become pure `(t, seed)` | yes — a documented, visible break |
| 3 | `vec2/3/4` + rank polymorphism | additive |
| 4 | the `element` domain | additive (plus §9's open question) |
| 5 | `param` auto-declaration wired to `ParamRef` | additive |
| 6 | `state` cells | additive |
| 7 | `pixel` via GLSL transpile into `GLUtil::CompileProgram` | additive |
| 8 | `reduce`, `map`, `resample`, `downsample` | additive |
| 9 | `sample` domain | additive |
| 10 | `graph` domain | additive |

**Step 1's non-negotiable:** `Expression::Evaluate(const std::string&, double,
const std::map<std::string,float>*, const std::map<std::string,float>*,
float&, std::string&)` keeps its exact signature. Its three call sites are
`src/main.cpp:37506`, `src/core/ExprGlobals.cpp:72`, and
`src/nodes/AnalyzeNodes.cpp:179` — none of them may change.

**A caching opportunity step 1 unlocks, and a trap inside it.** Today the
expression is *re-parsed on every evaluation* by design
(`src/core/Expression.h:9`: "expressions are short, so the cost is negligible").
Once there is an IR, caching it keyed by text is the obvious win. The trap:
`src/main.cpp:37500` **mutates the sibling map** to bind `lo`/`hi` and restores
it afterwards, and `AnalyzeNodes.cpp` binds a completely different variable set
(`lum`, `sat`, `hue`, `u`, `v`, `motion`, …). A cache keyed on text alone,
with identifier resolution baked into the IR, will resolve `min`/`max` — which
are *functions* in one caller and *bound variables* in `AnalyzeNodes` — the
wrong way. Key the cache on `(text, binding-set identity)`, or resolve
identifiers late.

## 11. Exit criterion for a compiler change

1. It builds clean.
2. Every existing `=` expression in the regression corpus
   ([`field-testing`](../field-testing/SKILL.md) §2) evaluates to its golden
   value at `double` precision, or the diff is the documented step-2 randomness
   break.
3. The three `Expression::Evaluate` call sites are unchanged.
4. Every construct in §8 is refused, with a message naming the span.
5. A deliberately broken program leaves the previously working program running,
   surfaces its error text, and does **not** recompile on the next frame (§7).
6. Domain inference reaches a fixpoint on every corpus program, and a
   hand-written incomparable-domain program errors with both spans and a
   transfer-operator hint.
7. No backend-specific node type entered the IR (§6.4).
8. `/run-infinite-hygiene` passes.
