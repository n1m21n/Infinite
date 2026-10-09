# Cluster: image filters (blur, bloom, dither, halftone, glitch, watercolour, LIC, hatching, LUT, blend)

Candidates were NOT cross-checked against the indexopensource.com list (site blocked, 403).
Picked from my own knowledge plus the first-pass scan (docs/plans/index-opensource-scan.md:27 names KinoAqua, LineIntegralConvolutions, HatchingShader).

## Projects read (licence verified from LICENSE on raw.githubusercontent.com)
- Jam3/glsl-fast-gaussian-blur, branch master: LICENSE.md is MIT. Read `13.glsl` (blur13, linear-sampling Gaussian).
- hughsk/glsl-dither, branch master: LICENSE.md is MIT. Read `4x4.glsl`, `8x8.glsl` (ordered Bayer dither).
- keijiro/KinoAqua, branch main: LICENSE is Unlicense (public domain). Only README.md read. The shader path could not be found (about 10 guessed paths all 404, no tree listing without the API), so no source was read. The README says the effect is inspired by flockaroo's Shadertoy `ltyGRV`. Shadertoy shaders default to CC BY-NC-SA unless stated, so the upstream idea may be non-commercial. Reimplement from the technique, not the code, and do not near-port.

## Skipped / not fetched
- GarrettGunnell/Post-Processing (MIT per LICENSE.md): licence OK, but I could not locate the Kuwahara shader path (4 guesses 404). Unread.
- LineIntegralConvolutions, HatchingShader: repo owner/name unknown without the index, so no fetch. Unfetched, not rejected.
- No GPL/AGPL/LGPL source opened.

## What Infinite does today
- Image effects are a `FilterDef` table: `src/core/FilterDefs.cpp` (1096 lines, `GetFilterDefs()` at :63), schema in `src/core/FilterDefs.h`. Optional `prePassBody` gives a separable first pass.
- Blur family (`gaussianblur` :72, `boxblur` :93): fixed 9 taps per pass, with the tap spacing multiplied by `uRadius` (0..10). At radius 10 the 9 taps sit about 10 px apart, so a wide blur is a sparse comb and can band or ghost on detailed input. Uses 2 passes, which is correct.
- `bloom` (:412): single scale, 11 taps per pass spaced by `uRadius` (0.5..12), hard bright-pass `step(uThreshold, lum)` per sample. There is no soft knee and no mip pyramid, so a large halo is impossible and wide settings look stepped. `diffuseglow` (:443) is a 9-tap blur plus screen blend.
- `glitch` (:472): six modes behind a dropdown (Slice Shift, RGB Shift, Scanlines, Blocks, Wave, Datamosh). Stateless, hash-based.
- `halftone` (:840): rotated dot grid, Mono or Colour, with fixed per-channel angle offsets. Dot radius follows `sqrt(1-lum)`, no antialiasing (hard `step`). FieldPixelNode also has a "Halftone Dither" preset (`src/nodes/FieldPixelNode.cpp:403`).
- `lut` (:904): HALD/strip LUT as a second image input. Grep for `.cube`/`hald` in src found only the help text (`src/app/panels/HelpWindows.cpp:351`), so no .cube import. Sampling adds `r/n` with no half-texel inset, so bilinear filtering bleeds across slice borders at r near 0 or 1. The user must also set LUT Size by hand to match the image.
- Blend: `src/core/BlendModes.cpp:8-15` has 32 modes (full Photoshop set plus Erase/Anti-Erase), used by `BlendNode` (`src/nodes/BlendNode.cpp:18-28`). Nothing to learn here.
- Dither: only `RampNode` has a hash-noise dither (`src/nodes/RampNode.cpp:69`), a +/-0.01 decorrelating noise, not an ordered or quantising dither. There is no Dither filter.
- Grep (src, tests, case-insensitive, word match) for kuwahara, hatching, watercolour/watercolor, bilateral, oilpaint, line integral, `LIC` found no image-filter hits. Dither hits are only RampNode, the Field preset, and audio/3D comments. Other absences: median, ASCII, CRT, bokeh/DoF in FilterDefs (grep, zero hits).

## What the peers do differently
- Jam3 `blur13`: uses bilinear hardware filtering to fold pairs of Gaussian taps into one fetch at fractional offsets (1.4118, 3.2941, 5.1765 px) with weights 0.1965/0.2969/0.0945/0.0104. 7 fetches give the quality of a 13-tap kernel, and the radius is in real pixels. Infinite instead scales one fixed 9-tap spacing by `uRadius`.
- hughsk `dither4x4`/`dither8x8`: ordered Bayer threshold on luma, `brightness < limit ? 0 : 1`, driven by pixel coordinates (`position`). It is an unrolled lookup table; the same matrix can be generated arithmetically in GLSL 150 with bit tricks, so no array is needed. Infinite has the screen-door look only through the Field preset.
- KinoAqua (README only): a post effect on the flockaroo idea (edge-following strokes). Not read; no claim about its internals.

