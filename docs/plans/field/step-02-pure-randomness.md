# Field build step 2 — `rand` / `noise` / `sh` become pure functions of `(t, seed)`

**Repo:** `/Users/namansoni/infinte` (C++17, ImGui + OpenGL, MIT).
**Branch first** (`.claude/skills/git-branch-workflow`): `feature/field-step-02-pure-randomness`.

Self-contained implementation prompt. Read the whole file before writing code.
Line numbers are from `src/` at the commit this was written against — re-grep the
**symbol** if a number has drifted; the symbol is authoritative, the number is not.

**Prerequisite that must already be finished and merged:**

| Step | What it delivered | How to confirm |
|---|---|---|
| 1 | `src/core/Expression.cpp` restructured into lexer → AST → typed IR → bytecode behind a byte-identical `Expression::Evaluate`, plus the frozen golden corpus and the `INFINITE_FIELDTEST` fixture | `ls src/core/field/` (or wherever step 1 put it) is non-empty; `INFINITE_FIELDTEST=1 build/Infinite.app/Contents/MacOS/Infinite` prints sections A–D with no `FAIL`; the corpus data file exists and is split into a `deterministic` set and a `random` set |

If step 1 is not merged, **stop and say so.** Step 2 is a change of *values*, and
without step 1's corpus there is nothing to prove the change was confined to the
values it was supposed to change. See `docs/plans/field/step-01-expression-to-ir.md`.

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
| TidalCycles (GPL3) | **relevant to this step specifically** — its *docs and papers* are safe; **its source is not** |
| BespokeSynth (GPLv3) | also at `/Users/namansoni/BespokeSynth` **on this machine — do not open it** |

The algorithm in §6 was **already independently reimplemented from the published
algorithm description** and is reproduced in this file in full. That reproduction
is your source. **Do not go looking for the original.** If you catch yourself
about to `grep` a Tidal checkout or `/Users/namansoni/BespokeSynth`, stop.

