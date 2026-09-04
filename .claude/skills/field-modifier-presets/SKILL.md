---
name: field-modifier-presets
description: How to write a new Field Modifier preset (FieldElementNode::Presets(), src/nodes/FieldElementNode.cpp) - reserved element-domain names (P/N/Cd), the self-mutation ordering bug, declared outputs (output frame float) vs the legacy publish variable, and what makes a modifier preset actually look good on a mesh. Use BEFORE adding, editing, or debugging any Field Modifier preset string, and when a preset does nothing, mutates wrong, or a declared/publish output reads a frozen value.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

Field Modifier (`FieldElementNode`, display name "Field Modifier") runs its
kernel once per mesh vertex/element through the bytecode VM
(`ElementBackend.cpp`). This skill is the concrete, load-bearing knowledge
from fixing real broken presets in `FieldElementNode::Presets()` - read this
before writing preset text, not the abstract `field-language` design docs.

---

## 1. Reserved names available in every modifier preset

Confirmed from `ElementBackend.cpp`/`FieldIR.cpp`'s reserved-name checks -
these cannot be redeclared and read/write specific element storage:

| Name | Type | Read/write | Meaning |
|---|---|---|---|
| `P` | vec3 | read+write | vertex position - write it to displace the mesh |
| `N` | vec3 | read+write | vertex normal |
| `Cd` | vec3 | read+write | vertex color |
| `uv` | vec2 | read | vertex UV, if the source mesh has one |
| `i` | int | read | element index |
| `count` | int | read | total element count |
| `t` | float | read | time in seconds (frame-domain) |
| `dt` | float | read | delta time (frame-domain) |
| `frame` | float | read | frame counter (frame-domain) |

Anything else (`a`, `disp`, `hit`, `factor`...) is a plain local, no
declaration needed - exactly like the pixel domain. `param float name =
default [lo, hi]` declares a slider; `attrib float name = init` declares a
per-element persistent value (see "Attrib Ramp" for the pattern: declare,
then assign, then read back in the same kernel). House style in this file:
statements end with a bare newline, no `;` (unlike Field Pixel's presets -
the grammar accepts either, this file's presets just don't use `;`).

## 2. The self-mutation ordering bug - the exact bug "Twist Modifier" had

Writing to a component of `P` and then reading `P`'s *other* components on
the next line reads back what you just wrote, not the original vertex
position - because `P.x = ...` mutates `P` in place immediately, not at the
end of the kernel. The real, shipped bug this skill exists to prevent:

```
# WRONG - P.x is already the NEW value by the time P.z's line reads it
a = P.y * twist
P.x = P.x * cos(a) - P.z * sin(a)
P.z = P.x * sin(a) + P.z * cos(a)   # reads the mutated P.x - wrong twist
```

```
# RIGHT - snapshot every component you need before writing any of them
a = P.y * twist
px = P.x
pz = P.z
P.x = px * cos(a) - pz * sin(a)
P.z = px * sin(a) + pz * cos(a)
```

This applies to any in-place rotation/shear/swizzle-mix on `P`, `N`, or
`Cd` that reads more than one component of the same variable it's writing.
Snapshot first, always, whenever a formula for one output component
mentions another component of the same vector you're about to overwrite.

## 3. A modifier only recomputes when something it depends on actually changes

`CookIfNeeded()` gates the rebuild on upstream revision, `t`-dependence, and
a couple of other signals - it does **not** unconditionally recompute every
frame. A pure `param` slider drag is tracked too (fixed as part of this
project's cook-gate bug pass), so today a plain param-driven preset (no `t`,
no `state`) responds live to slider moves. If you add a *new* signal source
a preset should react to (something other than a `param` value or `t`),
check `CookIfNeeded()`'s condition list before assuming it "just works" -
this exact class of bug (moved the slider, nothing happened) has shipped
before.

## 4. Two different output mechanisms - know which one you're using

- **Declared output** (`output frame float name = <frame-domain expr>`) -
  the correct, modern mechanism. Wires straight into the node's dynamic pin
  system (`PinTable`) and reads a real live value via
  `ElementVM::ReadFrameVar`. The expression must be frame-domain (built only
  from `t`, `param`s, and coarsening reductions like `reduce.max(x)` on an
  element-domain value) - **not** raw `P`/`N`/`Cd`, which are element-domain
  and can't be joined into a frame-domain declaration. `reduce.max(hit)` in
  "Boundary Chime Sensor" is the pattern for exposing an element-domain
  condition as a single frame-domain number.
- **`publish`** (bare `publish = <frame-domain expr>`, no `output` keyword,
  toggled on via the "publish scalar output" checkbox) - an older, separate
  mechanism (`PublishOutput::Value01()` hardcodes the name `"publish"`).
  **It reads a frozen 0 unless the preset itself contains a `publish =`
  assignment** - the toggle does not synthesize one. Every preset that wants
  the publish pin to work must assign `publish` explicitly, same
  frame-domain-only rule as declared outputs. If you're adding a new
  preset, prefer a declared output over `publish` unless there's a specific
  reason to keep the legacy pin working too (as of this pass, every built-in
  preset assigns both, for compatibility with users who have the toggle on).

## 5. What actually makes a modifier preset look good

The two presets users called "beautiful" both combine **displacement +
color** driven by the same underlying signal, not just one or the other:

```
disp = sin(P.x * wave + t * speed) * amp * 0.25
P.y += disp
Cd = vec3(0.5 + 0.5 * sin(P.x * 2.0 + t), 0.3, 0.9)
```

A preset that only moves `P` (no `Cd` change) reads as a flat, hard-to-read
wireframe wiggle unless the mesh/material already has strong shading. A
preset that only changes `Cd` (no `P` change) reads as a static texture, not
a "modifier". Tie both to the same phase/frequency terms so the color
visibly correlates with the motion (a bulge glows brighter, a ripple's crest
shifts hue) rather than scrolling independently - that correlation is what
reads as "designed" instead of "random".

Other things that consistently help:
- Fold `t` into the driving signal somewhere (even a slow `sin(t)`) so the
  preset is visibly alive at its default params, not inert until a slider
  moves.
- Keep displacement amplitude params bounded well inside a range that
  won't turn the mesh inside-out or fully degenerate it at the param's max
  (check the `[lo, hi]` bounds against what the base mesh's scale actually
  tolerates).
- `N` still matters for lighting after a `P` displacement - if the preset
  does a large displacement and doesn't touch `N`, the shading will look
  wrong (facets keep their pre-displacement normals). Cheap fix for small
  bulges: renormalize `N` toward the displacement direction; for anything
  beyond a gentle bulge, recomputing `N` properly needs neighbor
  information this per-element kernel doesn't have - flag that limitation
  rather than silently shipping visibly wrong shading.

## 6. Before shipping a new preset

1. Any in-place `P`/`N`/`Cd` rotation/shear - snapshot components first
   (§2)?
2. Does it change both geometry and color, tied to the same signal (§5)?
3. If it declares an output or assigns `publish`, is the expression provably
   frame-domain (§4) - built only from `t`/`param`s/`reduce.*`, never raw
   `P`/`N`/`Cd`?
4. Build (`cmake --build build -j 8`) checks the C++ string compiles, not
   that the Field kernel text itself is valid - that's checked at
   preset-load time inside the running app. Say so explicitly and ask for a
   one-time visual check rather than claiming the kernel itself is verified.
