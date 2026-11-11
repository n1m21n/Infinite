# Real-time engine: design (C11, 2026-11-11)

**The one ordered plan across all these files: `PLAN.md`.**

Doc only, no code. It turns what the render-gap Blocks A and B (docs/plans/fur/README.md, tracker) taught us into
the engine's design. The research doc, its gates and its benchmarks stay in README.md next to this file.

## 1. What Blocks A and B taught us

| # | Lesson | Evidence | Design rule |
|---|---|---|---|
| L1 | Each look was written four times (GL forward, GL path tracer, live, settle) and drifted | sheen 13× too bright at mid roughness in one site, coat missing on settle bounces (B6) | **One shading source.** Every BSDF lives once, in a header compiled for every backend (as `Render3DSheen.h`, `Render3DHair.h` do now) |
| L2 | A shader that fails to compile falls back to a plain path without a word | the hair splice came after its first use in the GI function; live and settle printed the same picture and the A/B still passed | **A failed pipeline is an error.** The engine refuses the frame and logs it; the A/B harness fails on it (it does now) |
| L3 | Most of the export time was work done for nothing | 48 warm-up frames per film frame; carrying the GI history over gave 8.2 → 4.4 s/frame, FLIP equal | **History is part of the frame.** Temporal state (GI, reflections, TAA) carries across exported frames like in playback |
| L4 | Raster without bounce light is 2–7× too bright or too flat | interior 6.6× bright, FLIP 0.79 → 0.103 with a probe grid | **Every path has indirect light.** Raster keeps a probe GI fallback; no path ships with ambient only |
| L5 | Geometry the light model doesn't expect breaks it | fur ribbons were culled and blotched the shadow map (A4); hair needs a fibre axis, not a surface normal | **Primitives carry their kind.** A mesh says two-sided, ribbon range, fibre axis; every pass reads it (`twoSided`, `ribbonStart`, `hairFrom`) |
| L6 | Units fixed by measurement beat units fixed by reading | AgX fitted to a 512-step ramp from Blender; sheen fitted to ten sun directions | **Fit against Blender, then freeze.** Each table is measured once, stored as data, and gated by the A/B |
| L7 | Speed now hides in the per-render fixed cost | live ≈ 160 ms per render after the warm-up fix; settle ≈ 0.3 s per sample, 40 s/frame at 128 spp | **Budget per frame, not per ray.** The 0.63 s/frame gate needs the fixed cost gone and far fewer samples (section 4) |

## 2. Shape

```
 Infinite graph (GL + ImGui) ──► Scene snapshot (instances, meshes, materials, lights, time in beats)
                                         │  hash per part: only what changed is re-uploaded
                                         ▼
  ┌──────────────────────────── Engine (own target) ─────────────────────────────┐
  │  RHI: Metal ─ Vulkan  (device, buffer, texture, pipeline, accel, cmd, fence)  │
  │                                                                              │
  │  Shading source: one set of headers (BSDF, sheen, hair, coat, tonemap)       │
  │        │                         │                          │                │
  │        ▼                         ▼                          ▼                │
  │  RASTER (fallback)         LIVE (play)                 SETTLE (film, still)  │
  │  probes GI, shadow maps    G-buffer, RT shadow/refl,   path trace, same      │
  │                            half-res GI, denoise        BSDF, denoise, stop   │
  └──────────────────────────────────────┬───────────────────────────────────────┘
                                         │ IOSurface / shared memory → GL texture
                                         ▼
                                   Viewer, projector, export
```

| Part | Decision | Why |
|---|---|---|
| Backends | Metal first, Vulkan second, GL frozen as the fallback | the owner's Mac is the test bed; Vulkan reuses the RHI shape |
| Shading source | Headers in a GLSL/MSL common subset, spliced at the top of every library (MSL) or program (GLSL); Vulkan takes them via glslang | L1, and L2's ordering bug cannot happen if the splice is first by construction |
| Scene upload | Per-part hashes (instances, materials, meshes, lights); a frame re-uploads only changed parts; the acceleration structure refits on transform-only change, rebuilds on topology change | the 160 ms fixed cost is mostly re-upload and rebuild of unchanged data |
| Material record | One struct for all paths: base, metal, rough, ior, transmission, coat, sheen, SSS radius, hair (sigma, β, shift), the ribbon/hair range | L5; today it is packed into spare lanes (`shc.w`), which ran out |
| Frame state | GI reservoirs, reflection history, TAA, settle accumulation all live in a `FrameState` owned by the view, kept across exported frames, reset by a camera cut | L3 |
| Pipelines | Compiled at load on a worker; a failure is shown on the Render 3D node and blocks that path | L2 |

