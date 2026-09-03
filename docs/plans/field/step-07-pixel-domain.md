# Field — Step 7: The Pixel Domain (IR → GLSL)

**Status:** ready to implement
**Depends on:** steps 1, 3, 4, 5, 6 (see §9 — this is not optional, step 6 names step 7 as its dependent)
**Deliverable:** a GLSL backend that turns the typed IR from step 1 into `#version 150` fragment-shader
text, hands it to `GLUtil::CompileProgram`, and runs it once per pixel per frame.

---

## 0. Regression gate and branch discipline — added 2026-09-03, read this first

This section postdates the rest of this file. Where anything below conflicts with it, **this section
wins**. It exists because of what actually happened when steps 3, 4, 5 and 6 were implemented as one
batch on one branch:

- Step 6 added a pre-scan in `LowerElementProgramToIR` (`src/core/field/FieldIR.cpp`) that pinned every
  bare local to `(float, Domain::Element)`. That silently defeated **step 4's rate inference** — nothing
  was ever hoisted, `ir.prologue` was empty for every program, and a frame variable left in the element
  loop read `0.0` instead of erroring. It also killed **step 3's** local type inference:
  `c = vec3(1,0,0)` was rejected as "cannot assign vec3 to float".
- Step 6's cycle checker used one graph node per assign-target *name*, so `P.y += sin(P.x) * 2.0` was
  rejected as a delay-free cycle. That is the canonical element idiom — the Field Element node's own
  default program and 2 of its 5 presets did not compile.
- Step 5's harness dereferenced a null `Find()` and **segfaulted**, so 4 of its 6 sections had never
  executed even once.
- Step 4's element backend shipped **two near-duplicate opcode interpreters** (a prologue switch and an
  element-loop switch). Unhandled opcodes fell through `default:` and left the register at `0.0` with no
  error. Frame-hoisted `-t`, `t > 1.0`, `if(t > 1.0, a, b)`, `&&`, `||`, `!` and `for` loops all
  evaluated to zero, silently.

None of this was caught by the step that caused it, and step 4's harness was registered in **no tier** of
the hygiene driver, so nothing ever ran it. Four rules follow.

### 0.1 One step, one branch, one commit

```bash
git checkout -b feature/field-step-NN-slug   # off the previous step's branch, before writing any code
```

Commit on that branch when the step's harness passes. **Never** leave two steps uncommitted in one tree —
that is what made the failures above unattributable. The chain today is
`main → feature/field-step-01-expression-ir → …-02-pure-randomness → …-03-vectors-and-rank → …`.

### 0.2 Run every Field harness, not only your own

Your step is not done when your fixture passes. It is done when **all** of them pass:

```bash
cmake --build build -j8
for v in FIELDTEST FIELDELEMENTTEST FIELDPARAMTEST FIELDSTATETEST NEWFIXTURES; do
  echo "== $v"; env INFINITE_$v=1 ./build/Infinite.app/Contents/MacOS/Infinite | tail -20
done
```

(replace `NEWFIXTURES` with every `INFINITE_FIELD*` fixture that exists by the time you run this —
`grep -o 'INFINITE_FIELD[A-Z0-9]*' src/main.cpp | sort -u`.)

A FAIL in an **earlier** step's harness is a regression in **your** change, not a pre-existing problem to
report and move past. `INFINITE_FIELDTEST` section A must stay at 170/170; if a corpus golden value
changes, that is a bug in your change — **do not re-baseline the corpus.**

### 0.3 Register your fixture in the hygiene driver

Add every fixture you create to `TIER1_CHECKS` in
`.claude/skills/run-infinite-hygiene/driver.sh` (the Field fixtures are headless early-exit runs costing
~1s each, so tier 1 is correct). A fixture that is not in the driver does not exist — that is exactly how
step 4's regression survived.

### 0.4 Never write an opcode switch with a silent `default:`

If this step adds a backend or an interpreter: **one** implementation per opcode, shared across every
register bank or execution context, and an unhandled opcode must be **loud** (an `outError` or an assert),
never a zero-valued fallthrough. Two switches over the same opcode enum will drift, and the drift is
invisible because the wrong answer is a plausible number. Same rule for a silent value fallback: an
unresolved name is a compile error with a source span, not a runtime `0.0`.

---

## 1. Invariants (restated verbatim — do not paraphrase, do not skip)

These are copied from the Field brief and the earlier steps. They are restated in full because you are
reading this file in a fresh session with no other context.

| # | Invariant | Why it exists |
|---|---|---|
| I1 | **CLEAN ROOM. You must NEVER open, read, `cat`, `grep`, or otherwise look at the source of Kronos, Cmajor, SuperCollider, or anything under `/Users/namansoni/BespokeSynth`.** All of them are GPL. Infinite is MIT. Cite the *papers* freely (Norilo, "Kronos: A Declarative Metaprogramming Language for Digital Signal Processing", Computer Music Journal 39:4, 2015). Never read their code. | A single GPL read event contaminates the provenance of an MIT codebase. This is a licensing invariant, not a style preference. |
| I2 | **No sigils. Bare names only: `P`, `N`, `uv`, `t`, `col`. Never `@P`, never `$t`, never `#N`.** | Owner decision, brief §7. Field reads as maths, not as a shell script. |
| I3 | **Rate is inferred, never declared.** There is no `@pixel` annotation, no `rate:` keyword, no `.pixel` suffix. The domain of every node falls out of the dataflow fixpoint in step 4. If you find yourself adding a syntax for "this is a pixel expression", you have broken the language. | The whole performance payoff is hoisting: coarser-domain subexpressions get evaluated once as a prologue. A declared rate makes hoisting the user's job. |
| I4 | **The typed IR is the durable asset.** GLSL is one backend of at least three (GLSL now, C++/WASM later). Nothing backend-specific may leak into the IR: no `SamplerNode` opcode, no `vec4`-because-GLSL-likes-it, no GLSL keyword avoidance in the IR's own naming. | Retargetability. If the IR knows about GLSL, the second backend is a rewrite. |
| I5 | **If a skill file contradicts the real code, the CODE WINS.** Record the discrepancy in this document (§3.3 already has a table — append to it) rather than silently following either one. | The skills were written ahead of the code and have drifted. §3.3 lists 14 already-confirmed drifts. |
| I6 | **Style of anything you write (code comments, docs, error strings): tables, bullets, wrong/right pairs, worked examples. Not prose paragraphs.** | House style across `docs/plans/`. |
| I7 | Steps 2–10 are **additive**. You add files under `src/core/field/`. You do not restructure `GLUtil`, `INode`, `Expression`, or `main.cpp`'s existing behaviour. | Every step must be independently revertable. |

---

## 2. Goal

Add a GLSL backend to the Field compiler: given the typed IR produced by step 1 (with domains assigned by
step 4, params bound by step 5, and state cells declared by step 6), emit a single `#version 150` fragment
shader whose body computes the kernel once per pixel, compile it through the existing
`GLUtil::CompileProgram` (`src/core/GLUtil.h:34`), and run it through the existing `GLUtil::RunShaderPass`
(`src/core/GLUtil.h:38`) into a node-owned FBO. Frame-domain and coarser subexpressions are **not** emitted
into the shader at all — step 4's hoisting already evaluated them on the CPU, and they arrive as uniforms.
Pixel-domain `state` cells become ping-pong texture pairs. A shader that fails to compile keeps the last
working program and shows the driver's error text in the node body, exactly as `FormulaNode` already does.

---

## 3. Files to read first, and why

### 3.1 Real source — read these, in this order

| Path | Read for | Why you specifically need it |
|---|---|---|
| `src/core/GLUtil.h` (whole file, 80-ish lines) | `Fbo` (`:13`), `EnsureFbo` (`:25`), `FboTexture` (`:28`), `CompileProgram` (`:34`), `RunShaderPass` (`:38`), `ReadTexturePixels` (`:59`) | This is your entire GPU surface. You call these five functions and add nothing to this file. The header comment at `:10` states the target: "GLSL `#version 150` shaders and a shared fullscreen-quad draw." |
| `src/core/GLUtil.cpp:100–145` (`EnsureFbo`) | `GLenum format = GL_RGBA;` (`:120`), `GLenum type = (internalFormat == GL_RGBA16F) ? GL_FLOAT : GL_UNSIGNED_BYTE;` (`:121`), `GL_LINEAR` min/mag (`:128`) | **Only `GL_RGBA8` and `GL_RGBA16F` are actually supported.** Passing `GL_R32F` gives you a texture uploaded with the wrong external type. And the filter is hardcoded `GL_LINEAR`, which is why state reads must use `texelFetch`, not `texture` (§5.5). |
| `src/core/GLUtil.cpp:170–235` (`CompileProgram`) | `char log[1024]` (`:181`, `:216`), `glCreateProgram` (`:201`), `glBindAttribLocation(program, 0, "aPos")` / `(1, "aUv")` (`:202–203`) | Two consequences. (a) Error text is **truncated at 1024 bytes** — your generated shader must not be so long that the real error scrolls off. (b) `glBindAttribLocation` is how 150 gets attribute locations without `layout(location=)`; there is **no `glBindFragDataLocation` call**, which is what caps you at one color output (§5.5, trap T12). |
| `src/core/GLUtil.cpp:230–250` (`RunShaderPass`) | `glClearColor(0, 0, 0, 0)` at `:238`, then clear, then draw; saves/restores previous FBO + viewport | **It clears the destination every pass.** For a ping-pong write target that is exactly what you want (you write every pixel anyway). It also means you cannot use `RunShaderPass` for a partial/accumulating write. |
| `src/nodes/FormulaNode.h` (67 lines) | Header comment; `bool Apply();` at `:28`; `LastError()` at `:38`; `mLastError` at `:62` | The documented contract you are copying verbatim: "A failed compile keeps the last working program and surfaces the error text in the UI, so a typo never blanks the graph or crashes the app." |
| `src/nodes/FormulaNode.cpp:11–45` (preamble/epilogue), `:390–437` (`Apply`, `CookIfNeeded`) | The exact keep-last-working-program shape, and the `mProgram == 0 && mLastError.empty()` guard at `:415` | §5.6 requires byte-for-byte equivalent behaviour. The `:415` guard is the do-not-recompile-every-frame fix; omitting it is step-01 trap 7.12 all over again. |
| `src/nodes/FeedbackNodes.h:144–146` and `src/nodes/FeedbackNodes.cpp:295–370` | `GLUtil::Fbo mState[2]; GLUtil::Fbo mDisplay; int mFront = 0;`; `GL_RGBA16F` at `:303`, `:304`, `:340`, `:342`; the sim loop at `:351–369` | The **only** working ping-pong in this tree. Copy its shape. Note that `ReactionDiffusionNode` swaps **inside** a `stepsPerFrame` loop (up to 32 swaps per cook), which contradicts a skill claim — see §3.3 row 12. |
| `src/core/Expression.cpp:113–235` (`CallFunction`) | The exact CPU semantics of `mod`, `round`, `clamp`, `lerp`, `sign`, `step`, `smoothstep`, `sqrt`, `log`, `pow`, `rand`, `noise`, `sh`, `if` | **This is the specification your GLSL must match.** Every row of the §5.3 table is justified against this function. Do not match GLSL's built-ins; match *this*. |
| `src/core/INode.h` | `ParamVisitor` (`:80`, five methods at `:84–88`), `NextTextureRevision()` (`:95`), `class INode` (`:112`), `GetOutputTexture()` (`:117`), `GetOutputWidth/Height` (`:118–119`), `CookIfNeeded(int)` (`:123`), `TextureRevision()` (`:132`), `BypassSource()` (`:151`), `InputLabel` (`:154`), `VisitParams` (`:159`) | Your node subclasses `INode`. `TextureRevision()`'s default means "always changed"; none of the three feedback nodes override it, and neither should yours (a stateful node genuinely does change every frame). |
| `CMakeLists.txt:206` (`set(COMMON_SOURCES`), `:218`, `:262`, `:296` | Where to add your new `.cpp` files | The list is alphabetical-ish by directory. Add `src/core/field/*.cpp` next to `src/core/Expression.cpp` at `:218`. **Ignore the "~line 98" / "~line 54" citations in steps 4/5/8** — see §3.3 row 10. |
| `.claude/skills/run-infinite-hygiene/driver.sh:79` (`TIER1_CHECKS=(`) and `:173` (`FULL_TESTS=(`) | The real build + test invocations | §7's command block is derived from these. Do not invent a `ctest` or `make test` — **there is no test binary in this repo**; tests are `INFINITE_*` env-var fixtures against the single executable. |
| `src/main.cpp:37619` (`INFINITE_DSPTEST` dispatch), `:43318` (`INFINITE_ROUNDTRIPTEST`, fires at `frameId == 4`) | The two fixture shapes | `INFINITE_DSPTEST` exits **before `glfwInit()`** — so it cannot be used for anything that needs a GL context. Your pixel fixture must be the in-frame shape (`:43318`), not the early-exit shape. This is the single most important scheduling fact in this document. |
| `src/main.cpp:5244` (`DrawFormulaParams`), `:5258–5262` (error draw), `:10245–10252` (node-body error pill, `"[!] " + n->LastError()`) | Where compile errors surface in the UI | Your node reuses `:10245–10252` for free if it exposes a `LastError()` accessor with the same name. |

