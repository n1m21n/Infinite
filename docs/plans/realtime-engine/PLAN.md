# Real-time engine: the one plan (2026-11-11)

Everything discussed for the engine, in one ordered list. The detail lives in three files; this file only orders
them and says where Infinite's script nodes make each step smaller.

| File | Holds |
|---|---|
| `README.md` | goal, owner survey, baselines, R0–R5 results, gates per GPU tier |
| `DESIGN.md` | the three paths (raster, live, settle), shape of the engine, steps 1–7 (speed, consistency), §6 look gaps G1–G10 vs Cycles |
| `REALISM.md` | real objects and scenes, scanned-map measurements, the shoe photo, gaps R1–R18, procedural textures (R2), Blender's shader menus node by node (§9) |
| `docs/plans/fur/README.md` | Blocks A–C (fur, hair BSDF, SSS, translucency) |

```
 Phase 0  close Block C ─────────────── fur/hair/SSS committed
 Phase 1  one engine, three paths ───── same shading code, fast upload, denoised settle, toggle, demo
 Phase 2  look fixes ────────────────── filtered normals, POM, lights > 8, strand shadows, live SSS, HDRI, blur
 Phase 3  materials as code ─────────── Field material domain → procedural per hit → layers/masks → recipes
 Phase 4  renderer features ─────────── volumes, light shapes/IES, sky, caustics, holdout
 Phase 5  worlds and content ────────── weathering, cloth/paint, scanned sets, vegetation, cities, photo cases
```

---

## 1. The edge: script nodes

Blender builds looks from hundreds of small nodes. Infinite already has languages that run on the GPU, and they
reach every stage of a patch:

| Script node | Language | Runs | What it gives the engine |
|---|---|---|---|
| Field Pixel | Field | per pixel, GPU (GLSL backend) | images, masks, procedural maps |
| Field Modifier | Field | per vertex / point, VM or GLSL | attributes: curvature, wetness, dirt, per-island random |
| Field Primitive | Field | per element | points and scatter sources |
| Field Graph | Field | once, at edit time | builds node chains from code: recipe presets |
| Formula, Shadertoy | GLSL | per pixel | pasted or hand-written image shaders |
| Sketch | JavaScript (p5-style, quickjs) | per frame | 2D drawing: decals, labels, stitch patterns, masks |
| Sketch 3D | JavaScript | per frame | geometry by rules: buildings, props, scatter layouts |
| three.js (not in Infinite) | JavaScript | — | Sketch 3D is the JS geometry route today; a three.js-style scene/material API on top of Sketch 3D (familiar calls → Infinite geometry + Material nodes) is a new mode: **owner**, design in `designs/3.9-threejs-style-api.md` |

Why this beats a node pile, in numbers that matter on an 8 GB M2:

- **One function instead of N images.** A Blender-style chain (noise → ramp → mix → bump) is 4 nodes; baked, that
  is 4 × 1024² RGBA images (16 MB) and 4 passes. As one compiled Field function it is zero images and one call per
  hit, sharp at any zoom, no UV seams.
- **Music for free.** Every `param` in a Field kernel is a modulatable knob. A material that breathes with the kick
  is a cable, not a rebake. Blender cannot do this live.
- **One language everywhere.** The same Field code shapes sound, moves vertices, draws pixels and (Phase 3) shades
  surfaces. Masks written by a Field Modifier are read by name in a Field material.
- **Agents write it well.** A 20-line kernel is easier for the chat assistant to write, check and fix than a
  40-node graph; the compiler is the validator (agent-collab plan).
- **Recipes as code.** Wood, marble, rust, leather ship as Field Graph presets that emit editable node chains, or
  as Field material presets: one file each, diffable, A/B-able.

Rules for using them:

| Rule | Why |
|---|---|
| Field is the per-hit material language; it gains an MSL backend so live and settle run the same code as raster | one source, three paths (DESIGN §3) |
| GLSL (Formula, Shadertoy) stays an image source; a "GLSL material" waits until a GLSL→MSL route passes a licence check | no translator in the tree today |
| Sketch and Sketch 3D feed images and geometry; they never run per hit | JS is CPU; 3D and video stay on the GPU |
| Nodes keep working without code: lowering (R2) turns a plain node chain into Field code behind the scenes | users who never type still get the speed |

