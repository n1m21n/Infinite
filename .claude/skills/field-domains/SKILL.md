---
name: field-domains
description: Field's domain transfer operators — `reduce` (many to one), `map` (one per element), `broadcast` (one to many, implicit and never written), `resample` (read domain A while standing in domain B) and `downsample` (run at a fraction of the ambient rate) — when each is legal, what each costs per crossing, and worked examples taking audio into geometry and geometry into pixels. Use when a Field kernel needs data from a different rate, when audio must drive geometry or geometry must drive pixels, when the compiler reports an incomparable-domain join, when deciding whether a value should be computed per-frame or per-element, or when reviewing any use of reduce/map/resample/downsample.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

Read [`field-language`](../field-language/SKILL.md) §2–3 for the domains and
rate inference, and [`field-compiler`](../field-compiler/SKILL.md) §5 for the
lattice and the fixpoint these operators override.
[`field-realtime`](../field-realtime/SKILL.md) carries the branching cost model
that interacts with every crossing here.

**Transfer operators are implemented.** `reduce`/`map`/`resample`/`downsample`
live in `src/core/field/Transfer.cpp` and `ReduceOps.cpp`, wired through
`FieldIR.cpp`'s validators (`ValidateReduce`, `ValidateResample`,
`ValidateDownsample`, `ValidateMap`). Verify against that source before
trusting a claim in this file — several worked examples below were wrong until
a live compile error (mixing `in` into an element kernel) forced a correction.

**The restriction this skill got wrong: a transfer operator does not let two
domains share one kernel body.** `reduce.rms(in, lo, hi)` is legal only
*inside a `sample`-domain kernel* — because `in` itself is a reserved word of
`sample` and does not exist in any other domain's scope. You cannot write
`bass = reduce.rms(in, 20, 200)` followed by `P.y += bass` in the same kernel:
that kernel would have to be simultaneously `sample` (to read `in`) and
`element` (to read/write `P`), which `FieldIR.cpp`'s structural pin validation
explicitly refuses — "a cross-domain audio read would need an explicit
resample, not supported in v1". The reduction has to happen in its own
sample-domain node; the result reaches an element/pixel kernel as a `param`
driven through the modulation matrix, not as a shared local variable. Same
restriction for `image` pins and `pixel`-domain reads from outside `pixel`.

---

## 0. Invariants

1. **Clean room.** Never read Kronos, Cmajor, SuperCollider or BespokeSynth
   source. The Kronos *paper* (Norilo, CMJ 39:4, 2015) is citable.
2. **A crossing between incomparable domains is always explicit.** `element`,
   `pixel` and `sample` do not join implicitly. The compiler errors rather than
   inserting a `map` for you.
3. **`map` and `reduce` keep those names.** Norilo reports (p.45) that students
   respond to "Map a bank, Reduce a cascade" where they struggle with the
   abstract framing. Do not rename them to something more precise and less
   teachable.

---

## 1. The five operators

| Operator | Direction | Written? | Example |
|---|---|---|---|
| `reduce` | many → one | explicitly | `bass = reduce.rms(in, 20, 200)` — only inside a `sample`-domain kernel |
| `map` | one per element | explicitly | `map` a bank of filters |
| `broadcast` | one → many | **never** — implicit | `P.y += amount` where `amount` is frame-domain |
| `resample` | read domain A while standing in domain B | explicitly | `resample(audio, frame)` |
| `downsample` | run at a fraction of the ambient rate | explicitly | `downsample(lfo, 32)` |

The lattice they move across (from `field-compiler` §5):

```
   graph  ⊑  frame  ⊑  element
   graph  ⊑  frame  ⊑  pixel
   graph  ⊑  frame  ⊑  sample

   element / pixel / sample are mutually INCOMPARABLE.
   Every crossing between them goes through `frame` (or a reduce to it).
```

## 2. `broadcast` — the one you never write

Coarse → fine is implicit, and there is no syntax for it. This is the same
mechanism as rate inference: a frame-domain value used inside an element-domain
statement is computed once and read N times.