The Kronos *paper* (Norilo, "Kronos: A Declarative Metaprogramming Language for
Digital Signal Processing", Computer Music Journal 39:4, 2015) may be cited
freely; its code may not be read. **Safe to read:** Faust (LGPL), ChucK, Houdini
VEX docs.

### 1.2 Bare names. No sigils. Ever.

An earlier draft used a VEX-style `@` sigil. The owner **removed it**. Plain
ASCII, no special characters, in every example, every doc, every test fixture,
every error message.

| Wrong | Right |
|---|---|
| `@P.y += bass * 2` | `P.y += bass * 2` |
| `@Cd = vec3(1,0,0)` | `Cd = vec3(1,0,0)` |
| `v@P` / `f@heat` | `P` / `heat` |

This applies to the `seed` surface you design in §7 Phase 3: no `@seed`, no
`$seed`, no `#seed`.

### 1.3 Rate is inferred, never declared

No `@rate` keyword, no `krate` parameter, no domain annotation on a binding.
Relevant here because `rand(t)` is **frame-domain**, and it is frame-domain
because it reads `t`, not because anybody wrote that down.

### 1.4 The other invariants

| # | Invariant | Source |
|---|---|---|
| 1 | **`Expression::Evaluate` keeps its exact signature.** All three call sites stay unchanged. | `field-compiler` §0.3, §10 |
| 2 | **The typed IR is the durable asset, not the syntax.** A change that makes the IR harder to retarget to GLSL / C++ / WASM is a regression even if every test passes. | `field-compiler` §0.2, §6.4 |
| 3 | **Every intrinsic has ONE semantic definition that every backend matches.** Not "whatever GLSL does". | `field-compiler` §6.4 |
| 4 | **A failed compile never blanks the output.** The last working value keeps running. | `field-compiler` §0.4, §7 |
| 5 | **Field is one primitive:** a kernel is a body of code run once per element of a domain. | `field-language` §1 |
| 6 | **A test that cannot fail is not a test.** Every case must be observed failing at least once — break it deliberately, watch `FAIL`, then fix it. | `field-testing` §0.2 |
| 7 | **The corpus is the contract.** | `field-testing` §0.3 |
| 8 | **Real-time safety:** no heap allocation past init in generated or runtime code, no recursion, no unbounded loops, every value's size known at compile time. | `field-realtime` §1 |
| 9 | **Only step 1 touches existing code.** Step 2 is the *one* deliberate exception, and only inside the intrinsic's body — see §4. | `field-compiler` §10 |

### 1.5 Do not

- Do not change the **arity** or the **argument order** of `rand`/`noise`/`sh`.
  The 0/1/2/3-argument overload shapes in §5.1 are saved-patch surface.
- Do not touch `vec2/3/4` or rank polymorphism. That is **step 3**
  (`docs/plans/field/step-03-vectors-and-rank.md`).
- Do not touch the patch file format, `Patch::Write`/`Read`, or the `expr` /
  `glob` line grammar.
- Do not resolve the §6.3 **OPEN** seed question silently. It becomes a
  permanent wire format.
- Do not commit or push unless the owner asks. (Except: §7 Phase 5 requires the
  work be *staged as two separate commits locally*; that is not a push.)

---

## 2. Goal

Replace the bodies of the `rand`, `noise` and `sh` intrinsics with a single
integer-hash function of `(t, seed)` that is bit-identical on macOS and Windows,
bit-identical between the CPU bytecode VM and every future backend, and
decorrelated enough that two calls at nearby `t` are actually independent —
without changing their arity, their argument order, their `[min,max]` scaling, or
their frame-domain placement. The two real defects being fixed are **(a)
autocorrelation** — today's `rand` is a sum of three sines and is a smooth wander,
not a random variable — and **(b) the total absence of a seed**, which makes two
`rand()` calls in one patch return the identical number and makes the shipped
`rand_glide` preset a no-op (§5.3). **This is a deliberate visible break:** saved
patches using `rand`/`noise`/`sh` will look different afterwards. The step is not
done until that break is in a release note and the `random` half of step 1's
corpus has been re-baselined **in its own reviewable commit**, separate from the
algorithm change.

```
 today                                         after step 2
 ─────                                         ────────────
 rand(t) = (sin τ + sin 1.618τ + sin 2.718τ)/6 + 0.5     rand(t, seed) = Hash(quantise(t), seed)
          └─ smooth, autocorrelated, no seed ─┘                       └─ integer, decorrelated,
                                                                          seeded, bit-exact ─┘
 sh(t)   = |fmod(sin(floor(t·speed)·123.456)·43758.5453123, 1)|
          └─ float-hash, no seed, and NOT fract() ─┘        sh(t, seed)  = Hash(floor(t·speed), seed)
```

---

## 3. Files to read first, and why

### Skills — the authoritative contract

| File | Why |
|---|---|
| `.claude/skills/field-language/SKILL.md` | **§12 is the spec for this step** — it already records what the code really does, reproduces the replacement algorithm, and carries the OPEN seed question. §4 the no-sigil rule (constrains the `seed` surface). §10 the operator table |
| `.claude/skills/field-compiler/SKILL.md` | §3 numeric precision (`double` throughout, narrow once); §6.4 **one semantic definition per intrinsic, matched by every backend** — the whole determinism half of this step; §7 error/retry policy; §10 the caching trap |
| `.claude/skills/field-testing/SKILL.md` | **§4 is the re-baselining procedure** and it is not optional; §2 the corpus record shape; §3 the harness sections; §6 step 2's exit row |
| `.claude/skills/field-realtime/SKILL.md` | §1 the diff checklist — run it against your own diff; §3 where allocation hides |
| `.claude/skills/ship-infinite/SKILL.md` | how a release note actually gets written here: `driver.sh whatsnew <tag>` harvests **raw commit subjects**, so the commit subject *is* the candidate bullet (§7 Phase 6) |
| `.claude/skills/run-infinite-hygiene/SKILL.md` + `driver.sh` | the gate. `TIER1_CHECKS` at `driver.sh:79`, `FULL_TESTS` at `driver.sh:173` |
| `.claude/skills/git-branch-workflow/SKILL.md` | branch before you start |

### Real source — the code wins over any skill

| Path | Why |
|---|---|
| `src/core/Expression.cpp:166-194` | the **whole** `rand`/`noise` body. Read it, do not trust §5.1's transcription — verify it |
| `src/core/Expression.cpp:195-223` | the **whole** `sh` body |
| `src/core/Expression.cpp:425` | `outValue = (float)result;` — the single narrowing point, and the reason everything above it is `double` |
| `src/core/Expression.h:30-33` | the documented function list. `rand noise sh` are in it; the header **is** the spec of what must not silently change |
| `src/core/ExprGlobals.cpp:88-95` | the four shipped **Random & Noise presets**. `rand_glide` (`:93`) is degenerate today — §5.3. These ship in the binary and their behaviour changes |
| `src/main.cpp:51669` | the in-app **language reference** line: `rand(speed) rand(min,max,speed) sh(min,max,speed)`. It does not mention `noise`. If the surface grows a `seed`, this line is wrong the moment you land |
| `src/core/Transport.cpp:9-32` | where `t` comes from: `mAudioSecondsOffset + counter/sampleRate`. **Verify for yourself whether it can ever be negative or unbounded** before you rely on either |
| `src/nodes/AnalyzeNodes.cpp:172-179` | the third `Expression::Evaluate` call site; `min`/`max` are **bound variables** here, not functions. Relevant because a `seed` that is an *identifier* rather than a literal collides with a binding set you do not control |
| `CMakeLists.txt:218` | where `src/core/Expression.cpp` is listed; new `.cpp` files go beside it |
| `CMakeLists.txt:467-469` | MSVC compiles `src/main.cpp` at **`/Od`** while every other TU is optimised. Your fixture lives in `main.cpp`; the hash lives elsewhere. Relevant to trap §8.4 |
| `docs/CODE_STANDARDS.md` | house style |

---

## 4. Files to create / modify

### Create

| Path | Contents |
|---|---|
| `src/core/field/FieldRandom.h` / `.cpp` | `Xorwise`, the `(t, seed)` hash, and the three intrinsic bodies. **One implementation, shared by every backend** (`field-compiler` §6.4). No other file may contain a second copy of the hash |
| `docs/plans/field/step-02-release-note.md` | the exact release-note bullet, verbatim, so whoever cuts the next tag cannot miss it (§7 Phase 6) |

### Modify

| Path | Change |
|---|---|
| step 1's intrinsic table (`src/core/field/FieldIR.*` or wherever step 1 put it) | point the `rand`/`noise`/`sh` entries at `FieldRandom`; mark all three **non-foldable** (§8.11) and **frame-domain-seeded** |
| step 1's corpus data file | **`random` set only.** Two commits — §7 Phase 5 |
| `src/main.cpp:51669` | the in-app language reference line, **only if** §6.3's answer adds surface syntax |
| `src/core/ExprGlobals.cpp:88-95` | the four presets, **only if** their text must change to stay meaningful (§5.3). Get the owner's answer first |
| `CMakeLists.txt` (~line 218) | add `src/core/field/FieldRandom.cpp` |
| `docs/plans/field/step-02-release-note.md` | new |

### Must not be modified

| Path | Why |
|---|---|
| `src/main.cpp:37507`, `src/core/ExprGlobals.cpp:72`, `src/nodes/AnalyzeNodes.cpp:179` | the three `Expression::Evaluate` call lines. A diff that touches one has failed the step's premise |
| `src/core/Expression.h` — the **declaration** at `:44-47` | comments may be updated; the signature may not |
| the `deterministic` half of the corpus | **one changed value there is a bug in your change, not an expected break** (`field-testing` §4.2) |
| `src/core/Patch.*` | no file-format change |

---

## 5. The current reality — measured, not assumed

> **The brief's framing is stale, and you must not implement against it.** The
> design brief (§13) says the functions "must become pure functions of `(t, seed)`
> — no hidden counter", which reads as though a hidden counter exists. **There is
> no counter.** `field-language` §12 already records the correction; the code
> confirms it. Verify this yourself with `sed -n '166,223p' src/core/Expression.cpp`
> before writing a line. **The real problem is autocorrelation and the missing
> seed. Do not implement a fix for a phantom.**

### 5.1 What `Expression.cpp` does today — the actual code

`rand` and `noise` are the **same function** (`Expression.cpp:166`), taking 0–3
arguments:

```cpp
// src/core/Expression.cpp:166-194
if (name == "rand" || name == "noise")
{
   double minVal = 0.0;
   double maxVal = 1.0;
   double speed  = 1.0;
   if      (a.size() == 1) { speed = a[0]; }
   else if (a.size() == 2) { minVal = a[0]; maxVal = a[1]; }
   else if (a.size() == 3) { minVal = a[0]; maxVal = a[1]; speed = a[2]; }
   else if (!a.empty())
   {
      s.Fail(name + "() expects 0 to 3 arguments (e.g. rand(speed) or rand(min, max, speed))");
      return 0.0;
   }
   const double tau_t = s.t * speed;
   const double n = (sin(tau_t) + sin(tau_t * 1.618033988749895) + sin(tau_t * 2.718281828459045)) / 6.0 + 0.5;
   return minVal + (maxVal - minVal) * n;
}
```

`sh` (`Expression.cpp:195-223`) has the identical overload shape and a
float-hash body:

```cpp
// src/core/Expression.cpp:195-223  (argument decoding identical to rand/noise)
   const double seed = floor(s.t * speed) * 123.456;
   const double frac = fabs(fmod(sin(seed) * 43758.5453123, 1.0));
   return minVal + (maxVal - minVal) * frac;
```

| Fact | Value |
|---|---|
| Hidden state / counter | **none.** Both are pure functions of `s.t` and their arguments |
| `rand` vs `noise` | **the same function.** Not aliases — one `if` matches both names |
| Overload shapes | `f()`, `f(speed)`, `f(min, max)`, `f(min, max, speed)`. 4+ args errors |
| `min > max` | legal and inverts — `sh(1, 0, 2)` runs 1→0 |
| `sh`'s local `seed` (`:220`) | a **hash input**, not a user seed. Do not mistake it for one |
| Everything is `double` | narrowed once at `Expression.cpp:425` |

### 5.2 Measured values — reproduce this table before you edit anything

Probe built against the unmodified `src/core/Expression.cpp`, siblings
`{lo:0, hi:1}`, globals `nullptr`, printed at `%.17g`:

| Expression | t=0 | t=0.25 | t=1.25 | t=2 | t=10 |
|---|---|---|---|---|---|
| `rand()` | `0.5` | `0.71157163381576538` | `0.76586776971817017` | `0.51098603010177612` | `0.48138362169265747` |
| `noise()` | `0.5` | `0.71157163381576538` | `0.76586776971817017` | `0.51098603010177612` | `0.48138362169265747` |
| `rand(2)` | `0.5` | `0.86345314979553223` | `0.55055892467498779` | `0.23975218832492828` | `0.6508219838142395` |
| `noise(2)` | `0.5` | `0.86345314979553223` | `0.55055892467498779` | `0.23975218832492828` | `0.6508219838142395` |
| `rand(0,1,2)` | `0.5` | `0.86345314979553223` | `0.55055892467498779` | `0.23975218832492828` | `0.6508219838142395` |
| `sh(0,1,4)` | `0` | `0.12977094948291779` | `0.17868475615978241` | `0.94377344846725464` | `0.44748982787132263` |
| `sh(1,0,2)` | `1` | `1` | `0.96006870269775391` | `0.89347875118255615` | `0.099620260298252106` |

`rand()`, `noise()`, `rand(0,1,2)` and `noise(2)` agreeing exactly is not a
coincidence — it is defect (b).

### 5.3 Defect (b) has a shipped witness: `rand_glide` is a no-op

`src/core/ExprGlobals.cpp:93` ships this preset:

```
{ "Random & Noise", "rand_glide",
  "lerp(sh(1, 0, 2), sh(1, 0, 2), smoothstep(0, 1, mod(t * 2, 1)))",
  "Smooth gliding random walk between targets" }
```

Both `sh(1, 0, 2)` calls have identical arguments and identical `t`, so they
return the identical number, and `lerp(x, x, k) == x` for every `k`.
**Measured:** the preset's value equals a bare `sh(1, 0, 2)` at every `t` in
§5.2's sweep — there is no glide, and the `smoothstep` is dead arithmetic.

> This is the single clearest argument for a seed, and it is also a **decision
> the owner has to make**: after step 2 this preset either (i) starts working, if
> the seed can differ between two textually identical call sites (§6.3 option c),
> or (ii) stays a no-op unless its text is edited to pass different seeds. Bring
> the measurement and the question together. Do not silently rewrite a shipped
> preset.

### 5.4 Defect (a), quantified

The three-sine sum `(sin τ + sin 1.618τ + sin 2.718τ)/6 + 0.5` is a smooth,
band-limited signal, not a random variable:

| Property | Measured over `t ∈ [0, 1000)`, step 0.0005 |
|---|---|
| Observed range | `[0.0036293648485448693, 0.9998575633825928]` — **not** `[0, 1]` |
| Continuity | continuous and differentiable everywhere; consecutive frames at 60 fps differ by ~`0.008` at `speed = 1` |
| Distribution | strongly centre-weighted (a sum of three sines), not uniform |

It is a perfectly good **LFO**. It is not `rand`. That is the defect.

### 5.5 Discrepancy — the skills describe `sh` as `fract`, and the code does not

`field-language` §12 and `field-testing` §4 both describe `sh` as
`fract(sin(floor(t·speed)·123.456) · 43758.5453)`. The code
(`Expression.cpp:221`) is `fabs(fmod(sin(...) * 43758.5453123, 1.0))`.

| | `fabs(fmod(x, 1.0))` — the code | `x - floor(x)` — `fract` |
|---|---|---|
| `x = -35179.1298` (this is the real value at step index 1) | `0.129770945` | `0.870229055` |
| Agreement over 200 000 step indices | **they differ for 50.0% of them** | |

The constant differs too: the skills write `43758.5453`, the code writes
`43758.5453123`. Measured at step index 1: `0.129761057` vs `0.129770945` — a
difference in the 5th decimal, which is corpus-visible.

> **The code wins.** Record the encoded-old-behaviour cases in the corpus from
> the **code**, never from the skill's prose, and fix `field-language` §12 and
> `field-testing` §4 in the same commit.

---

## 6. The replacement algorithm

### 6.1 Reproduced in full, from `field-language` §12 / brief §13

```cpp
static int64_t Xorwise(int64_t x) {
   int64_t a = (x << 13) ^ x;
   int64_t b = (a >> 17) ^ a;
   return (b << 5) ^ b;
}

static float TimeToRand(double t) {
   double frac = std::fmod(t / 300.0, 1.0);
   int64_t seed = Xorwise((int64_t)(frac * 536870912.0));   // 2^29
   int64_t m = seed % 536870912;
   if (m < 0) m += 536870912;    // Haskell's mod is non-negative; C++ % is not
   return (float)(m / 536870912.0);
}
```

This is a **reimplementation from a published algorithm description**, not a
copy of GPL source. It is reproduced here so you never need to go looking.

### 6.2 What it actually does — measured, so you know what you are shipping

Probed directly (not through `Expression`), sweeps of 300 000 points:

| Property | Measured |
|---|---|
| Range | `[0, 0.999994695]` — never negative, never ≥ 1 |
| **Period** | **300 seconds.** `fmod(t/300, 1)` wraps, so `TimeToRand(0) == TimeToRand(300) == TimeToRand(600) == 0`. A patch left running repeats its randomness every 5 minutes |
| Resolution | 2^29 distinct seeds over 300 s = **1 789 570 per second**. Step per frame at 60 fps: **29 826** seed units (plenty distinct). Step per sample at 48 kHz: **37.28** (still distinct, but only just — relevant to step 9) |
| `TimeToRand(1.25)` | `0.278840154` |
| Determinism | the only floating-point operations are `fmod`, one multiply, one truncating cast, and one divide by **2^29 (exact in `double`)`. Everything between is integer |

### 6.3 OPEN — how does `seed` enter? **Ask the owner. Do not decide in code.**

> **The discrepancy you must surface:** the requirement is that `rand`, `noise`
> and `sh` become pure functions of **`(t, seed)`**. The algorithm above is
> `TimeToRand(double t)` — **it has no `seed` parameter at all.** The brief and
> the skill both state the requirement and then reproduce a snippet that cannot
> satisfy it. Whichever way the seed is threaded becomes the **permanent wire
> format for every saved patch**: every `.inf` file written after this lands
> encodes values produced by that exact mixing function, and changing it later is
> a second visible break. Treat it as a format decision, not an implementation
> detail.

| Option | Mixing | Measured consequence | Cost |
|---|---|---|---|
| **(a)** fold the seed into the time word before hashing — `Xorwise(word ^ seedWord)` | one extra XOR | intermediate stays non-negative for the seeds probed, so the `m < 0` guard still never fires; **decorrelation between adjacent seeds is weak** — a single XOR before one Xorwise round is not a mixer | cheapest; weakest |
| **(b)** two rounds — `Xorwise(Xorwise(timeWord) ^ Xorwise(seed))` | one extra `Xorwise` | **measured: 150 000 of 300 000 outputs are negative without the `m < 0` guard** (§8.1). Good decorrelation | one extra round; **makes the sign trap live** |
| **(c)** derive an implicit seed from the **source position of the call site** | no user syntax at all | `rand_glide` (§5.3) starts working with no edit; two `rand()` calls in one program differ automatically | ergonomically the nicest and **the worst for reproducibility**: inserting a blank line above a `rand()` changes its output, and a saved patch's rendering then depends on its whitespace |
| **(d)** an explicit trailing argument — `rand(min, max, speed, seed)` | 4th argument | explicit, reproducible, and it is a **4th argument on a function whose 4-argument form currently errors** (`Expression.cpp:186-190`), so no saved patch can collide | widens the documented arity at `Expression.h:33` and `src/main.cpp:51669` |

**What the prompt-writer recommends, and why it is still the owner's call:**
**(b) + (d)** — a two-round mix so the seed genuinely decorrelates, reached
through an explicit optional 4th argument so it is reproducible and greppable, with
the seed defaulting to `0` when omitted. That combination keeps every existing
0–3-argument call site legal, keeps saved patches reproducible under whitespace
edits, and makes the `m < 0` guard load-bearing rather than decorative. **(c)
must be rejected explicitly rather than by omission** — write the reason down.

**Whatever is chosen, it constrains the no-sigil rule (§1.2):** the seed is
`rand(0, 1, 2, 7)` or `seed`-as-a-bare-identifier. Never `@seed`, never `$7`.

### 6.4 OPEN — 64-bit integers do not exist in the GLSL backend

The determinism requirement (§7 Phase 4) says the same `(t, seed)` must give the
same float on the CPU bytecode VM **and on the later GLSL pixel backend**.
`field-compiler` §6.2 requires that backend to emit **`#version 150`**.

| Fact | Consequence |
|---|---|
| GLSL 1.30+ has bitwise `& \| ^ << >>` | `Xorwise`'s *shape* is expressible |
| GLSL 1.50 has **no 64-bit integer type** (`int64_t` needs `ARB_gpu_shader_int64`) | `int64_t Xorwise` is **not lowerable** |
| GLSL `int` is 32-bit; signed overflow in GLSL is undefined, `uint` wraps | a 32-bit port must be `uint`, and `x << 13` with `x < 2^29` overflows 32 bits |

> **Ask the owner, and cost both:**
> **(i)** keep the hash 64-bit and accept that pixel-domain `rand` is either a
> **documented divergence** or **refused at compile time** in step 7 — the
> refusal is recoverable, a silently-different image is not;
> **(ii)** redesign the hash to run entirely in **32-bit unsigned wrapping**
> arithmetic so every backend agrees bit-for-bit — this diverges from the
> reproduced algorithm and must be re-validated for distribution, but it is the
> only option under which `field-compiler` §6.4's "one semantic definition every
> backend matches" is literally true.
>
> This question is **not** deferrable to step 7: the answer decides the integer
> width you write today, and the width is the wire format.

---

## 7. Step-by-step procedure

### Phase 0 — prove the ground you are standing on (before any edit)

1. `sed -n '166,223p' src/core/Expression.cpp` — read the real bodies. Confirm
   §5.1's transcription character by character.
2. Reproduce §5.2's table with your own probe against the **unmodified** binary,
   at `%.17g`. If a single value differs, stop: your build is not the one this
   prompt was measured against, and every number below is suspect.
3. Confirm step 1's corpus exists and is **already split** into `deterministic`
   and `random` sets (`field-testing` §4.1 required the split *before* step 2).
   If it is not split, **split it and commit that split on its own, first** —
   splitting and re-baselining in one commit destroys the audit trail.
4. Record the corpus commit SHA. You will diff against it in §9.

### Phase 1 — get §6.3 and §6.4 answered

Both are wire-format decisions. Put them to the owner **with the measurements
from §5.3, §6.2 and §8.1 attached** — a bare list of options is not a question.
Do not start Phase 2 without answers. Write the answers into this file's §6.3 and
§6.4 as the recorded decision, in the same commit as the implementation.

### Phase 2 — implement the hash in exactly one place

`src/core/field/FieldRandom.h` / `.cpp`:

```
   Hash(timeWord, seedWord)  ──▶  a value in [0, 1)
        │
        ├─ rand(min, max, speed[, seed])   ──▶  Hash(quantise(t * speed), seed)   scaled into [min, max]
        ├─ noise(...)                      ──▶  the same entry point as rand (they are one function today; keep them one)
        └─ sh(min, max, speed[, seed])     ──▶  Hash(floor(t * speed),    seed)   scaled into [min, max]
```

| Requirement | |
|---|---|
| **One copy of the hash.** | Every backend calls it. A second copy in the emitter is `field-compiler` §6.4's failure mode with a different face |
| All integer work in **`uint64_t`** (or `uint32_t` if §6.4 answers (ii)) | never `int64_t`, never `long` — §8.2, §8.3 |
| Argument decoding **byte-identical** to `Expression.cpp:168-190` | including `min > max` inverting and the 4+-argument error string |
| The final division is by a **power of two** | exact in `double`; see §8.5 |
| `rand` and `noise` stay the **same function** | `Expression.cpp:166` matches both names in one `if`. Do not split them |
| No allocation, no `std::string`, no branches on user data in the hash body | `field-realtime` §1 rules 1–3 |

Then point step 1's intrinsic table at it, and **mark all three intrinsics
non-foldable** (§8.11) and frame-domain-seeded (they read `t`).

### Phase 3 — the `seed` surface (only if §6.3 chose (d))

- The 4th argument. Default `0` when omitted.
- Update the documented list at `src/core/Expression.h:30-33`.
- Update the in-app language reference at `src/main.cpp:51669` — it currently
  reads `rand(speed) rand(min,max,speed) sh(min,max,speed)` and does not mention
  `noise` at all. Add `noise` while you are there.
- No sigil (§1.2). A seed is a plain number or a plain bare identifier.

### Phase 4 — determinism hardening

The contract: **the same `(t, seed)` produces the same `float` on macOS and on
Windows, and on the CPU bytecode VM and on every later backend.** That forbids:

| Forbidden | Why |
|---|---|
| `long double` anywhere in the path | 80-bit on x86 System V, 64-bit on MSVC — different results by construction |
| relying on x87 excess precision | not an issue on x64 (SSE2), but a 32-bit build would reintroduce it. Do not write code whose correctness depends on the register width |
| `-ffast-math`, `/fp:fast`, `-Ofast` | reassociation changes results. **Verified: `CMakeLists.txt` sets none of these today** (`grep -n 'fast-math\|fp:fast' CMakeLists.txt` → nothing). Keep it that way, and do not add a per-file flag |
| **unguarded FP contraction (FMA)** | clang contracts `a*b + c` into an FMA within a statement **by default**; MSVC's x64 codegen may too. `minVal + (maxVal - minVal) * n` is exactly that shape. Either write the scale so contraction cannot change it, or set `-ffp-contract=off` on this TU and say so in a comment. §8.4 |
| `std::mt19937`, `std::random_device`, `rand()`, `std::uniform_real_distribution` | all carry state and/or an implementation-defined distribution. The whole point of this step is that there is no state |
| `long` as the integer type | 32-bit on MSVC, 64-bit on the Itanium ABI. Use `uint64_t`/`uint32_t` from `<cstdint>` |
| signed shifts | §8.2, §8.3 |

### Phase 5 — re-baseline, as **two separate commits**

This is a hard requirement, not a style preference (`field-testing` §4).

```
   commit N     "Field step 2: <the algorithm change>"
                  src/core/field/FieldRandom.*        <- new
                  src/core/field/<intrinsic table>    <- repointed
                  CMakeLists.txt
                  src/core/Expression.h               <- comments only
                  src/main.cpp:51669                  <- help text, if Phase 3 applied
                  .claude/skills/field-language/SKILL.md, field-testing/SKILL.md  <- §5.5 fixes
                  ── the `random` corpus set is EXPECTED TO FAIL at this commit ──

   commit N+1   "Field step 2: re-baseline the random corpus set"
                  <the corpus data file>              <- and NOTHING ELSE
```

Why: the re-baseline commit's diff *is* the audit of the break. Every changed
number is visible on one screen with no code around it. Bundled into commit N it
is invisible, and a real regression that happened to change a `random` value
walks straight through.

Record commit N's SHA in the corpus file's own header comment
(`field-testing` §4.3).

Then add these harness cases to `INFINITE_FIELDTEST` (a new section, or extend
section A — `field-testing` §4.4-4.5):

| Case | Assert |
|---|---|
| non-negativity | the hash never returns a negative value over a `t` sweep **that includes the range where the intermediate goes negative**. §8.1 tells you where that is; a sweep of `t ∈ [0, 300)` alone does **not** exercise it |
| range | always in `[0, 1)` before scaling; never exactly `1.0` |
| same seed reproduces | two calls, same `(t, seed)` → bit-identical `float` |
| different seeds differ | two calls, same `t`, different seed → different value, over a sweep, with a stated collision budget |
| decorrelation | adjacent `t` at 60 fps produce values whose sample correlation over a 10 000-point sweep is below a stated threshold. **This is the case that would have caught today's three-sine defect**, and today's implementation must be observed failing it |
| period | `Hash(t) == Hash(t + 300)` — assert the period *deliberately*, so nobody "fixes" it by accident and shifts every value |
| `sh` step boundaries | `sh` is constant across a step and changes at `floor(t·speed)` boundaries |
| arity unchanged | `rand(1,2,3,4,5)` still produces the **same error string** as before (unless Phase 3 widened it to 4) |
| deterministic set untouched | the whole `deterministic` corpus still passes bit-exactly |

**A test that cannot fail is not a test.** For each row: break it deliberately,
watch `FAIL`, fix it. In particular, delete the `m < 0` guard, watch the
non-negativity case fail, and put it back.

### Phase 6 — the release note (required, not optional)

`ship-infinite`'s `driver.sh whatsnew <tag>` harvests **raw commit subjects**
between tags. So:

1. Commit N's **subject line must itself read as the user-facing bullet**, e.g.
   `Field step 2: rand/noise/sh are now seeded hash functions - patches using them will look different`.
2. Write `docs/plans/field/step-02-release-note.md` containing the exact bullet
   text and a one-line "what a user will notice" sentence, so whoever cuts the
   tag pastes rather than paraphrases.
3. The bullet must say **that saved patches change**, not just that randomness
   improved. A user who opens an old patch and finds it different needs to find
   that sentence.

### Phase 7 — verify, then the gates

Order matters: build → the Field fixture → hygiene → the `field-realtime` §1 diff
checklist → the Desktop copy this project requires. §9 is the command list.

---

## 8. Traps — each names the bug it prevents

### 8.1 `if (m < 0)` is dead code today and load-bearing tomorrow. Do not delete it.

Measured against the §6.1 snippet exactly as written:

| Input range | Negative `Xorwise` results | Negative `m` |
|---|---|---|
| `t ∈ [0, 300)`, 3 000 000 samples | **0** | **0** |
| `t ∈ [-300, 0)`, 300 000 samples | **0** | **0** |
| option (a), seed XORed into the time word | **0** | **0** |
| **option (b), two-round `Xorwise(Xorwise(tw) ^ Xorwise(seed))`** | **150 000 / 300 000 (50.0%)** | **150 000 / 300 000** |

The reason `t ≥ 0` never trips it: the time word is under 2^29, so
`(x<<13)^x` never sets the sign bit, and every later step preserves that. The
moment a **full-width** seed enters the state — which is exactly what option (b)
does — half the outputs go negative.

> **Bug prevented, in both directions.** (i) A session that "cleans up
> unreachable code" deletes the guard, option (b) later lands on top, and half of
> every random value in the app becomes negative — silently, because a negative
> `rand()` scaled into `[min,max]` still produces a plausible-looking number.
> (ii) A session that tests the guard only over `t ∈ [0, 300)` sees it never
> fire and concludes it is untested-because-unneeded. **The non-negativity
> fixture must sweep the seed axis, not just the time axis.**

### 8.2 `x << 13` on a negative `int64_t` is undefined behaviour in C++17

`Xorwise` as reproduced takes `int64_t`. Left-shifting a negative signed value is
**UB** in C++17 (it became defined only in C++20), and `>>` on a negative signed
value was implementation-defined before C++20. The repo is `CMAKE_CXX_STANDARD 17`
(`CMakeLists.txt:38`).
> **Bug prevented:** UB that "works" under clang `-O0` and produces a different
> value under MSVC `/O2`, breaking the macOS/Windows determinism requirement with
> no diagnostic anywhere. **Do the arithmetic in `uint64_t`** and cast once at the
> boundary. The `% 536870912` then becomes a mask (`& (536870912u - 1)`) and the
> `m < 0` guard becomes structurally unnecessary — **say so in a comment rather
> than silently dropping it**, and keep the fixture case from §7 Phase 5.

### 8.3 `long` is not 64 bits on Windows

MSVC's `long` is 32-bit; the Itanium ABI's is 64-bit. Use `<cstdint>` fixed-width
types everywhere in this file.
> **Bug prevented:** the hash silently truncates on Windows only, and every random
> value in the app differs between platforms — visible as "the same patch renders
> differently on my laptop".

### 8.4 Floating-point contraction (FMA) is on by default in clang

`minVal + (maxVal - minVal) * n` is the exact shape a compiler contracts into a
single FMA. clang's default is `-ffp-contract=on` (contraction allowed within a
statement); MSVC's x64 `/fp:precise` may also emit FMA. The contracted and
uncontracted results differ in the last ULP.
> **Bug prevented:** a corpus value that is bit-exact on macOS and one ULP off on
> Windows, which then gets "fixed" by loosening the corpus to an epsilon — and the
> corpus stops being able to detect a real regression. **The integer hash itself
> is immune** (its only FP steps are `fmod`, one multiply, one truncation and a
> divide by 2^29, which is exact). Only the `[min,max]` scale is exposed. Either
> compile the file with `-ffp-contract=off` or write the scale so contraction
> cannot change it, and state which in a comment.