---

## 2. The order

Each row keeps the gate and approval from the file it comes from. **fix** and **enh** need no approval;
**owner** rows start with a one-page design shown to the owner. A/B against `tools/blender-ab/baseline.json`
every step; keep a change only if it measures better.

### Phase 0: close Block C

| # | Step | Source | Gate | Kind |
|---|---|---|---|---|
| 0.1 | Full sweep (raster, live, settle) shows no regressions | fur README | ≤ 0.005 FLIP worse on any case | — |
| 0.2 | Baselines, A/B README sections, tracker rows C8–C11 | fur README | — | — |
| 0.3 | Commit `feature/render-gap-c` | — | owner's word | — |

### Phase 1: one engine, three paths

| # | Step | Source | Gate | Kind |
|---|---|---|---|---|
| 1.1 | Shading headers as the only source; delete per-site copies | DESIGN 1 | sweep unchanged | fix |
| 1.2 | Failed pipeline = error on the node | DESIGN 2 | broken header fails the harness | fix |
| 1.3 | Material record struct (replaces spare-lane packing) | DESIGN 3 | sweep unchanged | fix |
| 1.4 | Hash-gated upload, refit on transform-only change; many-object rays (C18) | DESIGN 4, README 3a | live boat ≤ 2 s/frame; R1, R2 on B4 | fix |
| 1.5 | Settle denoiser + adaptive stop | DESIGN 5, README 4 | settle 9/9 ≤ 0.05 at ≤ 2.5 s/frame | enh |
| 1.6 | Shared BSDF fixes for spot and materials | README 5 | settle ≤ 0.05 | fix |
| 1.7 | Preview export on the live path | DESIGN 6 | 1-minute film ≤ 15 min | enh |
| 1.8 | Engine toggle on Render 3D; demo patch reacting through Audio Analyze | README 6 | Milestone 1 demo | **owner** |
| 1.9 | Vulkan backend on the same RHI | DESIGN 7 | Linux/Windows within 0.01 of Metal | enh |

### Phase 2: look fixes (no new UI)

| # | Step | Source | Gate | Kind |
|---|---|---|---|---|
| 2.1 | Filtered normals (Toksvig/LEAN) + specular AA | REALISM R3 | `tex_fabric_far` better far, same near | fix |
| 2.2 | POM in raster and live | REALISM R4 | `tex_relief_grazing` better | fix |
| 2.3 | Lights beyond 8 (light list + sampling) | REALISM R14 | `lights_many`, `city_night` at the path gates | fix |
| 2.4 | HDRI importance sampling if the check needs it | DESIGN G9 | hard-sun HDRI settle ≤ 0.05 | fix |
| 2.5 | Strand shadows | DESIGN G4 | `hair_long`, `bed_fur_close` better | fix |
| 2.6 | Screen-space SSS colour in live | DESIGN G5 | `mat_subsurface` live ≤ 0.12 | enh |
| 2.7 | True motion blur in settle | DESIGN G2 | `motion_blur_shadow` settle ≤ 0.05 | enh |
| 2.8 | Thin film in Metal | DESIGN G10 | `mat_thinfilm` better | fix |
| 2.9 | Weak live cases: `tex_breakup`, `tex_detail`, `mat_sheen`, `emission_light` | live sweep 2026-11-11 | each better, none worse | fix |

### Phase 3: materials as code (the script edge)

