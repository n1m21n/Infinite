# GPU points (P8 phases 1-2): design and steps

Status: in progress on `feature/p8-gpu-points` (started 2026-10-23). Owner said go on the Mac
benchmark alone ("keep going, I don't have Ricardo's laptop", 2026-10-23); the Windows/fork runs
are not waited for. Phase 3 (new nodes and modes) is out of scope here and still needs the owner.
Full background, the PointZ mapping and the phase 0 table: `claude-context/P8_POINTS_PLAN.md`.

## What the measurements say (B11, Mac14,7 Release, 1080p, `tools/bench.py b11`)

Cost split at the worst rows, with bench-only knobs (`INFINITE_BENCH_B11_SAMPLES`,
`INFINITE_BENCH_B11_SIZE`, and a temporary flat-shading switch):

| Row | base | flat shading | 4x MSAA off | both | 1/4 point size |
|---|---|---|---|---|---|
| A video→points 262k: frame p50 / CPU cook / GPU cook (ms) | 37.7 / 13.3 / 24.4 | 38.2 / 17.1 / 20.2 | 33.2 / 15.6 / 15.8 | 33.4 / 17.9 / 14.4 | 33.2 / 17.6 / 14.0 |
| C 100k instanced cubes: frame / GPU cook | 42.5 / 29.7 | 26.6 / 16.2 | 22.5 / 13.2 | 19.4 / 9.9 | 28.5 / 18.5 |
| B 100k particles: frame / GPU cook | 14.0 / 6.0 | 13.5 / 5.8 | 16.4 / 8.9 | 11.1 / 4.9 | 9.5 / 3.1 |

- **A is CPU-bound.** The frame stays ~33 ms whatever the GPU is asked to do; the CPU cook is
  13-18 ms: a blocking `glGetTexImage` of the downsampled frame, a 262k `Particle` build, a full
  copy in Transform for the spin, and a 262k `Mat4` build plus upload in Render 3D. This is the
  "points live on the CPU" cost, and a GPU point buffer removes all four.
- **B and C are fill-bound.** 10.8M triangles cost 9.9 ms once shading and MSAA are cheap; the
  PBR fragment shader and the 4x samples cost the other ~20 ms. A GPU buffer does not touch
  that. What does: a cheaper fragment path that gives the same pixels (step 5).

## Design

`GpuPoints` (`src/core/GpuPoints.h`): two textures, one point per texel, point `i` at
`(i % texW, i / texW)`.

- `posTex` RGBA32F: xyz = position in the source's point space, w = sprite half-size in world
  units (already `p.scale * PointBaseSize()`); **w = 0 hides the point** (dropped by a threshold,
  dead particle).
- `colorTex` RGBA32F: rgb colour (float, so a tint matches the CPU path exactly).
- `count` (texels to draw), `revision` (from `NextMeshRevision()`, changes iff the texels do),
  `pointMatrix` (applied before the source's `GetModelMatrix()`), object-space bounds.

`IGeometrySource::GetGpuPoints()` returns `nullptr` by default. **Default null is the safety
rule:** a node that does not forward it makes every consumer fall back to `GetPointCloud()`, which
is correct, only slower. A node forwards it only when it passes points through unchanged or can
express its change as a matrix (Transform).

Consumers that prefer it: Render 3D's cloud draw (vertex shader `texelFetch`es by
`gl_InstanceID`, applies `uModel * pointMatrix`, sizes from the model's X scale exactly like the
CPU path), Render 3D's presence test, scene signature and bounds, and the node mini viewport.
Every other consumer keeps calling `GetPointCloud()`.

