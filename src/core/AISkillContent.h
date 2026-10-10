#pragma once

// Markdown text for the "Install AI Skill" buttons in Settings > Field
// Language and Settings > Expression Globals. Each is written verbatim to a
// single .md file in a user-chosen folder so it can be fed to any AI
// assistant (or dropped into ~/.claude/skills/ as a Claude Skill, since it
// already carries a SKILL.md-style frontmatter block).
//
// Kept content-accurate against the shipped Field/Expression compilers and
// their built-in node presets - see docs/ai-skills/ for the same text in
// standalone files, which is what these constants are generated from. If you
// change one, change the other; there is no single source of truth wiring
// them together yet. The exception is kPatchAuthoringMarkdown: its node facts
// are generated from `Infinite --describe --json`, and the whole constant is
// written by tools/gen-patch-skill.py (never edit it by hand; CI runs the
// script with --check).
namespace AISkillContent
{
   inline const char* kFieldLanguageMarkdown =
R"AISKILL(---
name: infinite-field-language
description: Write and debug Field kernel code for Infinite's Field nodes (Field Synth, Field Effect, Field Modifier, Field Pixel). Use whenever the user asks for a Field expression, a kernel for one of these nodes, or help fixing one that won't compile.
---

# Field — Infinite's per-element kernel language

Field is used inside these node types in Infinite: **Field Synth** and
**Field Effect** (audio), **Field Modifier** and **Field Primitive**
(geometry), **Field Pixel** (image), and **Field Graph** (edit-time node
graph metaprogramming — see its own section below). Each node's editor holds
one **kernel**: a body of code that runs once per element of that node's
domain. There is no other primitive — a "frame" kernel just has a domain with
one element per frame, a "pixel" kernel has one element per pixel.

Give the user a kernel body only — no function wrapper, no imports. Whatever
you write goes directly into that node's text editor.

## The one rule that matters most

**A single kernel lives in exactly one domain, and domains never mix.**
`in`/`out` (audio) only exist in Field Synth/Field Effect. `P`/`N`/`Cd`
(geometry) only exist in Field Modifier. `uv`/`col`/`res` (image) only exist
in Field Pixel. You cannot write one kernel that reads `in` and also writes
`P` — that is a compile error, not a missing feature. To make geometry or
pixels react to audio, expose a value from the audio kernel as a `param`,
then drive that same-named `param` on the geometry/pixel node from Infinite's
modulation matrix (a connection made in the app, not in code) — see the
worked example at the end of this file.

## Domains and their reserved words

| Domain | Node | Runs | Reserved names |
|---|---|---|---|
| frame | (implicit, any node) | once per frame (~60/s) | `t` (seconds), `dt`, `frame` (int counter) |
| element | Field Modifier, Field Primitive | once per point/vertex | `P` (vec3 position), `N` (vec3 normal), `uv` (vec2), `Cd` (vec3 color), `i` (index), `count` (total). In Field Modifier, `P`/`N`/`Cd` arrive from an upstream mesh to be offset; in Field Primitive there is no upstream mesh, so `uv` is a procedural parametric coordinate and the kernel computes `P`/`N`/`Cd` from scratch. |
| pixel | Field Pixel | once per pixel | `uv` (vec2, normalized 0-1), `xy` (pixel coord), `col` (vec3 output color), `res` (resolution), `aspect`, `alpha` |
| sample | Field Synth / Field Effect | once per audio sample (~48000/s) | `in`, `out`, `sr` (sample rate), `n` (sample index), `freq` (voice note in Hz), `gate` (1.0 while a note is held, else 0.0) |

Reserved words are read-only per-element context values — never declare a
local variable with the same name as one in the table above; that is a
compile error.

**Rates are never declared.** There is no `@rate` keyword and no `krate`
annotation. The compiler infers a value's domain from what it reads: an
expression that only reads `t` runs once per frame; one that reads `P` runs
once per element; and so on. A frame-domain value used inside an
element/pixel/sample expression is automatically computed once and reused —
this is called **broadcast** and it is never written explicitly:

```
amount = 0.5 + 0.5 * sin(t)   # computed once per frame
P.y += amount                  # every element reads the same amount, free
```

**No `@` sigils, ever.** Field uses bare names everywhere: `P.y += bass * 2`,
never `@P.y += bass * 2`.

## Declarations

```
param <type> <name> = <default> [<min>, <max>]
```
Declares a knob/slider that appears in the node's UI and can be modulated
from the matrix.
```
param float cutoff = 0.5 [0, 1]
```

```
state <type> <name> = <init>
```
A persistent one-step-delay memory cell — the only way to build a filter,
integrator, or feedback loop. A cycle in your math (`z` feeding back into
itself) is only legal if it passes through a `state` cell.
```
state float z = 0
z += (in - z) * cutoff
out = z
```

```
attrib <type> <name> = <init>
```
A custom per-element attribute (Field Modifier only) — must be declared
before use; there is no implicit creation.
```
attrib float heat = 0
heat += bass * 0.1
```

## Types

`float`, `int`, `bool`, `vec2`, `vec3`, `vec4`. Components: `.x .y .z .w` or
`.r .g .b .a`, and swizzles (`P.xz`, `col.bgr`). No structs, no arrays, no
strings, no pointers, no recursion.

**Rank polymorphism (scalar → vector broadcast only):**
```
P *= 2.0             # scalar 2.0 broadcasts across vec3 P — legal
P += vec2(1, 0)      # vec2 into vec3 — ERROR, no implicit fill
```

## Operators and built-in functions

| Category | Available |
|---|---|
| Arithmetic | `+ - * / % ^` (`^` is right-associative power: `2^3 == 8`) |
| Compound assign | `+= -= *= /=` |
| Compare / logic | `== != < <= > >= && \|\| !` |
| Trig | `sin cos tan asin acos atan atan2` |
| Math utilities | `abs floor ceil fract clamp lerp` (`lerp(a, b, t)`) |
| Advanced math | `smoothstep pow sqrt exp log min max` |
| Vector math | `length normalize dot cross reflect` |
| Randomness | `rand(t, seed)`, `noise(t, seed)` — pure, deterministic functions of time and seed, not stateful |

## Domain transfer operators

`element`, `pixel` and `sample` never mix implicitly. Every crossing between
them is either explicit (`reduce`, `resample`, `downsample`) or goes through
`frame`.

**`reduce.<op>(x)`** — many values to one, always written explicitly.
Ops: `reduce.sum`, `reduce.rms`, `reduce.mean`, `reduce.min`, `reduce.max`.

Inside a **sample**-domain kernel only, `reduce.rms(in, loHz, hiHz)` gives
band-limited RMS (e.g. "the bass level"). It is **output-only**: write it as
its own line, at most once per kernel, and it becomes a pin on that node —
not a value you can read back in the same kernel's other lines.
```
# inside a Field Synth / Field Effect kernel
output frame float bass = reduce.rms(in, 20, 200)   # exposed as a pin
out = in                                             # unaffected by the line above
```

**`map(N) { ... }`** — runs the body N times (N is a compile-time constant,
1-64) inside an element or pixel kernel, once per lane. Cost is N times the
body.

**`broadcast`** — one to many. Never write this; it happens automatically
(see the frame → element example above). Writing `broadcast(...)` is a
compile error.

**`resample(x, Domain)`** — read a value from another domain, sampling
rather than aggregating (can alias fine→coarse; prefer `reduce.rms` for
levels). `Domain` is a bare identifier: `frame`, `element`, `pixel`, `sample`.
```
level = resample(lfo, sample)   # frame value, held for the whole audio block
```

**`downsample(x, k)`** — runs `x`'s body once every `k` invocations and
holds the result in between. `k` must be a compile-time integer literal ≥ 1
(no benefit past ~32).
```
slow = downsample(lfo, 32)
```

## Branching

Field allows data-dependent branching, but the cost differs sharply by
domain — always know which domain you are in before you branch.

| Domain | Lowering | Cost |
|---|---|---|
| frame | real branch | free |
| element | real branch | breaks vectorization of the batch |
| sample | real branch | mispredict risk on the audio thread |
| pixel | predication | **always pays the cost of both sides** — the GPU evaluates both branches and selects, it never skips work |

```
if (P.y > 0.5) { Cd = vec3(1, 0, 0) }
```

Never tell a user a pixel-domain `if` "skips" the untaken branch — it doesn't.

## Canonical recipes

These are drawn from Infinite's own shipped built-in preset library for each
Field node type (the same presets a user sees in that node's "Presets"
dropdown), so they are guaranteed to compile and are worth studying as style
references, not just copy-paste starting points — the more real code you have
seen for a domain, the fewer compile errors you'll write in it.

### Field Synth (generates audio from `freq`/`gate`, no `in` needed)

Simple oscillator:
```
state float phase = 0
phase = phase + freq / sr
phase = phase - floor(phase)
out = sin(phase * 6.283185) * gate
```

A more complete shipped preset, "Dual Oscillator Pad" — two detuned
oscillators plus a sub, an amp envelope keyed off `gate`, and a one-pole
lowpass with resonance:
```
param float detune = 1.008 [1.0, 1.03]
param float cutoff = 2800.0 [200.0, 10000.0]
param float res = 0.35 [0.0, 0.92]
param float sub = 0.30 [0.0, 1.0]
param float warmth = 0.40 [0.0, 1.0]
param float attack = 0.05 [0.005, 0.5]
state float p1 = 0
state float p2 = 0
state float pSub = 0
state float env = 0
state float lp = 0

p1 = (p1 + freq / sr) % 1.0
p2 = (p2 + (freq * detune) / sr) % 1.0
pSub = (pSub + (freq * 0.5) / sr) % 1.0

target = if(gate > 0.5, 1.0, 0.0)
rate = if(target > env, clamp(1.0 / (sr * max(attack, 0.005)), 0.0001, 0.1), 0.0005)
env = env + (target - env) * rate

tone = sin(6.283185 * p1) * 0.45 + sin(6.283185 * p2) * 0.45 + sin(6.283185 * pSub) * sub
drive = 1.0 + warmth * 1.5
sat = (tone * drive) / (1.0 + abs(tone * drive * 0.5))

f = clamp((cutoff / sr) * 3.14159, 0.001, 0.95)
lp = lp + f * (sat - lp)
filt = lp + (sat - lp) * (1.0 - res)
out = filt * env
```
Note the pattern: `if(cond, a, b)` used freely for envelope-rate logic
(both branches evaluated, no side effects, so this is cheap), `%` used as
phase-wrap modulo, and every `state` cell named for what it holds.

### Field Effect (processes `in` -> `out`, one sample at a time)

1-pole lowpass:
```
param float cutoff = 0.25 [0.01, 0.99]
state float z = 0
z += (in - z) * cutoff
out = z
```

Expose a bass level for the mod matrix while passing audio through boosted:
```
param float boost = 1.5 [0.5, 4.0]
output frame float bass = reduce.rms(in, 20.0, 200.0)
out = in * boost
```

Shipped "Moog 4-Pole Ladder Filter" preset — four cascaded one-poles with
saturating feedback, and the standard dry/wet `mix` param pattern every
Field Effect preset uses:
```
param float cutoff = 2200.0 [80.0, 14000.0]
param float res = 0.70 [0.0, 0.98]
param float drive = 1.50 [1.0, 4.0]
param float mix = 1.0 [0.0, 1.0]
state float s1 = 0
state float s2 = 0
state float s3 = 0
state float s4 = 0

f = clamp((cutoff / sr) * 2.2, 0.005, 0.42)
feedback = (s4 * res * 3.6) / (1.0 + abs(s4) * 0.8)
u = (in * drive) - feedback
sat = u / (1.0 + abs(u) * 0.3)

s1 = s1 + f * (sat - s1)
s2 = s2 + f * (s1 - s2)
s3 = s3 + f * (s2 - s3)
s4 = s4 + f * (s3 - s4)

wet = s4
out = in * (1.0 - mix) + wet * mix
```
Every Field Effect preset ends the same way: compute `wet`, then
`out = in * (1.0 - mix) + wet * mix`. Reuse that shape for any new effect.

### Field Modifier (per-vertex geometry kernel: reads/writes `P`, `N`, `Cd`)

Wave ripple deformer:
```
param float speed = 2.0 [0, 10]
param float height = 0.3 [0, 2]
dist = length(P.xz)
P.y += sin(dist * 4.0 - t * speed) * height
Cd = vec3(0.5 + 0.5 * sin(P.y * 5.0), 0.4, 0.8)
```

**`publish`** is a special output name: assign a frame-scope scalar to a
variable named `publish` and it becomes the node's second output pin — a
single float other nodes can read, the same way `reduce` exposes one. Nearly
every shipped Field Modifier preset ends with a `publish = ...` line so the
node has something to feed the mod matrix, e.g. `publish = sin(t * speed)`
or `publish = reduce.max(hit)`. Add one whenever the user wants this node's
motion to drive something else downstream.

Shipped "Radial Ripple" preset:
```
param float freq = 8.0 [1.0, 25.0]
param float amp = 0.25 [0.0, 1.5]
param float speed = 3.0 [0.0, 10.0]
d = length(vec2(P.x, P.z))
P.y += sin(d * freq - t * speed) * amp / (1.0 + d)
publish = sin(t * speed)
```

### Field Primitive (generates a mesh from scratch: writes `P`, `N`, `Cd` from `uv`)

Unlike Field Modifier, there is no incoming mesh — `uv` here is a procedural
parametric coordinate (e.g. `[0,1]x[0,1]` over a plane or a sphere's
lat/long), and the kernel's job is to compute `P` outright, not offset it.
Shipped "Solid Terrain Plane" preset:
```
param float size = 2.5 [0.5, 10.0]
param float height = 0.40 [0.0, 2.0]
param float freq = 2.5 [0.5, 8.0]
param float speed = 1.0 [0.0, 5.0]
x = (uv.x - 0.5) * size
z = (uv.y - 0.5) * size
dist = sqrt(x * x + z * z)
wave = sin(dist * freq - (t + 1.0) * speed)
y = wave * height * exp(-dist * 0.4)
P = vec3(x, y, z)
slope = cos(dist * freq - (t + 1.0) * speed) * freq * height * exp(-dist * 0.4)
nx = if(dist > 0.001, -(x / dist) * slope, 0.0)
nz = if(dist > 0.001, -(z / dist) * slope, 0.0)
N = normalize(vec3(nx, 1.0, nz))
Cd = vec3(0.15 + 0.35 * (y + 0.5), 0.55 + 0.4 * sin(dist * 3.0), 0.85)
publish = sin((t + 1.0) * speed)
```
Note the analytic normal computed from the height field's slope — a Field
Primitive that displaces `P` procedurally should compute `N` to match, not
leave it at its default, or lighting will look wrong.

### Field Pixel (per-pixel kernel, GLSL-backed: writes `col`)

Declare `input pixel image img;` to read a patched image. Bare `img` is the
current pixel (vec4); `img(coord)` samples at one normalized vec2 coordinate,
clamped to the source texture's edge texel centres. Filtering follows the
source sampler, normally linear. Preserve sampled alpha with `alpha = c.a`.
Only declared image inputs are callable; `col(coord)` is invalid. One image
input is supported, and an unconnected input reads transparent black.
`1/res.x` is a one-source-texel shift only if source and output widths match.

```glsl
input pixel image img;
c = img(uv + vec2(1.0 / res.x, 0));
col = c.rgb;
alpha = c.a;
```

**Aspect correction is the single most common mistake in this domain.**
`uv` runs `[0,1]` on both axes regardless of the canvas's actual width and
height, so any shape built directly from raw `uv` distances stretches into
an ellipse on a non-square canvas. Every shipped preset that draws a round
or grid shape corrects for it the same way, using the reserved `aspect`
value, before doing anything else:
```
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5)   # aspect-correct, centered coords
```
Shipped "Radial Glow" style preset built on that pattern:
```
param float radius = 0.3 [0.05, 0.8]
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5)
d = length(p) - radius
glow = clamp(0.02 / (abs(d) + 0.02), 0.0, 1.0)
col = vec3(glow * 0.9, glow * 0.4, glow * 1.0)
```
Shipped "Organic Blobs / Metaballs" preset — note the inverse-square falloff
(`r2 / d2`, not `1 / d`): a plain inverse-distance falloff drops off too
fast for two blobs' fields to ever overlap and merge, so it always renders
as separate flat circles instead of blending:
```
param float speed = 1.2 [0.1, 5.0]
param float size = 0.16 [0.05, 0.3]
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5))AISKILL"
R"AISKILL(b1 = vec2(0.28 * cos(t * speed), 0.28 * sin(t * speed * 0.8))
b2 = vec2(0.24 * cos(t * speed * 1.3 + 2.0), 0.24 * sin(t * speed * 1.1 + 1.0))
r2 = size * size
d1 = dot(p - b1, p - b1) + 0.0008
d2 = dot(p - b2, p - b2) + 0.0008
fld = r2 / d1 + r2 / d2
iso = smoothstep(0.85, 1.15, fld)
col = vec3(iso * 0.2, iso * 0.8, iso * 0.9)
```

### Field Graph (edit-time kernel that builds part of the node graph itself)

Runs once, at edit-time, not per-frame. `emit("Type Name", k)` spawns a node
(k is a stable per-emit-site key so re-running diffs against what it built
last time instead of respawning), `connect(fromHandle, fromPin, toHandle,
toPin)` wires two nodes, `place(handle, x, y)` positions one.

**Use the exact spawnable type name Infinite's node browser shows**, e.g.
`"Field Modifier"` and `"Field Effect"` — not an internal class name or an
abbreviation. If the user names a node type ambiguously, prefer the name as
it appears in Infinite's own node browser/menus.
```
seq = emit("Note Sequencer", 0)
synth = emit("Wavetable", 1)
flt = emit("Audio Filter", 2)
connect(seq, 0, synth, 0)
connect(synth, 0, flt, 0)
place(seq, -420, 0)
place(synth, -200, 0)
place(flt, 20, 0)
```

**Audio driving geometry (two nodes, not one kernel):**
```
# Node A: Field Effect
output frame float bass = reduce.rms(in, 20, 200)
out = in
```
Then, in the app, connect Node A's `bass` pin to Node B's `bass` param
through the modulation matrix.
```
# Node B: Field Modifier
param float speed = 2.0 [0, 10]
param float bass = 0.0 [0, 1]     # driven by Node A via the mod matrix
dist = length(P.xz)
P.y += sin(dist * 4.0 - t * speed) * bass * 1.5
Cd = vec3(bass, 0.3, 1.0 - bass)
```

## Mistakes to avoid

| Wrong | Right | Why |
|---|---|---|
| `@P.y += bass` | `P.y += bass` | no sigils, ever |
| declaring `@rate` or `krate` | nothing — just use the reserved names | rate is always inferred |
| `float t = 0.5` inside a frame-rate expression | pick another name, e.g. `phase` | `t` is reserved |
| `heat += 1` with no prior declaration | `attrib float heat = 0` first | attributes must be declared |
| one kernel reading both `in` and `P` | two nodes + a mod-matrix connection | audio/geometry/pixel domains never share a kernel |
| `P += vec2(1, 0)` | `P += vec3(1, 0, 0)` | broadcast is scalar→vector only |
| describing a pixel `if` as "skipping" a branch | describe it as evaluating both and selecting | GPU predication, not a real branch |
| `reduce.rms(in, lo, hi)` used inline in the middle of per-sample math | its own `output frame float name = reduce.rms(...)` line | it's a pin, not a readable local |
| a Field Pixel shape built from raw `uv` distances | correct with `aspect` first: `p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5)` | uncorrected shapes stretch into ellipses on a non-square canvas |
| a metaball/blob field using `1 / dist` falloff | use inverse-square, `r*r / dist2` | `1/dist` drops off too fast for two fields to ever merge; it always renders as separate flat circles |
| a Field Modifier/Primitive preset with nothing feeding the mod matrix | end with `publish = <frame-scope scalar>` | `publish` is the reserved output name that becomes the node's second pin |
| `emit("Field Element", ...)` / `emit("Field Sample", ...)` in a Field Graph kernel | `emit("Field Modifier", ...)` / `emit("Field Effect", ...)` | those are the node browser's actual spawnable names |
)AISKILL";

   inline const char* kExpressionGlobalsMarkdown = R"AISKILL(---
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
)AISKILL";

   inline const char* kPatchAuthoringMarkdown =
