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
   draw does not use, same pixels (checked by the same A/B frame compare).
6. Instance on Points reads GPU points directly (removes its per-frame `Mat4` rebuild).
7. Particle System on the GPU (fragment-shader ping-pong sim, GL 4.1 has no compute), new params
   from the P8 plan, raised cap. Separate branch.

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
- B11 before/after: pending a Release build.

Step 6 is larger than its line above: Render 3D's instanced draw, its shadow pass, translucent
instance sorting, the scene signature and ~25 other files read `InstanceTransforms()` on the CPU.
Its per-frame cost only exists when the cloud changes every frame, which nothing in steps 1-4 does,
so it waits for the B11 numbers and naturally pairs with step 7 (GPU particles).
