# Visual system: art as stacked shapes (internal)

Replaces "pick nodes from a pool" (art.md) with a model that explains why a style looks the way it does. Owner's premise: all of art is shapes, in different sizes, layers, compositing and arrangement, plus text and colour; 3D is the same again with depth. Every art style and movement is a visual direction expressed as choices on those axes. The node system exists to make each axis a thing you can wire.

## 1. The model

A picture = **a stack of layers**. A layer = one *mark generator* (a shape, a field, text, a photo) made many times, placed by an *arrangement*, coloured from a *palette*, then merged by a *compositing rule*.

| Axis | Question a style answers | Parameters (sampled per picture) |
|---|---|---|
| Shapes | what are the marks, how many kinds, how complex | primitive set, edge quality (hard, soft, stroked), count of kinds, detail level |
| Multiplicity | how many of each | 1 (hero), 3-7 (cluster), 20-200 (field), 1000+ (texture) |
| Size | the scale ladder | hero : mid : small ratio (golden 1 : 0.62 : 0.38 or extreme 1 : 0.1), one hero per picture, cropping/bleed |
| Arrangement | where marks sit | grid, radial, spiral, thirds, golden, axis/mirror symmetry, diagonal, scatter, band stack, margins, alignment strictness |
| Layering | depth order and count | layer count (2-9), roles (ground, field, hero, accent, overlay), overlap amount, opacity per layer |
| Text | is there type, what role | none / label / headline / texture; size ladder; case; position rule; kind of face (grotesk, mono, display, pixel, script) |
| Compositing | how layers merge | blend mode, mask, key, feedback/trails, displacement, grain, finishing (bloom, halftone, dither) |
| Colour | palette rule | scheme (mono, analogous, complementary, triad, split), count (1-bit, 2-4 inks, 5-8, free), value structure (key: high/low, contrast), saturation, accent share (60-30-10) |
| 3D mirror | the same, with depth | geometry set, instancing rule, camera (ortho/persp, angle), light (key/rim/flat), material (matte, glass, chrome, wire), fog/depth |
| Motion | what moves and how | which axis animates (position, scale, hue, mask), speed, easing, loop |

Rule of the model: a **style is a distribution, not a template**. Each cell is a weighted choice or a range; two pictures of one style differ in the draw, not in the recipe.

## 2. Node categories and what they give each axis

Counts are from the node facts (`load_facts`). "Mirror" = the same job in 3D.