R"AISKILL(---
name: infinite-patch-authoring
description: Write, check and render Infinite patches (.inf text files) headless, without the UI. Use whenever the user asks you to build, change or debug an Infinite patch, wire nodes, add modulation, or render a frame, video or audio measurement from a patch file.
---

# Authoring Infinite patches headless

A patch is a line-based text file. You write it, Infinite checks it and renders it, and you read
the result back as JSON. Never guess a node's name, keys, slots or ranges: ask the binary.

`Infinite` below is the executable: `build/Infinite.app/Contents/MacOS/Infinite` on macOS,
`Infinite.exe` on Windows, `Infinite` on Linux. On Linux there is no display, so put
`xvfb-run -a` in front of every render (`E_NO_DISPLAY` means you forgot).

## The loop (do every step, in order)

1. **Describe** - `Infinite --describe "<type>" --json out.json`. Read the type's `params`
   (key, default, min, max, dropdown options), `inputs` (slot names) and `outputs`. Do this for
   every node type you use. Not from memory.
2. **Write** - the smallest patch that could work. Leave every parameter you do not change at its
   default; a node needs only the keys you set.
3. **Lay out and annotate** - a node with no `pos` spawns at 0,0, so a hand-written patch opens as one
   stacked pile. Always finish with `python3 tools/patch-layout.py patch.inf`: Picture / Sound /
   Modulation bands, left to right by wiring depth, sized per node type. Always add `Comment` nodes
   (`node N Compositing Comment`, `s text ...`, `f width`, `f height`): one header per band
   (`# band Picture` on the line before the node) and one beside every node a user would want explained
   (`# near <id>`); the tool places them. Check with `python3 tools/patch-layout-preview.py patch.inf box.png`
   (no GUI needed). New SIZES entries go in the tool when a type overlaps its neighbours.
