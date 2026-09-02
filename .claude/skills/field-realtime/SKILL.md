---
name: field-realtime
description: Field's real-time safety constraints as a checklist you can run against a diff — no heap allocation, no recursion, no unbounded loops, no strings/pointers/dynamic arrays/structs in v1, every value's size known at compile time, element and voice counts bounded and declared up front — plus the per-domain branching cost model including GPU predication and divergence. Use when reviewing any Field compiler diff, when generated code will run on the audio thread or in a fragment shader, when someone proposes a language feature that allocates or recurses, when a Field kernel causes an xrun or a frame-time spike, or when deciding whether a branch is affordable in a given domain.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

Read [`field-language`](../field-language/SKILL.md) for the syntax these
constraints restrict and [`field-compiler`](../field-compiler/SKILL.md) §8 for
where the checks run in the pipeline.
[`field-integration`](../field-integration/SKILL.md) covers the thread contract
these rules protect.

This is the skill to run **against a diff**, not to read for background.

---

## 0. Invariants

1. **Clean room.** Never read Kronos, Cmajor, SuperCollider or BespokeSynth
   source. The Kronos *paper* (Norilo, CMJ 39:4, 2015) is citable.
2. **These constraints are non-negotiable.** They are not defaults with an
   escape hatch. A feature that needs one relaxed does not ship.
3. **They apply to generated code, not just hand-written code.** A compiler
   that emits a `std::vector` push into the sample backend has broken rule 1 as
   surely as a hand-written one.

---

## 1. The checklist — run this against the diff

Each row is checkable. `✗` means the diff must not contain it.

| # | Rule | ✗ Look for in the diff | Where it bites |
|---|---|---|---|
| 1 | **No heap allocation anywhere** in generated or runtime code past init | `new`, `malloc`, `std::vector::push_back/resize/reserve`, `std::string` construction, `std::map` insert, `std::function`, `make_shared` | audio thread → xrun; render thread → frame spike |
| 2 | **No recursion**, direct or mutual | a function in the codegen or runtime that reaches itself; an IR walker without a depth bound | stack overflow, and unbounded stack is not analyzable |
| 3 | **No unbounded loops** | `while`, `for` with a non-constant bound, `do`, any loop whose trip count reads a runtime value | the whole point is that a bounded loop can be unrolled/vectorised |
| 4 | **No strings, pointers, dynamic arrays or structs in v1** | any of those in the *language surface* or in a generated kernel's data | v1 scope; each one reintroduces rule 1 |
| 5 | **Every value's size known at compile time** | a size read from a param, a resolution, an N | codegen cannot allocate registers otherwise |
| 6 | **Element and voice counts bounded and declared up front** | an unbounded `map`, a voice count from a runtime value | Kronos accepts the same constraint for polyphony (p.46) |
| 7 | **No locks, no `dynamic_cast`, no GL, no ImGui, no file I/O, no `printf`** inside anything the audio thread calls | any of those below a `ProcessBlock` | this is the existing house rule (`.claude/skills/new-audio-node/SKILL.md` §0.4); Field does not get an exemption |
| 8 | **No allocation in `CookIfNeeded`** beyond what already existed | a `std::string` built per frame, a vector rebuilt per frame | `CookIfNeeded` has a < 5 µs budget in the audio node contract |
| 9 | **No new cross-thread channel** | a new atomic, a new queue, a shared mutable field | `ParamMailbox` (main→audio) and `MeterRing` (audio→main) are the only two legal ones |
| 10 | **`param` count ≤ 128** per sample-domain kernel | more slots than `ParamMailbox::kMaxParams` | `src/audio/ParamMailbox.h:24` — silent truncation otherwise |
| 11 | **Compile happens on the main thread, never on the audio thread** | a compile, a shader compile, or an IR rebuild reachable from `ProcessBlock` | shader compiles take milliseconds |
| 12 | **A failing compile changes nothing that is running** | a diff that swaps in a program before checking it compiled | `FormulaNode::Apply()` (`src/nodes/FormulaNode.cpp:390`) is the reference |

