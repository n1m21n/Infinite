# Field build step 1 — restructure `Expression.cpp` into lexer → AST → typed IR → bytecode

**Repo:** `/Users/namansoni/infinte` (C++17, ImGui + OpenGL, MIT).
**Branch first** (`.claude/skills/git-branch-workflow`): `feature/field-step-01-expression-ir`.
This is the **only** Field build step that touches existing code. Treat it as a
refactor under a byte-identical public API, not as a language change.

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

Papers and public documentation about them are fine. The Kronos *paper*
(Norilo, "Kronos: A Declarative Metaprogramming Language for Digital Signal
Processing", Computer Music Journal 39:4, 2015) may be cited freely; its code
may not be read. **Safe to read:** Faust (LGPL), ChucK, Houdini VEX docs,
TidalCycles docs and papers (not its source).

If you catch yourself about to run `grep` inside `/Users/namansoni/BespokeSynth`,
stop.

### 1.2 The other invariants

| # | Invariant | Source |
|---|---|---|
| 1 | **`Expression::Evaluate` keeps its exact signature.** All three call sites stay unchanged. | `field-compiler` §0.3, §10 |
| 2 | **The typed IR is the durable asset, not the syntax.** A change that makes the IR harder to retarget to GLSL / C++ / WASM is a regression even if every test passes. | `field-compiler` §0.2 |
| 3 | **A failed compile never blanks the output.** The last working value keeps running. | `field-compiler` §0.4, §7 |
| 4 | **Field is one primitive:** a kernel is a body of code run once per element of a domain. Nothing in this step introduces a second one. | `field-language` §1 |
| 5 | **A test that cannot fail is not a test.** Every corpus case must be observed failing at least once — break it deliberately, watch `FAIL`, then fix it. | `field-testing` §0.2 |
| 6 | **The corpus is the contract.** Step 1's entire justification is that saved patches keep working; without a corpus that claim is unverifiable. | `field-testing` §0.3 |
| 7 | **No sigils.** Never write `@P`. Not relevant to this step's code, but relevant to any doc or comment you write. | `field-language` §4 |
| 8 | **Rate is never declared.** No `@rate`, no `krate`. Step 1 has exactly one domain (`frame`) and it is inferred, not written. | `field-language` §3 |

### 1.3 Do not

- Do not add language features. No `vec2/3/4` (that is **step 3**), no
  `attrib`/`param`/`state`, no statement-form `if`, no `element` domain.
- Do not change `rand`/`noise`/`sh` behaviour. That is **step 2** and it is a
  deliberate visible break that needs its own release note.
- Do not change the patch file format, `Patch::Write`/`Read`, or the
  `expr` / `glob` line grammar (`src/core/Patch.h:47-48`).
- Do not commit or push unless the owner asks.

---

## 2. Goal

`src/core/Expression.cpp` is today a single-pass recursive-descent evaluator: it
walks the source text and computes the value in the same traversal, and it does
this **again on every evaluation** — once per frame, per bound parameter, per
global. Replace that interior with a real four-stage pipeline — lexer producing
span-carrying tokens, parser producing an AST, a type-and-domain inference pass
producing a typed IR, and a register-bytecode emitter — plus a small VM that
executes the bytecode, and a compile cache so a given expression text is lexed,
parsed and lowered **once** rather than 60 times a second. The public surface
must not move a millimetre: `Expression::Evaluate` keeps its exact six-parameter
signature, returns the same `bool`, writes the same `float`, and produces error
strings that the existing corpus still recognises. The proof is a golden-value
regression corpus that you author **against the current binary, before you edit
anything**, and that passes bit-exactly afterwards.

```
 today                              after step 1
 ─────                              ────────────
 text ──▶ recursive descent ──▶ v   text ──▶ lex ──▶ parse ──▶ infer ──▶ IR ──▶ bytecode
          (every evaluation)                └──────── cached by (text, binding-set) ────┘
                                                                              │
                                            bindings (t, siblings, globals) ──▶ VM ──▶ v
```

---

## 3. Files to read before touching anything

| Path | Why |
|---|---|
| `.claude/skills/field-language/SKILL.md` | the language contract; §5 reserved words, §10 operators, §12 the randomness question you must **not** answer in this step |
| `.claude/skills/field-compiler/SKILL.md` | **the primary spec for this step.** §1 pipeline, §2 token grammar, §3 numeric precision, §4 AST node set, §5 domain inference, §6.1 bytecode VM, §6.4 retargetability, §7 error reporting, §8 refusals, §10 build order + the caching trap |
| `.claude/skills/field-testing/SKILL.md` | §1 how tests work here, §2 the corpus (**it does not exist — you author it**), §3 the harness, §6 step 1's exit row |
| `.claude/skills/field-realtime/SKILL.md` | §1 the diff checklist; run it against your own diff at the end |
| `.claude/skills/codebase-navigation/SKILL.md` | how to find *every* call site and registration point, not just the first grep hit |
| `.claude/skills/run-infinite-hygiene/SKILL.md` + `driver.sh` | the gate; `TESTS`/tier lists live in `driver.sh` around line 78 |
| `src/core/Expression.h` (48 lines) | **this header is the specification of what must not break.** Every documented behaviour is a corpus case |
| `src/core/Expression.cpp` (428 lines) | the entire thing. Read it line by line; §5 below depends on details that are only in the code |
| `src/core/ExprGlobals.h` / `.cpp` | call site 2, and the ordering/last-good-value semantics you must preserve |
| `src/main.cpp:37380-37525` | call site 1 in context, including the `lo`/`hi` sibling-map mutation that is the cache trap |
| `src/nodes/AnalyzeNodes.cpp:119-200` | call site 3, and the *only* place `min`/`max` are bound as **variables** rather than functions |
| `src/nodes/FormulaNode.cpp:390` and `:413-418` | the keep-last-working-program + do-not-retry-per-frame model |
| `src/core/Patch.h:40-60, 115-140` | the `expr` / `glob` line grammar you will harvest the corpus from |
| `CMakeLists.txt:205-230` | where `src/core/Expression.cpp` is listed; new .cpp files go here |
| `docs/CODE_STANDARDS.md` | house style |

---

## 4. Files to create / modify

### Create

| Path | Contents |
|---|---|
| `src/core/field/FieldLex.h` / `.cpp` | token type, `Token { kind, offset, line, col, length, double numberValue, string text }`, `Lex(const std::string&, std::vector<Token>&, FieldError&)` |
| `src/core/field/FieldAst.h` | the AST node set of `field-compiler` §4, exactly — no more nodes than that table |
| `src/core/field/FieldParse.h` / `.cpp` | tokens → `Program`; statement-boundary error recovery |
| `src/core/field/FieldIR.h` / `.cpp` | typed IR node (**type AND domain on the node**, never a side table — `field-compiler` §6.4), inference, constant folding in `double` |
| `src/core/field/FieldBytecode.h` / `.cpp` | register machine instruction set + emitter |
| `src/core/field/FieldVM.h` / `.cpp` | the executor; `double` accumulators for the frame domain |
| `src/core/field/FieldError.h` | `{ severity, span{line,col,length}, message, hint }` per `field-compiler` §7 |
| `src/core/field/FieldProgramCache.h` / `.cpp` | text + binding-set → compiled program, with the §6 trap handled |
| `tests/field/corpus.txt` (or `assets/tests/field-corpus.txt` — pick one and say why) | the frozen regression corpus, a **data file**, not literals in `main.cpp` (`field-testing` §2.1.4) |

### Modify

| Path | Change |
|---|---|
| `src/core/Expression.cpp` | body replaced by a thin adapter over the new pipeline. **`Evaluate`'s signature and semantics unchanged.** |
| `src/core/Expression.h` | comments only — update the "re-parsed on every evaluation" paragraph, which becomes false. **Do not touch the declaration.** |
| `CMakeLists.txt` | add the new `.cpp` files next to `src/core/Expression.cpp` (line ~218) |
| `src/main.cpp` | add **one** early-exit fixture `INFINITE_FIELDTEST` next to `INFINITE_DSPTEST` (`src/main.cpp:37619`) and its `RunFieldTest()` above. Nothing else in `main.cpp` changes. |
| `.claude/skills/run-infinite-hygiene/driver.sh` | register `"FIELDTEST:1"` in `TIER1_CHECKS` (line ~78) |

### Must not be modified

`src/main.cpp:37507`, `src/core/ExprGlobals.cpp:72`, `src/nodes/AnalyzeNodes.cpp:179`
— the three `Expression::Evaluate` call lines. If a diff touches one of them,
the step has failed its own premise.

---

## 5. The exact API and the exact behaviour that must not change

### 5.1 The signature — verbatim, from `src/core/Expression.h:44-47`

```cpp
bool Evaluate(const std::string& text, double t,
              const std::map<std::string, float>* siblings,
              const std::map<std::string, float>* globals,
              float& outValue, std::string& outError);
```

No overloads that shadow it, no default arguments added, no `noexcept`, no
change of namespace. Anything new goes in `namespace Field`, in the new files.

### 5.2 Every call site — this is the complete list

`grep -rn "Expression::Evaluate" src/` returns exactly three, and there are no
other consumers of the header (`grep -rn 'Expression\.h' src/`).

| # | Site | Bindings it passes | What breaks if you get it wrong |
|---|---|---|---|
| 1 | `src/main.cpp:37507` | `t` = `Transport::Instance().Seconds()`; `siblings` = `paramSnapshot[nodeIndex]` **temporarily mutated to add `lo`/`hi`** (`main.cpp:37505-37506`, restored at `:37508-37509`); `globals` = `ExprGlobals::Values()` | every `=` expression on every knob in every patch |
| 2 | `src/core/ExprGlobals.cpp:72` | `t`; `siblings` = `sValues`, **growing as the loop walks the global list** so a global sees only the globals above it; `globals` = `nullptr` | patch-wide globals, and the "cycles are structurally impossible" guarantee |
| 3 | `src/nodes/AnalyzeNodes.cpp:179` | `t`; `siblings` = a 22-entry map of `r g b a red green blue alpha lum l bright sat s hue h max min delta motion u v`; `globals` = `nullptr` | the Image Analyze node's custom-expression math op |

> **Discrepancy against the skills.** `field-compiler` §10 and `field-testing`
> §6 both cite the first call site as `src/main.cpp:37506`. The actual
> `Expression::Evaluate` call is on **line 37507**; 37506 is `siblings["hi"] = ref.maxValue;`.
> Verify by `grep -n "Expression::Evaluate" src/main.cpp` before you start, and
> fix the two skills' line references in the same commit.

### 5.3 Behavioural contract — measured against the current binary, not guessed

Every row below was **executed** against the current `src/core/Expression.cpp`.
Reproduce this table with your own probe before you edit anything, and again
after; it is the shape of the corpus.

Bindings for the table: `t = 1.25`, siblings `{max:7, min:3, lo:0, hi:1}`, globals `nullptr`.

| Expression | `ok` | value (as `double` of the returned `float`) | error |
|---|---|---|---|
| `-2^2` | 1 | `4` | — |
| `2^3^2` | 1 | `512` | — |
| `1.2.3` | 1 | `1.2000000476837158` | — |
| `1e3` | **0** | — | `unexpected trailing text` |
| `0 && 1/0` | **0** | — | `division by zero` |
| `1 \|\| 1/0` | **0** | — | `division by zero` |
| `if(0, 1/0, 5)` | **0** | — | `division by zero` |
| `sqrt(0-1)` | 0 | — | `sqrt() of a negative number` |
| `log(0)` | 0 | — | `log() needs a positive argument` |
| `5 % 0` | 0 | — | `division by zero` |
| `-3 % 2` | 1 | `-1` | — |
| `mod(0-3,2)` | 1 | `-1` | — |
| `2 < 3 < 1` | 1 | `0` | — |
| `!0` | 1 | `1` | — |
| `"   "` (spaces) | 0 | — | `empty expression` |
| `t` | 1 | `1.25` | — |
| `pi` | 1 | `3.1415927410125732` | — |
| `max` | 1 | `7` | — |
| `max(1,2)` | 1 | `2` | — |
| `round(-0.5)` | 1 | `0` | — |
| `round(2.5)` | 1 | `3` | — |
| `clamp(5,0,1)` | 1 | `1` | — |
| `step(0.5,0.7)` | 1 | `1` | — |
| `smoothstep(0,1,0.5)` | 1 | `0.5` | — |
| `(1+2` | 0 | — | `expected ')'` |
| `1,2` | 0 | — | `unexpected trailing text` |
| `0.1+0.2` | 1 | `0.30000001192092896` | — |

Plus the current randomness values (**these are the `random` corpus set — see
§7.7 and step 2**):

| Expression | value at `t = 1.25` |
|---|---|
| `rand()` | `0.76586776971817017` |
| `noise(2)` | `0.55055892467498779` |
| `rand(0,1,2)` | `0.55055892467498779` (identical to `noise(2)` — same 3-sine body) |
| `sh(0,1,4)` | `0.17868475615978241` |

### 5.4 Contract details the table does not show

| Rule | Where it lives today |
|---|---|
| **On failure, `outValue` is left untouched.** `Evaluate` returns before assigning. | `Expression.cpp:420-424` |
| **On success, `outError` is NOT cleared.** Callers pass a fresh string. Do not "fix" this. | `Expression.cpp:425-426` |
| **The first error wins.** `ParseState::Fail` refuses to overwrite a non-empty `error`. | `Expression.cpp:25-29` |
| **Identifier resolution order:** `t` → `pi` → siblings → globals → error. A sibling named `t` can never win. | `Expression.cpp:266-285` |
| **Function-ness is decided by the following `(`,** not by the name. `max` is a variable in AnalyzeNodes and a function everywhere. | `Expression.cpp:264-265` |
| **All computation is `double`;** narrowing to `float` happens once, at `outValue = (float)result`. | `Expression.cpp:425` |
| **Division and `%` by zero are errors, not `inf`/`nan`.** | `Expression.cpp:319, 326` |
| **`if(a,b,c)` is a function with three eagerly-evaluated arguments,** not a keyword. | `Expression.cpp:226`, `Expression.h:41-45` |
| **`&&` and `\|\|` do not short-circuit.** Both sides are fully evaluated before the logical op. | `Expression.cpp:374-394`, and the `0 && 1/0` row above |
| **Comparisons/logicals yield exactly `1.0` or `0.0`** and treat any non-zero as true. | `Expression.h:37-40` |
| **`step`/`smoothstep` take edges first, matching GLSL.** | `Expression.cpp:155-165` |
| **`rand`/`noise` are the same function**; `rand`/`noise`/`sh` all accept 0–3 args with the `(speed)` / `(min,max)` / `(min,max,speed)` overload shapes. | `Expression.cpp:166-223` |

---

## 6. Step-by-step procedure

### Phase 0 — corpus first, before any edit (non-negotiable)

> `field-testing` §2 finding, verified: there is **no** `INFINITE_EXPRTEST` or
> any fixture covering `Expression::Evaluate`
> (`grep -o 'INFINITE_[A-Z0-9]*' src/main.cpp | sort -u`), and the one shipped
> patch `assets/examples/patch_1.inf` contains **zero `expr` and zero `glob`
> lines** (verified: `grep -c '^expr \|^glob ' assets/examples/*.inf` → 0).
> The language layer has zero automated coverage today. **If you author the
> corpus after the rewrite, the golden values encode the new implementation and
> the test proves nothing.**

1. **Harvest.** `grep -rn '^expr \|^glob ' --include=*.inf .` across
   `assets/examples/`, `dist/`, and the user's own patch directory (ask the
   owner where that is; do not guess). Expect nothing from the shipped example.
   Record whatever you find verbatim.