4. **Validate** - `Infinite --validate patch.inf`. Strict by default: warnings are errors too.
   Fix until the exit code is 0. Every error carries a line number and a hint.
5. **Explain and compare to intent** - `Infinite --explain patch.inf`. It prints the graph that
   was actually built. Read it against what the user asked for: is every wire you wrote there,
   is every node you expect reachable from an Output or Audio Out, is any parameter still at a
   default you meant to change? A patch can validate and still be the wrong patch.
6. **Look and listen** - `Infinite --frame patch.inf 0,1,2 dir/ --contact-sheet sheet.png` for
   pictures, `Infinite --audio-summary patch.inf out.json` for sound. Read `frame_stats` and
   `audio_summary` from the status line before you open any file.
7. **Fix** - change the patch, go back to step 4. Change one thing at a time.
8. **Render** - `Infinite --render patch.inf out.mp4 [--start S] [--duration S] [--fps N]`.

## Reading the result

The last line of stdout is one JSON object: `ok`, `mode`, `frames`, `fps`, `size`, `audio`,
`elapsed_ms`, `files`, `warnings[]`, `errors[]`, plus `frame_stats[]` (`--frame`) or
`audio_summary` (`--audio-summary`). Each issue is `{code, message, line, node, hint}`.

| Exit | Means |
|---|---|
| 0 | ok |
| 2 | bad command line (`E_USAGE`) |
| 3 | the patch does not load or validate; read `errors[0]` |
| 4 | refused: a hardware source (camera, microphone, MIDI) cannot render headless |
| 5 | rendering failed (`E_RENDER`, `E_NO_DISPLAY`) |
| 6 | timeout (raise `--timeout`) |

