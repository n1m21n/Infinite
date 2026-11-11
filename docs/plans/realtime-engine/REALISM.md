# Realism: real objects and scenes (2026-11-11)

**The one ordered plan across all these files: `PLAN.md`.**

Plan only, no code. DESIGN.md section 6 lists the gaps between Infinite's render and Cycles (G1–G10). This doc
asks a harder question: what makes a **photograph** of a real object or place look real, which of those things
Infinite can do today, and how to close the rest. It starts from one photo (a worn sneaker on carpet in a sun
shaft), widens to the objects and scenes people actually make, and grounds the material side in measured scans.

## 1. How realism is judged

| Yardstick | What it proves | Tool |
|---|---|---|
| Cycles A/B | the light transport is right (same scene, same numbers) | `tools/blender-ab` (FLIP, brightness), baseline.json |
| Scanned material maps | the material inputs are right (real albedo, roughness, height, how they relate) | `tools/realism/pbr_measure.py` on CC0 scans |
| Photo match | the whole picture reads as real (light, materials, camera together) | a photo case: the owner's photo + a rebuilt scene, scored on crops by FLIP and by statistics (contrast, histogram, detail energy per scale); the `bed` case is the first |

A step is kept when it improves its yardstick and the sweep holds (same rule as every render step).

## 2. One photo, taken apart (worn sneaker, carpet, sun shaft)

| Layer | What is in the photo | What Infinite needs for it |
|---|---|---|
| Light | one hard sun shaft through a window, soft penumbra, nearly no fill; low grazing angle | sun with angle ✅, window geometry ✅, bounce ✅ |
| White leather | perforation pits, creases, crack lines, grain, slight yellowing, soft gloss | height detail (R4), filtered normals (R3), layered wear (R1) |
| Black suede | short nap, frayed edge hairs, rim glow, direction-dependent shade | directional fibre sheen (R5), Fur on edges ✅ settle, strand shadows live (G4) |
| Rubber midsole | scuffs, dirt in grooves, slight subsurface | cavity mask → dirt layer (R1), SSS ✅ |
| Laces | flat woven tape | weave as height + normals (R3, R4) |
| Carpet | loop pile in ribbed rows, sparkling fibre tips in the sun | Fur or height detail (R4), fibre sheen (R5) |
| Leather + suede + rubber on one shoe | three materials on one mesh, blended at seams | **material layers (R1)**: today one material per mesh |

## 3. What scanned materials teach (measured)

16 CC0 sets from Poly Haven at 1K (leather ×2, fleece, denim, jersey, carpet, rubber, rusty and painted metal,
oak veneer, bark, mossy brick, concrete, slate wall, dirty tiles, peeling paint), measured with `pbr_measure.py`.
Albedo is linear; `c(a,b)` is the correlation between two maps; octaves are shares of normal-map slope energy by
wavelength at 1K over the set's tile (about 1–2 m).