## 3. The three paths

| | Raster | Live | Settle |
|---|---|---|---|
| Use | no RT, safety net | playing, projector | export, stills |
| Direct light | shadow maps | RT shadows (key light), shadow maps (rest), soft sun from 4+12 probes | NEE, every light |
| Indirect | probe grid (16 on the long side, Chebyshev leak guard) | half-res ReSTIR GI, screen-space feedback | path traced, 8 bounces |
| Hair | fibre lobes averaged + multi-scatter term (gain 4) | same | full Chiang lobes, sampled |
| SSS | wrap widened by curvature × radius (gain 4), thin translucency from the back | same | random walk inside the mesh, mean free path = radius, albedo inverted from base; translucency sampled through |
| Gate (A/B FLIP vs Cycles) | ≤ 0.20 | ≤ 0.15 | ≤ 0.05 |

The live and settle paths share the scene, materials and BSDF code; raster shares the materials and the BSDF
headers. A material that changes one path's picture changes all three or none.

## 4. The export budget

Gate: a 1-minute 1080p film at 24 fps in ≤ 15 min, so 0.63 s per frame.

| Cost today | Live | Settle |
|---|---|---|
| Per frame | 4.4 s (boat) | 40 s (128 spp) |
| Fixed per render | ≈ 160 ms | — |
| Per sample | — | ≈ 0.3 s |

| Step | Live effect | Settle effect |
|---|---|---|
| Hash-gated upload + refit (section 2) | removes most of the 160 ms; frames that only move the camera pay nothing | same scene path |
| Keep `FrameState` across frames (done for live in B7) | 24 top-up frames instead of 48 | accumulation carries while the camera holds still |
| Denoise the settled frame (albedo + normal guides, à-trous on demodulated radiance) | — | 128 → 16 spp at equal FLIP is the target, 40 s → ≈ 5 s |
| Adaptive stop per tile on variance | — | quiet tiles stop at 4–8 spp |
| Preview export = live picture, settled a few samples | the 0.63 s gate | — |
| Final export = settle, denoised, adaptive | — | the ≤ 2.5 s/frame gate (≈ 1 h per minute) |

Two exports, as section 10 of README.md set: preview meets 15 min, final meets 1 h. Faster rays alone cannot
close 14× on an M2 (no RT hardware), so neither plan depends on them; an M3+ only makes both faster.

## 5. Order of work

| # | Step | Gate |
|---|---|---|
| 1 | Shading headers as the only source; delete the per-site copies | A/B sweep unchanged (no picture changes) |
| 2 | Failed pipeline = error on the node | a broken header fails the harness, not the picture |
| 3 | Material record struct (replaces the spare-lane packing) | sweep unchanged |
| 4 | Hash-gated upload, refit on transform-only change | live boat ≤ 2 s/frame, FLIP unchanged |
| 5 | Settle denoiser + adaptive stop | settle 9/9 cases ≤ 0.05 at ≤ 2.5 s/frame |
| 6 | Preview export on the live path | 1-minute film ≤ 15 min |
| 7 | Vulkan backend on the same RHI | Linux/Windows A/B within 0.01 of Metal |

Each step is A/B-gated against `tools/blender-ab/baseline.json` and kept only if it measures better or equal.

## 6. Closing the look gap to Cycles (after steps 1–7)

Steps 1–7 make the engine fast and keep the three paths consistent; they add little new look. The gaps below are
what still separates a settled frame from Cycles, and live from Eevee-class real time. Each one is a step with its
own A/B case, and like every step it is kept only if it measures better (`tools/blender-ab`, baseline.json).

Clean room holds for every item: the approach comes from published papers, talks and the Blender manual (what a
feature does and its parameters), measured against Cycles renders; never from Cycles or other GPL source.

### 6.1 The gaps

