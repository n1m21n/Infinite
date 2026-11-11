# Finish plan: every open row, cheapest route first (2026-11-11)

Companion to `PLAN.md` (the rows), `AUTORUN.md` (the log) and `REALISM.md` (why). This file says **how** to close
each open row: the method, the paper, the A/B switch, the cases, and the cost on the 8 GB M2.

## 1. The picture problem in layers (the shoe photo, REALISM §2)

A render reads as a photo only when every layer below is right. Each layer has one owner step; a lower layer that
is wrong cannot be fixed by a higher one (a wrong lobe is not fixed by a better map).

| # | Layer | What goes wrong today | Measured sign | Step that fixes it |
|---|---|---|---|---|
| L1 | Light transport: who lights what | settle has no glass; rough metal loses energy; castShadow ignored on Metal | `mat_glass` settle 0.10; `materials` 0.072 on all three paths | 1.6 (this session) |
| L2 | Shadows and contact | strand shadows missing live; one shadow map light on raster | `bed_fur_close` 0.34 / 0.23 | 2.5, 2.3 |
| L3 | Lobe shape: how a surface answers light | Schlick-k G, Schlick Fresnel, no multiscatter, isotropic sheen | 1.6 A/B numbers in AUTORUN | 1.6, then 5.2 (owner) |
| L4 | Micro detail under the pixel | normal maps alias far away; highlights sparkle or vanish | `tex_detail` 0.15 / 0.26, `tex_breakup` 0.57 | 2.1, 2.9 |
| L5 | Relief: height that shadows itself | flat normal map shows no parallax or self-shadow at grazing | REALISM M6 (slope 0.28–0.36) | 2.2, then 4.5 |
| L6 | Material mix on one mesh | one material per mesh: leather + suede + rubber impossible | shoe cannot be built | 3.6 (owner) |
| L7 | Wear: dirt, scuffs, edges | no cavity or edge masks | none yet (needs L6) | 3.2, 5.1 (owner, content) |
| L8 | Camera: exposure, tone, blur, noise | settle motion blur is a post smear | `motion_blur` cases | 2.7 |

Shoe, layer by layer: sun shaft = L1 + L2 (ready after 1.6). Leather gloss = L3 + L4. Perforations and creases =
L5. Suede nap = L3 (fibre sheen, owner). Three materials = L6. Dirt in grooves = L7. Nothing in L6–L7 can be
judged before L1–L5 are within gate, so the build order below is L1 → L5, and L6–L7 wait on the owner as designs.

## 2. Test routine that keeps the machine light

| rule | why |
|---|---|
| Every candidate goes behind `INFINITE_R3D_AB` switch | one build (≈ 4 min) carries any number of variants |
| Screen each switch on **raster** first (2–3 min, `--touch G --skip hair`) | raster and live share the direct-light code; raster is 10× cheaper |
| Confirm the winning **set** once on live and once on settle `--spp 16` | one confirm run instead of one per switch |
| `--skip hair` unless the change touches hair | the four hair cases cost ~150 s each on live (10 of a 17 min sweep) |
| Never run a sweep while building; one job at a time | 8 GB, swap already ~5 GB |
| Sweep saves per case | a killed or over-budget run keeps what it finished |

## 3. Open rows: how to close each

Order = picture layer first, then cost. "Switch" is the `--ab` name to use while measuring.

### Tier A: critical, no owner decision, small (do next, in this order)

