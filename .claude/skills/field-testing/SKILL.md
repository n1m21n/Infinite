---
name: field-testing
description: How to prove a Field change did not regress Infinite — building the step-1 regression corpus out of the `=` expressions saved patches already contain (and authoring it, because it does not exist yet), the golden-value harness as an INFINITE_* fixture, per-domain conformance cases for frame/element/pixel/sample, how to re-baseline across the deliberate randomness break, and the machine-checkable exit criterion for each of the 10 build steps. Use before and after any Field compiler or language change, when adding a backend, when asked "did this break saved patches", "how do I test the language", "what proves step N is done", or when writing the prompt for a session that will implement any build step.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

Read [`field-compiler`](../field-compiler/SKILL.md) for the pipeline this tests
and [`field-realtime`](../field-realtime/SKILL.md) for the diff checklist that
runs alongside it. Integration-level checks live in
[`field-integration`](../field-integration/SKILL.md) §10.

---

## 0. Invariants

1. **Clean room.** Never read Kronos, Cmajor, SuperCollider or BespokeSynth
   source. The Kronos *paper* (Norilo, CMJ 39:4, 2015) is citable.
2. **A test that cannot fail is not a test.** Every case below must have been
   observed failing at least once — introduce the bug deliberately, watch the
   fixture print `FAIL`, then fix it.
3. **The corpus is the contract.** Step 1's whole justification is that saved
   patches keep working. Without a corpus that claim is unverifiable.

---

## 1. How testing works in this repo

Infinite has **no test binary**. Tests are `getenv("INFINITE_…")` fixtures in
`src/main.cpp` that print a verdict line ending `OK`, containing `FAIL`, or
ending `BUG`. There are ~110 of them today
(`grep -o 'INFINITE_[A-Z0-9]*' src/main.cpp | sort -u`).

Two shapes to copy:

| Shape | Reference | When |
|---|---|---|
| **Early-exit, headless** — gated before `glfwInit()`, no GL/ImGui at all | `INFINITE_DSPTEST` (`src/main.cpp` ~9522) | anything that is pure computation: the lexer, parser, IR, bytecode VM, sample backend |
| **In-frame** — runs inside the normal render loop at a given `frameId` | `INFINITE_ROUNDTRIPTEST` (`src/main.cpp:43317`, fires at `frameId == 4`) | anything needing GL: the pixel backend, save/load of a real node |

`INFINITE_DSPTEST` is the model for the Field harness and its comment says why:
it "needs none of the GL/ImGui setup every other `INFINITE_*` fixture runs
inside; this keeps it fast and honest about being a pure-DSP test, not a
rendering test that happens to also touch audio." The same argument applies to a
language test.

**Then run `/run-infinite-hygiene` before committing.**

## 2. The step-1 regression corpus

> **Finding: the corpus does not exist yet. It has to be authored.**
>
> - There is **no `INFINITE_EXPRTEST`** or any fixture covering
>   `Expression::Evaluate` — `grep -o 'INFINITE_[A-Z0-9]*' src/main.cpp` has no
>   expression, formula, equation or modulation-expression entry. The language
>   layer has **zero** automated coverage today.
> - The one shipped example patch, `assets/examples/patch_1.inf` (687 lines),
>   contains **zero `expr` and zero `glob` lines**.
>
> So "the existing `=` expressions in saved patches are the regression corpus"
> is a statement of *intent*. **Step 1's first task is to write the corpus, and
> to write it against the current binary before touching `Expression.cpp`** —
> otherwise the golden values encode the new implementation's behaviour, and the
> test proves nothing.

### 2.1 Building it

1. **Harvest.** Grep every `.inf` file for `^expr ` and `^glob ` lines and
   collect the expression text. Include the user's own patch directory, not just
   `assets/examples/`. Today this yields nothing from the shipped example, so:
