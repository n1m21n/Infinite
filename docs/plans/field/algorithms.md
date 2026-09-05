# Field — algorithm catalogue

**What the five domains make buildable, and what they refuse.**

This is not an implementation prompt. It is the answer to *"why build a
language at all"* — the list of things that become one screen of readable text
in Field and are otherwise a new C++ node each.

Read [`field-language`](../../../.claude/skills/field-language/SKILL.md) for
the syntax every example below obeys,
[`field-state`](../../../.claude/skills/field-state/SKILL.md) for the cost
table every state figure is derived from,
[`field-domains`](../../../.claude/skills/field-domains/SKILL.md) for the
transfer operators, and
[`field-realtime`](../../../.claude/skills/field-realtime/SKILL.md) for the
rules that decide the real-time column.

---

## 1. Invariants — they override anything inferred from this document

1. **Clean room.** Infinite is MIT. Never open, read, grep or reference GPL
   sources: Kronos (`bitbucket.org/vnorilo/k3`), Cmajor, SuperCollider, or
   BespokeSynth (also at `/Users/namansoni/BespokeSynth` on this machine — do
   not open it). Papers and published algorithm descriptions are cited freely
   in every entry's **Prior art** row; **no algorithm below was read out of
   any of those code bases.** Safe to read: Faust (LGPL), ChucK, Houdini VEX
   docs, TidalCycles docs/papers.
2. **Bare names. No sigils. Ever.** `P.y += bass * 2`, never `@P.y += bass * 2`.
3. **Rate is inferred, never declared.** No example below carries a rate
   annotation, because there is no syntax for one.
4. **One primitive.** Every entry is a body of code run once per element of a
   domain. An entry that cannot be described that way is in §10, not §5–§9.
5. **Almost nothing here is implemented.** This catalogue is a target, not a
   status report. The exceptions, as of build step 23: **OPEN-C and OPEN-B are
   both answered and built.** From OPEN-C, §6.6 (trails), §6.7 (Gray-Scott
   reaction-diffusion) and §7.4 (advection) are writable today, the first two
   as shipped presets. From OPEN-B, §8.3's Verlet ropes and distance
   constraints and any fixed-topology curve smoothing or buckling are writable,
   two of them as shipped presets — but §6.5's *differential growth* is not,
   because a kernel cannot change `count`. Entries tagged `[A]` or `[D]` still
   depend on a language question that is **OPEN** (§4) and cannot be written
   today at all.

---

## 2. The ladder

Each rung adds one capability, and each capability costs exactly one thing.

| Rung | Name | What it adds | The cost of that rung |
|---|---|---|---|
| **0** | pure formula | output is a function of the domain's own names and `t` alone | none — 0 bytes of state, exactly reproducible on a seek |
| **1** | memory | `state` cells: the system remembers what it did last time | one cell per lane per domain element — 4 B in `frame`, 16.6 MB at 1080p |
| **2** | prediction | the system extrapolates forward and acts on the guess | the guess can be wrong, and a wrong guess has an audible/visible signature |
| **3** | self-correction | the system measures its own error and changes itself to reduce it | a stability bound. Every rung-3 entry below carries one, and crossing it diverges |
| **4** | learning from the user | the system adapts to what *this particular person* does | the learned state must persist across a reload, which forces `field-state` §6's serialization question |

```
   rung 0   out = f(in, t)
   rung 1   out = f(in, t, memory)                     memory' = g(...)
   rung 2   out = f(in, t, memory, prediction)         prediction = h(memory)
   rung 3   out = f(...)   AND   model' = model + mu * error * gradient
   rung 4   out = f(...)   AND   model persists into the patch file
```

**The rung is a property of the algorithm, not the domain.** Every domain
reaches every rung it can afford. `pixel` reaches rung 4 in principle and never
should in practice, because a learned per-pixel model is a texture pair that
has to be written into the patch file.

---

## 3. Notation and the constants every cost figure uses

### 3.1 Constants