2. **Author.** Cover every behaviour `src/core/Expression.h` documents — that
   header **is** the spec (`field-testing` §2.1.2):
   - `+ - * / % ^`, unary `-`, unary `+`, unary `!`, right-associative `^`
   - `< <= > >= == !=`, `&& || !`, each yielding exactly 1 or 0
   - every function: `sin cos tan abs min max clamp floor ceil round mod lerp
     sqrt exp log pow sign step smoothstep rand noise sh if`
   - the constant `pi`
   - `step`/`smoothstep` edges-first
   - binding: `t`, `lo`, `hi`, a sibling, a global, and **siblings shadowing
     globals** (`Expression.h:16-19`)
   - `if(cond,a,b)` evaluating both branches
   - every documented failure: unknown identifier, wrong argument count,
     `log()` of non-positive, `sqrt()` of negative, division by zero,
     unexpected trailing text, empty expression
   - every row of §5.3 above, verbatim
3. **Split the corpus into two sets now, not later:** `deterministic` and
   `random`. Only `rand`/`noise`/`sh` cases go in `random`. Step 2 is allowed
   to re-baseline `random` and nothing else (`field-testing` §4).
4. **Capture golden values by running the current binary**, at several fixed
   `t` values, at full `double` precision (`%.17g`).
5. **Freeze** as a data file with the `field-testing` §2.2 record shape:
   `expr | t | siblings | globals | expect | expectOk | expectErrSubstr`.