2. **Author.** Write expressions covering every feature `src/core/Expression.h`
   documents, because that header **is** the specification of what must not
   break:
   - operators `+ - * / % ^`, unary minus and unary plus, right-associative `^`
   - comparisons `< <= > >= == !=` and logicals `&& || !`, each yielding 1 or 0
     and treating any non-zero input as true
   - functions `sin cos tan abs min max clamp floor ceil round mod lerp sqrt
     exp log pow sign step smoothstep rand noise sh if`
   - the constant `pi`
   - `step`/`smoothstep` **edges-first**, matching GLSL — the header calls this
     out explicitly so the two languages in the app do not disagree
   - identifier binding: `t`, `lo`, `hi`, a sibling name, a global name, and the
     **shadowing precedence** (siblings win over globals — `Expression.h:16`)
   - `if(cond, a, b)` evaluating **both** branches, with no short circuit
   - every documented failure: unknown identifier, wrong argument count,
     `log()` of a non-positive number, unexpected trailing text, empty
     expression
3. **Capture golden values** by running the **current** binary, at
   `double` precision, at several fixed `t` values.
4. **Freeze** the corpus as a data file the fixture reads, not as literals
   scattered through `main.cpp`.

### 2.2 Corpus record shape

| Field | |
|---|---|
| `expr` | the expression text, without the leading `=` |
| `t` | the time to evaluate at |
| `siblings` | name → value bindings |
| `globals` | name → value bindings |
| `expect` | the golden value, full `double` precision |
| `expectOk` | whether `Evaluate` returns true |
| `expectErrSubstr` | for the failure cases, a substring the message must contain |

### 2.3 The precision rule

`src/core/Expression.cpp` computes in **`double`** throughout and narrows once
at `outValue = (float)result`. A compiler that folds constants in `float`, or a
bytecode VM with `float` accumulators, will drift in the last bits and every
golden value will fail by a hair. Compare `double`-exactly for the frame domain.
See [`field-compiler`](../field-compiler/SKILL.md) §3.

## 3. The golden-value harness

`INFINITE_FIELDTEST` — early-exit, headless, modelled on `INFINITE_DSPTEST`.

| Section | What it asserts |
|---|---|
| **A. Corpus** | every §2 record evaluates to its golden value; every failure case fails with the right message substring |
| **B. Lexer** | maximal munch: `<=` `>=` `==` `!=` `&&` `\|\|` `+=` `-=` `*=` `/=` each lex as one token. This is the bug class `Expression.cpp:56` and `:362` already carry hand-written comments about — the lexer either deletes it or reintroduces it |
| **C. Spans** | every token and every AST node reports the line and column it came from; a deliberate error on line 3 column 7 reports line 3 column 7 |
| **D. Types** | scalar→vector broadcast works; vec2→vec3 is refused; `.xz` and `.bgr` swizzles resolve |
| **E. Domain inference** | a table of programs, each with an expected per-node domain assignment; the fixpoint terminates; a `state` back-edge forces more than one pass; an incomparable-domain join errors with both spans |
| **F. Placement** | a frame-domain subexpression inside an element kernel is evaluated **once**, not N times — assert on an evaluation counter, not on the result value, because both give the same answer |
| **G. Refusals** | every row of [`field-realtime`](../field-realtime/SKILL.md) §1 rules 1–6 is refused, and the message names the span |
| **H. Cycles** | a delay-free cycle is refused and the message lists the cycle node by node; a cycle through `state` compiles |
| **I. Determinism** | compiling the same source twice yields byte-identical output for every backend |

**Verdict format**, matching the house style: one line per section ending `OK`,
or containing `FAIL` with the case name and both values.

## 4. Re-baselining across the randomness break

Step 2 makes `rand`/`noise`/`sh` pure functions of `(t, seed)`. **This is a
deliberate visible break** — saved patches using them will look different, and
it goes in the release notes.

What today's implementation actually is (`src/core/Expression.cpp:166–222`),
because the fixture must encode the old behaviour before replacing it:

| Function | Today | After step 2 |
|---|---|---|
| `rand` / `noise` | `(sin(τt) + sin(1.618τt) + sin(2.718τt)) / 6 + 0.5`, scaled into `[min,max]` | `TimeToRand`-based, per `field-language` §12 |
| `sh` | `fabs(fmod(sin(floor(t·speed)·123.456) · 43758.5453123, 1.0))` | same hash family, per-`(t, seed)` |

Note that neither has a hidden counter today — both are already pure in `t`.
The defect is autocorrelation and the absence of a seed, not statelessness.

**The procedure, so the corpus keeps its value:**

1. Split the corpus into `deterministic` and `random` sets **before** step 2.
2. Step 2 may change golden values **only** in the `random` set. A single
   changed value in the `deterministic` set is a bug in the change, not an
   expected break.
3. Re-baseline the `random` set against the new implementation and record the
   commit that did it, in the fixture's own comment.
4. Add a case for the `if (m < 0) m += 536870912;` trap: assert `TimeToRand`
   never returns a negative value, over a `t` sweep that includes the range
   where `Xorwise` produces a negative intermediate. **This is the one line the
   port gets wrong** — Haskell's `mod` is non-negative for a positive divisor
   and C++ `%` is not.
5. Add a case asserting two `rand` calls with different seeds differ, and that
   the same seed reproduces exactly.

## 5. Per-domain conformance cases

### `frame`
| Case | Assert |
|---|---|
| a kernel mentioning only `t` | evaluated once per frame |
| a `param` change | takes effect the same frame |
| `dt` | equals the real frame delta, and is not baked to 1/60 |
| a `state` cell | reset by seek, loop and stop; unchanged by a param change |

### `element`
| Case | Assert |
|---|---|
| N-element kernel | body ran exactly N times (counter, not result) |
| a frame-domain subexpression inside it | ran exactly once |
| `P` writes | land on the right element — write `P.y = i` and read back a ramp |
| `count` | equals N |
| a `state vec3` | 3N cells, and per-element values do not bleed across elements |
| **AoS/SoA** | if a conversion layer exists ([`field-compiler`](../field-compiler/SKILL.md) §9), assert round-trip equality `Mesh → Field store → Mesh` for vertices, normals, uvs **and** the parallel `vertexColor` / `faceMask` / `selectionGroup` arrays |
| geometry passthrough | run `.claude/skills/geometry-transform-sweep/SKILL.md` — an element node is a passthrough node and must forward every field in `field-integration` §7 |

### `pixel`
| Case | Assert |
|---|---|
| generated GLSL | starts `#version 150`, compiles via `GLUtil::CompileProgram`, `outError` empty |
| a deliberately broken program | `CompileProgram` returns 0, the **previous** program still renders, `LastError()` is non-empty |
| the retry policy | a broken program does **not** recompile on the next frame — assert on a compile counter over 10 frames |
| `if` lowering | both sides evaluated (predication) — assert by making both sides have observable side effects on the output value, not by timing |
| `state` | ping-pong pair swaps once per cook; a resolution change reallocates and clears both to the initial value |
| `TextureRevision()` | changes when pixels changed, does **not** when they did not — `.claude/skills/data-accuracy-sweep/SKILL.md` |
| precision | float, compared within a stated epsilon, **not** against the `double` frame-domain golden values |
| Windows | `.claude/skills/windows-parity/SKILL.md`. Generated GLSL is exactly the artifact that compiles on one driver and not the other |

