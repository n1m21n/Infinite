# Finish plan: every open row, cheapest route first (2026-11-11)

**Order superseded 2026-11-11 by `OPUS_PLAN.md`** (foundation first: material programs, shading nodes, layers,
Vulkan). The code briefs in section 6 here stay valid and are referenced from there.

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

Order = picture layer first, then cost. "Switch" is the `--ab` name to use while measuring. Code for each row: §6.

### Tier A: critical, no owner decision, small (do next, in this order)

| Row | Method | Paper | Switch | Cases | Est. |
|---|---|---|---|---|---|
| 1.6 leftovers | (a) settle multiscatter: bake E(nV, rough) of the tracer's lobe (GGX, height-correlated Smith, F = 1) into a 16×16 R16F texture by quadrature at start-up, gain = 1 + f0·(1/E − 1); analytic fits measured too coarse (max error 0.03–0.04). (b) `ggx` default: first find why deer / geom_monkey / cornell are 6–9 % bright on raster (ambient or sky fill is the suspect), then re-screen. (c) wire exact Fresnel into settle `ptEval` and screen | Kulla and Conty 2017; Turquin 2019 | `msE`, `ggx`, `ptfresnel` | `materials`, `furnace_metal`, `cornell`, `world_grey`, `mat_anisotropy` | 1 build, 1 raster + 1 settle run |
| 2.1 filtered normals + spec AA | (a) Toksvig: store the mip's shortened normal length, widen roughness `a' = sqrt(a² + (1−len)/len)`; needs no new texture, read `length(n)` before normalising at the sampled mip. (b) geometric spec AA: `a² += min(2·max(|dNdx|², |dNdy|²), 0.18)` from screen derivatives | Toksvig 2005; Kaplanyan 2016; Tokuyoshi 2019 | `toksvig`, `specaa` | new `tex_fabric_far`; `tex_detail`, `tex_maps`, `tex_tile` | 1 build, 2 raster runs |
| 2.9 weak live cases | `tex_breakup` 0.57 is not a defect (Cycles has no breakup; the case is judged by `tile_repeat.py`): take it out of the FLIP gate. `emission_light` 0.31 raster: raster has no emitter light; add emissive triangles' power as one area-light proxy per instance (raster only) | — | `emproxy` | the four named cases | 1 build |
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

## 6. Code briefs (for any model that executes a row)

Snippets are in the GLSL ∩ MSL subset the shared headers use (`vec3` is mapped to `float3` on Metal by the
header macros; no arrays, no `const` arrays, no `out` structs). Sites are named by function, not line: grep for
them. Every new piece goes behind `#ifdef R3D_AB_<NAME>` first (macro list comes from `Render3DAb.h`), is
measured with `sweep.py --ab <name>`, and loses its `#ifdef` only when it wins.

Sites that already exist (verified 2026-11-11):

| what | where |
|---|---|
| shared BSDF helpers `ggxG2`, `ggxMs` | `src/nodes/Render3DBsdf.h` (`R3D_BSDF_SRC`) |
| `fresnelExact` | `src/nodes/Render3DFresnel.h` |
| raster/live direct light, `fresnel()`, `lightFalloff`, `mapNormalTS`, spec AA (`nVar`) | `MetalDevice.mm` fragment source |
| settle: `ptEval`, `ptEvalT`, `ptProbs`, `ptVis`, `ggxSampleH`, `ptPickLight`, `ptFd`, `ptG1` | `MetalDevice.mm` settle source |
| settle accumulation and variance (`acc`, `aux.a` = luminance², `dn_prep_k`) | `src/nodes/Render3DDenoise.h` |
| GL forward: `evalIridescence`, `fresnelSchlick`, `uNormalMap` | `Geometry3DNodes.cpp` |
| TLAS build (instance descriptors, `o.mask`) | `MetalDevice.mm`, search `MTLInstanceAccelerationStructureDescriptor` |
| env upload | `MetalDevice.mm` `EnsureEnv`, `mEnvTex` |
| light record | `engine::Light` in `src/engine/Scene.h`; cap `engine::kMaxLights` |