| # | Step | Source | Gate | Kind |
|---|---|---|---|---|
| 3.1 | Field **MSL backend** (beside GlslBackend) | REALISM R2 | every Field Pixel preset: MSL = GLSL within 1/255 | enh |
| 3.2 | Field **material domain**: inputs P, N, UV, tangent, curvature, AO, facing/Fresnel, backfacing, per-object and per-island random, named attributes, time; outputs base, rough, metal, height, normal, mask, emission | REALISM R2, §9 | a hand-written material = the same Material set by knobs within 0.01 | **owner** (new domain) |
| 3.3 | Built-in library: noise3/4, fbm, multifractal, voronoi3 (all features), wave, magic, brick, checker, gradient, gabor, colour ramp, map range, blend modes, bump | REALISM R2 | statistics within 5 % of Cycles per generator | enh |
| 3.4 | **Lowering**: an all-procedural chain into a Material (Noise, Texture, Color Ramp, Blend, adjustments) compiles to one Field material; else bake as today | REALISM R2 | lowered = baked within 0.01 at bake resolution | enh |
| 3.5 | Space switch on Noise and Texture (UV / object / world); Gabor type; cell-colour and mortar outputs | REALISM R2, §9 | — | **owner** (new modes) |
| 3.6 | **Layers + masks** (Mix Shader): Material takes a second Material and a mask; mask from any image, Field material or attribute | REALISM R1, DESIGN G6 | layer cases at the path gates; shoe photo case | **owner** |
| 3.7 | Recipes as Field Graph / Field material presets: wood, marble, cracked mud, leather, rust, brushed metal, fabric, brick | REALISM R2 | each by statistics within 5 % of its Cycles setup | content |
| 3.8 | Holdout flag, blackbody emission | REALISM §9 | one case each | **owner** |

### Phase 4: renderer features

| # | Step | Source | Gate | Kind |
|---|---|---|---|---|
| 4.1 | Volumes: homogeneous in settle, then live/raster, then grids (Field Pixel 3D density) | DESIGN G1 | three volume cases at the path gates | shaped fog (falloff) + raster fog + multi-scatter gain done 2026-11-19; settle haze 0.062 and live shaft/haze over gate; grids open |
| 4.2 | Light shapes, IES profiles, light linking | DESIGN G8 | one case each | shapes + spread done 2026-11-19 (light_disc, light_spread); IES done 2026-11-19 (light_ies); linking + shadow linking done 2026-11-19 (self-test, R/L/T) |
| 4.3 | Sky + atmosphere | REALISM R15 | sky cases ≤ 0.05 settle | **owner** |
| 4.4 | Caustics in settle | DESIGN G3 | caustic cases ≤ 0.08 | done 2026-11-19: photon pass into a hash grid (caustic_glass_sphere .045, caustic_pool .076); live/raster none by design |
| 4.5 | Micro-displacement in settle | REALISM R4 | `tex_perforated` settle ≤ 0.05 | done (built as old row 4.3: bump map tessellated into the BLAS); re-measured 2026-11-19 settle 0.031 |

### Phase 5: worlds and content

| # | Step | Source | Gate | Kind |
|---|---|---|---|---|
| 5.1 | Weathering presets on layers: dust in cavities, rust at edges, wet below a line (Field Modifier writes masks) | REALISM R9 | one case each | done 2026-11-19: Rust + Wet Material recipes join Edge Wear / Dirt / Dust (row 4.5); weather_rust settle .037, weather_wet .037; snow and moss open |
| 5.2 | Fibre sheen, car-paint flakes | REALISM R5, R11 | their cases | **owner** |
| 5.3 | Scanned-set drop import, albedo library, stochastic tiling | REALISM R6–R8 | drop builds the same Material as by hand | **owner** |
| 5.4 | Skin | REALISM R10 | skin case | done 2026-11-19: settle random walk now enters as Cycles' Random Walk (incoming ray refracted at the IOR through a GGX microfacet, alpha = roughness^2) instead of a cosine; skin_head settle .078 -> .047, mat_subsurface .074 -> .069; coat second lobe already matched (no-SSS .022); live .123 / raster .127 pass but keep the wrap look (no ear glow) until G5 |
| 5.5 | Vegetation and cities by rules (Sketch 3D, Field Primitive scatter, instances) | REALISM R12, R18 | memory and frame budgets | enh |
| 5.6 | Photo cases: the shoe, a hand, a sunlit room, a street at night | REALISM §2, §5 | photo match per case | content |
| 5.7 | R5–R7 after Milestone 1: per-copy colour, splats, sound through the same BVH | README 12 | go/no-go after the demo | **owner** |

