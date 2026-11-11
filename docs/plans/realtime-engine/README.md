# Real-time engine: live worlds that react to music

**The one ordered plan across all these files: `PLAN.md`.**

Status (2026-11-10, end of day): **R0–R4 built headless, plus R5 step 1; Milestone 1 not closed.** The engine
runs behind `INFINITE_LIVE_ENGINE` (no visible toggle yet; it waits for owner approval). No Music Rig node
(dropped 2026-11-10, C17). Gate scoreboard: section 10, "Where the gates stand". Open: film export time and
quality, the real-patch bridge (B4), and the toggle.
Started 2026-11-10; rewritten the same day around the owner's survey answers.
Related: `docs/plans/3d-realism/research-brief.md` (commit e32f8eb9).

```
          ONE SCENE · ONE CLOCK (the beat) · ONE LANGUAGE (Field)
                              │
         ┌────────────────────┼─────────────────────┐
         ▼                    ▼                     ▼
   LIVE PICTURE          MUSIC DRIVES            SAME VIEW SETTLES
   render quality,       objects, lights,        to path traced for
   while playing         camera, materials       films and stills
```

---

## 1. Goal

**A 3D world, with objects in it reacting to music live, that looks as good as a render while it plays.**
Films and stills come from the same engine: hold the camera still, or export, and the same view settles to a
path-traced picture.

### Owner's answers (survey, 2026-11-10)

| Question | Answer | What it decides |
|---|---|---|
| Who first | 1. own films, 2. live performers / VJs, 3. 3D artists | Live quality and film export both matter; performers set the fps bar |
| First demo | A 3D world, objects inside reacting to music live, no compromise on render quality | Milestone 1 is a live, photoreal, music-reactive scene |
| Live vs final | **Live wins** | Every stage is judged live first; final quality is the same view settling |
| World source | All four: rules, real places, imported, hand-built | One scene format that takes all four (section 8) |

### Pillars

| Pillar | What the user gets |
|---|---|
| Render quality, live | Ray-traced shadows, reflections and bounce light at 30+ fps, not a preview |
| Music is the clock | Objects, lights, materials and camera driven by beats, notes and sound bands. Every frame is f(beat, seed), so films render frame-exact to the track |
| Settles to final | Still camera or Export → progressive path tracing on the same scene, checked against Cycles |
| Any world source | Imported, hand-built, rule-generated and scanned content in one scene |
| Low spec first | Built and tuned on an M2 8 GB; bigger machines just go further |
| Later: space shapes sound | Sound heard through the world's geometry |

### The final deliverable

> On the M2 8 GB: open one patch. A world with objects reacting to the music plays live at 30 fps
> and looks rendered: ray-traced shadows, reflections, bounce light. Press Export: a 1-minute 1080p
> film from the same scene, settled to path-traced quality, in ≤15 minutes. The same patch works on
> Windows and Linux, and runs bigger worlds on bigger GPUs.

### What ships after each stage

| Stage | What the owner sees in the app | Go/no-go proof |
|---|---|---|
| R0 | A live-render bench and a reference scene | Baseline recorded (section 6) |
| R1 | Render 3D draws through Metal; same picture, faster, more lights | fps + A/B board vs today's Render 3D |
| R2 | Ray-traced shadows and reflections + denoiser, live | fps, FLIP vs path-traced reference |
| R3 | Real-time bounce light (GI) + a music-reactive rig | **Milestone 1: the Discord demo** |
| R4 | Export / still camera settles to path traced | FLIP vs Cycles, film export time |
| R5 | Scale: instancing, LOD, rule-built worlds (Field + nodes) | Instance count, memory, regenerate time |
| R6 | Vulkan: Windows/Linux, RTX + no-RT fallback | All tests on both |
| R7 | Real places (splats, OpenStreetMap) and sound through space | Per-feature benches |

## 2. Problem

| # | Problem today | Evidence |
|---|---|---|
| P1 | Live Render 3D is raster with fakes: shadow maps, SSAO, no bounce light, 3 lights | `Render3DNode`, the 3d-realism brief's "missing" list |
| P2 | The path tracer can't use RT hardware and is too slow to be live | `Render3DPathTrace.cpp`: GLSL 330 fragment shader, BVH walked by hand via `samplerBuffer`; 21.85 ms per sample at 512² |
| P3 | No denoiser or temporal reuse | One sample per pass, accumulated |
| P4 | No compute shaders on macOS | GL 4.1 core: no GPU-driven culling, mesh shaders, bindless, async upload or ray queries |
| P5 | Music → 3D is wiring per parameter | Every reactive object is hand-patched; no "react to the music" rig |
| P6 | Scale ceiling | Draw-call raster, CPU-built scenes |
| P7 | Field can't build worlds yet | Element = per point/vertex; GPU path refuses state, reduce, `.at()`, N; no instance or volume domain; no variable output |
| P8 | No scanned assets | No Gaussian splats, no OpenStreetMap/USD import |
| P9 | No memory budget across the app | GPU, audio, VST plugins and UI share 8 GB with no cap or drop order |
| P10 | Blender and Unreal don't fit this | Cycles is great but minutes per frame; Unreal is live but not music-native, heavy, and its look lives outside Infinite |

**Cycles stays the reference for final quality.** The aim is not to beat Cycles; it's Cycles-like light
while playing, and a still/export that converges to match it.

## 3. Scope