**Where the rules are enforced.** Rules 1–6 are compile errors in the typed IR
(`field-compiler` §8). Rules 7–12 are properties of the *compiler's own C++*
and are caught by code review plus the existing sweeps — there is no automatic
check for them, which is exactly why this list exists as a diff checklist.

## 2. The bounded-size rules, spelled out

| Wrong | Right | Why |
|---|---|---|
| `while (x > 0) { … }` | `for (i = 0; i < 8; i++) { if (x > 0) { … } }` | rule 3 — the second form has a known trip count and the branch is predicated |
| `for (i = 0; i < count; i++)` where `count` is `element`'s reserved element count | `for (i = 0; i < 8; i++)` | `count` is a runtime value; a per-element kernel already runs once per element, so this loop is usually a mistake anyway |
| `map` over "however many points arrive" | declare the bound: the node's maximum element count, fixed up front | rule 6 |
| a voice count driven by a param | a compile-time maximum, with unused voices idle | rule 6 |
| a `state` array sized from a delay-time param | a fixed maximum buffer, with a runtime read index | rule 5 — this is how every delay line in the codebase already works |
| `downsample(x, k)` with `k` from a param | `k` a literal | `field-domains` §6 |

**The delay-line pattern is the general answer to rule 5.** When something
genuinely needs a runtime-varying length, allocate the compile-time maximum and
vary the *index*, never the *size*. That is what `src/audio/` already does and
it is the pattern to reach for before proposing an exception.

## 3. Where allocation hides

Rule 1 is the one that gets violated by accident, because C++ allocates
invisibly. The specific shapes to grep a Field diff for:

| Shape | Allocates | Safe replacement |
|---|---|---|
| `std::string` anywhere below `ProcessBlock` | yes, unless SSO | a fixed `char[]`, or don't format at all |
| `std::vector` returned by value from a per-cook function | yes | fill a member vector reserved once at init |
| `std::function` | usually | a raw function pointer or a template |
| `std::map` / `std::set` lookup that inserts | yes, on miss | a flat array indexed by a small int |
| `std::shared_ptr` / `make_shared` | yes | a value member, or an index into a pool |
| exceptions | yes | don't |
| `snprintf` into a `std::string` | yes | `snprintf` into a `char[]` |

**A precedent already in the codebase.** `EquationDsp` uses
`std::shared_ptr<AstNode>` for its AST (`src/audio/dsp/EquationDsp.h:119`) —
that is fine because the AST lives on the **main thread** and is only walked to
build a wavetable bank, never inside `ProcessBlock`. Field's IR may do the same.
Field's *generated sample-domain code* may not.

## 4. The branching cost model

Field **allows** branching (`field-language` §11). Kronos does not — it buys
total analyzability by shutting runtime values out of program flow entirely
(Norilo p.36). Because Field allows it, every place that shows branching syntax
owes the reader this table.

