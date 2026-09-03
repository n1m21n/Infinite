# Field build step 3 — `vec2` / `vec3` / `vec4` and APL-style rank polymorphism

**Repo:** `/Users/namansoni/infinte` (C++17, ImGui + OpenGL, MIT).
**Branch first** (`.claude/skills/git-branch-workflow`): `feature/field-step-03-vectors-and-rank`.

Self-contained implementation prompt. Read the whole file before writing code.
Line numbers are from `src/` at the commit this was written against — re-grep the
**symbol** if a number has drifted; the symbol is authoritative, the number is not.

**Prerequisite steps that must already be finished and merged:**

| Step | What it delivered | How to confirm |
|---|---|---|
| 1 | `src/core/Expression.cpp` restructured into lexer → AST → typed IR → bytecode behind a byte-identical `Expression::Evaluate`; the frozen golden corpus; the `INFINITE_FIELDTEST` fixture with sections A–D | `ls src/core/field/` (or wherever step 1 put it) is non-empty; `INFINITE_FIELDTEST=1 build/Infinite.app/Contents/MacOS/Infinite` prints sections A–C with no `FAIL`; the corpus data file exists, split into `deterministic` and `random` sets | 
| 2 | `rand`/`noise`/`sh` are seeded hash functions; the `random` corpus set re-baselined in its own commit | `grep -rln Xorwise src/` prints exactly one path; the corpus header records the re-baseline commit |

If either is missing, **stop and say so.** Step 3 has no independent value: it is
a type-system change measured entirely by "every existing scalar value is
unchanged", and that is unverifiable without step 1's corpus. See
`docs/plans/field/step-01-expression-to-ir.md` and
`docs/plans/field/step-02-pure-randomness.md`.

---

## 1. Invariants — restated verbatim, because you cannot infer them

### 1.1 Clean room (hard rule, non-negotiable)

Infinite is **MIT**. The following are GPL and **must not be opened, read,
grepped, copied or referenced at source level**:

| Project | Where |
|---|---|
| Kronos (GPLv3) | `bitbucket.org/vnorilo/k3` |
| Cmajor (GPLv3) | — |
| SuperCollider (GPL3) | — |
| BespokeSynth (GPLv3) | also at `/Users/namansoni/BespokeSynth` **on this machine — do not open it** |

Papers and public documentation are fine. The Kronos *paper* (Norilo, "Kronos: A
Declarative Metaprogramming Language for Digital Signal Processing", Computer
Music Journal 39:4, 2015) may be cited freely; its code may not be read.
**Safe to read:** Faust (LGPL), ChucK, Houdini VEX documentation, the **GLSL
specification and the OpenGL wiki** (this step's closest published prior art for
swizzles and constructors), TidalCycles docs and papers (not its source), and
Iverson's APL papers for rank polymorphism.

If you catch yourself about to run `grep` inside `/Users/namansoni/BespokeSynth`,
stop.

### 1.2 Bare names. No sigils. Ever.

An earlier draft used a VEX-style `@` sigil. The owner **removed it**. Plain
ASCII, no special characters, in every example, every doc, every test fixture,
every error message.

| Wrong | Right |
|---|---|
| `@P.y += bass * 2` | `P.y += bass * 2` |
| `@Cd = vec3(1,0,0)` | `Cd = vec3(1,0,0)` |
| `v@P` / `f@heat` | `P` / `heat` |

This matters more here than anywhere else so far: the `.` you are adding to the
lexer is the **only** punctuation Field puts before a component name. There is no
`@`, no `v@`, no `f@`.

### 1.3 Rate is inferred, never declared

No `@rate` keyword, no `krate` parameter, no domain annotation on a binding. A
`vec3` does not carry a rate; the node it lives on gets its domain from
inference, exactly as a scalar does. Adding a vector type must not add a place
where a rate could be written.

### 1.4 The other invariants

| # | Invariant | Source |
|---|---|---|
| 1 | **`Expression::Evaluate` keeps its exact signature.** All three call sites stay unchanged. | `field-compiler` §0.3, §10 |
| 2 | **The typed IR is the durable asset, not the syntax.** A change that makes the IR harder to retarget to GLSL / C++ / WASM is a regression even if every test passes. | `field-compiler` §0.2, §6.4 |
| 3 | **Type AND domain live on the IR node**, never in a side table keyed by pointer. A `vec` type is a node field. | `field-compiler` §6.4 |
| 4 | **A failed compile never blanks the output.** The last working value keeps running. | `field-compiler` §0.4, §7 |
| 5 | **Field is one primitive:** a kernel is a body of code run once per element of a domain. A vector is a *value*, not a second primitive. | `field-language` §1 |
| 6 | **Broadcast goes scalar → vector only.** There is no implicit `vec2` → `vec3` fill, no truncation, and no `float` → `int` demotion. | `field-language` §9 |
| 7 | **A test that cannot fail is not a test.** Break each case deliberately, watch `FAIL`, fix it. | `field-testing` §0.2 |
| 8 | **Real-time safety:** no heap allocation past init, every value's size known at compile time, no dynamic arrays. A `vec4` is 4 lanes decided by the type checker, never by a runtime length. | `field-realtime` §1 rules 1, 4, 5 |
| 9 | **Only step 1 touches existing code.** Step 3 is **additive**: new type-checker rules, new opcodes, new files. If your diff edits a node, something is wrong. | `field-compiler` §10 |

### 1.5 Do not

- Do not introduce a runtime `int` type for **numeric literals**. See §5.1 — it
  silently changes `5/2` from `2.5` to `2` in every saved patch.
- Do not add a `bvec2/3/4`. `field-language` §9's type list has `bool` and no
  vector of it. See §5.6.
- Do not touch the patch file format, `Patch::Write`/`Read`, or the `expr` /
  `glob` line grammar.
- Do not change `rand`/`noise`/`sh`. Step 2 owns them, and a vec-valued `rand`
  is not in the design.
- Do not re-baseline any corpus value. Step 3's whole claim is that **no
  existing value changes**. See §8.1.
- Do not commit or push unless the owner asks.

---

## 2. Goal

Add `vec2`, `vec3` and `vec4` to Field's type system, its typed IR, its bytecode
and its VM, together with APL-style rank polymorphism — a scalar used where a
vector is expected broadcasts across every lane, and nothing else broadcasts.
Add read swizzles (`P.xy`, `uv.yx`, `Cd.bgr`) and the three constructor forms
(`vec3(1)`, `vec3(x, y, z)`, `vec3(v2, z)`). The step is measured almost entirely
by what does **not** change: `Expression::Evaluate` keeps its exact
six-parameter signature and its `float&` out-parameter, all three call sites stay
byte-identical, and **every record in step 1's `deterministic` corpus still
evaluates to its golden value at `double` exactness** — a scalar program must not
acquire a new meaning because a vector type appeared next to it. There is no
`vec` node in the graph, no editor and no UI in this step; the deliverable is
type-checker rules, opcodes, a VM that executes them, and the fixtures that prove
the illegal cases are loud.

