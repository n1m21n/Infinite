---
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
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5)
b1 = vec2(0.28 * cos(t * speed), 0.28 * sin(t * speed * 0.8))
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