### 3.2 Skills — read these, but see §3.3 first

`field-compiler` (§3, §6.2 codegen), `field-language` (§7 branching, §12 randomness, §14 params),
`field-state` (§3 memory, §4 ping-pong), `field-realtime`, `field-testing` (§1 fixtures, §3 tolerance),
`field-integration` (§2, §8, §9), `windows-parity` (§3.9, §4), `node-ui-pillars`, `new-compositing-node` (§3).

Also read the sibling plans for house style and for the contracts you inherit:
`docs/plans/field/step-01-expression-to-ir.md` (§5.3, §5.4, §7),
`docs/plans/field/step-03-vectors-and-rank.md` (§5.1–§5.4 type lattice),
`docs/plans/field/step-04-element-domain.md` (§7's bash block is the rigour bar),
`docs/plans/field/step-05-param-declarations.md` (the uniform/param contract you extend),
`docs/plans/field/step-06-state-cells.md` (**the critical one** — §5.6 memory, its OPEN 4, and its closing
dependency note which names step 7 explicitly).

### 3.3 Skill/code discrepancies already confirmed — **the code wins in every row**

Do not "fix" these by following the skill. Follow the code. If you find a fifteenth, append it here.

| # | Skill claim | Reality in the code | What you must do |
|---|---|---|---|
| 1 | `field-compiler` §3: "GLSL 150 `highp` float" | Desktop GLSL 1.30+ **accepts precision qualifiers and ignores them**. `float` is IEEE-754 binary32, period. | Never emit `highp`/`mediump`/`lowp`. They are noise that implies a guarantee you do not have. |
| 2 | `field-compiler` §6.2: "`%` on floats → `mod()`" | **Wrong and silently wrong.** GLSL `mod(x,y)` is *floored*: `x - y*floor(x/y)`. Field's `%` is C `fmod` (`Expression.cpp:113–235`): truncated. `mod(-3,2)` = `1` in GLSL, `-1` in Field. Step-01 §7.5 already contradicts the skill. | Emit the `fld_mod` helper (§5.3). Never emit bare `mod()`. |
| 3 | `field-compiler` §6.2: "`if` → `mix()` or `?:`" | `?:` in GLSL evaluates **only one** of operands 2/3 — wrong cost model and wrong for side-effect-free-but-expensive branches. Naive `mix(b,a,step(0.5,c))` is **wrong for Field's truth rule** (any non-zero is true): `c = 0.2` is true in Field, but `step(0.5,0.2) = 0` picks `b`. | Use the **boolean-selector overload** `mix(b, a, c != 0.0)`. Justified in full in §5.4. |
| 4 | `field-state` §3: "1920×1080 → 8 MB as an RGBA32F pair" | Its own arithmetic gives **63.3 MiB** for an RGBA32F pair at 1080p. Step-06 §5.6 already corrected this: 8 MB ≈ an R16F pair, or an RGBA16F pair packing 4 cells. | Use the corrected arithmetic in §5.5. |
| 5 | `field-state` §4: "one sampler uniform per cell" | Ignores `GL_MAX_TEXTURE_IMAGE_UNITS`, whose GL 3.2 core **minimum is 16**. | Pack cells into channels and enforce the limit (§5.5, §5.7). |
| 6 | `field-language` §14 cites `ParamMailbox.h:24` for `kMaxParams` | It is **line 23**. | Cite 23. |
| 7 | `field-language` §7 cites `Modulation.h:29` for `ParamRef` | It is **line 28**. | Cite 28. |
| 8 | Several skills cite `Expression::Evaluate` call site 1 as `main.cpp:37506` | It is **37507**. | Cite 37507. |
| 9 | Steps 4/5/8 say the compiler lives at `src/field/` | Step 1 put it at **`src/core/field/`**; step 6 calls the others a drafting error. | Use `src/core/field/`. |
| 10 | Steps 4/5/8 cite CMakeLists "~line 98" / "~line 54" | `COMMON_SOURCES` starts at **206**. | Cite 206. |
| 11 | `field-testing` §1 cites `INFINITE_DSPTEST` at "~9522" | The dispatch is at **`main.cpp:37619`**. | Cite 37619. |
| 12 | `field-state` §4: "swap once per cook, after the pass" | `ReactionDiffusionNode` (`FeedbackNodes.cpp:351–369`) swaps **inside** a `stepsPerFrame` loop — up to 32 swaps per cook. | The skill's rule is right *for Field* (one kernel evaluation per frame), but state the reason rather than citing the skill: Field's pixel kernel is one evaluation per frame by definition, not a sub-stepped simulation. |
| 13 | `field-language` §12's `TimeToRand` / `Xorwise` uses `int64_t` | **GLSL 150 cannot express 64-bit integers** (they are GLSL 4.00 / `ARB_gpu_shader_int64`). No skill mentions this. | See §5.3's randomness row: `rand`/`noise`/`sh` depend only on `t`, so step 4 hoists them to CPU-computed `double` uniforms and they stay **bit-exact**. Only a genuinely pixel-varying `rand` is impossible; refuse it in v1 (OPEN-3). |
| 14 | `field-state` §4 omits that `GLUtil::EnsureFbo` supports only `GL_RGBA8` and `GL_RGBA16F` | `GLUtil.cpp:120–121`. | Choose `GL_RGBA16F`. Do not pass anything else without extending `EnsureFbo`, which is shared code (I7). |

---

## 4. Files to create / modify

### 4.1 Create

| Path | Contents | Approx. size |
|---|---|---|
| `src/core/field/GlslBackend.h` | `struct GlslEmitResult { std::string source; std::vector<UniformSlot> uniforms; std::vector<StateSlot> state; std::string error; };` and `GlslEmitResult EmitGlsl(const ir::Module& m);` | ~70 lines |
| `src/core/field/GlslBackend.cpp` | The emitter: preamble, helper prelude, SSA-to-GLSL walk, uniform declaration, epilogue | ~500 lines |
| `src/core/field/GlslHelpers.h` | `constexpr const char* kFieldHelperPrelude` — the emitted `fld_mod` / `fld_pow` / `fld_smoothstep` / `fld_clamp` source, as one string literal | ~60 lines |
| `src/core/field/PixelState.h` / `.cpp` | `class PixelStateBank` — owns `std::vector<std::array<GLUtil::Fbo,2>>`, `int mFront`, `Resize(w,h)`, `Reset()`, `Swap()`, `BindReadUnits(program)` | ~180 lines |
| `src/nodes/FieldPixelNode.h` / `.cpp` | The `INode` subclass. Owns a `PixelStateBank`, an output `GLUtil::Fbo`, `unsigned int mProgram`, `std::string mLastError`, `int mLastCookFrame` | ~300 lines |
| `docs/plans/field/step-07-notes.md` | *Optional.* Only if you discover a fifteenth discrepancy or must record an OPEN the owner has not answered | — |

### 4.2 Modify

| Path | Change | Constraint |
|---|---|---|
| `CMakeLists.txt` | Add the five new `.cpp` paths to `COMMON_SOURCES` (starts at `:206`; `src/core/Expression.cpp` is at `:218`, `src/nodes/FormulaNode.cpp` at `:262`) | Additive lines only. Do not reorder the list. |
| `src/main.cpp` | One `REGISTER_NODE(FieldPixelNode, FieldPixel, "Source")` next to `REGISTER_NODE(FormulaNode, Formula, "Source")` at `:3762`; one `INFINITE_FIELDPIXELTEST` fixture modelled on `INFINITE_ROUNDTRIPTEST` at `:43318` | Two insertions. The node-body error pill at `:10245–10252` already works if your class exposes `LastError()`. |

### 4.3 Must NOT be modified

| Path | Why |
|---|---|
| `src/core/GLUtil.h` / `.cpp` | I7. If you need `GL_RGBA32F`, or `glBindFragDataLocation` for MRT, that is an **owner-approved separate change** — raise it as an OPEN (§5.0), do not sneak it in. Everything in this step is achievable without touching it. |
| `src/core/Expression.h` / `.cpp` | It is the reference semantics **and** the shipping expression evaluator for every existing node. Changing it to make GLSL agree is backwards: GLSL must agree with it. |
| `src/nodes/FormulaNode.h` / `.cpp` | You are copying its behaviour, not refactoring it into a shared base class. A shared base is a good idea *later*; doing it here couples step 7 to a shipping node. |
| `src/core/INode.h` | No new virtuals. Everything you need already exists (`CookIfNeeded`, `GetOutputTexture`, `TextureRevision`, `VisitParams`). |
| `src/nodes/FeedbackNodes.h` / `.cpp` | Reference only. |
| `src/core/field/*` produced by steps 1–6 | You **consume** the IR. If the IR is missing something you need, that is a step-1 bug — record it, do not patch the IR in the backend. |
| `.claude/skills/**` | The skills are drifting (§3.3). Fixing them is a separate task the owner owns. |

---

## 5. Procedure

### 5.0 Phase 0 — the OPEN questions, resolved with recommendations

Three decisions belong to the owner. Each has a recommendation. **Implement the recommendation, mark it in
a code comment as `// OPEN-n: owner may overrule`, and do not block on an answer.**

| OPEN | Question | Recommendation | Why |
|---|---|---|---|
| OPEN-1 | Texture internal format for pixel `state` (step-06 §5.6 explicitly deferred this to step 7) | **`GL_RGBA16F`**, packing up to 4 scalar cells into RGBA | It is one of only two formats `EnsureFbo` supports (`GLUtil.cpp:120–121`), it is what all three existing feedback nodes use (`FeedbackNodes.cpp:303, 304, 340, 342`), and it lands on the brief's ~8 MB-per-cell figure. **Recorded cost:** a 10-bit mantissa (≈3 decimal digits). A long-running integrator (`state x = x + 0.001`) visibly stalls once `x` exceeds ~2048, because `0.001` falls below one ulp. Document this in the node's tooltip. |
| OPEN-2 | Max pixel-domain state cells in v1 | **4 scalar cells (one RGBA16F ping-pong pair). Refuse the 5th at compile time** with `"pixel state: 4 cells max in this build (one RGBA16F ping-pong pair); GLUtil::RunShaderPass binds a single color attachment"` | See trap T12. Writing N textures in one pass needs MRT: `glDrawBuffers` + N `out` variables + **`glBindFragDataLocation`**, because `layout(location=)` on fragment outputs is GLSL 330, not 150. `GLUtil::CompileProgram` binds only `aPos`/`aUv` (`GLUtil.cpp:202–203`) and never calls `glBindFragDataLocation`, so with 2+ outputs the linker assigns locations arbitrarily. The alternative — one full pass per state texture — multiplies the kernel cost by N. |
| OPEN-3 | Pixel-varying `rand` / `noise` / `sh` | **Refuse in v1** with `"rand/noise/sh cannot vary per pixel in this build: the reference generator uses int64 arithmetic, which GLSL 150 cannot express (64-bit integers are GLSL 4.00)"` | See §5.3's randomness row. The common case — `rand` driven by `t` alone — is **hoisted to the CPU by step 4 and is bit-exact**, so this refusal costs almost nothing. A float-hash substitute would be *silently different* from the CPU VM, which is worse than a clear refusal. |

### 5.1 Phase 1 — pin `#version 150` and know exactly what it gives you

`src/` contains **57 occurrences of `#version 150` and zero of any other `#version`.** Verified:

```
grep -rhno "#version [0-9]*" src/ | sed 's/^[0-9]*://' | sort | uniq -c
#  ->  57 #version 150
```

(The 15 repo-wide hits for `#version 330` are all inside `.claude/worktrees/` copies and vendored
`external/` dependencies, not in `src/`.) The shared vertex shader `kVertSrc` in `GLUtil.cpp` is
`#version 150` with `in vec2 aPos; in vec2 aUv; out vec2 vUv;`. **Emit `#version 150` and nothing else.**

`windows-parity` mentions "GLSL 330 strictness". Read it carefully: it is about *literal typing*, *exact
uniform type matching*, and *sequential texture units* — habits that keep a shader portable across strict
drivers. It is **not** an instruction to emit `#version 330`. Do not upgrade the version.

**What GLSL 150 HAS (verified against the GLSL 1.50 spec — you may rely on all of these):**

| Feature | Since | Note |
|---|---|---|
| `texture(sampler2D, vec2)` | 1.30 | The overloaded form. `texture2D()` was **removed** in core. |
| `texelFetch(sampler2D, ivec2, int lod)` | 1.30 | Integer, unfiltered, unnormalised. **This is how you read state.** |
| `textureSize(sampler2D, int lod)` → `ivec2` | 1.30 | Lets you avoid a resolution uniform if you prefer. |
| `mix(genType x, genType y, genBType a)` — the **boolean-selector overload** | 1.30 | Distinct from the float `mix`. A true per-component select. **This is your `if`.** |
| `trunc`, `round`, `roundEven`, `modf`, `isnan`, `isinf` | 1.30 | `round()` exists but is **implementation-defined at exactly .5** — never emit it (trap T3). `roundEven` is well-defined but is banker's rounding, which is not Field's rule either. |
| Integer and bitwise ops on `int`/`uint`, `<<`, `>>`, `&`, `\|`, `^`, `~` | 1.30 | 32-bit only. |
| `switch` statements, `for`/`while` with dynamic bounds | 1.30 | You will not need them; the IR is a DAG. |
| User-declared `out vec4` fragment outputs | 1.30 | `gl_FragColor` was **removed** in core. |
| Uniform blocks (`layout(std140) uniform Foo { ... };`) | 1.40 | Available, but §5.7 recommends plain uniforms. |
| `inverse()` on `mat2/3/4` | 1.50 | |
| `gl_FragCoord`, `gl_FrontFacing`, `gl_PointCoord` | 1.10 | `gl_FragCoord.xy` is pixel-centre: integer pixel + 0.5. |

**What GLSL 150 FORBIDS (emitting any of these is a hard compile error):**

| Feature | Requires | Consequence for you |
|---|---|---|
| `layout(location = N)` on vertex attributes **or** fragment outputs | 330 | Attributes are handled by `glBindAttribLocation` (`GLUtil.cpp:202–203`). Fragment outputs are **not** handled at all → single output only (OPEN-2, T12). |
| `layout(location = N)` on uniforms | 430 | Always `glGetUniformLocation` by name. |
| `intBitsToFloat`, `floatBitsToInt`, `packUnorm2x16` | 330 | **No clean way to construct a NaN constant.** Use `0.0/0.0` and record it (§5.3, `fld_pow`). |
| `fma`, `frexp`, `ldexp`, `textureGather`, `bitfieldExtract`, `findMSB` | 400 | Do not use. |
| `double`, `dvec2/3/4`, and all double-precision maths | 400 | **This is why §5.9 exists.** The CPU VM is `double`; you get `float`. |
| 64-bit integers (`int64_t`, `uint64_t`) | 400 / `ARB_gpu_shader_int64` | **This is why OPEN-3 exists.** Step 2's `Xorwise`/`TimeToRand` cannot be transliterated. |
| `packHalf2x16` | 410 | |
| `imageLoad` / `imageStore` | 420 | No scatter writes. Ping-pong is the only state mechanism. |
| SSBOs, compute shaders, `atomicAdd` | 430 | No reductions inside the shader. Anything that needs a reduction is not a pixel-domain kernel. |
| `precision highp float;` as a *guarantee* | never (desktop) | It parses and is ignored. See §3.3 row 1. |

### 5.2 Phase 2 — the emitted shader skeleton

Every emitted shader has exactly this shape. Nothing varies except the marked regions.

```glsl
#version 150

in  vec2 vUv;
out vec4 fragColor;

// ---- fixed internals (always emitted, even if unused; the compiler DCEs them)
uniform vec2      fld_res;      // output resolution in pixels
uniform float     fld_t;        // transport seconds, from the frame domain
uniform float     fld_dt;       // seconds since previous cook
uniform int       fld_frame;    // monotonic frame id
uniform sampler2D fld_srcTex;   // input image, or a 1x1 black texture when unconnected
uniform float     fld_srcAlpha; // 1.0 when connected, 0.0 when not

// ---- helper prelude (kFieldHelperPrelude, verbatim, always emitted) ----
float fld_mod(float x, float y)        { return x - y * trunc(x / y); }
float fld_clamp(float x, float a, float b) { return min(max(x, a), b); }
float fld_lerp(float a, float b, float t)  { return a + (b - a) * t; }
float fld_smoothstep(float e0, float e1, float x) {
   if (e0 == e1) return (x < e0) ? 0.0 : 1.0;      // matches Expression.cpp's explicit guard
   float u = fld_clamp((x - e0) / (e1 - e0), 0.0, 1.0);
   return u * u * (3.0 - 2.0 * u);
}
float fld_pow(float b, float e) {
   if (b >= 0.0)           return pow(b, e);
   if (e == trunc(e))      return (fld_mod(e, 2.0) == 0.0) ? pow(-b, e) : -pow(-b, e);
   return 0.0 / 0.0;                                // NaN; GLSL 150 has no intBitsToFloat
}

// ---- hoisted frame-domain values, as uniforms (emitted, count varies) ----
uniform float fld_h0;   // e.g. sin(t * 2.0), computed once per frame on the CPU
uniform float fld_h1;

// ---- step-5 params (emitted, count varies) ----
uniform float fld_p_speed;
uniform vec3  fld_p_tint;

// ---- pixel state read samplers (emitted, 0 or 1 in v1) ----
uniform sampler2D fld_s_bank0;

void main()
{
   // ---- reserved-name bindings (emitted only when the kernel uses them) ----
   vec2  uv  = vUv;
   vec2  xy  = gl_FragCoord.xy;
   vec2  res = fld_res;
   vec4  src = texture(fld_srcTex, vUv);
   vec3  col = src.rgb;

   // ---- state loads (emitted per declared cell) ----
   vec4  fld_st0 = texelFetch(fld_s_bank0, ivec2(gl_FragCoord.xy), 0);
   float fld_v_phase = fld_st0.r;

   // ---- SSA body: one `<type> fld_tN = <expr>;` line per IR node ----
   float fld_t0 = fld_v_phase + fld_p_speed * fld_dt;
   float fld_t1 = fld_mod(fld_t0, 1.0);

   // ---- writes ----
   fragColor = vec4(vec3(fld_t1), src.a * fld_srcAlpha);
}
```

Rules for the skeleton:

| Rule | Wrong | Right |
|---|---|---|
| One SSA line per IR node, in topological order. No nested expressions. | `fragColor = vec4(vec3(fld_mod(a+b*c, 1.0)), 1.0);` | `float fld_t0 = b * c; float fld_t1 = a + fld_t0; float fld_t2 = fld_mod(fld_t1, 1.0);` |
| One statement per line, `\n`-terminated. | Compact multi-statement lines | Line N of the generated source maps to IR node N-K for a fixed K, so the driver's `0(37) : error` maps back to an IR node. |
| Always emit the full helper prelude, even when unused | Conditionally emitting helpers | The prelude is ~20 lines; the driver dead-strips unused functions. Conditional emission means the generated source differs between two kernels that should differ only in the body, which makes golden-file diffs useless. |
| Never emit a bare integer where a float is expected | `pow(x, 2)` | `pow(x, 2.0)` — `windows-parity` §3.9. Strict drivers reject the implicit int→float conversion in an overload-resolution context. |
| Never emit an identifier containing `__`, and never one starting with `gl_` | `fld__t0`, `gl_myVar` | Both are **reserved by the GLSL spec** and may be rejected. |

### 5.3 Phase 3 — the IR → GLSL emission table

**The specification is `src/core/Expression.cpp:113–235` (`CallFunction`), not the GLSL built-in library.**
Where GLSL happens to agree, emit the built-in. Where it does not, emit a helper. There is no third option:
"close enough" is how you ship a kernel that looks right and is wrong at the edges.

#### 5.3.1 Direct 1:1 — emit the GLSL built-in

| IR opcode | GLSL | Verified against |
|---|---|---|
| `add` `sub` `mul` `neg` | `a + b`, `a - b`, `a * b`, `-a` | Correctly rounded in both. |
| `div` | `a / b` | See §5.3.4 for divide-by-zero divergence. |
| `sin` `cos` `tan` | `sin(x)` `cos(x)` `tan(x)` | Same function, **very different accuracy** — §5.9. |
| `asin` `acos` `atan` | `asin` `acos` `atan(x)` | |
| `atan2(y,x)` | `atan(y, x)` | **Argument order is (y, x) in both.** Do not swap. |
| `exp` `log2` `exp2` | `exp` `log2` `exp2` | |
| `abs` `floor` `ceil` `fract` | `abs` `floor` `ceil` `fract` | `fract(x) == x - floor(x)` in both. |
| `min` `max` | `min` `max` | |
| `sign` | `sign(x)` | `Expression.cpp`: `>0 ? 1 : (<0 ? -1 : 0)`. GLSL: identical, including `sign(0.0) == 0.0` and `sign(-0.0) == 0.0`. **Verified equivalent — emit the built-in.** |
| `step(edge, x)` | `step(edge, x)` | `Expression.cpp`: `a[1] < a[0] ? 0 : 1` where `a[0]` is edge. GLSL: `x < edge ? 0.0 : 1.0`. **Identical, including argument order (edge first).** Emit the built-in. |
| `sqrt` | `sqrt(x)` | Same for `x >= 0`. See §5.3.4 for `x < 0`. |
| `length` `dot` `cross` `normalize` `distance` | same names | Rank rules come from step 3. |

#### 5.3.2 No direct equivalent — **you MUST emit a helper**

Each row states the bug that emitting the built-in would cause.

| IR opcode | ❌ Wrong emission | ✅ Right emission | The bug you avoid |
|---|---|---|---|
| `mod` (and the `%` operator) | `mod(x, y)` | `fld_mod(x, y)` = `x - y * trunc(x/y)` | **GLSL `mod` is *floored*; Field's is C `fmod`, which is *truncated*. They differ in sign for every negative operand.** `mod(-3.0, 2.0)`: GLSL gives `1.0`, Field gives `-1.0`. A phase wrap or a tiling kernel is off by a full period on half the plane, and it looks plausible. This is the single most likely silent bug in the whole backend. |
| `round` | `round(x)` | `floor(x + 0.5)` | `Expression.cpp` is literally `floor(a[0] + 0.5)`. GLSL's `round()` is **implementation-defined at exactly ±0.5** — the spec permits either neighbour, so `round(0.5)` may be `0.0` on one GPU and `1.0` on another. `roundEven` is well-defined but is banker's rounding (`roundEven(0.5) == 0.0`), which is also not Field's rule. Emit `floor(x + 0.5)`. |
| `pow`, the `^` operator | `pow(b, e)` | `fld_pow(b, e)` | **GLSL `pow(x,y)` is undefined for `x < 0`** (and for `x == 0 && y <= 0`). `std::pow` handles a negative base with an integral exponent: `pow(-2.0, 3.0) == -8.0`. Bare `pow` gives you NaN or garbage there. `fld_pow` reproduces the C behaviour. |
| `clamp` | `clamp(x, lo, hi)` | `fld_clamp(x, lo, hi)` = `min(max(x, lo), hi)` | `Expression.cpp` is `min(max(x,lo),hi)`, which for `lo > hi` returns `hi`. **GLSL `clamp` is explicitly undefined when `minVal > maxVal`.** A user writing `clamp(x, 1.0, 0.0)` (a plausible typo, and a legitimate idiom for "invert then clamp") gets defined behaviour on the CPU and undefined behaviour on the GPU. |
| `lerp` | `mix(a, b, t)` | `fld_lerp(a, b, t)` = `a + (b - a) * t` | `Expression.cpp` is `a[0] + (a[1]-a[0])*a[2]`. GLSL `mix` is specified as `x*(1-a) + y*a`. These are algebraically equal and **numerically different**: at `t == 1.0`, `fld_lerp` returns exactly `b`, while `x*(1-a)+y*a` returns `x*0 + y*1`, which is exactly `b` too — but at `t == 0.5` with widely separated `a`/`b` the roundings differ by an ulp, and on a hardware `fma` path the difference is larger. Emit the CPU's algebra. |
| `smoothstep` | `smoothstep(e0, e1, x)` | `fld_smoothstep(e0, e1, x)` | **`Expression.cpp` has an explicit `e0 == e1` guard**; GLSL's `smoothstep` is undefined for `e0 >= e1`. This is not hypothetical: `FormulaNode`'s own **default preset** uses `smoothstep(0.005, -0.005, d)` — reversed edges — so reversed-edge smoothstep is a live idiom in this codebase that currently relies on undefined behaviour. `fld_smoothstep` makes the Field version defined and CPU-matching. Argument order `(edge0, edge1, x)` is the same in both — do not reorder. |
| `if(c, a, b)` | `mix(b, a, step(0.5, c))` **or** `c != 0.0 ? a : b` | `mix(b, a, c != 0.0)` — the **bool-selector overload** | See §5.4 for the full argument. Short version: `step(0.5, c)` implements "c > 0.5 is true", but **Field's rule is "any non-zero is true"** (`Expression.cpp`: `a[0] != 0.0 ? a[1] : a[2]`), so `c = 0.2` selects the wrong branch. And the float `mix` is arithmetic, so `inf * 0.0 == NaN` poisons the unselected branch into the result. |
| `rand`, `noise`, `sh` | any float-hash substitute | **nothing** — refuse at compile time (OPEN-3) | The reference generator (`Expression.cpp`, and step 2's `Xorwise`/`TimeToRand`) uses `int64_t`; **GLSL 150 has no 64-bit integers.** In the overwhelmingly common case these depend only on `t` → they are frame-domain → **step 4 hoists them to a CPU `double` computation delivered as a uniform, and they are bit-exact.** Only a genuinely pixel-varying `rand` is impossible; refuse it with a message that names the int64 reason. A silently-different float hash is far worse than a clear refusal. |

`fld_mod` uses `trunc`, which is GLSL **1.30** — available in 150. Confirmed.

#### 5.3.3 Vector/rank forms

Step 3 gives every IR node a rank. The helpers above are declared for `float` only. For rank > 1:

| Approach | Verdict |
|---|---|
| Emit `vec2`/`vec3`/`vec4` overloads of every helper in the prelude | ✅ **Do this.** GLSL supports overloading; the prelude grows from ~20 to ~60 lines; the driver strips the unused ones. Componentwise bodies (`return x - y * trunc(x/y);`) work unchanged for vector types because `trunc`, `min`, `max`, `pow` are all genType. |
| Emit per-component scalar calls and reassemble with a constructor | ❌ 4× the SSA lines, unreadable generated source, and the diff against a golden file becomes noise. |
| Emit only `float` and let GLSL's implicit conversions handle it | ❌ There is no scalar→vector implicit conversion for arguments in GLSL. Hard compile error. |

`fld_smoothstep`'s `if (e0 == e1)` does not vectorise — for the vector overloads write it as
`mix(hermite, stepFallback, equal(e0, e1))` using the bool-vector `mix` overload.

#### 5.3.4 Divergences you cannot remove — document them, do not paper over them

The CPU VM raises a **hard evaluation error** for these. A fragment shader cannot raise anything.

| Case | CPU VM (`Expression.cpp`) | GLSL 150 | Policy |
|---|---|---|---|
| `x / 0.0` | error | `±inf` (IEEE) | If both operands are compile-time constants, the IR's constant folder (step 1) already errors **before** the backend runs. A runtime pixel divide-by-zero produces `inf`, which propagates to `fragColor` and clamps to white in an RGBA8 output. Document; do not guard (a per-op guard costs a branch on every division). |
| `fld_mod(x, 0.0)` | error | `NaN` (`trunc(x/0)` is `±inf`, `0 * inf` is NaN) | Same policy. |
| `sqrt(x)` for `x < 0` | error: `"sqrt of negative"` | undefined per spec; NaN in practice | Same policy. |
| `log(x)` for `x <= 0` | error: `"log of non-positive"` | undefined per spec | Same policy. |
| `fld_pow(neg, non-integer)` | `std::pow` returns NaN | `0.0 / 0.0` | Explicitly emitted as NaN. Recorded here because `0.0/0.0` is the only NaN construction available in 150. |

**Write these five rows into the generated shader as a leading comment block.** When a user reports "my
kernel goes white at the origin", the shader source they can copy out of the node already explains it.

### 5.4 Phase 4 — branching lowered to predication

**Branching is ALLOWED in Field.** Brief §7, owner decision. This section is not a ban; it is the
specification of how a branch *lowers* on the GPU, and the cost model the user must be shown.

#### 5.4.1 Why predication, and why not `?:`

| Lowering | Semantics | Cost | Verdict |
|---|---|---|---|
| `c != 0.0 ? a : b` | GLSL spec: **only one** of the second/third operands is evaluated. Also needs the explicit `!= 0.0` because GLSL has no implicit float→bool. | Looks like `max(cost(a), cost(b))`; on real hardware a divergent warp still runs both sides serially. | ❌ **Reject.** It advertises a cost model that the hardware does not honour, and it does not match Field's non-short-circuiting `if`, where both arguments are already evaluated before the call (`Expression.cpp` evaluates `a[1]` and `a[2]` into the argument array before the `a[0] != 0.0` test). |
| `mix(b, a, step(0.5, c))` — the float overload | Implements **"c > 0.5 is true"**. | sum | ❌ **Wrong.** Field's truth rule is *any non-zero is true*. `if(0.2, a, b)` returns `a` on the CPU and `b` here. Also arithmetic: if the unselected branch is `inf` or `NaN`, `inf * 0.0 == NaN` poisons the result. |
| `mix(b, a, c != 0.0)` — the **bool-selector overload**, `genType mix(genType, genType, genBType)`, GLSL **1.30**, available in 150 | A true per-component **select**, not an arithmetic blend. Matches "any non-zero is true" exactly. An unselected `inf`/`NaN` is discarded, not multiplied. | sum of both sides — which is the truth | ✅ **Use this.** |

Note the argument order carefully: `mix(x, y, a)` returns `x` when `a` is `false` and `y` when `a` is
`true`. Field's `if(c, a, b)` returns `a` when true. Therefore **`mix(b, a, cond)`** — the false-branch
first. Getting this backwards inverts every conditional in the language and is trivially caught by the
fixture in §7.

```
IR:    if(c, a, b)
GLSL:  float fld_t7 = mix(fld_t6 /*b*/, fld_t5 /*a*/, fld_t4 != 0.0);
```

For rank > 1 with a scalar condition, broadcast: `mix(b, a, bvec3(c != 0.0))`.

#### 5.4.2 The GPU cost model — **cost is the SUM of both sides, never the max**

A fragment shader executes in SIMD lockstep across a warp/wavefront (32 or 64 pixels). Every pixel in the
group executes every instruction; the ones on the untaken side have their writes masked. So a branch does
not choose work — **it does both jobs and throws one away.**

| ❌ Wrong mental model | ✅ Right mental model |
|---|---|
| "`if(c, cheap, expensive)` costs `cheap` most of the time, because most pixels take the cheap side." | "`if(c, cheap, expensive)` costs `cheap + expensive` on **every** pixel, always." |
| "Nesting branches is fine, the tree prunes." | "Nesting branches **multiplies**. Three nested binary `if`s = 8 leaves = 8× the leaf cost, on every pixel." |
| "A branch that is uniform across the frame is still a branch." | A condition that is *frame-domain* is **hoisted by step 4** and never reaches the shader — the CPU picks the side and only the winner is emitted. This is the escape hatch, and it is free because rate is inferred (I3). |
| "The compiler will hoist the expensive side out." | It cannot. Both sides depend on pixel data by construction, or they would already have been hoisted. |

Worked example — the same kernel, two ways:

```
# 1 --- pixel-domain condition. Cost = raymarch + fbm, on every pixel, always.
d = length(uv - 0.5)
col = if(d < 0.3, raymarch(uv), fbm(uv, 8))

# 2 --- frame-domain condition. Step 4 hoists `mode`; only one side is emitted.
mode = floor(t / 4.0) % 2        # depends only on t -> frame domain
col = if(mode, raymarch(uv), fbm(uv, 8))
```

Emitted GLSL for case 2 contains **no `mix` at all** — the CPU evaluated `mode` in the prologue and the
backend emitted only the selected subtree. That is the payoff of I3 (inferred rate), and it is why case 1
and case 2 have wildly different costs despite looking almost identical.

**Requirement:** the node must surface the branch count in its UI. Emit
`"n branches -> 2^n paths evaluated per pixel"` into the node body (reusing the same draw path as the error
pill at `main.cpp:10245–10252`) whenever the emitted shader contains one or more pixel-domain `mix` selects.
A user who writes five nested `if`s and drops to 4 fps must be able to see why without a profiler.

### 5.5 Phase 5 — pixel-domain `state` as a ping-pong texture pair

Step 6 defined `state` cells and their reset contract on the CPU. Pixel-domain `state` means **one value per
pixel per cell**, which is a texture. You cannot read and write the same texture in one pass, so you need
two and you alternate.

#### 5.5.1 The memory arithmetic — do this honestly

At 1920 × 1080 = **2,073,600 pixels**:

| Format | Bytes/px | One texture | A **pair** | Scalar cells it holds | Bytes per cell (pair) |
|---|---|---|---|---|---|
| `GL_R16F` | 2 | 3.96 MiB | **7.91 MiB** | 1 | 7.91 MiB |
| `GL_R32F` | 4 | 7.91 MiB | 15.82 MiB | 1 | 15.82 MiB |
| `GL_RGBA16F` | 8 | 15.82 MiB | **31.64 MiB** | **4** | **7.91 MiB** ✅ |
| `GL_RGBA32F` | 16 | 31.64 MiB | 63.28 MiB | 4 | 15.82 MiB |
| `GL_RGBA8` | 4 | 7.91 MiB | 15.82 MiB | 4 (but 8-bit, unusable for state) | 3.96 MiB |

So the brief's "≈ 8 MB per texture" figure is right **only** for an R16F pair or an RGBA16F pair carrying
four cells. `field-state` §3's "1920×1080 → 8 MB as an RGBA32F pair" is off by a factor of 8 (§3.3 row 4).

`EnsureFbo` supports only `GL_RGBA8` and `GL_RGBA16F` (`GLUtil.cpp:120–121`), which settles OPEN-1:
**`GL_RGBA16F`, four scalar cells per pair, 31.64 MiB per pair at 1080p, 7.91 MiB amortised per cell.**

Cell → channel mapping is by declaration order: cell 0 → `.r`, cell 1 → `.g`, cell 2 → `.b`, cell 3 → `.a`.
Emit the mapping as a comment in the generated shader.

#### 5.5.2 The lifecycle, precisely

```
PixelStateBank owns:  GLUtil::Fbo mPair[2];   int mFront = 0;   int mW = 0, mH = 0;   bool mNeedsClear = true;

per cook (frameId changed):
  1. EnsureFbo(mPair[0], w, h, GL_RGBA16F)      // both, every cook; early-returns when unchanged
     EnsureFbo(mPair[1], w, h, GL_RGBA16F)
  2. if (mNeedsClear) { clear BOTH to 0; mNeedsClear = false; }
  3. read  = mPair[mFront]
     write = mPair[1 - mFront]
  4. bind read.tex to texture unit 0 as `fld_s_bank0`
  5. GLUtil::RunShaderPass(write, mProgram, setupUniforms)   // clears write to (0,0,0,0), then draws
  6. mFront = 1 - mFront;                                    // <-- THE SWAP, exactly here
  7. the node's output texture is FboTexture(mPair[mFront])  // i.e. what was just written
```

| Question | Answer | Why |
|---|---|---|
| **When exactly does the swap happen?** | Immediately **after** `RunShaderPass` returns, once per cook. Never before. Never twice. | Before the pass, you would read the texture you are about to write into — the read is undefined and on some drivers is a hazard. Twice per cook, and every second frame's result is discarded. |
| **Why once, not in a sub-step loop?** | Field's pixel kernel is **one evaluation per element per frame**, by definition of the domain. There is no `stepsPerFrame`. | `ReactionDiffusionNode` (`FeedbackNodes.cpp:351–369`) *does* loop and swap up to 32×, but that is a hand-written simulation with its own sub-stepping. Field has no such concept. (`field-state` §4 gets the rule right but for the wrong reason — §3.3 row 12.) |
| **What does the node output?** | `FboTexture(mPair[mFront])` *after* the swap = the texture just written. | If you output the pre-swap front, you display last frame's state and the whole graph is one frame behind. |
| **First frame / after a clear?** | Both textures cleared to `vec4(0)`, so `texelFetch` returns `(0,0,0,0)` and every cell reads as `0.0`. | Matches step 6's CPU contract: a `state` cell's initial value is its declared initialiser, and `0.0` when undeclared. **If the cell has a non-zero initialiser, clear to that value** — run one extra `RunShaderPass` with a trivial constant-write program, or `glClearColor(init.r, init.g, init.b, init.a)` on your own bind. Do not "just add the initialiser in the kernel on frame 0" — that needs a per-frame branch. |
| **Resize?** | `EnsureFbo` reallocates when `w`/`h`/`format` change, which **destroys the contents**. Therefore: detect the size change yourself (`mW != w \|\| mH != h`), set `mNeedsClear = true`, and reset. | Silently keeping garbage from a stale allocation is worse than a visible reset. Do **not** attempt to rescale state across a resize — a bilinear resample of an integrator's accumulator is meaningless. Say so in the tooltip: *"resizing resets pixel state."* |
| **How does this satisfy step 6's reset-on-seek/loop/stop contract?** | Step 6 requires every `state` cell to reset to its initialiser on transport **seek**, **loop wrap**, and **stop**. In the pixel domain, "reset" == `mNeedsClear = true` — the next cook clears both textures before the pass. Wire `PixelStateBank::Reset()` to the exact same transport callback step 6 uses for CPU cells, in the same place, so the two domains can never disagree about *when* a reset happens. | If pixel state resets on a different edge than frame state, a kernel that mixes both produces a frame of inconsistent output at every seek. One trigger, two implementations. |

#### 5.5.3 Reading state — `texelFetch`, never `texture`

```glsl
// ❌ WRONG
vec4 s = texture(fld_s_bank0, vUv);
// ✅ RIGHT
vec4 s = texelFetch(fld_s_bank0, ivec2(gl_FragCoord.xy), 0);
```

Three reasons, all load-bearing:

| # | Reason |
|---|---|
| 1 | `EnsureFbo` hardcodes `GL_LINEAR` min/mag filtering (`GLUtil.cpp:128`), and you may not modify it (I7). `texture()` therefore **bilinearly blends four neighbouring state values** into every read. An integrator built on that diffuses sideways a little every frame — it looks like a plausible blur effect and is a bug. |
| 2 | `vUv` is a normalised interpolated coordinate; mapping it back to a texel involves a half-texel offset. `ivec2(gl_FragCoord.xy)` truncates a pixel-centre coordinate (integer + 0.5) to exactly the right texel index, with no offset arithmetic and no rounding question. |
| 3 | `texelFetch` bypasses wrap mode entirely, so `GL_CLAMP_TO_EDGE` cannot silently duplicate an edge texel. |

`texelFetch` is GLSL **1.30**, available in 150. Confirmed.

**Reading a *neighbour*** (for a blur, a Laplacian, a reaction-diffusion kernel) is
`texelFetch(fld_s_bank0, clamp(ivec2(gl_FragCoord.xy) + ivec2(dx,dy), ivec2(0), textureSize(fld_s_bank0,0)-1), 0)`.
Emit the `clamp` — an out-of-range `texelFetch` is **undefined**, not clamped.

#### 5.5.4 The one-output constraint (this is why OPEN-2 caps you at 4 cells)

`GLUtil::RunShaderPass` binds a single `GLUtil::Fbo`, which has a single color texture
(`GLUtil.h:13–20`). A fragment shader can write more than one target only via MRT: an FBO with N color
attachments, a `glDrawBuffers` call, and N declared `out` variables whose locations are assigned by
`glBindFragDataLocation` — because `layout(location=)` on fragment outputs is **GLSL 330**, not 150.
`GLUtil::CompileProgram` calls `glBindAttribLocation` for `aPos`/`aUv` (`GLUtil.cpp:202–203`) and **never
calls `glBindFragDataLocation`**. With one `out`, GL binds it to location 0 automatically and everything
works. With two, the locations are linker's choice and your channels swap unpredictably between drivers.

| Option | Verdict |
|---|---|
| Pack ≤ 4 scalar cells into one RGBA16F pair; refuse the 5th | ✅ **v1.** Zero shared-code change, one pass, deterministic. |
| One full pass per state texture | ❌ N× the kernel cost for N cells, and the passes see inconsistent state (pass 2 reads what pass 1 wrote). |
| Build MRT | ❌ Requires `glBindFragDataLocation` inside `CompileProgram` — shared code, I7. Raise as a follow-up if a user actually needs 5+ cells. |

Even if MRT existed, memory bites first: 12 usable sampler units (16 minimum minus 4 reserved for input
images) × 31.64 MiB per pair = **380 MiB of state at 1080p**. The sampler limit is not the real ceiling.

### 5.6 Phase 6 — keep the last working program on compile failure

Copy `FormulaNode` exactly. Not approximately — exactly. Here is the real code.

**`src/nodes/FormulaNode.cpp:390–437`:**

```cpp
bool FormulaNode::Apply()                                    // :390
{
   std::string src = std::string(kPreamble) + formula + kEpilogue;   // :392

   std::string error;
   unsigned int program = GLUtil::CompileProgram(src.c_str(), &error);
   if (program == 0)
   {
      mLastError = error;                                    // :398
      return false;                          // <-- old mProgram left untouched
   }

   if (mProgram != 0)
      glDeleteProgram(mProgram);              // <-- only delete AFTER the new one links
   mProgram = program;
   mLastError.clear();                                       // :405
   return true;
}

void FormulaNode::CookIfNeeded(int frameId)                  // :409
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (mProgram == 0 && mLastError.empty())                  // :415  <-- the do-not-retry guard
      Apply();
   if (mProgram == 0)
      return;
   ...
   GLUtil::RunShaderPass(mOut, mProgram, [this]() {
      glUniform1f(glGetUniformLocation(mProgram, "uTime"), mClock);
      ...
   });
}
```

And the documented contract, `src/nodes/FormulaNode.h`:

> "A failed compile keeps the last working program and surfaces the error text in the UI, so a typo never
> blanks the graph or crashes the app."
> — and on `bool Apply();` at `:28`: *"Returns false and fills `LastError()` on failure, leaving the
> previously-working program active."*

#### 5.6.1 The five properties you must reproduce

| # | Property | The bug it prevents |
|---|---|---|
| 1 | Compile into a **local** `unsigned int program`. Only assign `mProgram` after `CompileProgram` returns non-zero. | Assigning first means a typo blanks the node. The user's whole composition goes black while they are mid-keystroke. |
| 2 | `glDeleteProgram(mProgram)` runs **only after** the new program links. | Deleting first and then failing leaves you with no program at all — same blanking bug, plus you have destroyed work the user cannot get back without retyping. |
| 3 | On failure, set `mLastError` and **return**. Do not clear `mProgram`. | This is the "keep last working" behaviour itself. |
| 4 | On success, `mLastError.clear()`. | A stale error pill under a now-working shader is a support ticket. |
| 5 | **`if (mProgram == 0 && mLastError.empty()) Apply();`** (`:415`) — never `if (mProgram == 0) Apply();`. | **This is the whole point.** Without `&& mLastError.empty()`, a permanently-broken shader is recompiled **every single frame**, forever. `glCompileShader` + `glLinkProgram` on a real driver is 1–50 ms. At 60 fps that is a hard freeze, not a slowdown. **This is the same bug class as step-01 trap 7.12** (recomputing a failed parse every frame). Compilation happens on the *edit* edge, not the *cook* edge. |

#### 5.6.2 Surfacing the error text

| Requirement | How |
|---|---|
| The error appears in the **node body**, not only in a params panel | `main.cpp:10245–10252` already draws `"[!] " + n->LastError()` for any node exposing `LastError()`. Give your class the identical accessor signature (`const std::string& LastError() const`) and you get this free. |
| The params panel also shows it | Model on `DrawFormulaParams` (`main.cpp:5244`), error draw at `:5258–5262`. |
| The error must reference the **user's Field source**, not the generated GLSL | The driver reports `0(37) : error C1503: undefined variable "fld_v_speed"`. Line 37 of the generated shader means nothing to the user. Keep a `std::vector<int> mLineToIrNode` built during emission (one entry per emitted line), map the reported line back to an IR node, and from there to the source span step 1 already records. Emit `"line 3, col 12: unknown name 'sped'"`. |
| Truncation | `GLUtil::CompileProgram` uses `char log[1024]` for **both** the compile and the link log (`GLUtil.cpp:181`, `:216`). A very long generated shader with many errors will have its log cut off mid-message. Do not fight this — but **do** put the mapped, human-readable line first in `mLastError`, before the raw driver text, so the useful part is never the part that gets cut. |
| Never `assert`, never `abort`, never throw out of a cook | A malformed kernel is normal user input, not a program error. |

### 5.7 Phase 7 — uniform plumbing, name mangling, and the limits

#### 5.7.1 Where uniforms come from

| Source | IR marking | Emitted as | Set where |
|---|---|---|---|
| **Hoisted frame-domain values** — any subexpression step 4 assigned to `frame` or coarser that a pixel node consumes | `domain <= frame` with at least one `pixel` consumer | `uniform float fld_h<N>;` (or `vec2/3/4` by rank) | In the `RunShaderPass` setup lambda. Value computed on the CPU as `double`, narrowed with an explicit `(float)` cast at the `glUniform1f` call. **This is the hoisting payoff made concrete** (I3). |
| **Step-5 `param` declarations** | `IrNode::Param` | `uniform float fld_p_<mangled name>;` | Same lambda, read from the step-5 param store / `ParamMailbox` (`kMaxParams` is at `ParamMailbox.h:23`, not 24 — §3.3 row 6). |
| **Fixed internals** | not in the IR | `fld_res`, `fld_t`, `fld_dt`, `fld_frame`, `fld_srcTex`, `fld_srcAlpha` | Same lambda, always. |
| **Pixel state samplers** | `IrNode::StateRead` | `uniform sampler2D fld_s_bank<N>;` | Bound to sequential texture units starting at 0 (`windows-parity` §3.9: sequential, never sparse). |

**Cache the locations.** `glGetUniformLocation` is a driver-side string lookup; calling it per uniform per
frame (which `FormulaNode` does, and gets away with because it has five) is fine at 5 and measurable at 60.
Resolve every location **once, right after a successful link**, into a `std::vector<int>` indexed by slot,
and store `-1` for "optimised out" (a legal, expected return — the driver strips uniforms the body does not
reference; setting a `-1` location is a silent no-op, which is correct, so **do not treat `-1` as an error**).

**Type exactness** (`windows-parity` §3.9): a `float` uniform is set with `glUniform1f`, a `vec3` with
`glUniform3f`, an `int` with `glUniform1i`, a `sampler2D` with `glUniform1i` (the *unit number*, not the
texture name). Setting a `vec3` with three `glUniform1f` calls, or a sampler with `glUniform1f`, is
undefined and fails on strict drivers while appearing to work on permissive ones.

#### 5.7.2 The limits — query them, do not assume them

| Limit | GL 3.2 core **minimum** | Query | What it bounds |
|---|---|---|---|
| `GL_MAX_FRAGMENT_UNIFORM_COMPONENTS` | **1024** | `glGetIntegerv` | Total floats/ints across all fragment uniforms. A `float` is 1 component, a `vec3` is 3 (padding may make it 4 — assume 4), a `mat4` is 16. 1024 components ≈ 256 `vec4`s. In practice you will never approach this with scalar params. |
| `GL_MAX_TEXTURE_IMAGE_UNITS` | **16** | `glGetIntegerv` | Samplers visible to the fragment stage. **This is the real ceiling**, and OPEN-2 already caps you far below it. |
| `GL_MAX_VARYING_COMPONENTS` | 60 | `glGetIntegerv` | Irrelevant — you inherit exactly one varying, `vUv`, from the shared vertex shader. |

Query both at first successful compile, cache them, and **refuse over-budget kernels at compile time with a
message that names the queried number**, e.g.
`"kernel needs 19 texture samplers; this GL context reports GL_MAX_TEXTURE_IMAGE_UNITS = 16"`.
A refusal that quotes the real limit is debuggable; `"too many uniforms"` is not.

Do **not** hardcode 1024 or 16. They are floors, not values; a modern desktop GPU reports far more, and a
software or remote-desktop context may report exactly the floor.

#### 5.7.3 Name mangling — every identifier, every time

**The rule: no user identifier ever appears in the generated GLSL unmangled.** Not even a "safe-looking"
one. Mangle by namespace:

| Field construct | Mangled form | Example |
|---|---|---|
| Local binding / let | `fld_v_<name>` | `phase` → `fld_v_phase` |
| `param` (step 5) | `fld_p_<name>` | `speed` → `fld_p_speed` |
| State cell sampler | `fld_s_bank<N>` | first pair → `fld_s_bank0` |
| State cell value after unpack | `fld_v_<name>` | (a state cell reads as an ordinary local) |
| SSA temporary | `fld_t<N>` | `fld_t0`, `fld_t1`, … |
| Hoisted uniform | `fld_h<N>` | `fld_h0` |
| Emitted helper | `fld_<op>` | `fld_mod`, `fld_pow` |
| Fixed internal | `fld_<role>` | `fld_res`, `fld_t`, `fld_dt`, `fld_frame`, `fld_srcTex`, `fld_srcAlpha` |

| Property | Why it matters |
|---|---|
| **Injective.** `fld_v_x` can only have come from local `x`; `fld_p_x` only from param `x`. Distinct namespaces cannot alias. | A user may legitimately have a local `speed` *and* a param `speed`. Without namespacing, one silently shadows the other. |
| **Collision with GLSL keywords is structurally impossible.** A user writing `float`, `discard`, `sampler2D`, `mix`, `main`, `in`, `out`, `layout`, or any of the ~80 reserved words gets `fld_v_float`, `fld_v_discard`, `fld_v_mix`. | This is the entire reason for the scheme. The alternative — maintaining a GLSL reserved-word blocklist — is a list you will get wrong, and it grows with every GLSL version. Never write that list. |
| **Collision with an emitted internal is impossible.** A user writing `res` or `t` gets `fld_v_res` / `fld_v_t`, which cannot collide with `fld_res` / `fld_t`. | Otherwise a user's `t` overwrites the transport clock. |
| **Never emit `__` anywhere in an identifier, and never a leading `gl_`.** | Both are reserved by the GLSL spec and may be rejected outright. Note this rules out `fld__t` — single underscores only. |
| Non-ASCII or otherwise non-`[A-Za-z0-9_]` characters in a Field identifier: **percent-escape them** (`é` → `_x0301_`) or refuse. | GLSL identifiers are ASCII. Do not pass a UTF-8 byte through into shader source. |
| The mangling table is emitted as a comment block at the top of the generated shader. | So the user reading the copied-out GLSL can find their own variable. |

#### 5.7.4 Reserved bare names in the pixel domain

Per `field-language`. **Bare names, no sigils (I2).** These are the names a pixel kernel may read without
declaring; every one of them is *reserved* — the user may not rebind them, and attempting to is a compile
error naming the reserved word.

| Field name | Type | Binds to | Notes |
|---|---|---|---|
| `uv` | `vec2` | `vUv` | Normalised `[0,1]²`, interpolated. Origin bottom-left (GL convention). |
| `xy` | `vec2` | `gl_FragCoord.xy` | Pixel coordinates. **Pixel-centred: integer + 0.5.** `xy` at the bottom-left pixel is `(0.5, 0.5)`, not `(0,0)`. State the offset in the tooltip — it is the classic off-by-half. |
| `res` | `vec2` | `uniform vec2 fld_res` | Output size in pixels. Also obtainable as `vec2(textureSize(fld_srcTex,0))`, but the uniform is correct even with no input connected. |
| `aspect` | `float` | `fld_res.x / fld_res.y` | Emitted inline, not as its own uniform. Matches `FormulaNode`'s `uAspect`. |
| `t` | `float` | `uniform float fld_t` | Transport seconds. **Frame-domain**, so anything derived from `t` alone is hoisted (I3) and never appears in the shader. |
| `dt` | `float` | `uniform float fld_dt` | Seconds since the previous cook. Frame-domain. |
| `frame` | `int` | `uniform int fld_frame` | Monotonic cook counter. Frame-domain. |
| `col` | `vec3` | `texture(fld_srcTex, vUv).rgb` | The input image's colour at this pixel. Reading `col` with nothing connected yields `vec3(0)`. |
| `alpha` | `float` | `texture(fld_srcTex, vUv).a` | |

Alpha handling: the epilogue is `fragColor = vec4(<result>, src.a * fld_srcAlpha);` — the source alpha
passes through by default, and `fld_srcAlpha` is `0.0` when no input is connected so an unconnected node
outputs transparent black rather than opaque black. If the kernel writes `alpha` explicitly, that value
replaces the pass-through.

### 5.8 Phase 8 — precision, and a designed tolerance

**The CPU VM is `double`. GLSL 150 is `float`. There is no `double` in GLSL below 4.00. Therefore
pixel-domain results will NOT be bit-identical to the frame-domain VM, ever, and no amount of care will
make them so.** The right response is to *design* the tolerance and write the number down, so that a
conformance failure means "the lowering is wrong" and not "floats, I guess".

#### 5.8.1 The four error sources, largest first

| # | Source | Magnitude | Reference |
|---|---|---|---|
| 1 | **Readback quantisation.** The conformance harness reads pixels back with `GLUtil::ReadTexturePixels` (`GLUtil.h:59`), which does `glReadPixels(..., GL_RGBA, GL_FLOAT, ...)` (`GLUtil.cpp:359`) — but the *attachment* is what limits precision. An `RGBA16F` attachment has a **10-bit mantissa**: relative error 2⁻¹¹ ≈ **4.9 × 10⁻⁴**. An `RGBA8` attachment quantises to 1/255 ≈ **3.9 × 10⁻³**, which is worse still. | **4.9e-4** relative | `GLUtil.cpp:120–121`, `:359` |
| 2 | **Transcendental accuracy.** The OpenGL spec's minimum-precision table (§2.3.1, unchanged since GL 2.0) allows `sin`/`cos`/`tan` an **absolute** error of 2⁻¹¹ ≈ **4.88 × 10⁻⁴** over `[-π, π]`. Not relative — absolute. Drivers routinely use exactly this budget. | **4.9e-4** absolute | GL spec §2.3.1 |
| 3 | **`exp2`/`log2` (hence `pow`), `1/x`, `inversesqrt`.** Spec allows 3 ULP for `exp2`/`log2`, 2.5 ULP for division and `inversesqrt`. `pow` inherits both → ~6 ULP ≈ **3.6 × 10⁻⁷** relative. | 3.6e-7 | GL spec §2.3.1 |
| 4 | **float32 rounding in the arithmetic chain.** eps = 2⁻²⁴ ≈ 5.96 × 10⁻⁸; `+`, `-`, `*` are correctly rounded. A 64-operation chain accumulates ≤ 64 × 0.5 ulp ≈ **1.9 × 10⁻⁶** relative. | 1.9e-6 | IEEE-754 binary32 |

Sources 1 and 2 dominate by three orders of magnitude. Everything else is noise.

#### 5.8.2 The number

> **Conformance tolerance: absolute 1 × 10⁻³ for results in `[-1, 1]`; relative 1 × 10⁻³ outside it.**
> Readback FBO must be `GL_RGBA16F` — never the `GL_RGBA8` default.

**Justification:** max(source 1, source 2) ≈ 4.9 × 10⁻⁴, doubled for one further operation applied to an
already-degraded value. Not a round number pulled from the air — 2 × the larger of two spec-mandated bounds.

**And critically, it is tight enough to catch every trap this document exists to prevent:**

| Bug | Error it produces | vs. tolerance |
|---|---|---|
| Emitting GLSL `mod` instead of `fld_mod`, at `mod(-3, 2)` | **2.0** | 2000× |
| Emitting `round()` instead of `floor(x+0.5)`, at `round(-0.5)` | **1.0** | 1000× |
| `mix(b, a, step(0.5, c))` with `c = 0.2` | the whole branch difference, typically **O(1)** | ~1000× |
| Swapping `mix(b,a,…)` to `mix(a,b,…)` | the whole branch difference | ~1000× |
| `texture()` instead of `texelFetch()` on state | grows every frame; visible within ~10 frames | ≫ |
| `float` vs `double` alone | ≤ 1.9 × 10⁻⁶ | 500× **under** — correctly does not fire |

So the tolerance separates real bugs from precision by a factor of ~500 in both directions. That is a
designed tolerance.

**If the owner later approves adding `GL_RGBA32F` to `EnsureFbo`** (a two-line change to shared code, out of
scope here — I7), source 1 drops to 6 × 10⁻⁸ and the tolerance can tighten to **1 × 10⁻⁵ for non-trig
kernels**, with trig kernels staying at 1 × 10⁻³ because source 2 does not improve. Recommend this as a
follow-up; do not do it in step 7.

#### 5.8.3 Rules for the harness

| Rule | Reason |
|---|---|
| Compare against the **frame-domain VM evaluating the same expression at the same `uv`**, not against a stored golden image. | A golden image bakes in one driver's transcendental accuracy. The VM is the specification. |
| Never compare pixel output against the double-precision golden corpus at exact equality (`field-testing` §3 says this; it is right). | |
| Report the **max absolute error and the pixel where it occurred**, not just pass/fail. | A failure at one pixel is a boundary bug; a failure everywhere is a lowering bug. The two need different investigations. |
| Skip pixels where the VM result is `inf` or `NaN`. | §5.3.4 divergences are documented and intentional. |
| Run the harness at a small resolution (64 × 64 is ample) with a fixed `t`. | 4096 samples of the kernel over the full `uv` domain, in milliseconds. |

### 5.9 Phase 9 — the node

`FieldPixelNode : public INode`. Everything it needs already exists on `INode` (`src/core/INode.h`).

| Member | Type | Mirrors |
|---|---|---|
| `mProgram` | `unsigned int` | `FormulaNode::mProgram` |
| `mLastError` | `std::string` | `FormulaNode::mLastError` (`:62`) |
| `mLastCookFrame` | `int` | `FormulaNode::mLastCookFrame` |
| `mOut` | `GLUtil::Fbo` | `FormulaNode::mOut` |
| `mState` | `PixelStateBank` | `ReactionDiffusionNode::mState[2]` + `mFront` (`FeedbackNodes.h:144–146`) |
| `mUniformLocs` | `std::vector<int>` | new — see §5.7.1 |
| `mLineToIrNode` | `std::vector<int>` | new — see §5.6.2 |

| Override | Behaviour |
|---|---|
| `CookIfNeeded(int frameId)` (`INode.h:123`) | Guard on `mLastCookFrame == frameId` first, exactly as `FormulaNode.cpp:411–413`. Then the `:415` recompile guard. Then `EnsureFbo`, state lifecycle (§5.5.2), `RunShaderPass`, swap. |
| `GetOutputTexture()` (`:117`) | `GLUtil::FboTexture(mOut)`. |
| `GetOutputWidth/Height()` (`:118–119`) | `mOut.w` / `mOut.h`. |
| `TextureRevision()` (`:132`) | **Do not override.** The default means "always changed", which is correct for a node whose output genuinely changes every frame. None of the three feedback nodes override it either. Overriding it to return a cached revision would let downstream nodes skip a frame of a live simulation. |
| `VisitParams(ParamVisitor&)` (`:159`) | Expose the step-5 params. `ParamVisitor` has exactly five methods (`INode.h:84–88`) — do not add a sixth (that is `INode.h`, forbidden by §4.3). |
| `LastError()` | `const std::string& LastError() const { return mLastError; }` — the **exact** signature, so `main.cpp:10245–10252` picks it up. |
| `BypassSource()` (`:151`) | Return the input image cable so bypass passes the source through. |

Register with `REGISTER_NODE(FieldPixelNode, FieldPixel, "Source")` next to
`REGISTER_NODE(FormulaNode, Formula, "Source")` at `main.cpp:3762`.

Per the `node-ui-pillars` skill (load it before touching any node UI): symmetry and dark-mode contrast are
non-negotiable. The error pill, the branch-count line, and the state-cell count line all render in the node
body and must be legible against the dark body fill.

### 5.10 Phase 10 — the fixture

**There is no test binary in this repo.** Tests are `INFINITE_*` environment-variable fixtures inside the
single executable, run by `.claude/skills/run-infinite-hygiene/driver.sh`, which invokes them as
`env INFINITE_<NAME>=1 INFINITE_EXITAFTER=<frames> build/Infinite.app/Contents/MacOS/Infinite`
(`driver.sh:390`).

**Your fixture must be the in-frame shape, not the early-exit shape.**

| Shape | Example | GL context? | Use for step 7? |
|---|---|---|---|
| Early-exit | `INFINITE_DSPTEST`, dispatched at `main.cpp:37619` — runs and `exit()`s **before `glfwInit()`** | ❌ none | **No.** Every line of this step needs a GL context to compile a shader. |
| In-frame | `INFINITE_ROUNDTRIPTEST`, `main.cpp:43318` — fires inside the frame loop at `frameId == 4` | ✅ yes | **Yes.** Model on this exactly. |

Fire at `frameId == 4` like `ROUNDTRIPTEST`, not `frameId == 0`: the state ping-pong needs several cooks to
have exercised a swap, and the compile has to have happened on a live context.

Verdict strings must match `driver.sh`'s markers (`:69–70`):

| | Pattern |
|---|---|
| Pass | `PASS_MARK=' OK$\|OK$\|PASS$\|SKIP$\|CLEAN$'` — end the line with ` OK` or `PASS` |
| Fail | `FAIL_MARK='FAIL\|BUG$\|MISMATCH\|SUSPECT\|DID NOT MOVE\|TONE MISSING'` |

A fixture that dies before printing anything counts as a failure (the driver requires a *positive* verdict,
not merely the absence of a negative one). Print your verdict line unconditionally.

`INFINITE_FIELDPIXELTEST` must assert, printing one verdict line each:

| # | Assertion | Catches |
|---|---|---|
| 1 | A trivial kernel (`col = vec3(uv.x, uv.y, 0.5)`) compiles: `mProgram != 0`, `mLastError.empty()` | The emitter produces valid 150 at all |
| 2 | The generated source contains `#version 150` and no other `#version` | T1 |
| 3 | The generated source contains `fld_mod(` and does **not** match `[^_]mod\(` | T2 — the highest-value single grep in this document |
| 4 | The generated source contains `floor(` + `0.5)` for a `round` kernel and does **not** contain `round(` | T3 |
| 5 | An `if(c,a,b)` kernel emits `mix(` with `!= 0.0` and does **not** contain `step(0.5` | T4 |
| 6 | A **deliberately broken** kernel leaves `mProgram` unchanged (compare the handle before and after) and sets a non-empty `mLastError` | §5.6 properties 1–3 |
| 7 | After that failure, cooking 10 more frames performs **zero** further `CompileProgram` calls (count them with a static counter) | §5.6 property 5 — the every-frame-recompile freeze |
| 8 | A state kernel (`state x = x + dt; col = vec3(fract(x))`) produces a **different** readback at frame 4 than at frame 2 | The ping-pong actually ping-pongs |
| 9 | After a simulated transport seek, the state readback returns to the initialiser | Step 6's reset contract, §5.5.2 |
| 10 | Conformance: for 8 kernels spanning every §5.3 opcode, the GPU readback at 64×64 (`GL_RGBA16F`) matches the frame-domain VM within **1e-3** (§5.8.2); print the max abs error and its pixel | The whole emission table |
| 11 | State reads use `texelFetch(` and the source contains no `texture(fld_s_` | §5.5.3 |
| 12 | Requesting 5 state cells is refused with an error string containing `"4 cells max"` | OPEN-2 |

---

## 6. Traps

Each row names the specific bug, not a general worry.

| # | Trap | The bug |
|---|---|---|
| **T1** | Emitting `#version 330` because `windows-parity` mentions "GLSL 330 strictness" | `src/` is uniformly `#version 150` (57 occurrences, zero others) and `GLUtil.cpp`'s shared vertex shader is 150. A 330 fragment shader **will not link** against a 150 vertex shader. The skill is about literal typing and uniform-type exactness, not the version directive. |
| **T2** | Emitting `mod(x, y)` | GLSL `mod` is floored, Field's `%` is C `fmod` (truncated). `mod(-3,2)` = `1` vs `-1`. Every negative operand is wrong, and the output still looks like a plausible tiling pattern. **The single most likely silent bug in this backend.** Emit `fld_mod`. |
| **T3** | Emitting `round(x)` | GLSL's `round()` is implementation-defined at exactly ±0.5; the same shader gives different results on AMD and NVIDIA. `Expression.cpp` is `floor(a[0] + 0.5)`. Emit that. `roundEven` is well-defined but is banker's rounding — also wrong. |
| **T4** | Lowering `if(c,a,b)` as `mix(b, a, step(0.5, c))` | Implements "c > 0.5", but Field's rule is "any non-zero is true". `if(0.2, a, b)` returns the wrong branch. The float `mix` is also arithmetic, so an `inf` in the *unselected* branch yields `inf * 0.0 = NaN` in the result. Use `mix(b, a, c != 0.0)`. |
| **T5** | Getting `mix`'s argument order backwards — `mix(a, b, cond)` | `mix(x,y,a)` returns `x` when `a` is **false**. Field's `if(c,a,b)` returns `a` when **true**. So it is `mix(b, a, cond)`. Backwards inverts every conditional in the language. |
| **T6** | Emitting `pow(b, e)` directly | Undefined for `b < 0`. `std::pow(-2.0, 3.0) == -8.0` on the CPU; the GPU gives NaN. Use `fld_pow`. |
| **T7** | Emitting `clamp(x, lo, hi)` directly | Undefined when `lo > hi`; `Expression.cpp`'s `min(max(x,lo),hi)` returns `hi`. `clamp(x, 1.0, 0.0)` is a real idiom. Use `fld_clamp`. |
| **T8** | Emitting `smoothstep(e0, e1, x)` directly | Undefined for `e0 >= e1`; `Expression.cpp` has an explicit `e0 == e1` guard. **`FormulaNode`'s own default preset uses `smoothstep(0.005, -0.005, d)`** — reversed edges are a live idiom here. Use `fld_smoothstep`. |
| **T9** | Emitting `mix(a, b, t)` for `lerp` | `Expression.cpp` is `a + (b-a)*t`; GLSL `mix` is `x*(1-a)+y*a`. Algebraically equal, numerically not, and on an `fma` path measurably not. Use `fld_lerp`. |
| **T10** | Reading state with `texture(sampler, vUv)` | `EnsureFbo` hardcodes `GL_LINEAR` (`GLUtil.cpp:128`) and you may not change it. Every state read becomes a bilinear blend of four neighbours; an integrator diffuses sideways every frame. It looks like an artistic blur. Use `texelFetch(s, ivec2(gl_FragCoord.xy), 0)`. |
| **T11** | Swapping the ping-pong before the pass, or twice per cook | Before → you read the texture you are about to write (undefined, driver-dependent). Twice → every second frame's work is discarded and the simulation runs at half speed. Swap exactly once, immediately after `RunShaderPass` returns. |
| **T12** | Declaring two or more `out vec4` in the fragment shader | `layout(location=)` on fragment outputs is **GLSL 330**, and `GLUtil::CompileProgram` never calls `glBindFragDataLocation` (it binds only `aPos`/`aUv` at `GLUtil.cpp:202–203`). With 2+ outputs the linker assigns locations arbitrarily, so your channels swap between drivers. One output. Pack ≤ 4 cells into RGBA (OPEN-2). |
| **T13** | Recompiling a failing shader every frame — `if (mProgram == 0) Apply();` | `FormulaNode.cpp:415` is `if (mProgram == 0 && mLastError.empty())`. Without `&& mLastError.empty()`, a permanently broken kernel calls `glCompileShader` + `glLinkProgram` (1–50 ms) **60 times a second, forever**. The app appears hung. Same bug class as step-01 trap 7.12. |
| **T14** | Assigning `mProgram` before checking the new program linked, or `glDeleteProgram` before the new one succeeds | Either one blanks the node on a typo, violating the documented `FormulaNode.h` contract that "a typo never blanks the graph". |
| **T15** | Passing `GL_R32F` or `GL_RGBA32F` to `EnsureFbo` | `GLUtil.cpp:120–121` only distinguishes `GL_RGBA16F` (→ `GL_FLOAT`) from everything else (→ `GL_UNSIGNED_BYTE`), with `format = GL_RGBA` always. Anything else gets the wrong external type. Only `GL_RGBA8` and `GL_RGBA16F` are supported. |
| **T16** | Comparing pixel output to the CPU VM at exact equality, or with an undocumented epsilon | The CPU VM is `double`; GLSL 150 has no `double`. They will never match bit-for-bit. State **1e-3** (§5.8.2) with the justification, or every future precision question reopens the argument. |
| **T17** | Reading conformance pixels back from an `RGBA8` FBO | 1/255 ≈ 3.9e-3 quantisation is **coarser than the 1e-3 tolerance** — the harness cannot pass. Use `GL_RGBA16F`. |
| **T18** | Emitting a user identifier unmangled, or maintaining a GLSL reserved-word blocklist | A user's variable named `mix`, `float`, `discard`, `main`, or `sampler2D` breaks the shader. The blocklist has ~80 entries and grows with every GLSL version; you will get it wrong. Mangle unconditionally (§5.7.3) and the problem is structurally impossible. |
| **T19** | Emitting a double underscore (`fld__t0`) or a `gl_` prefix | Both are reserved by the GLSL spec and may be rejected outright. Single underscores only. |
| **T20** | Emitting integer literals where floats are expected — `pow(x, 2)`, `mix(a, b, 0)` | `windows-parity` §3.9. Strict drivers reject the implicit int→float conversion during overload resolution. Every emitted numeric literal gets a decimal point. |
| **T21** | Binding sparse texture units (0, 2, 5) | `windows-parity` §3.9: sequential from 0. Sparse units work on macOS and fail on some Windows drivers. |
| **T22** | Calling `glGetUniformLocation` per uniform per frame | A driver-side string lookup on every uniform on every cook. Cache the locations once after link. Treat `-1` as "optimised out", **not** as an error — the driver legally strips uniforms the body does not reference. |
| **T23** | Transliterating step 2's `Xorwise`/`TimeToRand` into GLSL | They use `int64_t`; GLSL 150 has no 64-bit integers (they are 4.00). Substituting a float hash makes `rand` **silently different** from the CPU VM. The `t`-only case is hoisted and bit-exact; refuse the pixel-varying case with a message naming the reason (OPEN-3). |
| **T24** | Adding a `pixel` rate annotation to the syntax to make codegen easier | Violates I3. Rate is inferred. If the backend needs to know a node's domain, it reads step 4's fixpoint result off the IR node — it does not ask the user. |
| **T25** | Adding a GLSL-shaped opcode to the IR (`SamplerRead`, `Vec4Pack`) | Violates I4. The IR is retargeted to C++ and WASM next; a GLSL-shaped IR makes the second backend a rewrite. Sampler binding is a *backend* concern; the IR says `StateRead(cell)`. |
| **T26** | Emitting the helper prelude conditionally, to keep the source short | Two kernels that differ only in the body then produce sources that differ in the prelude, and golden-file diffs become unreadable. Always emit the whole prelude; the driver dead-strips it. |
| **T27** | Using an out-of-range `texelFetch` for a neighbour read | Out-of-range `texelFetch` is **undefined** — it is not clamped, and it is not zero. Emit `clamp(coord, ivec2(0), textureSize(s,0)-1)`. |
| **T28** | Resizing without resetting state | `EnsureFbo` reallocates on a size change, destroying contents; whatever the driver hands back is garbage. Detect the change, set `mNeedsClear`, and say so in the tooltip. Do not resample — a bilinear resample of an accumulator is meaningless. |
| **T29** | Using `INFINITE_DSPTEST`'s early-exit shape for the fixture | It exits before `glfwInit()` (`main.cpp:37619`). No GL context, so nothing in this step can be tested. Use the in-frame shape (`INFINITE_ROUNDTRIPTEST`, `main.cpp:43318`, `frameId == 4`). |
| **T30** | Modifying `GLUtil`, `Expression`, `INode`, or `FormulaNode` | I7 and §4.3. Every requirement in this step is reachable without touching them. If you believe it is not, raise an OPEN — do not edit shared code to make your step easier. |
| **T31** | Reading Kronos, Cmajor, SuperCollider, or `/Users/namansoni/BespokeSynth` for reference | I1. GPL. This is a licensing invariant, not a style rule. Cite the Kronos paper (Norilo, CMJ 39:4, 2015) instead. |

---

## 7. Machine-checkable exit criterion

Run this from the repo root. **Every line must print `OK`.** No line may print `FAIL`.

```bash
#!/usr/bin/env bash
# docs/plans/field/step-07-exit.sh  --  run from /Users/namansoni/infinte
set -uo pipefail
cd /Users/namansoni/infinte
export INFINITE_NO_UPDATE_CHECK=1
BIN="build/Infinite.app/Contents/MacOS/Infinite"
FAILED=0
chk() { if [ "$2" = "$3" ]; then echo "$1: OK"; else echo "$1: FAIL (got '$2', want '$3')"; FAILED=1; fi; }

# ---------------------------------------------------------------- 1. build
if [ ! -d build ]; then cmake -B build -DCMAKE_BUILD_TYPE=Debug >/tmp/f7_cfg.log 2>&1; fi
if cmake --build build -j8 >/tmp/f7_build.log 2>&1; then echo "build: OK"; else echo "build: FAIL"; tail -40 /tmp/f7_build.log; exit 1; fi

# ------------------------------------------------- 2. new files are wired in
for f in src/core/field/GlslBackend.cpp src/core/field/GlslHelpers.h \
         src/core/field/PixelState.cpp src/nodes/FieldPixelNode.cpp; do
  chk "exists $f" "$( [ -f "$f" ] && echo y || echo n )" "y"
done
chk "cmake lists GlslBackend"  "$(grep -c 'src/core/field/GlslBackend.cpp'  CMakeLists.txt)" "1"
chk "cmake lists PixelState"   "$(grep -c 'src/core/field/PixelState.cpp'   CMakeLists.txt)" "1"
chk "cmake lists FieldPixel"   "$(grep -c 'src/nodes/FieldPixelNode.cpp'    CMakeLists.txt)" "1"

# ------------------------------------------- 3. shared code was NOT modified
#    (compare against HEAD; any diff in these files is a hard failure)
for f in src/core/GLUtil.h src/core/GLUtil.cpp src/core/Expression.h src/core/Expression.cpp \
         src/core/INode.h src/nodes/FormulaNode.h src/nodes/FormulaNode.cpp \
         src/nodes/FeedbackNodes.h src/nodes/FeedbackNodes.cpp; do
  chk "unmodified $f" "$(git diff --stat HEAD -- "$f" | wc -l | tr -d ' ')" "0"
done

# --------------------------------------------- 4. version pinning (T1)
#    src/ must remain uniformly #version 150.  Expect: 150 only, count grew by
#    exactly the number of new emitters you added (>= 58, never any other version).
chk "no non-150 version in src" \
    "$(grep -rhno '#version [0-9]*' src/ | grep -cv '#version 150')" "0"
echo "version histogram: $(grep -rhno '#version [0-9]*' src/ | sed 's/^[0-9]*://' | sort | uniq -c | tr '\n' ' ')"

# ------------------------------- 5. emitter never emits the forbidden builtins
#    Grep the BACKEND SOURCE for string literals that would reach the shader.
#    Each of these must be 0 outside a comment.
B=src/core/field/GlslBackend.cpp
chk "no bare mod( emitted (T2)"        "$(grep -o '"[^"]*[^_a-zA-Z]mod("' $B | wc -l | tr -d ' ')" "0"
chk "no round( emitted (T3)"           "$(grep -o '"[^"]*round("'        $B | wc -l | tr -d ' ')" "0"
chk "no step(0.5 emitted (T4)"         "$(grep -c 'step(0\.5'            $B)" "0"
chk "no layout(location emitted"       "$(grep -c 'layout(location'      $B)" "0"
chk "no precision qualifier emitted"   "$(grep -cE '"(highp|mediump|lowp) ' $B)" "0"
chk "no double underscore ident (T19)" "$(grep -c 'fld__'                $B)" "0"
chk "fld_mod helper present"           "$(grep -c 'fld_mod' src/core/field/GlslHelpers.h)" "1"
for h in fld_clamp fld_lerp fld_smoothstep fld_pow; do
  chk "helper $h present" "$( [ "$(grep -c "$h" src/core/field/GlslHelpers.h)" -ge 1 ] && echo y || echo n )" "y"
done
chk "texelFetch used for state (T10)"  "$( [ "$(grep -c 'texelFetch' $B)" -ge 1 ] && echo y || echo n )" "y"
chk "no texture(fld_s_ (T10)"          "$(grep -c 'texture(fld_s_'       $B)" "0"

# ------------------------------------- 6. keep-last-working-program (T13/T14)
N=src/nodes/FieldPixelNode.cpp
chk "do-not-retry guard present (T13)" \
    "$(grep -cE 'mProgram == 0 && mLastError\.empty\(\)' $N)" "1"
chk "cook frame guard present" "$(grep -c 'mLastCookFrame == frameId' $N)" "1"
chk "LastError accessor present" \
    "$(grep -c 'const std::string& LastError() const' src/nodes/FieldPixelNode.h)" "1"

# -------------------------------------- 7. the fixture (in-frame, needs GL)
env INFINITE_FIELDPIXELTEST=1 INFINITE_EXITAFTER=35 "$BIN" >/tmp/f7_fixture.log 2>&1
chk "fixture produced output" "$( [ -s /tmp/f7_fixture.log ] && echo y || echo n )" "y"
chk "fixture no negative verdict" \
    "$(grep -cE 'FAIL|BUG$|MISMATCH|SUSPECT|DID NOT MOVE' /tmp/f7_fixture.log)" "0"
chk "fixture 18 positive verdicts" \
    "$(grep -cE ' OK$|PASS$' /tmp/f7_fixture.log)" "18"
grep -E 'FIELDPIXEL' /tmp/f7_fixture.log

# ----------------------------------- 8. tier-1 regression gate (unchanged)
.claude/skills/run-infinite-hygiene/driver.sh --fast --skip-build >/tmp/f7_tier1.log 2>&1
chk "tier1 clean" "$(grep -cE 'FAIL|BUG$|MISMATCH|SUSPECT' /tmp/f7_tier1.log)" "0"
tail -5 /tmp/f7_tier1.log

echo "-----"
if [ "$FAILED" = "0" ]; then echo "step-07 exit criterion: PASS"; else echo "step-07 exit criterion: FAIL"; exit 1; fi
```

Expected terminal output when the step is complete:

```
build: OK
exists src/core/field/GlslBackend.cpp: OK
... (all `exists` lines OK)
cmake lists GlslBackend: OK
cmake lists PixelState: OK
cmake lists FieldPixel: OK
unmodified src/core/GLUtil.h: OK
... (all 9 `unmodified` lines OK)
no non-150 version in src: OK
version histogram:   58 #version 150
... (all emitter greps OK)
do-not-retry guard present (T13): OK
fixture produced output: OK
fixture no negative verdict: OK
fixture 18 positive verdicts: OK
tier1 clean: OK
-----
step-07 exit criterion: PASS
```

Notes on the block:

| Line | Note |
|---|---|
| `cmake -B build -DCMAKE_BUILD_TYPE=Debug` / `cmake --build build -j8` | Taken from `driver.sh:342–352`. Not invented. |
| `build/Infinite.app/Contents/MacOS/Infinite` | `driver.sh:33`. |
| `env INFINITE_<NAME>=1 INFINITE_EXITAFTER=<n> "$BIN"` | `driver.sh:390`. `35` matches `ROUNDTRIPTEST`'s frame budget in `TIER1_CHECKS` (`driver.sh:79`). |
| Verdict regexes | `driver.sh:69–70` (`FAIL_MARK` / `PASS_MARK`). |
| `--fast --skip-build` | `--fast` is Tier 1, the pre-commit smoke list (`driver.sh:14`, parsed at `:43`); `--skip-build` reuses `build/` (`:18`, `:41`). The flag is `--fast`, **not** `--tier1` -- the latter does not exist and the script would reject it. Tier 1 is the 11-entry `TIER1_CHECKS` array at `driver.sh:79`. |
| The version histogram is **printed, not asserted at an exact count** | You will add one or more `#version 150` string literals; the invariant is *only 150 ever appears*, which is what the assertion checks. Today's count in `src/` is **57**. |

---

## 8. Out of scope

| Not in this step | Where it belongs |
|---|---|
| The `element` domain (per-vertex, per-instance) | Step 4 — already done |
| The `sample` domain (audio-rate) | Step 9 |
| The `graph` domain | Step 10 |
| Transfer operators between domains (`gather`, `reduce`, `lift`) | Step 8 |
| A C++ or WASM backend for the same IR | Later. The IR must stay backend-neutral (I4) so this remains cheap. |
| MRT / more than 4 pixel state cells | Follow-up. Needs `glBindFragDataLocation` inside `GLUtil::CompileProgram` — shared code, owner approval. |
| `GL_RGBA32F` state and the tighter 1e-5 tolerance | Follow-up. Needs two lines in `GLUtil::EnsureFbo` — shared code, owner approval. |
| Pixel-varying `rand`/`noise`/`sh` | Blocked on GLSL 150's lack of 64-bit integers (OPEN-3). Revisit only if the target moves to 330+ **and** the owner accepts a documented divergence from the CPU generator. |
| Compute shaders, reductions, scatter writes | Not expressible below GLSL 430. Anything needing them is not a pixel-domain kernel. |
| Multi-pass kernels (a kernel that needs an intermediate full-resolution buffer) | Later. v1 is one kernel, one pass. |
| Shader caching to disk | Later. Compiles happen on the edit edge, not the cook edge (§5.6), so the cost is already off the hot path. |
| Refactoring `FormulaNode` and `FieldPixelNode` onto a shared compile-policy base class | Good idea; not now. It would couple step 7 to a shipping node (§4.3). |
| Fixing the 14 skill drifts in §3.3 | Owner's task. Record, do not edit `.claude/skills/**`. |
| `git add`, `git commit`, `git push` | **Explicitly forbidden.** Leave the working tree dirty for the owner to review. |

---

## 9. Which earlier steps must be finished first

| Step | Required? | What step 7 consumes from it | What breaks without it |
|---|---|---|---|
| **1 — expression → IR** | **Hard blocker** | The typed IR itself: opcodes, SSA form, source spans, the constant folder | There is nothing to transpile. §5.3 is a table from IR opcodes to GLSL; with no IR there is no left-hand column. Source spans are also what make §5.6.2's error mapping possible. |
| **2 — pure randomness** | **Soft** | The `rand`/`noise`/`sh` definitions | You only need to know that they exist and use `int64_t`, which is the basis of OPEN-3. If step 2 is unfinished, implement OPEN-3's refusal anyway — it is the correct behaviour either way. |
| **3 — vectors and rank** | **Hard blocker** | The type lattice, the promotion table, constructors, swizzles | Every emitted line needs a GLSL type. Without rank, you cannot decide between `float fld_t0` and `vec3 fld_t0`, and §5.3.3's vector helper overloads have no rule for which to call. |
| **4 — element domain** | **Hard blocker** | The **domain-inference fixpoint and hoisting** | This is the load-bearing dependency and the least obvious one. §5.7.1's "hoisted frame-domain values arrive as uniforms" *is* step 4's output. Without it, every subexpression is pixel-domain, `t`-driven `rand` is not hoisted (so OPEN-3 refuses kernels that should work), and §5.4's frame-domain-condition escape hatch does not exist. Step 4 also formalises the `graph ⊑ frame ⊑ {element \| pixel}` lattice that tells you which nodes belong in the shader at all. |
| **5 — param declarations** | **Hard blocker** | The `param` node type and the value store | §5.7.1's `fld_p_*` uniforms come straight from here, as does the `kMaxParams` bound (`ParamMailbox.h:23`). |
| **6 — state cells** | **Hard blocker; step 6 names step 7 as its dependent in its own closing note** | The `state` declaration syntax, delay-sugar desugaring, initialisers, and the **reset-on-seek/loop/stop contract** | §5.5 is the pixel-domain *implementation* of step 6's semantics. Step 6 also explicitly deferred the texture-format decision to step 7 (its OPEN 4 → this document's OPEN-1) and corrected `field-state`'s memory arithmetic in its §5.6. Attempting §5.5 without step 6 means inventing the reset contract, and the two domains will then disagree about when a reset fires. |
| **8 — transfer operators** | **No** | — | Step 8 depends on 7, not the reverse. |
| **9 — sample domain**, **10 — graph domain** | **No** | — | Independent domains. |

**Minimum set: 1, 3, 4, 5, 6.** Step 2 is soft. Steps 8, 9, 10 come after.

Order note: step 4 is easy to skip because "the pixel domain" sounds self-contained. It is not — without
hoisting, the GLSL backend produces *correct* shaders that are several times slower than they should be,
and the language's central claim (I3: rate is inferred, so hoisting is free) is unverified. Do step 4 first.