### `sample`
| Case | Assert |
|---|---|
| a one-pole from `field-language` §13 | matches the analytic response at known cutoffs |
| `param` transport | a pushed value reaches the audio thread within one block, smoothed — this is what `AUDIOPARAMSWEEPTEST` already checks generically |
| 129 params | a **compile error naming `kMaxParams`**, not a silent truncation |
| allocation | zero allocations after `PrepareToPlay` — run under an allocation counter |
| teardown | delete the node mid-playback: no crash, no dangling cable, **zero xruns** (`AUDIOTEARDOWNSWEEPTEST`) |
| `state` reset | seek/loop/stop returns every cell to its initial value |
| denormals | a long decay tail does not fall into denormal territory (the engine's FTZ/DAZ guard plus the usual tiny-DC check) |

### `graph`
| Case | Assert |
|---|---|
| evaluation order | list order, as `ExprGlobals` already guarantees — "a cycle is structurally impossible rather than something to detect at runtime" (`src/core/ExprGlobals.h:22`) |
| a failing global | keeps its last good value and records the error (`ExprGlobals.h:26`) |
| name rules | `IsValidName` still refuses `t`, `pi`, `lo`, `hi` |

## 6. Exit criterion per build step

Each step is done when its row's checks all pass **and**
`/run-infinite-hygiene` passes.

| Step | What | Exit criterion |
|---|---|---|
| **1** | split `Expression.cpp` into lexer → AST → typed IR → bytecode | corpus (§2) authored **before** the change and passing after, at `double` exactness; `Expression::Evaluate`'s signature and its three call sites (`main.cpp:37506`, `ExprGlobals.cpp:72`, `AnalyzeNodes.cpp:179`) unchanged; harness sections A–D pass; a saved patch with `expr` lines loads and renders identically |
| **2** | pure `rand`/`noise`/`sh` | `deterministic` corpus set **unchanged**; `random` set re-baselined and the commit recorded; `TimeToRand` never negative over a `t` sweep; same seed reproduces, different seeds differ; the break is in the release notes |
| **3** | `vec2/3/4` + rank polymorphism | harness section D; scalar→vector broadcasts, vec2→vec3 refused; every corpus record still passes (scalars must not change meaning) |
| **4** | the `element` domain | element conformance table; body ran exactly N times and the hoisted subexpression exactly once; AoS/SoA round trip; `geometry-transform-sweep` passes |
| **5** | `param` → `ParamRef` | every `param` appears in the modulation matrix and is bindable; `node-param-audit` passes; a binding survives **editing the Field source** (this is where `field-integration` §3's open question must already be answered); params survive save/load/undo/copy-paste |
| **6** | `state` cells | `field-state` §9's nine criteria; harness section H; seek/loop/stop reset verified by fixture; hot-reload transplant by `(name, type)` verified in all four cases |
| **7** | `pixel` via GLSL transpile | pixel conformance table; `#version 150`; broken program keeps the last working one and does not retry per frame; predication observable; Windows check |
| **8** | `reduce`/`map`/`resample`/`downsample` | `field-domains` §10's nine criteria; incomparable crossings refused with hints; `downsample(x, k)` with non-constant `k` refused; no new cross-thread channel |
| **9** | `sample` domain | sample conformance table; zero allocations after `PrepareToPlay`; zero xruns on teardown; `AUDIOPARAMSWEEPTEST` and `AUDIOTEARDOWNSWEEPTEST` pass; a one-pole matches its analytic response |
| **10** | `graph` domain | graph conformance table; list-order evaluation preserved; `IsValidName` unchanged; **and nothing in steps 1–9 regressed** — the full harness plus hygiene |

## 7. What the harness cannot prove, and what covers it instead

| Gap | Covered by |
|---|---|
| the compiler's own C++ allocating on the audio thread | [`field-realtime`](../field-realtime/SKILL.md) §6, by review — there is no automatic check |
| a new cross-thread channel sneaking in | `grep` the diff for new atomics and queues |
| Windows-only GLSL divergence | `.claude/skills/windows-parity/SKILL.md`; the harness runs on macOS only |
| node UI regressions from auto-generated knob rows | `.claude/skills/node-ui-pillars/SKILL.md` acceptance checklist |
| whether the *design* is right | nothing. Every question marked **OPEN** in these skills goes to the owner, not to a test |