| Set | albedo | albedo CV | rough | rough SD | c(height, albedo) | c(height, rough) | c(AO, albedo) | slope energy 2–4 / 4–16 / 16–64 / 64+ px |
|---|---|---|---|---|---|---|---|---|
| bark_brown_01 | 0.133 | 0.64 | 0.90 | 0.020 | +0.54 | −0.07 | +0.41 | 0.12 / 0.49 / 0.36 / 0.03 |
| black_oak_veneer | 0.125 | 0.13 | 0.66 | 0.024 | −0.04 | −0.25 | −0.09 | 0.26 / 0.45 / 0.27 / 0.02 |
| brick_moss_001 | 0.101 | 0.45 | 0.87 | 0.029 | +0.38 | +0.01 | +0.45 | 0.16 / 0.52 / 0.31 / 0.01 |
| brown_leather | 0.038 | 0.27 | 0.66 | 0.152 | −0.20 | −0.25 | −0.42 | 0.33 / 0.65 / 0.01 / 0.01 |
| castle_wall_slates | 0.235 | 0.57 | 0.52 | 0.160 | +0.09 | −0.13 | +0.20 | 0.09 / 0.36 / 0.44 / 0.11 |
| concrete_wall_003 | 0.517 | 0.30 | 0.62 | 0.062 | −0.12 | −0.02 | +0.39 | 0.13 / 0.35 / 0.19 / 0.32 |
| cotton_jersey | 0.429 | 0.05 | 0.80 | 0.050 | +0.50 | +0.13 | +0.62 | 0.72 / 0.28 / 0.00 / 0.00 |
| denim_fabric | 0.164 | 0.46 | 0.81 | 0.046 | −0.08 | +0.12 | +0.18 | 0.14 / 0.30 / 0.27 / 0.29 |
| dirty_carpet | 0.033 | 0.60 | 0.81 | 0.009 | +0.32 | −0.30 | **+0.92** | 0.07 / 0.53 / 0.34 / 0.05 |
| dirty_tiles | 0.025 | 0.24 | 0.85 | 0.039 | +0.04 | +0.06 | −0.01 | 0.21 / 0.61 / 0.12 / 0.05 |
| green_metal_rust | 0.066 | 0.14 | 0.50 | 0.011 | −0.00 | +0.03 | −0.03 | 0.06 / 0.45 / 0.43 / 0.06 |
| leather_white | 0.288 | 0.10 | 0.62 | 0.027 | +0.15 | −0.14 | −0.08 | 0.57 / 0.40 / 0.03 / 0.00 |
| painted_metal_shutter | 0.229 | 0.21 | 0.63 | 0.043 | +0.23 | −0.24 | +0.53 | 0.04 / 0.47 / 0.50 / 0.00 |
| peeling_painted_wall | 0.180 | 0.08 | 0.84 | 0.004 | −0.16 | **−0.69** | +0.30 | 0.38 / 0.49 / 0.11 / 0.01 |
| polar_fleece | 0.453 | 0.06 | 0.73 | 0.128 | +0.29 | +0.55 | +0.39 | 0.05 / 0.45 / 0.49 / 0.01 |
| rubber_tiles | 0.015 | 0.13 | 0.75 | 0.032 | **−0.59** | −0.53 | −0.61 | 0.04 / 0.80 / 0.16 / 0.00 |

| # | Lesson | Evidence | Consequence |
|---|---|---|---|
| M1 | Real things are darker than people set them | median albedo ≈ 0.13 linear; carpet 0.033, rubber 0.015, brown leather 0.038; only concrete passes 0.5 | a base colour of 0.8 is snow or paper; scenes built by eye are too bright and flat (R6) |
| M2 | Roughness is high and fairly even inside one scanned material | mean 0.5–0.9, SD mostly 0.01–0.05; leather, fleece and slate up to 0.16 | the variation that reads as real lives in **height and albedo**, plus a second material where one surface changes (R1) |
| M3 | Height drives colour | bark +0.54, jersey +0.50, mossy brick +0.38, carpet +0.32 (fibre tips lighter, as in the photo); rubber −0.59 (dirt in grooves); carpet AO↔albedo +0.92 | dirt, wear and moss follow the shape: masks from height, AO and curvature are what artists rebuild in Blender with nodes (R1) |
| M4 | Two materials in one surface | peeling paint: height↔roughness −0.69 (smooth paint on top, rough plaster below) | one material with maps cannot describe it; a layer with a mask can (R1) |
| M5 | Detail lives at every scale | fabrics and leather put 57–72 % of slope energy at 2–4 px (the finest weave and grain); stone and bark at 4–64 px; concrete and denim 30 % beyond 64 px | the finest scale vanishes or sparkles when the texture shrinks on screen (R3); the large scales need real relief, not only shading (R4) |
| M6 | Relief is part of the look | slate, bark, painted shutter: slope 0.28–0.36, energy at 16–64 px | a flat normal map on a flat plane shows no self-shadow or parallax at grazing light, the exact light of the shoe photo (R4) |

## 4. Real objects