6. **Commit the corpus and the fixture on their own, before any change to
   `Expression.cpp`.** Green on the unmodified binary is the baseline.

### Phase 1 — the harness

Add `INFINITE_FIELDTEST` as an **early-exit, headless** fixture — the
`INFINITE_DSPTEST` shape (`src/main.cpp:28204` for the body's comment style,
`:37619` for the dispatch). It runs before `glfwInit()` and touches no
GL/ImGui, for the same reason `DSPTEST` does: this is a pure-computation test,
not a rendering test.

Sections, per `field-testing` §3 — **A–D are step 1's scope**; E–I land in later
steps but stub them so the section list is stable:

| Section | Asserts |
|---|---|
| **A. Corpus** | every record hits its golden value at `double` exactness; every failure case fails with the right message substring |
| **B. Lexer** | maximal munch: `<= >= == != && \|\| += -= *= /=` each lex as **one** token |
| **C. Spans** | a deliberate error on line 3 column 7 reports line 3 column 7 |
| **D. Types** | scalar arithmetic types as `float`; the vector cases land in step 3 |

Verdict format matches the house style: one line per section ending ` OK`, or
containing `FAIL` with the case name and both values. Then register
`"FIELDTEST:1"` in `driver.sh`'s `TIER1_CHECKS`.