---

## 3. Blocks and branches

Three blocks at a time, one branch each, commit per step, tracker at the top of the block's README.

| Block | Steps | Branch |
|---|---|---|
| C (open) | Phase 0 | `feature/render-gap-c` |
| D | 1.1–1.3 (one shading source, errors, material record) | `feature/engine-d-shading` |
| E | 1.4–1.7 (upload, denoised settle, export) | `feature/engine-e-speed` |
| F | 2.1–2.3 (filtered normals, POM, many lights) | `feature/engine-f-look` |

### Block D inventory (2026-11-11): what step 1.1 merges

The pattern to copy is `Render3DHair.h`: one source string that compiles as GLSL (forward, GL path tracer) and as
MSL (live, settle), with the includer defining a few macros (`vec3` = `float3`, out parameters). `Render3DSheen.h`
is shared by the GL sites only; Metal holds a hand copy of its tables.

| Piece | Metal (live + settle) | GL path tracer | GL forward | Copies |
|---|---|---|---|---|
| GGX distribution | `distGGX` | `ggxD` | `distributionGGX`, `distributionAnisotropic` | 3 |
| GGX visibility | `geomSchlick` | `ggxVis`, `smithG1` | `geometrySchlickGGX` | 3 (two different models) |
| Fresnel | `fresnel`, `fresnelRough` | `schlick5`, `fresnelDielectric` | `fresnelSchlick`, `fresnelSchlickRoughness` | 3 |
| GGX sampling | `ggxSampleH` (twice) | `sampleGGX` | — | 3 |
| Lobe pick + eval | `ptProbs`, `ptEval` | `lobeProbs`, `evalOpaque` | — | 2 |
| Coat | `coatSpec` | inline | inline | 3 |
| Sheen tables | hand copy | `Render3DSheen.h` | `Render3DSheen.h` | 2 |
| Light falloff | `lightFalloff` | `lightFalloff` | `lightFalloff` | 3 |
| Random, cosine dir | `pcg`/`rnd`/`cosineDir` (three libraries) | `pcgHash`/`rand`/`sampleCosineHemisphere` | — | 4 |
| Map UV, rotate, breakup | `mapRot`, `mapUv`, `brkHash`, `brkNoise` | `mapRotate`, `atlasTap` | `mapRotate`, `computeMapUv`, `brkHash`, `brkNoise` | 3 |
| Normal map frame | `mapTangent`, `mapNormalTS` | — | `getCotangentFrame`, `applyNormalMap` | 2 |
| Tonemap, sRGB | `aces`, `agxTone`, `neutralTone`, `toSrgb` | `acesFilm`, `toneDisplay`, `toSrgb` | same as PT | 3 (`Render3DTonemap.h` exists) |

New headers: `Render3DBsdf.h` (GGX, Fresnel, coat, sampling, lobe pick), `Render3DSheen.h` made MSL-ready,
`Render3DMaps.h` (UV, breakup, normal frame), `Render3DRandom.h`, `Render3DTonemap.h` extended. Each merge is its
own commit. Where two copies differ (visibility model, Fresnel form), the A/B picks the one that scores better and
both sites take it. Known differences to settle by A/B:

- Visibility: raster and live use the Schlick-k form (k = (r+1)²/8, tuned for analytic lights); both path
  tracers use height-correlated Smith (Cycles' form). First candidate: height-correlated everywhere.
- Lobe pick: settle's `ptProbs` has no coat or sheen term; the GL tracer's `lobeProbs` has both.
- `ggxSampleH` exists twice inside MetalDevice.mm itself (live reflections and settle).

Gate per commit: the whole sweep within 0.005 FLIP on every case, or better.

Phase 3 starts once 3.2's one-page design is approved; it can be written while D–F run. 1.8 (the toggle) is asked
for as soon as Block E lands.

## 4. What stays out

| Item | Why |
|---|---|
| CPU render paths | 3D and video run on the GPU; the CPU belongs to audio |
| Blender source, GPL code | superseded 2026-11-12 by OPUS_PLAN rule 12: reference reading allowed; Apache 2.0 Cycles formulas may be ported with a notice; GPL code never copied |
| Light Path tricks, Shader to RGB, Ray Portal, Raycast node | no case needs them (REALISM §9) |
| Per-hit JavaScript | JS is CPU; Sketch nodes feed images and geometry |

## 5. Testing procedure (every step)

Rule of load on the 8 GB M2: **one build tests many variants; a sweep runs only the cases a change can touch,
fastest first, inside a time budget; results merge, so nothing is rendered twice.**

| tier | when | command (options go before the subcommand) | cost |
|---|---|---|---|
| T0 | refactor that must move no pixel | `sweep.py --spp 16 --tag post run`, then `same` | ~4 min / engine |
| T1 | real change | `sweep.py --tag X --ab X --touch G1,G2 run`, then `--base post --tag X diff` | 1 to 4 min |
| T2 | block close | `sweep.py --tag post --all run` on raster, live, settle `--spp 16` | ~20 min, capped |

| piece | what it does |
|---|---|
| `INFINITE_R3D_AB=a,b` | shader switches: a candidate sits under `#ifdef R3D_AB_<NAME>` in GL forward, GL tracer and Metal (`src/nodes/Render3DAb.h`). One build, every variant. Winner: remove the `#ifdef`, keep its branch |
| `--ab a,b` | renders the sweep with those switches on |
| `--touch G` | picks cases by what their scene uses (`sweep.py pick G` lists them): direct, falloff, sun, spec, gloss, metal, coat, sheen, glass, sss, aniso, thinfilm, emission, maps, hair, hdri, motion, dof |
| `--budget MIN` | default 20; fastest cases first (from `times.json`), stops at the budget and keeps what finished |
| merge | `run` adds to `<tag>_<engine>.json`; `diff` / `same` compare only the cases of the latest run |
| `times` | per-case seconds of past runs, to size a job before starting it |

```
 0. the step's gate case exists in tools/blender-ab/cases.py (add it first, with its Cycles truth)
 1. candidate goes in behind a switch; one build (scripts/ibuild.py), self-test
 2. T0 with no switch: 0 changed (the switch plumbing moved nothing)
 3. T1 per switch on the engines it touches; keep = gate case better, no case worse by > 0.005 FLIP
 4. winners become the default in one more build; T1 again on the union of touched cases
 5. perf: live fps soak and settle s/frame where the step touches speed (README §10 routes)
 6. app: self-test + the hygiene driver for touched areas
 7. tracker row with the numbers; commit; rotate post -> pre
```

| Layer | Tool | Gate |
|---|---|---|
| Picture vs Cycles | `tools/blender-ab/harness.py --engine raster/pt/live/settle` | FLIP per case: raster ≤ 0.20, live ≤ 0.15, settle ≤ 0.05; no case worse by > 0.005 |
| Sweep + compare | `tools/blender-ab/sweep.py` (pre/post of every case, prints WORSE) | none worse |
| Procedural patterns | statistics (feature size, histogram, spectrum slope, cell sizes) vs Cycles, from `tools/realism` | within 5 % (built in Phase 3) |
| Tiling | `tile_repeat.py` | `tex_breakup` repeat score |
| Speed | engine soak (`INFINITE_ENGINETEST_SOAK`), export timing | fps tier targets, s/frame targets |
| App | self-tests (FURTEST etc.), `driver.sh`, pre-push hook | green |
| Photo match | owner photos (shoe, hand, room, street) | per-case, Phase 5 |

Baselines: `baseline.json` keys are `case` (raster), `case_pt`, `case_live`, `case_settle`; every case on every
engine is kept there, so the harness's own exit code guards all three paths.

Cases the plan's gates still need (each added by its step before the change): `tex_fabric_far`,
`tex_relief_grazing`, `lights_many`, `city_night`, `motion_blur_shadow`, `tex_perforated`, volume cases, sky cases,
caustic cases, layer cases, recipe cases.