### 8.5 The divide must stay a power of two

`536870912.0` is 2^29, so `m / 536870912.0` is exact in `double` on every
IEEE-754 platform. A "tidier" `/ 536870911.0` (to make the range inclusive of 1)
or a multiply by a precomputed reciprocal is not.
> **Bug prevented:** a last-bit platform divergence introduced by a cosmetic edit.

### 8.6 Do not reach for a stateful RNG

`std::mt19937`, `std::minstd_rand`, `std::random_device`, C `rand()`,
`std::uniform_real_distribution` — all forbidden. `mt19937` carries 2.5 KB of
state (rule: no state), `uniform_real_distribution`'s output is
**implementation-defined** and differs between libstdc++, libc++ and MSVC's STL
even from the same engine and seed, and `rand()` is a process global.
> **Bug prevented:** exactly the macOS/Windows divergence the determinism
> requirement exists to forbid — and a `state` that step 6 would then have to
> reason about.

### 8.7 `sh`'s local variable is named `seed` and is not a user seed

`Expression.cpp:220`: `const double seed = floor(s.t * speed) * 123.456;`. It is
the hash input.
> **Bug prevented:** a session wires the new user `seed` parameter into that
> local, producing `floor(t*speed)*123.456 + userSeed`, which makes nearby seeds
> alias onto nearby time steps — the worst possible correlation structure.