```
   step 1's IR node                 step 3's IR node
   ────────────────                 ────────────────
   { op, type=float, domain,        { op, type ∈ {float, vec2, vec3, vec4},
     children }                       lanes = 1..4, domain, children }
                                              │
   register: double v                register: double v[4]   ── §5.7 option (A)
                                              │
   VM:  r[d] = f(r[a])               VM:  for (l = 0; l < inst.lanes; l++)
                                               r[d].v[l] = f(r[a].v[l])
```

---

## 3. Files to read first, and why

### Skills — the authoritative contract

| File | Why |
|---|---|
| `.claude/skills/field-language/SKILL.md` | **§9 is the spec for this step** — the type list, the rank rule, and the three worked wrong/right lines. §10 the operator table and the **OPEN `^` question this step must close**. §5 reserved words (`P` is a `vec3`, `uv` a `vec2`, `Cd` a `vec3` — you are building the types they will have in step 4). §14 rows 6 and 9 |
| `.claude/skills/field-compiler/SKILL.md` | §2 the token grammar (the `.` you are adding); **§3 numeric precision — read it twice, it is trap §8.2**; §4 the AST node set (`Access` already exists in it; do not add a `Swizzle` node); §6.1 the bytecode VM; §6.2 the GLSL lowering table your semantics must stay compatible with; **§6.4 retargetability** |
| `.claude/skills/field-testing/SKILL.md` | §2 the corpus record shape; **§3 section D is your acceptance list** — "scalar→vector broadcast works; vec2→vec3 is refused; `.xz` and `.bgr` swizzles resolve"; §6 step 3's exit row: "every corpus record still passes (scalars must not change meaning)" |
| `.claude/skills/field-realtime/SKILL.md` | §1 rules 1, 4, 5 — a vector is a compile-time-sized value, never a dynamic array; §3 where allocation hides; run §1 against your own diff at the end |
| `.claude/skills/run-infinite-hygiene/SKILL.md` + `driver.sh` | the gate. `TIER1_CHECKS` at `driver.sh:79`, `FULL_TESTS` at `driver.sh:173` |
| `.claude/skills/git-branch-workflow/SKILL.md` | branch before you start |

### Real source — the code wins over any skill

| Path | Why |
|---|---|
| `src/core/Expression.h` (48 lines) | **this header is the specification of what must not break.** `^` is documented at **line 30**; the function list at `:32-33`; the no-boolean-type rationale at `:38-43`; the signature at `:44-47` |
| `src/core/Expression.cpp:77-90` (`ParseNumber`) | the number lexer that already accepts a leading `.`. This is the maximal-munch collision in §5.4 |
| `src/core/Expression.cpp:113-231` (`CallFunction`) | the complete intrinsic list. Note what is **absent**: no `mix`, no `length`, no `dot`, no `normalize`, no `fract`, no `vec2`/`vec3`/`vec4`. Verified: `mix(0,1,0.5)` → `unknown function 'mix'` |
| `src/core/Expression.cpp:262-265` | **function-ness is decided by the following `(`,** not by the name. This is why a sibling named `vec3` and the constructor `vec3(...)` can coexist |
| `src/core/Expression.cpp:292-303` (`ParsePower`) | `^`, right-associative, calling `ParseAtom` for the base — which is why unary minus binds tighter (§8.9) |
| `src/core/Expression.cpp:425` | `outValue = (float)result;` — the single narrowing point. Everything above it is `double` |
| `src/nodes/AnalyzeNodes.cpp:150-179` | call site 3, and the reason §8.5 exists: it binds a 22-name set including the **single letters `r g b a l s h u v`** and binds `min`/`max` as **variables** |
| `src/main.cpp:37499-37509` | call site 1, and the `lo`/`hi` sibling-map mutation |
| `src/core/ExprGlobals.h:30-36` | `Global::value` is a **`float`**, and `Values()` is a `map<string, float>`. A global can never hold a vector — §8.11 |
| `src/core/Modulation.h:28-45` | `ParamRef::value` is a **`float*`**. A modulation destination is one float — §8.12 |
| `src/main.cpp:51664-51673` | the in-app language reference. It lists operators and functions; it becomes wrong the moment `vec3` exists |
| `CMakeLists.txt:218` | where `src/core/Expression.cpp` is listed; new `.cpp` files go beside it |
| `docs/CODE_STANDARDS.md` | house style |

---

## 4. Files to create / modify

### Create

| Path | Contents |
|---|---|
| `src/core/field/FieldTypes.h` | the type lattice as data: `Type { kind, lanes }`, the promotion/broadcast table of §5.2, the swizzle-set tables, and `JoinRank(Type, Type)` returning either a type or an error. **One table, read by the type checker, the emitter and every error message.** Two copies will drift |
| `src/core/field/FieldSwizzle.h` / `.cpp` | swizzle parsing and validation: set membership, arity bound, duplicate detection for lvalues |

### Modify (Field's own files from steps 1–2, all additive)

| Path | Change |
|---|---|
| step 1's lexer (`FieldLex.*`) | emit `.` as an `OP` token; the one-dot-per-number rule of §5.4 |
| step 1's parser (`FieldParse.*`) | `Access` postfix; the constructor call forms are ordinary `Call` nodes — **do not add a `Swizzle` or `VecCtor` AST node**, `field-compiler` §4's set is the whole set |
| step 1's IR + inference (`FieldIR.*`) | `lanes` on the node's `type`; the rank rules; the intrinsic rank table of §5.6; constant folding **per lane, in `double`** |
| step 1's emitter (`FieldBytecode.*`) | `lanes` on the instruction; the swizzle/shuffle opcode; the constructor opcode |
| step 1's VM (`FieldVM.*`) | the register widening of §5.7 |
| step 1's adapter (`src/core/Expression.cpp`) | the top-level-vec refusal of §5.8. **The `Evaluate` signature does not change** |
| `src/core/Expression.h` | comments only — the function list at `:30-33` gains `vec2 vec3 vec4` and (if §5.6 adds it) `mix`. **Do not touch the declaration at `:44-47`** |
| step 1's corpus data file | **additive only.** New records for the new surface. **Zero existing records change** — §8.1 |
| `src/main.cpp:51664-51673` | the in-app language reference gains the vector types and swizzles |
| `CMakeLists.txt` (~line 218) | the new `.cpp` files |

### Must not be modified

| Path | Why |
|---|---|
| `src/main.cpp:37507`, `src/core/ExprGlobals.cpp:72`, `src/nodes/AnalyzeNodes.cpp:179` | the three `Expression::Evaluate` call lines |
| `src/core/Expression.h:44-47` | the declaration |
| `src/core/Modulation.h`, `src/core/ExprGlobals.h` | `float*` and `float` destinations. Making them vector-capable is not this step and is probably not ever |
| any existing value in step 1's corpus | invariant 1.5; §8.1 |
| `src/core/Patch.*` | no file-format change |

---

## 5. The design

### 5.1 The type lattice, and the `int` decision

```
                    ┌─ vec2 ─┐
   int  ──▶  float ─┼─ vec3 ─┼──  (broadcast, scalar → vector only)
                    └─ vec4 ─┘

   vec2, vec3, vec4 are MUTUALLY INCOMPARABLE.
   bool is the result of a comparison; it is not a numeric rank.
```

