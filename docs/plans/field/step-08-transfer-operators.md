# Field build step 8 — transfer operators (`reduce`, `map`, `broadcast`, `resample`, `downsample`)

Self-contained implementation prompt. Read this whole file before writing code.
Line numbers are from `src/` at the commit this was written against — re-grep the
symbol name if a number has drifted; the **symbol** is authoritative, not the number.

Prerequisite reading, in this order: `.claude/skills/field-language/SKILL.md`,
`.claude/skills/field-compiler/SKILL.md` §5, `.claude/skills/field-domains/SKILL.md`
(the authoritative contract for this step), `.claude/skills/field-realtime/SKILL.md`,
`.claude/skills/field-testing/SKILL.md` §5–6.

---

## 1. Invariants — restated verbatim, they override anything you infer

**Clean room.** Infinite is MIT. **Never** open, read, grep or reference GPL
sources: Kronos (`bitbucket.org/vnorilo/k3`), Cmajor, SuperCollider, or
BespokeSynth (also at `/Users/namansoni/BespokeSynth` on this machine — do not
open it). The *Kronos paper* (Norilo, "Kronos: A Declarative Metaprogramming
Language for Digital Signal Processing", Computer Music Journal 39:4, 2015) is a
published paper and may be cited freely; its code may not be read. Safe to read:
Faust (LGPL), ChucK, Houdini VEX documentation, TidalCycles docs/papers.

**No sigils. Bare names. Ever.** An earlier draft used a VEX-style `@`. The owner
removed it.

| Wrong | Right |
|---|---|
| `@P.y += bass * 2` | `P.y += bass * 2` |
| `@Cd = vec3(1,0,0)` | `Cd = vec3(1,0,0)` |
| `v@P` / `f@heat` | `P` / `heat` |

**Rate is inferred, never declared.** No `@rate`, no `krate`, no domain
annotation on a binding.

**One primitive.** A kernel is a body of code run once per element of a domain.
A transfer operator that cannot be explained in those terms does not go in.

**Steps 2–10 are additive.** Only step 1 touched existing code. If this diff
edits an existing node, `src/core/Expression.cpp`, or any of the three
`Expression::Evaluate` call sites, something is wrong.

**Two cross-thread channels, and no third.** `ParamMailbox` (main → audio,
`src/audio/ParamMailbox.h`) and `MeterRing` (audio → main,
`src/audio/MeterRing.h`). This step adds neither a new atomic nor a new queue.

**A crossing between incomparable domains is always explicit.** The compiler
errors; it never inserts a `map` for you.

**`map` and `reduce` keep those names.** Norilo reports (p.45) that students
respond to "Map a bank, Reduce a cascade" where they struggle with the abstract
framing. Do not rename them to something more precise and less teachable.

---

## 2. Goal

Add the five domain transfer operators to the typed IR and to every backend that
exists at this point (bytecode VM for `frame`/`element`, GLSL for `pixel`).

| Operator | Direction | Written? | Example |
|---|---|---|---|
| `reduce` | many → one | yes | `bass = reduce.rms(in, 20, 200)` |
| `map` | one per element | yes | `map { … }` — a bank of filters in one line |
| `broadcast` | one → many | **never** — implicit, and there is no syntax for it | `P.y += amount` |
| `resample` | read domain A while standing in domain B | yes | `resample(audio, frame)` |
| `downsample` | run at a fraction of the ambient rate | yes | `downsample(lfo, 32)` |

The lattice they move across:

```
   graph  ⊑  frame  ⊑  element
   graph  ⊑  frame  ⊑  pixel
   graph  ⊑  frame  ⊑  sample

   element / pixel / sample are mutually INCOMPARABLE.
   Every crossing between them goes through `frame` (or a reduce to it).
```

Done means: two domains can exchange data, the cost of every crossing is stated
and matches measurement, and an illegal crossing is a compile error whose message
names the operator that would fix it.

**The `sample` half of this step is spec-only until step 9.** The sample domain
does not exist yet. Implement `reduce`'s sample→frame path in the IR and in the
error messages, and land the audio-thread half in step 9 against the design
recorded here. Do not build a stub audio path now.

---

## 3. Files to read first

| File | Why |
|---|---|
| `.claude/skills/field-domains/SKILL.md` | the contract this step implements — §1–§9 are the spec, §10 is the exit criterion |
| `.claude/skills/field-compiler/SKILL.md` §5 | the inference fixpoint these operators override (step 3 of the algorithm) |
| `.claude/skills/field-realtime/SKILL.md` §4 | the branching cost model; a branch inside a `map` or a `downsample` costs what §4 says, not less |
| `src/core/Expression.h` / `.cpp` | the step-1 output this builds on; **do not change its public API** |
| `src/audio/ParamMailbox.h` | `kMaxParams = 128` (:23), `Push` (:36, main only), `SmoothedValue` (:40, audio only, one-pole smoothed), `SetImmediate` (:44). The header comment (:11–20) explains why it is not a ring — read it before proposing any transport for a reduce |
| `src/audio/MeterRing.h` | the audio → main channel. `Write` (audio), `Read` / `ReadLatest` (main), `kCapacity = 4096`. **`ReadLatest` is the one a `reduce` wants** — its comment explains why one-per-frame `Read` against a per-block producer accumulates unbounded lag |
| `src/core/Mesh.h` | what an `element` reduce actually walks. `Vertex` (:10) is AoS; `vertexColor` (:45), `faceMask` (:29), `selectionGroup` (:37) are parallel SoA arrays |
| `src/core/GLUtil.h:34` | `CompileProgram(const char* fragSrc, std::string* outError)` — the pixel backend's only target |
| `src/nodes/AnalyzeNodes.cpp:179` | a real, shipping `element`-ish reduction over pixels with its own variable set (`lum`, `sat`, `hue`, `u`, `v`, `motion`, `max`, `min`) — the closest thing in the tree to what a `pixel` reduce must produce, and the reason a text-keyed IR cache would resolve `min`/`max` wrong |
| `src/nodes/PaletteNode.*` and the colour-extraction nodes | **read before answering the pixel→frame open question below** — they already solve a version of GPU→CPU readback |

---

## 4. Files to create / modify

**Create** (new, additive):

| Path | Contents |
|---|---|
| `src/core/field/Transfer.h` / `.cpp` | operator recognition, legality rules, the domain-override half of inference step 3, and the cost model as data (used by both codegen and the UI readout) |
| `src/core/field/ReduceOps.h` / `.cpp` | the reduction kernels themselves: `sum`, `rms`, `rms(x, lo, hi)`, `min`, `max`, `mean`. One implementation each, shared by every backend |
| `docs/plans/field/step-08-notes.md` | *(optional)* measurements and the answers to the open questions below |

**Modify** (Field's own files only, all created in steps 1–7):

| Path | Change |
|---|---|
| `src/core/field/Infer.cpp` (or wherever step 1 put domain inference) | transfer nodes override the join: `reduce(x, …)` → `coarsen(domain(x))`, `resample(x, D)` → `D`, `downsample(x, k)` → `domain(x)` with divisor `k` recorded, `map` → the mapped body's domain |
| `src/core/field/Ir.h` | a `divisor` field on the IR node for `downsample`; a `transferKind` on `Call`. **No new backend-specific node types** (`field-compiler` §6.4) |
| `src/core/field/BackendBytecode.cpp` | emit the frame/element reduce loop and the `downsample` hold cell |
| `src/core/field/BackendGlsl.cpp` | `reduce` is **not lowerable here** — it must arrive as a uniform, and attempting to lower one is a compile error, not a silent CPU fallback |
| `CMakeLists.txt` | the new `.cpp` files (Field sources live under `src/core/field/`; node `.cpp` files go near line 98, audio-thread files near line 54) |

**Do not modify:** `src/core/Expression.h`/`.cpp` public API, `src/core/Mesh.h`,
`src/audio/ParamMailbox.h`, `src/audio/MeterRing.h`, `src/main.cpp` node wiring,
or any existing node.

---

## 5. Procedure

### 5.1 `broadcast` — implement it by implementing nothing

Coarse → fine is implicit. There is no syntax and no IR node.

```
amount = 0.5 + 0.5 * sin(t)     # frame domain, 60/s
P.y   += amount                  # element domain, 60*N/s — reads `amount`
```

1. Confirm the parser **rejects** `broadcast` as a callee name, with the message
   "broadcast is implicit; write `P.y += amount`".
2. Confirm the placement pass (`field-compiler` §5.5) hoists `amount` out of the
   per-element loop and evaluates it once.
3. **Cost is zero.** A broadcast is a hoist, not a copy. If a broadcast ever
   costs something, the compiler placed the node in the wrong domain — that is a
   compiler bug, not a language cost. Assert this with an evaluation counter, not
   with a timing measurement (`field-testing` §3 section F).

### 5.2 `reduce` — many → one

| Form | Result domain | Legal from | Cost |
|---|---|---|---|
| `reduce.sum(x)` | one step coarser | element, sample | O(N), one pass |
| `reduce.rms(x)` | one step coarser | element, sample | O(N), one pass |
| `reduce.rms(in, lo, hi)` | frame | **sample only** | O(N) + a band filter |
| `reduce.min(x)` / `reduce.max(x)` | one step coarser | element, sample | O(N) |
| `reduce.mean(x)` | one step coarser | element, sample | O(N) |
| `reduce.*(x)` from `pixel` | frame | pixel | O(w·h) **+ a GPU→CPU sync** — see 5.6 |

Legality, each a compile error with a span:

| Refuse | Message must say |
|---|---|
| `reduce` on a value already at the target coarseness | "`x` is already frame-domain" |
| `reduce` crossing two levels at once (`sample` → `graph`) | reduce one level; `graph` is edit-time and has no per-frame value |
| `reduce` inside a per-element loop over the same domain being reduced | that is a delay-free cycle in disguise — point at `field-state` §2 |

`reduce.rms(in, 20, 200)` is band-limited RMS: filter `in` to the 20–200 Hz band,
take the RMS of the result. It is the canonical "give me the bass" call and the
reason `reduce` exists at all.

**Where a sample→frame reduce actually runs (step 9 lands this; specify it now):**
on the audio thread, publishing **one float per block** through the existing
`MeterRing`, drained on the main thread with `ReadLatest`. **Never add a new
cross-thread channel.** `grep` the diff for new `std::atomic` and new queue types
before you claim this holds.

### 5.3 `map` — one per element

```
map { state float z = 0
      z += (in - z) * cutoff
      out = z }
```

| Rule | |
|---|---|
| the mapped body's domain must be **finer than or equal to** the surrounding one | otherwise it is a reduce, spelled wrong — say so in the message |
| the element count must be **bounded and known at compile time** | `field-realtime` §1 rule 6; Kronos accepts the same constraint for polyphony (p.46) |
| `state` inside a `map` | one cell **per element** — `field-state` §3, and the cost goes in the node's UI readout |

Cost is **N × the body**, and that is the point. It is not hidden: the UI shows
cell count × per-element cost = total.

### 5.4 `resample` — read domain A while standing in domain B

`resample(x, D)` gives `x`'s value as seen from `D`. Unlike `reduce` it does not
aggregate — it **samples**, so it can alias.

| Direction | What happens | Trap |
|---|---|---|
| fine → coarse (`sample` → `frame`) | takes the most recent value | **aliasing** — 48 kHz sampled at 60 Hz keeps 1 sample in 800. For a level or envelope use `reduce.rms`; that is exactly why `reduce` sits next to `resample` |
| coarse → fine (`frame` → `sample`) | holds the value for the block | **zipper noise** — a 60 Hz staircase. Must arrive through `ParamMailbox::SmoothedValue` (`src/audio/ParamMailbox.h:40`), never around it |
| between incomparable domains | **error** | route through `frame` explicitly |

| Wrong | Right |
|---|---|
| `level = resample(audio, frame)` to get "the bass" | `bass = reduce.rms(in, 20, 200)` |
| `resample(P, pixel)` | error — element and pixel are incomparable |
| a frame value stepped raw into `sample` | let it arrive through the mailbox's smoother |

### 5.5 `downsample` — run at a fraction of the ambient rate

```
slow = downsample(lfo, 32)
```

| Property | |
|---|---|
| Domain | unchanged |
| Rate | ambient / `k` |
| Cost | body/`k`, plus **one hold cell per output** |
| `k` | compile-time constant integer ≥ 1. A `k` read from a `param` is a compile error naming the expression that was not constant |
| With `state` | the cell updates only on the `k`-th invocation. A filter inside `downsample(…, 32)` runs at 1.5 kHz, not 48 kHz — **its coefficients must be computed for that rate**, and this is the bug this row exists to prevent |

**The measured payoff and where it stops.** Kronos Table 3, p.45: the same
program with only the LFO downsample factor changed ran 257 µs/1024 samples at
`k`=1 and 114 µs at `k`=128 — a 2.25× speedup, **saturating near `k`=32**. Past
~32 there is nothing left to win and the aliasing is worse. Default ceiling 32;
anything higher needs a stated reason.

| Wrong | Right |
|---|---|
| `downsample` on a sample-accurate path (audio, a trigger) | leave it at full rate |
| `downsample(x, 128)` "because bigger is faster" | 32 |
| `downsample` to make an expensive kernel affordable, then a branch inside it | the branch costs the same *per invocation*; the saving is on the count only |

### 5.6 The cost table — build it as data, ship it in the UI

| Crossing | Operator | Cost | Notes |
|---|---|---|---|
| `graph` → anything | implicit | 0 | a uniform / mailbox slot |
| `frame` → `element` | implicit broadcast | 0 | a hoist |
| `frame` → `pixel` | implicit broadcast | 0 | becomes a `uniform` |
| `frame` → `sample` | implicit broadcast | ~0 | through `ParamMailbox::SmoothedValue` |
| `element` → `frame` | `reduce` | O(N) CPU, once per frame | N ≈ 5000 |
| `sample` → `frame` | `reduce` | O(block) on the audio thread, one float per block | via the existing `MeterRing` |
| `pixel` → `frame` | `reduce` | O(w·h) **plus a GPU→CPU sync** | the expensive one |
| `element` ↔ `pixel` | none | — | error; route through `frame`, or render and sample |
| `element` ↔ `sample` | none | — | error; route through `frame` |
| `pixel` ↔ `sample` | none | — | error; route through `frame` |
| any | `downsample(x, k)` | body/`k` + 1 hold cell | saturates near k=32 |

Encode this table once, in `src/core/field/Transfer.cpp`, and read it from both the
codegen and the node's cost readout. Two copies will drift.

> **OPEN — how many frames late is a pixel reduce? Ask the owner; do not decide
> in code.** An async PBO readback is 1–2 frames behind by construction.
> **(a)** accept the latency and document it (frame N−2: fine for a colour
> average driving a slow parameter, wrong for anything a user reads as
> synchronous); **(b)** a synchronous readback with a stated frame-time cost;
> **(c)** forbid `pixel` → `frame` reductions in v1 and revisit.
> The existing `Palette` / colour-extraction nodes already solve a version of
> this — read what they do and bring the finding to the owner with the question.
> If the answer has not arrived, ship **(c)** behind a clear error message; a
> refusal is recoverable, a silently-two-frames-stale value is not.

### 5.7 Worked example — audio driving geometry

Three domains, two crossings, one of them implicit and unwritten.

```
# --- sample domain: the incoming audio ---
# --- crossing 1: reduce, sample -> frame ---
bass = reduce.rms(in, 20, 200)
high = reduce.rms(in, 4000, 12000)

# --- frame domain ---
param float amount = 1.0 [0, 4]
push = bass * amount

# --- crossing 2: broadcast, frame -> element (implicit, never written) ---
P.y += push
Cd  = vec3(bass, 0.2, high)
```

| Line | Domain | Rate | Crossing |
|---|---|---|---|
| `reduce.rms(in, …)` ×2 | sample → frame | 60/s output | explicit `reduce` |
| `push = bass * amount` | frame | 60/s | — |
| `P.y += push` | element | 60 × N/s | implicit broadcast |
| `Cd = vec3(…)` | element | 60 × N/s | implicit broadcast |

Two reductions run on the audio thread and publish two floats per block. The
multiply runs 60 times a second. Only the last two lines run 300 000 times a
second. **No annotation was written to make that happen** — that hoist is the
entire 2.25× payoff of rate inference (Norilo Table 3, p.45).

### 5.8 Worked example — geometry driving pixels

`element` and `pixel` are incomparable, so this does **not** cross directly.

```
# --- element domain ---
attrib float heat = 0
heat = length(P) * 0.1

# --- crossing: reduce, element -> frame ---
avgHeat = reduce.mean(heat)

# --- pixel kernel, in a different node ---
# avgHeat is frame-domain -> arrives as a uniform. Implicit broadcast.
col = lerp(col, vec3(1, 0.3, 0), avgHeat)
```

| Wrong | Right |
|---|---|
| `col = lerp(col, red, heat)` reading element `heat` in a pixel kernel | reduce to `frame` first, or render the geometry to a texture and sample it |
| `resample(heat, pixel)` | error — incomparable domains |

**The other legal route is the renderer, not an operator.** For genuine
*per-pixel* access to per-element data the answer is to render the elements into
a texture (the 3D pipeline already does this) and sample it in the pixel kernel.
That is a node-graph connection, not a language construct, and it is the right
answer far more often than a reduction is. Say so in the error message's hint.

### 5.9 Tests

Extend the `INFINITE_FIELDTEST` harness (early-exit headless, modelled on
`INFINITE_DSPTEST` — `field-testing` §1; note **no Field fixture exists yet**, so
if step 1 did not create it, create it here) with:

| Case | Assert |
|---|---|
| every incomparable crossing | refused, message carries **both** spans and names the fixing operator |
| `broadcast(x)` written explicitly | refused with the "it is implicit" message |
| a frame subexpression inside an element kernel | evaluated **once** — an evaluation counter, not the result value, because both give the same answer |
| `downsample(x, k)` with `k` from a param | refused, naming the non-constant expression |
| `downsample(x, 4)` | body ran `ceil(n/4)` times over an n-invocation sweep; the hold value is correct between invocations |
| an unbounded `map` | refused |
| a `map` over N with `state float` inside | N cells reported by the cost readout, and per-element values do not bleed |
| `reduce.mean` over a known element array | exact expected value |
| `reduce` in a pixel kernel's GLSL lowering | refused at compile time; **no silent CPU fallback** |
| no new cross-thread channel | `grep` the diff for new atomics/queues — record the result in the commit message |

---

## 6. Traps, and the bug each one prevents

| # | Trap | The bug it prevents |
|---|---|---|
| 1 | Inserting an implicit `map` when the join fails | a user writes `col = lerp(col, red, heat)` and gets a silently wrong image instead of an error. The whole value of an inferred-domain language is that the *illegal* cases are loud |
| 2 | A one-line "cyclic/type error" message | the user never wrote a domain anywhere; a message without both spans and a suggested operator is unusable. Report "`in` is sample-domain and `P` is element-domain; wrap it, e.g. `reduce.rms(in, 20, 200)`" |
| 3 | Publishing a sample→frame reduce through a new atomic or queue | `ParamMailbox.h:11–20` records the real bug: a ring whose producer overrun-drop path wrote the consumer-owned head index broke the single-consumer invariant under load. Use `MeterRing`, and use `ReadLatest`, not `Read` |
| 4 | Draining a reduce with `MeterRing::Read` once per frame | a per-block producer against a per-frame consumer accumulates **unbounded lag** — the meter reads further behind the longer the patch runs. `ReadLatest`'s own comment says exactly this |
| 5 | Stepping a frame value into `sample` without the mailbox smoother | zipper noise: an audible 60 Hz staircase on every knob move |
| 6 | Using `resample(audio, frame)` to get a level | 1 sample in 800 survives. The user gets a value that flickers with the frame phase and blames the audio engine |
| 7 | Computing `downsample` coefficients at the ambient rate | a filter inside `downsample(…, 32)` runs at 1.5 kHz; coefficients computed for 48 kHz put its cutoff 32× off. Silent, and it sounds like a broken filter, not a broken rate |
| 8 | A non-constant `k` | codegen cannot allocate the hold cell or unroll; it is `field-realtime` §1 rule 5 (every size known at compile time) with a different face |
| 9 | Two copies of the cost table (codegen + UI) | they drift, and then the number shown on the node is a lie. One table, read twice |
| 10 | A pixel reduce done synchronously "just to get it working" | a GPU→CPU stall in the render path, showing up as a frame-time spike nobody attributes to the Field node. If (c) is chosen, refuse it instead |
| 11 | Emitting a CPU fallback when GLSL cannot lower a `reduce` | a per-frame readback nobody asked for. A reduction is a coarser domain; it is computed elsewhere and arrives as a uniform, or it is refused |
| 12 | Letting `map`'s bound come from a runtime value | an unbounded loop in generated code — `field-realtime` §1 rule 3, and on the audio thread it is an xrun |
| 13 | Assuming a branch inside `downsample` is 1/k cheaper | it is 1/k of the *count*, not of the branch (`field-realtime` §4.2). Budgets written the other way are wrong by k |
| 14 | Adding a transfer-specific node type to the IR | `field-compiler` §6.4 — the moment `GlslReduce` exists, the C++ and WASM backends have to special-case it and retargetability is gone. Transfers are `Call` nodes with a kind |

---

## 7. Exit criterion — a real command

Build:

```
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Must compile clean. Do not rely on the edit looking right.

Field harness (headless, no GL):

```
INFINITE_FIELDTEST=1 ./build/Infinite.app/Contents/MacOS/Infinite 2>&1 \
  | tee /tmp/field-step8.log
grep -E 'FAIL|BUG$|MISMATCH|SUSPECT' /tmp/field-step8.log && echo "STEP 8 NOT DONE" || echo "STEP 8 harness clean"
```

No-new-channel check — this must print nothing:

```
git diff --unified=0 main -- src/field src/nodes src/audio \
  | grep -E '^\+.*(std::atomic|RingBuffer|std::mutex|std::condition_variable)'
```

Full hygiene gate:

```
.claude/skills/run-infinite-hygiene/driver.sh
```

Then, per this project's convention:

```
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

Step 8 is done when all of the following hold (`field-domains` §10):

1. It builds clean.
2. Every crossing between two incomparable domains is refused, with **both**
   spans and a hint naming the operator that would fix it.
3. `broadcast` has no syntax and cannot be written.
4. A `downsample` with a non-constant `k` is refused.
5. `map`'s element count is bounded and known at compile time; an unbounded one
   is refused.
6. A `sample` → `frame` reduce publishes through the existing `MeterRing` —
   the `grep` above confirms no new cross-thread channel.
7. A `frame` → `sample` value arrives through `ParamMailbox::SmoothedValue`,
   not around it.
8. The §5.6 cost table is accurate after the change, lives in exactly one place,
   and a pixel reduction (if implemented at all) states its frame latency.
9. `/run-infinite-hygiene` passes; `AUDIOPARAMSWEEPTEST` and
   `AUDIOTEARDOWNSWEEPTEST` still pass for any node hosting a kernel that
   reduces audio, with **zero xruns**.

---

## 8. Out of scope

- **Do not build the sample-domain backend.** That is step 9. Specify the
  reduce's audio-thread half here; land it there.
- **Do not build the graph domain.** That is step 10.
- Do not touch `src/core/Expression.h`/`.cpp`'s public API or its three call
  sites: `src/main.cpp:37507`, `src/core/ExprGlobals.cpp:72`,
  `src/nodes/AnalyzeNodes.cpp:179`.
- Do not change `Mesh`'s layout. The AoS/SoA question is `field-compiler` §9's
  open question and belongs to step 4, not here.
- Do not add a new patch-file line kind. `state` serialization is step 6's open
  question and is not reopened here.
- Do not add a `parallel`, `scan`, `fold`, `zip` or `filter` operator. Five
  operators, and every one of them has to be explainable as "a kernel per element
  of a domain".
- Do not resolve any question marked **OPEN** silently. The pixel-readback
  latency question goes to the owner.
- Do not commit or push. Report the diff and the harness output.

---

## 9. Which earlier steps must be done first

| Step | Why this step needs it |
|---|---|
| **1** — `Expression.cpp` → lexer/AST/typed IR/bytecode | there is no IR to attach a domain to without it, and no spans for the error messages this step is mostly made of |
| **3** — `vec2/3/4` + rank polymorphism | `reduce.mean` over a `vec3` attribute, and `Cd = vec3(bass, …)` in the worked example |
| **4** — the `element` domain | half the crossings in the cost table have `element` on one side |
| **5** — `param` → `ParamRef` | `downsample`'s "`k` from a param is refused" rule needs params to exist to refuse one |
| **6** — `state` cells | `downsample`'s hold cell and `map`'s per-element state are state cells |
| **7** — `pixel` via GLSL | the pixel column of the cost table, and the "a reduce is not lowerable in GLSL" refusal |

Steps 9 and 10 depend on **this** step, not the other way round.