| Category (n) | Axis it serves | Key nodes | Notes for generation |
|---|---|---|---|
| Source (98) | Shapes, Text, field marks | Rectangle, Circle, Ellipse, Triangle, Polygon, Star, Ring, Hexagon, Line, Cross, Arrow, Chevron, Crescent, Heart, Gear, Pie, Teardrop, Superellipse, Blob, Rounded Rect, Shape; Text; Noise, Ramp; Sketch (grid, lissa, orbit, rose, tree, ~17 presets); FieldPixel (plasma, rings, spiral, tunnel, grid, ~35 presets); Shadertoy, Formula, Draw | Round shapes keep one size on both axes. `posX/posY/rotation/strokeWidth/feather` are the arrangement and edge controls. One node = one mark; multiplicity needs Sketch grid/orbit/lissa, FieldPixel grid, Mirror Tile, or Layer Stack |
| Effects (28) | Edge quality, multiplicity by repetition, symmetry | mirror tile, symmetry, kaleidoscope (radial arrangement), halftone, pixelate, posterize-like, edge outline/sobel, emboss, twirl, ripple, liquify, displace, glitch, bloom, diffuseglow, vignette, motionblur, radialblur | symmetry and mirror tile are arrangement tools; halftone/pixelate are print and screen finishes |
| Compositing (41) | Layering, compositing, colour | Blend (all modes), Layer Stack, Transform, Fit, opacity, luma key, chroma key, alpha from luma, threshold, posterize, dither, gradientmap, Color Ramp, Audio Color Ramp, lookup, lut, color adjustments, exposure, dropshadow, outerglow, Feedback, Trails, Reaction Diffusion, Fluid, Time Machine | Blend + keys are the layer-merge vocabulary; gradientmap/lut/dither are the palette enforcers (a 4-colour look is gradientmap + posterize/dither) |
| 3D (127) | 3D mirror: shapes, multiplicity, arrangement | primitives (Cube, Sphere, Torus, Cone, Cylinder, Icosphere, Prism, Pyramid, Torus Knot, Supershape, Klein, Mobius, Rounded Cube, Platonic solids, Capsule), Text 3D, Curve/Sweep/Revolve/Screw/Extrude/Tube/Helix; Array, Instance on Points, Distribute Points (grid, faces), Voronoi, Delaunay, Metaballs, L-System, Particle System, Ocean; Material, Light, HDRI, Camera, Render 3D, Wireframe, Mirror, Twist, Displacement, Subdivide, Bevel | Array/Instance on Points/Distribute give multiplicity; Camera + Light + Material give the style (ortho + flat light = isometric; chrome + HDRI = Y2K; Wireframe + black = vector wireframe). Render 3D output is a layer like any other |
| Modulators (33) | Motion, palette drift | LFO, Random, Envelope, MSEG, Pattern, Path, Palette, Image Analyze, Audio Analyze, Audio to CV, Math, Smoothing | `Palette` and Color Ramp modulation animate colour; Path animates arrangement; Audio Analyze ties picture to sound (AV samples) |
| Prediction (8) | Motion | Drift, Moves, Predictive Coloring | optional, for organic colour/position drift |
| Utility (18) | Output | Output, Projection | canvas size and projection (9:16) |
| Macros, Notes, Synths, AudioEffects | not visual | | sound side |

Gaps to confirm before building (do not assume): a node that scatters N copies of one 2D shape with seeded size and position jitter (today multiplicity in 2D comes from Sketch/FieldPixel/Mirror Tile, not from placing a Circle many times); a text-on-grid/text-as-texture mode; palette extraction from a style table straight into gradientmap stops. Each gap has a scripted answer: a Sketch or Field program generates the multiplicity and the arrangement, and the node stack colours and merges it.

## 3. Style card (what every style must fill)

One card per style; every cell is a range or weighted list, never a single value. Fully worked examples in section 4; the table in section 5 holds the compact form of all styles.

```
id, name, family, era/lineage, refs
shapes:        primitive weights, edge quality, kinds count, detail
multiplicity:  per-layer count ranges
size:          ladder ratios, hero rule, bleed
arrangement:   sampler weights (grid, radial, thirds, golden, mirror, diagonal, scatter, bands) + alignment strictness
layers:        count range, roles, opacity, overlap
text:          role, face kind, size rung, case, position rule (or none)
compositing:   blend list, masks/keys, finishing chain, ban list
colour:        scheme, count, value key, contrast, accent share, palette list
3d:            geometry set, instancing, camera, light, material, fog (or "2D only")
motion:        axis, speed range, loop, link to music
nodes:         concrete pools per role (from section 2)
gates:         what must be true of the frame (one hero, coverage, value range, palette distance)
```

## 4. Worked cards (six, to fix the pattern)

### Bauhaus geometry (1919-33; Moholy-Nagy, Albers, Schlemmer, Bayer)
- shapes: circle, square, triangle only; hard edges; no stroke
- multiplicity: 3-9 marks total; size: one hero at 0.5 of height, rest 0.1-0.25
- arrangement: asymmetric balance, thirds and diagonals, strict 0/45/90 angles, overlap with multiply
- layers: 3-5, flat opaque + one translucent overlap
- text: optional lowercase grotesk, 1 line, aligned to an edge
- compositing: Blend multiply/overlap, no blur, no glow; ban bloom, diffuseglow, liquify
- colour: primary red/yellow/blue + black + off-white paper; 60% ground, 30% hero, 10% accent
- 3D: Cube/Sphere/Cone/Cylinder, ortho camera, flat light, matte
- motion: slow rotation and slide on axes, 4 s loop
- gate: hero exists, 3-5 primary-colour areas present

