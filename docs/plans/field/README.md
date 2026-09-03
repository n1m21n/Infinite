# Field — the master build plan

**Repo:** `/Users/namansoni/infinte` (C++17, ImGui + OpenGL, **MIT**).
**What Field is:** one expression language, five domains, one typed IR, three backends.
It absorbs the four-and-a-bit incompatible mini-languages Infinite already carries
and replaces them with a single primitive.

This file **sequences** the work. Each row's prompt is self-contained — a fresh
session needs the prompt and nothing else, not this file.

---

## 0. Invariants that apply to every step

These are restated verbatim inside each step prompt. They are repeated here so a
reader who only opens the README still cannot get them wrong.

| # | Invariant | Why |
|---|---|---|
| 1 | **Clean room.** Never open Kronos, Cmajor, SuperCollider, or `/Users/namansoni/BespokeSynth`. All GPL; Infinite is MIT. Cite the papers freely, never the code. | a single GPL read contaminates the licence of the whole project |
| 2 | **Bare names. No sigils. Ever.** `P`, `N`, `uv`, `t` — never `@P`. | owner decision, final |
| 3 | **Rate is inferred, never declared.** No `@rate`, no `krate`, no domain annotation. | the compiler decides from data dependencies; the programmer cannot override it |
| 4 | **Only step 1 changes existing *behaviour*.** Steps 2–10 add new files and new node types. But "additive" is not "touches nothing": every step that adds a node type must also make **additive registration edits in `src/main.cpp`** — an include, `REGISTER_NODE`, body-draw dispatch, and help text (`SpecificNodeHelpText:24153`, `NodeHelpText:24472`). That applies to steps **4, 7, 9 and 10**. Step 10 needs more than registration — `SpawnNode` (`main.cpp:4967`) and `RemoveNodeByIndex` (`main.cpp:25666`) are file-local to `main.cpp`, so it must add a seam. See §4 discrepancy 14. | nothing already built changes meaning — but the brief's flat "only step 1 touches existing code" is false, and a session that believes it will be blocked the moment it tries to register its node |
| 5 | **No allocation, no locks, no I/O on the audio thread.** | an xrun is a bug, not a performance note |
| 6 | **The code wins over any skill.** Where a skill or the design brief disagrees with `src/`, follow the code and record the discrepancy in the prompt. | the skills were written from a brief; the brief was written from memory |
| 7 | **Branch first**, per `.claude/skills/git-branch-workflow`: `feature/field-step-NN-slug`. | |

---

## 1. Start here

**If you are the owner, deciding what to do next** → read §2's table, top to bottom. Steps are ordered; there is no useful reordering.

**If you are a fresh session about to implement one step** → open only that step's prompt file. Do not read the others; they will not help and they will crowd your context.

**If you are about to start step 1** → the first task in that prompt is *not* an edit. It is **authoring the regression corpus against the current binary**, because the corpus does not exist yet (see §4, discrepancy 3). Do that before touching `Expression.cpp` or you have nothing to compare against.

**Reading order for the contract itself** (the skills, all committed):

| Order | Skill | What it settles |
|---|---|---|
| 1 | `.claude/skills/field-language/SKILL.md` | the surface — one primitive, five domains, bare names, reserved words, types, operators |
| 2 | `.claude/skills/field-compiler/SKILL.md` | lexer → AST → typed IR → three backends; domain inference as a lattice fixpoint |
| 3 | `.claude/skills/field-state/SKILL.md` | `state` as delay sugar; the every-cycle-contains-a-delay rule |
| 4 | `.claude/skills/field-domains/SKILL.md` | `reduce` / `map` / `broadcast` / `resample` / `downsample` and what each crossing costs |
| 5 | `.claude/skills/field-realtime/SKILL.md` | the 12-row checklist to run against a diff |
| 6 | `.claude/skills/field-integration/SKILL.md` | `INode` / `ParamRef` / `ParamMailbox` / `GLUtil::CompileProgram` contracts |
| 7 | `.claude/skills/field-testing/SKILL.md` | the corpus, the `INFINITE_FIELDTEST` harness, **§6 is the authoritative exit-criteria table for all ten steps** |