## The format in one screen

```
infinite-patch 1
node <index> <category> <type name>
  id <word>                 optional name you can use instead of the index
  f <key> <float>   i <key> <int or option name>   b <key> <0|1>
  c <key> <r> <g> <b>       s <key> <text to end of line>
end
cable <dst> <slot> <src>    image          geo <dst> <slot> <src>   geometry, camera, light
aud   <dst> <slot> <src>    audio          note <dst> <slot> <src>  notes
mod <dst> <param|key> <src> <srcOutput> <polarity> <depth> <centre>
expr <dst> <param|key> <expression>        glob <name> <expression>
```

- Wires read destination first: `cable out 0 shape` = "Output slot 0 is fed by shape".
- Names work everywhere an index does: node `id`s, slot names (`input`, `input_2`, ...), and a
  parameter's saved key in `mod`/`expr` (`mod shape sizeX lfo 0 0 1 0.5`).
- Free text (type names, strings, expressions) is always last on its line.
- Full reference: `docs/reference/patch-format.md`. Three verified starting points live in
  `assets/examples/authoring/` (image, audio, modulation).

## Rules that bite

- **Defaults rule.** Omit what you do not change. Never copy every key of a node into a patch.
- **No sigils.** Field and expression code uses bare names (`P.y += bass * 2`), never `@P.y`.
  Field kernels: see the `infinite-field-language` skill.
- **Field `param`s are not modulatable headless.** `mod scene myParam ...` fails `E_BAD_KEY` (only
  width/height/animate are keyed). Animate Field code from `t` inside the kernel, and drive the rest of
  the chain (bloom, lensdistortion, Trails, audio effects) from LFOs/`Audio Analyze`. To keep audio and
  picture locked, give the LFO and the kernel the same period (kernel `sin(6.2832*t/12)` = LFO `rateBeats 24` at 120 bpm).