| Object | What makes it real | Infinite today | Gaps |
|---|---|---|---|
| Clothing, fabric | weave at sub-mm scale, fuzz, sheen at grazing light, folds | Cloth sim, sheen, maps, detail normals | R3, R5, R4 |
| Leather goods, shoes | grain, creases, perforations, wear at edges, mixed materials | maps, one material per mesh | **R1**, R3, R4 |
| Skin, faces | SSS (red travels far), two specular lobes (oil + skin), pores, fine hair | SSS (settle real, live wrap), Fur | G5 live SSS, R1 (two lobes = layer), R3 pores |
| Hair, fur, carpet | fibre BSDF, strand shadows, clumping | Hair BSDF (C8), Fur, guides | G4, R5 |
| Food | SSS, wetness, translucency, crumbs | SSS, translucency | R9 wetness, R1 |
| Plants, leaves | translucency, alpha cutouts, waxy spec, huge counts, wind | translucency (C9), alpha, Instances | R12 scale, wind |
| Glass, liquids | refraction, absorption by thickness, caustics, bubbles | transmission, glass | G3 caustics, G1 (absorption inside) |
| Metals | brushed anisotropy, scratches, fingerprints, oxidation | anisotropy, coat, maps | R1 (oxide/grime layer), R3 (scratch sparkle) |
| Wood | grain, varnish coat, wear through the coat at edges | coat, maps | R1 (edge wear), R4 |
| Car paint | metallic flakes under clear coat, orange peel | coat, metal | R11 flakes |
| Stone, brick, concrete | relief, cavities with dirt, moss in cracks, tiling at scale | maps, triplanar, Mapping breakup | R4, R1, R7 |
| Worn painted objects | paint over metal/wood, chips, rust streaks | maps | R1, R2 |
| Screens, practical lights | emissive pixels, glow, light on surroundings | emission, emissive mesh lights (settle) | R14 (many lights live) |

## 5. Real scenes

| Scene | What makes it real | Infinite today | Gaps |
|---|---|---|---|
| Interior, daylight | sun shafts, soft bounce, dust in the beam, window glare | GI (live/settle), probes (raster) | G1 volumes (dust in the shaft) |
| Exterior, sun and sky | physical sky, haze with distance (aerial perspective), sun colour by time of day | gradient sky, HDRI with sun extraction | **R15 sky + atmosphere** |
| Night city | hundreds of lights, wet streets, neon, fog | 8 lights max (`kMaxLights`), emissive meshes in settle | **R14 many lights**, R9 wet, G1 |
| Nature, forest | millions of plants, translucency, wind, depth haze | Instances, translucency | R12 scale, R15, G1 |
| Water, ocean | waves, Fresnel, foam, glitter | ocean (boat case 0.06) | foam as a layer (R1), G3 |
| Studio product shot | soft boxes, clean reflections, caustics under glass | area lights, HDRI | G3, R8 (scanned material import) |
| Macro close-up | depth of field, micro relief, SSS | DoF, SSS | R4, R3 |
| Whole worlds, cities | LOD, streaming, memory (8 GB M2) | hash-gated upload planned (DESIGN step 4) | R18 scale |

## 6. Gaps and how to tackle them

R ids are new here; G ids are in DESIGN.md section 6.

**R1 Material layers and masks (the main one; replaces DESIGN route 1 of G6).**