| Rule | |
|---|---|
| `int ⊑ float` | int promotes to float; **float never demotes to int** without an explicit call (`field-language` §9) |
| `float ⊑ vecN` | scalar broadcasts to every lane |
| `vecN` vs `vecM`, `N ≠ M` | **incomparable — a compile error, never a truncation or a fill** |
| Rank is a **compile-time** property | `lanes ∈ {1,2,3,4}`, on the IR node. Never read from a runtime value (`field-realtime` §1 rule 5) |

> **The `int` trap, and the decision this step must make explicitly.** Today
> **every numeric literal is a `double`** (`Expression.cpp:89`, `atof`), and every
> operation is `double`. **Measured: `5/2` evaluates to `2.5`, `1/3` to
> `0.3333333432674408`.** If step 3 introduces an `int` type and lets integer
> literals take it, `5/2` becomes `2` and **every saved patch containing an
> integer division silently changes value with no error**.
>
> **Decision: numeric literals stay `float` in the frame domain, full stop.**
> `int` exists in `field-language` §9's type list for loop counters, indices and
> mode selectors — surfaces that arrive in **steps 4 and 5** (`i`, `count`, a
> `for` bound). Reserve the `int` *kind* in `FieldTypes.h` so the lattice is
> complete, and make it **unreachable from step 3's surface syntax**. Write that
> sentence into `FieldTypes.h` as a comment; the next session will otherwise
> "finish" the type system and break the corpus.

### 5.2 The promotion / broadcast table — which pairs are legal

For every binary arithmetic operator (`+ - * / % ^`):

| lhs \ rhs | `float` | `vec2` | `vec3` | `vec4` |
|---|---|---|---|---|
| **`float`** | `float` | `vec2` | `vec3` | `vec4` |
| **`vec2`** | `vec2` | `vec2` | **ERROR** | **ERROR** |
| **`vec3`** | `vec3` | **ERROR** | `vec3` | **ERROR** |
| **`vec4`** | `vec4` | **ERROR** | **ERROR** | `vec4` |

Every **ERROR** cell is a compile error carrying **both operands' spans** and
**both arities**, in the shape `field-compiler` §7 requires:

```
   P += vec2(1, 0)
   ^^   ^^^^^^^^^
   |    vec2
   vec3

   error: cannot combine vec3 and vec2
   hint:  broadcast goes scalar to vector only; there is no rank-narrowing rule.
          write vec3(1, 0, 0), or take a component: P.xy += vec2(1, 0)
```

| Wrong | Right | What the error must not do |
|---|---|---|
| `P += vec2(1, 0)` | `P += vec3(1, 0, 0)` | **never** zero-fill to `vec3` |
| `Cd = P` where `Cd` is `vec3`, `P` is `vec4` | `Cd = P.xyz` | **never** truncate |
| `vec2(1,2) * vec4(1,2,3,4)` | pick one arity | **never** pick the wider |
| `P *= 2.0` | *(already right)* | broadcast — this one is legal and silent |
| `Cd = 0.5` | *(already right)* | broadcast |

### 5.3 Constructors

Ordinary `Call` nodes whose callee is `vec2` / `vec3` / `vec4`
(`field-compiler` §4 — no new AST node).

| Form | Rule | Example |
|---|---|---|
| **splat** — one scalar argument | fills every lane | `vec3(1)` → `(1,1,1)` |
| **full** — exactly `N` scalar arguments | one per lane | `vec3(x, y, z)` |
| **mixed** — arguments whose **lanes sum to exactly `N`** | concatenated left to right | `vec3(v2, z)`, `vec4(v2, v2)`, `vec4(v3, w)`, `vec4(x, v2, w)` |

| Refuse | Message must say |
|---|---|
| lane sum ≠ N and not the 1-argument splat | the lane sum it computed and the arity it needed |
| zero arguments | `vec3() needs 1 or 3 components` — **not** a silent zero vector |
| a lane-sum overshoot (`vec3(v2, v2)` = 4) | the overshoot; **never** truncate to fit |

> **The splat is a special case of nothing.** `vec3(1)` is not "lane sum 1
> padded"; it is a distinct rule. Implement it as its own branch, because
> `vec3(v2)` (lane sum 2) must be an **error**, not a partial splat.

### 5.4 Swizzles — **in scope**, and the maximal-munch trap

**Decision: swizzles are IN scope for step 3.** `field-testing` §3 section D —
the section this step's exit criterion names — asserts "`.xz` and `.bgr`
swizzles resolve". They cannot be deferred: step 4's reserved names are `P`
(`vec3`), `uv` (`vec2`) and `Cd` (`vec3`), and every element-domain example in
every skill is written as `P.y += …`. A step 3 without swizzles ships a vector
type nothing can read a component of.

**Grammar** (read swizzles; see the lvalue note below):

```
   Postfix  := Primary ( '.' SWIZZLE )*
   SWIZZLE  := [xyzw]{1,4}  |  [rgba]{1,4}
```

| Rule | |
|---|---|
| Result arity | the swizzle's length. Length 1 → `float`; length `k` → `vec`k |
| Two sets, never mixed | `.xy` and `.rg` are both legal; **`.xg` is an error** naming both sets |
| Bound by the base's arity | `.w` / `.a` on a `vec3` is an error naming the base's arity, **not** a zero |
| Base must be a vector | `.x` on a `float` is an error saying so — §8.5 |
| Repeats legal on **read** | `P.xxx` is a legal `vec3` |
| Chaining legal | `P.xy.y` — apply left to right |
| Lvalue swizzles | **deferred to step 4**, where assignment exists in the surface. The type checker's duplicate-component rule (`P.xx = …` is an error) goes in **now**, unreachable, with a comment saying why; retrofitting it after step 4 ships means an already-saved kernel becomes an error |

**The maximal-munch trap.** `ParseNumber` (`Expression.cpp:77-90`) today consumes
**any run of digits and dots** and hands it to `atof`. Measured against the
current binary:

| Input | Today | Why |
|---|---|---|
| `.5` | `0.5`, ok | `ParseAtom` dispatches to `ParseNumber` on `.` |
| `1.5` | `1.5`, ok | |
| `1.` | `1`, ok | |
| **`1.2.3`** | **`1.2000000476837158`, ok=1** | all dots consumed, `atof` stops at the second |
| `v.x` (with sibling `v = 9`) | **error**, `unexpected trailing text` | there is no `.` operator today |
| `sin(t).x` | error, `unexpected trailing text` | same |

The lexer rule that resolves it:

```
   inside a NUMBER, consume AT MOST ONE '.'    ── a second '.' terminates the number
   a leading '.' starts a NUMBER only when the next character is a digit
      AND the previously emitted token cannot end an operand
   otherwise '.' is the ACCESS operator
```

"Can end an operand" = `NUMBER`, `IDENT`, `)`, or a swizzle. Worked:

| Input | Tokens | Result |
|---|---|---|
| `.5` | `NUMBER(0.5)` | `0.5` — **unchanged** |
| `1.` | `NUMBER(1.0)` | `1` — **unchanged** |
| `2*.5` | `NUMBER(2) OP(*) NUMBER(0.5)` | `1` — **unchanged** |
| `P.xy` | `IDENT(P) OP(.) IDENT(xy)` | swizzle |
| `sin(t).x` | `… ) OP(.) IDENT(x)` | swizzle on a `float` → error §8.5 |
| **`1.2.3`** | `NUMBER(1.2) OP(.) NUMBER(3)` | **error: expected a component name after `.`** |

> **`1.2.3` is a behaviour change, and it is the *second* time this exact input
> has come up.** Step-01 §7.1 already flagged it as an owner question (the
> `field-compiler` §2 number grammar is wider than the code, and `1e3` is the
> other half of that question). **Do not answer it unilaterally here.**
> - If step 1's owner answer was "implement §2's grammar", `1.2.3` is already a
>   lex error and this step inherits it with no new break.
> - If step 1's owner answer was "keep the current behaviour", then step 3
>   **cannot** — a `.` access operator and a number that eats arbitrary dots are
>   mutually exclusive. Say so, put the case in the corpus as a deliberate,
>   commented change, and get the owner's answer **before landing**.
>
> **Bug prevented:** a saved patch containing the typo `=1.2.3` (which
> currently evaluates to `1.2` with `ok=1`) starts erroring after a step billed
> as "purely additive".

### 5.5 The `^` operator — component-wise, and a recorded discrepancy

> **Discrepancy: the design brief's operator list omits `^` entirely.** The brief
> (§4) lists `+ - * / %`, `+= -= *= /=`, `= == != < <= > >=`, `&& || !`, `.`,
> `()`, `{}`, `#`. **`^` is not in it.** But `src/core/Expression.cpp:292-303`
> implements `^` as a right-associative power operator **today**, and
> `src/core/Expression.h:30` documents it: *"Supports + - * / % ^ (with unary
> minus and right-associative ^)"*. `field-language` §10 already carries this as
> an OPEN question with three options. **The code wins: `^` exists.**

Step 3's obligations for `^`:

| Property | Value | Measured / source |
|---|---|---|
| Exists at all | **yes** — dropping it breaks every saved `=2^x` | `Expression.h:30`, `Expression.cpp:292` |
| Associativity | **right** — `2^3^2` = `512` | `Expression.cpp:298-301` |
| vs unary minus | **unary minus binds tighter** — `-2^2` = **`4`**, not `-4` | measured; `ParsePower` calls `ParseAtom` first |
| vs binary minus | `^` binds tighter — `0-2^2` = `-4` | measured |
| Negative base, fractional exponent | `pow(-1, 0.5)` → **`NaN`, with `ok=1`** | measured: `-1^0.5` returns true and a NaN |
| **On vectors** | **component-wise**, using the §5.2 table for its operand pair. `vec3 ^ float` broadcasts the exponent; `vec3 ^ vec3` pairs lane by lane; `vec3 ^ vec2` is an error | this step decides it |
| IR lowering | **lower `^` to the `pow` intrinsic in the IR** — `field-language` §10 option (b), the only option that costs nothing. Never emit a literal `^` in generated GLSL, where it is **XOR** | `field-compiler` §6.2 |

**Close `field-language` §10's OPEN question in this step, in writing**, and fix
the design brief's operator list in the same commit — leaving it open past step 3
means the vector semantics of `^` are undefined at the exact moment step 4 starts
writing `P^2`.

### 5.6 Which arguments broadcast — the intrinsic rank table