```
amount = 0.5 + 0.5 * sin(t)     # frame domain, evaluated 60 times/sec
P.y   += amount                  # element domain, reads `amount` 60*N times/sec
```

| Wrong | Right |
|---|---|
| `P.y += broadcast(amount)` | `P.y += amount` |
| computing `sin(t)` inside the element loop "to be explicit" | let inference hoist it — that hoist **is** the 2.25× payoff (Norilo Table 3, p.45) |

**Cost: zero.** A broadcast is a hoist, not a copy. If a broadcast ever costs
something, the compiler placed the node in the wrong domain — that is a
compiler bug, not a language cost.

## 3. `reduce` — many → one

Fine → coarse, and it must be written, because the compiler cannot guess which
reduction you mean.

| Form | Result domain | Legal from | Cost |
|---|---|---|---|
| `reduce.sum(x)` | one step coarser | element, sample | O(N), one pass |
| `reduce.rms(x)` | one step coarser | element, sample | O(N), one pass |
| `reduce.rms(in, lo, hi)` | frame | **sample only** | O(N) + a band filter; see below |
| `reduce.min(x)` / `reduce.max(x)` | one step coarser | element, sample | O(N) |
| `reduce.mean(x)` | one step coarser | element, sample | O(N) |
| `reduce.*(x)` from `pixel` | frame | **pixel** | O(w·h) — a GPU reduction pass, or a readback; see §7 |

`reduce.rms(in, 20, 200)` is band-limited RMS: it filters `in` to the 20–200 Hz
band and takes the RMS of the result. It is the canonical "give me the bass"
call and the reason `reduce` exists at all.

| Legality rule | |
|---|---|
| `reduce` on a value that is already coarse | error — "`x` is already frame-domain" |
| `reduce` crossing two levels at once (`sample` → `graph`) | error — reduce one level; `graph` is edit-time and has no per-frame value |
| `reduce` inside a per-element loop over the same domain being reduced | error — that is a delay-free cycle in disguise; see [`field-state`](../field-state/SKILL.md) §2 |

**Where the reduction actually runs.** For `sample` → `frame`, it runs on the
audio thread and publishes one float per block; the main thread reads the
latest. That path already exists in this codebase in the form of `MeterRing`
(audio → main) — see [`field-integration`](../field-integration/SKILL.md) §4.
**Never add a new cross-thread channel for it.**

## 4. `map` — one per element

`map` applies a kernel once per element of a domain, producing one result per
element. It is the operator that makes "a bank of N filters" one line.

```
# a bank of resonators, one per element
map { state float z = 0
      z += (in - z) * cutoff
      out = z }
```

| Legality rule | |
|---|---|
| the mapped body's domain must be **finer than or equal to** the surrounding one | otherwise it is a reduce, spelled wrong |
| the element count must be **bounded and known at compile time** | `field-realtime` §2 — this is the same constraint Kronos accepts for polyphony (p.46) |
| `state` inside a `map` | costs one cell **per element** — `field-state` §3 |

**Cost: N × the body.** That is the whole point and it is not hidden. A `map`
over 5000 elements with a `state float` inside is 5000 cells, and the UI shows
it.

## 5. `resample` — read domain A while standing in domain B

```
level = resample(audio, frame)
```

`resample(x, D)` gives the value of `x` as seen from domain `D`. Unlike
`reduce`, it does not aggregate — it **samples**, which means it can alias.

| Direction | What happens | Trap |
|---|---|---|
| fine → coarse (`sample` → `frame`) | takes the most recent value | **aliasing.** 48 kHz sampled at 60 Hz keeps 1 sample in 800. For a level or envelope, use `reduce.rms` instead — that is the whole reason `reduce` exists next to `resample` |
| coarse → fine (`frame` → `sample`) | holds the value for the whole block | **zipper noise.** A frame-rate value stepped into the sample domain is a 60 Hz staircase. The existing fix is already in the codebase: `ParamMailbox::SmoothedValue` one-pole smooths on the consumer side (`src/audio/ParamMailbox.h:39`). A `resample` into `sample` must go through that path, not around it |
| between incomparable domains | **error** | route through `frame` explicitly |