Producers keep the CPU cloud **lazily**: `GetPointCloud()` builds it on first call in a frame (the
old code path, unchanged), so a CPU consumer (MetaBall, Proximity Lines, Delaunay, Geometry
Table, the Agent's stats) sees exactly what it saw before.

`INFINITE_GPU_POINTS=0` turns the GPU path off for the process (A/B checks and a field fallback).

## Steps (one commit each)

1. `GpuPoints` + `GetGpuPoints()` + Render 3D and mini viewport draw it. No producer yet, so no
   behaviour change.
2. Image to Points produces GPU points with one MRT pass (no readback); its count comes from an
   occlusion query one frame late; its revision moves only when the input image or a param does.
3. Transform forwards GPU points as a matrix (no copy); its cloud revision no longer builds the CPU
   copy. Pass-throughs that forward the cloud unchanged forward GPU points too.
4. Pixel check: a selftest renders the same Image to Points chain with the GPU path on and off and
   compares the frames; B11 A/B on the Release build.
5. Fill cost for sprites and instances (B, C): a fragment path that skips the material features a
   draw does not use, same pixels (checked by the same A/B frame compare). Done as: opaque mesh
   draws use a program with `discard` compiled out (a shader that can discard defeats the tile
   GPU's hidden-surface removal), and clearcoat is skipped at 0.
6. Instance on Points reads GPU points directly (removes its per-frame `Mat4` rebuild).
7. Particle System on the GPU (fragment-shader ping-pong sim, GL 4.1 has no compute), new params
   from the P8 plan, raised cap. On this branch as its own commit (design and status below).

The rest of the P8 plan's phases 1-2 (`claude-context/P8_POINTS_PLAN.md` section 4), not yet in
the steps above, in the order they will be done:

8. Mesh to Points outputs GPU points.
9. Twist and Displacement run on GPU points when their input has them.
10. Field Modifier compiles to GLSL (`src/core/field/GlslBackend.cpp`) when its input is GPU points.
11. Model 3D reads `.ply` point clouds as points.
12. Field Modifier presets for point jobs: random size, random colour, colour from image, stagger
    over index, age to colour.

## Done check (Lane 3)

B11 before/after table in this file; frame compare passes; `render-pipeline-sweep` and
`geometry-transform-sweep` drivers pass; `windows-parity` / `linux-parity` reviewed (GL 3.3 core
only: `texelFetch`, MRT, `GL_SAMPLES_PASSED`, RGBA32F render targets are all core 3.0-3.3).

## Status (2026-10-23, dev build, steps 1-4)

- Frame compare (`GPUPOINTSTEST`): 1828 points both ways, occlusion count equal, 3 of 1M pixels
  differ (max 37, one sprite edge). Pass.
- `run-infinite-hygiene` group 3d: 23/23. `render-pipeline-sweep`'s other fixtures
  (RENDER3DCACHE, RENDER3DLIVE, DISTRIBUTE, PHASE4, INSTANCESELECT, SELECT, CLOTH, PARTICLE): all OK.
  `geometry-transform-sweep`'s three sweeps are in group 3d. (Both sweep drivers default to
  `build/`, the Release tree; these ran against `build-dev` by hand.)
- Parity review: every new GL call (`glGetFragDataLocation`, `glDrawBuffers`, `glGenQueries`,
  `glBeginQuery`/`glEndQuery`, `glGetQueryObjectuiv`) is GL 3.0-3.3 core and in glad; shaders stay
  `#version 150` (`texelFetch`, `gl_InstanceID`); no `Platform::` or `#ifdef` code. Sampler units:
  `uPointPos`/`uPointColor` sit on 11/12 when drawing and default to 0 otherwise, which holds only
  `sampler2D`s (the one `sampler2DShadow` is on 1), so strict drivers see no mixed-type unit.
- B11 before/after: below.

## B11 before/after (Mac14,7 Release universal, 1080p, steps 1-4, 2026-10-23)

`INFINITE_GPU_POINTS=0` (before) against the default (after), same binary, `tools/bench.py b11`.

| Row | before: frame p50 / p99 / CPU cook / GPU cook (ms) | after | fps p50 |
|---|---|---|---|
| A video→points 57.6k | 15.05 / 28.03 / 3.98 / 8.38 | 9.99 / 19.60 / 0.40 / 5.17 | 66 → 100 |
| A video→points 230k | 33.60 / 70.18 / 10.88 / 20.55 | 16.60 / 26.07 / 1.60 / 8.52 | 30 → 60 |
| A video→points 262k | 36.62 / 61.86 / 12.04 / 23.34 | 16.72 / 35.52 / 3.15 / 9.02 | 27 → 60 |
| B particles 10k / 100k / 200k | 6.82 / 12.42 / 19.53 | 6.94 / 12.16 / 21.46 | unchanged |
| C instanced cubes 10k / 100k | 12.80 / 42.19 | 14.33 / 42.45 | unchanged |

A is the one the GPU path touches: about 2.2x at 230k-262k, the CPU cook down 4-7x, and the GPU
cook less than half (no `glGetTexImage` stall, no 262k `Mat4` upload). B and C do not reach the
path (Particle System offers no GPU points); their spread is run-to-run noise: B 200k re-runs
were 20.6 / 20.5 before and 23.4 / 20.2 after, C 10k 13.6 / 11.9 before and 12.3 / 12.6 after.
They stay fill-bound (step 5) and CPU-sim-bound (step 7).

Step 6 is larger than its line above: Render 3D's instanced draw, its shadow pass, translucent
instance sorting, the scene signature and ~25 other files read `InstanceTransforms()` on the CPU.
Its per-frame cost only exists when the cloud changes every frame, which nothing in steps 1-4 does,
so it waits for the B11 numbers and naturally pairs with step 7 (GPU particles).

## B11 steps 5-6 (Mac14,7 Release universal, 1080p, 2026-10-23, two rounds each)

Load was ~4.8 (a Desktop Infinite and system daemons running), so read the pairs, not the
absolute numbers. Frame p50 / p99 ms, fps p50.

| Row | off, round 1 | off, round 2 | on, round 1 | on, round 2 |
|---|---|---|---|---|
| C 10k cubes, opaque variant (`INFINITE_RENDER3D_OPAQUE_VARIANT=0` = off) | 12.27 / 16.40, 82 | 16.51 / 20.79, 61 | 10.64 / 14.77, 94 | 10.31 / 16.50, 97 |
| C 100k cubes, opaque variant | 44.96 / 64.93, 22 | 41.88 / 54.57, 24 | 33.28 / 114.97, 30 | 33.34 / 70.75, 30 |
| D 57.6k video→points→cubes, GPU instances (`INFINITE_GPU_POINTS=0` = off) | 308.80 / 471.75, 3.2 | 314.38 / 459.69, 3.2 | 315.42 / 475.50, 3.2 | 307.70 / 447.01, 3.2 |
| D 230k, GPU instances | 1147.16 / 1626.05, 0.9 | 1129.75 / 1621.62, 0.9 | 1133.22 / 1663.60, 0.9 | 1134.09 / 1634.07, 0.9 |

- **Step 5 pays:** 100k cubes 22-24 → 30 fps, 10k cubes 61-82 → 94-97 fps, same pixels
  (`GPUPOINTSTEST`). The 100k p99 got worse in both rounds (115 / 71 against 65 / 55); one long frame
  per run, not chased here.
- **Step 6 does not show in B11 D**, and should not be expected to: D draws 6.2M-24.9M triangles
  (108 per cube) and is fill-bound at 0.9-3.2 fps either way; the per-frame `Mat4` build it removes is
  ~1-2 ms of a 300-1100 ms frame. It matters where the cloud changes every frame and the copies are
  cheap (small stamps, particles into Instance on Points); its frame compare passes.

## Step 7: Particle System on the GPU (2026-10-23)

**Design.** Particle `i` lives in texel `(i % texW, i / texW)` of three ping-pong state textures:
A = position + age (RGBA32F), B = velocity + life (RGBA32F, life 0 = dead or never born),
C = birth colour (RGBA16F). One fullscreen pass per 120 Hz step births that step's particles into
a ring of slots (cursor = births so far mod the cap) and integrates every live one with the CPU
path's maths (gravity, the same value noise for turbulence, drag, Euler). A present pass writes the
`GpuPoints` textures every consumer reads (w = size, 0 for dead) inside an occlusion query, which
gives the alive count a cook later. Bounds come from a 3-level 16x16 min/max reduction read back
through a PBO + fence, never waited on, and padded by top speed and acceleration times the lag.
CPU consumers (MetaBall, Proximity Lines, the readout) get a readback the first time they ask.

**What differs from the CPU path, on purpose.**
- Each birth draws its random numbers from a hash of its birth number, not a running generator:
  a rewind replays exactly, and the run matches the CPU run statistically, not number for number.
  The CPU path is unchanged for existing patches (same generator calls, same maths at the defaults).
- At the cap the ring replaces the oldest particles; the CPU path drops new births.
- Caps: 1,048,576 on the GPU, 200,000 on the CPU fallback (`INFINITE_GPU_POINTS=0`, or float render
  targets missing; a failed shader switches the node to CPU for the session).

**New params** (appended after `seed`, so saved modulation keeps its slots): an `emit` geometry
pin with `emit from input` (born on a mesh's surface, area-weighted, or on a cloud's points; GPU
points are sampled in the shader without a readback), `speed along normal`, `copy source colour`,
`mass` (divides drag and turbulence), `time scale`, `size curve`, `colour curve` (t^curve).

**Checks.** `GPUPOINTSTEST` step 7 section, all pass: a beam with nothing random gives 3000 alive
on both paths with the same mean height; a random spray gives 15925 vs 15936 alive and the same
spread; a rewind replays a bit-identical checksum; every particle lies inside the offered bounds;
births on a cube land on its surface in its colour on both paths; births on Image to Points' GPU
points land inside that cloud; 600,000 alive at 600k/s, past the CPU cap. Parity: every new call
(`glFenceSync`, `glClientWaitSync`, `glGetBufferSubData`, `glUniform1ui`, MRT, `GL_SAMPLES_PASSED`)
is GL 3.0-3.3 core and in glad; shaders stay `#version 150`; no `Platform::` code.

**Patch facts.** `tools/gen-patch-skill.py` re-run against the Release build (2026-10-24): 16 s, no
hang. The 9+ minute spin in `Sketch3DNode::CookIfNeeded` seen on the Debug build does not happen in
Release, so it is a Debug-only cost (freeing a very large mesh unoptimised), not a hang.

### B11 scene B, particles (Mac14,7 Release universal, 1080p, 2026-10-24, two rounds each)

Frame p50 / p99 ms, fps p50, cook ms p50. Off = `INFINITE_GPU_POINTS=0` (the CPU sim, capped at
200k; it keeps a few short of the ask because births stop at the cap).

| Particles | off, round 1 | off, round 2 | on, round 1 | on, round 2 |
|---|---|---|---|---|
| 10k | 7.93 / 38.66, 126, cook 0.94 | 8.40 / 14.60, 119, cook 0.94 | 8.24 / 27.53, 121, cook 0.78 | 7.42 / 16.06, 135, cook 0.76 |
| 100k | 25.03 / 117.89, 40, cook 9.73 | 15.92 / 50.01, 63, cook 4.51 | 9.29 / 17.04, 108, cook 0.77 | 10.67 / 17.53, 94, cook 0.95 |
| 200k | 27.21 / 84.47, 37, cook 8.70 | 24.67 / 52.36, 41, cook 8.87 | 11.18 / 16.59, 90, cook 1.07 | 10.45 / 15.28, 96, cook 0.97 |

- **Step 7 pays:** 200k particles 37-41 → 90-96 fps, 100k 40-63 → 94-108 fps, and the p99 drops
  from 50-118 ms to 15-18 ms (the CPU sim's cook, 4.5-9.7 ms, is gone: ~1 ms on the GPU).
- 10k is the same either way (~120-135 fps): the frame is the rest of the render, not the sim.
