# GPU points (P8 phases 1-2): design and steps

Status: built on `feature/p8-gpu-points` (started 2026-10-23, every step done 2026-10-24; merged to main 2026-10-24). Owner said go on the Mac
benchmark alone ("keep going, I don't have Ricardo's laptop", 2026-10-23); the Windows/fork runs
are not waited for. Phase 3 (new nodes and modes) was approved by the owner on 2026-10-24 ("Start
Phase 3"): 5a Displacement noise, 5b Feedback carries geometry, 5c Point Trails.
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

8. Mesh to Points outputs GPU points. Done (section below).
9. Twist and Displacement run on GPU points when their input has them. Done (section below).
10. Field Modifier compiles to GLSL (`src/core/field/ElementGlslBackend.cpp`) when its input is GPU
    points. Done (section below).
11. Model 3D reads `.ply` point clouds as points. Done (section below).
12. Field Modifier presets for point jobs: random size, random colour, stagger over index, age to
    colour. Done (section below). Colour from image is not shipped: it needs image reads in element
    kernels, which is new language surface (owner decision).

Phase 3 (`claude-context/P8_POINTS_PLAN.md` section 5), owner OK 2026-10-24:

- 5a. Displacement gets a Noise source. Done (section below).
- 5b. Feedback carries geometry, as a new Feedback 3D node. Done (section below).
- 5c. Point Trails, a new node. Done (section below).

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
hang. The 9+ minute spin in `Sketch3DNode::CookIfNeeded` on the Debug build was memory corruption,
not a slow free: the 2D and 3D sketch engines each defined a global `struct Program` with a different
layout, and the linker merged their inline members. Fixed in 41b94296 (Program moved into each file's
anonymous namespace); Debug BYPASSSWEEPTEST now passes.

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

### B11 after Phase 3 (Mac14,7 Release universal, 1080p, 2026-10-24, one round each)

Same binary, `INFINITE_GPU_POINTS=0` (off) against the default (on). Frame p50 / p99 ms, fps p50,
cook ms p50.

| Row | off | on |
|---|---|---|
| A video→points 57.6k | 15.39 / 29.61, 65, cook 4.04 | 9.79 / 15.45, 102, cook 0.45 |
| A video→points 230k | 37.15 / 67.28, 27, cook 11.39 | 15.41 / 22.30, 65, cook 0.55 |
| A video→points 262k | 37.08 / 59.96, 27, cook 11.99 | 16.11 / 37.31, 62, cook 2.16 |
| B particles 10k | 7.99 / 13.09, 125, cook 0.69 | 6.41 / 8.64, 156, cook 0.68 |
| B particles 100k | 15.92 / 21.51, 63, cook 3.28 | 9.40 / 10.50, 106, cook 0.96 |
| B particles 200k | 26.07 / 40.91, 38, cook 10.05 | 11.19 / 16.04, 89, cook 0.81 |
| C instanced cubes 10k | 11.51 / 17.56, 87, cook 0.87 | 10.82 / 14.64, 93, cook 1.61 |
| C instanced cubes 100k | 33.43 / 58.88, 30, cook 5.23 | 36.08 / 40.97, 28, cook 9.63 |
| D 57.6k (6.2M tris) | 308.50 / 483.56, 3.2 | 303.41 / 450.04, 3.3 |
| D 230k (24.9M tris) | 1225.32 / 2189.93, 0.8 | 1134.04 / 1604.86, 0.9 |

Phase 3 kept the earlier gains: A 2.4x at 230k and B 2.3x at 200k, as in steps 1-4 and 7. C and D
are unchanged within noise (C 100k p99 is lower on, p50 a little higher). D is bound by triangle
count, not by the points path.

### B11 full cost: memory, GPU time, bytes moved (Mac14,7 Release universal, 1080p, 2026-10-24)

`tools/bench.py b11` now prints, besides frame and cook times: **gpu ms** (sum of the GPU stage
timers, p50), **foot MB** (process footprint at the end; on Apple silicon it includes GPU
allocations), **gpu MB** (the tracked textures and buffers), and **up / down MB/f** (CPU→GPU and
GPU→CPU bytes per frame: `RecordBuffer`, the GPU points upload, the point readbacks). The run found
one bug, fixed before the "on" column: `DescribeGeometryMismatch` asked for the CPU cloud, so
Instance on Points read the whole GPU cloud back every cook (6.2 MB/frame at 100k; C and D).

| Row | off: fps, cook, gpu ms, foot, gpu MB, up/down MB/f | on |
|---|---|---|
| A video→points 57.6k | 49, 5.2, 16.8, 645, 161, 6.9 / 0.22 | 107, 0.4, 7.1, 655, 156, 0 / 0 |
| A video→points 230k | 28, 10.9, 27.7, 844, 182, 27.5 / 0.89 | 61, 1.3, 12.7, 776, 162, 0 / 0 |
| A video→points 262k | 28, 11.5, 30.0, 883, 186, 31.3 / 1.01 | 61, 2.3, 14.0, 794, 163, 0 / 0 |
| B particles 10k | 138, 1.2, 4.1, 576, 154, 0.6 / 0 | 153, 0.8, 4.9, 571, 155, 0 / 0 |
| B particles 100k | 82, 3.4, 6.3, 937, 161, 7.2 / 0 | 106, 1.0, 7.3, 675, 164, 0 / 0 |
| B particles 200k | 66, 5.4, 7.2, 1179, 168, 14.6 / 0 | 92, 1.4, 8.4, 734, 175, 0 / 0 |
| C instanced cubes 10k | 96, 1.3, 7.2, 651, 155, 1.3 / 0 | 102, 0.8, 7.6, 671, 155, 0 / 0 |
| C instanced cubes 100k | 31, 7.2, 28.0, 869, 167, 13.4 / 0 | 30, 1.7, 29.3, 711, 165, 0 / 0 |
| D 57.6k (6.2M tris) | 3.2, 10.9, 313, 1216, 165, 10.4 / 0.22 | 3.6, 2.9, 274, 1333, 157, 0 / 0 |
| D 230k (24.9M tris) | 0.9, 30.9, 1095, 981, 162, 41.7 / 0.89 | 0.9, 3.1, 1086, 740, 162, 0 / 0 |

- **Bytes moved:** with GPU points on, nothing crosses the bus per frame in any row; off, up to
  42 MB/frame goes up and ~1 MB/frame comes back.
- **GPU time drops too** where points were uploaded (A: 27.7 → 12.7 ms at 230k): no `glGetTexImage`
  stall, no 230k `Mat4` upload. Where the cost is fill (B, C, D) it stays within ~1 ms.
- **Memory:** tracked GPU memory moves by at most ±20 MB; the process footprint is lower on in
  every large row (B 200k 1179 → 734 MB, A 230k 844 → 776 MB: no CPU `Particle` and `Mat4` copies).
  D 57.6k is the one row higher on (+117 MB); one round each, so its spread is not measured.
- **What still bounds it:** C 100k (10.8M triangles) and D are fill / triangle bound; GPU points
  do not touch that.

## Step 8: Mesh to Points outputs GPU points (2026-10-24, dev build)

Mesh to Points samples on the CPU as before and uploads the samples once per mesh revision
(`GpuPointsGL::Upload`: 1024 texels a row, w = scale x point size, 0 for a dead point), so a
Transform, Twist or Render 3D below it reads textures instead of rebuilding sprites from the cloud
every frame. Its GPU points carry the CPU cloud's revision; the CPU cloud stays the one every other
consumer reads.

**Checks.** `GPUPOINTSTEST` step 8 section, all pass: a sphere's 738 samples behind a Transform
give no GPU points with the path off and GPU points on both nodes with it on; count, live count
and revision equal the CPU cloud's, and the revision is stable on a second ask; the frame matches
the CPU frame (0 pixels differ). Group 3d and `render-pipeline-sweep`: see step 9 (run once over
both). Parity: `glTexSubImage2D` and `glPixelStorei` are GL 1.x core; no shader, no `Platform::`
code.

## Step 9: Twist and Displacement on points (2026-10-24, dev build)

Both now act on point clouds, on both paths: a fragment pass per point (`GpuPointsGL::RunPointPass`
with `kPointPassHeader`, the shared prelude Field Modifier will use too) when the input offers GPU
points, and the same maths on the CPU cloud otherwise. The GPU pass reruns only when the point
revision changes; a CPU consumer below gets the cloud computed lazily on the CPU, with no readback.

**Behaviour change (saved patches).** Before this step Twist passed a cloud through untouched and
Displacement dropped it (its output had no cloud). Now:
- Twist turns points exactly as it turns vertices (same angle, axis and normal rotation).
- Displacement samples its texture at planar UVs `(x, y) * 0.5 + 0.5` (wrapped, bilinear; points
  have no UVs). Scalar mode pushes along +Z by `(luma - midlevel) * strength`; vector mode adds
  `(rgb - 0.5) * 2 * strength`. With GPU points the texture is sampled where it lives, with no
  readback; the readback happens only if a CPU consumer asks for the cloud.
- Bounds without a readback: Twist keeps the extent along its axis and widens the other two to
  the largest corner radius; Displacement grows the box by the most a 0..1 texel can move a point.
- The "needs a mesh" warning no longer shows on either node when the input carries points.

**Checks.** `GPUPOINTSTEST` step 9 section, all pass: Twist over Image to Points (behind a
Transform, so the pass bakes an upstream matrix) and over Mesh to Points; Displacement scalar and
vector over Image to Points. Each case: no GPU points with the path off, GPU points with it on; the
points move; the frame is drawn and matches the CPU frame (0 pixels differ by more than 1); the
lazy CPU cloud after a GPU frame equals the CPU run (max error 0). `run-infinite-hygiene --group 3d`
23/23 and `render-pipeline-sweep` 23/23, both against `build-dev` with steps 8-9 in.
Parity: the pass uses `texelFetch`, MRT and `glUniformMatrix4fv` (GL 3.0-3.3 core, in glad);
shaders stay `#version 150`; no `Platform::` or `#ifdef` code.

## Step 10: Field Modifier on points, on the GPU (2026-10-24, dev build)

Field Modifier (`FieldElementNode`) now acts on point clouds. Without GPU points it runs the
element VM over the cloud (`ElementStore::FromParticles` / `ToParticles`). With GPU points it runs
the kernel as one fragment pass per point: `Field::EmitElementPointGlsl` turns the element loop
into GLSL behind `kPointPassHeader`. The CPU still runs the prologue (`ElementVM::RunPrologue`) and
passes its values in as uniforms, so only the per-element maths moves. The pass reruns only when
the upstream revision, program, params, `t` or `age` change.

**New attrib `size`.** On points, `size` reads the point's scale (W / point size) and writing it
sets the scale (negative clamps to 0; a hidden point stays hidden). A Cd write gives the cloud
colour. Meshes ignore `size`.

**Semantics copied from the VM, not GLSL's.**
- `/` and `%` by 0 give 0, and `P /= 0` keeps P.
- Comparisons and `if()` read lane 0.
- sqrt and log clamp their argument; normalize of a zero vector is 0.
- smoothstep with equal edges is a step.
- rand/noise/sh hash the same 29 bits. The time half is computed on the CPU per call site and
  passed in as a `uvec2`; the seed half is computed in the shader with 64-bit maths on two uints.

**VM fixes found by the GPU/CPU compare** (both paths behave the same now; FIELDELEMENTTEST
section 18):
- A prologue value or new local assigned inside a `for` body re-ran its first assignment on every
  trip, so `s = 0; for (...) { s += x }` gave x, not 4x. Loop targets are now bound once, before
  the loop.
- A copied local aliased its source's register, so a compound assign wrote through to the source.
- Compound assigns of mixed widths (`vec3 += scalar`) took the rhs width; they now keep the wider.

**Stays on the CPU** (the node passes `PointGpuFallback()` text; points are still modified on the
CPU):
- state cells, reduce, `.at()` neighbour reads, a post-loop pass, image reads, graph statements;
- reading or writing N (GPU points carry no normal);
- a local that changes width;
- a rand speed that is not a literal or a frame value.

A kernel with state stays mesh-only and passes points through untouched.

**Known limits.**
- The GPU runs in float, not double.
- A `for` loop stops after 4096 trips.
- `pow` of a negative base with a non-integer exponent gives 0 on the GPU, NaN on the CPU.
- On a Particle System, `i` and `count` are per texel: count is the capacity, not the live count.
- maxElements does not truncate points.
- Publish reads the mesh VM only.

**Checks.**
- `GPUPOINTSTEST` step 10 section, all pass. Six kernels run over a sphere's 738 Mesh to Points
  samples: wave over t; colour from index and P; random size; params, prologue and if/else; for
  loop, swizzle and smoothstep; divide by zero, pow, mod, sh and noise. For each kernel:
  - with the path off there are no GPU points; with it on there are;
  - the points change;
  - the readback texels equal the CPU cloud (position and size error 0, colour 1.8e-5).
- An N kernel falls back and still modifies the CPU cloud.
- The wave kernel over Image to Points + Transform draws a frame equal to the CPU frame (0 pixels
  differ).
- FIELDELEMENTTEST 18/18 sections OK; FIELDTEST, FIELDSTATETEST, FIELDTRANSFERTEST,
  FIELDPARAMTEST, FIELDPIXELTEST and FIELDSAMPLETEST OK.
- `run-infinite-hygiene --group 3d` 23/23 and `render-pipeline-sweep` 23/23, against `build-dev`.

**Parity.** Shaders stay `#version 150`. The pass uses `uint`/`uvec2` maths (GLSL 1.30+) and
`glUniform2ui`, which is GL 3.0 core and in glad. There is no `Platform::` or `#ifdef` code.

## Step 11: Model 3D reads `.ply` point clouds as points (2026-10-24, dev build)

A `.ply` with no faces (a scan, a photogrammetry dump, a Gaussian-splat export) now loads as a
point cloud. Before this step, ModelIO and the portable importer only ever returned triangles, so
such a file loaded as nothing. The new reader is `src/core/PlyPoints.cpp`: plain C++ over the file's
bytes, the same code on all three platforms.

**What it reads.**
- Bodies: ascii, binary_little_endian and binary_big_endian.
- Positions: x/y/z. Normals: nx/ny/nz, kept as the particle's orientation hint.
- Colour: red/green/blue (or r/g/b). Integer types are scaled to their range; floats are 0..1.
  A splat export without rgb takes its colour from `f_dc_0..2` (0.5 + 0.2821 x f_dc).
- Every other property is skipped (any type). Elements before the vertices are skipped if their
  rows have a fixed size.
- To tell a point cloud from a mesh, only the first 64 KB (the header) is read. A mesh `.ply` goes
  on to the model loader exactly as before.

**Model 3D with points.**
- The mesh is empty and `GetPointCloud()` carries the points. They are fitted and recentred like a
  mesh ("fit to unit size", "recentre").
- A new `point size` slider (saved as `pointSize`, default 0.005, modulatable) sets each sprite's
  half-size. Patches saved before this step load with the default.
- `GetGpuPoints()` uploads the points once per load or point-size change (`GpuPointsGL::Upload`).
- The body line reads "N points". Decoded clouds share an `AssetCache` across nodes, like meshes.

**Checks.**
- `GPUPOINTSTEST` step 11 section, all pass:
  - The same 300 points written four ways (ascii with uchar colour and normals; little-endian with
    float colour and a double to skip; big-endian with ushort colour and an empty face element; a
    splat with f_dc) all parse to the same positions and colours (error 0, splat 1e-7).
  - A `.ply` with faces reads as a mesh, and Model 3D still loads it as 2 triangles. Truncated and
    non-PLY bytes are refused.
  - Model 3D fits a cloud to a unit box, keeps its colours, and moves the point revision only when
    the point size changes. Ascii and binary files load alike.
  - GPU points are offered only on the GPU path, and the texels equal the cloud (error 0).
  - The cloud behind a Transform draws the CPU frame on the GPU path (0 pixels differ).
- `run-infinite-hygiene --group 3d` 23/23 and `render-pipeline-sweep` 23/23.

**Parity.** No `Platform::` code and no `#ifdef`. The file opens through `AppPaths::FsPath`, so a
UTF-8 path works on Windows. Binary reads assemble bytes explicitly, so endianness does not depend
on the host. The Windows open dialog already lists `*.ply`.

## Step 12: Field Modifier point presets (2026-10-24, dev build)

Four new Field Modifier presets for point jobs. Each one compiles to GLSL, so it runs on the GPU
when its input has GPU points, and on the CPU otherwise.
- **Points: Random Size.** Each point gets a steady random size between `smallest` and `largest`
  (squared, so most points are small), with a light shade to match. `flicker` re-rolls it over time.
- **Points: Random Colour.** A random hue per point. `saturation` goes from white to full colour,
  and `keep` mixes back toward the incoming colour.
- **Points: Stagger over Index.** A wave runs along the point order: each point lifts, grows and
  brightens a little after the one before. Controls: `speed`, `spread` (waves over the cloud),
  `lift` and `pulse`.
- **Points: Age to Colour.** Particles go young (warm white) to mid (coral) to old (dim violet)
  over their life, shrink by `shrink` and rise by `rise`. Immortal points stay young.

**New: the `life` attrib.** `attrib float life = 0` reads each point's life fraction, age / life
clamped to 0..1 (0 when a point is immortal or not a particle). It is read-only: writes are
dropped. To carry it on the GPU, the colour texture's alpha is now the life fraction on every
path:
- `GpuPointsGL::Upload` fills it (`PointLifeFraction` in `Mesh.h`).
- The Particle System present pass writes it.
- Image to Points writes 0.
- Every point pass reads it as `Life` and writes it back (`outColor = vec4(Cd, Life)`): Twist,
  Displacement and Field Modifier.

Nothing drew colour alpha before this step, so frames do not change.

**Colour from image: not shipped.** A Field Modifier kernel cannot read an image; image reads only
exist in pixel kernels. Adding them to element kernels means new language surface on both
backends. Decided 2026-10-24: not built. Image to Points already colours its points from an image,
and new kernel surface on both backends is not worth it for the users we have. Reopen if a user asks
for particles coloured by an image.

**Checks.**
- `GPUPOINTSTEST` step 12 section, all pass. It uses a test cloud of 600 mortal particles (ages 0
  to life, every 50th immortal):
  - The upload carries each point's life fraction in colour alpha (error 0).
  - For each preset:
    - It compiles to GLSL; GPU points appear only on the GPU path.
    - The GPU texels (position, size, colour, life alpha) equal the CPU cloud (error at most 1e-6).
    - It does what its name says:
      - Random Size: sizes span 0.012..0.061.
      - Random Colour: colour spread 1.55.
      - Stagger over Index: neighbours differ by 0.002 and points a sixth apart by 0.15.
      - Age to Colour: exact young and old colours; the oldest point is at 0.3x size.
- `FIELDELEMENTTEST` (every preset compiles) and `FIELDTEST` pass.
- `run-infinite-hygiene --group 3d` 23/23 and `render-pipeline-sweep` 23/23.

**Parity.** GLSL stays `#version 150`. There is no `Platform::` code and no `#ifdef`. The presets
are Field source, which is the same on every platform.

## Phase 3: Limit and Projection presets (2026-10-24, dev build)

The fork's Limit and Projection operators become two more Field Modifier presets, as the plan
decided. Phaser was already step 12's Stagger over Index.
- **Points: Limit to Box.** Keeps points inside a box of half-size `size` around the origin.
  `mode` 0 clamps to the walls, 1 wraps (out one side, in the other), 2 mirrors back.
- **Points: Project onto Shape.** Pulls points onto a shape by `amount` (0 leaves them, 1 lands
  them on it). `shape` 0 is the XZ plane, 1 a sphere and 2 a Y-axis cylinder, both of `radius`.

Left out from the fork's params: the attribute picker (both act on P) and the mask (any Field
Modifier kernel can be masked by editing it). Normals are not set: points carry none on the GPU.