| Wrong | Right |
|---|---|
| `level = resample(audio, frame)` to get "the bass" | `bass = reduce.rms(in, 20, 200)` |
| `resample(P, pixel)` | error — element and pixel are incomparable; reduce or render, then sample |
| a `frame` value stepped raw into `sample` | let it arrive through the mailbox's smoother |

## 6. `downsample` — run at a fraction of the ambient rate

```
slow = downsample(lfo, 32)
```

Keeps the value's domain but records a divisor: the body runs once every `k`
invocations and holds its result in between.

| Property | |
|---|---|
| Domain | unchanged |
| Rate | ambient / `k` |
| Cost | 1/`k` of the body, plus one hold cell per output |
| `k` | must be a compile-time constant integer ≥ 1 |
| Interaction with `state` | the cell updates only on the `k`-th invocation — a filter inside a `downsample(…, 32)` is running at 1.5 kHz, not 48 kHz, and its coefficients must be computed for that rate |

**The measured payoff, and where it stops.** Kronos Table 3, p.45: the same
program with only the LFO downsample factor changed ran 257 µs/1024 samples at
`k`=1 and 114 µs at `k`=128 — a 2.25× speedup, **saturating near `k`=32**.
Beyond ~32 there is nothing left to win, and the aliasing gets worse. Use 32 as
the default ceiling and justify anything higher.

| Wrong | Right |
|---|---|
| `downsample` on a signal that must be sample-accurate (an audio path, a trigger) | leave it at full rate |
| `downsample(x, 128)` "because bigger is faster" | 32; the curve is flat past it |
| `downsample` used to make an expensive kernel affordable, then branched inside | the branch cost is unchanged per invocation — see `field-realtime` §4 |
| a `k` computed from a param | compile-time constant only |

## 7. Cost table — every crossing on one page

| Crossing | Operator | Cost | Notes |
|---|---|---|---|
| `graph` → anything | implicit | 0 | a uniform / mailbox slot |
| `frame` → `element` | implicit broadcast | 0 | a hoist |
| `frame` → `pixel` | implicit broadcast | 0 | becomes a `uniform` |
| `frame` → `sample` | implicit broadcast | ~0 | through `ParamMailbox::SmoothedValue` |
| `element` → `frame` | `reduce` | O(N) CPU, once per frame | N ≈ 5000 |
| `sample` → `frame` | `reduce` | O(block) on the audio thread, one float published per block | via the existing meter path |
| `pixel` → `frame` | `reduce` | O(w·h) **plus a GPU→CPU sync** | the expensive one — see below |
| `element` ↔ `pixel` | none | — | error; route through `frame`, or render the elements and sample the result |
| `element` ↔ `sample` | none | — | error; route through `frame` |
| `pixel` ↔ `sample` | none | — | error; route through `frame` |
| any | `downsample(x, k)` | body/`k` + 1 hold cell | saturates near k=32 |

**Why `pixel` → `frame` is the expensive one.** Reading a value back from the
GPU stalls the pipeline. Infinite already has this problem and already has the
machinery for it — the PBO readback path used by recording/export (see
`.claude/skills/av-sync-sweep/SKILL.md` and
`.claude/skills/render-pipeline-sweep/SKILL.md`). A pixel reduction must use
that asynchronous path.

> **OPEN — how many frames late is a pixel reduce?** An async PBO readback is
> 1–2 frames behind by construction. Options: **(a)** accept the latency and
> document it (a pixel `reduce` reads frame N−2 — fine for a colour average
> driving a slow parameter, wrong for anything a user will perceive as
> synchronous); **(b)** a synchronous readback with a stated frame-time cost
> (predictable and slow); **(c)** forbid `pixel` → `frame` reductions in v1
> entirely and revisit. The existing `Palette` / colour-extraction nodes
> already solve a version of this problem — check what they do before choosing.
> Ask the owner.