### 8.8 The skills describe `sh` with `fract()`, and the code uses `fabs(fmod())`

They differ for **50.0%** of step indices (§5.5). The constant differs too
(`43758.5453` vs `43758.5453123`).
> **Bug prevented:** the "old behaviour" corpus records — the ones the
> re-baseline diff is measured against — encode the *skill's* description rather
> than the shipped behaviour, so the diff shows a break that never happened and
> hides one that did. **Harvest golden values by running the binary, never by
> transcribing prose.** Fix `field-language` §12 and `field-testing` §4 in the
> same commit.

### 8.9 The arity and the overload shapes are saved-patch surface

`rand(2)` means `rand(speed=2)`, not `rand(seed=2)`. `sh(1, 0, 2)` means
`min=1, max=0, speed=2` and legitimately runs **downwards**.
> **Bug prevented:** reinterpreting the 1-argument form as a seed silently
> changes the meaning of every `rand(speed)` in every saved patch, with no error
> and no visible cause. If a seed is added it goes in a **new** position (§6.3
> option d), where nothing can currently be.

### 8.10 The break is only allowed in the `random` corpus set

`field-testing` §4.2: a single changed value in the `deterministic` set is **a bug
in your change**, not an expected break.
> **Bug prevented:** an intrinsic-table edit that accidentally repoints `mod` or
> `smoothstep`, discovered six months later.