### Suprematism / Constructivism (Malevich 1915, Lissitzky, Rodchenko)
- shapes: rectangles and bars, rotated off-axis, thin lines, one circle; hard edges
- size: one dominant bar, diagonal thrust; arrangement: diagonal composition on a white or cream field, floating, much negative space
- layers: 4-8 overlapping at varied rotation; text: constructivist poster type (heavy sans, rotated 90 or 15 degrees, red band)
- colour: red, black, cream, one cold accent; 3D: Planes and Prisms in space, no ground

### Op art (Vasarely, Riley)
- shapes: one primitive repeated (lines, squares, circles); multiplicity 200-2000; arrangement: strict grid, then a smooth distortion (twirl, ripple, pinchpunch) as the *only* variation
- colour: black/white or two high-contrast; layers 1-2; compositing: displace/ripple, no blur
- motion: phase shift of the grid so the eye vibrates
- gate: stripe frequency in a band that shimmers but is not noise (edge density mid-high, no moire clipping)

### Vaporwave (2010-)
- shapes: Greek bust (3D/photo), grid plane, sun (circle), palm, windows; soft and glossy; text: Japanese/Latin wide-tracked, small
- arrangement: horizon at 0.6, centered hero, symmetric; layers 5-6 with a gradient sky and a grid floor
- compositing: screen/add, glow, scanlines, VHS displace; colour: pink, cyan, purple, peach, gradients; value key mid-high
- 3D: bust-like Sphere/Torus + Plane grid, perspective, pink/cyan rim lights

### Swiss / International Typographic Style (1950s; Müller-Brockmann, Hofmann)
- shapes: rectangles, one circle, rules; text is the image: grotesk at 3 sizes on a 12-column grid, flush-left ragged-right
- arrangement: grid (columns/baseline), big margins, asymmetry; colour: black, white, one red; no effects at all; compositing: none (hard cut layers)
- gate: text present and legible, margins respected

### Brutalist poster (neo-brutalism, Thundr, concrete)
- shapes: oversized blocks, hard stroke 4-12 px, drop shadows offset and hard (no blur), raw bitmaps; text: mono or giant grotesk, all caps, crowded, overflowing
- arrangement: collision and overlap, off-grid on purpose, edge bleed; layers 5-9; colour: concrete grey + one loud (yellow, red, blue) + black
- compositing: multiply, hard shadows, posterize, dither; ban bloom and gradients

The other styles are filled the same way when step 3 of the build order starts; section 5 is the research that decides each card's ranges.

## 5. Movements and subgenres (the style list)

Grouped by lineage; "new" = not in `styles_art.json` yet; "have" = in it. Card ranges are filled from the lineage notes, not from node availability (nodes are matched after).