### 6.1 Settle multiscatter (1.6 leftover a): bake E instead of fitting it

Why the fit failed: the tracer's lobe is GGX with height-correlated Smith; its albedo `E(nV, rough)` is not
separable and bends sharply at grazing + mid roughness. A 16×16 table is exact enough and costs one fetch.

CPU, once at device start (C++; 256 texels × 64×128 samples ≈ 2 M evaluations, a few ms):

```cpp
// E(mu, r): directional albedo of the single-scatter lobe with F = 1. Sample H by GGX NDF.
static float BakeE(float mu, float r) {
   const float a = std::max(0.002f, r * r), a2 = a * a;
   const int N1 = 64, N2 = 128; double sum = 0;
   for (int i = 0; i < N1; ++i) for (int j = 0; j < N2; ++j) {
      const float u1 = (i + 0.5f) / N1, ph = 6.2831853f * (j + 0.5f) / N2;
      const float ct = std::sqrt((1 - u1) / (1 + (a2 - 1) * u1)), st = std::sqrt(std::max(0.f, 1 - ct * ct));
      const float hx = st * std::cos(ph), hz = ct;                 // V = (sqrt(1-mu²), 0, mu)
      const float vh = std::sqrt(1 - mu * mu) * hx + mu * hz;
      const float nl = 2 * vh * hz - mu;
      if (vh <= 0 || nl <= 0) continue;
      const float gV = nl * std::sqrt(a2 + (1 - a2) * mu * mu), gL = mu * std::sqrt(a2 + (1 - a2) * nl * nl);
      sum += (0.5f / (gV + gL)) * nl * 4 * vh / std::max(ct, 1e-6f);   // Vis · nL · 4·vH / nH  (pdf = D·nH/(4·vH))
   }
   return float(sum / (N1 * N2));
}
// texel (x, y): mu = (x + 0.5) / 16, r = (y + 0.5) / 16 -> MTLPixelFormatR16Float, linear, clamp.
```

Checked against quadrature in Python this session: rough 1.0 gives 0.53–0.89 over nV, rough 0.37 gives 0.90–0.99.

Shader (settle only; raster/live keep the Karis-based `ggxMs`, which measured as a win there):

```c
float3 ptMs(float3 f0, float rough, float nV, texture2d<float> eLut, sampler s) {
   const float E = clamp(eLut.sample(s, float2(nV, rough), level(0)).r, 0.05, 1.0);
   return float3(1.0) + f0 * (1.0 / E - 1.0);            // Turquin 2019, eq. 16
}
// ptEval: fr = D * ptVis(nV,nL,a) * F * ptMs(...) * w.y + ...   (spec term only; the diffuse (1-F) keeps plain F)
```

Gate: `--engine settle --spp 16 --ab msE run materials furnace_metal cornell world_grey mat_anisotropy lineup tex_maps`;
keep if 0 worse and `furnace_metal`, `materials` drop. Furnace check: a rough metal sphere of f0 = 1 in a white
furnace must return 1.0 ± 0.02.

### 6.2 `ggx` default (1.6 leftover b): find the brightness excess first

`deer`, `geom_monkey`, `cornell` render 6–9 % bright on raster (`b` column 1.06–1.09) before any switch. Schlick-k
G darkens the lobe and hid that. Order of checks, each one raster run on those three cases:

1. `--ab noamb` (new switch: ambient/sky fill × 0): if `b` falls to ~1.0, the fill term is double counting sky
   that the env specular already adds.
2. Check that the env diffuse uses the cosine-convolved mip (highest lod), not a mid lod.
3. Check `kD = (1 − F)(1 − metal)` uses the same `F` as the specular (it does in direct light; verify in the
   ambient branch).

Then `--ab ggx` again. Keep only with 0 worse.