| Symbol | Value | Source |
|---|---|---|
| `float` | 4 B | — |
| N (elements) | 5 000 typical | `field-language` §2 |
| 1080p | 1920 × 1080 = **2 073 600** px | — |
| sim res (fluid/RD default) | 512 × 512 = **262 144** px | this document's recommendation, §8.4 |
| voices | 8 (matches `EquationNode`'s poly count) | design brief §14 |
| sample rate | 48 000 | `field-language` §2 |
| frame rate | 60 | `field-language` §2 |
| pixel invocations | 60 × 2 073 600 = **124.4 M/s** at 1080p | `field-realtime` §5 |
| element invocations | 60 × 5 000 = **300 k/s**, budget ~2 ms/frame | `field-realtime` §5 |
| audio callback budget | ~5.3 ms per 256-frame block | `field-realtime` §5 |
| `param` ceiling in `sample` | 128 | `ParamMailbox::kMaxParams` |
| node types in Infinite today | 167 | `run-infinite-hygiene` full round trip |

### 3.2 Texture arithmetic, once, so §5–§9 can just cite it

| Format | One texture at 1080p | Ping-pong pair | Cells packed |
|---|---|---|---|
| R16F | 4.15 MB | **8.29 MB** | 1 |
| R32F | 8.29 MB | **16.6 MB** | 1 |
| RGBA16F | 16.6 MB | **33.2 MB** | 4 |
| RGBA32F | 33.2 MB | **66.4 MB** | 4 |
| RGBA32F at 512² | 4.19 MB | **8.39 MB** | 4 |
| RGBA32F at 960×540 | 8.29 MB | **16.6 MB** | 4 |

The brief's "a `state float` in a 1080p pixel kernel is an 8 MB ping-pong
texture pair" is the **R16F** row. Anything that integrates for more than a few
seconds needs R32F and therefore **16.6 MB per scalar cell**
(`field-state` §3's OPEN).

### 3.3 The one semantic every example depends on

> A `state` cell **enters each invocation holding the previous invocation's
> final value**. Inside the body, reads and writes are ordinary and imperative.

That is what makes `z += (in - z) * cutoff` / `out = z` produce the *filtered*
sample rather than the previous one, and it is what makes a hand-written shift
register work:

```
x4 = x3
x3 = x2
x2 = x1
x1 = in
```

Written top-down, each line reads the value the line below has not yet
overwritten. Written bottom-up it is four copies of `in`. **Order matters
inside the body; only the cell's carry-over is delayed.**

### 3.4 Symbols used in the per-entry tables

| Real-time | Meaning |
|---|---|
| **yes** | every bound is a compile-time literal, no allocation, no data-dependent iteration |
| **yes, bounded** | safe *given* a stated numeric bound (a step size, a param range, a memory ceiling). The bound is written in the entry and the param range must enforce it |
| **no** | fails a `field-realtime` §1 rule; see §10 |

Tags `[A]`–`[D]` mark a dependency on an unresolved language question (§4).

---

## 4. Four primitives this catalogue needs that Field v1 does not have

These are not proposals smuggled in as examples. They are **findings**: the
ladder demands them, and the skills do not currently define them. Each entry
that needs one is tagged.

Nothing in §5–§9 invents any other syntax. Every untagged example uses only
`param` / `attrib` / `state`, the operators in `field-language` §10, the
function set in the design brief §14, `for` with a literal bound, `if`/`else`,
and the transfer operators in `field-domains` §1.

---

> ### OPEN-A — bounded `state` arrays and a ring cell
>
> **Who needs it:** any delay line (comb, reverb, chorus), pitch-period
> repetition (§7.2), LPC/LMS above order ~8 without 32 hand-written lines,
> any transition table (§9.2).
>
> **The conflict in the skills as written:**
>
> | Says | Where |
> |---|---|
> | `float buf[512]` is **not in v1**; no arrays | `field-language` §14 row 6 |
> | a `state` array sized from a param is wrong; **"a fixed maximum buffer, with a runtime read index"** is right, and "this is how every delay line in the codebase already works" | `field-realtime` §2 |
>
> Those two rows disagree. Row 6 bans a *local* array; the `field-realtime` row
> blesses a *fixed-size state* buffer. Nothing resolves it.
>
> | Option | Spelling | Cost |
> |---|---|---|
> | **(a)** no arrays, ever; unroll everything | order-16 LPC is 16 declaration lines + 16 shift lines | delay lines longer than ~16 taps become unwritable; §7.2 and §9.2 are impossible |
> | **(b)** a fixed-size `state` cell with ring access — **this document's assumption** | `state float buf[2048] = 0` then `buf.write(x)` / `buf.read(d)` | the size is a literal; the read offset is clamped to the size, so rule 5 holds and an out-of-range index cannot exist |
> | **(c)** a fixed-size `state` cell with raw indexing | `buf[k]` with `k` an int | needs int/modulo semantics and a bounds decision (clamp? wrap? error?) — three more questions |
> | **(d)** express banks as `map` with `state` inside | `field-domains` §4's blessed idiom | gives one cell per mapped element but **no cross-element indexing**, so it builds a filter bank and cannot build a delay line |
>
> **Ask the owner.** (b) is the smallest thing that unblocks the whole sample
> domain above order 8; (d) is already in the language and solves a different
> problem. Every `[A]` example below is written in (b) and is **wrong syntax
> until this is answered**.

---

> ### OPEN-B — ordered-neighbour reads in `element` — **ANSWERED AND BUILT (step 23)**
>
> **Resolved.** Option **(b)**, `P.at(i - 1)`, per
> `language-decisions-and-presets.md` §1, and implemented on
> `feature/field-step-23-element-neighbour-reads`. See
> `step-23-element-neighbour-reads.md`.
>
> **Who needed it:** differential growth (§6.5), any spring/rope/cloth
> constraint (§8.3), any curve smoothing.
>
> | | |
> |---|---|
> | spelling | `X.at(k)`, where `X` is `P`, `N`, `uv`, `Cd`, a declared `attrib`, or an element `state` cell |
> | what it reads | the cook's **input** buffer — the incoming mesh for an attribute, the previous cook's value for a state cell. Never a value written earlier in the same loop |
> | out of range | clamped to `[0, count-1]`; element 0 asking for `i - 1` sees itself |
> | domain | element only; `.at()` on a param or frame value is refused with a message saying it holds one value for the whole mesh |
> | cost | the named bases are copied once per cook, before any element runs; a kernel with no `.at()` copies nothing |
>
> The "previous cook" half was the important half and it survived intact: an
> in-place read would have made the result depend on iteration order, killing
> vectorization and determinism.
>
> **What this does NOT unlock.** `count` is the input mesh's vertex count and a
> kernel cannot change it, so §6.5's *differential growth* — which inserts
> points where the curve stretches — is still not writable. Everything at fixed
> topology is: §8.3's Verlet ropes and distance constraints, curve smoothing,
> and buckling/folding, which ship as the "Verlet Rope" and "Buckling Ribbon"
> presets.
>
> One more thing had to come with it: **`age`** is now reserved in the element
> domain too, for the same reason it was added to `pixel` in step 22 — a
> simulation needs a one-shot seed, and `frame` is the global cook counter.
> Element simulations also have to keep their positions in `state`, not in `P`:
> the store is refilled from the incoming mesh every cook.

---

> ### OPEN-C — offset reads of a `pixel` state cell — **ANSWERED AND BUILT (step 22)**
>
> **Resolved.** Option **(b)**, the call form, per
> `language-decisions-and-presets.md` §1, and implemented on
> `feature/field-step-22-pixel-offset-reads`: `A(uv + d)` is one `texture()`
> fetch of the previous cook's cell; bare `A` stays sugar for `A(uv)`; the
> boundary rule is **per cell** — `state float A = 1 [wrap]`, default clamp,
> `border` reads the declared initial value. A kernel containing any offset
> read gets an RGBA32F bank instead of RGBA16F. An offset read of a
> non-`pixel` cell is a compile error naming the fact that the cell has no
> spatial extent. Verified by `INFINITE_FIELDPIXELTEST` assertions 19-27,
> including a diffusion test that fails if the fetch coordinate is ever
> dropped, and §6.7 Gray-Scott now ships as a preset. Everything below is the
> original question, kept for the reasoning.
>
> **Who needs it:** every interesting pixel algorithm. Reaction-diffusion
> (§6.7) needs a 4-tap Laplacian; advection (§7.4) needs a read at
> `uv - flow`; a fluid pressure solve (§8.4) needs both. **Infinite's existing
> Feedback / Trails / Reaction Diffusion nodes already do this** — a pixel
> `state` cell that can only be read at the current `uv` cannot express nodes
> the app already ships.
>
> | Option | Spelling | Cost |
> |---|---|---|
> | **(a)** current-pixel reads only | `field-state` §8's example | trails work; blur, RD, advection, fluid, and every existing feedback node do not |
> | **(b)** call form — **this document's assumption** | `prev(uv + d)` reads cell `prev` at that coordinate | lowers to one `texture()` fetch; the cost is a fetch, and a fetch count belongs on the node face next to the byte count |
> | **(c)** an explicit sampler function | `fetch(prev, uv + d)` | unambiguous, but a fifth spelling next to `reduce`/`resample`/`downsample`/`map` |
> | **(d)** fixed neighbour offsets only | `prev.at(-1, 0)` | covers RD and blur; **not** advection, which needs a continuous coordinate |
>
> Also unstated: **what happens outside `[0,1]`** — clamp, wrap, or border.
> Reaction-diffusion looks completely different under wrap (seamless tiling)
> versus clamp (edge accumulation). **Ask the owner; the answer is a per-cell
> declaration, not a global.**

---

> ### OPEN-D — a second `sample` input, and note/event input
>
> **ANSWERED AND BUILT (step 25), the audio-input half only.** Resolved as
> **kernel-declared dynamic input pins**, not option (a), (b), or (c) below:
> `input sample audio <name>` binds the name into the kernel's scope (via a
> new `LoadDeclaredIn` register-machine opcode reading a
> `SampleRuntimeInput::declaredIns[]` slot) and spawns a real, connectable
> `AudioCable` on the node — one per declared audio input, following the
> already-working `FieldPixelNode::DeclaredImageInput` pattern rather than a
> hardcoded `in2`. `input sample float <name>` remains collected but unbound
> (unresolved, not part of this step). The note/event half (rows below tagged
> `[D]`) is still open.
>
> **Who needs it:** the two-input adaptive filters (echo cancellation, noise
> cancellation), and every rung-4 entry that learns from what the user *plays*
> (§9.2).
>
> `sample`'s reserved set is `in out sr n` — **one input, one output.** That is
> enough for the adaptive line enhancer form of LMS used in §8.1 (the reference
> is the delayed input), and not enough for the textbook two-input form. There
> is likewise **no note or event name in any domain's reserved set**, so a
> kernel cannot see that the user played a C#.
>
> | Option | |
> |---|---|
> | **(a)** `in` becomes a vector (`in.x`, `in.y`) for a stereo/two-input node | smallest change; conflicts with `in` being a `float` everywhere else |
> | **(b)** `in2` as a second reserved name, present only on a two-input node | explicit; the reserved set becomes node-shaped, which nothing else in Field is |
> | **(c)** a `note` domain, or note names reserved in `frame` (`noteOn`, `notePitch`, `noteVel`) | the honest answer for rung 4, and a sixth domain is a large decision |
>
> **Ask the owner.** Nothing in §5–§9 depends on this except the two entries
> tagged `[D]`, which are marked SPEC-ONLY.

---

## 5. Rung 0 — pure formula

**Definition:** the output is a function of the domain's own reserved names,
`param`s, and `t`. Zero `state`, zero `attrib`.

**What rung 0 buys that rung 1 cannot:** a rung-0 kernel is *identical after a
seek*. Scrub to bar 33 and it produces exactly what it produced the first time.
Every rung-1 entry has to be re-run from the start to get the same answer, which
is why `field-state` §5 resets every cell on a seek. **Prefer rung 0 whenever it
is expressible** — it is the only rung with no reset semantics to get wrong.

### 5.1 Transport-locked scalar — `frame`

| | |
|---|---|
| Domain | `frame` |
| State cost | **0 B** |
| Real-time | yes |
| Produces | a size/brightness/gain that breathes exactly on the transport, and lands on the same value every time you loop |

```
param float depth = 1.0 [0, 4]
param float rate  = 2.0 [0.1, 8]
size = 1.0 + depth * sin(t * rate)
```

### 5.2 Closed-form oscillator — `sample`

The phase is computed from the sample index, not accumulated. No cell, no
drift, no reset problem.

| | |
|---|---|
| Domain | `sample` |
| State cost | **0 B** — compare 1 cell × 4 B × 8 voices = 32 B for a phase-accumulator oscillator |
| Real-time | yes, bounded |
| Produces | a tone that is bit-identical at any seek position; an oscillator bank that cannot go out of phase with itself |
| Bound | `n` must be an integer or `double` sample counter. As a 32-bit float it loses 1-sample resolution past 2²⁴ = 16 777 216 samples ≈ **5 min 50 s** at 48 kHz, and the pitch quantises audibly after that |

```
param float hz = 440 [20, 8000]
phase = mod(hz * n / sr, 1.0)
out   = sin(2 * pi * phase)
```

### 5.3 Procedural field — `pixel`

| | |
|---|---|
| Domain | `pixel` |
| State cost | **0 B** — no texture, no ping-pong, nothing on the node face |
| Real-time | yes |
| Cost | ~20 ALU/px × 2 073 600 px × 60 = 2.5 G ALU/s; well inside `field-realtime` §5's ">200 ALU/px needs justification" |
| Produces | concentric bands that pulse from the centre; the whole existing Ramp/Gradient node family as one kernel |

```
param float scale = 6.0 [1, 32]
d    = length(uv - vec2(0.5, 0.5))
band = 0.5 + 0.5 * sin(d * scale * pi - t * 2)
col  = vec3(band, band * 0.6, 1.0 - band)
```

### 5.4 Pure deformation — `element`

| | |
|---|---|
| Domain | `element` |
| State cost | **0 B** |
| Real-time | yes |
| Produces | a travelling wave through a mesh or point cloud, coloured by its own displacement |
| Note | `sin(P.x * freq + t)` is element-domain because it mentions `P`; `t` is hoisted out of the loop by rate inference and evaluated once per frame (`field-domains` §2) |

```
param float amp  = 0.3 [0, 2]
param float freq = 3.0 [0.5, 12]
w    = sin(P.x * freq + t)
P.y += amp * w
Cd   = vec3(0.5 + 0.5 * w, 0.4, 0.9)
```

### 5.5 Euclidean rhythm — `frame`

The Bresenham construction: step `j` of `steps` is a hit iff
`(j × pulses) mod steps < pulses`. Equal to Bjorklund's algorithm up to a
rotation for the patterns anyone actually uses, and it needs **no loop and no
memory** where Bjorklund's needs a recursive list split.

| | |
|---|---|
| Domain | `frame` |
| State cost | **0 B** — compare a step sequencer, which needs a step counter cell and can therefore drift out of sync with the transport |
| Real-time | yes |
| Produces | E(5,16) is the Bossa-Nova clave; E(3,8) the tresillo; E(7,16) a Samba. One knob sweeps continuously between them, and a transport seek lands on the right step with no resync |
| Prior art | Toussaint 2005, *The Euclidean Algorithm Generates Traditional Musical Rhythms* |

```
param float steps  = 16 [1, 32]
param float pulses = 5 [1, 32]
param float bpm    = 120 [40, 240]
param float rot    = 0 [0, 32]
j    = floor(mod(t * bpm / 60 + rot, steps))
gate = 0
if (mod(j * pulses, steps) < pulses) { gate = 1 }
```

---

## 6. Rung 1 — memory

**Definition:** one or more `state` cells. The kernel's output depends on what
it did last invocation.

**The bill arrives here.** `field-state` §3's table is the whole difference
between the entries below: the same `state float` word costs 4 B in §6.8 and
16.6 MB in §6.6.

### 6.1 One-pole lowpass — `sample`

The canonical entry. Everything else in the sample domain is this with more
cells.

| | |
|---|---|
| Domain | `sample` |
| State cost | 1 cell × 4 B × 8 voices = **32 B** |
| Real-time | yes |
| Produces | the tone control every other filter is built out of |

```
param float cutoff = 0.2 [0, 1]
state float z = 0
z += (in - z) * cutoff
out = z
```

### 6.2 State-variable filter (Chamberlin) — `sample`

Two cells, three simultaneous outputs, and a stability bound that a knob can
cross — so the bound belongs in the `param` range, not in a runtime check.

| | |
|---|---|
| Domain | `sample` |
| State cost | 2 cells × 4 B × 8 voices = **64 B** |
| Real-time | yes, bounded |
| Bound | with `f = 2 * sin(pi * fc / sr)` the topology is stable for **fc < sr/6** (8 kHz at 48 kHz). Above it the resonance runs away. Enforce it by clamping the `f` param range, never by testing `lp` at runtime |
| Produces | lowpass, bandpass and highpass from the same two cells — the three taps of a classic analogue filter, all available at once |
| Prior art | Chamberlin, *Musical Applications of Microprocessors*, 1980 |

```
param float f = 0.2 [0, 1]
param float q = 0.7 [0.05, 1]
state float lp = 0
state float bp = 0
hp  = in - lp - q * bp
bp += f * hp
lp += f * bp
out = lp
```

### 6.3 Comb / feedback delay — `sample` `[A]`

| | |
|---|---|
| Domain | `sample` |
| State cost | 4096 × 4 B = 16 KB per voice; × 8 voices = **128 KB**. The write cursor is implicit in the ring cell |
| Real-time | yes, bounded — `[A]` |
| Bound | the buffer length is a **literal**, so the maximum delay is fixed at 4096 samples = 85 ms at 48 kHz. A longer delay is a bigger literal, never a bigger param (`field-realtime` §2) |
| Produces | flanger at 1–10 ms, chorus at 20–40 ms, slapback at 85 ms; `fb` near 0.98 is the edge of self-oscillation and the ringing pitch is `sr / delay` |

```
param float fb    = 0.6 [0, 0.98]
param float delay = 1200 [1, 4095]
state float buf[4096] = 0        # OPEN-A
r = buf.read(delay)
buf.write(in + r * fb)
out = r
```

### 6.4 Verlet integration — `element`

Position Verlet: velocity is never stored, it is the difference between where
you are and where you were. One `state vec3` buys momentum for a whole point
cloud.

| | |
|---|---|
| Domain | `element` |
| State cost | `state vec3` = 3 cells × 4 B × 5 000 elements = **60 KB** |
| Real-time | yes |
| Cost | 5 000 element invocations/frame, ~12 flops each; `gravity * dt * dt` is frame-domain and hoisted, so it is computed **once**, not 5 000 times |
| Produces | a point cloud that keeps moving when you stop dragging it — a mesh with weight. The visible difference from a spring sim is that it does not need a velocity attribute and cannot desynchronise position from velocity |
| Prior art | Verlet 1967, *Computer Experiments on Classical Fluids* (Phys. Rev. 159); Jakobsen 2001 for the games form |

```
param float gravity = 9.8 [0, 40]
param float damp    = 0.99 [0.9, 1]
state vec3 prev = vec3(0, 0, 0)
cur  = P
vel  = (cur - prev) * damp
P    = cur + vel
P.y -= gravity * dt * dt
prev = cur
```

The `cur` temporary is not decoration. Without it, whether `prev` captures the
pre-update or post-update position depends on statement order, and the reader
cannot tell which integrator they are looking at.

### 6.5 Differential growth — `element` `[B]`

A curve that lengthens, pushes itself apart, and buckles into folds. The
*chain-local* version fits; the textbook version does not, and the difference
is worth stating precisely.

| | |
|---|---|
| Domain | `element` |
| State cost | `attrib float age` = 1 × 4 B × 5 000 = **20 KB**. `P` is the mesh, not a cell |
| Real-time | yes for the chain-local form — `[B]` |
| **Does not fit** | the global repulsion term. Every node repelled by every other node is 5 000² = **25 M pair tests/frame = 1.5 G/s**, and a spatial index to cut it needs data-dependent buckets — see §10.7 |
| Produces | a line that folds into brain-coral / *Ammonite*-suture patterns as it grows; the folding is the repulsion losing to the growth |
| Prior art | Turing 1952 for the morphogenesis framing; the curve form is folklore, usually credited to Pearson/Anders Hoff writeups |

```
param float springK = 0.4 [0, 1]
param float repel   = 0.02 [0, 0.2]
param float grow    = 0.001 [0, 0.01]
attrib float age = 0
a   = P.at(i - 1)                        # OPEN-B, reads the previous cook
b   = P.at(i + 1)
mid = (a + b) * 0.5
P  += (mid - P) * springK                # attract to the chain
P  += (P - mid) * repel * noise(t, i)    # push apart, jittered per element
age += grow
Cd  = vec3(clamp(age, 0, 1), 0.4, 1 - clamp(age, 0, 1))
```

`noise(t, i)` assumes the seed enters as a second argument — that is
`field-language` §12's OPEN, not a new one.

### 6.6 Trails / feedback — `pixel`

The smallest possible pixel-domain state kernel, and the one that makes the
memory cost real.

| | |
|---|---|
| Domain | `pixel` |
| State cost | 1 cell **per pixel**. R16F pair = **8.29 MB**; R32F pair = **16.6 MB** at 1080p |
| Real-time | yes, bounded |
| Bound | the bound is memory, not time. `field-state` §3: this figure must be **on the node face**, not in a tooltip. R16F is fine here because `decay` re-normalises every frame; §6.7 is not so lucky |
| Produces | motion trails that decay; the existing Trails node as five lines |

```
param float decay = 0.95 [0.8, 0.999]
state float prev = 0
prev = max(prev * decay, col.r)
col  = vec3(prev, col.g, col.b)
```

### 6.7 Reaction–diffusion (Gray–Scott) — `pixel` `[C]`

| | |
|---|---|
| Domain | `pixel` |
| State cost | 2 cells/px, packed into one RGBA32F pair (4 cells/pair). At 1080p = **66.4 MB**; at 512² = **8.39 MB** |
| Precision | **must be R/RGBA32F.** 16F drifts visibly within seconds — this is exactly the case `field-state` §3's OPEN says 16F is wrong for |
| Real-time | yes, bounded — `[C]` |
| Cost | 8 texture fetches + ~25 ALU per pixel per frame. At 512² × 60 = 15.7 M invocations/s, comfortable; at 1080p × 60 = 124 M invocations/s with 8 fetches each = **~1 G fetches/s**, which is the real ceiling, not the ALU |
| Produces | coral, fingerprints, mitosis, Turing spots — `feed`/`kill` at (0.055, 0.062) gives the classic "worms"; (0.030, 0.062) gives dividing cells |
| Prior art | Turing 1952; Gray & Scott 1983; Pearson 1993, *Complex Patterns in a Simple System* (Science 261) |

```
param float feed = 0.055 [0.01, 0.09]
param float kill = 0.062 [0.03, 0.07]
param float dA   = 1.0 [0, 1]
param float dB   = 0.5 [0, 1]
state float A = 1
state float B = 0
d    = 1.0 / res
lapA = A(uv + vec2(d.x, 0)) + A(uv - vec2(d.x, 0))
     + A(uv + vec2(0, d.y)) + A(uv - vec2(0, d.y)) - 4 * A
lapB = B(uv + vec2(d.x, 0)) + B(uv - vec2(d.x, 0))
     + B(uv + vec2(0, d.y)) + B(uv - vec2(0, d.y)) - 4 * B
r    = A * B * B
A   += dA * lapA - r + feed * (1 - A)
B   += dB * lapB + r - (kill + feed) * B
col  = vec3(B, B * 0.6, 1 - B)
```

### 6.8 Logistic map — `frame`

| | |
|---|---|
| Domain | `frame` |
| State cost | 1 cell = **4 B** |
| Real-time | yes, bounded |
| Bound | `x` in [0,1] is invariant **only** for `r ≤ 4`. At `r = 4.01` the orbit escapes to infinity in about 20 steps. Enforce it in the `param` range — and do **not** clamp `x`, because clamping replaces the map with a different, boring map |
| Produces | one knob that walks a modulator from a fixed point (r < 3) through a 2-cycle (3.0), a 4-cycle (3.45), an 8-cycle (3.54), into chaos at the Feigenbaum point **r = 3.56995** — and back out through the period-3 window at r = 3.83. A modulation source with a *route* through it, not a shape |
| Prior art | May 1976, *Simple mathematical models with very complicated dynamics* (Nature 261) |

```
param float r = 3.7 [2.5, 4.0]
state float x = 0.5
x = r * x * (1 - x)
```

### 6.9 Hénon map — `frame`

| | |
|---|---|
| Domain | `frame` |
| State cost | 2 cells = **8 B** |
| Real-time | yes, bounded |
| Bound | bounded (the strange attractor) only near a ≈ 1.4, b ≈ 0.3. Outside that region the orbit diverges; the `param` ranges above are the safe basin |
| Produces | two correlated modulators that never repeat and stay inside [-1.3, 1.3] — plot (x, y) and you get the Hénon attractor's folded ribbon. Unlike two free-running LFOs, the pair is *related*, which is why it reads as a gesture rather than as noise |
| Prior art | Hénon 1976, *A two-dimensional mapping with a strange attractor* |

```
param float a = 1.4 [1.0, 1.45]
param float b = 0.3 [0.0, 0.4]
state float x = 0
state float y = 0
nx = 1 - a * x * x + y
y  = b * x
x  = nx
```

`y = b * x` reads the old `x` because `x` has not been written yet in this
invocation — §3.3. Swap the last two lines and you get a different, wrong map.

### 6.10 Small Markov chain — `frame`

The table-free form. Honest about where it stops scaling.

| | |
|---|---|
| Domain | `frame` |
| State cost | 2 cells = **8 B** |
| Real-time | yes |
| Scales to | ~4 states. A general S-state chain with distinct per-transition weights is **S² params and an S²-arm branch cascade**: 16 at S = 4 (readable), 144 at S = 12 (unwritable). Past that it needs `[A]`'s table — which is §9.2 |
| Produces | a sequence that wanders with a memory of where it just was — the difference between "random note" and "random note that avoids repeating" |
| Prior art | Markov 1913; Hiller & Isaacson 1958, *Illiac Suite*; Conklin 2003 for the generation framing |

```
param float stay = 0.6 [0, 1]
param float rate = 4 [0.5, 16]
state float s     = 0
state float phase = 0
phase += dt * rate
if (phase >= 1) {
  phase -= 1
  u = rand(t, 17)
  if (u > stay) { s = mod(s + 1 + floor(u * 2), 3) }
}
note = 60 + s * 4
```

---

## 7. Rung 2 — prediction

**Definition:** the kernel builds a model of what comes next and *acts on the
guess* before the real value arrives.

**What rung 2 costs:** the guess can be wrong, and every rung-2 algorithm has a
signature failure that a user will hear or see. Those signatures are listed —
they are not bugs to hide, they are the material.

| Entry | Signature of a wrong prediction |
|---|---|
| LPC (§7.1) | the residual stops being buzzy and starts sounding like the input — the model has learned nothing |
| pitch repeat (§7.2) | a metallic buzz at `sr / period` — the sound of a dropped VoIP packet |
| dead reckoning (§7.3) | overshoot ringing; the visual hits *before* the beat and snaps back |
| advection (§7.4) | smear along the wrong axis; the aperture problem made visible |
| predictive collision (§7.5) | a point that stops one frame early, hovering above the floor |

### 7.1 Linear predictive coding — `sample`

Predict this sample from a weighted sum of the last L. What the model cannot
predict is the **residual**, and the residual is the interesting output.

| | |
|---|---|
| Domain | `sample` |
| State cost | **order-16 LPC in the sample domain at 8 voices = 16 cells × 4 B × 8 = 512 B** of history. The 16 coefficients are frame-domain `param`s shared across voices: 16 × 4 B = **64 B**. Total **576 B** |
| Real-time | yes |
| Cost | 16 MAC + 16 shifts per sample × 48 000 × 8 voices = **6.1 M MAC/s** — under 1% of the audio budget |
| Produces | `out = in - pred` is the LPC residual: a buzzy glottal-pulse-like excitation with the spectral envelope removed. Feed a *different* excitation through the inverse filter and you have cross-synthesis — someone else's voice with your spectrum. This is the talkbox, built from arithmetic instead of a tube |
| Ergonomics | order 16 hand-unrolled is 16 `state` lines + 16 shift lines + 16 params. **This is OPEN-A's strongest argument** |
| Prior art | Itakura & Saito 1968; Atal & Hanauer 1971, *Speech Analysis and Synthesis by Linear Prediction of the Speech Wave* (JASA 50:2); Makhoul 1975 for the tutorial |

Order 4 shown; order 16 is the same text four times over.

```
param float a1 = 1.8 [-3, 3]
param float a2 = -1.2 [-3, 3]
param float a3 = 0.5 [-3, 3]
param float a4 = -0.1 [-3, 3]
state float x1 = 0
state float x2 = 0
state float x3 = 0
state float x4 = 0
pred = a1 * x1 + a2 * x2 + a3 * x3 + a4 * x4
x4 = x3
x3 = x2
x2 = x1
x1 = in
out = in - pred
```

The synthesis direction is the same block with the history taken from the
**output** instead of the input (`y1..y4` instead of `x1..x4`) and
`out = exc + pred`. Fixed coefficients make this rung 2; letting the
coefficients chase the error makes it rung 3 (§8.2).

### 7.2 Pitch-period repetition — `sample` `[A]`

The simplest useful predictor in audio: **the next period will look like the
last one.**

| | |
|---|---|
| Domain | `sample` |
| State cost | 2048 × 4 B = 8 KB per voice; × 8 voices = **64 KB** |
| Real-time | yes, bounded — `[A]` |
| Bound | the buffer is a literal, so the **longest representable period is 2048 samples = 42.7 ms at 48 kHz = a lowest pitch of 23.4 Hz**. A lower pitch needs a bigger literal and a bigger patch, never a bigger param |
| Produces | infinite sustain / freeze with no grain window and no crossfade artefacts *when the period is right*; a metallic buzz at `sr / period` when it is wrong. Sweep `period` while frozen and you get a formant-preserving pitch smear, which is a different instrument from a granular freeze |
| Needs | a period **estimate** to sound like sustain rather than glitch — that is §8.5's job, and it is exactly the rung 2 → rung 3 step |
| Prior art | Goodman et al. 1986, waveform substitution for packet voice; ITU-T G.711 Appendix I (packet-loss concealment) |

```
param float period = 240 [40, 2048]
param float freeze = 0 [0, 1]
state float buf[2048] = 0        # OPEN-A
src = in
if (freeze > 0.5) { src = buf.read(period) }
buf.write(src)
out = src
```

When `freeze` is on the buffer feeds itself one period back and the sound
sustains forever with zero further input. That is the whole algorithm.

### 7.3 Dead reckoning — `frame`

Extrapolate a control value forward to cancel the latency between an audio
transient and the frame that reacts to it.

| | |
|---|---|
| Domain | `frame` |
| State cost | 1 cell = **4 B** |
| Real-time | yes, bounded |
| Bound | `lead` past ~3 frames overshoots on every transient and the response rings. The `param` range is the bound |
| Produces | bass-driven visuals that land **on** the kick rather than 2 frames behind it. The audible/visible tell of too much lead is a visual that anticipates and then snaps back — the eye reads that as a stutter, not as tightness |
| Note | `reduce.rms(in, 20, 200)` is the sample → frame crossing (`field-domains` §3); the rest is frame domain and free |

```
param float lead = 2.0 [0, 6]
state float prev = 0
level = reduce.rms(in, 20, 200)
vel   = level - prev
prev  = level
ahead = level + vel * lead
```

### 7.4 Optical-flow advection — `pixel` `[C]`

Estimate where each pixel's content came from, then carry the previous frame
along that flow. **This is prediction, not filtering**: the frame is
reconstructed from a motion model rather than blended.

| | |
|---|---|
| Domain | `pixel` |
| State cost | `state vec3 prev` (3) + `state vec2 flow` (2) = **5 cells/px** → 2 RGBA32F ping-pong pairs = **132.8 MB at 1080p**; RGBA16F = 66.4 MB; with the flow field at half res (960×540) = **83 MB** |
| Real-time | yes, bounded — `[C]`. The bound is memory, and at 132.8 MB it is the single largest line item any Field kernel can produce |
| Honest limitation | this is **normal flow**, not full optical flow. A single-pixel gradient recovers only the flow component along the image gradient — the aperture problem. Full 2-D flow needs a neighbourhood least-squares (Lucas–Kanade, 5×5 = 25 taps × 2 fields = 50 fetches/px). That is still **bounded and therefore still fits**; it costs 6× the fetches |
| Produces | content that keeps moving along its own motion after the source freezes — a smear that follows the subject instead of the frame. Freeze the input and the image keeps sliding for as long as `dissipate` allows |
| Prior art | Horn & Schunck 1981; Lucas & Kanade 1981; Stam 1999 for the semi-Lagrangian backward-trace form used here |

```
param float flowScale = 1.0 [0, 4]
param float dissipate = 0.98 [0.9, 1]
state vec3 prev = vec3(0, 0, 0)
state vec2 flow = vec2(0, 0)
d   = 1.0 / res
gx  = (prev(uv + vec2(d.x, 0)).r - prev(uv - vec2(d.x, 0)).r) * 0.5
gy  = (prev(uv + vec2(0, d.y)).r - prev(uv - vec2(0, d.y)).r) * 0.5
gt  = col.r - prev.r
den = gx * gx + gy * gy + 0.0001
flow = lerp(flow, vec2(-gt * gx / den, -gt * gy / den) * flowScale, 0.5)
prev = lerp(col, prev(uv - flow * d), dissipate)
col  = prev
```

### 7.5 Predictive collision — `element`

Resolve against where the point **will be**, not where it is. The difference
only shows at speed, and then it shows completely.

| | |
|---|---|
| Domain | `element` |
| State cost | `state vec3 prev` = 3 cells × 4 B × 5 000 = **60 KB** (shared with §6.4 if in the same kernel) |
| Real-time | yes |
| Produces | particles that bounce off a floor at any speed. Detection-after-the-fact loses fast points through the floor entirely — the visible artefact is a fountain that leaks |
| Failure signature | too small a `vel` estimate on the first frame after a reset makes a point stop one frame early, hovering |

```
param float floorY = 0 [-4, 4]
param float bounce = 0.5 [0, 1]
state vec3 prev = vec3(0, 0, 0)
cur  = P
vel  = cur - prev
next = cur + vel
if (next.y < floorY) { vel.y = -vel.y * bounce }
P    = cur + vel
prev = cur
```

---

## 8. Rung 3 — self-correction

**Definition:** the kernel measures its own error and changes its own
parameters to reduce it. The model is no longer written by the user.

**Every rung-3 entry carries a stability bound.** That is the defining cost of
the rung: a self-correcting system with the gain set too high does not degrade
gracefully, it diverges. The bound goes in the `param` range, because
`field-realtime` §1 rule 3 forbids the runtime convergence test that would
otherwise catch it.

| Entry | The knob that diverges | The bound |
|---|---|---|
| LMS / NLMS (§8.1) | `mu` | `0 < mu < 2` (NLMS) |
| adaptive LPC (§8.2) | `mu` | same, plus leakage |
| constraint relaxation (§8.3) | `stiff` × passes | `stiff ≤ 1`, passes a literal |
| fluid projection (§8.4) | Jacobi count vs. `dt` | CFL: `vel * dt < 1` cell |
| PLL (§8.5) | `kp`, `ki` | `ki ≈ kp² / 4` for critical damping |
| PI auto-gain (§8.6) | `ki` without anti-windup | clamp the integrator, with a literal |

### 8.1 LMS adaptive filter — the exemplar

**Widrow & Hoff, "Adaptive switching circuits", IRE WESCON Convention Record,
1960.** Sixty-five years old, four lines of arithmetic, and it is the whole of
rung 3 in miniature: a filter that measures how wrong it is and edits itself.

#### 8.1.1 The rule

| Step | | |
|---|---|---|
| 1 | **filter** | `y = Σ w_k · x_{n-k}` for k = 0 … L−1 |
| 2 | **error** | `e = d − y` where `d` is the desired signal |
| 3 | **update** | `w_k ← w_k + mu · e · x_{n-k}` |

Step 3 is a stochastic gradient step on `e²`: the gradient of `e²` with respect
to `w_k` is `−2·e·x_{n-k}`, so moving *against* it is exactly the line above.
There is no matrix, no inverse and no accumulator — that is the entire reason it
fits and RLS (§10.3) does not.

#### 8.1.2 Step size and the stability bound

| Form | Update | Bound | Why you would pick it |
|---|---|---|---|
| **LMS** | `w += mu * e * x` | `0 < mu < 2 / (L · Px)`, `Px = E[x²]` per tap | one multiply cheaper |
| **NLMS** | `w += (mu / (eps + Σx²)) * e * x` | **`0 < mu < 2`**, independent of signal level | the only sane choice for a knob a user turns |
| **leaky NLMS** | `w = w * (1 - mu*gamma) + ...` | as NLMS | stops the weights drifting without bound on a DC-only or silent input |

**Use NLMS.** LMS's bound depends on the input power, so a `mu` that is stable
for a quiet input diverges when the user turns up the gain — the bound moves
under the knob. NLMS's bound is the constant 2, so a `param float mu [0.001,
1.9]` range *is* the stability proof.

| Quantity | Expression | At L = 32, mu = 0.3 |
|---|---|---|
| convergence time constant | ≈ L / (2 · mu) samples | ≈ 53 samples ≈ 1.1 ms |
| practical lock-on (to ~1% misadjustment) | ≈ 10–20 τ | **~15 ms** |
| misadjustment (excess MSE over the Wiener optimum) | ≈ mu / 2 | **15%** |

The trade is one line: **small `mu` converges slowly and settles accurately;
large `mu` converges fast and never settles.** Both ends are musically useful,
which is why `mu` is a knob and not a constant.

#### 8.1.3 State cost

Order 4 shown. The scaling is linear, which is the point.

| Order L | `w` cells | `x` cells | power cell | bytes, mono | × 8 voices |
|---|---|---|---|---|---|
| 4 | 4 | 4 | 1 | 9 × 4 = **36 B** | 288 B |
| 16 | 16 | 16 | 1 | 33 × 4 = **132 B** | 1.03 KB |
| **32** | 32 | 32 | 1 | 65 × 4 = **260 B** | **2.03 KB** |
| 128 | 128 | 128 | 1 | 257 × 4 = **1.03 KB** | 8.22 KB |

Compute at L = 32: `2L + 6` ≈ **70 flops/sample** → 70 × 48 000 = 3.4 M flop/s
mono, 26.9 M flop/s at 8 voices. Against the ~5.3 ms callback budget that is
noise.

#### 8.1.4 Real-time verdict

**Yes, unconditionally.** Every loop bound is a literal, nothing allocates,
there is no branch in the inner arithmetic, and no iteration count depends on
data. The only hazard is numerical, and §8.1.2's `param` range is the guard.

#### 8.1.5 The code — adaptive line enhancer form

The textbook two-input form (desired + reference) needs a second sample input,
which Field does not have — **OPEN-D**. The **adaptive line enhancer** form
needs only `in`: the reference is the *delayed* input, so the filter predicts
the input from its own past. Mathematically identical update; one input.

```
param float mu = 0.3 [0.001, 1.9]     # NLMS: stable for 0 < mu < 2
state float x1 = 0
state float x2 = 0
state float x3 = 0
state float x4 = 0
state float w1 = 0
state float w2 = 0
state float w3 = 0
state float w4 = 0
state float pw = 1
y   = w1 * x1 + w2 * x2 + w3 * x3 + w4 * x4
e   = in - y
pw  = lerp(pw, x1 * x1 + x2 * x2 + x3 * x3 + x4 * x4, 0.001)
g   = mu * e / (0.000001 + pw)
w1 += g * x1
w2 += g * x2
w3 += g * x3
w4 += g * x4
x4 = x3
x3 = x2
x2 = x1
x1 = in
out = y
```

#### 8.1.6 What it sounds like

| Output | Sound |
|---|---|
| `out = y` | every steady tone in the input is pulled out and left ringing alone, with a **~15 ms lock-on swoop** at mu = 0.3 as the weights converge. It is a resonator that finds its own pitch — and it re-finds it when the pitch slides |
| `out = e` | the same tones are **removed**. A de-hummer that tracks a drifting 50/60 Hz and all its harmonics with no knob, and keeps working while the mains frequency wanders. Point it at a howling PA and it kills the feedback tone in tens of milliseconds |
| `mu` past ~1.5 | lock-on becomes a warble: the weights chase the signal *within* each period and the output gains a rough, ring-modulated edge. This is a good sound and it is one knob away from the clean one |
| `mu` past 2.0 | divergence, audibly, in about 40 ms — a rising screech into full-scale clipping. The `param` range is what stops it |
| L = 4 | catches one partial |
| L = 32 | catches a small chord |
| L = 128 | catches a formant structure and starts to sound like a talkbox |

#### 8.1.7 Why this is the exemplar and not just an example

- **It is the smallest program that could not be a knob.** Four lines of update
  produce behaviour no static filter can, and no amount of modulation routing
  reproduces it, because the coefficients depend on the signal.
- **It is 260 bytes.** The rung-3 capability costs less memory than one
  400-sample delay line.
- **It is audible in under a second.** A demo does not need a chart.
- **It exercises the whole sample backend at once:** `state`, an inner product,
  a division, a normalisation, and a `param` reaching the audio thread through
  `ParamMailbox`.

### 8.2 Adaptive LPC — `sample`

§7.1's coefficients, updated by §8.1's rule instead of by hand. Prediction
becomes self-correction with one substitution.

| | |
|---|---|
| Domain | `sample` |
| State cost | order 16: (16 history + 16 weights + 1 power) × 4 B = **132 B** mono; × 8 voices = **1.03 KB** |
| Real-time | yes, bounded — same NLMS bound as §8.1 |
| Produces | a filter that tracks the input's formants with no analysis band bank and no FFT — a vocoder whose bands are learned rather than declared. Excite it with a different source and the second source speaks in the first's voice, *live*, with the ~15 ms lock-on as its characteristic sound |

### 8.3 Position-based dynamics / constraint relaxation — `element` `[B]`

Verlet (§6.4) integrates; the relaxation passes measure how badly each
constraint is violated and push the positions back. That measure-then-correct
loop is what puts it on rung 3.

| | |
|---|---|
| Domain | `element` |
| State cost | `state vec3 prev` = 3 cells × 4 B × 5 000 = **60 KB** — the passes add none, because the constraint error is re-derived each pass rather than stored |
| Real-time | yes, bounded — `[B]` |
| Cost | 8 passes × 5 000 = **40 000 element invocations/frame**, 8× the single-pass load. `field-realtime` §5 gives the element domain ~2 ms/frame total, so **4 passes is the safe default and 8 needs measurement.** The pass count must be a literal, never a param — §10.6 |
| Bound | `stiff ≤ 1`. Above 1 each pass overshoots and the chain oscillates instead of settling; the visible tell is a rope that shivers at rest |
| Produces | rope, chain and cloth that hold their length instead of sagging. The difference from a spring sim is visible immediately: springs stretch under load, constraints do not |
| Prior art | Jakobsen 2001, *Advanced Character Physics* (GDC); Müller et al. 2007, *Position Based Dynamics* (J. Vis. Commun. Image R. 18:2) |

```
param float rest  = 0.1 [0.01, 1]
param float stiff = 0.5 [0, 1]
state vec3 prev = vec3(0, 0, 0)
cur  = P
vel  = (cur - prev) * 0.99
P    = cur + vel
P.y -= 9.8 * dt * dt
prev = cur
for (k = 0; k < 8; k++) {
  a   = P.at(i - 1)                        # OPEN-B
  d   = P - a
  len = max(length(d), 0.0001)
  err = len - rest
  P  -= (d / len) * err * stiff * 0.5
}
```

### 8.4 Fluid pressure projection — `pixel` `[C]`

Advection alone smears. The **projection** step measures the velocity field's
divergence — how much the field is creating or destroying fluid — and subtracts
a pressure gradient to remove it. That measurement-and-correction is the whole
reason fluid curls instead of blurring.

| | |
|---|---|
| Domain | `pixel` |
| State cost | `vel` (2) + `prs` (1) + `dye` (3) = **6 cells/px** → 2 RGBA32F pairs. At 512² = **16.8 MB**; at 1080p = **132.8 MB** |
| Real-time | yes, bounded — `[C]` |
| Cost | 20 Jacobi passes at 512² = 20 × 262 144 = **5.24 M pixel invocations/frame** = 314 M/s at 60 fps — **2.5× the entire full-screen 1080p pixel budget** of 124 M/s. So: 512² with 20 passes, **or** 1080p with ≤ 8 passes. Not both |
| Bound | CFL: the backward trace `uv - vel * d` must move less than one cell per step, i.e. `max(vel) * dt < 1` cell. Break it and the advection samples past its own neighbourhood and the sim explodes into blocky noise |
| Produces | smoke and ink that **curl** — vortices, sheets that roll up at their edges. Delete the projection lines and the same code produces a directional blur. The vortices are literally what the correction buys |
| Prior art | Stam 1999, *Stable Fluids* (SIGGRAPH); Harris 2004, *Fast Fluid Dynamics Simulation on the GPU* (GPU Gems ch. 38) |

```
param float visc = 0.0001 [0, 0.01]
state vec2  vel = vec2(0, 0)
state float prs = 0
state vec3  dye = vec3(0, 0, 0)
d = 1.0 / res
v = vel(uv - vel * d)
div = (vel(uv + vec2(d.x, 0)).x - vel(uv - vec2(d.x, 0)).x
     + vel(uv + vec2(0, d.y)).y - vel(uv - vec2(0, d.y)).y) * 0.5