| # | Gap | Today | What Cycles does (manual / measured) | Effect on the picture |
|---|---|---|---|---|
| G1 | Volumes in the scene | Volume node is a post pass over the image: no shadows from geometry, no light shafts, settle does not scatter in it | Volume Scatter / Absorption / Principled Volume on a mesh or the world; lights scatter in it, objects shadow it | fog, god rays, smoke, underwater; the biggest look gap in moody scenes |
| G2 | Motion blur | Render 3D `motionBlur` + `motionShutter`: a post pass that smears along a per-pixel velocity (Render3DPost.cpp); `motion_blur` live 0.060 | shutter open/close sampled per ray: shadows, reflections and edges blur too | post blur is right for most shots; it fails on blurred shadows, reflections and objects crossing each other |
| G3 | Caustics | settle finds them only through lucky bounces (noisy, mostly missing); live/raster none | path traced with manifold next-event for caustics from glass/water onto shadow receivers | light through glass, pools, water on a floor |
| G4 | Strand self-shadowing (live, raster) | ribbons lit by a flat 0.4 visibility; settle is right | full traced shadows between strands | fur and hair look flat in live and raster |
| G5 | Live / raster subsurface colour | wrap widened by curvature; pale and grey next to Cycles (live 0.165) | random walk: red travels farther, so skin and wax glow warm in thin parts and at shadow edges | skin, wax, marble, fruit |
| G6 | Material depth | one fixed material (base, metal, rough, coat, sheen, SSS, transmission, hair, anisotropy) + texture maps | shader node graphs: any mix of BSDFs, procedural textures, layered materials | stylised and layered looks (rust over paint, wet over dry) |
| G7 | Settle noise | 128 spp, grain left on SSS and glossy | OpenImageDenoise / OptiX with albedo + normal guides | covered by step 5 |
| G8 | Light shapes and control | sun, point, spot, area (rectangle); emissive meshes as lights in settle | disc/ellipse area, spread, IES profiles, light linking, shadow linking | product shots, interior lighting design |
| G9 | World light sampling | World `sunThreshold` pulls the HDRI's sun out as a light; the rest of the map is sampled as is (`world_hdri` live 0.084) | importance-sampled world map | noise from bright HDRI areas below the threshold (windows, sky gradients) |
| G10 | Thin film, iridescence | Material `iridescence` (thickness, IOR) in GL; the Metal paths do not read it (`mat_thinfilm` live 0.100) | Principled thin film | soap bubbles, oil slicks, coated lenses |

### 6.2 How to tackle each

**G1 Volumes (largest gap first).**

| Path | Approach | Notes |
|---|---|---|
| Settle | Delta (Woodcock) tracking through a bounded density (a mesh box, or the world as homogeneous fog); next-event estimation from every scatter point with ratio-tracked transmittance; Henyey–Greenstein phase with `g` | heterogeneous density from the same noise the Volume node uses, so the look carries over |
| Live | Froxel grid (camera frustum voxels, ~160×90×64): inject density + lighting per froxel with the RT shadow from the key light and the shadow map for the rest, integrate front to back, temporal reprojection | the standard real-time route (Wronski 2014, Hillaire 2015); half-res, ~2 ms on M2 is the budget |
| Raster | Same froxel grid with shadow-map lighting | shares the live shaders |
| Graph | The Volume node gains a 3D output that Scene 3D takes as a volume object (bounds + density + colour + anisotropy), keeping the 2D post mode for non-3D images | **needs owner approval** (new input/mode on an existing node) |
| A/B | cases `vol_fog_sun` (god rays through a window), `vol_smoke` (lit plume), `vol_world_haze` | Blender: Principled Volume, same density from a baked VDB or a box with noise |
| Gate | settle ≤ 0.05, live ≤ 0.15 | |

**G2 Motion blur.** The post blur stays (cheap, right for most shots). Settle adds true blur on top:

| Path | Approach | Notes |
|---|---|---|
| Settle | Each sample draws a time in the shutter; instances carry the previous frame's transform and rays use the transform at their time (Metal motion acceleration structures where available, else interpolate per ray) | blurred shadows and reflections; deforming meshes need per-vertex previous positions as a second step |
| Live / raster | keep the post pass; add tile-max velocity (McGuire 2012) so fast objects blur past their own silhouette | |
| A/B | `motion_blur` (exists), add `motion_blur_shadow` (a fast ball over a sunlit floor) | |

**G3 Caustics.**

