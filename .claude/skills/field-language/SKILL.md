---
name: field-language
description: The syntax, semantics and reserved-word contract of Field, the embedded language being built for Infinite — the one primitive (a kernel per element of a domain), the five domains (graph/frame/element/pixel/sample), inferred rates, bare-name attributes with no sigils, attrib/param/state declarations, the type set and rank polymorphism, and the wrong/right table of the mistakes every fresh session makes. Use BEFORE writing or reviewing any Field source text, any Field example in a doc, any node body that hosts a Field editor, or any prompt for a session that will touch the language; use when asked "what does a Field program look like", "is this valid Field", "what rate does this run at", "why is my expression compiling to the wrong domain", or when anyone writes `@P` or declares a rate.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

**Field is implemented.** The compiler lives in `src/core/field/` (`FieldParse.cpp`,
`FieldIR.cpp`, `Transfer.cpp`, `ReduceOps.cpp`, and friends) and is wired into
real nodes (`src/nodes/Field*Node.*`) with live editors in the app ("Field
element editor", "Field pixel editor", etc.). Everything below that reads as a
future plan ("build step N", "does not exist in Infinite today") should be
treated with suspicion — verify against the actual source before trusting it,
and fix this doc when you find it stale. The design is settled with the owner —
do not re-litigate it, do not invent alternative syntax, and do not reintroduce
the `@` sigil.

**The one rule that trips people up most:** domains do not mix inside a single
kernel body. `in`/`out`/`sr`/`n`/`freq`/`gate` exist ONLY in a `sample`-domain
kernel; `P`/`N`/`uv`/`Cd`/`i`/`count` ONLY in `element`; `uv`/`xy`/`col`/`res`/
`aspect`/`alpha` ONLY in `pixel`. A kernel cannot reference another domain's
reserved names to "pull audio into geometry" or similar — v1 has no
cross-domain read of an `audio` or `image` structural pin from inside kernel
text (confirmed via `FieldIR.cpp`'s pin-validation and `Transfer.cpp`'s
`ValidateReduce`, which requires `reduce.rms(in, lo, hi)`'s argument to already
be `Domain::Sample`). If you want geometry or pixels to react to audio, expose
a `param` in the element/pixel kernel and drive it from an audio-analysis node
through the modulation matrix — do not write `in`/`reduce.rms` inside an
element or pixel kernel body.

Read order: this skill, then
[`field-compiler`](../field-compiler/SKILL.md) for how the text becomes code.
The subsets deepen independently:
[`field-state`](../field-state/SKILL.md),
[`field-domains`](../field-domains/SKILL.md),
[`field-realtime`](../field-realtime/SKILL.md),
[`field-integration`](../field-integration/SKILL.md),
[`field-testing`](../field-testing/SKILL.md).

---

## 0. Two invariants that override anything you infer

1. **Clean room.** Infinite is MIT. **Never** open, read, grep, or reference
   GPL sources: Kronos (`bitbucket.org/vnorilo/k3`), Cmajor, SuperCollider,
   or BespokeSynth (also at `/Users/namansoni/BespokeSynth` on this machine —
   do not open it). The *Kronos paper* (Norilo, "Kronos: A Declarative
   Metaprogramming Language for Digital Signal Processing", Computer Music
   Journal 39:4, 2015) is a published paper and may be cited freely; its code
   may not be read. Safe to read for reference: Faust (LGPL), ChucK
   (dual MIT/GPL), Houdini VEX documentation, TidalCycles docs/papers.

2. **Field is one primitive, not a pile of features.** Every construct below
   has to be explainable as "a body of code run once per element of a domain".
   If a proposed feature cannot be explained that way, it does not go in.

---

## 1. The one primitive

> **A kernel is a body of code run once per element of a domain.**

There is no second primitive. A frame kernel runs once per frame because a
frame domain has one element per frame. A pixel kernel runs 2 million times
per frame because the pixel domain has that many elements. Nothing else
changes.

```
                      ┌──────────────────────────┐
   source text  ──▶   │  kernel body             │  ──▶  run once per
                      │  (the only construct)    │       element of its domain
                      └──────────────────────────┘
```

## 2. The five domains

| Domain | Runs | Count/sec at 60 fps, 48 kHz | Backend | Status |
|---|---|---|---|---|
| `graph` | once per graph edit | 0 (edit time) | interpreter | build step 10, last on purpose |
| `frame` | once per frame | 60 | bytecode VM | build step 1 |
| `element` | once per point or vertex | 60 × N (N ≈ 5000) | bytecode VM | build step 4 — **does not exist in Infinite today** |
| `pixel` | once per pixel | 60 × w × h (124 M at 1080p) | GLSL text → `GLUtil::CompileProgram` | build step 7 |
| `sample` | once per audio sample | 48 000 | register machine | build step 9 |

`graph` is last deliberately. Norilo's two-year teaching evaluation (p.45)
found students grasped filters immediately because "the patches correspond very
closely to textbook diagrams" but could not apply algorithmic routing — the
metaprogramming layer — unaided. Graph metaprogramming impresses experts and
loses everyone else. It ships when the other four are solid.

## 3. Rate is inferred, never declared

There is **no** `@rate` keyword, **no** `krate` parameter, **no** domain
annotation on a binding. The compiler decides which domain an expression lives
in from its data dependencies:

| The expression mentions | It runs |
|---|---|
| no per-element name | once, at the coarsest rate its inputs allow |
| `t`, `dt`, `frame` | per frame |
| `P`, `N`, `uv`, `Cd`, `i`, `count` | per element |
| `uv`, `xy`, `col`, `res` (pixel kernel) | per pixel |
| `in`, `out`, `sr`, `n` | per sample |

Worked example — the same three lines, three placements:

```
amount = 0.5 + 0.5 * sin(t)     # mentions t          -> frame rate, 60/s
P.y   += amount                  # mentions P          -> element rate, 60*N/s
```

`amount` is computed **once per frame** and broadcast to all N elements. The
programmer wrote no annotation to make that happen and cannot write one to
prevent it.

Prior art — cite it, never claim novelty: Faust's computation levels; Kronos's
"automated factorization of signal rates" (Norilo p.36). Measured payoff,
Kronos Table 3 p.45: the same program with only the LFO downsample factor
changed ran 257 µs/1024 samples at krate=1 and 114 µs at krate=128 — a 2.25×
speedup from rate inference alone, saturating near krate=32.

## 4. Bare names. No sigils. Ever.

An earlier draft used a VEX-style `@` sigil. The owner **removed it**. Plain
ASCII, no special characters, in every example in every skill and every doc.

| Wrong | Right | Why |
|---|---|---|
| `@P.y += bass * 2` | `P.y += bass * 2` | the sigil was removed by owner decision |
| `@Cd = vec3(1,0,0)` | `Cd = vec3(1,0,0)` | same |
| `v@P` / `f@heat` | `P` / `heat` | Field has no type-prefix sigil either |

## 5. Reserved words, per domain

Attribute names are **reserved words of their domain**. A local variable may
not shadow one. Attempting to is a compile error, not a warning.

| Domain | Reserved names |
|---|---|
| `frame` | `t` `dt` `frame` |
| `element` | `P` `N` `uv` `Cd` `i` `count` |
| `pixel` | `uv` `xy` `col` `res` `aspect` `alpha` |
| `sample` | `in` `out` `sr` `n` `freq` `gate` |
| `graph` | none |

`uv` appears in two domains. It is the element's texture coordinate in
`element` and the pixel's normalized coordinate in `pixel`. A kernel is only
ever in one domain, so the two never collide inside one body — but the error
message must say which domain it resolved in, or the user cannot tell why a
`uv` reference typed the way it did.

`freq` and `gate` are the per-voice **generator-mode** additions
(`docs/plans/field/design-prompt-sample-generator-mode.md`): `freq` is the
current voice's note frequency in Hz (MIDI note → Hz, `440 * 2^((note-69)/12)`
— the same formula and naming convention as `WavetableSynthCore::NoteToHz`
and every other synth node's local `MidiNoteToHz` helper); `gate` is `1.0`
from note-on until note-off, `0.0` otherwise. Both are read-only, both are
always populated — **never gated on whether `in` is connected**, unlike
`FieldElementNode`'s `generateCount`-when-unconnected pattern — so a sample
kernel can be a self-contained generator (an oscillator) with no upstream
audio patched into `in` at all. `gate` reflects note-on/note-off directly; it
is not the same signal as the per-voice amplitude envelope applied outside
the kernel, which keeps decaying through its own release stage after `gate`
has already dropped to `0`.

**Precedent for the shadowing ban already in the codebase:**
`ExprGlobals::IsValidName` (`src/core/ExprGlobals.h:44`) already refuses to let
a global be named `t`, `pi`, `lo` or `hi`, "enforced by the editor rather than
at evaluation time so the failure is visible where it is typed". Field keeps
that policy and extends it per domain.

## 6. `attrib` — user attributes need a declaration

This is deliberately **stricter than VEX**, where `@heat` and `@heta` both
silently succeed and one of them is a typo you find three hours later.

```
attrib float heat = 0
heat += bass * 0.1
```

| Wrong | Right |
|---|---|
| `heat += bass * 0.1` with no declaration | `attrib float heat = 0` first |
| `attrib float P` (shadowing a reserved name) | pick another name |
| `attrib heat = 0` (no type) | `attrib float heat = 0` |

An `attrib` is storage that lives on **every element of the domain**, for the
lifetime of the element. Its per-domain cost is the same table as `state` —
see [`field-state`](../field-state/SKILL.md) §3.

## 7. `param` — a declaration that grows a knob

A `param` line auto-creates a control in the node body, registered through the
existing `ParamRef` machinery in `src/core/Modulation.h`.

```
param float amount = 0.5 [0, 2]
```

| Piece | Meaning | Maps to `ParamRef` field |
|---|---|---|
| `float` | type | (float params only in v1) |
| `amount` | identifier and knob caption | `name` |
| `0.5` | initial value | **no field exists — see open question** |
| `[0, 2]` | range | `minValue`, `maxValue` |

Prior art, not novel: Houdini `chf()`, Cabbage markup.

> **OPEN — where does a `param`'s default value live?**
> `ParamRef` (`src/core/Modulation.h:29`) carries `nodeIndex, paramIndex,
> value*, minValue, maxValue, step, name, isEnum, isBool, enumOptions,
> posToValue, valueToPos`. There is **no `defaultValue`**. Options:
> **(a)** store defaults in the Field node's own side table keyed by name —
> zero change to shared code, but "reset to default" is Field-only;
> **(b)** add `defaultValue` to `ParamRef` — every node gets reset-to-default
> for free, but it touches a struct every node registers into every frame;
> **(c)** treat the literal as the initial value only, with no reset concept.
> Do not pick silently. Ask the owner.

See [`field-integration`](../field-integration/SKILL.md) §3 for registration
mechanics and the `ParamMailbox::kMaxParams = 128` ceiling that bounds how many
params a sample-domain kernel can have.

## 8. `state` — one line here, a whole skill there

```
state float z = 0
z += (in - z) * cutoff
out = z
```

`state` lowers to a **unit delay node**. A cycle in the dataflow graph is legal
**iff** it passes through at least one delay. Everything else about it — reset,
serialization, hot reload, the per-domain memory cost, why a `state float` in a
1080p pixel kernel is an 8 MB texture pair — is in
[`field-state`](../field-state/SKILL.md). Read it before writing any `state`.

## 9. Types and rank polymorphism

| Type | Notes |
|---|---|
| `float` | the default numeric type |
| `int` | loop counters, indices, mode selectors |
| `bool` | result of a comparison or logical operator |
| `vec2` `vec3` `vec4` | components `.x .y .z .w`, aliases `.r .g .b .a`, swizzles (`P.xz`, `Cd.bgr`) |

**No user structs in v1. No arrays, no strings, no pointers.**

Rank polymorphism in the APL sense: a scalar used where a vector is expected
broadcasts across every lane.

```
P *= 2.0             # float broadcasts to vec3 — legal
Cd = 0.5             # float broadcasts to vec3 — legal
P += vec2(1, 0)      # vec2 into vec3 — ERROR, no rank-narrowing rule
```

Broadcast goes **scalar → vector only**. There is no implicit vec2→vec3 fill,
no truncation, and no int→float loss in the other direction (int promotes to
float; float does not demote to int without an explicit call).

## 10. Operators

| Class | Operators |
|---|---|
| arithmetic | `+ - * / % ^` |
| compound assign | `+= -= *= /=` |
| assign / compare | `= == != < <= > >=` |
| logical | `&& \|\| !` |
| access | `.` component and swizzle |
| grouping | `()` call and precedence, `{}` block |
| comment | `#` to end of line |

> **Settled in Step 3 — `^` (power).** `src/core/Expression.cpp` implements `^` as
> a right-associative power operator (`2^3^2 == 512`, `-2^2 == 4`), component-wise
> on vectors (`vec3(1,2,3)^2 == (1,4,9)`). Following option **(b)**: `^` is kept in
> the surface syntax and lowered to `pow()` in the IR, generating clean GLSL.

## 11. Branching is allowed — with a cost model attached

Kronos forbids data-dependent control flow entirely ("runtime values are, in
effect, shut out from influencing program flow", p.36) and buys total
analyzability with it. **Field allows branching.**

```
if (bass > 0.5) { Cd = vec3(1, 0, 0) }
```

| Domain | Lowering | Cost |
|---|---|---|
| `frame` | real branch | free |
| `element` | real branch | breaks vectorization of the batch |
| `sample` | real branch | mispredict risk — see `field-realtime` §4 |
| `pixel` | **predication**: evaluate both sides, select | always pays the sum of both sides |

The owner's condition on allowing branching was that it must not compromise
integration or optimisation quality across audio and visual. So a skill or doc
that shows branching syntax **must** show the cost model next to it. The full
model, including GPU divergence, is
[`field-realtime`](../field-realtime/SKILL.md) §4.

Note a compatibility wrinkle: today's `if(cond, a, b)` in `src/core/Expression.cpp`
is a **function**, evaluates both branches, and has no short-circuit
(`src/core/Expression.h:41`). Field's statement-form `if` in `frame` is a real
branch. Both spellings must survive step 1 — the function form is in the
regression corpus.

## 12. Pure randomness

`rand`, `noise` and `sh` become pure functions of `(t, seed)`.

**What the code actually does today** (`src/core/Expression.cpp:166–222`):
there is no hidden counter — `rand`/`noise` are already deterministic in `t`,
computed as a sum of three incommensurate sines
(`(sin(τ) + sin(1.618τ) + sin(2.718τ)) / 6 + 0.5`), and `sh` was
`fabs(fmod(sin(floor(t·speed)·123.456) · 43758.5453123, 1.0))`. The defect is not
statelessness — it is that the three-sine sum is strongly autocorrelated and
neither takes a `seed`, so two `rand()` calls in one patch return the identical
value.

The replacement, reimplemented from the published TidalCycles algorithm
description (**not** copied from its GPL source):

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

**The `if (m < 0)` line is the trap.** Haskell's `mod` returns non-negative for
a positive divisor; C++ `%` does not. Omit it and half the outputs are
negative.

> **OPEN — how does `seed` enter?** The snippet above is `TimeToRand(t)` with
> no seed parameter, but the requirement is a pure function of `(t, seed)`.
> Options: **(a)** `Xorwise` the seed into the time word before hashing;
> **(b)** hash `(t, seed)` as two rounds; **(c)** derive an implicit seed from
> the call site's source position so two `rand()` calls in one program differ
> without the user typing anything. **(c)** is the nicest ergonomically and the
> worst for reproducibility across an edit. Ask the owner.

**This is a deliberate visible break.** Saved patches using `rand`/`noise`/`sh`
will look different after step 2. It goes in the release notes. See
[`field-testing`](../field-testing/SKILL.md) §4 for how the golden-value corpus
is re-baselined across that break without losing its value as a regression net.

## 13. Worked examples

**Frame — a value that follows the transport.**

```
# runs 60 times a second; mentions only t, so it is frame-rate
param float depth = 1.0 [0, 4]
size = 1.0 + depth * sin(t * 2)
```

**Element — a pure geometry deformer.**

```
param float speed = 2.0 [0, 10]
param float height = 0.3 [0, 2]
dist = length(P.xz)
P.y += sin(dist * 4.0 - t * speed) * height   # mentions t and P -> element rate
Cd = vec3(0.5 + 0.5 * sin(P.y * 5.0), 0.4, 0.8)
```

**Element — audio-reactive, the actually-legal way.** `reduce.rms(in, ...)`
only compiles inside a `sample`-domain kernel — `in` is a reserved word of
`sample`, not `element`. To make geometry react to audio, compute the level in
a sample/frame-analysis node, expose it as a `param`, and drive that `param`
from the modulation matrix; the element kernel only ever sees its own `param`:

```
param float bass = 0.0 [0, 1]     # driven externally via the mod matrix
P.y += bass * 2
Cd = vec3(bass, 0.2, 1.0 - bass)
```

There is no single kernel that legally mixes `in` with `P`/`Cd` — see the rule
at the top of this file.

**Pixel — a state cell you should think twice about.**

```
attrib float heat = 0
state float prev = 0             # 8 MB ping-pong pair at 1080p — see field-state
prev = mix(prev, col.r, 0.1)
col = vec3(prev, col.g, col.b)
```

**Sample — a one-pole through familiar syntax.**

```
param float cutoff = 0.2 [0, 1]
state float z = 0
z += (in - z) * cutoff
out = z
```

The cycle `z -> z` is legal because it passes through the delay `state`
introduces. This is the Kronos model (p.36: "cycles in the signal flow, as long
as each cycle includes at least one sample of delay") reached through familiar
syntax rather than a `z-1` operator.

**Sample — a generator, no `in` required.** `freq`/`gate` let a sample kernel
synthesize its own audio directly from the incoming notes, with no upstream
audio source patched into `in` at all:

```
state float phase = 0
phase = phase + freq / sr
phase = phase - floor(phase)
out = sin(phase * 6.283185) * gate
```

`phase` accumulates by `freq / sr` each sample — a phase increment of exactly
one cycle per `sr / freq` samples — wraps with `phase - floor(phase)`, and
`sin(phase * 2π)` turns it into a sine oscillator. Multiplying by `gate`
silences the oscillator's raw output the instant the note releases, on top of
(not instead of) the per-voice amplitude envelope `AudioFieldSampleNode`
already applies outside the kernel. This is the reserved-word doc's example
kernel, and `freq`/`gate` interact with `in` exactly like every other
reserved name: a kernel is free to read both in the same body (e.g. a synth
voice that also filters an `in` sidechain), because reading them costs
nothing and neither is gated on the other being connected.

## 14. The wrong / right table — the mistakes a fresh session will make

| # | Wrong | Right | Why it matters |
|---|---|---|---|
| 1 | `@P.y += bass` | `P.y += bass` | the sigil was removed by owner decision, permanently |
| 2 | `@rate frame` / `krate 32` | *(nothing)* | rate is inferred; there is no syntax to declare it |
| 3 | `downsample(lfo, 32)` written "to make it faster" when nothing needs it | let inference do it | `downsample` is a domain transfer, not an optimisation hint |
| 4 | `float t = 0.5` in a frame kernel | `float phase = 0.5` | `t` is a reserved word of `frame`; shadowing is a compile error |
| 5 | `heat += 1` with no declaration | `attrib float heat = 0` first | VEX's silent-typo failure is exactly what the declaration exists to prevent |
| 6 | `float buf[512]` | *(not in v1)* | no arrays; every value's size is known at compile time |
| 7 | `while (x > 0) { … }` | `for (i = 0; i < 8; i++)` | bounded loops only, so they can be unrolled/vectorised |
| 8 | a helper kernel that calls itself | flatten it | no recursion, anywhere |
| 9 | `P += vec2(1, 0)` | `P += vec3(1, 0, 0)` | broadcast is scalar→vector only; there is no rank-narrowing rule |
| 10 | `state float z = 0` in a 1080p pixel kernel, unremarked | declare it and **surface the 8 MB cost in the UI** | `field-state` §3 — this cost must never be silent |
| 11 | `if (…)` in a pixel kernel described as "skipping work" | describe it as predication | the GPU evaluates both sides; it never skips |
| 12 | a `param` count above 128 in a sample kernel | keep it under | `ParamMailbox::kMaxParams = 128` (`src/audio/ParamMailbox.h:24`) |

## 15. Exit criterion for a language-surface change

A change to Field's surface syntax is done when all of these hold, each stated
as a checkable fact:

1. Every example in every `field-*` skill and every doc uses bare names — a
   repo-wide `grep -rn '@[A-Za-z]' .claude/skills/field-*` returns nothing that
   is a Field attribute reference.
2. No example declares a rate.
3. Every reserved word in §5 is rejected as a local variable name, with an
   error naming the domain it collided in.
4. Every `attrib` used without a declaration errors at the use site with its
   line and column.
5. The regression corpus in [`field-testing`](../field-testing/SKILL.md) §2
   still evaluates to its golden values, or the diff is a deliberate,
   documented break (only §12's randomness change qualifies today).
6. Any question this skill marks **OPEN** that the change touches has been put
   to the owner and answered — not silently resolved in code.