- **Field kernels: unroll loops** (generate repeated blocks from Python) and declare no `param`s you cannot
  reach. Check brightness with `frame_stats.mean_luma` (aim ~0.1-0.3 for dark-ground art); Trails in
  Screen/Add mode accumulates to white, use Max. `--frame` times are seconds. A generator script like
  `art/prism/gen_prism.py` is the easiest way to iterate.
- **Bypass.** A node with two or more inputs never bypasses; a bypassed node does not cook.
  `flags 0 1 0 0` on such a node raises `W_BYPASS_IGNORED`.
- **Something must be reachable.** An image needs an `Utility Output`; sound needs an
  `Utility Audio Out` or an Output with audio wired in.
- **Animate inside the patch.** Use modulator nodes, `mod`, `expr` and `glob`. Do not render a
  frame at a time with edited parameters.
- **One-input merge nodes** (Blend, Layer Stack, Mixer) with an empty input dim rather than fail:
  `W_OPEN_INPUT`. Wire every input.

## Troubleshooting

| Symptom | Look at |
|---|---|
| Black frame, `W_BLACK_FRAME` | `--explain`: is the Output's image slot wired (`W_OUTPUT_EMPTY`)? Is the source's size or colour zero? Is a node upstream bypassed? |
| Silent audio, `W_SILENT` | is an `Audio Out` reachable from a synth? Do the notes reach the synth (`note` wire)? Is a volume or gain at 0? |
| `W_UNUSED_NODE` | the node reaches no Output or Audio Out: wire it in or delete it |
| `E_KIND_MISMATCH` | the source and destination slot kinds differ (image into an audio slot); `--describe` lists each slot's kind |
| `E_BAD_KEY` / `E_BAD_VALUE` | misspelled control key or dropdown option; the hint names the nearest valid one |
| `E_HARDWARE_SOURCE` (exit 4) | the patch has a camera, microphone or MIDI source; replace it with a file or generator |
| `E_NO_DISPLAY` (Linux) | run under `xvfb-run -a` |

## Node facts

Everything below is generated from `Infinite --describe --json` by `tools/gen-patch-skill.py`.
Do not edit between the markers; run the script. Use the `node` line exactly as shown (category
first, then the type name), then `--describe "<type>"` for parameters.

<!-- generated:begin -->
307 node types. `node <index> <category> <type>`; inputs are `slot name:kind`, outputs `label:kind`; `p` is the number of saved parameters; `bypass` marks single-input nodes that can be bypassed.