prs = (prs(uv + vec2(d.x, 0)) + prs(uv - vec2(d.x, 0))
     + prs(uv + vec2(0, d.y)) + prs(uv - vec2(0, d.y)) - div) * 0.25
vel = v - vec2(prs(uv + vec2(d.x, 0)) - prs(uv - vec2(d.x, 0)),
               prs(uv + vec2(0, d.y)) - prs(uv - vec2(0, d.y))) * 0.5
dye = dye(uv - vel * d)
col = dye
```

One Jacobi pass per cook; 20 cooks per frame is 20 passes. **The pass count is a
node-graph property, not a `while` loop** — that is how a bounded iterative
solver stays inside `field-realtime` §1 rule 3.

### 8.5 Phase-locked loop / tempo tracker — `frame`

Measures the distance between where it *thinks* the beat is and where an onset
actually landed, then corrects both the phase and the period.

| | |
|---|---|
| Domain | `frame` |
| State cost | 3 cells = **12 B** |
| Real-time | yes, bounded |
| Bound | `kp` and `ki` form a second-order loop. **`ki ≈ kp² / 4` is critical damping.** Above it the tracker overshoots and the tempo visibly hunts on every syncopation; below it, it never catches a tempo change |
| Produces | sequencers and visuals that lock to a live drummer or a DJ set and *stay* locked through a drift. At high `kp` it audibly hunts — which, on purpose, is the sound of a machine trying to follow a human |
| Prior art | Large & Kolen 1994, *Resonance and the perception of musical meter*; Ellis 2007 for the offline dynamic-programming cousin that does **not** fit — §10.6 |

```
param float kp = 0.08 [0, 0.5]
param float ki = 0.002 [0, 0.05]
state float phase  = 0
state float period = 0.5
state float prev   = 0
flux  = reduce.rms(in, 60, 200)
onset = clamp((flux - prev) * 10, 0, 1)
prev  = lerp(prev, flux, 0.2)
phase += dt / period
if (phase >= 1) { phase -= 1 }
err = phase
if (err > 0.5) { err -= 1 }
if (onset > 0.5) {
  phase  -= kp * err
  period += ki * err * period
}
beat = 1 - phase
```

Feed `period` into §7.2's `period` param and the pitch-repeat freeze becomes
beat-synchronous — that is a rung-3 system correcting a rung-2 one.

### 8.6 PI auto-gain — `frame`

| | |
|---|---|
| Domain | `frame` |
| State cost | 2 cells = **8 B** |
| Real-time | yes, bounded |
| Bound | the integrator **must** be clamped with a literal. Without `clamp(acc, -4, 4)`, a long silence winds `acc` up without limit and the moment sound returns the gain slams to maximum — the classic integral-windup failure, and here it is loud |
| Produces | a level that holds through a quiet passage without a compressor's pumping, because the correction is on the *average*, not the envelope |

```
param float target = 0.2 [0, 1]
param float kp = 0.5 [0, 4]
param float ki = 0.05 [0, 1]
state float acc  = 0
state float gain = 1
level = reduce.rms(in, 20, 18000)
err   = target - level
acc  += err * dt
acc   = clamp(acc, -4, 4)
gain  = clamp(1 + kp * err + ki * acc, 0.05, 20)
```

The integrator is the only cell here that can misbehave, and the two clamps are
the whole safety story: `acc` is bounded by ±4 by construction and `gain` by
`[0.05, 20]`, so no combination of `kp`, `ki` and a silent input can produce an
unbounded output. That is what a rung-3 bound looks like when it is cheap — two
literals in two `clamp` calls, guarding 8 bytes of state.

---

## 9. Rung 4 — learning from the user

**Definition:** the model is written by neither the user nor the algorithm
designer. It accumulates from what *this particular person* keeps doing, and it
is expected to still be there tomorrow.

**What rung 4 costs, and it is not compute.** Every entry below is
arithmetically trivial — a handful of exponentially-weighted means. The cost is
three structural things the earlier rungs never had to pay.

| The cost | The rule it collides with |
|---|---|
| the model must **survive a reload** | `field-state` §6 is still OPEN. Under its option (b) — frame/sample cells persist, element/pixel cells are reinitialised — **every `pixel`- and `element`-domain rung-4 entry silently demotes to rung 3 on the next patch load** |
| the model is **per user, not per patch** | `field-state` §6 keys state into the **patch file**. A preference learned in one patch is invisible in the next one. Field has no per-user store, and adding one is a larger decision than any syntax question in §4 |
| the model must **not reset on a seek** | `field-state` §5 is one rule with no exceptions: seek, loop and stop return every cell to its declared initial value. A habit forgotten every time the user scrubs is not a habit. **This is the sharpest conflict in this document, and it has no workaround inside the current rules** |

### 9.0 Where rung 4 is allowed to live, and why

Four of the six entries below are `frame` and one is `graph`. That is not a
stylistic choice — each one is pushed there by a bound it cannot meet anywhere
finer.

| Entry | Domain | Why it cannot be `sample` | Why it cannot be `pixel` or `element` |
|---|---|---|---|
| 9.1 param prior | `frame` | a `sample` cell is allocated **per voice** (`field-state` §3). Eight voices are eight learners, each seeing 1/8 of the gestures, and none of them is "the user". A sample kernel also cannot see a knob *stop moving*: frame values arrive pre-smoothed through `ParamMailbox::SmoothedValue`, so the stillness test has nothing left to test | 12 B in `frame` becomes 60 KB in `element` (3 cells x 4 B x 5 000) and **49.8 MB** in `pixel` (three R32F ping-pong pairs, §3.2), for a quantity that has exactly one value |
| 9.2 transition table | `frame` | 580 B × 8 voices = 4.6 KB of per-voice tables that each learn a different eighth of the same melody; and a note event is not a sample-rate quantity | one table, not N copies of it |
| 9.3 groove | `frame` | as 9.2, per voice; and the 60 Hz grid is already finer than the ~10 ms timing deviation being learned | same |
| 9.4 control curve | `frame` | per voice again, and the warp must be applied **before** the mailbox or every voice gets a different curve for the same knob | same |
| 9.5 palette | model in `frame`, application in `pixel` | the model is six floats; there is nothing to win and a GPU readback to lose | **this is the interesting one** — the split is the entry; see 9.5 |
| 9.6 co-occurrence | `graph` | the 167-iteration argmax is 64.1 M iterations/s at 48 kHz × 8 voices, for a quantity that changes when a cable is dragged | there is no per-pixel or per-vertex notion of "which node type" |

**The pattern is not a coincidence.** Rung 4's state is *one model, not one model
per element* — that is what "this particular person" means. Its natural home is
therefore the domain with exactly one element: `frame` has one per frame,
`graph` has one per edit. `element`, `pixel` and `sample` all multiply the model
by their element or voice count, and rung 4 is the first rung where that
multiplication buys nothing whatsoever.

### 9.1 Per-user parameter prior — `frame`

An exponentially-weighted mean and variance of where the user *leaves* a knob,
not where they drag it through. The stillness test is the whole algorithm.

| | |
|---|---|
| Domain | `frame` |
| State cost | 3 cells × 4 B = **12 B** per tracked param. 8 tracked params = 96 B; at `ParamMailbox::kMaxParams` = 128 it is 128 × 12 = **1.5 KB**, still smaller than one order-32 NLMS filter at 8 voices (§8.1.3) |
| Real-time | yes |
| Bound | `rate` must be slower than a gesture or the prior learns the *move* instead of the habit. At 60 fps, `rate = 0.004` is a 250-frame time constant ≈ **4.2 s**; anything above ~0.02 (0.8 s, one knob sweep) tracks the sweep itself and the "default" moves while you watch it |
| Produces | the node's reset value and a modulator's centre point migrate to where this user actually parks the knob. `sqrt(var)` is the second half: it is how wide a modulation range this user tolerates on that control, so a `Random` bound to it stops needing a depth knob |

```
param float x    = 0.5 [0, 1]
param float rate = 0.004 [0.0005, 0.05]
state float mu   = 0.5
state float var  = 0
state float prev = 0.5
still = 0
if (abs(x - prev) < 0.0005) { still = 1 }
prev = x
mu  += still * rate * (x - mu)
var += still * rate * ((x - mu) * (x - mu) - var)
```

`still` is a multiplier rather than a branch around the two updates on purpose:
in `frame` a branch is free (`field-realtime` §4), but the same five lines
transplanted into `element` or `pixel` would then be predicated and pay for both
sides anyway. Written as a multiply, the cost is identical in every domain.

### 9.2 Learned note-transition table — `frame` `[A]` `[D]` — SPEC-ONLY

§6.10's Markov chain with the transition weights learned from what the user
plays instead of collapsed into one `stay` knob.

| | |
|---|---|
| Domain | `frame` |
| State cost | 12 × 12 pitch-class table = 144 cells + 1 `last` cell = 145 × 4 B = **580 B**. Against §6.10's hand-written 3-state chain at 8 B: **72× the memory for 144 learned transitions and one fewer knob** |
| Real-time | yes — both loops have the literal bound 12, so 24 iterations per frame = 1 440/s |
| Needs | **OPEN-A option (c), not (b).** A ring cell can be written at a cursor and read at an offset; a transition table needs an arbitrary index on *both* sides. §4's OPEN-A lists (c) and this document assumed (b) throughout — **§9.2 is the entry that forces the choice between them.** Also OPEN-D, for `noteOn` / `notePitch` |
| Bound | rows stay distributions **by construction**: each row starts at 1/12, is decayed by `(1 - lr)` and handed back exactly `lr`, so it sums to 1 forever with no normalisation pass and no drift. `bias` blends toward the uniform row so a cold table is a random sequencer rather than a broken one |
| Produces | play for two minutes and the arpeggiator starts finishing your phrases — it offers the intervals you use and stops offering the ones you never play. `bias = 0` is uniform random; `bias = 1` is your own vocabulary with nothing else in it, and the sweep between them is the knob |
| Prior art | Markov 1913; Hiller & Isaacson 1958, *Illiac Suite*; Pachet 2002, *The Continuator: Musical Interaction with Style* (J. New Music Research 32:3) for the learn-from-the-player framing |

```
param float lr   = 0.08 [0.005, 0.5]
param float bias = 0.7 [0, 1]
state float tab[144] = 0.083333      # OPEN-A option (c): 12 x 12, rows = 1/12
state float last = 0
if (noteOn > 0.5) {                  # OPEN-D
  pc = floor(mod(notePitch, 12))
  for (k = 0; k < 12; k++) {
    tab[last * 12 + k] *= 1 - lr
  }
  tab[last * 12 + pc] += lr
  last = pc
}
u   = rand(t, 23)
acc = 0
cur = 0
for (k = 0; k < 12; k++) {
  acc += lerp(0.083333, tab[last * 12 + k], bias)
  if (acc < u) { cur = k + 1 }
}
note = 60 + clamp(cur, 0, 11)
```

`last * 12 + k` is a **float** used as an index. OPEN-A option (c) says it
"needs int/modulo semantics and a bounds decision" — this line is exactly that
decision, and it needs a truncation rule (`floor`? round-to-nearest? error on a
non-integral index?) as well as a clamp rule. Do not pick it silently.

### 9.3 Learned groove — `frame` `[D]` — SPEC-ONLY

Two numbers: how late this user plays on the on-beats, and how late on the
off-beats. Their **mean** is laid-back-ness and their **difference** is swing.

| | |
|---|---|
| Domain | `frame` |
| State cost | 3 cells × 4 B = **12 B**. A per-slot table over 16 slots would be 16 + 1 = 68 B and would need OPEN-A; two slots does not, and two slots is what swing actually is |
| Real-time | yes, bounded |
| Bound | `rate` again. At 120 bpm one bar is 120 frames; `rate = 0.05` is a 20-hit time constant ≈ 3 bars of 8ths, which is slow enough to average out a flam and fast enough to follow a change of feel. Above ~0.3 the groove tracks a single late hit and the sequencer lurches |
| Needs | OPEN-D, for `noteOn` |
| Produces | the sequencer stops sounding quantised. It lands where you land — behind the beat if you play behind it, and at *your* swing ratio rather than a menu of 54% / 58% / 62%. `pull = 0` gives the grid back for an instant A/B |
| Prior art | Bilmes 1993, *Timing is of the Essence* (MIT MSc) for the expressive-deviation framing; Wright & Berdahl 2006, *Towards Machine Learning of Expressive Microtiming* (ICMC) |

```
param float bpm  = 120 [40, 240]
param float div  = 4 [1, 16]
param float rate = 0.05 [0.005, 0.4]
param float pull = 1.0 [0, 1]
state float late0 = 0
state float late1 = 0
state float clock = 0
clock = mod(clock + dt * bpm / 60, 4)
s    = clock * div
slot = floor(mod(s, 2))
err  = mod(s + 0.5, 1) - 0.5
if (noteOn > 0.5) {                          # OPEN-D
  if (slot < 0.5) { late0 += rate * (err - late0) }
  else            { late1 += rate * (err - late1) }
}
off = late0
if (slot > 0.5) { off = late1 }
gate = 0
if (mod(s - off * pull, 1) < 0.08) { gate = 1 }
```

`err = mod(s + 0.5, 1) - 0.5` is the signed distance to the nearest slot in slot
units, in `[-0.5, 0.5)` — one line, no branch, and it is why the whole groove
model is 12 bytes. `clock` is wrapped to a 4-beat bar rather than left to run,
because a free-running float32 accumulator loses millisecond resolution after a
few hours of uptime, and the groove is measured in milliseconds.

### 9.4 Adaptive control curve — `frame`

The knob's response bends so that the middle of its travel lands on the value
this user actually settles at. Schlick's bias curve is chosen because it needs
only `+ - * /` — no `pow`, no `log` — so it costs the same in every domain.

| | |
|---|---|
| Domain | `frame` |
| State cost | 2 cells × 4 B = **8 B** per curved knob. 8 curved knobs = 64 B; all 128 mailbox slots curved = **1 KB** |
| Real-time | yes, bounded |
| Bound | `b` **must** be clamped away from both ends with literals: at `b = 0` the denominator `(1/b - 2)(1 - x) + 1` divides by zero and at `b = 1` the curve inverts. `clamp(…, 0.02, 0.98)` is the entire guard, in the same shape as §8.6's integrator clamp. Second bound: `rate` fast enough to move within one gesture makes the knob feel rubbery — the same physical travel produces a different amount on each pass, which users read as a failing encoder rather than as help |
| Produces | after ten minutes, the useful part of a `[20, 20000]` cutoff knob is under the middle of the travel instead of squeezed into the bottom 15%. Somebody who lives between 200 and 900 Hz gets 200–900 Hz spread across the middle third of the arc, without ever opening a curve editor |
| Prior art | Schlick 1994, *Fast Alternatives to Perlin's Bias and Gain Functions* (Graphics Gems IV) |

```
param float x    = 0.5 [0, 1]
param float rate = 0.003 [0.0005, 0.05]
param float bend = 1.0 [0, 1]
state float mu   = 0.5
state float prev = 0.5
b = clamp(lerp(0.5, mu, bend), 0.02, 0.98)
y = x / ((1 / b - 2) * (1 - x) + 1)
if (abs(x - prev) < 0.0005) { mu += rate * (y - mu) }
prev = x
```

`mu` builds `b`, `b` builds `y`, and `y` updates `mu` — a cycle, and a legal one,
because it passes through the `state` cell's delay (`field-state` §2). That
cycle is also why the curve settles instead of drifting: at `x = 0.5` the warp
gives `y = b = mu` **exactly**, so a user who has stopped moving contributes a
zero update and the learned centre is a fixed point. Learning from the *raw*
`x` instead would have the opposite behaviour — the user re-settles at mid-travel
after each bend, `mu` walks back toward 0.5, and the curve un-bends itself.

### 9.5 Learned palette — model in `frame`, application in `pixel`

Keep a running mean and spread of the frames the user marks as keepers, then
grade every later frame toward them. The split between where the model lives and
where it is applied **is** the entry.

| | |
|---|---|
| Domain | `frame` (the model) + `pixel` (the application) |
| State cost | 2 cells per channel × 3 channels = 6 frame cells = **24 B**, and **zero pixel cells** — no ping-pong pair, nothing on the node face |
| Real-time | yes, bounded |
| Cost | 2 GPU reduction passes per channel = **6 reduction passes per frame**, each O(w·h), through the existing async PBO path (`field-domains` §7). The result is **1–2 frames late**; invisible against a multi-second learning constant, and it would not be invisible in a per-frame auto-exposure |
| Bound | `g = clamp(sr / sd, 0.25, 4)` with **literals**. A near-flat frame (a fade to black, `sd → 0`) otherwise produces an unbounded gain and a full-screen flash on the next cut. `sd` also carries its own 1e-6 floor, so the clamp is the second of two guards, not the only one |
| Does not fit | true histogram *matching*, which inverts the target CDF per pixel — that is a search over bins, i.e. §10.6 |
| Produces | point it at footage, hold `keep` on the shots you like, and every later shot drifts toward that grade — the same warm-shadow / cool-highlight balance, with no LUT and no curve control. Release `keep` and the look freezes |
| Prior art | Reinhard, Ashikhmin, Gooch & Shirley 2001, *Color Transfer between Images* (IEEE CG&A 21:5) |

```
param float rate   = 0.01 [0.0005, 0.1]
param float amount = 1.0 [0, 1]
param float keep   = 0 [0, 1]
state float mr = 0.5
state float sr = 0.25
mu  = reduce.mean(col.r)
m2  = reduce.rms(col.r)
sd  = sqrt(max(m2 * m2 - mu * mu, 0.000001))
mr += keep * rate * (mu - mr)
sr += keep * rate * (sd - sr)
g   = clamp(sr / sd, 0.25, 4)
col.r = clamp(mr + (col.r - mu) * lerp(1, g, amount), 0, 1)
```

One channel shown; RGB is the same three lines three times. `mu` and `m2` are
two **independent** reductions rather than a mean followed by a reduction of
`col.r - mu`, because the second form reduces a value derived from the same
reduction and reads as the delay-free cycle `field-domains` §3 refuses.

**The learned part is deliberately not in `pixel`.** A per-pixel learned model is
a ping-pong texture pair that §2's rung table already warns about, and that
`field-state` §6 option (b) would refuse to persist — so it would be forgotten
on every load, which is rung 3 wearing a rung-4 label. Six frame cells persist;
two million pixel cells do not.

### 9.6 Node co-occurrence — `graph` `[A]` — SPEC-ONLY

Count how often node type A ends up wired to node type B in this user's patches,
and suggest the winner when a node is placed.

| | |
|---|---|
| Domain | `graph` — runs once per graph edit, 0 invocations/s (§3.1) |
| State cost | 167 × 167 ordered type pairs = 27 889 cells × 4 B = **111.6 KB**. Bounded, and smaller than one 512² RGBA32F texture (4.19 MB, §3.2) — **this is the only rung-4 model in the document whose full, unapproximated form is affordable** |
| Real-time | yes, **and only because it is `graph`.** The two 167-iteration loops below are 334 iterations per cable drag. The same scan at frame rate is 20 040 iterations/s (still fine); at 48 kHz × 8 voices it is **64.1 M iterations/s**, and the 111.6 KB table becomes 892 KB of per-voice state that means nothing, because a habit is not per-voice |
| Blocked on | **`graph`'s reserved set is empty** (`field-language` §5). There is no name for "the edge that was just made" or "the type at each end"; `srcType` and `dstType` below are placeholders, not syntax. **This is a fifth finding and §4 does not carry it** — §4 counts the four the ladder needed to reach rung 3. Record it and ask the owner; do not invent the names in code |
| Needs | OPEN-A **option (c)** as well — a table, not a ring |
| Produces | place a Wavetable and the next suggestion is the Filter *you* always put after it, not the alphabetically first node. After a week, the suggestion for a Reverb is the Delay you always feed it from |
| Prior art | the co-occurrence / association-rule framing: Agrawal, Imieliński & Swami 1993, *Mining Association Rules between Sets of Items in Large Databases* (SIGMOD) |

```
param float lr   = 0.1 [0.02, 1]
param float show = 0.08 [0, 1]
state float link[27889] = 0.005988   # OPEN-A option (c): 167 x 167, rows = 1/167
row = srcType * 167                  # BLOCKED: graph has no reserved names
for (j = 0; j < 167; j++) {
  link[row + j] *= 1 - lr
}
link[row + dstType] += lr
top  = 0
best = -1
for (j = 0; j < 167; j++) {
  if (link[row + j] > top) {
    top  = link[row + j]
    best = j
  }
}
if (top < show) { best = -1 }
```

Row-normalised by construction, exactly as §9.2's table is: decay the row by
`(1 - lr)`, hand back `lr`, and the row stays a distribution forever with no
normalisation pass. `lr = 0.1` gives roughly a 10-edge memory, so recent habits
win; `lr = 0.02` gives a 50-edge one. `best = -1` is the honest "no suggestion
yet" — a cold table has every row flat at 1/167 = 0.006, below `show`, and the
UI must show nothing rather than show the first index.

### 9.7 The honest summary of rung 4

| Question | Answer today |
|---|---|
| Can rung 4 be written? | **three of the six** (9.1, 9.4, 9.5) with no new syntax at all. 9.2 needs OPEN-A (c) **and** OPEN-D, 9.3 needs OPEN-D, 9.6 needs OPEN-A (c) and three `graph` reserved names that do not exist |
| Can it be written in `sample`? | **no** — every model would be per voice, and a voice is not a person |
| Can it be written in `pixel` or `element`? | the *application* can (9.5). The *model* should not, and under `field-state` §6 option (b) it would not survive a reload if it were |
| Does it survive a reload? | frame cells, yes. Element and pixel cells, unknown — `field-state` §6 is OPEN |
| Does it survive a **seek**? | **no.** `field-state` §5 resets every cell on seek, loop and stop. Nothing in §9 works as advertised until that rule gains an exception, and the exception is a design decision, not a bug fix |
| Does it survive a **new patch**? | **no.** State is keyed into the patch file. Cross-patch learning needs a per-user store that Field does not have |

Two of those six rows are hard stops, and both are the same shape: **rung 4 needs
a lifetime longer than a `state` cell has.** That is the cost of the rung, in the
same way that a stability bound is the cost of rung 3.

---

## 10. What does NOT fit

Everything below is a real algorithm somebody will want. None of it is in
§5–§9, and pretending otherwise would make the catalogue useless. Where a
bounded replacement exists it is named; where none exists that is said plainly
instead of invented.

| Needs | Example algorithms | Why it fails Field's rules | Bounded approximation that DOES fit |
|---|---|---|---|
| **heap at run time** (§10.1) | dynamic voice allocation, a growing event list, any per-cook container | `field-realtime` §1 rule 1, and §3's hidden-allocation table | a fixed pool with a free index; the compile-time maximum with unused slots idle. **No partial version exists** |
| **unbounded history** (§10.2) | whole-take waveform buffers, a growing corpus (concatenative synthesis), k-NN over a dataset | rules 3, 5, 6 — the size is a runtime value | a fixed-order ring buffer (OPEN-A (b)); a **fixed-size reservoir** (Vitter 1985) holding a uniform sample at constant memory; a fixed codebook instead of a dataset |
| **big matrices, matrix inversion** (§10.3) | full n-state Kalman, RLS with an L×L covariance, FFT convolution with unbounded partitions | O(L²) state and O(L³) work from a runtime L; a matrix is a dynamic container; a pivot search is a data-dependent branch | **NLMS** (§8.1) instead of RLS; **scalar / α–β Kalman** instead of matrix Kalman; a **fixed partition count** for convolution |
| **recursion** (§10.4) | tree traversal, recursive subdivision, L-systems | rule 2 — no recursion anywhere, direct or mutual | a fixed-depth unrolled loop; N cooks of a node chain (§8.4's trick) |
| **all-pairs coupling** (§10.5) | n-body gravity, SPH, the global repulsion term §6.5 drops, boids over a full neighbour set | O(N²) = 25 M pair tests/frame at N = 5 000, against a ~2 ms element budget | a **fixed-K neighbour** kernel, K a literal, over an ordered chain (OPEN-B); or rasterise to a fixed `pixel` grid and read the gradient |
| **data-dependent iteration** (§10.6) | Newton solve to tolerance, flood fill, A*, Ellis-style DP beat tracking, march-until-hit | rule 3 — the trip count reads a runtime value | a **fixed iteration count** with the residual left on the table: Jacobi / Gauss–Seidel at a literal pass count (§8.4), a fixed Newton step count, a fixed march length with an honest miss |
| **a sort, or a dynamic container** (§10.7) | median filter, top-k, spatial hash, priority queue, voice stealing by age, histogram-CDF inversion | rules 4 and 5 — no dynamic arrays, no resizing, and a comparison sort's swap sequence is data-dependent | a **fixed-size sorting network** for small n; a **running quantile estimator** instead of an exact median; a fixed-bucket grid with a literal per-bucket capacity and a visible overflow drop |

### 10.1 Heap allocation at run time

Field's surface has no `new`, so this rule is broken by the **backend**, not by
the user. Three user-facing spellings force an allocation:

| The user writes | The backend must | The fix |
|---|---|---|
| a `map` over "however many points arrive" | size an array at run time | declare the node's maximum element count (`field-realtime` §2) |
| a `state` array sized from a delay-time param | resize on a knob turn | the compile-time maximum with a runtime **index** — §6.3's `buf[4096]` is the pattern |
| `downsample(x, k)` with `k` from a param | rebuild the hold schedule | `k` a literal (`field-domains` §6) |

**Worked cost of getting it wrong.** The audio callback budget is ~5.3 ms per
256-frame block (§3.1). A single `malloc` that takes a slow path in a fragmented
allocator is tens of microseconds and is not bounded at all — one is survivable,
one per sample is 48 000 unbounded pauses a second. This is the one row in §10
with **no partial version and no approximation**: the replacement is always the
same, allocate the maximum once and index into it.

### 10.2 Unbounded history

| Wanted | The full form | The fixed form | What is lost |
|---|---|---|---|
| freeze or reverse a whole take | a buffer as long as the session | a 10 s ring at 48 kHz = 480 000 × 4 B = **1.92 MB** per voice; §6.3's 4096-sample ring is 16 KB | you cannot scrub past the ring, and the length is in the patch rather than on a knob |
| concatenative synthesis over a corpus | every grain the user ever recorded | a **reservoir** of K grains, K literal: 256 grains × 2048 samples × 4 B = **2.1 MB** | the corpus becomes a uniform *sample* of itself. A rare grain may never enter it. **This is a real loss with no version that recovers it** |
| k-NN over a dataset | the dataset, plus a search structure | a fixed codebook of K centroids updated by competitive learning: 32 centroids × 8 dims × 4 B = **1 KB** | it is a quantiser, not a nearest neighbour. Ties and outliers land on the wrong centroid and stay there |

Prior art for the reservoir: Vitter 1985, *Random Sampling with a Reservoir*
(ACM TOMS 11:1) — one pass, constant memory, uniform without knowing the stream
length in advance, which is exactly the shape rule 5 demands.

### 10.3 Big matrices and matrix inversion

This is the section §8.1.1 forward-references, and the numbers are why NLMS is
the exemplar.

| Algorithm | What it needs | At L = 32 | Verdict |
|---|---|---|---|
| **RLS** (recursive least squares) | an L×L inverse-correlation matrix updated every sample | 1 024 cells × 4 B = 4.1 KB mono, **32.8 KB at 8 voices**, and O(L²) ≈ 2 048 MAC/sample = **98 M MAC/s mono, 786 M at 8 voices** | the memory is survivable; the arithmetic is not, and the update is numerically fragile — float32 drift breaks the matrix's symmetry and it needs a re-symmetrisation pass to stay stable |
| **NLMS** (§8.1) | 2L + 1 cells, 2L + 6 flops | **260 B**, 70 flops/sample | **the replacement.** RLS converges in ≈ 2L = 64 samples *regardless of the input's eigenvalue spread*; NLMS at mu = 0.3 takes τ ≈ L/(2·mu) ≈ 53 samples on a white input and degrades in proportion to the spread on a coloured one. **That degradation is the entire price**, and it buys 1/29th the arithmetic and 1/16th the memory |
| **full Kalman**, n states | an n×n covariance and a **matrix inverse** in the gain step | at n = 6 the 36 cells are nothing; the inverse is the problem — Gauss–Jordan's pivot search is a data-dependent branch and a sort in disguise (§10.7) | out |
| **α–β / scalar Kalman** | 2 cells and a fixed gain pair | **8 B** | **the replacement.** §7.3's dead reckoning *is* the α–β filter with β folded into `lead` |
| **partitioned FFT convolution** | P partitions, P = tail length / block size, from a runtime IR | a 4 s IR at 48 kHz / 256 = **750 partitions** | out as written |
| **fixed-P convolution** | P a literal | P = 16 × 256 = 4 096 samples = **85 ms** of IR | fits — and 85 ms is a room, not a hall. **A hall has no bounded Field version.** It is a C++ node |

**And the FFT itself.** A fixed-N FFT is bounded, straight-line and allocation
free — it passes every rule in `field-realtime` §1. It still is not a Field
kernel, because it is not "a body of code run once per element of a domain"
(§1 invariant 4): its butterfly reads a *different* element each stage, with a
stride that changes per stage. Say it once and stop proposing it — **an FFT is a
C++ node that Field consumes through a `param` or a texture, not a kernel.**

### 10.4 Recursion, and the shapes that are bounded but still not kernels

Rule 2 bans recursion outright, direct and mutual. The replacements are dull and
they work: unroll to a fixed depth, or run N cooks of a node chain the way §8.4
runs 20 Jacobi passes as 20 cooks rather than as a loop.

The more interesting case is the family that passes §1 rules 1–6 and still does
not fit: **FFT, prefix scan, bitonic sort, a wavelet lift.** All bounded, all
straight-line, none expressible as one body per element, because each stage
reads a different element than the last did. §1 invariant 4 refuses them, and
that refusal is correct — relaxing it turns Field from a kernel language into a
general parallel-programming language, which is a different project.

### 10.5 All-pairs coupling

§6.5's arithmetic, in full: N = 5 000 gives 5 000² = **25 M pair tests/frame =
1.5 G/s**, against `field-realtime` §5's element budget of 300 k invocations/s.
Off by a factor of 5 000.

| Option | Cost at N = 5 000 | Fits? | What changes |
|---|---|---|---|
| all pairs | 25 M tests/frame | no | — |
| **fixed-K neighbours**, K = 8, over an ordered chain (OPEN-B) | 40 000 tests/frame = 2.4 M/s | **yes** — it is §8.3's relaxation loop with a wider stencil | long-range repulsion is gone, so a curve **passes through itself** where two distant parts meet. There is no bounded fix for that: self-intersection *is* the global term |
| **rasterise to a `pixel` field**, read the gradient | 512² grid = 16.8 MB, §8.4's budget | **yes** | O(N²) becomes O(N + w·h), and the result is a different algorithm — a density field, not pairwise forces. Clusters below one cell stop interacting |

### 10.6 Data-dependent iteration counts

| Algorithm | The unbounded part | The fixed version | What is lost |
|---|---|---|---|
| Newton solve to tolerance | `while (abs(f) > eps)` | 4 Newton steps, literal | a bad initial guess leaves a visible residual instead of an xrun. **Prefer the visible residual** — it is a look, and a dropout is not |
| Jacobi / Gauss–Seidel pressure solve | "iterate until divergence < eps" | §8.4: one pass per cook, 20 cooks per frame, the count a **node-graph property** | incompressibility is approximate; the visible tell is fluid that slowly gains or loses volume |
| flood fill, connected components | a work queue whose length is the region | N passes of a fixed dilation kernel in `pixel`, N literal | a region wider than N pixels is not filled, and you cannot know N in advance |
| ray march until hit | a `while` on the distance estimate | a fixed march length with a miss | a grazing ray misses; the visible tell is a hard silhouette that shimmers |
| **A\* / shortest path** | an open set, a priority queue, and a loop until the goal pops | **none.** A fixed-iteration flood is not a path, and no literal pass count recovers the optimality guarantee A* exists to provide | route it to a C++ node. There is no honest approximation here |
| **Ellis DP beat tracking** | a Viterbi backtrace over the whole onset envelope, offline | §8.5's PLL: causal, 12 B, one correction per onset | the PLL cannot revise a decision it has already made. Ellis can — which is why it is better *offline* and unusable *live*, and why §8.5 is the entry that ships |

Prior art: Ellis 2007, *Beat Tracking by Dynamic Programming* (J. New Music
Research 36:1).

### 10.7 Sorts and dynamic-size containers

| Wanted | Why it fails | The fixed version | What is lost |
|---|---|---|---|
| median of a 9-tap window | a comparison sort's swap sequence depends on the data | a **sorting network**: 9 inputs in 25 compare-exchanges, straight-line and branch-free — pairs of `min` / `max` and nothing else. **This fits, and it is the right answer** | nothing up to about n = 16; past that the unrolled text stops being readable before it stops being fast |
| median of a 1 s window | 48 000 elements to sort | a running quantile estimator (P², Frugal-1U): 3–5 cells | it converges *to* the quantile; it is not the quantile, and it lags a step change by its own time constant |
| top-k over N elements | a partial sort | k argmax passes, k literal: k × N compares | O(kN) instead of O(N log N) — fine for k ≤ 4, wasteful past it |
| a spatial hash (§6.5, §10.5) | buckets of unbounded occupancy | a fixed grid with a literal capacity: 32 × 32 cells × 8 slots × 4 B = **32 KB** | a dense cluster silently loses interactions. **Surface the drop count on the node face**; a silent drop is the bug users cannot diagnose |
| voice stealing by age | a priority queue | a fixed argmin over 8 age cells, unrolled | nothing — 8 is small enough that the "approximation" is exact |
| histogram-CDF inversion (§9.5) | a search over bins, per pixel | mean / σ transfer (Reinhard) | shape: only the first two moments match, so a bimodal target reads as its midpoint |

Prior art: Batcher 1968, *Sorting Networks and their Applications* (AFIPS);
Knuth, *TAOCP* vol. 3 §5.3.4 for the 25-comparator 9-input network.

### 10.8 The test, in one line

> If the loop bound, the byte count and the branch count can all be written down
> **without running the program**, it belongs in §5–§9. If any one of the three
> needs the data, it belongs here.

Every row above fails on exactly one of those three, and knowing which one is
what points at the replacement.

---

## 11. Pick your first three

"Steps 1–9 done" means everything in §5–§9 except the `graph` domain (step 10)
is expressible, and §4's four questions have been answered. These three are
chosen for **contrast** — one per backend, one per sense, one per kind of
evidence — not for being the three best algorithms in the catalogue.

### 11.1 NLMS adaptive line enhancer (§8.1) — rung 3, `sample`

| | |
|---|---|
| Rung / domain | **3** / `sample` |
| Build steps | 1 (Expression → IR), 5 (`param`), 6 (`state`), 9 (sample backend). **No OPEN required** — §8.1.5's order-4 form is 20 lines of scalar `state` |
| Why it is the demo | it is the smallest program that could not have been a knob. No amount of modulation routing reproduces a coefficient that depends on the signal, and the whole argument for a language is on that one line |
| What the user hears | with `out = e`, point it at a howling PA and the feedback tone dies in about **15 ms**. Flip to `out = y` and the same patch pulls the sustained notes out of a noisy recording and leaves them ringing, with the lock-on swoop as its signature. Turn `mu` from 0.3 to 1.6 and the clean lock becomes a ring-modulated warble — one knob between two instruments |
| Evidence it produces | zero xruns under `AUDIOPARAMSWEEPTEST`, and **260 B** on the node face next to a filter that could not exist as a preset |

### 11.2 Gray–Scott reaction–diffusion (§6.7) — rung 1, `pixel`

| | |
|---|---|
| Rung / domain | **1** / `pixel` |
| Build steps | 1, 3 (vectors and rank), 5, 6, 7 (GLSL backend) — and **OPEN-C answered**, including the edge rule, because wrap and clamp are two visibly different patterns |
| Why it is the demo | it is a node Infinite **already ships as C++**, rewritten as fifteen lines the user can edit while it runs. Nothing argues "the node is now text" faster than editing a shipping node's algorithm live and watching it change |
| What the user sees | coral, fingerprints and dividing cells. `feed`/`kill` from (0.055, 0.062) to (0.030, 0.062) crosses from worms to mitosis with no reload. Switch the state cell's edge rule from clamp to wrap and the pattern stops piling up at the border and starts tiling seamlessly |
| Evidence it produces | **8.39 MB at 512², shown on the node face** — the first time `field-state` §3's "the cost is never silent" rule is a thing a user can see rather than a rule in a skill |

### 11.3 PLL tempo tracker (§8.5) — rung 3, `frame`

| | |
|---|---|
| Rung / domain | **3** / `frame`, reading `sample` |
| Build steps | 1, 5, 6, 8 (transfer operators — `reduce.rms` is the only crossing it needs) |
| Why it is the demo | it is the cheapest possible proof that the five domains are one language and not five: **12 bytes** of `frame` state reading a `sample` signal and driving a `pixel` param, with no glue node anywhere in the chain |
| What the user sees and hears | drop a live drum loop in and a Euclidean gate (§5.5) locks to it and stays locked through a tempo drift. Feed `period` into §7.2's freeze and the freeze becomes beat-synchronous — a rung-3 system correcting a rung-2 one, in one patch. Push `kp` past 0.3 and the visuals visibly hunt on every syncopation: the failure is as legible as the success |
| Evidence it produces | the sample → frame crossing works through the existing meter path with **no new cross-thread channel** (`field-realtime` §1 rule 9), and §3.1's budget table stops being theoretical |

### 11.4 Why these three, side by side

| | 11.1 NLMS | 11.2 Gray–Scott | 11.3 PLL |
|---|---|---|---|
| Rung | 3 | 1 | 3 |
| Domain | `sample` | `pixel` | `frame` + a `sample` reduce |
| Backend exercised | register machine | GLSL text → `GLUtil::CompileProgram` | bytecode VM |
| Sense | ear | eye | both, and the clock between them |
| State | 260 B | 8.39 MB | 12 B |
| Blocked on | nothing | OPEN-C | nothing |
| What it proves | a coefficient can depend on the signal | a shipping C++ node is now editable text | the five domains are one language |

**Runner-up, and why it is not in the three.** §6.4 Verlet in `element` — the
one domain Infinite has never had, 60 KB, no OPEN tags, and a point cloud that
keeps moving when you stop dragging it. It is the largest new *capability* on the
list and the weakest *demo*, because a user reads it as "a new node" rather than
as "the node is now text". Build it fourth, on the day step 4 lands.

**What all three share, deliberately.** Each is under twenty lines. Each fails
*visibly* rather than silently when its bound is crossed — a screech, a
saturated pattern, a hunting tempo. And each has a knob whose extreme is a
second instrument rather than a bug, which is the property that makes a bound
worth exposing instead of hiding.