| Lineage | Style | In file | Core marks and arrangement | Colour rule | Needs (nodes) |
|---|---|---|---|---|---|
| Early modernism | Bauhaus geometry | have | primitives, asymmetric balance | primaries + black | Source shapes, Blend multiply |
| | Suprematist blocks | have | rotated bars, diagonal | red/black/cream | Rectangle, Transform |
| | Constructivism | new | diagonals, heavy type, photomontage bands | red/black/cream | Text, Rectangle, halftone |
| | De Stijl | new | orthogonal grid of black bars, 3 primaries + white | primary | Rectangle, Line |
| | Futurism | new | repeated motion trails, diagonal energy | warm saturated | Trails, motionblur, Feedback |
| | Cubism-like fragmentation | new | faceted overlapping planes, mixed viewpoints | earth, muted | Polygon, Triangle, Blend |
| | Art Deco | new | symmetric fans, sunbursts, chevrons, stepped forms | gold, black, teal | Symmetry, kaleidoscope, Chevron, Ring |
| | Art Nouveau | new | whiplash curves, floral line, border frame | muted olive/ochre | Sketch (rose/tree), Path |
| Graphic and print | Swiss / International | new | grid, type as image | black, white, red | Text, Rectangle |
| | Neo-brutalist poster | have | oversize blocks, hard shadow | concrete + 1 loud | Rectangle, dropshadow, posterize |
| | Thundr brutalism | have | see art.md | | |
| | Riso zine | have | 2-4 inks, misregistration, halftone | fluoro pink/blue/yellow | halftone, Blend multiply, displace |
| | Pop art | new | halftone dots, flat outlines, speech bursts | CMY + black | halftone, Circle, Text |
| | Memphis (1981) | new | squiggles, confetti, triangles, zigzags, patterns | pastels + black | Sketch, Star, Triangle, Polygon |
| | Ukiyo-e cartography | have | flat layered waves, outline | indigo, red, cream | Ramp, Line, posterize |
| | Psychedelic poster (1966-70) | new | melting lettering, vibrating complements, radial | complementary clash | Text, twirl, ripple, kaleidoscope |
| | Rave flyer 1991-93 | new | smiley, fractal tunnel, chrome type | acid yellow, neon | FieldPixel tunnel, Text |
| Optical, kinetic, systems | Op art | have | repeated primitive + distortion | B/W | Sketch grid, ripple |
| | Minimalism / colour field (Albers, Rothko, Martin) | new | 1-3 rectangles or one grid, soft edge, huge calm | 2-3 related hues | Rectangle, gaussianblur, gradientmap |
| | Generative plotter | have | lines, flow fields, hatching | paper + 1-2 inks | Sketch, Line |
| | Cymatics / Chladni | have | node-line figures | mono + 1 | FieldPixel, Formula |
| Screen and signal | Analog CRT | have | scanlines, bloom, convergence | phosphor | vignette, lensdistortion |
| | Vector wireframe | have | white lines on black, 3D mesh | mono | Wireframe 3D, Render 3D |
| | Data landscape | have | terrain of points/lines | mono + glow | Displacement, Instance on Points |
| | Ditherpunk | have | 1-bit dither | 1-bit or 2 inks | dither, threshold |
| | 8-bit pixel | have | 8x8 sprites, limited palette | 8-16 colours | pixelate, lut |
| | 1-bit Mac / ASCII | new | bitmap windows, ASCII glyph grids | 1-bit | Text, dither |
| | Technical blueprint | have | white line on blue, dimension marks | cyan on blue | Line, Text mono |
| | Minimal signal | have | single line/dot moving on black | mono | Line, Circle |
| | Glitch / datamosh | have | block displacement, channel split | RGB | glitch, displace |
| Net aesthetics | Vaporwave collage | have | see card | pink, cyan | bloom, Blend screen |
| | Synthwave grid | have | sun, grid floor, mountains | magenta, orange, navy | Ramp, Line, Circle |
| | Y2K chrome / acid | have | blobs, chrome, stars, bubble type | silver, cyan, pink | Blob, bloom, gradientmap |
| | Holographic iridescence | have | smooth gradient foils, rainbow shifts | spectrum | Color Ramp, Palette |
| | Frutiger Aero | new | glossy bubbles, sky, water, glass | sky blue, green | Circle, gradient, bloom, Light |
| | Cyberpunk neon | have | night city, signs, rain | cyan/magenta | glow, Text |
| | Weirdcore / dreamcore | new | stretched UI, low-res photo, odd labels | washed pastel | Text, pixelate, lensdistortion |
| Avant-garde | Dada photomontage | new | cut-out scraps, ransom type | newsprint | Image Source, luma key, Text |
| | Retro-surreal / lilliputian / Chavetta / liminal | have | see art.md | | |
| Demoscene | Plasma / tunnel / rotozoomer | have (plasma) | field effects, copper bars | cycling palette | FieldPixel, Palette |
| 3D styles | Low-poly facet | new | flat-shaded triangles | 5-8 hues | Icosphere, Subdivide + flat Material |
| | Isometric | new | ortho cube city/blocks | 4-8 hues | Cube, Array, ortho Camera |
| | Voxel | new | cube grids | blocky | Cube, Distribute Points in Grid |
| | Toon / clay / glass | new | one material family | 3-6 hues | Material, Light |

