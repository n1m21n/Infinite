# Real-time engine: live worlds that react to music

Status: **sections 1–7 for owner sign-off.** Sections 8–12 are a preview; nothing is built.
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
| R3 Live GI + music rig | fps | **≥ 30 fps sustained over 10 minutes** (thermals), 1080p output |
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
│ music rig: beat / notes / bands → scene parameters                     │
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
| Music rig | A rig node maps beat phase, note on/velocity, band energy to any scene parameter; envelopes and smoothing per target | Turns "react to music" into one node, not 30 wires |
| Determinism | Frame = f(beat, seed) | Films sync frame-exact at any render speed |
| World sources | One instance format: glTF/USD import, existing 3D nodes, Field + city nodes, splats later | All four answers in one scene |
| Worlds by rules | Graph holds structure (lots → blocks → buildings); Field kernels set per-instance rules; new `instance` domain, GPU spatial grid for `.at()`, a split/emit step | Houdini's VEX + SOP split, which Infinite already mirrors |
| Memory | App-wide GPU cap; drop order LOD → splat detail → resolution | 8 GB shared with audio and plugins |

New nodes this implies (each needs approval): Music Rig, City/Lot/Facade split nodes, Splat import, Listener (R7).

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
uses the transport's beat only; band/note-driven parameters need the Music Rig node (R3).

## 11. Critique (first pass)

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

## 12. Next steps

1. Owner signs off sections 1–7.
2. R0: re-record B4 with GPU-inclusive timing + per-pass deltas, add the music-reactive reference scene, freeze the baseline.
3. R1 on its own branch: RHI skeleton, Metal backend, IOSurface interop, Render 3D raster through Metal. Kill rule C4.
4. R2 → R3 → Milestone 1 review with the Discord demo.