| Part | Approach |
|---|---|
| Model | Material gains up to two extra layers. Each layer is a full material record (base, metal, rough, normal, sheen, coat …) plus a mask; layers blend top-down by mask. Blending inputs, not shading twice: the shared BSDF header evaluates once on the mixed inputs (as Cycles' Mix Shader of two Principled with the same normal behaves to first order); a "two lobes" mode keeps both specular lobes for skin and wet surfaces |
| Masks | any image (as today's map inputs) or a built-in mask: **cavity** and **edge** from mesh curvature (computed once per mesh on the CPU when topology changes, stored per vertex; finer than the mesh from the height map's Laplacian), **AO** (per-vertex bake from the BVH, the AO bake A/B already exists), **world up** (dust, snow, moss on top), **height** (from the height map), **noise** (3D, object space) — each with level and softness |
| Graph | stays Infinite-shaped: one Material node with layer slots, or Material → Material chaining (a second Material node's output into a `layer` input). Owner picks; recommended: chaining, since it adds no new node type |
| Paths | all three read the same mixed inputs (shared header, DESIGN step 1); raster and live evaluate masks per pixel, settle per hit |
| A/B | `mat_layer_rust` (painted metal with rust in cavities, Blender: Mix Shader by a Pointiness/AO mask), `mat_layer_dirt` (rubber with dirt in grooves), photo case `shoe` |
| Gate | Cycles cases at the path gates; the shoe photo case reads as one shoe (leather, suede, rubber on one mesh) |
| Approval | **owner** (new input on Material) |

**R2 Procedural textures per hit (Blender's procedural texture nodes; DESIGN G6 route 2).**

Most realistic Blender materials are not photos. They are procedural texture nodes (Noise, Voronoi, Wave, Magic,
Brick, Checker, Gradient …) mixed through ColorRamp / Mix / Math / Map Range into colour, roughness and a Bump.
They are evaluated in 3D at every shading point, so they have no UV seams, no resolution limit and no tiling.

| Blender node (what it is used for) | Infinite today | Plan |
|---|---|---|
| Noise: fBm, multifractal, hetero terrain, ridged, hybrid; distortion; 4D W for animation (grime, stone, clouds, skin blotches, scratches when stretched) | Noise node: Value, fBm, Ridged, Perlin/Simplex (fBm), warp; Texture node Musgrave: fBm, Multifractal, Hybrid, Ridged, Hetero Terrain. **Both are 2D images in UV space** | 3D per hit; 4D W; distortion = warp |
| Voronoi: F1, F2, smooth F1, distance to edge, cell colour; Euclidean / Manhattan / Chebyshev / Minkowski (cells, scales, cracks, leather grain, stone tiles, perforations) | Texture node Voronoi: all four metrics, F1, F2, Smooth F1, Distance to Edge, N-Sphere Radius, cell colour (2D) | 3D per hit; cell colour as its own output |
| Wave: bands / rings, sine / saw / triangle, distortion (wood grain, fabric, sand ripples) | Texture node Wave: Bands / Rings, Sine / Saw / Triangle, distortion, detail (2D) | 3D per hit |
| Magic (interference swirls: iridescent fabric, stylised marble, psychedelic) | Texture node Magic: depth, distortion (2D) | 3D per hit |
| Brick (bricks, tiles, planks with mortar and per-brick colour) | Texture node Brick: size, row offset, mortar size and smooth, bias (2D) | mortar mask as its own output |
| Checker, Gradient (linear, radial, spherical), White Noise (per-cell random) | Texture node Checker, Gradient (7 types); Ramp; Noise White (2D) | 3D per hit |
| Gabor (directional noise: brushed metal, wood fibres, fabric) | none | new Texture type |
| ColorRamp, Map Range, Math, Mix (multiply, overlay, screen, … by a factor) | Color Ramp, gradientmap, Blend (2D images); Math (modulators only) | the same ops on per-hit values |
| Bump (height → shading normal, strength, distance) | `normal map` image filter (2D) | per-hit bump from the procedural height (derivatives in raster/live, finite difference in settle) |
| Texture Coordinate: Generated, Object, UV, Normal, Reflection; Geometry: Position, Pointiness | UV only (+ triplanar on maps) | per-hit inputs: object/world position, normal, UV, curvature (pointiness), AO |

**How, in Infinite's way (recommended): the texture graph compiles.** People keep patching nodes as today:
Noise → Color Ramp → Blend → Material. When every node feeding a Material input is procedural (no video, no
file), the chain is lowered to one per-hit function instead of being baked into a 1024 image:

```
 Noise ─► Color Ramp ─┐
 Voronoi ─────────────┴► Blend ─► Material.roughness
        │ all procedural?
        ├ yes ─► lowered to a Field material program (object space, any zoom), compiled to GLSL (raster)
        │        and MSL (live, settle); a Space switch on Noise picks UV / object / world
        └ no  ─► baked as today (2D image in UV space)
```

The lowering target is the Field language: its compiler already emits GLSL; it gains an MSL backend, a
`material` domain (inputs P, N, UV, curvature, AO; outputs base, rough, metal, height, mask) and a built-in library
(noise3, fbm3, multifractal3, voronoi3 with all features, wave, magic, brick, checker, gradient, gabor, colour
ramp, map range, mix modes, bump). A user can also write a Field material by hand. Nodes the graph lacks today
(Gabor, and Blender's newer Noise distortion) become Texture node types, not new nodes. The Texture node
already has Voronoi, Brick, Magic, Wave, Musgrave, Checker, Gradient, Clouds, Marble and Wood: the generators exist,
what is missing is evaluating them per hit in 3D.

Clean room: every function is written from its paper or public description (Perlin 2002 improved noise, Worley
1996 cellular, Musgrave's fractal terrain chapters in *Texturing and Modeling*, Lagae 2009 Gabor noise), never from
Blender's GPL source. That means Blender's exact patterns cannot be reproduced bit for bit, so procedural A/B cases
are scored by **statistics, not FLIP**: feature size (autocorrelation length), histogram, power spectrum slope,
and for Voronoi the cell-size distribution, each within 5 % of the Cycles render of the same node setup. Lighting
and shading of a procedural material are still checked by FLIP by baking our pattern to an image and feeding the
same image to Cycles.

| Part | Approval |
|---|---|
| Noise and Texture nodes: Space switch (UV / object / world), Gabor type, separate outputs (cell colour, mortar mask) | **owner** (new modes on existing nodes) |
| Lowering the graph to per-hit code | none in the UI (same picture, sharper); gate: lowered = baked within 0.01 FLIP at the bake's resolution |
| Hand-written Field materials | **owner** (new Field domain) |

Recipes to prove it, each as a case: wood (wave rings + noise distortion + colour ramp), marble (noise fBm through a
sine + colour ramp), cracked mud (voronoi distance to edge → bump + dark mask), leather (voronoi smooth F1 →
bump, noise → roughness), rust patches (multifractal → mask on R1 layers), brushed metal (Gabor along a tangent →
roughness and anisotropy), fabric (wave × wave → bump), brick wall (brick + noise per brick colour + mortar mask).

**R3 Filtered normals and specular antialiasing (fix).** When a normal map shrinks on screen its fine slopes
average to flat: weave and grain vanish, or alias into sparkle. Fix: store each mip's normal variance and add it to
roughness (Toksvig 2004 / LEAN, Olano & Baker 2010), plus geometric specular AA from screen-space normal change
(Kaplanyan 2016) in raster and live; settle needs only the variance term at far mips. A/B: `tex_fabric_far`
(jersey at three distances) vs Cycles at high spp. Gate: FLIP better at the far distances, unchanged near.

**R4 Height detail.**

| Path | Approach |
|---|---|
| Raster, live | parallax occlusion mapping from the height map (linear search + binary refine, 8–32 steps by angle) with self-shadow along the sun (Tatarchuk 2006); silhouettes stay flat |
| Settle | true micro-displacement: tessellate patches near the camera to about one triangle per pixel from the height map and put them in the BVH, cached per view; far patches keep the normal map |
| Existing | the Displacement node stays for large shapes (it moves real vertices) |
| A/B | `tex_relief_grazing` (slate wall under a grazing sun), `tex_perforated` (leather with holes) |
| Approval | none for POM (a fix to how height maps shade); micro-displacement reuses the existing height input |

**R5 Directional fibre sheen.** Suede, velvet and carpet change shade with the nap direction. Add a sheen
direction from the tangent (or a flow map) and stretch the fitted sheen lobe along it (anisotropic version of
`Render3DSheen.h`, fitted to Blender renders the same way as Block A). A/B: `mat_suede_nap` (two patches brushed
opposite ways). Approval: owner (new material params).

**R6 Albedo truth.** A measured materials library: each preset from a CC0 scan with its measured albedo,
roughness and maps (pbr_fetch + pbr_measure), so starting points are physically right. Approval: owner (browser
content). No warning text under the colour control (short UI text rule).

**R7 Tiling at scale.** Mapping breakup exists; `tex_breakup` is judged by `tile_repeat.py`. Extend to
histogram-preserving stochastic tiling (Heitz & Neyret 2018) only if tile_repeat shows repeats on the stone and
ground sets. Fix, no approval.

**R8 Scanned set import.** Dropping a folder or zip of PBR maps (`*_diff`, `*_nor_gl`/`*_nor_dx`, `*_rough`,
`*_disp`, `*_ao`, `*_arm`) should build a wired Material: albedo as sRGB, the rest linear, DirectX normals flipped
in green, ARM split. Today glTF drops are handled (StageDropHandling.cpp); a map folder is not. Enhancement to
drop handling; check with the owner since it creates nodes.

**R9 Weathering recipes (on R1).** Wet (darker albedo, roughness → 0.05, a water layer filling cavities first),
dust (world-up mask, high roughness, grey), snow (world-up + noise, SSS white), moss (cavity + world-up, green
fibre sheen), rust (edge + cavity, orange-brown, rough). Each is a layer preset, not code. A/B: one case each.

**R10 Skin.** SSS in settle is real (C9); live needs G5. Two specular lobes (R1 two-lobe mode), pores via R3/R4,
peach fuzz via Fur. Photo case: a hand under a window.

**R11 Car paint.** Flakes: a hashed per-cell normal perturbation under the clear coat, sparkle density and size as
params (or an R2 Field material). A/B: `mat_carpaint` vs Cycles with a Voronoi flake normal.

**R12 Vegetation at scale.** Instances in the TLAS (one BLAS per plant), alpha cutouts with any-hit (done for
shadows in B6), wind as a vertex shader sway driven by the transport. Memory budget per plant on 8 GB.

**R14 Many lights.** Today live and raster take 8 lights (`kMaxLights`, `LightU lights[8]`). Live: ReSTIR direct
lighting (Bitterli 2020) over a light buffer with no fixed cap, emissive meshes included; raster: clustered
forward lighting (lights binned into a froxel grid); settle: a light tree (BVH over lights, importance by power and
distance). A/B: `lights_many` (100 point lights), `city_night` (emissive windows + street lights).

**R15 Sky and atmosphere.** Physical sky from sun position (Hillaire 2020: transmittance + multi-scattering LUTs),
the sun's colour following it, aerial perspective as a froxel haze (shares G1's grid). The gradient sky stays as an
artistic option. A/B: `sky_noon`, `sky_sunset` vs Cycles' Nishita/multiple-scattering sky.

**R18 Worlds and cities.** LOD per mesh, texture streaming (sparse mips), hash-gated upload (DESIGN step 4), and a
memory budget readout on Render 3D. Gate: a city block at 1080p stays under the 8 GB machine's GPU share.

## 7. Order

| # | Step | Kind | Gate |
|---|---|---|---|
| 1 | R3 filtered normals + specular AA | fix | `tex_fabric_far` better far, same near |
| 2 | R4 POM in raster/live | fix | `tex_relief_grazing` live/raster better |
| 3 | R14 many lights | fix (removes a hard cap) | `lights_many`, `city_night` at the path gates |
| 4 | R1 layers + masks | **owner** | layer cases at the path gates; shoe photo case |
| 5 | R2 procedural textures per hit (graph lowering, Field materials, Space switch, Gabor) | **owner** | lowered = baked within 0.01; recipe cases by statistics within 5 % |
| 6 | R9 weathering presets on R1 | content | one case each |
| 7 | R15 sky + atmosphere | **owner** (new sky mode) | sky cases ≤ 0.05 settle |
| 8 | R4 micro-displacement in settle | enhancement | `tex_perforated` settle ≤ 0.05 |
| 9 | R5 fibre sheen, R11 flakes | **owner** | their cases |
| 10 | R8 scanned set import, R6 library | **owner** | drop builds the same Material as by hand |
| 11 | R12, R18 scale | enhancement | memory and frame budgets |

These run beside DESIGN.md's steps 1–16: R1 and R3 lean on the shared shading header (DESIGN step 1), R14 and R18
on the material record and hash-gated upload (steps 3, 4). Photo cases (the shoe, a hand, a sunlit room, a street
at night) are added as each part lands, from the owner's own photos.

## 8. Assets

CC0 only (Poly Haven, ambientCG), fetched by `tools/realism/pbr_fetch.py` into a scratch folder, measured, and
deleted; never committed. A case that needs a set fetches it by name, as the HDRI cases do.

## 9. Blender's shader menus, node by node

The owner's reference: every node in Blender's shader editor menus (Input, Shader, Displacement, Color, Texture,
Utilities), what Infinite has for it today and what closes the gap. "2D" means the node bakes an image in UV
space; "per hit" means it is evaluated at every shading point in 3D (R2). Status: **have**, **partial**, **missing**,
or **n/a** (not needed in Infinite's model).

**Shader.** Blender builds a surface by mixing BSDFs; Infinite has one Principled Material with modes.

| Blender | Infinite | Status | Plan |
|---|---|---|---|
| Principled BSDF | Material (base, metal, rough, IOR, transmission, specular, clearcoat, sheen, SSS, anisotropy, iridescence, dispersion, emission, alpha) | have | close the per-engine gaps (DESIGN §6) |
| Diffuse, Glossy, Metallic, Glass, Refraction, Specular, Emission, Transparent | Material with those lobes set alone | have (as settings) | presets in the Material menu, no new nodes |
| Translucent BSDF | Material translucency | have | — |
| Subsurface Scattering | Material SSS (random walk in settle, wrap in raster/live) | have | G5 live colour |
| Hair BSDF (Principled Hair) | Material model Hair (melanin, redness, tint, cuticle shift) | have | G4 strand shadows |
| Holdout | Render 3D alpha only | missing | a Material "holdout" flag: cut a hole to alpha, for compositing over video (enhancement) |
| Mix Shader, Add Shader | none: one Material per mesh | **missing, the big one** | R1 layers + masks: a Material takes a second Material and a mask; Add = emission/coat on top |
| Principled Volume, Volume Absorption / Scatter / Coefficients | Volume is a 2D compositing node; no 3D volume shader | missing | G1 volumes (homogeneous first, then grids) |
| Toon, Ray Portal, Sheen BSDF | Material sheen; no toon | partial | toon = Render 3D stylised mode later; portal not needed |

**Input.** What a shader can ask about the point it shades.

| Blender | Infinite | Status | Plan |
|---|---|---|---|
| Texture Coordinate (Generated, Object, UV, Normal, Camera, Window, Reflection) | Mapping node: UV, Generated, Object space, triplanar | partial | R2 Space switch adds world/normal/reflection per hit |
| UV Map | Mapping uv; one UV set per mesh | partial | second UV set for lightmaps/decals when a case needs it |
| Geometry (Position, Normal, Tangent, Pointiness, Random per island, Backfacing) | Render 3D cavity/curvature in view shading only | missing in materials | R2 material domain inputs P, N, curvature, backfacing, random per island |
| Ambient Occlusion (as a mask) | Render 3D AO pass; Material ao image | partial | per-hit AO mask for grime in creases (R9 weathering) |
| Bevel (rounded-edge normals at render time) | Bevel geometry node | partial | shader bevel in settle (short-range normal blend), raster uses real bevels |
| Fresnel, Layer Weight | inside Principled only | missing as masks | material domain outputs `facing` and `fresnel` for rim dust, velvet, car-paint flop (R11) |
| Light Path (is camera ray, ray depth …) | none | missing | settle only, for tricks (invisible lights, cheap GI); low priority |
| Attribute, Color Attribute | Set Vertex Color, geometry attributes | partial | read any named attribute in the material domain |
| Object Info (random, colour, location), Particle Info, Point Info | Instance on Points per-instance values | partial | `random` per object/instance in the material domain: no two rocks alike |
| Curves Info (intercept, thickness, random) | Fur strands (root to tip used by Hair model) | partial | expose intercept/random for strand colour gradients |
| Tangent | anisotropy rotation | partial | tangent from UV or radial, for brushed metal (R2 Gabor) |
| Camera Data, Scene Time | modulators drive any param | have (better: audio, LFO) | material domain reads `time`, `view depth` |
| Wireframe | Wireframe geometry node | partial | shader-only wire mask: material domain `edge distance` |
| Raycast, Volume Info | none | missing | settle only, later |

**Texture.** Generators. Infinite has most of them already, as 2D images.

| Blender | Infinite | Status | Plan |
|---|---|---|---|
| Noise | Noise node (10 types) + Texture Musgrave/Clouds (2D) | partial | R2 per hit, 4D W |
| Voronoi | Texture Voronoi, all metrics and features (2D) | partial | R2 per hit |
| Wave, Magic, Brick, Checker, Gradient | Texture node (2D) | partial | R2 per hit; Brick mortar mask output |
| White Noise | Noise White (2D) | partial | per-cell / per-object random in material domain |
| Gabor | none | missing | new Texture type (Lagae 2009) |
| Image Texture | Image Source → Material inputs; Mapping (box/triplanar) | have | R7 stochastic tiling, R8 scanned-set import |
| Environment Texture | HDRI node | have | G9 importance sampling of the whole map |
| Sky Texture | none | missing | R15 sky + atmosphere |
| IES Texture | Light cone/penumbra only | missing | IES profile on Light (G8 light shapes) |

**Color.** Image-side Infinite is strong; what it lacks is doing it per hit.

| Blender | Infinite | Status | Plan |
|---|---|---|---|
| Color Ramp | Color Ramp (OKLab/OKLCH, 16+ stops), gradientmap | have (2D) | per hit (R2) |
| Mix Color (all blend modes) | Blend (28 modes) | have (2D) | per hit (R2) |
| Hue/Saturation/Value, Brightness/Contrast, Gamma, Invert, RGB Curves | color adjustments, Curves, invert, exposure | have (2D) | per hit (R2) |
| RGB to BW, Separate / Combine Color | alpha from luma, threshold; no channel split | partial | channel split/merge in the material domain |
| Blackbody, Wavelength | Light kelvin | partial | blackbody for emission colour (fire, hot metal) |
| Light Falloff | Light falloff setting | have | — |
| Shader to RGB | none (Eevee-only toon trick) | n/a | stylised mode later |

**Displacement.**

| Blender | Infinite | Status | Plan |
|---|---|---|---|
| Normal Map | Material normal input + detail scale; normal map filter | have | R3 filtered normals |
| Bump | Material bump height map | have (image) | per-hit bump from procedural height (R2) |
| Displacement | Displacement node (moves vertices; needs Subdivide) | have | R4 POM in raster/live, micro-displacement in settle |
| Vector Displacement | Displacement vector/RGB mode | have | — |

**Utilities.** Math, Vector Math, Map Range, Clamp, Float Curve, Repeat, Closure, Bundle, Menu Switch.

| Blender | Infinite | Status | Plan |
|---|---|---|---|
| Math, Vector Math, Map Range, Clamp, Float Curve | Math node (modulators only); Range to Range; Mod Curve | partial | the material domain's built-in library; as nodes, Math gains an image/per-hit form (owner) |
| Repeat (loop), Closure / Evaluate Closure (functions), Bundles | Field language: loops, functions, structs | have, as code | a Field material is the "power user" escape hatch (R2) |
| Menu Switch, Implicit Conversion | Switcher; cables convert types | have | — |

**What this says, in short.**

- Infinite already owns most of Blender's *pieces*: the generators (Texture, Noise), the colour tools (Color Ramp,
  Blend, adjustments, Curves), Principled, Hair, SSS, Translucency, Normal / Bump / Displacement, HDRI.
- Two structural gaps hold back realism, not single missing nodes:
  1. **Per-hit evaluation** (R2): everything procedural is a 2D UV image today.
  2. **Mixing materials** (R1): no Mix Shader, so no dirt over paint, rust over metal, wet over dry.
- Then a set of **masks** realistic materials are built from, all one "material domain inputs" job inside R2:
  pointiness/curvature, AO, Fresnel/facing, per-object random, attributes, edge distance.
- Missing renderer features already planned elsewhere: volumes (G1), sky (R15), IES (G8), holdout.
- Not worth doing: Light Path tricks, Shader to RGB, Ray Portal, Raycast/Volume Info, until a case asks.
