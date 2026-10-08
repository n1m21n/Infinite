# Field Pixel: image offset-read + camera-FX presets

Status: implemented and verified on macOS (2026-10-08). Origin: cables.gl vetting; owner chose "FX as Field presets, not nodes".
Skills to load before building: `field-compiler`, `field-language`, `field-pixel-presets`, `field-testing`.

## Problem

Field Pixel can read its input image **only at the current pixel**. `src` is a single
fixed fetch, `vec4 src = texture(fld_srcTex, vUv);` (`src/core/field/GlslBackend.cpp:683`),
and a declared `input pixel image` name just aliases it (`GlslBackend.cpp:208-214`).
Any effect that moves, warps or offsets the picture can't be written as a preset
(`field-pixel-presets` §7).

## Fix: `img(coord)` for images, mirroring `A(coord)` for state

State cells already have an offset read (build step 22):

| Stage | State today | Image, new |
|---|---|---|
| IR lowering | `FieldIR.cpp:597-640`: call whose callee is a pixel `state` → `IRKind::StateRead`, `isOffsetRead`, 1 vec2 arg | Same check for a callee that is a declared `image` input (`FieldIR.cpp:1679-1694` / `3453-3468` register it) → new `IRKind::ImageRead` (or reuse with a flag), type vec4 |
| GLSL emit | `GlslBackend.cpp:136-175`: `texture(fld_s_bank0, clamp(coord, half, 1-half))` with Clamp/Wrap/Border | `texture(fld_srcTex, <boundary>(coord))`; default Clamp, reuse the same boundary helper |
| Bare name | `A` = `A(uv)` | `img` stays the current-pixel `src` (no change, no perf cost) |

Rules:
- Pixel domain only. Same error wording style as the state case ("an offset read of image 'img' needs a vec2 coordinate").
- **Only declared image inputs.** `col` stays the output and stays non-callable.
- Count image reads in `ctx.offsetReadCount` (or a sibling counter) so the perf HUD sees them.
- Texture unit stays 1 (`FieldIR.cpp:1086`); no new uniforms.
- The texture's own sampler state decides filtering. Confirm `fld_srcTex` is bound `GL_LINEAR`, or zoom blur will band.

Estimated size: ~40 lines across `FieldIR.cpp` and `GlslBackend.cpp`, plus corpus cases.

### Tests (`field-testing`)
- Corpus: `img(uv)` must equal bare `img` (pixel-exact); `img(uv + vec2(1/res.x, 0))` shifts one texel; wrong arity / non-vec2 / element domain / calling `col` → errors.
- Existing state offset-read cases unchanged.

## Presets (after the fix)

All follow `field-pixel-presets` §2 (aspect-correct `p` for any radial/angle maths), `#` comments, `atan2`.

| Preset | Needs offset read? | Core idea | Params |
|---|---|---|---|
| Levels | no | `pow(clamp((c - inBlack)/(inWhite - inBlack)), 1/gamma)` remapped to out range | in black, in white, gamma, out black, out white |
| Scanlines / Interlace | no | multiply by `0.5+0.5*cos(uv.y*res.y*π/lines)`; optional odd-line roll with `t` | lines, depth, roll speed |
| Lens Dirt + Flare | no | procedural smudge noise × luminance-weighted add; flare = ghost discs along the line from `flarePos` through centre | dirt amount, flare x/y, flare strength, ghosts |
| Polar Coords | **yes** | `img(vec2(atan2(p.y,p.x)/2π+0.5, length(p)*2))` plus inverse mode | mode (to/from polar), twist |
| Chromatic Aberration | **yes** | `r = img(c + dir*amt).r`, `g = img(c).g`, `b = img(c - dir*amt).b`, `dir` radial from centre | amount, radial/linear |
| Zoom Blur | **yes** | 12-16 taps along `(uv - centre) * k`, weighted average | amount, centre x/y, taps (fixed in text) |

Dropped from this batch: **Depth of field, Fog.** Both need a depth pin, which Field Pixel doesn't have. Revisit if Render 3D exposes depth.