### Phase 2 — lexer

Per `field-compiler` §2. Every token carries `{ offset, line, col, length }`.
Not optional — §7's error UX is impossible without it, and today's evaluator has
no positions at all.

Emit `<= >= == != && || += -= *= /=` as single tokens by maximal munch. This is
exactly the bug class `Expression.cpp:54-56` and `:361-362` carry hand-written
comments about; the current parser only avoids it by hand-ordering its
`ConsumeStr` calls, and the tokenizer exists to delete the class.

**Number literals — read §7.1 before writing this.**

### Phase 3 — parser → AST

`field-compiler` §4's node set, and nothing beyond it. `Program` is the kernel
body; there is no `Kernel` node and no `Transfer` node — transfer operators are
`Call` nodes and inference gives them meaning later (step 8).

Error recovery at statement boundaries (newline or `;`), max ~5 errors reported
(`field-compiler` §7). A step-1 expression is a single expression with no
statements, so recovery has nothing to do yet — build it anyway, it is free now
and expensive to retrofit.

### Phase 4 — typed IR + inference

- **Type and domain live on the node**, never in a side table keyed by pointer
  (`field-compiler` §6.4 — a side table does not survive serialization).
- Step 1 has one domain: `frame`. `t` seeds `frame`; literals and bound
  identifiers seed `graph`; the join is the finer of two comparable domains
  (`field-compiler` §5). Implement the fixpoint loop now even though a
  single-domain program converges in one pass — step 4 adds the back-edge that
  needs it.