### 3D

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Array` | geo:geometry | out:geometry | 65 | bypass |
| `Arrow 3D` | texture:image | out:geometry | 50 | bypass |
| `Audio Displacement` | geo:geometry, audio:audio | out:geometry | 32 |  |
| `Audio Ribbon` | input:audio | out:geometry | 23 | bypass |
| `Camera` | none | out:camera | 13 | bypass |
| `Capsule` | texture:image | out:geometry | 50 | bypass |
| `Cloth` | geo:geometry | out:geometry | 38 | bypass |
| `Cone` | texture:image | out:geometry | 50 | bypass |
| `Cube` | texture:image | out:geometry | 50 | bypass |
| `Curve` | none | out:geometry | 32 | bypass |
| `Cylinder` | texture:image | out:geometry | 50 | bypass |
| `Delete` | geo:geometry | out:geometry | 65 | bypass |
| `Delete Selected` | geo:geometry | out:geometry | 65 | bypass |
| `Depth Projection` | depth:image, color:image | out:geometry | 35 |  |
| `Difference` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry | out:geometry | 23 |  |
| `Disc` | texture:image | out:geometry | 50 | bypass |
| `Displacement` | geo:geometry, texture:image | out:geometry | 23 |  |
| `Distribute Points in Grid` | none | out:geometry | 8 | bypass |
| `Distribute Points on Faces` | geo:geometry | out:geometry | 22 | bypass |
| `Dodecahedron` | texture:image | out:geometry | 50 | bypass |
| `Explode` | geo:geometry | out:geometry | 65 | bypass |
| `Extrude` | geo:geometry | out:geometry | 65 | bypass |
| `Extrude Selected` | geo:geometry | out:geometry | 65 | bypass |
| `Field Modifier` | geo:geometry | geo:geometry | 11 | bypass |
| `Field Primitive` | none | geo:geometry | 13 | bypass |
| `Gear 3D` | texture:image | out:geometry | 50 | bypass |
| `Geometry` | texture:image | out:geometry | 50 | bypass |
| `Group 3D` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry, geo_e:geometry, geo_f:geometry, geo_g:geometry, geo_h:geometry | out:geometry | 0 |  |
| `HDRI` | none | out:environment | 3 | bypass |
| `Helix` | texture:image | out:geometry | 50 | bypass |
| `Icosphere` | texture:image | out:geometry | 50 | bypass |
| `Image to Points` | input:image | out:geometry | 10 | bypass |
| `Instance on Points` | points:geometry, shape:geometry, cloud:geometry | out:geometry | 25 |  |
| `Intersect` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry | out:geometry | 23 |  |
| `Join Geometry` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry | out:geometry | 23 |  |
| `Klein Bottle` | texture:image | out:geometry | 50 | bypass |
| `Light` | none | out:light | 10 | bypass |
| `Mapping` | geo:geometry | out:geometry | 11 | bypass |
| `Material` | geo:geometry, albedo:image, roughness:image, metallic:image, normal:image, ao:image, emission:image, clearcoat:image, sheen:image | out:geometry | 30 |  |
| `Merge by Distance` | geo:geometry | out:geometry | 1 | bypass |
| `Mesh to Edges` | geo:geometry | out:geometry | 22 | bypass |
| `Mesh to Faces` | geo:geometry | out:geometry | 22 | bypass |
| `Mesh to Points` | geo:geometry | out:geometry | 22 | bypass |
| `Metaballs` | cloud:geometry | out:geometry | 28 | bypass |
| `Mirror` | geo:geometry | out:geometry | 65 | bypass |
| `Mobius Strip` | texture:image | out:geometry | 50 | bypass |
| `Model 3D` | texture:image | out:geometry | 27 | bypass |
| `Normals` | geo:geometry | out:geometry | 65 | bypass |
| `Null 3D` | geo:geometry | out:geometry | 0 | bypass |
| `Ocean` | texture:image | out:geometry | 29 | bypass |
| `Octahedron` | texture:image | out:geometry | 50 | bypass |
| `Particle System` | none | out:geometry | 23 | bypass |
| `Plane` | texture:image | out:geometry | 50 | bypass |
| `Points to Vertices` | points:geometry | out:geometry | 18 | bypass |
| `Prism` | texture:image | out:geometry | 50 | bypass |
| `Pyramid` | texture:image | out:geometry | 50 | bypass |
| `Render 3D` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry, camera:camera, light_1:light, light_2:light, light_3:light, env:environment | out:image | 39 |  |
| `Resynthesize 3D` | geo:geometry | out:geometry | 13 | bypass |
| `Rounded Cube` | texture:image | out:geometry | 50 | bypass |
| `Screw` | geo:geometry | out:geometry | 65 | bypass |
| `Select` | geo:geometry | out:geometry | 65 | bypass |
| `Set Vertex Color` | geo:geometry, texture:image, palette:palette | out:geometry | 6 |  |
| `Smooth` | geo:geometry | out:geometry | 65 | bypass |
)AISKILL"
R"AISKILL(| `Solidify` | geo:geometry | out:geometry | 65 | bypass |
| `Sphere` | texture:image | out:geometry | 50 | bypass |
| `Star 3D` | texture:image | out:geometry | 50 | bypass |
| `Subdivide` | geo:geometry | out:geometry | 65 | bypass |
| `Supershape` | texture:image | out:geometry | 50 | bypass |
| `Switcher 3D` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry | out:geometry | 5 |  |
| `Tetrahedron` | texture:image | out:geometry | 50 | bypass |
| `Text 3D` | texture:image | out:geometry | 29 | bypass |
| `Torus` | texture:image | out:geometry | 50 | bypass |
| `Torus Knot` | texture:image | out:geometry | 50 | bypass |
| `Transform` | geo:geometry | out:geometry | 65 | bypass |
| `Transform Selected` | geo:geometry | out:geometry | 65 | bypass |
| `Triangulate` | geo:geometry | out:geometry | 65 | bypass |
| `Tube` | texture:image | out:geometry | 50 | bypass |
| `Twist` | geo:geometry | out:geometry | 65 | bypass |
| `Union` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry | out:geometry | 23 |  |
| `Wireframe` | geo:geometry | out:geometry | 65 | bypass |
| `Wrap` | source:geometry, target:geometry | out:geometry | 26 |  |

### AudioEffects

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Audio Filter` | audio:audio | out:audio | 10 | bypass |
| `Bitcrush` | audio:audio | out:audio | 4 | bypass |
| `Chorus` | audio:audio | out:audio | 10 | bypass |
| `Cycle Shaper` | audio:audio | out:audio | 5 | bypass |
| `Delay` | audio:audio | out:audio | 10 | bypass |
| `Drive` | audio:audio | out:audio | 6 | bypass |
| `Dynamics` | audio:audio, sidechain:audio | out:audio | 9 |  |
| `EQ` | audio:audio | out:audio | 28 | bypass |
| `Field Effect` | in:audio | out:audio | 10 | bypass |
| `Flanger` | audio:audio | out:audio | 9 | bypass |
| `Formant Filter` | audio:audio | out:audio | 3 | bypass |
| `Frequency Shifter` | audio:audio | out:audio | 6 | bypass |
| `Key-Snap` | audio:audio | out:audio | 6 | bypass |
| `Limiter` | audio:audio | out:audio | 5 | bypass |
| `Phaser` | audio:audio | out:audio | 9 | bypass |
| `Pitch Shifter` | audio:audio | out:audio | 4 | bypass |
| `Plugin` | in:audio | out:audio | 7 | bypass |
| `Resonator Bank` | audio:audio | out:audio | 9 | bypass |
| `Reverb` | audio:audio | out:audio | 7 | bypass |
| `Ring Mod` | audio:audio | out:audio | 4 | bypass |
| `Shape Resonator` | audio:audio, shape:geometry | out:audio | 6 |  |
| `Spec Blur` | audio:audio | out:audio | 6 | bypass |
| `Spectrum Slide` | audio:audio, to:audio | out:audio | 2 |  |
| `Stereo` | audio:audio | out:audio | 4 | bypass |
| `Stutter` | audio:audio | out:audio | 6 | bypass |
| `Transient Shaper` | audio:audio | out:audio | 3 | bypass |
| `Tremolo` | audio:audio | out:audio | 7 | bypass |
| `Wavetable Shaper` | audio:audio | out:audio | 8 | bypass |

