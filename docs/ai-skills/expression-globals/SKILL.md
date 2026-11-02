---
name: infinite-expression-globals
description: Write '=' parameter expressions and app-wide Expression Globals for Infinite. Use whenever the user wants a parameter driven by a formula (e.g. "=sin(t)*0.5+0.5"), a reusable named value shared across many parameters, or help fixing one that errors or behaves unexpectedly.
---

# Infinite parameter expressions and Expression Globals

Any numeric parameter field in Infinite accepts a formula instead of a fixed
number: type `=` followed by an expression, e.g. `=sin(t)*0.5+0.5`. This is a
different, simpler language than Field (Infinite's per-node kernel language
for Field Synth/Effect/Modifier/Pixel) — there are no domains, no `param`/
`state`/`attrib` declarations, and no kernels. It is one flat expression that
re-evaluates every frame and produces a single float.

**Expression Globals** (Settings > Expression Globals) are named expressions
defined once, app-wide, so many parameters can share one formula by name
instead of repeating it. Give the user either a bare expression (for a single
parameter's `=` field) or a `name = expression` line (for a Global), matching
what they asked for.

## What every expression can read

| Name | Meaning |
|---|---|
| `t` | seconds since the transport started playing |
| `pi` | the constant π |
| `lo`, `hi` | (inside a parameter's own `=` field only) that parameter's own configured minimum and maximum — lets you write `lerp(lo, hi, ...)` instead of hardcoding raw units |
| any sibling parameter's name | that parameter's current value, e.g. `width * 0.5` reads a sibling named `width` |
| any Expression Global's name | its current value, evaluated once per frame in declaration order |

**Shadowing rule:** a sibling parameter of the same name as a Global wins —
the local meaning always beats the global one. `t`, `pi`, `lo`, `hi` cannot be
used as a Global's name; that name is rejected at creation time.

**Global ordering matters.** Globals see `t` plus every Global defined *above*
them in the list, never one below — write dependencies top-to-bottom. A
Global's own expression cannot reference itself.

## Operators

`+ - * / % ^` (right-associative power), `< <= > >= == !=`, `&& || !`,
unary `-`, `.` for vector component/swizzle access (`.xy`, `.rgb`, etc.).
Comparisons and logic yield `1` or `0` and compose directly with arithmetic —
`lerp(lo, hi, x > 0.5)` is a valid gate; there is no separate boolean type.

## Built-in functions

| Function | Notes |
|---|---|
| `sin cos tan` | radians |
| `abs sign sqrt exp log pow` | standard |
| `floor ceil round mod` | `mod(a, b)` is floating-point modulo |
| `min max clamp lerp mix` | `lerp(a, b, t)` / `mix(a, b, t)` — linear interpolation |
| `step(edge, x)` | 0 below `edge`, 1 at/above it |
| `smoothstep(e0, e1, x)` | smooth Hermite ramp from `e0` to `e1` |
| `bend pinch mirror skew formant` `(phase, amount)` | phase shaping: take a 0..1 phase (wrapped) and return a new one, so `sin(bend(p, 0.5) * 2 * pi)` reshapes a sine. `bend`, `pinch`, `skew` take -1..1, `mirror` 0..1, `formant` a ratio 1..64; amount 0 (ratio 1) leaves the phase alone |
| `if(cond, a, b)` | evaluates **both** branches (no short-circuit) and returns one — fine since the language has no side effects |
| `rand(min, max, speed)` | smooth continuous random wander between `min` and `max` at the given speed — organic, not stepped |
| `noise(min, max, speed)` | same family as `rand`; use when the preset library's "noise" flavor is wanted over "rand"'s |
| `sh(min, max, rate)` | sample-and-hold: jumps to a new random value `rate` times per second and holds it |
| `rand`/`noise`/`sh` also accept fewer args: `f()`, `f(speed)`, or a 4th `seed` argument to get a different random stream | |
| `vec2(x,y) vec3(x,y,z) vec4(x,y,z,w)` | vector constructors; a single scalar arg splats, e.g. `vec3(1)` == `vec3(1,1,1)` |

Swizzles work on vector expressions: `.xy .xyz .xyzw` or `.rg .rgb .rgba`
(e.g. `P.xz`, `Cd.bgr`) — but remember a plain numeric parameter field wants a
single float back, so end a vector expression with a component access like
`.x` unless the field itself accepts a vector.

## Worked examples

**A simple oscillating parameter (typed directly into a `=` field):**
```
=sin(t) * 0.5 + 0.5
```

**Sweep a parameter across its own configured range:**
```
=lerp(lo, hi, sin(t) * 0.5 + 0.5)
```

**A tempo-synced pulse as a reusable Global**, then referenced by name from
any parameter:
```
beat = mod(t * 2, 1) < 0.5
```
Any parameter can then read `=lerp(lo, hi, beat)`.

**A gliding random walk Global** (combining `sh` and `smoothstep`):
```
rand_glide = lerp(sh(1, 0, 2), sh(1, 0, 2), smoothstep(0, 1, mod(t * 2, 1)))
```

**A one-bar (4-beat) ramp at 120 BPM:**
```
measure = mod(t * 0.5, 1)
```

## Mistakes to avoid

| Wrong | Right | Why |
|---|---|---|
| naming a Global `t`, `pi`, `lo`, or `hi` | pick another name | those are already bound by the evaluator and are rejected |
| a Global that references one defined below it in the list | reorder so dependencies come first | evaluation only sees globals *above* the current one |
| expecting `if(cond, a, b)` to skip the untaken branch | assume both are always evaluated | there is no short-circuit in this language |
| writing Field syntax here (`param float x = ...`, `state`, `attrib`, domains) | plain expressions only | this is the flat expression language, not Field — no declarations, no kernels |
| hardcoding raw units inside a parameter's own `=` field | use `lo`/`hi` and `lerp` | keeps the expression correct if the parameter's range is later changed |

## Before you finish

Put the expression in a patch and run `Infinite --validate patch.inf` (or `infinite patch check patch.inf`). Warnings count as errors. Fix every one before you tell the user it works. With the app running, `infinite context` shows `codeErrors` for what is open right now. Never hand over an expression that has not passed.