- Constant folding in **`double`** (`field-compiler` §3).

### Phase 5 — bytecode + VM

Register machine, not a stack machine (`field-compiler` §6.1). `double`
accumulators for the frame domain; narrow once at the boundary. No allocation
in the execute path — run `field-realtime` §1 rules 1–3 and §3 against your own
VM loop.

### Phase 6 — the cache, and its trap

Compile once, execute many. **Key the cache on `(text, binding-set identity)`,
or resolve identifiers late** (`field-compiler` §10). See §7.2 below for the
bug this prevents; it is the single most likely way to ship a silent
wrong-answer regression in this step.

Bound the cache (an entry count or byte budget with LRU eviction). A session
that retypes an expression character by character for an hour must not grow
without limit.

### Phase 7 — adapt `Expression::Evaluate`

```
Evaluate(text, t, siblings, globals, outValue, outError)
   │
   ├─ empty/whitespace-only text?  ──▶ outError = "empty expression"; return false
   ├─ look up (text, binding-set) in the cache
   │     miss ──▶ lex → parse → infer → emit; on error: outError = first error; return false
   ├─ bind t / siblings / globals into the VM's environment
   ├─ execute ──▶ runtime error (div-zero, sqrt<0, log<=0)? outError = that; return false
   └─ outValue = (float)result; return true          # outError untouched
```