---

## 2. The ten steps

Size is rough implementation effort, not prompt length.

| # | Delivers | Depends on | Touches existing code | Exit criterion (short form — `field-testing` §6 is authoritative) | Size |
|---|---|---|---|---|---|
| **1** | [`Expression.cpp` → lexer / AST / typed IR / bytecode](step-01-expression-to-ir.md), behind a **byte-identical** public API. Parses once and caches instead of re-parsing every evaluation. | — | **YES — the only one** | corpus authored *before* the change and passing after, at `double` exactness; `Expression::Evaluate`'s signature and its three call sites unchanged; harness A–D; a saved patch with `expr` lines renders identically | L |
| **2** | [`rand` / `noise` / `sh` as pure functions of `(t, seed)`](step-02-pure-randomness.md). **Deliberate visible break.** | 1 | no | `deterministic` corpus set unchanged; `random` set re-baselined in its own commit; `TimeToRand` never negative over a `t` sweep; same seed reproduces, different seeds differ; **release-note entry exists** | S |
| **3** | [`vec2`/`vec3`/`vec4` + APL-style rank polymorphism](step-03-vectors-and-rank.md) (scalar broadcasts where a vector is expected). | 1, 2 | no | harness section D; scalar→vector broadcasts; `vec2`→`vec3` **refused**; every corpus record still passes — scalars must not change meaning | M |
| **4** | [the `element` domain](step-04-element-domain.md) — one kernel run per point/vertex. **Does not exist in Infinite today**; biggest new surface. | 1, 3 | additive only — node registration in `main.cpp` | element conformance table; body ran exactly N times and the hoisted subexpression exactly once; AoS/SoA round trip; `geometry-transform-sweep` passes | XL |
| **5** | [`param float amount = 0.5 [0, 2]` auto-creates a knob](step-05-param-declarations.md) registered through the existing `ParamRef` machinery. | 1, 3 | no | every `param` appears in the modulation matrix and is bindable; `node-param-audit` passes; **a binding survives editing the Field source**; params survive save/load/undo/copy-paste | M |
| **6** | [`state` cells](step-06-state-cells.md) lowered to unit delays; the cycle-legality check; reset, serialization, hot-reload transplant. | 1, 3 | no | `field-state` §9's nine criteria; harness section H; seek/loop/stop reset verified by fixture; `(name, type)` transplant verified in all four cases | L |
| **7** | [the `pixel` domain](step-07-pixel-domain.md) — typed IR transpiled to GLSL text, fed to `GLUtil::CompileProgram`. | **1, 3, 4, 5, 6** (2 soft) | additive only — node registration in `main.cpp` | pixel conformance table; `#version 150`; a broken program keeps the last working one and **does not retry per frame**; predication observable; Windows check | L |
| **8** | [`reduce` / `map` / `broadcast` / `resample` / `downsample`](step-08-transfer-operators.md) — the domain-crossing operators. `broadcast` is implicit and never written. | 1, 3, 4, 6 | no | `field-domains` §10's nine criteria; incomparable crossings refused **with hints**; `downsample(x, k)` with non-constant `k` refused; no new cross-thread channel | L |
| **9** | [the `sample` domain](step-09-sample-domain.md) — a register machine on the audio thread. | 1, 2, 3, 5, 6 (8 for crossings) | additive only — node registration in `main.cpp` | sample conformance table; **zero allocations after `PrepareToPlay`**; zero xruns on teardown; `AUDIOPARAMSWEEPTEST` and `AUDIOTEARDOWNSWEEPTEST` pass; a one-pole matches its analytic response | XL |
| **10** | [the `graph` domain](step-10-graph-domain.md) — kernels that emit nodes, at edit time (rate zero). **Last on purpose.** | 1–9 | **yes, beyond registration** — needs an `IFieldGraphHost` seam; see §4 disc. 14 | graph conformance table; list-order evaluation preserved; `IsValidName` unchanged; **and nothing in steps 1–9 regressed** — full harness plus hygiene | XL |

