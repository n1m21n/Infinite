# 3D next: prove it, keep it live, then make it ours

The four basics (rigging, materials, textures, sculpting) are built and pass
their self-tests. We are not at parity with Blender, and parity is not the
goal: Infinite is a live design instrument, so its 3D has to (1) hold 60 fps
on real assets, (2) run one unbroken flow from import to render, and (3) do
what Blender cannot: every 3D edit is a layer that sound, time or a knob can
drive.

Decided 2026-11-02 (owner handed 3D direction over). Three blocks, in order.
Each block is one branch, one commit per step, merged to main when done.

## Block P: proof on real assets (speed)

The self-tests use a UV sphere and the Fox (a few thousand points). Nothing
has been measured at the sizes people bring: scans and sculpts of 100k-1M
points. A flow break (a stutter while sculpting) is top severity.

| Step | Work | Done when |
|---|---|---|
| P1 | `INFINITE_3DBENCH`: times the CPU paths per frame at 16k / 65k / 262k / 1M points: sculpt (frame + dab + apply), topology build, skinning (CesiumMan, Fox), bake AO 512. One JSON line per row, so runs compare. | Numbers recorded below |
| P2 | Fix the rows over budget, worst first, keep only if better. | Every row inside budget, or the reason it can't be written here |

Budgets: a sculpt frame (point frame + dab + apply) <= 8 ms at 262k points,
so the GPU upload and the rest of the frame still fit in 16.7 ms. Skinning
<= 2 ms per 10k vertices. Topology build is a one-off (entering Sculpt mode,
changing Level): <= 300 ms at 1M. Bake AO 512 on the Fox <= 1.5 s.

No downloads: high-poly meshes are made in-process (sphere detail, the Fox
subdivided by Level).

## Block F: one flow, import to render

Each block was tested alone. The chain an artist actually runs has not been:
Model 3D -> UV Project -> Bake AO -> Material -> Sculpt -> pose -> Render 3D,
saved, reopened, rendered headless. `INFINITE_3DFLOWTEST` builds that patch on
the Fox, CesiumMan and AntiqueCamera, checks every stage passes something on
(no empty mesh, no black bake, picture not black), saves, reopens, renders.
Also the messy cases: several materials (AntiqueCamera), skinned + sculpted
(Sculpt after a skinned Model 3D must follow the pose: offsets ride the
posed mesh, the point count does not change). Fix what breaks, smallest fix.

## Block L: live 3D, ours (needs the owner's OK before building)

Shown to the owner as proposals, built only on a yes. Each one turns a
Blender edit into a live layer:

| Proposal | Why it is ours |
|---|---|
| Image as brush: any 2D node (noise, video, camera, Field Pixel) as the Sculpt brush's alpha | A live texture shapes the stroke; nothing in Blender does this live |
| Sculpt -> normal map: Bake gets "from Sculpt", so a heavy sculpt drives a light mesh | Keeps a sculpted look at live frame rates on the projector |
| Weight paint in Pose mode (Armature node) | The one rigging gap artists hit first: envelope weights are wrong at joints |

Not planned: dyntopo, multires, a UV editor, a shader node graph. They are
modelling-tool depth; Infinite's answer is nodes plus modulation.

## Results

P1 + P2, 2026-11-02, M1 8 GB, dev build. `INFINITE_3DBENCH=1`
(`_FOX`, `_CESIUM` = glb paths, `_VERBOSE` = per-frame lines). Sculpt
frames are 30 dabs across the front of the model, Draw and Smooth
alternating, radius a tenth of its height, X symmetry; median. The first
bench (Before) stroked over the top, which on a UV sphere is the pole, so
its sphere frames are pessimistic (the same stroke after these changes:
14 ms at 260k, ~60k points under the brush).