### 6.3 Fresnel in settle (1.6 leftover c)

`ptEval` and `ptEvalT`: replace `f0 + (1 − f0) * ptS5(vH)` with `fresnelExact(vH, f0)` under `R3D_AB_PTFRESNEL`.
`ptProbs` keeps Schlick (it is only a sampling probability; any positive value is unbiased). Run the same seven
settle cases as 6.1.

### 6.4 Row 2.1: filtered normals

Geometric spec AA is **already in** the Metal fragment shader (`nVar` from `dnx`, `dny`, cap 0.18). Missing:
(a) the same in GL forward, (b) Toksvig from the normal map's own mips, (c) both on the settle first hit.

```c
// (b) in mapNormalTS, before normalising the sampled tangent normal `t` (xyz in −1..1, trilinear-filtered):
float len = clamp(length(t), 1e-3, 1.0);
float tokVar = (1.0 - len) / len;                    // Toksvig 2005: variance of the averaged normals
// return it with the normal; at the roughness site:
float a2 = roughIn * roughIn;  a2 = a2 * a2;         // alpha²
rough = sqrt(sqrt(clamp(a2 + nVar + min(tokVar * 0.5, 0.25), 0.0, 1.0)));
```

Needs the normal map uploaded **without** per-mip renormalisation (check the mip generator: Metal
`generateMipmaps` averages, which is what Toksvig wants). (c) settle: use ray-cone footprint to pick the lod
(`lod = log2(coneWidth · texSize / worldPerUv)`, cone width = pixel angle × distance), then the same widening.
New case `tex_fabric_far`: fine weave normal map, uv ×40, camera 8 m away. Switches `toksvig`, `specaa_gl`.

### 6.5 Row 2.9: weak cases