### Phase 8 — verify, then run the gates

Order matters: build, then the new fixture, then hygiene, then the diff
checklist, then the Desktop copy this project requires.

---

## 7. Traps

Each row names the **bug it prevents**. These are not style notes.

### 7.1 The number grammar in the skill is wider than the code

`field-compiler` §2 specifies `NUMBER` as
`[0-9]+ ('.' [0-9]*)? ([eE][+-]?[0-9]+)?` and `'.' [0-9]+`.
**The current code does not implement that.** `ParseNumber`
(`Expression.cpp:77-90`) consumes any run of digits and dots and hands it to
`atof`. Measured consequences:

| Input | Today | A §2-conformant lexer |
|---|---|---|
| `1e3` | **error**, `unexpected trailing text` | `1000` |
| `1.2.3` | **`1.2`**, silently, `ok=1` | lex error |

Both are corpus-visible changes.

> **The code wins over the skill.** Do not silently widen the grammar.
> Recommended: implement §2's `NUMBER` pattern **but** put both rows in the
> corpus as explicit, commented, deliberate changes, and get the owner's
> answer before landing. If the owner says no, restrict the lexer to the
> current behaviour and record the divergence in `field-compiler` §2.
> **Bug prevented:** a patch containing `=1.2.3` (a typo that currently
> evaluates) starts erroring after an "identical-API" refactor, or a patch
> containing `1e3` changes value.

### 7.2 The cache-key trap — `min` and `max` are functions in two callers and variables in the third

`AnalyzeNodes.cpp:172-173` binds `vars["max"]` and `vars["min"]` as **values**.
`main.cpp` and `ExprGlobals.cpp` do not. Measured: with sibling `max = 7`,
`max` evaluates to `7` and `max(1,2)` evaluates to `2` — **in the same
program**. A cache keyed on text alone, with identifier resolution baked into
the IR, will hand AnalyzeNodes an IR compiled for main.cpp's binding set and
resolve `max` as a function (or vice versa).

Same shape, second instance: `main.cpp:37505-37509` **mutates the caller's
sibling map** to inject `lo`/`hi` and restores it afterwards, so the same
`nodeIndex`'s binding set differs between the moment of the call and the
moment before it.

> **Bug prevented:** the Image Analyze node's custom formula silently returns a
> different number after the refactor, with no error and no crash — the worst
> possible failure mode, and one no compile check catches.
> **Fix:** key on `(text, binding-set identity)` — a hash of the sorted key
> names, not the values — or keep identifier→slot resolution outside the cached
> program and do it per call.

### 7.3 Unary minus binds tighter than `^`

Measured: `-2^2` → **`4`**, not `-4`. Most languages (and most people) expect
`-4`. `ParsePower` calls `ParseAtom` first, and `ParseAtom` consumes the `-`
(`Expression.cpp:242-247, 292-303`).
> **Bug prevented:** "fixing" precedence to match convention silently changes
> the value of every saved `=-x^2`.

### 7.4 `round` is `floor(x + 0.5)`, not `std::round`

`Expression.cpp:139`. Measured: `round(-0.5)` → **`0`**. `std::round(-0.5)` is
`-1`. `round(2.5)` → `3` from both, so a spot check will not catch it.
> **Bug prevented:** a "cleanup" to `std::round` changes every negative
> half-integer in every saved patch.

### 7.5 `mod` is C `fmod`, not GLSL `mod`

Measured: `mod(-3, 2)` → **`-1`**. GLSL's `mod` gives `1`. `field-compiler` §6.4
requires every intrinsic to have **one semantic definition** all backends match
— so pick C `fmod` now, write it down in the IR's intrinsic table, and make the
future GLSL backend emit the correction rather than plain `mod()`.
> **Bug prevented:** the step-7 pixel backend produces different numbers from
> the frame VM for the same source, and it is discovered a year later.

### 7.6 `&&`, `||` and `if()` do **not** short-circuit — and runtime errors leak out of the untaken side