| Row | Points | Before | After | Budget |
|---|---|---|---|---|
| sculpt frame, sphere L1 | 16k | 3.3 ms | 0.25 ms | 8 ms ✅ |
| sculpt frame, sphere L2 | 65k | 13 ms | 0.7 ms | 8 ms ✅ |
| sculpt frame, sphere L3 | 260k | 53 ms | 2.4 ms | 8 ms ✅ |
| sculpt frame, Fox L3 | 18k | 1.2 ms | 0.5 ms | 8 ms ✅ |
| build, sphere L2 | 65k | 1.11 s | 0.21 s | 0.3 s ✅ |
| build, sphere L3 | 260k | 4.9 s | 0.88 s | 0.3 s ❌ |
| build, Fox L3 | 18k | 0.06 s | 0.07 s | 0.3 s ✅ |
| skinning, Fox / CesiumMan | 1.7k / 3.3k verts | 0.12 / 0.27 ms | same | 2 ms ✅ |
| bake AO 512, Fox | 1.7k verts | 0.73 s | same | 1.5 s ✅ |

What changed:

- A dab re-shapes only the points it touched and their faces' corners
  (`Sculpt::ApplyPoints`), not the whole mesh; the point frame and the grid
  follow those points (`PointFrameAt`, `MoveInGrid`).
- A uniform grid over the points serves both the brush (`Gather`) and the
  mouse ray (`Raycast` walks it), instead of every point and every face.
  It is sized from the model on hover, so the first dab does not rebuild it,
  and it survives the stroke's first (full) re-shape.
- `MeshOps::Subdivide` uses flat arrays and one hashed edge table instead of
  `std::map`/`std::set` (7.6x; output bit-identical, checked by checksum).
  `Sculpt::Build` gathers neighbours in face order and sorts them in place.

Why the L3 build stays over: it is a one-off on a Level change (Subdivide
0.53 s + topology 0.32 s at 260k), down from 4.9 s; the rest is memory
traffic over ~1.5M face corners. Next lever is threading Subdivide; not
worth it while Level 3 is the top and edits stay incremental.

Known edges: the first dab of a stroke on a fresh sculpt re-shapes in full
(~50 ms at 260k) because untouched normals must switch from the model's to
computed ones. Dab cost scales with points under the brush, about 0.5 us a
point: a brush over a UV sphere's pole (every ring crowds in) touches ~60k
points and costs ~14 ms.

F, 2026-11-02. `INFINITE_3DFLOWTEST=<glb,glb,..>` (`_OUT` = folder for the
saved patches) builds Model 3D -> Sculpt -> UV Project -> Bake AO -> Material
-> pose -> Render 3D on each model and checks every stage passes something
on. It saves the patch, reopens it, and renders it headless. OK, 0 failures
on Fox, CesiumMan and AntiqueCamera. The saved patches render with
`--frame` in a fresh process (not black, no warnings).

| Check | Fox | CesiumMan | AntiqueCamera |
|---|---|---|---|
| a pose change re-cooks the chain (budget 25 ms) | 3.7 ms | 19.2 ms | n/a (no rig) |
| another part flows through, Sculpt warns it does not fit | n/a | n/a | OK |

What broke and was fixed:

- **glTF import turned rotated parts inside out.** The node-matrix
  determinant read column 0 as `m[0], m[4], m[8]` (a row), which gives
  cos 2θ for a Y turn. Any node turned more than 45° came in with its
  winding and normals flipped. Self-tests passed for months; a Blender A/B
  of the AntiqueCamera AO bake exposed it (ours 0.04, Blender's bright).
- **Bake banded.** Each texel's ray turn was `fmod(float(x*7919 +
  y*104729) * 0.618, 1)`. Past 2^24 a float holds only even integers, so it
  read 0 and every texel shot the same 16 rays: stepped rectangles instead
  of fine noise. Now hashed in integers.
- **UV Project re-projected every pose (90 ms).** The layout is kept when
  only the points move (same topology, same settings); positions, normals
  and colours are copied through `from` indices: 0.1-0.5 ms. As in Blender,
  UVs ride the surface instead of swimming.
- **Sculpt rebuilt its topology every pose.** Build runs only when the point
  count or indices change.
- MODELTEST's extent check expected every model *on* the unit box; a part
  keeps the whole file's fit and a skinned model is fit to its rest pose,
  so it now checks *inside*.

Known edge: CesiumMan at Sculpt Level 1 spends ~15 ms of a pose in
Subdivide (10k points). The lever is subdivision stencils (re-use the
weights, only re-mix positions); not needed under the 25 ms budget.

Blender A/B (`tools/blender-ab/ab.py`, see its docstring). Same mesh,
reach, rays, texture size and camera in headless Blender and Infinite. Plain
display: Render 3D `tonemap None` = Blender Standard; both default to AgX,
which dims white to 0.8. Reference = Cycles AO computed per pixel (no UVs),
not Blender's bake, because a bake can itself be the outlier (Smart UV
without packing filled 1.9% of the texture). Mean |error| against the
reference, AO 512, 16 rays, reach 0.25:

| Object | Blender bake | Infinite bake |
|---|---|---|
| Suzanne (repo) / generated | 0.039 / 0.013 | 0.020 / 0.015 |
| Fox | 0.008 | 0.007 |
| AntiqueCamera | 0.073 | 0.045 |
| torus on slab, corner box, cup, stairs | 0.013, 0.009, 0.062, 0.016 | 0.016, 0.009, 0.016, 0.016 |
| node_lfo | Blender 5.1 aborts packing its UVs | 0.018 |
| controls_kit | no reading (empty mask, not looked at) | |

Mean error alone missed the banding (the bands average out), so read the
sheet (truth, Blender bake, Infinite, diff x4) before the number.

## Results: render gaps vs Blender (picture A/B, `fix/render-ab`)

Measured with the harness against a dev build of main 4b123939. FLIP, lower is closer;
path-trace numbers are at 128 samples (cornell, light_spot) and 64 (hero), so noise
inflates them a little.

| Case | Raster | Path tracer | Reading |
|---|---|---|---|
| cornell | 0.579 | 0.101 | raster has no bounce light (walls' colour bleed, lit shadows, ceiling fill); pt matches Blender, brightness x1.00 |
| light_spot | 0.150 | 0.057 | cone and blend already follow Cycles' spot_attenuation; the raster remainder is the missing floor-to-sphere bounce and hard shadow fill |
| hero (all) | 0.175 | 0.109 | raster worst regions: models 0.47 (brightness x0.61), coat 0.44 (x0.72), instances 0.41, fox 0.43; pt worst: velvet 0.29 (x1.17), instances 0.35, fox 0.28-0.33 |

Findings:

- The cornell raster gap is global illumination, not a bug. A raster fix would need a baked
  or probe-based bounce; that is not live-speed-free, so no change was made. Use the path
  tracer for GI scenes.
- Spot light units and falloff already agree (brightness x0.93 raster, x0.98 pt).
- Raster models, coat and velvet read dark because their undersides get no floor bounce
  (the room floor is white in Blender). Same cause as cornell.
- Fixed in the path tracer: the sheen layer now takes its directional albedo out of the base
  (Cycles layering). Hero velvet 0.290 -> 0.266, brightness x1.17 -> x1.14 (still a little high).
- Raster floor bounce (no scene lookup): the light reaching a floor under the point, at grey 0.5,
  lights downward-facing normals by how far down they face. An isotropic version (added fill on
  every normal) brightened the light cases by 10% and was dropped.

| Raster case | Before | After |
|---|---|---|
| cornell | 0.583 | 0.547 |
| light_sun / point / spot / area | 0.070 / 0.071 / 0.150 / 0.088 | 0.059 / 0.065 / 0.151 / 0.084 |
| materials | 0.117 | 0.109 |
| lineup | 0.076 | 0.074 |
| view_agx | 0.071 | 0.067 |
| deer | 0.119 | 0.120 |
| hero (all) | 0.175 | 0.171 |
| world_grey, furnace_* | unchanged | unchanged |

Hero raster regions: models 0.473 -> 0.459, coat 0.438 -> 0.382, velvet 0.318 -> 0.314,
instances 0.412 -> 0.389, fox 0.433 -> 0.412; emissive 0.092 -> 0.097. Cornell stays far from
Blender: without wall-to-wall bounce a closed room cannot match; use the path tracer there.
Baselines refreshed (raster and `hero_pt`). `INFINITE_VIEW3DTEST` passes.