## 8. Worked example — audio into geometry

Sound drives point positions and colour. This is **two kernels in two nodes**,
not one — `in` and `P`/`Cd` cannot share a kernel body (see §0 above).

```
# --- Node A: a sample-domain kernel (Field Effect / Field Synth) ---
output frame float bass = reduce.rms(in, 20, 200)   # sample -> frame, output-only
out = in                                              # pass audio through unchanged
```

`reduce.rms` in the sample domain is output-only: it must be written as its
own `output frame float <name> = reduce.rms(in, loHz, hiHz)` declaration (at
most one per kernel), and `bass` is **not** readable back inside this same
kernel's per-sample lines — it only exists as a pin. That pin is exposed on
Node A the same way an existing meter/analysis node exposes a level, and is
connected to a target `param` on a different node through the **modulation
matrix** — a graph connection, not language syntax.

```
# --- Node B: an element-domain kernel (Field Modifier) ---
param float speed = 2.0 [0, 10]
param float bass = 0.0 [0, 1]          # driven by Node A via the mod matrix
dist = length(P.xz)
P.y += sin(dist * 4.0 - t * speed) * bass * 1.5
Cd = vec3(bass, 0.3, 1.0 - bass)
```

| Kernel | Domain | Rate | How the other kernel's value arrives |
|---|---|---|---|
| Node A's `reduce.rms(in, …)` | sample → frame | 60/s output | explicit `reduce`, sample-domain kernel only |
| Node B's `bass` | element, read as a `param` | 60 × N/s | modulation-matrix connection, not a language crossing |

**No transfer operator crosses `sample` directly into `element` or `pixel`.**
The frame-domain result of a `sample`-kernel reduce still has to leave that
node and re-enter the next one as a `param`.

## 9. Worked example — geometry into pixels

`element` and `pixel` are incomparable, so this crossing does **not** go
directly. It goes through `frame`, or through the renderer.

```
# --- element domain ---
attrib float heat = 0
heat = length(P) * 0.1

# --- crossing: reduce, element -> frame ---
avgHeat = reduce.mean(heat)

# --- pixel domain kernel, in a different node ---
# avgHeat does not appear here by name — it reaches this node as a param
# driven through the modulation matrix, same as the audio case in §8.
param float avgHeat = 0.0 [0, 1]
col = mix(col, vec3(1, 0.3, 0), avgHeat)
```

| Wrong | Right |
|---|---|
| `col = mix(col, red, heat)` reading element `heat` in a pixel kernel | reduce it to `frame` first, or render the geometry to a texture and sample that texture per-pixel |
| `resample(heat, pixel)` | error — incomparable domains |

**The other legal route is the renderer, not an operator.** If you want
*per-pixel* access to per-element data, the answer is not a transfer operator —
it is to render the elements into a texture (which the 3D pipeline already
does) and have the pixel kernel sample it. That is a node-graph connection,
not a language construct, and it is the right answer far more often than a
reduction is.

## 10. Exit criterion for a transfer-operator change

1. It builds clean.
2. Every crossing between two incomparable domains is refused, with both spans
   and a hint naming the operator that would fix it.
3. `broadcast` has no syntax and cannot be written.
4. A `downsample` with a non-constant `k` is refused.
5. `map`'s element count is bounded and known at compile time; an unbounded one
   is refused.
6. A `sample` → `frame` reduce publishes through the existing audio→main meter
   path — `grep` confirms no new cross-thread channel was added.
7. A `frame` → `sample` value arrives through `ParamMailbox::SmoothedValue`,
   not around it.
8. The §7 cost table is still accurate after the change, and a pixel reduction
   (if implemented) states its frame latency.
9. `/run-infinite-hygiene` passes; the audio sweeps
   (`AUDIOPARAMSWEEPTEST`, `AUDIOTEARDOWNSWEEPTEST`) still pass for any node
   hosting a kernel that reduces audio.