## Improvements: existing nodes first
1. **Blur/Bloom quality (gaussianblur, boxblur, bloom, diffuseglow)** | Benefit: a smooth wide blur and a real wide bloom halo instead of a banded comb at high radius. Method: either (a) use linear-sampling taps as in blur13, keeping the horizontal prePass plus vertical pass shape, and add a pixel-accurate sigma; or (b) for large radii, add a downsample chain. (b) cannot be one `FilterDef` since `prePassBody` is one same-size intermediate (`FilterDefs.h:73`); it needs a class or a FilterNode extension. Add a soft-knee threshold to bloom. Skill: new-effect-node (option a is a pure table edit; option b is the class path, section 4b). Effort: S for (a), M-L for (b). Licence: algorithm (linear-sampling weights are a standard derivation); recompute the weights rather than copy the table. If constants are copied, keep the Jam3 MIT notice.
2. **LUT**: add inset sampling (half-texel) and cross-slice clamp, auto-detect N from the image size (N = cuberoot(width) for a HALD, height for a strip), and consider a `.cube` loader in the image source path. Benefit: correct grades from real-world LUT files, no manual size. Skill: new-effect-node plus new-source-node if a loader is added. Effort: S for the sampling fix and auto-size, M for .cube (it needs a texture built from a text file, which a FilterDef cannot do alone). Licence: .cube is a plain text format, nothing to attribute.
3. **Halftone**: antialias the dot edge (`smoothstep` over `fwidth`-style texel size) and add a CMYK mode with the standard 15/75/0/45 degree screen angles. Benefit: print-like output without aliasing. Effort: S, a table edit. Licence: none needed.
4. **Glitch**: nothing structural to learn from the peers I read; the mode list is already wide. Possible small upgrades (blockwise displacement driven by smooth noise rather than hash) are optional. Effort: S.

## Improvements: new nodes
5. **Dither (Effects)**: Bayer 2/4/8 ordered, plus blue-noise or interleaved-gradient-noise, with a palette-level count (quantise to N levels per channel) and a mono/colour switch. Benefit: stylised 1-bit/low-colour looks and banding removal on gradients. FilterDef table entry, one pass, no second input. The Bayer matrix is generated in the shader, so there is no 64-case chain. Skill: new-effect-node. Effort: S. Licence: Bayer is a public mathematical construction; hughsk (MIT) read for reference only, no code copied.
6. **Kuwahara / oil-paint (Effects)**: anisotropic Kuwahara (structure tensor, then sector means) gives painterly flattening. Benefit: the most requested painterly node and a base for watercolour. Needs a structure-tensor pass and a filter pass, so it is a multi-pass class or a prePass trick (tensor in prePass, Kuwahara in main reading `uPass`; the prePass reads `uSrc` so it can output the tensor into RGBA16F, which may fit a FilterDef). Skill: new-effect-node. Effort: M. Licence: implement from the Kyprianidis et al. papers, not from any repo (I did not read one).
7. **Watercolour (Effects)**: edge-following strokes plus pigment pooling after Kuwahara. Reimplement from the technique; KinoAqua is Unlicense, but its inspiration (flockaroo) has an unclear licence, so write it independently. Effort: M-L. Depends on 6.
8. **LIC / hatching**: Line Integral Convolution along a structure-tensor or luminance-gradient flow is the same machinery as item 6 (the tensor field) plus a fixed-step integration loop (about 16-32 samples each side); it does not fit a single table pass cheaply. Hatching = tone-mapped line layers rotated by the flow field. Peers unread (repo names unknown here). Effort: M each once the tensor pass exists. Licence: implement from the papers (Cabral and Leedom 1993).

## Nothing to learn
- Blend modes: Infinite already covers the full set plus erase modes.
- Glitch: broad already.

## Open questions
- Whether a `FilterDef` can express the Kuwahara two-stage (prePass output format and `uPass` filtering) needs a read of `src/nodes/FilterNode.cpp` prePass setup (:57-60) before committing to the table route; I did not read the FBO format code.
- I did not verify GLSL 150 compile-ability of the generated Bayer matrix; it is plain integer/bit math, so it should work, but it is not tested.

## Candidates
Dither node (Bayer + blue noise) | banding fix and stylised low-colour looks, pure FilterDef | S | none needed (hughsk MIT read only)
Blur/bloom linear-sample taps and soft-knee bloom | clean wide blur/halo, fixes sparse-comb banding | S (taps) / M-L (mip bloom) | MIT (Jam3) if constants copied
LUT hardening (half-texel, auto-size, .cube loader) | correct grading from real LUT files | S-M | none
Kuwahara then watercolour/LIC/hatching family | painterly node set sharing one tensor pass | M-L | write from papers; avoid flockaroo-derived code