Measured: `0 && 1/0` → **error `division by zero`**, not `0`. Same for
`1 || 1/0` and `if(0, 1/0, 5)`. This is not an accident of the current
implementation being an evaluator — `Expression.h:41-45` documents the
no-short-circuit rule on purpose.
> **Bug prevented:** a bytecode VM that naturally emits a branch for `&&` makes
> `0 && 1/0` succeed with `0`, which is *better* behaviour and still a corpus
> failure. If you want to change it, that is a separate, owner-approved,
> release-noted change — not a side effect of step 1.

### 7.7 Parse errors and evaluation errors share one channel, and the first one wins

Today `sqrt(-1)`, `log(0)` and `1/0` are raised **during the single walk**, by
the same `Fail()` that raises `expected ')'`. After the split they become
*runtime* errors from the VM, raised on a different pass. Two things must hold:
1. The error **string** is unchanged, so `expectErrSubstr` still matches.
2. Where a program has both a parse error and a would-be runtime error, the
   **parse** error is the one reported — which is also what happens today,
   because `Fail` keeps the first.
> **Bug prevented:** the error text shown under a knob changes, and every
> failure-case corpus record has to be "re-baselined", which is exactly the
> hole through which a real regression walks.

### 7.8 `atof` is locale-dependent

`Expression.cpp:89` uses `atof`. In a locale where the decimal separator is `,`,
`atof("1.5")` returns `1`. Infinite does not appear to call `setlocale`, so this
is latent — but a new lexer using `std::stod` inherits the same hazard, and one
using `std::from_chars` does not.
> **Bug prevented:** a Windows build under a German/French locale evaluating
> every decimal literal as its integer part. Use `std::from_chars` (C-locale by
> construction) and note the change in the commit message. Then add a corpus
> case that runs under a forced non-C locale, or say plainly why you did not.

### 7.9 `Evaluate` does not clear `outError` on success

`Expression.cpp:425-426`. Callers pass a fresh string every time, so nothing
depends on it — but "tidying" it to clear on success changes nothing today and
could mask a caller bug later. Leave it, and add a comment saying it was
deliberate.

### 7.10 `ExprGlobals`'s growing sibling map is a *feature*

