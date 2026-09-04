---
name: field-pixel-presets
description: How to write a new Field Pixel preset (FieldPixelNode::Presets(), src/nodes/FieldPixelNode.cpp) that actually looks right the first time - reserved names, aspect correction, the metaball/SDF falloff trap, and the shapes that are and aren't buildable in this backend. Use BEFORE adding, editing, or debugging any Field Pixel preset string, and when a preset looks stretched, flat, or "just a circle/blob that won't blend".
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

Field Pixel compiles its kernel text to GLSL 150 via `GlslBackend.cpp` and
runs it once per pixel through `GLUtil::CompileProgram`. This skill is the
concrete, load-bearing knowledge accumulated fixing real broken presets in
`FieldPixelNode::Presets()` - not the abstract language design (see
`field-language`/`field-compiler` for that). Read this before writing preset
text; it will save you a stretched-image or dead-blob bug report.

---

## 1. Reserved names available in every pixel preset

Confirmed from `GlslBackend.cpp`'s `EmitNode` (the only source of truth - it
lists exactly which bare names get special treatment instead of becoming a
`fld_v_<name>` local):

| Name | Type | Meaning |
|---|---|---|
| `uv` | vec2 | 0..1 across the output, **not** aspect-corrected |
| `xy` | vec2 | pixel-space coordinate variant |
| `res` | vec2 | output resolution in pixels (`fld_res` uniform) |
| `aspect` | float | `fld_res.x / fld_res.y` - **use this, see §2** |
| `t` | float | time in seconds |
| `dt` | float | delta time |
| `frame` | float | frame counter |
| `col` | vec3 | **the output** - assign it, don't declare it |
| `alpha` | float | output alpha, optional |

Anything else you write (`p`, `d`, `fld`, `b1`...) is just a local - no
`attrib`/`state` keyword needed, exactly like element-domain locals.
`param float name = default [lo, hi];` declares a uniform slider. Pixel
presets in this codebase end statements with `;` (house style; the grammar
also accepts a bare newline, but match the existing presets).

## 2. Aspect correction is not optional - do it on every preset with a spatial pattern

`uv` is `[0,1]×[0,1]` regardless of the node's width/height sliders. Any
preset that computes distance, angle, or a repeating grid from raw `uv` will
visibly stretch into an ellipse/rectangle the moment width != height (the
FieldPixelNode default is 1024×1024, but users change it - a Chladni or
metaball preset at 2015×4096 stretched badly before this was fixed). The
Formula/GLSL node never has this bug because it's expected to handle its own
aspect; Field Pixel presets must do it themselves, every time:

```glsl
p = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);   // centered, aspect-correct
```

or, for a pattern that doesn't need centering (e.g. a grid or noise field):

```glsl
p = vec2(uv.x * aspect, uv.y) * scale;
```

**Checklist for a new preset:** does it call `length(...)`, compute an angle
with `atan`, tile with `fmod`/`mod`, or otherwise treat x and y as
interchangeable spatial units? If yes, it needs one of the two lines above
before that math - not raw `uv`. If it only reads `uv.x` and `uv.y`
independently as a pure gradient (e.g. "Default UV Gradient", "Color
Gradient"), it doesn't need correction.

## 3. The metaball / SDF falloff trap - `1/dist` does not blend

The single most common way a "blobby merge" preset instead renders as
several separate hard circles: using a `1/(dist + eps)` falloff and summing
it. `1/dist` decays so fast that each blob's field is only large very close
to its own center - by the time you're far enough out to be near a
*neighboring* blob's circle too, your own blob's contribution has already
collapsed near zero. The sum barely exceeds the single-blob case anywhere,
so the iso-surface threshold is crossed at almost the same radius around
each blob independently, in isolation. It compiles fine and "looks like
something" (a circle orbiting), which is exactly why this bug survives
review - it's not a compile error, it's a wrong shape.

**Use the inverse-square metaball energy field instead** (the standard
technique - each blob contributes `radius² / distance²`, and the sum crosses
the iso-threshold of `1.0` exactly where two influence circles overlap and
merge):

```glsl
param float size = 0.16 [0.05, 0.3];   // blob radius, in the same units as p

r2 = size * size;
d1 = dot(p - b1, p - b1) + 0.0008;     // squared distance, not length()
d2 = dot(p - b2, p - b2) + 0.0008;
fld = r2 / d1 + r2 / d2;                // + more blobs as needed
iso = smoothstep(0.85, 1.15, fld);      // threshold sits at fld == 1
col = vec3(iso * ..., iso * ..., iso * ...);
```

Two more things that quietly kill blending even with the right falloff:

- **Orbit radius vs. blob radius.** If the blobs' orbit paths keep their
  centers farther apart than `2 * size` for most of the cycle, they simply
  never overlap - you'll see near-merges only briefly. Pick orbit radii in
  the same order of magnitude as `size`, not 1.5-2x larger.
- **`length()` vs `dot()`.** `length(p - b)` is `sqrt(dot(...))` - an extra
  transcendental per blob per pixel for no benefit, since the inverse-square
  formula wants squared distance anyway. Use `dot(p - b, p - b)` directly.

## 4. `if`/`else` is predicated, not branched - both sides always run

Per `field-compiler` §6.2: GLSL 150 fragment predication means `if(a, b, c)`
(or an `if`/`else` statement) evaluates **both** branches every pixel and
selects with `mix()`. Don't rely on a branch to skip an expensive or
divide-by-zero-prone computation - guard the value itself (e.g. `+ 0.0008`
in the denominators above), not the control flow.

## 5. What's realistically buildable here vs. not

Field Pixel is a per-pixel, stateless-within-a-frame shader (any `state`
becomes a ping-pong texture pair - expensive and only worth it for real
feedback effects like trails/reaction-diffusion). Good fits: SDF shapes,
noise fields, domain-warped patterns, metaballs (with §3's formula), radial/
grid patterns, palette/gradient mapping, cellular/voronoi-style patterns
(`floor`/`fract` tiling), audio-reactive color fields (via a `param` wired
to a modulator). Poor fits: anything needing per-pixel history from a
*previous, different* pixel's neighborhood over many frames (blur/diffusion
that needs many taps - possible but costs a real ping-pong texture and
should be scoped deliberately, not bolted onto an otherwise-simple preset).

## 6. Before shipping a new preset

1. Does every spatial-distance/angle computation use an aspect-corrected `p`
   (§2), not raw `uv`?
2. If it's a blend/merge/metaball effect, does it use §3's inverse-square
   field, not a bare `1/dist`?
3. Read it once for a divide-by-zero or `1/x` at `x=0` on the predicated-`if`
   path (§4) - add a small epsilon to any denominator that can hit zero.
4. Build (`cmake --build build -j 8`) - this only checks the C++ string
   compiles, **not** that the GLSL inside it is valid. The Field pixel
   compiler runs at preset-load time inside the app; there is no headless
   CLI to check GLSL validity ahead of time (confirmed - no such tool
   exists in this repo as of this writing). Say so explicitly when handing
   off a new preset, and ask the user to load it once and check for a
   compile-error banner - don't claim "verified" for the GLSL body itself.