| Domain | Lowering | Cost when the branch is taken/not | Divergence |
|---|---|---|---|
| `frame` | real branch | one predictable branch, 60/s | n/a |
| `element` | real branch | correctly predicted when elements agree; a mispredict when they do not, **and the batch loses vectorization either way** | per-batch |
| `sample` | real branch | ~18 cycles on a mispredict, in which the chip could have retired **144 floating-point ops** (Norilo p.31, citing Ertl & Gregg's 50–98% interpreter mispredict rate) | per-sample |
| `pixel` | **predication** — both sides evaluated, result selected | **always** the sum of both sides, never less | per-warp/wavefront |

### 4.1 GPU predication, precisely

A fragment shader does not skip work. The hardware executes both sides of a
branch for every lane in the warp and masks the writes.

```
   if (c) { A } else { B }        cost on GPU  =  A + B + select
                                  cost on CPU  =  max(A, B) + a branch
```

| Consequence | |
|---|---|
| A pixel branch never saves time | it costs the sum, plus the select |
| Nesting multiplies | two nested branches evaluate up to 4 bodies |
| An expensive `else` you "never take" still costs full price | move it out of the kernel |
| A branch is still worth writing for **correctness** | just never for speed |
| **Uniform** branches (condition is a `param`/frame-domain value) are the exception | the whole warp agrees, so the driver may skip a side — but this is a driver optimisation, not a guarantee, so do not budget for it |

| Wrong | Right |
|---|---|
| "the `if` skips the expensive path for most pixels" | it evaluates both; budget for the sum |
| a raymarcher with an early-out `if` inside the pixel kernel, budgeted as if it exits early | budget the worst case |
| deep nesting to "specialise" per pixel | compute both and `mix()`, which is what the lowering does anyway — at least then the cost is visible in the source |

### 4.2 Branching and the domains that interact with it

- A branch inside a `downsample(…, k)` costs the same *per invocation* — the
  saving is 1/k on the *count*, not on the branch (`field-domains` §6).
- A branch inside a `map` is per element (`field-domains` §4).
- A branch whose condition is a coarser-domain value is hoisted by rate
  inference and becomes free in the fine domain — the compiler should emit two
  specialised loops. **Check that it does**; if it does not, that is the single
  highest-value optimisation in the element and pixel backends.

## 5. Budgets — what "affordable" means per domain

| Domain | Invocations/sec | A kernel budget that will not be noticed |
|---|---|---|
| `graph` | 0 (edit time) | anything under a few ms per edit |
| `frame` | 60 | ~100 µs of the 16.6 ms frame |
| `element` | 60 × 5 000 = 300 k | ~2 ms total per frame → ~6 ns per element |
| `pixel` | 60 × 2.07 M = 124 M | measured in shader instructions, not time — treat >200 ALU ops per pixel at 1080p as needing justification |
| `sample` | 48 000 (per voice) | at 48 kHz with a 256-frame block, the whole audio callback budget is ~5.3 ms; a Field kernel should be a small fraction of it |

The `CookIfNeeded` < 5 µs budget from the audio-node contract
(`.claude/skills/new-audio-node/SKILL.md` §0.3) applies unchanged to a Field
node: **`CookIfNeeded` drains meters and pushes dirty params. It does not
compile and it does no DSP.**

## 6. Review procedure for a Field diff

1. `grep` the diff for every shape in §3's left column. Any hit below a
   `ProcessBlock` or inside generated sample/pixel code is a stop.
2. Confirm every loop in generated code has a literal bound.
3. Confirm no generated function reaches itself.
4. Confirm the `param` count check against `ParamMailbox::kMaxParams` exists and
   errors rather than truncating.
5. Confirm compilation cannot be reached from the audio thread (§1 rule 11) —
   trace every caller of the compile entry point.
6. Confirm a failing compile leaves the running program untouched (§1 rule 12).
7. Confirm no new cross-thread channel was introduced (§1 rule 9).
8. For a pixel-backend change, count the ALU cost of the *sum* of both sides of
   every branch, not the max.
9. Run `/run-infinite-hygiene`. For anything touching the sample domain, also
   run the audio sweeps (`AUDIOPARAMSWEEPTEST`, `AUDIOTEARDOWNSWEEPTEST`) and
   confirm **zero xruns**.
10. For anything touching the pixel domain, check both platforms — see
    `.claude/skills/windows-parity/SKILL.md`. Generated GLSL is exactly the kind
    of thing that compiles on one driver and not the other.

## 7. Exit criterion

A Field change satisfies the real-time contract when:

1. Every row of §1 is `✗`-clean against the diff.
2. Every constraint in §1 rules 1–6 is a **compile error with a message naming
   the span**, not an unchecked convention.
3. Generated sample-domain code allocates nothing after `PrepareToPlay` —
   verified by a run under an allocation counter or by inspection of every
   emitted construct.
4. A branch-heavy pixel kernel's measured cost matches the *sum-of-both-sides*
   prediction, not the max — if it matches the max, the lowering is not
   predicating and the model in §4 is wrong for this backend, which must then
   be corrected here.
5. `/run-infinite-hygiene` passes and the audio sweeps report zero xruns.