### Compositing

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Audio Color Ramp` | img:image, audio:audio | out:image | 28 |  |
| `Blend` | input:image, input_2:image | out:image | 2 |  |
| `Color Ramp` | input:image | out:image | 67 | bypass |
| `Comment` | none | out:image | 5 |  |
| `Curves` | input:image | out:image | 6 | bypass |
| `Feedback` | input:image | out:image | 1 | bypass |
| `Fit` | input:image | out:image | 8 | bypass |
| `Group` | none | out:image | 4 | bypass |
| `Layer Stack` | input:image, input_2:image, input_3:image, input_4:image | out:image | 8 |  |
| `Null` | in:image | out:image | 0 | bypass |
| `Reaction Diffusion` | input:image | out:image | 11 | bypass |
| `Remove Background` | input:image | out:image | 9 | bypass |
| `Switcher` | input:image, input_2:image, input_3:image, input_4:image | out:image | 5 |  |
| `Trails` | input:image | out:image | 7 | bypass |
| `Viewport` | in:image | out:image | 0 | bypass |
| `alpha from luma` | input:image | out:image | 1 | bypass |
| `alpha invert` | input:image | out:image | 0 | bypass |
| `alpha levels` | input:image | out:image | 3 | bypass |
| `chroma key` | input:image | out:image | 5 | bypass |
| `color adjustments` | input:image | out:image | 23 | bypass |
| `coloroverlay` | input:image | out:image | 2 | bypass |
| `dropshadow` | input:image | out:image | 4 | bypass |
| `exposure` | input:image | out:image | 1 | bypass |
| `gradientmap` | input:image | out:image | 3 | bypass |
| `invert` | input:image | out:image | 0 | bypass |
| `lookup` | input:image, input_2:image | out:image | 3 |  |
| `luma key` | input:image | out:image | 5 | bypass |
| `lut` | input:image, input_2:image | out:image | 2 |  |
| `opacity` | input:image | out:image | 1 | bypass |
| `outerglow` | input:image | out:image | 2 | bypass |
| `posterize` | input:image | out:image | 1 | bypass |
| `premultiply` | input:image | out:image | 1 | bypass |
| `set alpha` | input:image, input_2:image | out:image | 0 |  |
| `show alpha` | input:image | out:image | 0 | bypass |
| `threshold` | input:image | out:image | 1 | bypass |
| `transform` | input:image | out:image | 10 | bypass |

### Effects

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Resynthesize` | input:image | out:image | 20 | bypass |
| `addnoise` | input:image | out:image | 1 | bypass |
| `bloom` | input:image | out:image | 3 | bypass |
| `boxblur` | input:image | out:image | 1 | bypass |
| `convolve` | input:image | out:image | 13 | bypass |
| `crop` | input:image | out:image | 6 | bypass |
| `diffuseglow` | input:image | out:image | 2 | bypass |
| `displace` | input:image, input_2:image | out:image | 2 |  |
| `edge outline` | input:image | out:image | 3 | bypass |
| `edge sobel` | input:image | out:image | 2 | bypass |
| `emboss` | input:image | out:image | 4 | bypass |
| `gaussianblur` | input:image | out:image | 1 | bypass |
| `glitch` | input:image | out:image | 5 | bypass |
| `halftone` | input:image | out:image | 3 | bypass |
| `kaleidoscope` | input:image | out:image | 5 | bypass |
| `lensdistortion` | input:image | out:image | 3 | bypass |
| `liquify` | input:image | out:image | 3 | bypass |
| `mirror tile` | input:image | out:image | 1 | bypass |
| `motionblur` | input:image | out:image | 2 | bypass |
| `normal map` | input:image | out:image | 3 | bypass |
| `pinchpunch` | input:image | out:image | 4 | bypass |
| `pixelate` | input:image | out:image | 1 | bypass |
| `radialblur` | input:image | out:image | 3 | bypass |
| `ripple` | input:image | out:image | 5 | bypass |
| `symmetry` | input:image | out:image | 4 | bypass |
| `twirl` | input:image | out:image | 4 | bypass |
| `unsharpmask` | input:image | out:image | 2 | bypass |
| `vignette` | input:image | out:image | 5 | bypass |

### Macros

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Macro Bipolar Knob` | none | out:modulator | 2 | bypass |
| `Macro Knob` | none | out:modulator | 2 | bypass |
| `Macro NumBox` | none | out:modulator | 5 | bypass |
| `Macro Radio Selector` | none | out:modulator | 3 | bypass |
| `Macro Slider` | none | out:modulator | 2 | bypass |
| `Macro Step Gate` | none | out:modulator | 3 | bypass |
| `Macro Toggle` | none | out:modulator | 2 | bypass |
| `Macro Trigger` | none | out:modulator | 2 | bypass |
| `Macro XY` | none | x:modulator, y:modulator | 5 | bypass |

### Modulators

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Audio Analyze` | audio:audio | level:modulator, low:modulator, mid:modulator, high:modulator, onset:modulator, b1:modulator, b2:modulator, b3:modulator, b4:modulator, b5:modulator, b6:modulator, b7:modulator, b8:modulator | 4 | bypass |
| `Audio File` | none | out:audio | 9 | bypass |
| `Audio to CV` | audio:audio | out:modulator | 6 | bypass |
| `CV Recorder` | in:modulator | out:modulator | 6 | bypass |
| `CV to Pitch` | in:modulator | out:modulator | 6 | bypass |
| `Compare` | input:modulator, input_2:modulator | out:modulator | 4 |  |
| `Constant` | none | out:modulator | 1 | bypass |
| `Envelope` | in:modulator | out:modulator | 6 | bypass |
| `Geometry Table` | geo:geometry | cx:modulator, cy:modulator, cz:modulator, spread:modulator, x1:modulator, y1:modulator, z1:modulator, x2:modulator, y2:modulator, z2:modulator, x3:modulator, y3:modulator, z3:modulator, x4:modulator, y4:modulator, z4:modulator | 10 | bypass |
| `Hand Track` | input:image | present:modulator, palm x:modulator, palm y:modulator, index x:modulator, index y:modulator, thumb x:modulator, thumb y:modulator, pinch:modulator, open:modulator, roll:modulator, size:modulator | 3 | bypass |
| `Image Analyze` | input:image | result:modulator, bright:modulator, contrast:modulator, red:modulator, green:modulator, blue:modulator, sat:modulator, hue:modulator, motion:modulator, cx:modulator, cy:modulator | 14 | bypass |
| `Invert` | in:modulator | out:modulator | 3 | bypass |
| `LFO` | none | out:modulator | 5 | bypass |
| `MIDI CC` | none | out:modulator | 7 | hardware: refused headless, bypass |
| `MIDI Trigger` | none | out:modulator | 6 | hardware: refused headless, bypass |
| `Math` | input:modulator, input_2:modulator | out:modulator | 6 |  |
| `Mod Curve` | in:modulator | out:modulator | 3 | bypass |
| `Mod Depth` | in:modulator | out:modulator | 2 | bypass |
| `Note to CV` | notes:note | out:modulator | 3 | bypass |
| `Null Modulator` | in:modulator | out:modulator | 1 | bypass |
| `Palette` | ref:image | out:palette | 14 | bypass |
| `Path` | curve:geometry, geo:geometry | x:modulator, y:modulator, z:modulator, t:modulator | 14 |  |
| `Pattern` | none | out:modulator | 24 | bypass |
| `Random` | none | out:modulator | 5 | bypass |
| `Range to Range` | in:modulator | out:modulator | 6 | bypass |
| `Smoothing` | in:modulator | out:modulator | 2 | bypass |
| `Velocity to CV` | notes:note | out:modulator | 2 | bypass |
| `Vibrato` | none | out:modulator | 1 | bypass |