`ExprGlobals.cpp:72` passes `sValues`, which grows as the loop walks the global
list, so a global that references one **below** it fails with `unknown
identifier` rather than silently reading last frame's value
(`ExprGlobals.h:22-23`: "a cycle is structurally impossible rather than
something to detect at runtime"). A cache that memoizes "this text resolved
identifier `foo` to slot 3" across the loop breaks that guarantee.
> **Bug prevented:** a forward-referencing global silently starts working, and a
> cycle becomes possible.

### 7.11 An empty/whitespace-only expression is `empty expression`, not a parse error

`Expression.cpp:407-411` checks `Peek() == '\0'` **after** skipping whitespace,
*before* parsing. And in `ExprGlobals::EvaluateAll` an empty global expression
never reaches `Evaluate` at all — it publishes its last value and clears its
error (`ExprGlobals.cpp:56-63`), because a blank row mid-edit is not a failure.
> **Bug prevented:** typing over a global's expression makes every downstream
> parameter jump to zero for one frame.

### 7.12 Do not recompile a failing program every frame

`FormulaNode::CookIfNeeded` recompiles only when `mProgram == 0 &&
mLastError.empty()` (`src/nodes/FormulaNode.cpp:415-416`), so a broken program
costs nothing per frame. The expression path must get the same property once
there is a compile step: **cache the failure**, keyed the same way as a success,
and return the cached error.
> **Bug prevented:** one typo'd expression under a knob re-runs the lexer,
> parser, inference and emitter 60 times a second forever.

### 7.13 The last good value must survive a failure

`main.cpp:37515-37524` deliberately leaves `*ref.value` alone when `Evaluate`
returns false and records the error via `Modulation::SetExpressionError`, "so a
typo mid-edit should not blank out the render". `ExprGlobals` does the same
(`ExprGlobals.cpp:74-80`). Neither call site changes — but if your adapter ever
writes `outValue` on the failure path, both protections evaporate.

### 7.14 Precision — `double` everywhere, narrow once

`field-compiler` §3. A VM with `float` accumulators, or constant folding in
`float`, drifts in the last bits and every golden value fails by a hair.
Measured witness: `0.1+0.2` → `0.30000001192092896`, which is
`(float)(double)0.1 + (double)0.2` — computed in `double`, narrowed once.

### 7.15 Do not let a backend-specific node into the IR

`field-compiler` §6.4. The moment a `GlslMix` node exists, the future C++ and
WASM backends have to special-case it. Check this rule on every IR change, not
at the end.

---

## 8. Machine-checkable exit criterion

Every command below must pass. Run them in this order. Paste the actual output
into the commit message — do not paraphrase.

```bash
cd /Users/namansoni/infinte

# 1. The three call sites are untouched. Must print exactly 3 lines,
#    at the same line numbers as before the change.
grep -rn "Expression::Evaluate" src/

# 2. The public signature is byte-identical to main's.
git diff main -- src/core/Expression.h | grep -E '^[+-]\s*(bool Evaluate|const std::map|float& outValue)' 
#    ^ must print NOTHING.

# 3. It builds clean.
cmake --build build -j"$(sysctl -n hw.ncpu)" 2>&1 | tail -20

# 4. The corpus passes. Every section line ends OK; no FAIL anywhere.
INFINITE_FIELDTEST=1 build/Infinite.app/Contents/MacOS/Infinite

# 5. The corpus fixture can actually fail (invariant 1.2.5).
#    Perturb one golden value in the data file, re-run, confirm FAIL, revert.

# 6. The full gate.
.claude/skills/run-infinite-hygiene/driver.sh

# 7. Real-time diff checklist — field-realtime §1 rules 1-3 against the VM.
git diff main -- src/core/field/ | grep -nE 'new |malloc|push_back|resize|reserve|std::function|while *\(' 
#    ^ every hit must be justified in the commit message, or removed.

# 8. Project convention.
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

Plus these, which need a human but are still binary pass/fail:

| # | Check |
|---|---|
| 9 | A saved patch containing `expr` and `glob` lines (author one if none exists) loads and renders **identically** before and after — screenshot diff, not "looks the same". |
| 10 | A deliberately broken expression under a knob leaves the last good value in place, shows its error, and does **not** recompile per frame (assert on a compile counter over 10 frames). |
| 11 | `INFINITE_FIELDTEST` is registered in `driver.sh`'s `TIER1_CHECKS` and appears in the hygiene output. |

---

## 9. Explicitly out of scope for step 1

| Not in this step | Where it belongs |
|---|---|
| `vec2` / `vec3` / `vec4`, swizzles, rank polymorphism | **step 3** — `docs/plans/field/step-03-vectors-and-rank.md` |
| Changing `rand` / `noise` / `sh` | **step 2** — a deliberate visible break with its own release note |
| The `element`, `pixel`, `sample` or `graph` domains | steps 4, 7, 9, 10 |
| `attrib`, `param`, `state` declarations | steps 5–6 |
| Statement-form `if`, `for`, `{}` blocks, `#` comments, `;` — as *language surface* | later steps. The **lexer and AST** may recognise them now (§4's node set is the whole set), but `Evaluate` must still reject anything today's grammar rejects |
| `reduce` / `map` / `resample` / `downsample` | step 8 |
| A Field **node** in the graph, an editor, a `ParamRef` registration | step 5 and `field-integration` |
| Answering `field-language` §7's OPEN question (where a `param`'s default lives) | step 5; ask the owner |
| Answering `field-language` §10's OPEN question (keep `^`, lower to `pow`, or remove) | **must be asked now** — the corpus depends on it. Recommended default (b): keep `^` in the surface syntax, lower to `pow()` in the IR. Do not decide silently |
| Answering `field-language` §12's OPEN question (how `seed` enters `rand`) | step 2 |
| Answering `field-compiler` §9's OPEN question (SoA vs `Mesh`'s AoS) | step 4 |
| Making the undo/patch format faster, changing `Patch::Data`, or anything in `docs/plans/undo-delete-perf-prompt.md` | that separate brief |
| `FormulaNode`, `EquationNode` — reading them is required, changing them is not | steps 7 and 9 |

---

## 10. Report back with

1. The corpus file's path and record count, and the commit that froze it
   **before** the rewrite.
2. The before/after output of every command in §8.
3. Owner answers to: the `^` question (§9), the `1e3` / `1.2.3` grammar
   question (§7.1), and where the user's own `.inf` patches live (§6 Phase 0.1).
4. Any further place where a skill and the code disagree — the **code wins**,
   and the skill gets fixed in the same commit.