**Checks.** `GPUPOINTSTEST` step 12 section now covers both: they compile to GLSL, GPU texels
equal the CPU cloud, Limit (clamp, 0.6) leaves no point outside and inside points untouched, and
Projection (sphere, radius 1) puts every point on the sphere (error under 1e-4). 139 checks pass.
`FIELDELEMENTTEST` and `FIELDTEST` pass.

## Phase 3, 5a: Displacement's Noise source (2026-10-24, dev build)

Displacement has a new **source** dropdown: Texture (the default, so saved patches are unchanged)
or **Noise**. Noise moves geometry with 3D noise sampled at each vertex or point, so a surface or a
cloud can move without first making and mapping a noise texture.

**Controls (noise group).**
- `type`: Smooth, Fractal, Curl (flow), Ridged or Cells.
- `affects`: Position, Colour (points) or Size (points).
- `frequency` (0.01-20), `speed` (0-10), `detail` (octaves 1-8) and `grain` (roughness 0-1).
  `detail` and `grain` only shape Fractal and Ridged, and are greyed out otherwise.
- `seed` (0-999).
- The existing `mode` is the direction: Scalar moves along the normal (+Z on points, which have
  none), Vector moves in XYZ. Curl is always a vector. `midlevel` is not used with Noise.
- The saved key for `grain` is `noiseRoughness`, because Displacement's material already saves
  `roughness`.