### 8.11 A constant folder will freeze `rand(0, 1, 2)` at compile time

`rand(0, 1, 2)` has three literal arguments. Step 1's IR does constant folding in
`double` (`field-compiler` §3). A folder that sees an all-literal-argument call
and folds it will bake in the value of `rand` **at the moment of compilation** —
and step 1 caches compiled programs, so it stays frozen for the life of the
process.
> **Bug prevented:** `=rand(0, 1, 2)` under a knob stops moving. Mark
> `rand`/`noise`/`sh` **non-foldable** in the intrinsic table, and add a fixture
> case that evaluates the same cached program at two different `t` values and
> asserts the results differ.

### 8.12 The compile cache must not key randomness into the program

Related to 8.11 and to step-01 §7.2. `t` is a **runtime binding**, not part of the
cache key. If the seed is a literal it may be baked into the program; if the seed
is an identifier it must be resolved through the binding set like any other name
— and `AnalyzeNodes.cpp:172-179` binds a 22-name set you do not control.
> **Bug prevented:** the Image Analyze node's custom formula gets a program
> compiled for `main.cpp`'s binding set and reads the wrong `seed`.

### 8.13 The in-app help text becomes a lie

`src/main.cpp:51669` documents `rand(speed) rand(min,max,speed) sh(min,max,speed)`
and never mentions `noise`. `src/core/Expression.h:30-33` documents the function
list.
> **Bug prevented:** a user reads the built-in language reference, writes a
> 3-argument `rand`, gets the new default seed, and has no way to discover the
> 4th argument exists.

