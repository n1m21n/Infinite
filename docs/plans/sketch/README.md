# Sketch: a JavaScript drawing source node

Status: built on branches `feature/sketch-node` and `feature/svg-node` (worktree `~/infinte-sketch`), not merged (2026-10-10). Open: cross-OS pixel hashes (needs Windows + the Linux container), optional image input pin (S6), raw `ctx` (S4), Flow field preset ~6 ms at 1080p. SVG import (lunasvg) shipped in the same branch: drop/paste an SVG, `svgDraw/svgSet/svgText/svgBox`. Owner: "seems simple, let's do this."
Skills to load before building: `new-source-node`, `node-ui-pillars`, `field-integration` (code-editor + keep-last-working patterns), `windows-parity`, `linux-parity`, `data-accuracy-sweep`.

## What it is

A Source node whose body is a code editor (like Field Pixel). The user writes p5.js / Canvas-2D style JavaScript; the node outputs an image.

```js
param("count", 12, 1, 64);        // becomes a modulatable knob
param("spin", 0.2, 0, 2);

function draw(t) {                // t = transport seconds
  background(0.05);
  translate(width / 2, height / 2);
  for (let i = 0; i < count; i++) {
    rotate(TAU / count + spin * t);
    fill(hsl(i / count, 0.7, 0.6));
    circle(200 + 80 * sin(t + i), 0, 30);
  }
}
```

Why it isn't Field: Field runs one kernel per pixel or element with no loops over other things. Sketch is imperative: loops, arrays, objects, recursion (L-systems, particle lists, layouts, type on a path).

## Decisions

| # | Question | Decision | Why |
|---|---|---|---|
| S1 | JS engine | **quickjs-ng** (MIT, ~1 MB) | The original QuickJS doesn't build with MSVC; the -ng fork does, with CMake. V8 is about 30 MB and a large build |
| S2 | Rasteriser | **plutovg** (MIT, C): CPU, Canvas-2D semantics (paths, even-odd/non-zero fill, strokes with joins/caps/dashes, linear/radial gradients, transforms, clipping) | Same pixels on all three OSes, which export determinism needs. Infinite has no general vector rasteriser today (`DrawNode` stamps brushes, `TextNode` uses `Platform::RasterizeText`) |
| S3 | Upload | Raster to RGBA buffer → `glTexSubImage2D` into an FBO-sized texture; bump `NextTextureRevision()` only when `draw` actually ran | Fits the existing cook/cache model |
| S4 | API surface | p5-flavoured globals (`background, fill, stroke, strokeWeight, noFill, rect, circle, ellipse, line, beginShape/vertex/endShape, bezier, push/pop, translate/rotate/scale, text, textSize, image(img,…)`) plus `ctx` for the raw Canvas-2D subset | p5 is the most-known creative-coding API; raw `ctx` serves people who know Canvas |
| S5 | Params | `param(name, default, lo, hi)` declares a knob that is a real `ParamRef` (modulatable, saved). Re-declared each compile; values survive recompiles by name | Same contract as Field's `param` |
| S6 | Inputs | Optional image pin (exposed as `img` for `image(img, x, y, w, h)`; read back from GL once per cook, only when used) | Composite over video, trace an image |
| S7 | Time & randomness | `t`, `beat`, `frame`, `width`, `height` globals. `random()` seeded per node + frame so export is reproducible | Export quality is never degraded (`project-perf-initiative`) |
| S8 | Safety | No `quickjs-libc`: no files, no network, no `eval` of external code. `JS_SetMemoryLimit` (64 MB) + interrupt handler (abort a `draw` > 50 ms) | Shared patches must not be able to touch the user's machine; runaway loops must not freeze the canvas |
| S9 | Errors | Compile/runtime error shows a banner with line:col; node keeps outputting the **last working** frame | Field's keep-last-working rule |
| S10 | Thread | Runs on the main/render thread inside `CookIfNeeded`, only when params, inputs or time changed. Never the audio thread | Same as other source nodes |
| S11 | Text | Fonts from `external/fonts` through plutovg's font loader | Avoids per-OS text paths diverging |
| S12 | Presets | Ship 6-8 sketches: radial burst, flow-field lines, L-system tree, kinetic type, grid poster, particle list with `state` | Every code node ships with something that looks good first try |

## Out of scope (later)
- `svg` *output* (SVG import and animation is built, see Status).
- GPU raster (only if profiling shows CPU raster > 4 ms at 1080p for typical sketches).
- HTML/CSS: that's the CEF Web pack (`docs/plans/extensions/README.md`).

## Wiring checklist (from `new-source-node` / `codebase-navigation`)
- CMake: vendor `external/quickjs-ng`, `external/plutovg`, all 3 platforms + the Linux container build.
- Node registration, save/load (`code` Text param + params), help tables, `ROUNDTRIPTEST`.
- `docs/node_param_audit.md` regen; patch-authoring facts regen (`tools/gen-patch-skill.py`).

## Exit criteria
- All presets render identically (pixel hash) on macOS, Windows and Linux.
- Modulated params move the drawing in real time; save/load round-trips code and values.
- Runaway `while(true){}` is aborted with a banner; app stays responsive.
- 1080p preset sketches cook in < 4 ms on the perf baseline machine.