**Time.** The noise evolves with transport time multiplied by `speed`: two noise layers, one per
whole step, faded into each other. It pauses and scrubs with the transport. At speed 0 it holds
still and the mesh is not rebuilt.

**Colour and Size act on points only.** A mesh vertex has no colour or size of its own. On a mesh,
those two modes leave it unchanged and show a cook warning.

**One noise, two languages.** `src/core/DisplaceNoise.h/.cpp` holds the C++ noise (meshes and CPU
clouds) and `kGlsl`, the same maths for the GPU point pass. Both use the same integer hash and the
same order of operations. Change one, change the other: GPUPOINTSTEST compares them.

**Checks.**
- `GPUPOINTSTEST` 5a section, all pass. It uses a 900-point test cloud. For each of the five types
  and each use (scalar position, vector position, scalar colour, vector colour, size):
  - GPU points appear only on the GPU path.
  - The cloud changes (moved at least 0.10).
  - The GPU texels equal the CPU cloud: position error at most 4e-6, colour 2e-6, size 0.
- Measured ranges on a 3D grid:

  | Type | Range | Mean |
  |---|---|---|
  | Smooth | -0.82..0.91 | 0.00 |
  | Fractal | -0.63..0.61 | 0.00 |
  | Curl | -1.12..1.16 | 0.00 |
  | Ridged | -0.72..0.97 | 0.33 |
  | Cells | -0.97..1.00 | 0.05 |

  Ridged leans positive by nature, so scalar Ridged also swells a mesh a little.