**Also in this directory, not a build step:**

| File | What it is |
|---|---|
| [`algorithms.md`](algorithms.md) | the payoff. A ladder — (0) pure formula, (1) memory, (2) prediction, (3) self-correction, (4) learning from the user — of what the five domains make buildable, each with its state cost and a real-time-safe verdict. Read this to answer "why build a language at all". |

### Why step 10 is last

Not scheduling convenience. Norilo's two-year teaching evaluation (*Computer
Music Journal* 39:4, 2015, p. 45) found students grasped filters immediately
because the patches corresponded closely to textbook diagrams, but could not
apply algorithmic routing — the metaprogramming layer — unaided. Graph
metaprogramming impresses experts and loses everyone else. It ships when the
other four domains are solid.

### Dependency shape

```
1  Expression → IR          the only step that changes existing behaviour
│
└─ 2  pure randomness       (soft dep for 7 and 9)
   │
   └─ 3  vectors + rank
      │
      ├─ 4  element domain ─────────────┐
      │     (domain inference + hoisting — 7 and 8 both need it)
      │                                 │
      ├─ 5  param → ParamRef ───────────┤
      │                                 │
      └─ 6  state cells ────────────────┤
                                        │
            ┌───────────────────────────┤
            │                           │
      7  pixel domain            8  transfer operators
      (needs 1,3,4,5,6)          (needs 1,3,4,6)
            │                           │
            └─────────┬─────────────────┘
                      │
              9  sample domain   (needs 1,2,3,5,6; 8 for crossings)
                      │
             10  graph domain    (needs all of 1–9)
```

**The easy mistake:** step 7 looks self-contained — "it's just a GLSL emitter".
It is not. Without **step 4's domain-inference fixpoint and hoisting**, the
backend emits shaders that are *correct* but several times slower than they
should be, and the language's central claim — rate is inferred, so hoisting is
free — goes unverified. Do 4 before 7.

---

## 3. Glossary

| Term | Means |
|---|---|
| **domain** | the rate a piece of code runs at. Five of them: `graph` (per graph edit, rate 0), `frame` (60/s), `element` (60 × N, N ≈ 5000), `pixel` (60 × w × h — 124 M/s at 1080p), `sample` (48 000/s). A domain is *inferred* from what names an expression mentions, never written down. |
| **kernel** | one Field body, compiled once, run many times — once per whatever its domain counts. The same three lines are a frame kernel or an element kernel depending only on which names they mention. |
| **rate inference** | the compiler deciding a value's domain from its data dependencies, as a dataflow fixpoint over a lattice. `amount = 0.5 + 0.5 * sin(t)` mentions `t`, so it is frame-rate and is computed once per frame; `P.y += amount` mentions `P`, so it is element-rate. The scalar is broadcast to all N elements with nothing written to say so. Prior art: Faust's computation levels, Kronos's automated factorization of signal rates. |
| **state cell** | `state float z = 0`. **Sugar for a unit delay, not a mutable variable.** Reading `z` gives last tick's value. Every cycle in the dataflow graph must contain one, or the program is rejected. Serialized by `(name, type)` so editing the body does not scramble it. |
| **transfer operator** | how a value crosses domains. `reduce` many→one, `map` one-per-element, `broadcast` one→many (**implicit, never written**), `resample` read domain A while standing in B, `downsample` run at a fraction of the ambient rate. Every crossing has a cost; the compiler refuses incomparable ones. |
| **typed IR** | the middle of the pipeline. Every node carries **both a type and a domain**. One IR, three backends: bytecode VM (`frame`, `element`), GLSL text (`pixel`), register machine (`sample`). Retargetable — nothing backend-specific is allowed into the IR. |

---

## 4. Known discrepancies between the skills and the real code

Recorded when the skills were written and again when each prompt was written.
**The code wins.** Each is handled inside the relevant step prompt; they are
collected here so they are not rediscovered from scratch.