### Notes

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Arpeggiator` | notes:note | out:note | 8 | bypass |
| `Bouncing Balls` | none | out:note | 9 | bypass |
| `Chorder` | none | out:note | 9 | bypass |
| `Gate` | notes:note | out:note | 1 | bypass |
| `Glide` | notes:note | out:note | 1 | bypass |
| `Humanizer` | notes:note | out:note | 2 | bypass |
| `Keyboard` | none | out:note | 5 | bypass |
| `MIDI File` | none | out:note | 6 | bypass |
| `MIDI Notes` | none | out:note | 6 | hardware: refused headless, bypass |
| `Note Capturer` | notes:note | out:note | 2 | bypass |
| `Note Echo` | notes:note | out:note | 7 | bypass |
| `Note Filter` | notes:note | out:note | 6 | bypass |
| `Note Merge` | in_1:note, in_2:note, in_3:note, in_4:note | out:note | 0 |  |
| `Note Router` | notes:note | 1:note, 2:note, 3:note, 4:note | 2 | bypass |
| `Note Sequencer` | none | out:note | 54 | bypass |
| `Note Stack` | notes:note | out:note | 17 | bypass |
| `Note Strum` | notes:note | out:note | 1 | bypass |
| `Note Switcher` | in_1:note, in_2:note, in_3:note, in_4:note | out:note | 5 |  |
| `Note Transpose` | notes:note | out:note | 2 | bypass |
| `Pitch Bend` | notes:note | out:note | 1 | bypass |
| `Quantizer` | notes:note | out:note | 1 | bypass |
| `Random Note Generator` | none | out:note | 10 | bypass |
| `Velocity Curve` | notes:note | out:note | 1 | bypass |

### Prediction

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Drift` | none | out:predictor | 20 | bypass |
| `Moves` | none | out:predictor | 6 | bypass |
| `Predictive Coloring` | input:image | out:image | 7 | bypass |
| `Predictive Modulator` | in:modulator | out:modulator | 6 | bypass |
| `Predictive Notes` | notes:note | out:note | 10 | bypass |
| `Predictive Quantize` | notes:note | out:note | 1 | bypass |
| `Predictive Rhythm` | notes:note | out:note | 5 | bypass |
| `Predictive Velocity` | notes:note | out:note | 1 | bypass |

### Source

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Arrow` | none | out:image | 20 | bypass |
| `Audio Texture` | input:audio | out:image | 4 | bypass |
| `Blob` | none | out:image | 20 | bypass |
| `Chevron` | none | out:image | 20 | bypass |
| `Circle` | none | out:image | 20 | bypass |
| `Crescent` | none | out:image | 20 | bypass |
| `Cross` | none | out:image | 20 | bypass |
| `Draw` | input:image | out:image | 13 | bypass |
| `Ellipse` | none | out:image | 20 | bypass |
)AISKILL"
R"AISKILL(| `FieldPixel` | none | out:image | 9 | bypass |
| `Formula` | none | out:image | 8 | bypass |
| `Gear` | none | out:image | 20 | bypass |
| `Heart` | none | out:image | 20 | bypass |
| `Hexagon` | none | out:image | 20 | bypass |
| `Image Source` | none | out:image | 1 | bypass |
| `Line` | none | out:image | 20 | bypass |
| `Noise` | none | out:image | 15 | bypass |
| `Pie` | none | out:image | 20 | bypass |
| `Polygon` | none | out:image | 20 | bypass |
| `Ramp` | none | out:image | 22 | bypass |
| `Rectangle` | none | out:image | 20 | bypass |
| `Ring` | none | out:image | 20 | bypass |
| `Rounded Rect` | none | out:image | 20 | bypass |
| `Shape` | none | out:image | 20 | bypass |
| `Sketch` | none | out:image | 8 | bypass |
| `Slideshow` | none | out:image | 7 | bypass |
| `Star` | none | out:image | 20 | bypass |
| `Superellipse` | none | out:image | 20 | bypass |
| `Teardrop` | none | out:image | 20 | bypass |
| `Text` | none | out:image | 20 | bypass |
| `Texture` | none | out:image | 47 | bypass |
| `Triangle` | none | out:image | 20 | bypass |
| `Video` | none | video:image, audio:audio | 7 | bypass |
| `Video In` | none | out:image | 4 | hardware: refused headless, bypass |

### Synths

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Analog` | notes:note | out:audio | 33 | bypass |
| `Drum Sequencer` | none | out:audio, 1:audio, 2:audio, 3:audio, 4:audio, 5:audio, 6:audio, 7:audio, 8:audio | 371 | bypass |
| `Equation Synth` | notes:note | out:audio | 30 | bypass |
| `Field Synth` | notes:note, in:audio | out:audio | 13 | bypass |
| `Grain Molder` | notes:note, record_in:audio | out:audio | 14 | bypass |
| `Granular` | record_in:audio | out:audio | 21 | bypass |
| `Looper` | audio:audio | out:audio | 10 | bypass |
| `MPC` | notes:note | out:audio | 177 | bypass |
| `Metallic` | notes:note | out:audio | 15 | bypass |
| `Molder` | record_in:audio | out:audio | 16 | bypass |
| `Oscillator` | notes:note, fm_in:audio | out:audio | 22 | bypass |
| `PaulStretch` | record_in:audio | out:audio | 14 | bypass |
| `Sampler` | notes:note, record_in:audio | out:audio | 14 | bypass |
| `Slicer` | notes:note, record_in:audio | out:audio | 13 | bypass |
| `Spectral Synth` | image:image, note:note | out:audio | 33 | bypass |
| `Wave Terrain` | texture:image, note:note | out:audio | 35 | bypass |
| `Wavetable` | notes:note, fm_in:audio | out:audio | 73 | bypass |

### Utility

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Audio In` | none | out:audio | 4 | hardware: refused headless, bypass |
| `Audio Meter` | audio:audio | out:audio | 0 | bypass |
| `Audio Out` | audio:audio | out:image | 3 | bypass |
| `Blend Audio` | a:audio, b:audio | out:audio | 1 |  |
| `Field Graph` | none | out:image | 7 | bypass |
| `Gain` | audio:audio | out:audio | 1 | bypass |
| `Mixer` | in_1:audio, in_2:audio, in_3:audio, in_4:audio, in_5:audio, in_6:audio, in_7:audio, in_8:audio | out:audio | 49 |  |
| `NDI In` | none | out:image | 1 | hardware: refused headless, bypass |
| `NDI Out` | in:image | out:image | 1 | bypass |
| `OSC Receive` | none | out:modulator | 4 | bypass |
| `OSC Send` | in:modulator | out:image | 5 | bypass |
| `Output` | in:image, audio:audio | out:image | 9 |  |
| `Projection` | in:image | out:image | 135 | bypass |
| `Splitter` | audio:audio | out:audio | 0 | bypass |
| `Syphon In` | none | out:image | 3 | hardware: refused headless, bypass |
| `Syphon Out` | in:image | out:image | 1 | bypass |
<!-- generated:end -->
)AISKILL";
}