- Curl is divergence free: mean |div| / mean |dV| = 0.0007.
- On a sphere, vertices move (0.137) and the 64 seam vertices stay together (gap 0).
- Speed 0 holds still as the transport moves; speed 0.8 moves the surface by 0.155 over 1.25 s.
- A new Displacement's source is Texture.
- `run-infinite-hygiene --group 3d` 23/23 and `render-pipeline-sweep` 23/23.

**Parity.** GLSL stays `#version 150`, with `uint` and bit operations (core since GLSL 1.30). The
C++ uses only `<cmath>` and fixed-width unsigned arithmetic, so the hash wraps the same on every
compiler. There is no `Platform::` code and no `#ifdef`.

## Phase 3, 5b: Feedback 3D (2026-10-24, dev build)

A new 3D node, **Feedback 3D**: a one-frame delay for geometry, so the end of a chain can be
patched back into its start. The owner picked a separate node over making the 2D Feedback carry
geometry (2026-10-24). The 2D Feedback is unchanged: it is an image source at about 33
`dynamic_cast<IGeometrySource*>` sites, which would have started treating it as 3D.

**Pins.** `seed` (slot 0) is where the loop starts. `loop` (slot 1) is the end of the chain that
reads this node. Each cook the node outputs what `loop` made from the previous capture. It re-seeds
on the first cook, on **Reset**, and when the seed cable moves. With nothing in `loop` it passes the
seed through. No params; the node face shows "looping, frame N" and a Reset button.