The Effects category already has `radialblur`. Ship Zoom Blur anyway because it's editable as text. Note the overlap in the preset's description.

## Exit criteria
- Corpus green, all six presets load with no compile banner at a non-square size (e.g. 1920×1080).
- Each preset's params are modulatable (they are `param` uniforms, so yes by construction).
- Help text: add the `img(coord)` spelling to the Field language help tab and to `field-language` SKILL wrong/right table.
- Update `field-pixel-presets` §7: offset image reads now exist; keep the note that DoF/fog need depth.

## Implementation notes

- `IRKind::ImageRead` resolves only declared image inputs, with pixel-domain,
  one-argument, and vec2 checks. Both input-symbol registration paths mark
  image inputs; outputs and ordinary vec4 locals remain non-callable.
- GLSL uses the existing source sampler on unit 1. The shared boundary
  helper clamps image reads using `textureSize(fld_srcTex, 0)`; state cells
  retain their own Clamp/Wrap/Border behavior and output resolution.
- `offsetReadCount` includes image reads. `usesOffsetReads` keeps its state-only
  precision-selection meaning so camera FX do not allocate 32F state banks.
- Six presets use existing `param` controls and persistence. Coordinate effects
  carry sampled alpha. Levels guards crossed/equal input endpoints. Zoom Blur
  lists 16 weighted taps (weight total 12) to avoid existing pixel loop and
  mutable-scalar hoisting limitations. Flare uses eight fixed ghosts.
- Language help, both exported AI references, and the two Field skills describe
  the new spelling. Depth of field/fog remain out of scope without a depth pin.
- The scalar corpus stays unchanged. `tests/field/image-offset-corpus.txt` adds
  12 language cases to `FIELDPIXELTEST`. GPU checks cover identity, texel shift,
  interpolation, clamping at differing sizes, disconnected input, alpha, and
  1080p renders at default/min/max params. Zoom Blur has an analytic assertion.

| Concern lens | Scope | Handling |
|---|---|---|
| Structure | Touched: 1a, 1c | Semantic IR kind; existing image pin and preset registry |
| Execution | Primary: 2d | GLSL sampling; existing transport and source pull |
| Data/state | Touched: 3a, 3b, 3c | Existing ParamTable, stable IDs, VisitParams and pin persistence |
| UI/UX | Touched: 4f | Help and preset names; existing automatic controls |
| Platform | Clear | No native APIs or shortcuts; GLSL remains 150 |
| Performance | Touched: 6d | Fixed taps, fetch count, unchanged state precision |
| Correctness | Primary: 7a, 7b, 7c, 7d | GPU readback, refusals, endpoint guards and Field gates |

The invariant audit checked both declaration paths, aliasing, source-unit
binding, source/output dimensions, tap bounds, state allocation, and existing
state offset-read cases. Bare image reads still emit `src`. The generic Field
grid registers all declared params through `ModSlider` using the stable
`kFieldDeclaredParamBase + p.id` key; no new registration path is needed.

Emitter errors are surfaced by FieldPixelNode::Apply before shader compilation,
so unsupported multiple-image kernels retain the previous program and show the
compiler refusal rather than a generic driver link error.

## Verification (2026-10-08)

- Clean macOS build passed (VST3 disabled for this isolated Field checkout).
- FIELDPIXELTEST: all 30 assertions passed; 12 image-read corpus cases; all
  six camera presets rendered finite pixels at 1920x1080 at defaults and
  parameter endpoints. Sampling and weighted Zoom Blur match GPU references.
- Fast hygiene: 46 checks passed; its sole FIELDPIXELTEST failure exposed
  the swallowed emitter error. After that fix, the final pixel suite passed.
  Unaffected successful checks were retained instead of rerun.
- Compositing hygiene: 16 passed, zero failures or expected failures.
- Original compiler rejected img(uv); final compiler accepts it.
- Parameter audit confirmed the existing Field ModSlider registration path.
- git diff --check passed. Windows/Linux execution was not available here.