- `tex_breakup` 0.57 is **not a defect**: `cases.py` says Cycles has no breakup, so its truth is the plain tile
  and the case is judged by `tile_repeat.py`, not FLIP. Action: exclude it from the FLIP gate list (one line in
  the sweep's gate summary), keep the tile-repeat score.
- `emission_light` raster 0.31: raster has no light from emissive meshes. Proxy, raster and live only:

```cpp
// Render3DEngine.cpp, when filling fr.lights: for each instance with emission strength > 0 and room under kMaxLights
Light l; l.type = LightType::Area;
// centre and size from the instance's world box; aim = the box's thinnest axis (a panel emits along its normal)
l.intensity = emissionStrength * area;  // radiant exitance × area; same unit path as an Area light
l.castShadow = true;  l.specular = 0.0f; // the surface itself is visible in reflections; avoid a doubled highlight
```

  Skip the emitter's own instance in that light's shadow test (instance mask bit 2, or bias the ray start by the
  box half-thickness). Switch `emproxy`. Gate: `emission_light` raster ≤ 0.20, 0 worse elsewhere.

### 6.6 Row 2.8: thin film on Metal

Move GL's `evalIridescence(nDotV, ior, thickness)` out of `Geometry3DNodes.cpp` into a new
`src/nodes/Render3DThinFilm.h` as `R3D_THINFILM_SRC` (same pattern as `Render3DSheen.h`), include it in both. On
Metal call it at the three Fresnel sites: direct specular, env specular, settle `ptEval`:
`F = mix(F, irid, m.mat.iridescence)`. `MatRec` needs `iridescence`, `iridescenceIor`, `iridescenceThickness`
if not already packed (check `FillMat`). Tier 0 first on GL (`same`: the move must not change a pixel), then
`mat_thinfilm` on live and settle.

### 6.7 Row 1.5: adaptive stop (export only)

`dn_prep_k` already computes per-pixel variance of the mean. Add a reduction every 8 samples:

```c
// rel = sqrt(var) / max(l, 0.05)   per pixel; write to an R16F target, then one 64-bin histogram kernel
// stop when the 95th percentile of rel < INFINITE_ENGINE_STOP (default 0.02) and spp >= 8
```

CPU reads the 64-bin histogram (256 bytes) instead of the image. Export loop: replace the fixed `ptSamples` count
by `while (spp < ptSamples && !converged)`. Gate: boat s/frame falls, FLIP vs the full-count render ≤ 0.005.

### 6.8 Row 2.4: HDRI importance sampling

CPU at `EnsureEnv` (env already on CPU before upload):

```cpp
// 64×32 grid: w[y][x] = mean luminance of the block × sin(theta_y). Build
//   margY[32]  = cdf over rows,   condX[32][64] = cdf over columns per row   -> one 64×33 R32F texture
```

Settle, at each non-specular hit, next to the analytic-light NEE:

```c
float2 u = float2(rnd(seed), rnd(seed));
// binary search margY (5 steps), then condX row (6 steps) -> (x, y), jitter inside the cell
float pdfUv  = w(x,y) / sumW * (64.0 * 32.0);
float pdfDir = pdfUv / (2.0 * PI * PI * max(sinTheta, 1e-4));
float3 L = dirFromEquirect(uv);          // must match Render 3D's lookup: u from atan(z, x) + rotation, v = 0 at zenith
float3 f = ptEval(n, V, L, ..., bsdfPdf);
if (!occluded(P, L)) Lo += T * f * env(L) * (pdfDir / (pdfDir*pdfDir + bsdfPdf*bsdfPdf)); // power heuristic, f already holds nL
// and on a BSDF-sampled ray that misses: weight env by bsdfPdf² / (bsdfPdf² + pdfDir(L)²)  (today: skyK = 1)
```

The miss-side weight is the part that is easy to forget: without it the sky is counted twice. `lastPdfB` already
carries the BSDF pdf; set `skyK` from it. Switch `envis`. New case `world_hdri_sun`: an HDRI with a 50 000 nit
sun disc. Gate: that case at 16 spp ≤ its 256 spp FLIP + 0.01; `world_hdri` 0 worse.

### 6.9 Row 2.2: parallax occlusion mapping (raster, live)

Height = the bump map's grey (the same texture `mapNormalTS` reads with `bump = true`).

```c
float2 pomUv(texture2d<float> h, sampler s, float2 uv, float3 vTS, float scale, float2 gx, float2 gy, thread float& hOut) {
   const float n = mix(24.0, 8.0, abs(vTS.z));             // more steps at grazing
   const float2 dUv = -vTS.xy / max(vTS.z, 0.1) * scale / n;
   float layer = 1.0, dl = 1.0 / n;  float2 c = uv;  float hc = h.sample(s, c, gradient2d(gx, gy)).r;
   for (int i = 0; i < 24 && hc < layer; ++i) { c += dUv; layer -= dl; hc = h.sample(s, c, gradient2d(gx, gy)).r; }
   // secant refine between the last two layers (4 steps), then:
   hOut = hc;  return c;
}
// self-shadow: march 8 steps from (c, hc) toward lTS; shadow = 1 − smoothstep of the max (h − layer) found × 4
```

Gradients must be the **original** uv derivatives (`gx`, `gy`), or mips break at the step edges. Live: start the RT
shadow ray at `wp − n · (1 − hc) · scale · worldPerUv` so contact shadows meet the relief. Fade `scale` to 0
where `fwidth(uv) · texSize > 4` (far away the normal map + Toksvig carries it). No new UI: driven by the
existing bump strength; if it needs its own depth knob that is an owner row.

### 6.10 Row 2.3: more than 8 lights

- Raster/live: one `R32Uint` buffer of froxel lists. Grid 16×9×24, depth slices exponential
  (`z_k = near · (far/near)^(k/24)`). CPU bins lights per frame (sphere of `cutoff`, or of the distance where
  the light falls under 1/256 of exposure white; suns go in every cell). Shader:
  `cell = (uint2(pos.xy · grid.xy / size), slice(viewZ))`, loop `offset..offset+count`. Light data moves from the
  uniform block to a buffer (`struct LightRec`, 64 bytes). Do the buffer move as a Tier 0 change first (`same`).
- Settle: `ptPickLight` picks by power today; keep it and add a floor `p_i = 0.5 · power_i/Σpower + 0.5/N`, so a
  weak nearby light is not starved.
- GL forward stays at 8 (it is the fallback path); the node's limit lifts only when the Metal path is default (1.8).

### 6.11 Row 2.7: settle motion blur

`MTLInstanceAccelerationStructureDescriptor`: set `instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeMotion`,
`motionTransformCount = 2 × instances`, per instance `motionTransformsStartIndex`, `motionStartTime = 0`,
`motionEndTime = 1`. Ray: `intersector.intersect(ray, tlas, mask, time)` with `time = rnd(seed)` per path; camera
origin/basis lerped with the same `time`. Shadow rays reuse the path's `time`. The engine already has previous
transforms for the post smear (motion vectors): feed those as key 0. Remove the post smear on settle when on.

### 6.12 Rows 2.5, 2.6

- 2.5 strand shadows, live: replace the binary hair hit with transmittance
  `T = exp(−k · hits)`, `hits ≤ 4` by re-tracing from each hit (`k ≈ 0.7` per strand; Yuksel and Keyser's deep
  opacity idea, counted by rays instead of layers). One ray per pixel, blue-noise rotated, resolved by the
  existing half-res mask upsample. Measure s/frame on `hair_long` before and after; budget +20 %.
- 2.6 live SSS: separable blur of the diffuse target, per channel, width `sssRadius.rgb / viewDepth` in pixels,
  11 taps, weights from Burley's profile `R(r) = (e^(−r/d) + e^(−r/3d)) / (8π d r)`; reject taps whose depth
  differs by more than the radius. Applies only where the material has subsurface > 0 (mask in the diffuse
  target's alpha).

### 6.13 Rows 4.4, 4.5

- 4.4 first cut needs no new code path: with the glass lobe in, a refracted path that reaches an emissive mesh or
  the sky already carries the caustic; it is just noisy. Raise the Walter weight clamp from 4 to 16 behind a
  switch and measure noise vs bias on the caustic cases. Second cut (manifold next-event, Zeltner 2020) only if
  the first cannot reach 0.08.
- 4.5: tessellate at BLAS build only. Per triangle, split count from `edge_px / 2` at the bench camera, capped so
  the scene stays under 2 M triangles; displace along the interpolated normal by the height map; raster keeps the
  base mesh + POM (6.9), so both paths share one height source.

### 6.14 Rows 3.1, 3.3 (no sweep needed)

- 3.1: copy `GlslBackend`'s IR walk into `MslBackend`; differences in one table: `vec2/3/4 → float2/3/4`,
  `mat3 → float3x3`, `mod(a,b) → a − b·floor(a/b)` (MSL `fmod` truncates), `mix/fract` same, `atan(y,x) → atan2`,
  `texture(s, uv) → t.sample(s, uv)`, uniforms into one struct. Test: every Field Pixel preset rendered by both,
  max difference ≤ 1/255.
- 3.3: built-ins as plain Field functions over `P`: `noise3`, `noise4`, `fbm(P, octaves, lacunarity, gain)`,
  `voronoi3` (F1, F2, cell id), `wave`, `brick`, `checker`, `gradient`. Hash: PCG3D (Jarzynski and Olano 2020),
  integer-only, identical on GLSL and MSL. Verify by mean, variance and power spectrum slope in `tools/realism`.

### 6.15 Row 1.4: R1 on B4

Profile first (`INFINITE_ENGINE_PROF=1`): if CPU encode time dominates, sort runs by (pipeline, mesh), bind once per
mesh, and use one instanced draw per mesh with a per-instance buffer (most of B4's objects share meshes). Only if
that is not enough, `MTLIndirectCommandBuffer`. Gate: self-test R1 ≥ 1.5× on B4 m/l.