**How a cycle stays safe.**
- The node stamps its cook frame before cooking upstream, so the loop's nodes find it cooked.
- `GetMesh`, `GetPointCloud` and `GetGpuPoints` hand out the stored capture and never pull `loop`.
- Material, texture and mapping forward from the seed only; `PassthroughSource` is null.
- New `WouldCreateGeometryCycle` (`src/app/graph/Wiring.cpp`), used by the drag path
  (`StageLinks.cpp`) and `ConnectNodes` (RPC, headless): a geometry cycle is refused unless it
  closes through a Feedback 3D's loop pin. Before this, nothing checked geometry cycles.
- Patch load does not run the check (no other cycle check does either).

**What is captured.** The capture is baked to world space (the source's model matrix applied), so
the node's own matrix is identity and a Transform inside the loop accumulates. A capture is taken
only when the source's revision or matrix changes, so a loop that settles stops moving.

**Points on the GPU.** If the loop offers GPU points, the capture is a copy pass (with the bake)
into one of two ping-pong targets, so the loop never reads the texture being written. The CPU
cloud is read back only when a CPU consumer asks. A CPU capture is uploaded once per revision.

**Checks.**
- `GPUPOINTSTEST` 5b section, all pass:
  - A 64-point seed with a Transform (+0.25 in x) in the loop: after 5 cooks the mean x is 1.35 on
    both paths (seed 0.35 + 4 steps), 64 points, generation 4. GPU and CPU agree (error 0).
  - Reset starts over from the seed (generation 0); with no loop the seed passes through.
  - A box mesh in a loop nudged up 0.5 a cook has moved up 1.5 after 3 loops.
  - Wiring: a plain geometry cycle is refused (both into the Transform and into the seed pin);
    the same cycle closed through the loop pin is accepted and cooks without recursing.
- `run-infinite-hygiene --group 3d` 23/23 and `render-pipeline-sweep` 23/23, against `build-dev`.

**Parity.** The node is plain C++ and GLSL 150 with no `Platform::` code and no `#ifdef`. It reads
textures back with `glGetTexImage`, which Step 9 already uses on all three platforms.

## Phase 3, 5c: Point Trails (2026-10-24, dev build)

A new 3D node, **Point Trails** (`src/nodes/PointTrailsNode.h`): points in, a mesh of tails out.
Particle System > Point Trails > Render 3D gives comet tails the camera can fly through.

**How it works.**
- Each point keeps a ring of its last `length` positions and colours, in world space (the input's
  model matrix applied), so a moving source leaves its tails behind. The output matrix is identity.
- A sample is taken when the input's points or matrix change, every `sample every`-th change. A
  paused input freezes the tails instead of collapsing them.
- The tails are three-sided prisms, the same lines Proximity Lines draws, so they take materials
  and draw in Render 3D, its shadows and the mini viewport like any mesh. Vertex colour carries
  the tail colour.
- A look param (width, fade, taper, colour) rebuilds the mesh from the ring without sampling.
- Material, textures and mapping forward from the input; `PassthroughSource` is null (the mesh
  starts here). A plain mesh input trails its vertices.

**Params** (all saved, the sliders modulatable): length 2..240 (30), sample every 1..10 (1), width
0.0005..0.2 (0.01, the head radius), fade 0..1 (1, the tail darkens to black), taper 0..1 (0.5, the
tail radius is width x (1 - taper)), colour From points / Gradient over age (head and tail swatches
in gradient mode), match by Particle ID / Index, point limit 1..100,000 (10,000), and a Clear button.

**Matching.** Point i is tracked as slot i, which is how both particle paths store particles: a
particle keeps its slot for life, and a dead slot is reused. By Particle ID, a slot whose age went
back (a new particle in it) starts a new tail, so no line joins a death to the next birth. By Index,
the slot is followed regardless (static clouds, Image to Points). A dead or size-0 point breaks
its tail either way.

**Where it differs from the plan, on purpose.**
- Width has no 0 = 1-pixel lines: Render 3D draws no GL lines, so the floor is 0.0005.
- Tails are prisms, not camera-facing ribbons: a geometry node does not know the camera, and a
  prism reads from every side.
- Fade darkens toward black, not toward transparent: vertex colour has no alpha, and opacity is per
  material. On a dark background the two look the same.
- The ring lives on the CPU. GPU particles are read back once a sample (the readback Proximity
  Lines and MetaBall already use); the output has to be a mesh for Render 3D anyway.
- A sample budget: at most 1,000,000 samples (points x length), so length 240 tracks at most 4,166
  points whatever the point limit says.

**Checks.**
- `GPUPOINTSTEST` 5c section, all pass:
  - 8 points moved 0.1 a cook with length 5: 8 tails of 5 rings (120 vertices, 576 indices). The
    head ring sits on the live point in world space (model z = 2), the tail 4 steps back; the head
    radius is the width (0.02) and the tail half of it (taper 0.5); head colour = point colour,
    tail colour 0 (fade 1).
  - Two cooks with no input change: same mesh revision, no new sample.
  - Width and gradient changes rebuild (new revision) without a sample; colours are exactly head
    and tail.
  - Sample every 2 over 10 changing cooks takes 5 samples.
  - A reborn particle starts over by ID (1 tail left of 3) and keeps its tail by index (2 left); the
    dead one has none either way.
  - Length 240 with a 100k limit tracks 4,166 points.
  - A Particle System on the GPU path (600/s for 40 frames) gives 389 tails, all finite.
- Against `build-dev`: `run-infinite-hygiene --group 3d` 23/23, `render-pipeline-sweep` 23/23,
  `geometry-transform-sweep` all pass, and ROUNDTRIPTEST save/load and copy/paste pass for Point
  Trails (the one copy/paste failure, Shadertoy, is other uncommitted work in this checkout).

**Parity.** Plain C++ (`<cmath>`, `<algorithm>`), no GL, no `Platform::` and no `#ifdef`.

## Handoff (2026-10-24)

Commits on `feature/p8-gpu-points` (merged to main 2026-10-24): 093bb6b6, 283f9757, a03c9dd0,
7e2267c4, 45094715, 7656ee6e, dcbfc000 (5a), 80fbd3af (5b, Feedback 3D) and the 5c commit (Point
Trails). Every step in this file is done.

**Open limits and follow-ups.**
- Feedback 3D: patch load does not refuse a geometry cycle written by hand into a `.inf`.
- Feedback 3D runs every cook, including while the transport is paused, like the 2D Feedback.
- Feedback 3D: the life attribute reads back as a fraction of a one-second life (see `ReadBack`).
- Sketch 3D hang on Debug `--describe`: fixed in 41b94296 (duplicate `struct Program`, see Patch facts).
- Colour from an image (for points): decided not to build (see step 12); Image to Points covers it.