### 8.14 The 300-second period is a property, not an accident

`fmod(t/300, 1)` means the sequence repeats every 5 minutes exactly
(measured: `TimeToRand(0) == TimeToRand(300) == TimeToRand(600) == 0`).
> **Bug prevented, in two directions:** (i) somebody "improves" it by removing
> the wrap, and every value in every patch changes a second time, for free;
> (ii) somebody ships an installation that runs for an hour and is surprised that
> the visuals loop. Assert the period in the fixture, and put it in the release
> note.

### 8.15 Do not add a backend-specific node to the IR

`field-compiler` §6.4. `GlslRand` must not exist. `rand` is a `Call` with one
semantic definition (§6.4's open question is precisely about making that
definition achievable on all backends).
> **Bug prevented:** the C++ and WASM backends have to special-case it, and
> retargetability — invariant 2, the durable asset — is gone.

### 8.16 `t` may be unbounded, and its provenance is not what you assume

`Transport::Seconds()` (`src/core/Transport.cpp:9-32`) returns
`mAudioSecondsOffset + counter/sampleRate` when a device is live, and a
separate offline path otherwise. **Verify for yourself** whether an offset can go
negative before writing code that assumes `t ≥ 0`. `fmod` handles both, but
`(int64_t)(negative)` truncates **toward zero**, so the negative half of the
sequence is a mirror of the positive half rather than a continuation.
> **Bug prevented:** a seek to a negative position (or a future feature that
> allows one) making `rand` symmetric around zero — a visible artefact with no
> obvious cause.