| Path | Approach |
|---|---|
| Settle | First: let receivers marked as caustic-catchers use manifold next-event estimation (Zeltner 2020 specular manifold sampling) for one refractive/reflective interface; this is the published method for the case Cycles' "caustics" flags target. Fallback if too slow: photon pass into a hash grid, gathered at the first diffuse hit |
| Live | Out of scope; a projected caustic texture for water planes only (screen-space, from the water normal) |
| A/B | `caustic_glass_sphere` on a white floor under a sun; `caustic_pool` |

**G4 Strand self-shadowing.**

| Path | Approach |
|---|---|
| Live | Trace the key-light shadow ray for ribbon pixels too (today they take the flat 0.4); add a deep opacity map for the other lights (Kim & Neumann 2001 opacity shadow maps, 8 slices) |
| Raster | Deep opacity map only |
| A/B | `hair_long`, `bed_fur_close`: gate is FLIP better than today in both |

**G5 Live subsurface colour.**

| Path | Approach |
|---|---|
| Live | Screen-space separable SSS (Jimenez 2015): blur the diffuse irradiance in screen space with a per-channel profile from the radius, depth-aware; plus thin-part transmittance from the shadow map depth (light travel through the object) |
| Raster | Same pass |
| A/B | `mat_subsurface` (0.165 today), add `mat_subsurface_backlit` (thin ear-like shape against the sun) |

**G6 Material depth.** Expanded in REALISM.md (R1 layers + masks, R2 Field materials), grounded in measured scans and real objects and scenes. Two routes; pick one with the owner (it is a new feature either way):

| Route | What | Cost |
|---|---|---|
| Layer stack on Material | a second material blended by a mask (texture, curvature, AO, height); covers rust, wet, dust | small: one more record in the material struct, every path reads two and blends |
| Field materials | a Field program (the language already compiles to GLSL) outputs the material's inputs per hit; MSL backend added to the Field compiler | large, but every procedural texture becomes free; fits Infinite's identity |

Recommended: the layer stack first (fast, measurable against Cycles' Mix Shader), Field materials as the long goal.

**G7** is step 5. **G8** in order: disc area light (same sampling as rectangle with a disc), spread (cone limit on
area emission), IES (1D/2D table per light, sampled by angle), light linking last (needs a per-instance mask in the
TLAS: Metal intersection masks). **G9**: the sun is already extracted (`sunThreshold`); for the rest build a 2D CDF over the map (luminance × sin θ)
and use it for NEE and MIS in settle; gate on `world_hdri` and an indoor HDRI with bright windows. **G10**: move the GL thin-film Fresnel into the shared BSDF header (step 1) so live and settle read
`iridescence` too.

### 6.3 Order and gates

| # | Step | Gate | Approval |
|---|---|---|---|
| 8 | G9 check + world importance sampling if needed | `world_hdri` and hard-sun HDRI settle ≤ 0.05 | no (fix) |
| 9 | G4 strand shadows | `hair_long`, `bed_fur_close` better in live and raster | no (fix) |
| 10 | G5 screen-space SSS | `mat_subsurface` live ≤ 0.12, backlit case ≤ 0.15 | no (enhancement) |
| 11 | G2 true blur in settle | `motion_blur_shadow` settle ≤ 0.05 | no (existing shutter param) |
| 12 | G1 volumes, settle then live/raster | three volume cases at the path gates | Volume 3D output: owner |
| 13 | G8 light shapes, IES, linking | one case each | new light params: owner |
| 14 | G10 thin film in Metal | `mat_thinfilm` live and settle better than today | no (existing param) |
| 15 | G3 caustics (settle) | both caustic cases ≤ 0.08 settle | no |
| 16 | G6 layer stack, then Field materials | Mix Shader cases ≤ path gates | owner (new feature) |

Steps 8–10 are pure fixes and can follow step 5 directly; 11 onward each start with a one-page design shown to the
owner. Expected result after step 16: settle matches Cycles on everything Infinite users build (stills and films);
live sits at Eevee level with volumes, blur and SSS; raster stays the safety net at ≤ 0.20.

## 7. What stays out

| Item | Why |
|---|---|
| New nodes or a visible engine toggle | need owner approval (README.md section 12, step 6) |
| CPU rendering paths | video and 3D render on the GPU; the CPU belongs to audio |
| Gaussian splats, sound propagation | R7, after Milestone 1 |