| # | Discrepancy | Where it is handled |
|---|---|---|
| 1 | **`^` is missing from the design brief's operator list.** `Expression.cpp` implements it as right-associative power and `Expression.h:30` documents it. Dropping it would break the very corpus step 1 exists to protect. | `field-language` §10 (OPEN, "lower to `pow()`" flagged as the zero-cost option); step 3 |
| 2 | **"Four incompatible mini-languages" — there are five, plus a sixth consumer.** `src/audio/dsp/EquationDsp.h` has its own `TokenType`/`Token`/`AstType`/`AstNode`/`Parser` — that is where EquationNode's language actually lives, not `EquationNode.h`. And `src/nodes/AnalyzeNodes.cpp:179` is a third `Expression::Evaluate` caller binding a different variable set (`lum`, `sat`, `hue`, `u`, `v`, `motion`…). | `field-integration` §1; steps 1 and 9 |
| 3 | **The step-1 regression corpus does not exist.** There is no `INFINITE_EXPRTEST`; zero of ~110 fixtures touch the language; `assets/examples/patch_1.inf` has zero `expr`/`glob` lines. "Existing `=` expressions are the corpus" is intent, not fact. | `field-testing` §2 makes authoring it — against the *current* binary, before any edit — step 1's first task |
| 4 | **`rand`/`noise`/`sh` have no hidden counter today.** They are already pure in `t` — `rand` is a 3-sine sum, `sh` is `fract(sin(...)*43758)`. The real defects are autocorrelation and the absence of a seed. | `field-language` §12, `field-testing` §4, step 2 |
| 5 | **The brief's `TimeToRand(double t)` takes no `seed`** despite the stated `(t, seed)` requirement. | OPEN; step 2 |
| 6 | **`ParamRef` has no `defaultValue` field**, so `param float amount = 0.5 [0,2]`'s initial value has nowhere to live. | OPEN in `field-language` §7; step 5 |
| 7 | **GLSL is `#version 150` app-wide** (all 57 occurrences). `windows-parity`'s "GLSL 330 strictness" note is about something else — emitting 330 would be a cross-driver divergence. | `field-compiler` §6.2, `field-integration` §8, step 7 |
| 8 | **`Expression` evaluates in `double` throughout**, narrowing once at the end. A `float` IR fails every golden value by a hair. | `field-compiler` §3; steps 1, 3, and step 7's explicit GPU tolerance |
| 9 | **`min`/`max` are functions in two callers and *bound variables* in the third** (`AnalyzeNodes.cpp`). A naive IR cache keyed on source text alone will serve the wrong program. | step 1, trap 7.2 |
| 10 | **`ParamMailbox::kMaxParams = 128`** caps sample-domain `param` count; and editing a Field body reorders `param` declarations, which re-points every modulation cable if `paramIndex` is the ordinal. Same bug class as `node-ui-pillars` P7 for filter-mode indices. | OPEN in `field-integration` §3; steps 5, 9, 10 |
| 11 | **`src/core/Mesh.h` is hybrid, AoS where it matters.** `struct Vertex { px py pz nx ny nz u v }` in `std::vector<Vertex>` (`Mesh.h:10`) — interleaved AoS, 32 B/vertex; `Mesh.h:260` explicitly argues for it. But every attribute added *later* is SoA-shaped (`vertexColor`, `faceMask`, `selectionGroup`, `Polyline::points`). Field requires SoA. | `field-compiler` §9, as a table plus an OPEN with three costed options; step 4 |
| 12 | **`mix` does not exist. The interpolator is `lerp`.** `Expression.cpp:137` implements `lerp(a,b,t) = a + (b-a)*t`; there is no `mix` in the intrinsic list, and `mix(0,1,0.5)` returns `unknown function 'mix'`. But `field-language/SKILL.md:359`'s own worked example writes `prev = mix(prev, col.r, 0.1)` — **the skill ships an example that does not compile.** Ten further Field examples across `algorithms.md` and `step-08` inherited the error; all now rewritten to `lerp`, which is valid under either outcome of step 3's Decision 2. This is a Field-source rule only: GLSL's `mix` is real, so step 7 emitting `mix` is correct. | step 3 (Decision 2, escalated); step 7 §5.4; **the skill itself still needs the fix** |
| 13 | **The `graph` domain's reserved-word set is empty** (`field-language` §5), so a graph kernel has no name for "the edge just made" or "the type at each end". `algorithms.md` §9.6 (node co-occurrence) is therefore SPEC-ONLY and blocked. This is a **fifth** open primitive; `algorithms.md` §4 only carries four. | OPEN, unassigned — needs an owner decision before step 10 |
| 14 | **"Only step 1 touches existing code" is false.** Steps 4, 7, 9 and 10 each add a node type, which requires additive registration in `src/main.cpp` (include, `REGISTER_NODE`, body-draw dispatch, help text at `SpecificNodeHelpText:24153` / `NodeHelpText:24472`). Step 10 goes further: `SpawnNode` (`main.cpp:4967`) and `RemoveNodeByIndex` (`main.cpp:25666`) are **file-local to `main.cpp`**, so a graph kernel cannot emit or delete a node without a seam. | invariant 4 above, corrected; step 10 §4.2 confines it to a pure-virtual `IFieldGraphHost` plus a remap hook and enumerates the full permitted diff |
| 15 | **The step prompts disagreed on where Field's sources live** — steps 01/02/03/06/07/10 wrote `src/core/field/`, steps 04/05/08/09 wrote `src/field/`. Neither directory exists yet, so the split would have produced two parallel trees and step 4 would not have found step 1's IR headers. | **Resolved: `src/core/field/`** — it is what the foundational step 1 uses, and `Expression.cpp` already lives in `src/core/`. All ten prompts normalised. |
| 16 | **`ExprGlobals` is frame-domain, not graph-domain.** The brief calls patch-wide globals "graph-domain constants", but `EvaluateAll(t)` runs **per frame** (`main.cpp:37421`) and a global may read `t`. | step 10 D3: classify each global by constant-foldability; a `t`-dependent global is not readable from a graph kernel |
| 17 | **The skills' line-number citations have drifted.** Verified against the current tree: `Expression::Evaluate` call site 1 is `main.cpp:`**`37507`** (skills say 37506); `kMaxParams` is `ParamMailbox.h:`**`23`** (skills say 24); `ParamRef` is `Modulation.h:`**`28`** (skills say 29); `COMMON_SOURCES` is `CMakeLists.txt:`**`206`** (steps 4/5/8 said ~54/~98); `IsValidName` is `ExprGlobals.h:`**`45`** (said 44); `INFINITE_DSPTEST` dispatch is `main.cpp:`**`37619`** (`field-testing` §1 says ~9522). | **Rule for every session: the symbol is authoritative, the line number is a convenience. Re-grep the symbol.** Steps 2, 3, 6, 9 and 10 already say this in their headers; treat it as global. |
| 18 | **`GLUtil::CompileProgram` never calls `glBindFragDataLocation`** (`GLUtil.cpp:202-203` binds only `aPos`/`aUv`), and `layout(location=)` on fragment outputs is GLSL 330. So the pixel backend gets **one colour output**, capping pixel `state` at **4 scalar cells** in one RGBA16F ping-pong pair. MRT would mean editing shared code. Separately, `EnsureFbo` hardcodes `GL_LINEAR` (`GLUtil.cpp:128`), so state reads must use `texelFetch`, never `texture()` — otherwise an integrator diffuses sideways one texel per frame. | step 7 §5.0, as two OPEN items with implementable recommendations |

---

## 5. Rules for anyone editing this directory

- These are **prompts**, not implementation notes. Each is pasted into a session with zero context, so redundancy between files is correct and must not be "cleaned up".
- Tables, bullets, wrong/right pairs, worked examples. **Not prose paragraphs.**
- Every exit criterion is a **runnable command with stated expected output**, never a vibe.
- Every trap states **the bug it prevents**. A trap without a bug is a preference; delete it.
- When a step is implemented and merged, add the merge commit to its row in §2 rather than deleting the prompt.