---

## 9. Machine-checkable exit criterion

Every command must pass. Run them in this order. Paste the **actual output** into
the commit message; do not paraphrase.

```bash
cd /Users/namansoni/infinte

# 0. Two commits, in the right order and with the right contents.
git log --oneline -2
#    ^ HEAD must be the re-baseline commit, HEAD~1 the algorithm commit.
git diff --name-only HEAD~1 HEAD
#    ^ must print EXACTLY ONE path: step 1's corpus data file. Nothing else.

# 1. The three Expression::Evaluate call sites are untouched.
grep -rn "Expression::Evaluate" src/
#    ^ exactly 3 hits: src/main.cpp, src/core/ExprGlobals.cpp, src/nodes/AnalyzeNodes.cpp

# 2. The public signature is byte-identical to main's.
git diff main -- src/core/Expression.h | grep -E '^[+-]\s*(bool Evaluate|const std::map|float& outValue|double t)'
#    ^ must print NOTHING.

# 3. Exactly one copy of the hash exists.
grep -rln "Xorwise" src/
#    ^ must print exactly one path: src/core/field/FieldRandom.cpp

# 4. No stateful RNG, no forbidden precision constructs, no signed shifts.
git diff main -- src/core/field/ | grep -nE 'mt19937|random_device|minstd|uniform_real|std::rand|srand|long double|__float128|\blong\b'
#    ^ must print NOTHING.
grep -nE '<<|>>' src/core/field/FieldRandom.cpp
#    ^ every operand must be an unsigned type. Read every hit; there is no
#      grep that proves this, so state in the commit message that you did.

# 5. No fast-math anywhere in the build.
grep -rn 'fast-math\|/fp:fast\|-Ofast' CMakeLists.txt
#    ^ must print NOTHING. (Verified empty before this change.)

# 6. It builds clean.
cmake --build build -j"$(sysctl -n hw.ncpu)" 2>&1 | tail -20

# 7. The Field harness. Every section line ends OK; no failure marker anywhere.
INFINITE_FIELDTEST=1 build/Infinite.app/Contents/MacOS/Infinite 2>&1 | tee /tmp/field-step2.log
grep -cE 'FAIL|BUG$|MISMATCH|SUSPECT' /tmp/field-step2.log
#    ^ must print 0.

# 8. The deterministic corpus set is byte-identical to its pre-step-2 state.
#    Replace <CORPUS_SHA> with the SHA recorded in Phase 0.4, and <CORPUS_PATH>
#    with step 1's corpus data file.
git show <CORPUS_SHA>:<CORPUS_PATH> | sed -n '/^# --- deterministic/,/^# --- random/p' > /tmp/det-before.txt
sed -n '/^# --- deterministic/,/^# --- random/p' <CORPUS_PATH>                          > /tmp/det-after.txt
diff /tmp/det-before.txt /tmp/det-after.txt
#    ^ must print NOTHING. A single line of output is a bug in the change.

# 9. The random set actually changed (this is the point of the step).
git diff HEAD~1 HEAD -- <CORPUS_PATH> | grep -c '^[+-][^+-]'
#    ^ must be > 0, and every line must be inside the `random` section.

# 10. The corpus fixture can actually fail (invariant 1.4.6).
#     Perturb one re-baselined value, re-run step 7, confirm FAIL, revert.
#     Then delete the `m < 0` guard (or the unsigned mask), re-run, confirm the
#     non-negativity case FAILs, restore it. Record both in the commit message.

# 11. Real-time diff checklist — field-realtime §1 rules 1-3 against the hash.
git diff main -- src/core/field/ | grep -nE 'new |malloc|push_back|resize|reserve|std::function|std::string|while *\('
#    ^ every hit must be justified in the commit message, or removed.

# 12. The release note exists and names the break.
test -f docs/plans/field/step-02-release-note.md && grep -in 'look different\|will change\|differ' docs/plans/field/step-02-release-note.md
#    ^ must print at least one line.
git log --format=%s -1 HEAD~1
#    ^ the algorithm commit's subject is the release-note bullet `whatsnew` will
#      harvest. Read it and confirm a user would understand it.

# 13. The full gate.
.claude/skills/run-infinite-hygiene/driver.sh

# 14. Project convention.
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

Plus these, which need a human but are still binary pass/fail:

| # | Check |
|---|---|
| 15 | The four `Random & Noise` presets (`src/core/ExprGlobals.cpp:88-95`) were each evaluated before and after, and the owner has seen both readouts. `rand_glide` specifically: state whether it is still a no-op (§5.3) |
| 16 | A saved patch containing `=rand(...)` under a knob loads, renders, and **moves** — a cached compiled program that has been constant-folded looks identical to a working one until you watch it (§8.11) |
| 17 | `INFINITE_FIELDTEST` is registered in `driver.sh`'s `TIER1_CHECKS` (line 79) and appears in the hygiene output |
| 18 | §6.3 and §6.4 have **owner answers**, written into this file, not decided in code |

---

## 10. Explicitly out of scope for step 2

| Not in this step | Where it belongs |
|---|---|
| `vec2` / `vec3` / `vec4`, swizzles, rank polymorphism, a vec-valued `rand` | **step 3** — `docs/plans/field/step-03-vectors-and-rank.md` |
| Any change to a **non-random** intrinsic — `mod`, `round`, `smoothstep`, `^` | never in this step. Step-01 §7.3–7.5 records why each of those is the way it is |
| The `element`, `pixel`, `sample` or `graph` domains | steps 4, 7, 9, 10 |
| Actually building the GLSL lowering of `rand` | step 7. §6.4 decides the **integer width** now because that is the wire format; the lowering itself is step 7's |
| A per-sample `rand` at 48 kHz | step 9. Note §6.2's measured resolution (37.28 seed units per sample) so step 9 inherits the number |
| `attrib`, `param`, `state` declarations | steps 5–6 |
| `reduce` / `map` / `resample` / `downsample` | step 8 |
| A Field **node** in the graph, an editor, a `ParamRef` registration | step 5 and `field-integration` |
| Changing `Expression::Evaluate`'s signature or its three call sites | never |
| Changing the patch file format or `Patch::NodeRecord` | never in this step |
| Deciding step-01's still-open `1e3` / `1.2.3` number-grammar question | step 1's brief; if it is still open, say so and do not touch it |
| Adding a new modulator **node** that produces randomness | a different piece of work with its own brief |

If you find a genuine bug in existing code while in here, **report it rather than
fixing it inline.**

---

## 11. Which earlier steps must be finished first

| Step | Why this step needs it |
|---|---|
| **1** — `Expression.cpp` → lexer / AST / typed IR / bytecode | there is no intrinsic table to repoint, no compile cache whose folding you must disable (§8.11), and — decisively — **no corpus**. Without step 1's frozen, split corpus, "only the random values changed" is an unverifiable claim, which is the one claim this entire step rests on |

Steps 3–10 depend on **this** step only insofar as they inherit its intrinsic
semantics; none of them is a prerequisite for it.

---

## 12. Report back with

1. The two commit SHAs and subjects, and the output of
   `git diff --name-only HEAD~1 HEAD` proving the re-baseline commit touches
   only the corpus.
2. §5.2's table reproduced against the **unmodified** binary, and the same table
   against the new one, side by side.
3. The owner's answers to §6.3 (how `seed` enters — and it is the permanent wire
   format) and §6.4 (64-bit hash with a pixel-domain divergence, or a 32-bit
   hash every backend can match).
4. The measured non-negativity sweep, including the seed axis, and the
   before/after of deliberately deleting the guard (§9 step 10).
5. Whether `rand_glide` (`ExprGlobals.cpp:93`) is still a no-op, and what the
   owner decided to do about it.
6. The exact release-note bullet.
7. Any further place where a skill or the design brief disagrees with the code —
   the **code wins**, and the skill gets fixed in the same commit. §5.5 and the
   brief's "no hidden counter" framing are the two already known.