Add in this order: those marked "new" that need only nodes already in section 2 (Swiss, De Stijl, Constructivism, Memphis, Pop, Op, Art Deco, Minimalism, Isometric, Low-poly); the others (Dada, weirdcore) need photo sources and wait.

## 6. Colour rules (the axis that fakes the most)

- Schemes as generators: mono (one hue, value range), analogous (±30 deg), complementary, split, triad, tetrad; draw a scheme, then set hue and the 60-30-10 shares. Palette lists in `art_taxonomy.py` stay as seeds for named looks.
- Value key before hue: set the picture's key (low, mid, high) and contrast; most "washed" or "blown" frames are a value-key failure (the luminance gate already catches the extremes).
- Palette enforcement is a node job: gradientmap / lut / posterize / dither at the end of the stack makes any content obey the palette. Always end a palette-limited style with it.
- Colour on 3D: Material base colour from the palette, light colour from the accent, background from the ground.

## 7. Arrangement samplers (the part randomness cannot fake)

| Sampler | Output | Used by |
|---|---|---|
| grid(n, m, jitter) | cell centres | Op, Swiss, De Stijl, Memphis |
| radial(n, r, phase) | polar ring | Art Deco, mandala, Y2K |
| spiral / phyllotaxis | golden-angle points | organic, plotter |
| thirds / golden(hero) | hero anchor + secondary anchors | poster styles |
| axis(mirror) | symmetric about x or y | Art Deco, vaporwave, Bauhaus (broken) |
| diagonal(angle) | points along a line at an angle | Suprematism, Constructivism |
| bands(k) | horizontal strips | synthwave, landscape, ukiyo-e |
| scatter(poisson) | blue-noise points (no overlap) | Memphis, confetti |
| collide(n) | deliberate overlaps and bleed | brutalist |

Each returns positions in 0-1 canvas space plus a suggested size ladder; the compiler writes `posX/posY/rotation` per layer (or hands the field to a Sketch/FieldPixel program for 100+ marks). Aspect is applied once, so round shapes stay round at 9:16.

## 8. Build order (decided)

1. **Stack model + compiler**: layer record → nodes; arrangement samplers; palette enforcement; native 9:16. Contact-sheet test of 3 styles (Bauhaus, Op, Swiss) across 8 seeds each, before anything else.
2. **Style cards for the existing 30 + the 10 "new" that need no new nodes**; replace `sources/fx/comp` pools with card cells; keep `ban`, `pairs`, `motion`.
3. **Scripted marks**: Sketch/FieldPixel programs for multiplicity (grid, orbit, phyllotaxis, hatch, scatter) as a shared snippet library under `tools/universe/marks/`.
4. **3D mirror**: geometry + camera + light + material per card; Render 3D as a layer.
5. **Text layer**: size ladder, word list per style (Swiss: grid labels; brutalist: shouting words; vaporwave: wide Japanese/Latin).
6. **Gates per axis** (one hero; coverage 15-70%; value range; palette distance; text legible when present; no ellipse where a circle was asked). Rerender, contact sheet, publish only those that pass.

Decided without asking: model = distributions on the ten axes; style cards are data (`styles_art.json` v2) and the compiler is code; the existing 30 ids are kept so `pairs` and the published library do not break.

## 9. Sources (read, not scraped)

- Josef Albers, *Interaction of Color*; Johannes Itten, *The Art of Color* (colour schemes and contrast types).
- Rudolf Arnheim, *Art and Visual Perception* (balance, weight, direction); Kandinsky, *Point and Line to Plane* (shape grammar).
- Josef Müller-Brockmann, *Grid Systems in Graphic Design*; Philip Meggs, *A History of Graphic Design* (movement chronology).
- Owen Jones, *The Grammar of Ornament* (repetition, symmetry, pattern systems).
- Bridget Riley and Victor Vasarely catalogues (op art structure).
- Rosa Menkman, *The Glitch Moment(um)*; Lev Manovich, *The Language of New Media*.
- Local skills used as method: `design-style` (families, line, texture, style bible, A/B), `colour-and-light` (palettes, value, grading), `compositing` (blending, layers and passes, finishing), `environment-design` and `camera-language` (3D framing).