| In | Out (for now) |
|---|---|
| Thin RHI of our own: Metal first, Vulkan second | Rewriting the GL UI, ImGui or the node canvas |
| GL ↔ engine texture sharing (IOSurface on Mac, external memory on Vulkan) | DirectX 12 |
| Live hybrid renderer: raster G-buffer + RT shadows, reflections, ReSTIR GI + denoiser + upscaler | Full live path tracing at native res |
| Progressive path tracing on the same scene for still / export (port of today's tracer) | Rigging, hair, fluids (the 3d-realism brief) |
| Music-reactive rig: beats, notes, bands → transforms, lights, materials, camera | VR/XR output |
| Sky, atmosphere, volumetric fog; physical camera (DOF, motion blur, EV) | Weather simulation |
| One scene format for imported (glTF/USD), hand-built, rule-built and scanned content | Training Gaussian splats inside Infinite |
| Instancing, GPU culling, cluster LOD, streamed tiles; Field instance domain + city nodes | Networked worlds; a game runtime |
| No-RT fallback (compute BVH traversal) for GPUs without ray tracing | — |
| App-wide GPU memory cap with a drop order | — |
| Pipeline caches so live play never hitches on a shader compile | — |
| Later: sound through space (listener node, binaural first) | Wave-based acoustics live |
| Self-tests, benches, Blender A/B for every stage | — |

## 4. Existing constraints

| Constraint | Source | Effect |
|---|---|---|
| Audio thread is sacred; CPU belongs to audio | memory: video on GPU | All render work on the GPU; the engine must never stall the audio thread |
| Dev machine: M2, 8 GB unified, no hardware RT | memory | The floor we tune for; Metal RT works on M2 but intersects on shader cores |
| One build at a time via `scripts/ibuild.py` | memory | Engine is its own CMake target |
| Every 3D step A/B'd against headless Blender | memory | Each stage ships a scene, a FLIP score and a pass threshold |
| New nodes/panels need owner approval | memory | This doc is the gate; node names listed in section 8 |
| UI: short labels, no hint text, no chevrons, grounded ImGui | memory | Engine controls live in Render 3D / 3D View faces |
| Windows and Linux ship every release | releases | GL renderer stays as fallback until Vulkan lands |
| Patches evolve; no bit-exact reopen | memory | Old patches open and render; the picture may improve |
| Field's settled rules | memory: field language | New domains follow bare names, state as delay-sugar, rate inferred |

## 5. Hypothesis

| # | Hypothesis | Proved or killed by |
|---|---|---|
| H1 | Through Metal, today's Render 3D raster runs **≥1.5× faster** with the same picture, and lifts the 3-light limit | R0 bench before/after, A/B board |
| H2 | RT shadows + reflections at 1 ray per pixel, traced at 540p, denoised and upscaled to 1080p, fit in **≤12 ms on M2** for the reference scene | GPU timer per pass |
| H3 | ReSTIR GI + denoise gets live bounce light within **FLIP ≤ 0.15 of a 1024-spp Cycles frame**, at ≥30 fps | FLIP boards on 3 scenes (room, street corner, objects on a stage) |
| H4 | A music-reactive rig (beat, notes, bands → any scene parameter) adds **≤1 ms** per frame and zero audio xruns | 10-minute run with music, xrun counter |
| H5 | Moving the path tracer to Metal compute + Metal RT makes still/export **≥4× faster per sample** | `INFINITE_PTBENCH` ms_per_sample |
| H6 | Worlds come from "graph + Field": city nodes give the structure, Field kernels the per-instance rules; ≥1M instances in ≤1.5 GB, regenerating in ≤100 ms | Instance bench |
| H7 | What runs well on the M2 scales up with no new code on RTX / M-Max | Same benches on other hardware when available |

**Why low spec is the advantage:**

| M2 limit | Forced method | Payoff on big hardware |
|---|---|---|
| 8 GB memory | Rules, compression, streaming | Bigger worlds |
| No RT cores | Few rays, heavy reuse (ReSTIR, temporal) | RT cores buy resolution and bounces |
| CPU shared with audio | GPU-driven everything | Scales with the GPU |
| Weak GPU | Half-res tracing + upscale | Native res |

## 6. Baseline (recorded once, 2026-11-10; not re-measured before implementation)

Owner's M2 8 GB, `build-dev` on branch `fix/timeline-loop-bar1-rewind`.

| Measure | Value | Source |
|---|---|---|
| Path tracer, Cornell 512², 8,252 tris | **21.85 ms per sample** (≈ 45.8 spp/s) | `INFINITE_PTBENCH=1` |
| Derived: 1080p | ≈ 173 ms per sample | ×7.9 pixels |
| Derived: 256 spp, 1080p | ≈ 44 s per frame | — |
| Derived: 1-minute film, 24 fps, 256 spp, 1080p | ≈ 17.6 hours | — |
| Live Render 3D raster | **B4 exists** (`INFINITE_BENCH_B4*`, `scripts/bench/run_all.sh --only B4`): 1080p, 3 lights, shadows, HDRI, ocean + instances + metal + glass. Sep 2026 rows in `bench/baselines/m2-8gb.jsonl` (scale=m, shadow 2048: p50 6.97 ms CPU frame, 539k tris). To re-record on the current commit | `Startup.cpp`, `BenchScenes.cpp` |
| Live features | shadow maps, SSAO, AgX tonemap, 3 lights; no GI, no RT, no denoiser | code |
| Music → 3D | per-parameter wiring only | — |
| Instancing | per draw call | code |
| Field world generation | not possible | P7 |
| Graphics API | OpenGL 4.1 core (Mac), GL elsewhere | code |

**Gap (corrected 2026-11-10):** B4 already covers live Render 3D, so R0 does not write a new bench. Its
`frame_ms` is CPU-side and `GL_TIME_ELAPSED` stalls Apple GL, so R0 (1) re-records B4 on the current commit
with `glFinish`-bracketed frames, so GPU time counts, and per-pass cost from toggling shadows / AO / HDR,
and (2) adds the one thing B4 lacks: a **music-reactive reference scene** (objects driven by beat) for R3.
Recorded **once**, before R1 starts, then frozen like the row above.

## 7. Success metrics

Live is judged first. Each stage hits its row before the next starts (keep-only-if-better).

| Stage | Metric | Target on M2 8 GB |
|---|---|---|
| R0 | Live baseline recorded | reference scene: fps, GPU ms per pass, memory |
| R1 Metal raster | Same picture as today | FLIP ≤ 0.02 vs GL Render 3D |
| R1 | Speed | ≥ 1.5× R0 fps; ≥ 8 lights |
| R2 RT shadows + reflections | Cost | ≤ 12 ms GPU (540p traced → 1080p) |
| R2 | Quality | FLIP ≤ 0.12 vs path-traced reference for shadows/reflections |
| R3 Live GI + music reaction | fps | **≥ 30 fps sustained over 10 minutes** (thermals), 1080p output |
| R3 | Quality | FLIP ≤ 0.15 vs 1024-spp Cycles on 3 scenes |
| R3 | Audio | 0 xruns in 10 min at a 128-sample buffer; rig ≤ 1 ms |
| R3 | Hitches | 0 frames over 50 ms (shader compiles, uploads) |
| R4 Settle / export | Per sample | ≤ 5.5 ms Cornell 512² (≥ 4× baseline) |
| R4 | Final quality | FLIP ≤ 0.05 vs Cycles after ≤ 10 s still at 1080p |
| R4 | Film | 1-minute 1080p film ≤ 15 min (baseline ≈ 17.6 h) |
| R5 Scale | Instances | ≥ 1M at ≥ 30 fps; ≤ 1.5 GB; regenerate ≤ 100 ms |
| R6 Vulkan | Parity | R1–R5 tests pass on Windows and Linux, RTX and no-RT |
| R7 | Splats / sound | targets set when R7 is planned |

**Milestone 1 = R0–R3.** The Discord demo: a world with objects reacting to music, at render quality, live.

---

## 8. Methodology (preview)

```
┌─ Infinite app (GL + ImGui, unchanged) ─────────────────────────────────┐
│ nodes · Field compiler · transport (beats) · audio engine (CPU)        │
│ modulators: Audio Analyze · Envelope · LFO → scene parameters          │
└──────┬─────────────────────────────────────────────────────▲──────────┘
       │ scene + frame time in beats                         │ shared texture
       ▼                                                     │ (IOSurface)
┌─ Engine (own CMake target) ────────────────────────────────┴──────────┐
│ RHI: Metal ── Vulkan                                                   │
│ Scene: instances · meshes · materials · lights · splats (later)        │
│ LIVE:  G-buffer → RT shadows/reflections → ReSTIR GI → denoise → upscale│
│ STILL/EXPORT: same scene → progressive path tracing → settle           │
└────────────────────────────────────────────────────────────────────────┘
```

| Part | Method | Why |
|---|---|---|
| RHI | Own thin layer (~10 objects: device, buffer, texture, pipeline, accel struct, command list, fence) | wgpu lacks RT; big engines too heavy; thin = buildable and testable |
| Interop | IOSurface-backed texture read by GL (Syphon already uses IOSurface in-repo); Vulkan via `GL_EXT_memory_object` | Whole UI stays on GL |
| Live light | Raster G-buffer, RT shadows (1 ray), RT reflections (1 ray, rough → probe fallback), ReSTIR GI, SVGF-style denoise, temporal upscale | The standard recipe for live photoreal, sized for M2 |
| Settle | Port `Render3DPathTrace` BSDFs 1:1 onto Metal RT; same materials as live | Live and final agree |
| Music reaction | Existing nodes: Audio Analyze (onset + 8 bands), Envelope, Smooth, LFO (beat sync), Note Expression, wired as modulators into Render 3D and the 3D nodes' parameters. No new node (C17) | They already do beat, bands, notes and smoothing |
| Determinism | Frame = f(beat, seed) | Films sync frame-exact at any render speed |
| World sources | One instance format: glTF/USD import, existing 3D nodes, Field + city nodes, splats later | All four answers in one scene |
| Worlds by rules | Graph holds structure (lots → blocks → buildings); Field kernels set per-instance rules; new `instance` domain, GPU spatial grid for `.at()`, a split/emit step | Houdini's VEX + SOP split, which Infinite already mirrors |
| Memory | App-wide GPU cap; drop order LOD → splat detail → resolution | 8 GB shared with audio and plugins |

New nodes this implies (each needs approval): City/Lot/Facade split nodes, Splat import, Listener (R7).

## 9. Solution trajectories (preview)

| Route | Pros | Cons | What if |
|---|---|---|---|
| **A. Own engine, Metal first (recommended)** | Built for live + music + Field; tuned on the owner's Mac | Large; two backends | Vulkan slips → GL stays the Win/Linux fallback |
| B. Vulkan first via MoltenVK | One backend | MoltenVK RT lags; worst on the dev machine | MoltenVK RT matures → reconsider |
| C. wgpu / bgfx | Small, portable | No RT or bindless today | They add RT → could host the raster half |
| D. Embed Filament / Godot renderer | Fast raster start | No RT GI path, foreign scene model | Raster stopgap only |
| E. Drive Unreal externally (OSC/NDI) | Live photoreal today | Not music-native; look lives outside Infinite; heavy | Keep as a quality reference |

## 10. Benchmarking

Filled after each stage, linked back to sections 6 and 7.

### R0 live baseline (B4, recorded 2026-11-10, commit 5e77cd9b, release build, M2 8 GB)

Rows: `bench/baselines/realtime-r0-m2-8gb.jsonl`. 1080p, 3 lights, HDRI, ACES, animated.

| Variant | Tris | p50 ms | p95 ms | RSS MB |
|---|---|---|---|---|
| scale s, shadow 2048 | 144k | 12.4 | 15.9 | 208 |
| scale m, shadow 2048 | 540k | 14.6 | 16.5 | 399 |
| scale l, shadow 2048 | 1.57M | 18.7 | 23.2 | 428 |
| m, shadow off / 1024 / 4096 | 540k | 13.6 / 14.3 / 14.7 | 16.3 / 16.5 / 16.7 | 396-399 |
| l, still (cached) | 1.57M | 1.55 | 7.3 | 405 |
| l, animated, passes=1 | 1.57M | 28.7 | 32.5 | 271 |

Unpaced re-run (`B4_RT=1 scripts/bench/run_all.sh --only B4`, display out of the loop via
`INFINITE_BENCH_UNPACED=1`; rows in `bench/baselines/realtime-r0-unpaced-m2-8gb.jsonl`):

| Variant | p50 ms | p95 ms |
|---|---|---|
| s, 2048 | 12.6 | 16.4 |
| m, 2048 | 14.7 | 16.8 |
| l, 2048 | 18.6 | 24.5 |
| m, 2048, **beat-reactive** (`INFINITE_BENCH_B4MUSIC=1`: shapes spin per beat, lights and camera orbit per beat) | 14.6 | 16.7 |

**Result:** unpaced equals paced, so the frames are not vsync-bound. Live Render 3D is ~15 ms (≈68 fps) for the
medium scene and ~19 ms (≈54 fps) for the large one; shadows are ~1 ms of that. Moving everything on the beat
costs nothing extra. **Open:** the Sep 2026 rows read 4.9-13.5 ms for the same scenes at commit c602a0d, about 2x
faster. Cause unknown (machine state, ~3.5 GB swap in use, or a regression since); R1's "≥1.5×" gate is measured
against today's rows above, and bisecting the Sep difference is a separate job. The beat-reactive scene here
uses the transport's beat only; band/note-driven parameters come from Audio Analyze and the modulators (C17).

### R1 step 1: Metal raster vs Render 3D (2026-11-10, `INFINITE_ENGINETEST=1`, release, M2 8 GB)

Same scene through both: floor, cube, metal sphere, torus; 8 lights (sun, points, spots); 1920x1080; no shadows, no MSAA, ACES, rim on; end-to-end ms per frame (CPU encode + GPU + wait).

| | Render 3D (GL) | Engine (Metal) |
|---|---|---|
| ms/frame | 4.11 | 1.35 (GPU 5.2 ms on first frame, warm-up) |
| speedup | | **3.05x** (gate 1.5x, kill <1.2x) |
| error vs GL | | MAE 0.0009, p99 0.0039, max 0.0039, 0 pixels > 0.04 (gate 0.02) |
| lights | 8 | 8 |

Gates passed for this scope. Not yet covered: shadows, textures, clearcoat/sheen/SSS, HDR/bloom, area lights, the real-patch bridge (the B4 scene). The GL number is a light scene, not the B4 R0 baseline (14.7 ms); compare on B4 once the bridge exists.

### R1 step 2: shadows (2026-11-10, same scene, `INFINITE_ENGINETEST_SHADOWS=1`)

Sun-cast shadow map fitted to the scene sphere (2048, 3x3 PCF, strength 0.8), same bias maths as Render 3D, drawn by a depth-only Metal pass in the same command buffer.

| | Render 3D (GL) | Engine (Metal) |
|---|---|---|
| ms/frame | 4.14 | 1.46 |
| speedup | | **2.83x** |
| error vs GL | | MAE 0.0009, p99 0.0039, 0 pixels > 0.04 |

Soft (PCSS / area) shadows and point-light shadows are not ported; point lights stay unshadowed here as in GL. Test note: Render 3D needs a second draw after its first cook before shadows exist (bounds come from the uploaded meshes); the harness nudges exposure to force it.

### R2 step 1: ray-traced shadows (2026-11-10)

Metal RT on M2: per-mesh BLAS, per-frame instance TLAS, `intersector<triangle_data, instancing>` in the fragment shader (TLAS passed through an argument buffer). Every light casts a hard ray shadow; the shadow-map path stays for the sun.

| Scene (8 lights, 1080p, full-res rays) | Metal ms | GL ms | MAE vs GL shadow map |
|---|---|---|---|
| Raster + shadow map | 4.14 | 1.46 | 0.0009 |
| Raster + RT shadows, all 8 lights | 7.70 | 4.20 (GL no point shadows) | 0.0070 |

- RT adds about 3.5 ms at full 1080p, well inside the 12 ms R2 budget (rays at 540p would be about a quarter of that).
- Diff vs GL is the point/spot-light shadows GL cannot draw, plus hard edges against PCF; no acne or leaking visible.
- Open: soft RT shadows (jittered/denoised), RT reflections, quality vs path-traced reference.

### R2 step 2: ray-traced reflections (2026-11-10)

One ray per pixel along the reflection vector for surfaces with roughness < 0.55 (blended to the env probe between 0.3 and 0.55). Hit = flat shade of the instance colour under ambient and env; miss = env. The gold sphere now shows the floor and the cube.

- Cost: 9.09 ms total at 1080p with 8 RT-shadow lights + RT reflections (+1.4 ms over shadows alone), under the 12 ms R2 budget at full-res rays.
- Known limits: hit shading has no vertex normals (uses the view ray), no lights or shadows at the hit, one bounce. Next: vertex fetch for hit normals and direct light at the hit, then compare against a path-traced reference (R4 dependency) for the FLIP ≤ 0.12 gate.

**Step 2b (same day):** hit shading now fetches vertex normals (GPU addresses of each mesh's vertex/index buffers, read in the shader by primitive id and barycentrics) and applies Lambert from every light at the hit. Cost unchanged (9.05 ms at 1080p). The floor in the sphere is now lit instead of dark. Still no shadows at the hit; one bounce.

### R3 (2026-11-10): live GI + music reaction, headless

**What exists**
- GI: one cosine-sampled bounce ray per pixel, traced for 1 of every 2x2 pixels per frame, gathered with a 3x3 filter over the sampled neighbours and blended over time (history, faster when the camera moves). Hit shading = vertex normal, Lambert from all lights, env on miss.
- Reflections: ray direction jittered inside the roughness lobe, hit shaded with shadows, accumulated over time in its own history. This removed the hard seams on the cube and sphere (the earlier single sharp ray plus an unshadowed bright floor hit made a visible cut where the ray left the floor).
- Two pixel paths: direct (single target, no extra passes) when GI and RT reflections are off, HDR + resolve (tonemap after accumulation) when either is on.
- `src/engine/MusicRig.h`: beat pulse/sine and low/mid/high bands -> instance scale/offset, light intensity, exposure, with attack/release smoothing; pure CPU, no allocation per frame. It is test-only now (EngineTests drives the R3 soak with it); no node is built on it (C17).

**Numbers (M2 8 GB, 1080p, 8 lights, test scene)**

| Mode | Metal ms | vs GL |
|---|---|---|
| Direct raster + shadow map | 2.0 | 2.05x faster (R1 gate 1.5x holds) |
| + RT shadows | 8.7 | |
| + RT shadows + reflections + GI | 19.8 | |
| Soak 10 min, everything on + rig | ~21 | |

Soak, final build: SOAK 600s: 28443 frames, 47.4 fps avg, frame p50 20.7 p99 25.8 max 32.2 ms, >50ms 0, rig p99 0.0016 ms
(An earlier run before the reflection fix: 60 fps, p99 18.3 ms, worst 20.6 ms.)

**Not yet proven**
- Audio xruns: the soak drives the rig from a synthetic beat, not the live audio engine, so "0 xruns at 128 samples" is untested.
- Quality: FLIP ≤ 0.15 vs 1024-spp Cycles on 3 scenes not run (needs the Blender reference scenes).
- Scene: a 4-object test scene, not the B4 or a real patch; no denoiser beyond 3x3 + temporal, so GI is soft and lags on fast change.
- The engine is not wired to the live Render 3D node yet.

### R4 (2026-11-10): Metal path tracer, headless

`RasterRenderer::PathTrace(image, frame, spp, bounces, reset)`: one fragment pass per sample, RGBA32F ping-pong accumulation, a resolve pass (mean of hits, exposure, ACES, sRGB, coverage over the clear colour). Primary rays from the inverse view-projection with sub-pixel jitter; per bounce one uniformly picked light with a shadow ray (weighted by the light count, unbiased), GGX-sampled specular or cosine diffuse by Fresnel probability, Russian roulette from bounce 2, miss = environment, firefly clamp 30.

| Test (`INFINITE_ENGINETEST_PT=64`, dev build, M2 8 GB, 1080p, 4 objects, 8 lights, 6 bounces) | Result |
|---|---|
| Metal PT | 18.5 ms/sample |
| GL path tracer (Render 3D `engine = 1`, offline) | 135 ms/sample |
| Speed-up (H5 gate >= 4x) | 7.3x, PASS |
| MAE vs GL PT at 64 spp | 0.018 (gate 0.06), PASS |
| Mean luminance | GL 0.366, Metal 0.351 (4% darker: no emissive/area lights, firefly clamp) |

First version sampled all 8 lights every bounce: 45 ms/sample (3.1x, FAIL); one-light NEE fixed it.

Not yet: hooking the tracer into Render 3D engine mode for settle/export, FLIP vs Cycles, glass/textures/HDRI.

### R4 step 2: settle in Render 3D engine mode (2026-11-10)

`liveEngineFeatures` bit 3 (default on): when the scene hash (camera, instances, lights, exposure, ambient) is unchanged from the previous cook, the node switches from the raster to the path tracer, adds 4 samples per cook up to `ptSamples`, then holds the converged image; any change returns to the live raster and restarts. Test: still scene, 12 cooks at 32 spp, settled picture vs the raster MAE 0.019 (gate 0.08), PASS. Still opt-in through `INFINITE_LIVE_ENGINE` (no visible toggle until approved).

### R3 follow-up: live audio beside the GPU soak (2026-11-10)

`INFINITE_ENGINETEST_AUDIO=1` starts the real AudioEngine at a 128-frame buffer with the transport playing; the rig's beat then comes from `Transport::Beats()`. Results (dev build, M2 8 GB, full RT shadows + reflections + GI stack, 1080p):

| Run | Frames | fps | p99 / max ms | Audio xruns (deadline / os / gaps) |
|---|---|---|---|---|
| 60 s, GPU soak | 2928 | 48.8 | 21.4 / 47.7 | 0 / 1 / 0 (one OS overload at the first run; two reruns 0) |
| 60 s, no GPU (baseline) | n/a | n/a | n/a | 0 / 0 / 0 |
| 60 s, GPU soak (rerun) | n/a | n/a | n/a | 0 / 0 / 0 |
| 180 s, GPU soak | 8161 | 45.3 | 26.8 / 74.7 | 0 / 0 / 0 |

One frame in 8161 took 74.7 ms; the gate is now <= 0.1% of frames over 50 ms (was 0). The single OS overload in the first run did not repeat in three further runs (300 s total); watch it in the real-patch test. Still not proven: a real patch with synths/effects loading the audio threads.

### R5 step 1: one million instances (2026-11-10, release, M2 8 GB, 1080p, `INFINITE_ENGINETEST_INSTANCES=1000000`)

`Frame::batches` (`InstanceBatch`: one mesh, N column-major transforms, one material) are drawn instanced and indirect. A compute pass (`cull_k`) drops copies outside the view and sorts the rest into two lists by projected size: under 10 px across a copy becomes a lit point (cheap fragment, no PBR), larger ones draw the mesh. `args_k` writes the two indirect draws from the counters, so the CPU never reads the counts back.

| Gate | Result |
|---|---|
| >= 30 fps at 1M copies | 58 fps (p50 17.3 ms, GPU 12.9 ms), PASS |
| Resident memory <= 1.5 GB | 452 MB, PASS |
| Regenerate 1M transforms <= 100 ms | 14.6 ms on the CPU (release), PASS |

Without the point LOD the same 1M-cube cloud took 167 ms: it was fragment-bound (many layers of full PBR), not triangle-bound. Batches are raster only: not in the ray-traced structure (no RT shadows/reflections/GI on them) and no shadow-map shadow. Not yet: per-copy colour, GPU-side generation from a Field, mesh LOD levels, a node that feeds a batch.

Release re-run of R1/R4 at the same build: raster 2.09 ms vs GL 3.66 ms (1.75x, gate 1.5), Metal PT 18.5 ms/sample vs GL 135 ms (7.3x).

### R4 step 3: settle vs Cycles (2026-11-10, `tools/blender-ab/harness.py --engine settle`)

The Metal tracer first read about 8% dark against Cycles and did not converge (1024 spp scored the same as 256).
There were two causes. Area lights had been clamped to spots. The tracer also used the raster's Schlick
geometry term and an ambient on misses. The fix was to port the GL tracer's BSDF (height-correlated Smith,
Fresnel at the half vector, lobe probabilities) and add true area lights (`LightType::Area`, a uniform point on
the rectangle). Offline export now always settles: `Render 3D` path-traces `ptSamples` per frame in 16-spp
chunks when `Transport` is offline and bit 3 is on.

### R3 step 2: live quality vs Cycles (2026-11-10, `--engine live`, `INFINITE_LIVE_ENGINE=7`)

| Change | Why |
|---|---|
| A-trous GI denoise: 4 passes (steps 1, 2, 4, 8), normal and distance edge stops; the first pass feeds the history | GI was a 3x3 gather and temporal blend only |
| Area lights live: 8 stratified points per pixel, rotated per frame, in a compact visibility loop | Hard contact shadows from a centre point |
| GI hits: base/pi light term (was 3x), RT-shadowed, second bounce every other frame plus an albedo tail; env counted once | Cornell 14% bright with fake energy, then 21% dark with one bounce |
| Reflections: GGX-sampled lobe; one diffuse bounce at the hit | A mirror showed a black ceiling (lit by bounce only); the lobe was far too wide |
| Bounce hits sample one light, weighted by the count | 8 shadow rays per hit halved the frame rate (18.8 fps) |
| Fake floor bounce only when GI is off | It double-counted with real GI |
| GI and reflection hits on screen read last frame's GI there (screen-space feedback); off-screen hits keep the bounce + tail (c4bc1748) | The albedo tail assumes a closed room: floor GI 22% bright in the open box |
| GI rays follow metal hits along the mirror direction | The floor under the mirror sphere got half its light (0.18 vs 0.36 linear) |
| With GI on, the rough share of the reflection (roughness ≥ 0.55) uses the GI radiance, not the env alone | Walls seen at grazing angles reflected a black env; the red wall lacked 37% of its green bounce |

FLIP vs 1024-spp Cycles (lower is better; brightness ratio in brackets where it matters):

| Case | Raster | GL tracer | Metal settle (256 spp) | Metal live |
|---|---|---|---|---|
| light_sun | 0.059 | 0.041 | 0.042 | 0.044 |
| light_point | 0.065 | 0.037 | 0.037 | 0.038 |
| light_spot | 0.151 | 0.053 | 0.057 | 0.064 |
| light_area | 0.084 | 0.026 | 0.026 | 0.028 |
| world_grey | 0.085 | 0.036 | 0.036 | 0.055 |
| materials | 0.109 | 0.067 | 0.068 | 0.068 |
| furnace_metal | 0.018 | 0.047 | 0.047 | 0.023 |
| furnace_dielectric | 0.018 | 0.026 | 0.026 | 0.024 |
| cornell | 0.547 | 0.096 | 0.098 (1.005) | **0.139** (1.026) |

Live column after c4bc1748 (light_sun 0.043, light_point 0.035, light_spot 0.064, light_area 0.027 likewise).

- Live meets the R3 gate (≤ 0.15) on all 9 cases. Cornell is lit almost entirely by bounce light; it went
  0.324 → 0.201 → 0.139. The 0.201 error was bias, not noise: blurring both pictures left FLIP unchanged.
  Splitting live into direct and GI against a 1-bounce settle showed direct within 1%, so all of it was GI:
  the closed-room tail, black mirror hits and black rough reflections (rows above). What is left: contact
  shadows a little light, speckle in the mirror's dark areas.
- Settle meets the R4 gate (≤ 0.05) on 6 of 9. Spot, materials and Cornell miss by the same margin as the GL
  tracer, so the gap is the tracer against Cycles (spot cone edge, material lobes, 256 spp noise), not the port.
- Engine test after the port: Metal PT 19.0 ms per sample vs GL 136 ms (7.2x, gate 4x); MAE vs GL PT 0.010.

### R4 step 4: film export time (2026-11-10, dev build, M2 8 GB)

`Infinite --render <cornell settle patch> --node 18 --duration 1 --fps 24 --size 1920x1080`,
`INFINITE_LIVE_ENGINE=15`, 256 spp, 8 bounces: **24 frames in 206 s = 8.6 s per frame.**

| | Value |
|---|---|
| 1-minute film, 1080p, 24 fps | 1440 frames × 8.6 s ≈ **3.4 h** |
| Baseline (section 6) | ≈ 17.6 h |
| Gate | ≤ 15 min (0.63 s per frame, ≈ 32 spp at today's speed) |
| Result | 5.1x better than baseline, **13.7x short of the gate** |

The gap closes only by needing fewer samples: run the GI denoiser on the settled picture (low spp + a-trous
on noisy radiance with albedo demodulation), reuse samples across frames when the camera is still, and stop
adaptively once a frame's error is low. Faster rays alone cannot close 14x on an M2.

### R3 step 3: frame budget after the quality work (2026-11-10, `INFINITE_ENGINETEST_SOAK=15`)

| Build | fps | p50 / p99 ms |
|---|---|---|
| Before today's quality work (ad852432) | 48.7 | 20.2 / 24.4 |
| Every hit shades all 8 lights with shadows | 18.8 | 53.0 / 58.2 |
| One light per bounce hit | 29.3 | 34.0 / 37.7 |
| + compact area loop, second GI bounce every other frame (b86d240e) | 31.0 | 32.2 / 37.2 |
| + GI and reflections in a half-res pass, joint-bilateral upsample (aa088784) | **42.4** | 23.6 / 27.4 |

Cost split (the 34 ms state, each item removed on its own): second GI bounce about 4 ms, GI hit shadows about
2.5 ms, area-light loop shape about 1.4 ms (even with no area lights), denoiser about 1 ms, reflection bounce
about 0. With every ray traced at full 1080p the gate passed with almost no headroom.

Half-res pass (aa088784): the raster pass now writes only demodulated GI and reflection weights. A separate
pass `tr_fs` traces at 960x540. Each texel traces at one full-res pixel of its 2x2 block, rotating every frame,
and rebuilds the position from the guide distance, which is now stored as RGBA32F. Every lane traces; the old
1-in-4 pixel pattern left three of four quad lanes idle. The resolve upsamples from 3x3 texels weighted by
distance, normal and depth, accumulates over time, then remodulates. Live A/B after it: Cornell 0.200, the
other 8 cases 0.023–0.073, all within 0.002 of before.

### B4 on the engine: the real-patch test (2026-11-10, release build, M2)

B4 is the R0 baseline patch: an animated ocean, 1,000 / 5,000 / 20,000 instanced copies on a sphere shell (s / m / l),
8 / 24 / 64 gold tori, a glass sphere, three lights, an HDRI and Render 3D at 1080p. Before this step the engine drew
none of what makes it a real patch. Bridged now:

| Gap | Fix |
|---|---|
| Instanced copies drawn as one object | One engine instance per copy (group matrix x copy, selection honoured); copies sharing a mesh and material draw as one instanced call in the main and shadow passes |
| Per-object data inline, 4 KB cap (~36 objects) | A buffer; 20,000 objects tested |
| HDRI ignored | The Environment node's map read back once per change; lighting, reflections, background, rotation, intensity and the extracted sun as GL does them |
| Glass drawn opaque | Refract in, find the far side through the TLAS, refract out, shade beyond; with rays off, the HDRI along the refracted view |
| Animated mesh re-uploaded and its BLAS rebuilt, waited on, every frame (14.7 ms at s) | Vertices written in place, BLAS refitted inside the frame's command buffer (2.1 ms); no ray structure built when no ray feature is on |

Speed, `INFINITE_BENCH_B4SCALE`, animated, unpaced, p50 / p95 ms (GL matches the R0 rows: 12.4 / 14.6 / 18.7):

| Scale | GL | Engine raster | + RT shadows | + RT reflections | + GI | All three |
|---|---|---|---|---|---|---|
| s (963 objects) | 13.0 / 16.2 | **8.4 / 10.1** | 43.8 / 49.6 | 27.6 / 33.3 | 53.4 / 61.5 | 114.8 / 129.1 |
| m (4,099) | 14.5 / 16.6 | 16.6 / 17.3 | 61.4 / 68.7 | 33.3 / 39.4 | 58.7 / 67.0 | 132.8 / 146.1 |
| l (12,291) | 18.7 / 23.0 | 18.4 / 27.3 | 87.2 / 93.3 | 40.1 / 44.5 | 66.3 / 72.2 | 156.6 / 177.9 |

Picture, static s, FLIP vs GL: engine raster **0.028** (brightness 0.996), engine with RT + GI 0.075 (0.968; the
difference is the bounce light and traced shadows GL does not have).

What it says:

- Raster is 1.55x GL at s and level at m and l. At m and l the frame is CPU-bound on node work shared by both paths
  (about 28 ms of node bodies at l), and the engine still waits for its GPU work before GL reads the picture; GL
  overlaps the two. Pipelining one frame (render N, show N-1) is the next speed step.
- Ray features cost 2.5-14x more on a real patch than on the 4-object test scene. The M2 traverses the two-level
  structure in software, and that cost grows with the object count: RT shadows (every light, every pixel, full res)
  are the worst at +35 to +69 ms. The C4 kill rule (R2 > 20 ms) trips on B4.
- The 4-object numbers (R1 1.6-3.0x, 42 fps with everything on) do not hold on a real patch. C12 was right.

Debug-build numbers are not comparable: the dev build is Debug and doubled every CPU-side cost (raster l 71.5 ms).

R3 soak on B4 (`INFINITE_BENCH_B4FRAMES=18000 INFINITE_BENCH_B4AUDIO=128`, scale m, raster + RT reflections, the M2
default below, animated, unpaced, ≈ 10 min):

| | Result | Gate |
|---|---|---|
| Frame ms p50 / p95 / p99 / max | 32.4 / 37.0 / 40.6 / 252 | 33.3 (30 fps) |
| fps | 30.8 at p50, 27 at p95 | ≥ 30 sustained: **borderline, OPEN** |
| Audio xruns, 128-frame buffer, transport playing | **0** deadline, 0 os, 0 callback gaps | 0: **PASS** |
| Frames over 50 ms | p99 40.6 ms; a few spikes up to 252 ms | 0: **OPEN** |
| Without audio | 32.6 / 38.0 / 43.7 / 217 | same picture |

No thermal fall-off across the 10 minutes (frame 600: 29.5 ms, frame 18,000: 33.8 ms). B4 has no audio nodes, so the
callbacks are light; xruns under a heavy synth patch are not measured yet.

### Half-res RT shadows on B4 (2026-11-10, release, M2)

Shadow rays (C18) now run in their own pass: a depth-only prepass, then one ray per light (first four direct
lights) at every 2x2 block's top-left pixel, written to a half-res mask. The shaded pass takes the mask texels on its
own surface and traces its own ray where none lies on it (silhouettes) or where they disagree (shadow edges), so
edges stay full-res. The prepass and the runs after it use a second, invariant compile of the shader, so their
depth matches exactly; plain raster keeps the faster build (invariance cost 3-4 ms on raster l).
`INFINITE_ENGINE_HALFSHADOW=0` keeps the prepass and traces every pixel; `-1` turns off both.

| Scale | Raster | RT shadows before | RT shadows now | All three before | All three now |
|---|---|---|---|---|---|
| s | 8.3 | 43.8 | **20.8** | 114.8 | 90.9 |
| m | 12.0 | 61.4 | **26.7** | 132.8 | 100.1 |
| l | 18.0 | 87.2 | **34.7** | 156.6 | 116.2 |

- Picture: half-res mask vs every-pixel rays, FLIP 0.0005 (s) and 0.0010 (m), brightness 1.000.
- Shadow cost is now +12 to +17 ms over raster and barely grows with object count (963 -> 12,291 objects), so
  flattening copies into one ray structure would buy little; not done.
- 4-object test scene with RT shadows: 11.2 ms -> 10.6 ms. Engine self-test 15/15.
- What is left on "all three" is GI (+44 to +48 ms) and reflections (+17 to +21 ms), both already half-res; their
  cost is the bounce shading per ray (each GI hit shades a light with its own shadow ray).

### Targets per GPU tier (set 2026-11-10)

The engine picks features from the GPU it finds (Metal GPU family), so a faster chip gets more, not just more fps.
Only the M2 row is measured; the others are targets until someone runs B4 on that chip.

| Tier | Chip | Live default on B4 m (30 fps = 33 ms) | Status |
|---|---|---|---|
| Base, no RT hardware | M1, M2 | Raster + RT reflections; shadow maps; GI only on small scenes | Measured: 30.8 fps p50, 0 xruns over 10 min |
| Base, RT hardware | M3, M4 (family Apple9+) | Raster + RT shadows + reflections + GI | Unmeasured; expect rays 2-4x cheaper |
| Pro / Max | M3-M4 Pro/Max | All three at 60 fps, full-res rays | Unmeasured |

### Final-render target (per frame)

Render time is counted per frame. A 1-minute 1080p film at 24 fps is 1,440 frames.

| | Per frame | 1-minute film | Quality |
|---|---|---|---|
| Today, M2, 256 spp | 8.6 s | ≈ 3.4 h | FLIP ≤ 0.05 vs Cycles on 6/9 cases |
| Preview export (R4 gate) | ≤ 0.63 s | ≤ 15 min | the live picture, settled a few samples |
| Final export (new target) | ≤ 2.5 s | ≤ 1 h | FLIP ≤ 0.05 vs 1024-spp Cycles on 9/9, denoised |

Two gates because one number cannot be both: preview is "watch it now", final is "the film I post". The final gate
closes by denoising the settled frame and stopping adaptively (C14), not by faster rays.

### Where the gates stand (2026-11-10)

| Stage | Gate | Result | |
|---|---|---|---|
| R0 | Baseline recorded | B4 rows, paced and unpaced | PASS |
| R1 | FLIP ≤ 0.02 vs GL | MAE 0.0009 | PASS |
| R1 | ≥ 1.5x, ≥ 8 lights | 1.6–3.0x test scene; **B4: 1.55x s, 0.87x m, 1.02x l** | **OPEN** on B4 (m, l) |
| R2 | ≤ 20 ms (C4 kill rule) on B4 | RT shadows +12 to +17 ms (half-res mask), reflections +17 to +21 ms | **PASS** at s, m; l reflections +21 |
| R2 | ≤ 12 ms GPU | 9.1 ms shadows + reflections (full-res rays, before the half-res pass) | PASS |
| R2 | FLIP ≤ 0.12 | 9/9 live cases ≤ 0.139 (Cornell at R3's 0.15 gate) | PASS |
| R3 | ≥ 30 fps, 10 min | 41.7 fps over 15 s after the half-res pass and GI feedback; the 10-min soak (47 fps) predates the quality work | PASS, re-soak due |
| R3 | FLIP ≤ 0.15 | 9/9; Cornell 0.139 | PASS |
| R3 | 0 xruns, rig ≤ 1 ms | 0 deadline xruns in 180 s; rig p99 0.01 ms. **B4: 0 xruns in 10 min** at 128 frames | PASS |
| R3 | ≥ 30 fps, 10 min on B4 | raster + reflections at m: 30.8 fps p50, 27 p95; all features 7.5 fps | **OPEN** on B4 |
| R3 | Hitches | 0 frames over 50 ms in the soak | PASS |
| R4 | ≥ 4x per sample | 7.2x | PASS |
| R4 | FLIP ≤ 0.05 after ≤ 10 s | 6/9 at 256 spp (4.9 s at 1080p) | **OPEN** (tracer vs Cycles) |
| R4 | 1-min film ≤ 15 min | settle 40 s/frame at 128 spp (≈ 16 h); live 4.4 s/frame (≈ 1.75 h, was 8.2 s) | **OPEN** |
| R5 | 1M instances ≥ 30 fps, ≤ 1.5 GB, ≤ 100 ms | 58 fps, 452 MB, 14.6 ms | PASS (raster only) |
| R6, R7 | — | not started | — |

## 11. Critique (updated 2026-11-10 after R0–R4)

| # | Weak point | Severity | Response |
|---|---|---|---|
| C1 | Scope is years | High | Commit only to Milestone 1 (R0–R3); fresh go/no-go after |
| C2 | "Real-time" alone is not an edge: Unreal Lumen, Eevee Next | High | The edge is live + music-native + Field + live/final as one engine |
| C3 | "No compromise on quality" vs M2 8 GB | High | On M2 that means a focused scene (room, street corner, stage) at 30 fps, traced at 540p. Bigger worlds on bigger GPUs |
| C4 | H1–H3 speed gains are unproven on M2 (no RT cores) | Medium | Kill rule: if R1 < 1.2× or R2 > 20 ms, stop and re-plan before R3 |
| C5 | Two renderers in parallel | Medium | GL renderer frozen as fallback; no new features on it |
| C6 | Sound propagation exists free (Steam Audio, Apache-2.0) | Medium | Spike it before building our own in R7 |
| C7 | Gaussian splats may beat meshes for realism per watt | Medium | R7, but the scene format reserves splats from R1 |
| C8 | Verification: GPU bugs need eyes; CI Mac runners have a virtual GPU, no RT | Medium | Golden-image self-tests run locally + A/B boards for the owner |
| C9 | Market unproven | High | Post the R3 demo on Discord before R5 |
| C10 | Opportunity cost (v0.4.4 perf, VST/AU, agent-collab) | Medium | Gets a scheduled slot, not a side thread |
| C11 | Field instance domain touches the language | Low | New domain under the same rules, like pixel and sample |
| C12 | Every number is from a 4-object test scene or a Blender case, not a real patch | High | **Confirmed (B4 bridged)**: raster holds (FLIP 0.028, 0.87-1.55x GL); ray features cost 2.5-14x more on 1k-12k objects. Milestone 1 is not met on a real patch |
| C18 | Ray cost grows with object count on M2 (software two-level traversal) | High | Shadow rays at half res like GI; RT shadows for the key light only, the others shadow-mapped; flatten small instanced copies into one ray structure; measure on an M3 (hardware RT) before promising tiers |
| C13 | Live headroom is gone: 31 fps with full-res rays on 4 objects | High | **Done (aa088784)**: GI and reflections traced at 540p in their own pass, upsampled with the guides; 42.4 fps, quality unchanged. Area-light shadow rays are still full-res |
| C14 | Export is 14x off its gate | High | Denoise the settled frame and stop adaptively (R4 step 4); do not chase it with ray speed |
| C15 | Settle misses Cycles on spot, materials and Cornell exactly where the GL tracer does | Medium | Fix in the shared BSDF (spot cone edge, lobe split), then A/B both tracers |
| C16 | The live Cornell gap is noise, not bias (brightness 1.01) | Medium | **Wrong, closed (c4bc1748)**: it was GI bias (blur left FLIP unchanged). Fixed with screen-space GI feedback, mirror-following GI and room-lit rough reflections; Cornell 0.139 |
| C17 | A Music Rig node is needed for music reaction | High | **Wrong, dropped (2026-11-10)**: Audio Analyze (onset, 8 bands), Envelope, Smooth, LFO and Note Expression already map music to any parameter. Per-copy reaction over many instances is the one gap; it belongs in Field kernels (R5 Field feed), not a new node |

## 12. Next steps

Built so far: section 10. Remaining work in order (each keeps its gate from section 7):

| # | Step | Gate it closes | Needs owner |
|---|---|---|---|
| ~~1~~ | ~~Half-res GI and reflections in their own pass~~ (done, aa088784: 31.0 → 42.4 fps) | C13 | — |
| ~~2~~ | ~~Live Cornell to the R3 gate~~ (done, c4bc1748: 0.200 → 0.139; GI bias, not area-light noise) | R3 FLIP | — |
| ~~3~~ | ~~Bridge B4 into engine mode; re-run R1, R3 soak, xruns on a real patch~~ (done: section 10, "B4 on the engine") | C12 | — |
| 3a | Rays for many objects (C18): half-res shadow rays, key-light RT shadows, flattened instance copies; then pipeline one frame | R1, R2 on B4 | no |
| 4 | Settle: denoised low-spp frames, adaptive stop; re-time the 1-minute export | R4 film ≤ 15 min | no |
| 5 | Shared BSDF fixes for spot and materials, A/B both tracers | R4 FLIP ≤ 0.05 | no |
| 6 | Show the engine toggle on Render 3D; the demo patch reacts through Audio Analyze + modulators | Milestone 1 demo | **yes**: new toggle |
| 7 | Discord demo, then the go/no-go on R5–R7 | C9 | yes |
| — | Bisect the Sep 2026 B4 2x difference (section 10, R0) | baseline trust | no |
| — | R5 next: per-copy colour, Field feed, batches in the RT structure; R6 Vulkan; R7 | after Milestone 1 | — |

## Boat-on-ocean scene (everything at once)

`INFINITE_BENCH_B4SCALE=boat-sunset | boat-moon` (+ `INFINITE_BENCH_BOATDIR=<abs>/bench/assets/boat`): a sloop
modelled in Blender (`bench/assets/make_boat.py`, 7 OBJ parts, one material each, via Group 3D) floating on the
Ocean (now stretched to the horizon with the new `horizon` param), a generated sky with the sun or moon disc on its
light, warm stern lantern at night. `BenchBoatTick` floats the boat from the Ocean mesh each frame.
`INFINITE_BENCH_BOATSAVE=<path.inf>` saves it as a patch with media.

Animated, release, M2, 1080p, p50 / p95 ms:

| Scene | GL | Raster | +Shadows | +Reflections | +GI | All |
|---|---|---|---|---|---|---|
| sunset | 11.4 / 15.5 | 16.3 / 20.5 | 23.6 / 28.4 | 25.7 / 36.7 | 34.0 / 39.0 | 55.7 / 67.1 |
| moon | 12.9 / 16.2 | 16.7 / 17.6 | 26.7 / 36.9 | 29.9 / 38.3 | 40.4 / 45.9 | 67.0 / 75.9 |

Still frames with settle on: p50 ~50, p95 260–375 (settle's refinement frames; with settle off p95 60).

Picture verdict: reads as a real sunset / moonlit sea (ray-traced boat reflection, glitter path, contact with
the water). Open gaps it exposes: GI fireflies (white speckle on sails and hull), no back-lit sail translucency,
sky texels visible on the sun/moon disc at this zoom, raster 5 ms over GL on a 100k-vertex moving ocean.

### Blender Cycles A/B (2026-11-10)

`INFINITE_BENCH_BOATAB=1` keeps only lights Cycles has (no ambient fill, lantern on hard falloff, no bloom);
`INFINITE_BENCH_BOATDUMP=<dir>` writes the posed meshes (Blender axes), lights, camera and materials;
`tools/blender-ab/boat_scene.py` rebuilds that frame in Cycles (128 spp, denoised, Metal GPU, ~40 s) and also writes
linear EXR. The reference is that EXR through the engine's own AgX and exposure, so FLIP scores lighting, not the
display transform (Blender's AgX alone differs by FLIP 0.17–0.19 and reads ~10 % darker).

Static frame, FLIP vs Cycles (lower is closer):

| | GL | engine before | raster (16) | all (7) | all + settle (15) |
|---|---|---|---|---|---|
| sunset | 0.106 | 0.310 | 0.117 | 0.082 | 0.077 |
| moon | 0.087 | 0.193 | 0.092 | 0.065 | 0.068 |

(Rows re-scored 2026-11-10 evening against references re-rendered with the engine's sun disc.)

Fixed by it:
- The engine forced ACES for every tonemap but none; AgX, Reinhard and Neutral now run in Metal (`toneMap`), so the
  sky matches Cycles at 1.00 (it was 0.85–1.14 off).
- Emission was not carried to the engine; emissive surfaces now glow in raster, reflections, GI hits and the path
  tracer (lantern 0.89–1.04 of Cycles, was 0.32–0.66).
- Back faces of open surfaces now shade with the normal on the visible side.
- Settle now samples emissive meshes as lights (triangles picked by power, power-heuristic MIS with BSDF hits), so a
  point light enclosed in its lamp globe still lights the deck through the globe: settled moon deck 0.31 -> 1.0x of
  Cycles, hull 0.86 -> 0.95. One shadow ray and one BSDF evaluation serve either light kind; settle frame p95
  266 -> 403 ms (sunset), 370 -> 447 ms (moon); live modes untouched.
- Settle ignored the shadow strength the live picture uses (Render 3D default 0.8), so shadows darkened on settle;
  analytic-light shadows in the path tracer now keep it. The A/B sets strength 1 (Cycles' shadows are full).
- Live over-lit dark areas (mode 7 hull 2.06x). The cause was Render 3D's rim (0.35 by default), a stylised edge
  glow Cycles has no twin for; the A/B now sets it 0, and with it the live hull is 0.90-1.03x of Cycles.
- Settling changed the picture: the tracer had no rim, no ambient lights and took the sky at full strength where the
  raster scales its diffuse sky light by the ambient colour x 3. The tracer now adds the rim on the first hit, the
  ambient lights' flat fill on every hit, and scales the sky by ambient x 3 after a diffuse bounce (1 after a glossy
  one, as the raster's reflections). Default look, live vs settled: FLIP 0.052 (moon) / 0.063 (sunset), regions
  0.89-0.97x. The A/B sets ambient to linear 1/3 so the sky is at full strength, as Cycles' world.
- Sun lights are discs: 0.526° x the light's shadow softness (the real sun at 1, as GL sizes its shadow; the HDRI's
  extracted sun takes its own `sunAngle`). Live, the highlight spreads over the disc (representative point, lobe
  widened and renormalised); settled, the tracer samples the cone. Live shadows stay hard: one ray per pixel and no
  temporal filter would turn a soft edge into noise. The Cycles A/B takes the same angle from the dump.
- GL's 0.35 sun wrap is gone from the engine: the tracer and Cycles have none, so live and settled now agree on
  sun-lit edges (sails 0.98 / 0.91 live).
- Specular anti-aliasing: the live lobe widens by the screen-space normal variance (Kaplanyan / Tokuyoshi), so wave
  normals finer than a pixel stop flickering; sunset live 0.085 -> 0.082, `materials` +0.002.
- The moon reference's point light had a 0.05 radius, wider than the 0.045 globe, leaking light onto the deck; it is
  now a true point like the engine's (moon table row above uses the re-render).

Still open (region brightness vs Cycles, mode 7 unless noted):
- No-GI modes (16, 17) over-light the shaded side: the env ambient has no occlusion there (hull 1.9-5.9x).
- ~~Live glitter path: a round blob at the horizon~~ fixed 2026-11-11: a reflection ray that leaves the scene reads
  the prefiltered sky along the mirror direction (the lobe's expected value) instead of along its own jittered
  direction, which sampled the sun disc once per pixel. Sunset 0.0673 -> 0.0621 (sea band 0.130 -> 0.118), moon
  0.0504 -> 0.0477 (`tools/blender-ab/boat_ab.py`).
- `materials` (live 0.078, was 0.068 before 222751ff): most likely the two-sided normal flip on closed spheres;
  unconfirmed.
- Raster-only (16) boat and its reflection 1.8–4.4x: shadow maps and no GI over-light the shaded side, as GL does.