| Row | Method | Paper | Switch | Cases | Est. |
|---|---|---|---|---|---|
| 1.6 leftovers | (a) settle multiscatter: bake E(nV, rough) of the tracer's lobe (GGX, height-correlated Smith, F = 1) into a 16×16 R16F texture by quadrature at start-up, gain = 1 + f0·(1/E − 1); analytic fits measured too coarse (max error 0.03–0.04). (b) `ggx` default: first find why deer / geom_monkey / cornell are 6–9 % bright on raster (ambient or sky fill is the suspect), then re-screen. (c) wire exact Fresnel into settle `ptEval` and screen | Kulla and Conty 2017; Turquin 2019 | `msE`, `ggx`, `ptfresnel` | `materials`, `furnace_metal`, `cornell`, `world_grey`, `mat_anisotropy` | 1 build, 1 raster + 1 settle run |
| 2.1 filtered normals + spec AA | (a) Toksvig: store the mip's shortened normal length, widen roughness `a' = sqrt(a² + (1−len)/len)`; needs no new texture, read `length(n)` before normalising at the sampled mip. (b) geometric spec AA: `a² += min(2·max(|dNdx|², |dNdy|²), 0.18)` from screen derivatives | Toksvig 2005; Kaplanyan 2016; Tokuyoshi 2019 | `toksvig`, `specaa` | new `tex_fabric_far`; `tex_detail`, `tex_maps`, `tex_tile` | 1 build, 2 raster runs |
| 2.9 weak live cases | `tex_breakup` 0.57 is the same on all three paths, so it is a **case or mapping mismatch**, not shading: compare Blender's node setup with the breakup hash first (contact sheet), fix the mapping or the case. `emission_light` 0.31 raster: raster has no emitter light; add emissive triangles' power as one area-light proxy per instance (raster only) | — | `emproxy` | the four named cases | 1 build |
| 2.8 thin film in Metal | port GL's `evalIridescence` into a shared header (as sheen, tonemap were in 1.1), call it at the three Metal Fresnel sites | Belcour and Barla 2017 (already the GL source) | none (parity port) | `mat_thinfilm` | 1 build |
| 1.5 adaptive stop | per-pixel variance is already in the denoiser's luminance moments; stop the frame when the 95th-percentile relative error < 2 %; export only | — | env knob | boat s/frame | 1 build |
| 2.4 HDRI importance | build a 64×32 luminance CDF on upload (CPU), sample it for next-event with MIS against the BSDF bounce | Veach 1997 (MIS); Pharr PBRT §12.6 | `envis` | `world_hdri`, a new hard-sun HDRI case | 1 build |

### Tier B: larger, still no owner decision

| Row | Method | Paper | Cases | Risk |
|---|---|---|---|---|
| 2.2 POM raster + live | height from the bump map; 8–24 linear steps along the view in tangent space + 4 secant steps; self-shadow with 8 steps toward the light; depth offset written so RT shadow rays start at the displaced point | Tatarchuk 2006; Policarpo 2005 | new `tex_relief_grazing` | silhouettes stay flat (accepted; 4.5 fixes in settle) |
| 2.3 lights beyond 8 | raster/live: clustered froxel list (16×9×24), 16-bit indices in one buffer; settle: light BVH is overkill, use power-proportional pick with a 1/N floor. Removes `kMaxLights` | Olsson 2012; Estevez and Kulla 2018 (reference only) | new `lights_many`, `city_night` | uniform buffer layout change: T0 `same` first |
| 2.5 strand shadows | live: hair BLAS is already traced; cost is the ray count. Use 1 ray per pixel with blue-noise + the existing half-res mask upsample; add deep-opacity-style attenuation `exp(−k·hits)` with `max_hits 4` instead of binary | Yuksel and Keyser 2008 | `hair_long`, `bed_fur_close` | live hair is already ~150 s/case: measure s/frame first |
| 2.6 live SSS colour | separable screen-space diffusion on the GI/diffuse target, kernel per channel from `sssRadius`, depth-aware | Jimenez 2015; Christensen and Burley 2015 | `mat_subsurface` live ≤ 0.12 | needs a diffuse-only target (c1 exists) |
| 2.7 settle motion blur | per sample pick a time in the shutter; TLAS built with two motion keys (Metal `motionKeyframeCount = 2`), camera interpolated | Cook 1984 | new `motion_blur_shadow` | TLAS rebuild cost: transforms only, meshes static |
| 4.4 settle caustics | first cut: allow refraction paths to end at emitters by BSDF sampling with MIS (now possible with the glass lobe); second: manifold next-event for one refractive interface | Zeltner 2020 (specular manifold sampling) | caustic cases ≤ 0.08 | second cut is research-grade: budget 3 attempts |
| 4.5 micro-displacement | tessellate on upload into the BLAS only (settle), raster keeps POM; cap triangles by memory readout | — | `tex_perforated` | 8 GB: cap at 2 M triangles |
| 1.9 Vulkan backend | same RHI surface; ray query (`VK_KHR_ray_query`) in fragment shaders mirrors Metal's inline intersector; MSL shader → GLSL by keeping the shared headers in the GLSL∩MSL subset (already the rule) | Vulkan spec | within 0.01 of Metal | largest row (~weeks); do after Tier A/B |
| 3.1 Field MSL backend | emitter beside `GlslBackend`: same IR walk, type names and `mix/fract/mod` differences in one table | — | every Field Pixel preset within 1/255 | low |
| 3.3 noise library | parts with no material domain: noise3/4, fbm, voronoi3, wave, brick, checker, gradient as Field built-ins; verify by statistics (`tools/realism`) | Perlin 2002; Worley 1996; Gustavson 2022 (psrdnoise) | statistics within 5 % | low |
| 1.4 R1 gap | CPU side: sort runs by pipeline then mesh, one `setVertexBuffer` per mesh, indirect multi-draw (`MTLIndirectCommandBuffer`) for B4's many objects | — | B4 m/l ≥ 1.5× GL | medium |

### Tier C: waits on the owner (designs exist in `designs/`)

| Row | Unblocks | Smallest first version to approve |
|---|---|---|
| 3.6 layers + masks | the shoe (L6), 5.1 weathering, 5.6 photo cases | Material gets one `Layer` input (a second Material) + `Mask` input; blend the two material records per pixel **before** shading (one BSDF evaluation, not two) |
| 3.2 Field material domain | 3.4 lowering, 3.7 recipes, L7 masks from curvature/AO | outputs base, rough, height, mask only; inputs P, N, UV, curvature |
| 5.2 fibre sheen | suede and carpet on the shoe photo | one `direction` knob on the existing sheen |
| 1.8 engine toggle | Milestone 1 demo | one dropdown on Render 3D |
| 1.7, 3.5, 3.8, 3.9, 4.1–4.3, 5.3, 5.7 | as in their design pages | — |

Recommendation to the owner: approve **3.6 first**. It is the single row between today's engine and the shoe.

## 4. Session-sized blocks

| Block | Rows | Builds | Sweep time | Outcome |
|---|---|---|---|---|
| E-close (this session) | 1.6 winners, castShadow, glass, multiscatter | 2 | ~40 min | L1 and L3 inside gate where the numbers allow |
| F1 | 2.1, 2.8, 2.9 | 2 | ~30 min | L4 |
| F2 | 2.2, 2.4, 1.5 adaptive stop | 2 | ~30 min | L5 (raster/live), HDRI noise |
| F3 | 2.3 | 2 | ~30 min | lights uncapped |
| G | 2.5, 2.6, 2.7 | 3 | ~45 min | L2 live, L8 |
| H | 3.1, 3.3 | 2 | none (unit statistics) | script edge groundwork |
| I | 4.4, 4.5 | 3 | ~45 min | glass caustics, true relief |
| J | 1.9 Vulkan | many | Linux container | parity |

## 5. Results of this session

| change | paths | result |
|---|---|---|
| exact Fresnel | raster, live | default; 0 worse |
| multiscatter gain | raster, live | default; raster 4 better, 0 worse. Settle skipped (needs the E table) |
| glass lobe | settle | default; mat_glass 0.101 -> 0.059 |
| castShadow | live, settle, Metal raster | fixed |
| height-correlated G2 | raster, live | switch only (`--ab ggx`); blocked by over-bright cases |
| falloff cap | — | dropped, no effect |

Full numbers: AUTORUN log.