`T` denotes `float | vec2 | vec3 | vec4`. "broadcasts" means a `float` in that
position is splatted to the result arity. Two vector arguments in the same
call must have **equal** arity (§5.2's ERROR cells apply unchanged).

| Intrinsic | Signature | Which arguments may be a scalar and broadcast |
|---|---|---|
| `sin cos tan abs floor ceil round sign sqrt exp log` | `T -> T` | n/a — one argument, applied **component-wise** |
| `min(a, b)` `max(a, b)` | `(T, T) -> T` | **either** argument |
| `mod(a, b)` `pow(a, b)` | `(T, T) -> T` | **either** argument |
| `clamp(x, lo, hi)` | `(T, T, T) -> T` | `lo` and `hi` each independently; `x` sets the arity |
| `lerp(a, b, w)` | `(T, T, T) -> T` | `w` broadcasts; `a` and `b` must share an arity (either may itself be the scalar that broadcasts to the other) |
| `mix(a, b, w)` | `(T, T, T) -> T` | **does not exist today** — see the decision box below |
| `step(edge, x)` | `(T, T) -> T` | `edge` broadcasts against `x`; edges-first, matching GLSL (`Expression.cpp:155-157`) |
| `smoothstep(e0, e1, x)` | `(T, T, T) -> T` | `e0` and `e1` each broadcast; `x` sets the arity |
| `if(c, a, b)` | `(float, T, T) -> T` | **`c` is scalar-only in v1**; `a` and `b` follow `lerp`'s rule. Both branches are still eagerly evaluated (`Expression.h:41-45`) |
| `rand noise sh` | `... -> float` | **scalar-only.** Step 2 owns them; a vec-valued `rand` is not in the design |
| `vec2 vec3 vec4` | see §5.3 | the splat form is the only broadcast |
| `<` `<=` `>` `>=` `==` `!=` | `(float, float) -> float` | **scalar-only — refuse vectors.** See the decision box |
| `&&` `\|\|` `!` | `(float, …) -> float` | **scalar-only — refuse vectors** |
| unary `-`, unary `+` | `T -> T` | component-wise |

> **Decision 1 — vector comparisons are REFUSED in v1.** GLSL returns a `bvecN`
> from `vec == vec`. Field has **no `bvecN`** (`field-language` §9's list is
> `float int bool vec2 vec3 vec4`), and today's comparisons return exactly `1.0`
> or `0.0` and feed straight into arithmetic (`Expression.h:37-40`,
> `"lerp(lo, hi, x > 0.5)"`). Returning a `vec3` of 0/1 would work, and returning
> a single `float` would silently reduce. **Refuse, with an error naming the
> arity and suggesting a component (`P.x > 0.5`).** A refusal is recoverable; a
> silent all-or-any reduction is not. Record the decision in `field-language` §9.

> **Decision 2 — `mix` does not exist today.** Verified: `mix(0, 1, 0.5)` →
> `unknown function 'mix'`. Only `lerp` exists (`Expression.cpp:130`, and it is
> `a[0] + (a[1] - a[0]) * a[2]`, **not** clamped). Two options: **(a)** add `mix`
> as a strict alias for `lerp` — matches the `step`/`smoothstep` edges-first
> precedent (`Expression.cpp:152-154` explicitly aligns with GLSL "so the two
> languages in this app don't disagree"), costs one table row, and every skill's
> pixel example already writes `mix(...)`; **(b)** leave it out and let those
> examples keep failing. **(a) is recommended.** It is purely additive — no
> existing text can change meaning, because `mix(...)` is an error today. Add a
> corpus record for it. Put it to the owner rather than assuming.

### 5.7 Bytecode and VM impact — two options, costed

Step 1 built a **register machine** (not a stack machine) with **`double`
accumulators** for the frame domain (`field-compiler` §3, §6.1). Two ways to make
a register hold a vector:

| | **(A) widen every register to 4 lanes** | **(B) a separate vector register file** |
|---|---|---|
| Shape | `struct Reg { double v[4]; }`; scalars use lane 0 | `double s[]` + `struct V { double v[4]; } vr[]`, distinct opcodes |
| Register-file size | **32 B/register** (vs 8 B). 256 registers → **8 KB** vs 2 KB. Both fit in L1 | 2 KB + whatever the vector file needs; smaller for the scalar-only programs that are the *entire* existing corpus |
| Opcode count | **unchanged.** Each instruction gains a `lanes` field (1–4) and the VM loop becomes `for (l = 0; l < inst.lanes; l++)` | **roughly doubles** — every op needs a scalar and a vector form, plus move-between-files instructions |
| Emitter complexity | one path. `lanes` comes straight off the IR node's type — a single source of truth (invariant 1.4.3) | two paths, plus register-allocation across two files, plus a spill story between them |
| Scalar throughput | identical: `lanes == 1` runs one iteration | identical |
| Retargetability (`field-compiler` §6.4) | a `lanes` field is a **semantic** property of the node; GLSL, C++ and WASM all read it directly | two opcode families are a **backend-shaped** distinction leaking into the IR-adjacent layer |
| Risk | wasted bytes on scalar-only programs | the two files disagree about where a value lives — the classic register-allocator bug |

> **Recommendation: (A).** The programs this VM runs are inline parameter
> expressions — tens of instructions, not thousands. Register pressure is not the
> constraint; emitter and error-message clarity are. 6 KB of extra register file
> against halving the opcode count and keeping one emitter path is not a close
> call. Bound the register count explicitly so the file is a compile-time-sized
> array (`field-realtime` §1 rule 5) and **allocate it once, not per evaluation**
> (§8.14).

Whichever is chosen: the **swizzle is a shuffle instruction**, not four moves —
one opcode carrying a 4×2-bit lane-index byte plus the output arity. That keeps
`P.xxx` and `P.zyx` the same cost as `P.x`, and keeps the GLSL lowering a literal
`.xxx` rather than three component reads.

### 5.8 API preservation — what happens when a top-level expression is a `vec`

`Expression::Evaluate` (`src/core/Expression.h:44-47`) returns a `bool` and
writes a single `float&`. That does not change. So a top-level vector needs an
answer:

| Option | Verdict |
|---|---|
| **(a) compile error**: `expression has type vec3; a parameter expression must produce a single number` + hint *"take a component, e.g. `P.x`"* | **CHOOSE THIS** |
| (b) silently take `.x` | **No.** It is exactly the truncation §5.2 forbids between `vec3` and `vec2`, applied to the API boundary. A user writes `=vec3(1,0,0)` on a colour knob and gets `1` with no diagnostic |
| (c) `length()` it | **No.** Invents a semantic nobody asked for |
| (d) sum / mean the lanes | **No.** Same objection, worse |

Consequences of (a) that must all hold:

| Requirement | Why |
|---|---|
| It is a **compile** error, not a runtime one | so step 1's compile cache **caches the failure** and does not re-lex, re-parse and re-lower it 60 times a second (step-01 §7.12; `FormulaNode.cpp:415` is the reference) |
| `outValue` is **left untouched** on the failure path | `Expression.cpp:420-424`; both call sites rely on it to keep the last good value (`main.cpp:37515-37524`, `ExprGlobals.cpp:74-80`) |
| `outError` is **not** cleared on success | `Expression.cpp:425-426`. Do not "tidy" this — step-01 §7.9 |
| All three call sites keep working **byte-identically** | no existing expression can produce a vec (no vector syntax existed), so **no `deterministic` corpus record can change**. That is the whole safety argument for this step; §9 checks it mechanically |
| The error string is **new**, so it needs new corpus records | `expectErrSubstr` cases for the top-level-vec refusal, the arity mismatch, the mixed swizzle set, and the out-of-range component |

---

## 6. Step-by-step procedure

### Phase 0 — establish the baseline (before any edit)

1. Confirm steps 1 and 2 are merged (the table at the top of this file).
2. Run `INFINITE_FIELDTEST=1 build/Infinite.app/Contents/MacOS/Infinite` on the
   **unmodified** tree and record the output. Every later run is compared to it.
3. Reproduce the measured rows in §5.4 and §5.5 with your own probe, at `%.17g`.
   If one differs, stop — the numbers in this prompt are not describing your
   build.
4. Record the current corpus file's SHA. §9 diffs against it.
5. Confirm the state of step-01 §7.1's number-grammar question (`1e3`, `1.2.3`).
   §5.4 depends on the answer.

### Phase 1 — get the four decisions answered

| # | Decision | Where |
|---|---|---|
| 1 | `^` — keep in the surface, lower to `pow` in the IR (recommended), and it is **component-wise** on vectors | §5.5, closes `field-language` §10's OPEN |
| 2 | `mix` — add as a `lerp` alias (recommended) or leave out | §5.6 Decision 2 |
| 3 | vector comparisons — refuse (recommended) or add `bvecN` | §5.6 Decision 1 |
| 4 | `1.2.3` — inherited from step 1's answer, or a new deliberate break | §5.4 |

Put all four to the owner **with the measurements attached**. Write the answers
into this file as the recorded decision, in the same commit as the code.

### Phase 2 — types before syntax

Build `FieldTypes.h` first: the lattice, the §5.2 table, `JoinRank`, and the
§5.6 intrinsic rank table — **as data, in one place**. Write the unit-level
fixture cases for `JoinRank` before there is any parser that can reach it. A
rank rule that only exists inside the type checker's `switch` cannot be read by
the error formatter, and then the error messages drift from the rules.

### Phase 3 — lexer

The one-dot-per-number rule and the `.` operator (§5.4). This is the highest-risk
edit in the step because it touches the path every existing expression takes.
Add fixture section **B** cases for every row of §5.4's worked table, including
the three "unchanged" ones — those are the regression net.

### Phase 4 — parser and IR

`Access` postfix (the AST node `field-compiler` §4 already lists). Constructors
as `Call`. Rank inference bottom-up, using `FieldTypes.h`'s table and nothing
else. Constant folding **per lane, in `double`** (§8.2).

### Phase 5 — bytecode and VM

§5.7 option (A) unless the owner says otherwise. `lanes` on the instruction, the
shuffle opcode, the constructor opcode. Then run `field-realtime` §1 rules 1–3
against your own VM loop: no allocation in the execute path, no unbounded loop,
no recursion.

### Phase 6 — the adapter

§5.8's top-level-vec refusal, as a **compile** error, with `outValue` untouched.
`Expression::Evaluate`'s signature does not move a millimetre.

### Phase 7 — corpus and fixtures

**Additive only.** New records for the new surface; **zero existing records
change**. Fixture section **D**, per `field-testing` §3:

| Case | Assert |
|---|---|
| scalar → vector broadcast | `vec3(1,2,3) * 2` = `(2,4,6)`; `vec3(1) + 0.5` = `(1.5,1.5,1.5)` |
| `vec2` + `vec3` | **refused**, message names **both** arities and **both** spans |
| `vec4` + `vec3`, `vec2` * `vec4` | refused, same shape |
| swizzle read | `.xz` and `.bgr` resolve to the right lanes and the right arity |
| swizzle repeat | `P.xxx` is a legal `vec3` |
| swizzle chain | `P.xy.y` resolves |
| mixed swizzle set | `.xg` refused, naming both sets |
| out-of-range component | `.w` on a `vec3` refused, naming the base's arity |
| swizzle on a scalar | `t.x` refused, saying `t` is a `float` |
| constructors | `vec3(1)`, `vec3(1,2,3)`, `vec3(v2, 3)`, `vec4(v2, v2)`, `vec4(v3, 1)` all correct lane by lane |
| bad constructor | `vec3(v2)` and `vec3(v2, v2)` refused, naming the lane sum and the arity needed |
| top-level vec | `Evaluate("vec3(1,0,0)", …)` returns **false**, `outValue` **unchanged from its prior contents**, `outError` names the type |
| `^` on vectors | `vec3(1,2,3)^2` = `(1,4,9)`; `vec3 ^ vec2` refused |
| `^` scalar regressions | `-2^2` = `4`, `2^3^2` = `512`, `0-2^2` = `-4`, `-1^0.5` returns **`ok=1` with a NaN** — all four still exactly as before |
| rank table coverage | one case per row of §5.6, including a `clamp` with a vector `x` and scalar `lo`/`hi`, and a `lerp` with a scalar weight |
| vector comparison | `vec3(1) > vec3(0)` **refused** (Decision 1) |
| number lexing | every row of §5.4's worked table |
| precision | `vec3(0.1) + vec3(0.2)` matches `0.1+0.2`'s golden `0.30000001192092896` **in every lane** |
| **the whole `deterministic` corpus** | **byte-identical golden values.** This is the step's headline claim |

**A test that cannot fail is not a test.** For each row: introduce the bug
deliberately, watch `FAIL`, then fix it. In particular: make `vec2 + vec3`
zero-fill, watch the refusal case fail, then put the refusal back.

### Phase 8 — verify, then the gates

Build → Field fixture → hygiene → `field-realtime` §1 diff checklist → the
Desktop copy this project requires. §9 is the command list.

---

## 7. Traps — each names the bug it prevents

### 7.1 An `int` type for literals silently changes `5/2`

Measured today: `5/2` → **`2.5`**, `1/3` → **`0.3333333432674408`**. Every
literal is a `double` (`Expression.cpp:89`).
> **Bug prevented:** a session "completes" the type system from
> `field-language` §9's list, integer literals become `int`, and every saved
> patch containing an integer division changes value with no error and no
> visible cause. §5.1's decision — literals are `float`, `int` is reserved and
> unreachable — must be a **comment in the code**, not just in this file.

### 7.2 A `float` IR fails every golden value by a hair

`field-compiler` §3 and `field-testing` §2.3: everything is `double`, narrowed
once at `Expression.cpp:425`. Witness: `0.1+0.2` → `0.30000001192092896`, which
is `(float)((double)0.1 + (double)0.2)`.
> **Bug prevented:** the obvious implementation of a vector — `struct { float
> x, y, z, w; }`, because that is what GLSL and every graphics library use —
> makes **every** golden value fail in the last bits, including the scalar ones,
> because scalars now live in lane 0 of a float vector.
> **Vectors are stored as `double` lanes** in the frame-domain VM, narrowed once
> at the API boundary. `field-compiler` §3 permits `float` for the **element**
> backend and requires `float` for **pixel** — those are documented, tested
> divergences arriving in steps 4 and 7, and they do not license a `float` IR
> today. The IR carries `double` semantics; only a backend narrows.

### 7.3 `vec2 + vec3` silently truncating or zero-filling

Every graphics library the reader has used does *something* here. GLSL errors;
some do not.
> **Bug prevented:** `P += vec2(1, 0)` on a `vec3` position quietly leaves `z`
> alone — a wrong render with no message, in the exact place the user is least
> able to inspect intermediate values. `field-language` §14 row 9 is this row.

### 7.4 `.x` versus a leading-dot float literal

`.5` is a valid number **today** (measured, ok=1, `0.5`). `1.2.3` is `1.2`
**today** (measured, ok=1). A naive `.`-as-operator lexer breaks the first and a
naive number lexer breaks the second.
> **Bug prevented:** `=2*.5` — a spelling people actually type — starting to
> lex as `2 * (something) . 5` and erroring, after a step billed as additive.
> §5.4's rule handles both; its worked table is the fixture.

### 7.5 Swizzling something that is not a vector — and `AnalyzeNodes` makes this concrete

`src/nodes/AnalyzeNodes.cpp:150-179` binds a 22-name variable set including the
**single letters `r g b a l s h u v`** (plus `red green blue alpha lum bright sat
hue delta motion`). Every one of those is a scalar `float`.
> **Bug prevented:** a swizzle resolver that sees `v.x` and treats `v` as a
> vector because the component name is valid. `v` is a **bound scalar** in that
> call site. The error must read *"`v` is a float; `.x` needs a vec2, vec3 or
> vec4"*, and it must name the **binding** the identifier resolved to, because in
> a different call site `v` does not exist at all.

### 7.6 `min` and `max` are bound **variables** in one caller and functions in the others

`AnalyzeNodes.cpp:172-173` binds `vars["max"]` and `vars["min"]` as values.
Measured in step 1: with sibling `max = 7`, `max` evaluates to `7` **and**
`max(1,2)` evaluates to `2` in the same program — function-ness is decided by the
following `(` (`Expression.cpp:262-265`).
> **Bug prevented:** §5.6's rank table is written as though `min`/`max` are
> always intrinsics. A rank checker that resolves `max` to the intrinsic before
> consulting the binding set gives AnalyzeNodes a different answer than it gets
> today. This is step-01 §7.2's cache trap wearing a rank-polymorphism hat: keep
> identifier resolution keyed on the binding set, and let `(` decide.

### 7.7 A mixed swizzle set, or an out-of-range component, resolving to something

`.xg` and `.w`-on-a-`vec3` are the two.
> **Bug prevented:** `.w` on a `vec3` silently reading lane 3 of a widened
> 4-lane register (§5.7 option A) — **whatever garbage lane 3 holds**. Option
> (A) makes this trap *easier* to fall into, which is precisely why the arity
> bound is a type-checker rule, not a VM bound-check.

### 7.8 A `Swizzle` or `VecCtor` node entering the AST or the IR

`field-compiler` §4's node set is the **whole** set: `Access` already covers
swizzles, `Call` already covers constructors. §6.4: no backend-specific node
types.
> **Bug prevented:** the C++ and WASM backends have to special-case two more
> node kinds, and invariant 1.4.2 — the IR is the durable asset — is gone.

### 7.9 Unary minus still binds tighter than `^`, on vectors too

Measured: `-2^2` = **`4`**, not `-4` (step-01 §7.3). Component-wise, `-P^2` must
therefore be `(-P)^2` lane by lane, i.e. `(P.x*P.x, P.y*P.y, P.z*P.z)`.
> **Bug prevented:** "fixing" precedence to the conventional reading while
> adding vector support, silently changing the value of every saved `=-x^2`.

### 7.10 `pow` of a negative base with a fractional exponent returns NaN with `ok=1`

Measured: `-1^0.5` → `ok=1`, value **`NaN`**. That is `pow(-1, 0.5)`.
> **Bug prevented:** a component-wise `^` that "improves" this into a compile or
> runtime error changes the result of a scalar program from *a NaN that flows
> downstream* to *a failure that keeps the last good value* — a corpus break in
> the `deterministic` set (§8.1). Propagate the NaN per lane, unchanged.

### 7.11 A `vec` escaping through `ExprGlobals`

`src/core/ExprGlobals.h:33` — `Global::value` is a **`float`**; `Values()` returns
`const std::map<std::string, float>&`. A global's value is one number, and it is
also the *sibling binding* every parameter expression reads.
> **Bug prevented:** a global whose expression is `vec3(...)` compiling and then
> being truncated on the way into the map. §5.8's compile-time refusal covers
> this automatically **because `ExprGlobals` goes through the same `Evaluate`** —
> confirm that with a fixture case rather than assuming it, and confirm the
> global keeps its last good value and records the error
> (`ExprGlobals.cpp:74-80`).

### 7.12 A `vec` escaping through `ParamRef`

`src/core/Modulation.h:31` — `float* value`. A modulation destination is one
float, registered fresh every frame.
> **Bug prevented:** the same truncation on the knob side. Same fix, same
> reason: refuse at compile time, in one place.

### 7.13 Introducing `bvec2/3/4`

`field-language` §9's type list is `float int bool vec2 vec3 vec4`. There is no
vector of bool, and today comparisons return exactly `1.0` or `0.0` and compose
with arithmetic (`Expression.h:37-43`).
> **Bug prevented:** four new types, four new rank rules, a new set of GLSL
> lowering questions, and an `any()`/`all()` API — all to support an operation
> §5.6 Decision 1 refuses. Refusing is one table row.

### 7.14 Allocating the widened register file per evaluation

`field-realtime` §1 rule 1 and §3: no heap allocation in the execute path. Option
(A) quadruples the register file, which makes a `std::vector<Reg>` look more
tempting than it did with scalars.
> **Bug prevented:** an allocation on the per-frame parameter-apply path, once
> per bound knob per frame. Fixed-size array, bounded register count, allocated
> once — and the bound is a compile error when exceeded, naming it
> (`field-realtime` §1 rule 5).

### 7.15 Constructor names shadowing a binding

Function-ness is decided by the following `(` (`Expression.cpp:262-265`). A
sibling literally named `vec3` still resolves as a variable when written bare,
and as the constructor when written `vec3(...)`.
> **Bug prevented:** reserving `vec2`/`vec3`/`vec4` as keywords in the *lexer*
> (`field-compiler` §2 lists them under `KEYWORD`) and thereby making a node
> parameter named `vec3` an error. Recognise keywords **after** lexing, as §2
> itself says, and keep the `(`-decides rule.

### 7.16 Folding a vector constant in `float`, or lane by lane inconsistently

`field-compiler` §3: constant folding in `double`.
> **Bug prevented:** `vec3(0.1) + vec3(0.2)` producing a value in lane 0 that
> differs from the scalar `0.1+0.2` golden `0.30000001192092896`. The fixture
> case in Phase 7 exists for exactly this.

### 7.17 The in-app language reference becomes wrong

`src/main.cpp:51664-51673` lists operators, functions and bound names. It does
not know about `vec3`, swizzles, or (after Decision 2) `mix`.
> **Bug prevented:** a user reads the built-in reference, concludes vectors are
> not supported, and never finds them. Update it in the same commit; it is four
> `ImGui::TextDisabled` lines.

### 7.18 Widening the rank rules "while you are in there"

Tempting additions that are **not** in this step: `length`, `dot`, `cross`,
`normalize`, `distance`, `fract`, `atan2`, matrices, arrays.
> **Bug prevented:** each one is a new intrinsic with its own rank rule, its own
> GLSL lowering, its own corpus records — and none is needed until step 4 has a
> `P` to take the length of. If step 4 needs one, step 4 adds it. §10 lists them
> as out of scope on purpose.

---

## 8. The headline invariant, stated separately because it is the whole step

### 8.1 No existing value may change

Step 3's justification is that vectors are **additive**: no expression that
parses today can contain a vector, so no expression that parses today can change
meaning. Everything in §7 is a way that claim fails. The mechanical proof:

- every record in the `deterministic` corpus set passes at **`double`
  exactness**, byte-identically to its pre-step-3 golden value;
- every record in the `random` set passes against **step 2's** re-baseline,
  unchanged — step 3 does not touch randomness;
- the corpus data file's diff contains **only additions**.

§9 checks all three. **If a single `deterministic` value moved, do not
re-baseline it.** Find out why. That is a bug in this change, not an expected
break — the one expected break in this whole project was step 2's, and it is
already spent.

---

## 9. Machine-checkable exit criterion

Every command must pass. Run them in this order. Paste the **actual output**
into the commit message; do not paraphrase.

```bash
cd /Users/namansoni/infinte

# 1. The three Expression::Evaluate call sites are untouched.
grep -rn "Expression::Evaluate" src/
#    ^ exactly 3 hits: src/main.cpp, src/core/ExprGlobals.cpp, src/nodes/AnalyzeNodes.cpp

# 2. The public signature is byte-identical to main's.
git diff main -- src/core/Expression.h | grep -E '^[+-]\s*(bool Evaluate|const std::map|float& outValue|double t)'
#    ^ must print NOTHING.

# 3. No existing node was edited — step 3 is additive (invariant 1.4.9).
git diff --name-only main -- src/nodes/ src/audio/ src/platform/
#    ^ must print NOTHING.

# 4. THE HEADLINE CHECK: the corpus gained records and changed none.
#    Replace <CORPUS_PATH> with step 1's corpus data file.
git diff main -- <CORPUS_PATH> | grep '^-[^-]'
#    ^ must print NOTHING. One removed or altered line is a failure of §8.1.
git diff main -- <CORPUS_PATH> | grep -c '^+[^+]'
#    ^ must be > 0 (the new vector records).

# 5. No float-precision IR crept in.
grep -rnE '\bfloat\b' src/core/field/FieldTypes.h src/core/field/FieldSwizzle.* \
  | grep -v '// ' | grep -v 'kFloat\|Float,\|"float"'
#    ^ read every hit. A `float` storing a value (rather than naming the TYPE
#      `float`) is trap 7.2. There is no grep that decides this — state in the
#      commit message that you read them.

# 6. No new AST or IR node kind (field-compiler §4, §6.4).
git diff main -- src/core/field/ | grep -nE '^\+.*(Swizzle|VecCtor|GlslVec|VectorNode)\s*[,{=]'
#    ^ must print NOTHING. Swizzles are `Access`; constructors are `Call`.

# 7. It builds clean.
cmake --build build -j"$(sysctl -n hw.ncpu)" 2>&1 | tail -20

# 8. The Field harness, including section D.
INFINITE_FIELDTEST=1 build/Infinite.app/Contents/MacOS/Infinite 2>&1 | tee /tmp/field-step3.log
grep -cE 'FAIL|BUG$|MISMATCH|SUSPECT' /tmp/field-step3.log
#    ^ must print 0.
grep -E '^D\.' /tmp/field-step3.log
#    ^ section D must be present and end OK.

# 9. The scalar regressions this step is most likely to break, spot-checked
#    through the real binary. Every one of these is already a corpus record;
#    this is the human-readable version for the commit message.
#       -2^2      -> 4                        (unary minus binds tighter than ^)
#       2^3^2     -> 512                      (right-associative)
#       0-2^2     -> -4
#       -1^0.5    -> ok=1, NaN                (trap 7.10)
#       5/2       -> 2.5                      (trap 7.1)
#       .5        -> 0.5                      (trap 7.4)
#       1.        -> 1                        (trap 7.4)
#       2*.5      -> 1                        (trap 7.4)
#       0.1+0.2   -> 0.30000001192092896      (trap 7.2)
#    Print them from the fixture and paste the output.

# 10. The fixture can actually fail (invariant 1.4.7).
#     Make vec2+vec3 zero-fill instead of erroring, re-run step 8, confirm the
#     refusal case FAILs, revert. Then perturb one lane of one new golden
#     value, confirm FAIL, revert. Record both in the commit message.

# 11. No sigils, no declared rates, anywhere in the new surface.
grep -rn '@[A-Za-z]' src/core/field/ docs/plans/field/step-03-vectors-and-rank.md \
  | grep -v '@brief\|@param\|email' || echo "no sigils OK"
grep -rn 'krate\|@rate' src/core/field/ && echo "RATE DECLARED - FAIL" || echo "no rate syntax OK"

# 12. Real-time diff checklist — field-realtime §1 rules 1-3 against the VM.
git diff main -- src/core/field/ | grep -nE 'new |malloc|push_back|resize|reserve|std::function|while *\('
#    ^ every hit must be justified in the commit message, or removed.
#      The widened register file must be a fixed-size array (trap 7.14).

# 13. The full gate.
.claude/skills/run-infinite-hygiene/driver.sh

# 14. Project convention.
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

Plus these, which need a human but are still binary pass/fail:

| # | Check |
|---|---|
| 15 | A saved patch containing `expr` and `glob` lines loads and renders **identically** before and after — screenshot diff, not "looks the same" |
| 16 | `=vec3(1,0,0)` typed under a real knob shows the §5.8 error, leaves the knob's last good value in place, and does **not** recompile per frame (assert on a compile counter over 10 frames) |
| 17 | The four Phase 1 decisions have **owner answers**, written into this file, and `field-language` §10's OPEN `^` question is closed in the skill |
| 18 | The design brief's operator list (§4) has been corrected to include `^` — the discrepancy recorded in §5.5 |
| 19 | The in-app language reference (`src/main.cpp:51664-51673`) mentions the vector types and swizzles |

---

## 10. Explicitly out of scope for step 3

| Not in this step | Where it belongs |
|---|---|
| `length`, `dot`, `cross`, `normalize`, `distance`, `reflect`, `fract`, `atan2` | later, and only when a step actually needs one. Every added intrinsic is a rank rule, a GLSL lowering and a set of corpus records |
| Matrices of any kind | not in the v1 design at all (`field-language` §9: no user structs, no arrays) |
| `bvec2/3/4` and vector comparisons | refused — §5.6 Decision 1 |
| Lvalue swizzles (`P.xy = …`) as **reachable syntax** | **step 4**, where assignment enters the surface. The duplicate-component type rule goes in now, unreachable (§5.4) |
| The `element` domain, `P` / `N` / `uv` / `Cd` as **bound names** | **step 4**. Step 3 gives them their *types*; it does not bind them |
| `attrib`, `param`, `state` declarations | steps 5–6 |
| The `pixel` domain and the GLSL lowering of swizzles and constructors | step 7. Keep the semantics GLSL-compatible now (`field-compiler` §6.2); do not write the emitter |
| The `sample` and `graph` domains | steps 9, 10 |
| `reduce` / `map` / `resample` / `downsample`, including `reduce.mean` over a `vec3` | step 8 |
| A vec-valued `rand`/`noise`/`sh` | not in the design. Step 2 owns those and they return `float` |
| A Field **node** in the graph, an editor, a `ParamRef` registration | step 5 and `field-integration` |
| Making `ParamRef` or `ExprGlobals` vector-capable | never in this step; probably never at all (§7.11, §7.12) |
| Changing `Expression::Evaluate`'s signature or its three call sites | never |
| Changing the patch file format or `Patch::NodeRecord` | never in this step |
| Re-baselining any corpus value | §8.1. Step 2 spent the project's one deliberate break |
| Making the undo/patch format faster, or anything in `docs/plans/undo-delete-perf-prompt.md` | that separate brief |

If you find a genuine bug in existing code while in here, **report it rather than
fixing it inline.**

---

## 11. Which earlier steps must be finished first

| Step | Why this step needs it |
|---|---|
| **1** — `Expression.cpp` → lexer / AST / typed IR / bytecode | there is no type field to widen, no `Access` AST node, no register machine to add lanes to, no span to put in a rank-mismatch error, and — decisively — **no corpus**. Without step 1's frozen corpus, "no existing value changed" is an unverifiable claim, and it is the *only* claim this step makes |
| **2** — seeded `rand`/`noise`/`sh` | the `random` corpus set must already be re-baselined and stable. If step 3 runs first, a step-2 re-baseline later lands on top of step 3's additions and the two breaks become indistinguishable in the diff. §5.6 also relies on step 2 having settled that those three intrinsics return `float` and are not candidates for a vector form |

Steps 4–10 depend on **this** step, not the other way round. Step 4 in particular
is unbuildable without it: `P` is a `vec3` and every element-domain example in
every skill reads `P.y`.

---

## 12. Report back with

1. The four Phase 1 decisions and the owner's answers: `^` (surface + lowering +
   vector semantics), `mix`, vector comparisons, and `1.2.3`.
2. The output of §9 check 4 — proof that the corpus diff contains **only
   additions**.
3. The §9 check 9 table (the scalar regressions) printed from the real binary,
   before and after.
4. Which §5.7 option was implemented and the measured register-file size.
5. The before/after of the two deliberate breakages in §9 check 10.
6. Every place a skill or the design brief disagrees with the code — the **code
   wins**, and the skill gets fixed in the same commit. Two are already known:
   the brief's operator list omits `^` (§5.5), and `field-compiler` §2 lists
   `vec2`/`vec3`/`vec4` as `KEYWORD`s while `Expression.cpp:262-265` decides
   function-ness by the following `(` (§7.15).
