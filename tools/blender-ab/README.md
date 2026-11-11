# Blender A/B

Proves a 3D feature against Blender. The same scene goes through both apps,
and a number says how far apart the two pictures are.

```
python3 tools/blender-ab/harness.py --out /tmp/blender-ab            # every picture case
python3 tools/blender-ab/harness.py --out /tmp/blender-ab light_spot # one case
python3 tools/blender-ab/harness.py --out /tmp/blender-ab --set-baseline
python3 tools/blender-ab/harness.py --out /tmp/blender-ab --ao       # also the AO bake A/B
python3 tools/blender-ab/harness.py --out /tmp/blender-ab --engine pt # Render 3D's path tracer
```

`--engine pt` (with `--pt-samples`, default 256, and `--pt-bounces`, default 8) renders
with the path tracer. Its files carry `_pt` (`inf_pt.png`, `flip_pt.png`, `board_pt.png`,
`results_pt.jsonl`, baseline key `case_pt`) beside the raster ones, and the board shows
Blender | raster | path trace | FLIP.

Needs `/Applications/Blender.app`, `build-dev/Infinite.app`, and numpy, Pillow and
`flip-evaluator` (`pip install --user flip-evaluator`).

```
cases.py ──scene.json──> blender_scene.py ──> truth.png (Cycles) + obj_N.glb
                     └──> harness.to_patch ──> inf.inf ──> Infinite --frame ──> inf.png
truth.png + inf.png ──> FLIP, brightness ratio, luma error ──> results.jsonl, board.png
```

| File | Role |
|---|---|
| `harness.py` | runs the cases, translates the scene into a patch, scores the pictures, writes the board, gates against the baseline |
| `cases.py` | the scenes |
| `blender_scene.py` | the Blender side: builds the scene, renders with Cycles, writes each object as a glb |
| `boat_ab.py` | the B4 boat-on-ocean scene (sunset / moon) vs Cycles: frame FLIP plus the glitter band (IoU, band FLIP, brightness) |
| `baseline.json` | the FLIP score each case must hold (within 0.01) |
| `ab.py`, `blender_side.py`, `metrics.py` | the older AO bake A/B, run by `--ao` |

## Scene format

| Key | Meaning |
|---|---|
| `objects[]` | `shape` (sphere, plane, cube, monkey), `location` (Z-up metres), `scale`, `material` |
| `material` | Principled inputs, colours linear: `base_color`, `metallic`, `roughness`, `ior`, `transmission`, `coat`, `coat_roughness`, `sheen`, `emission`, `emission_strength` |
| `camera` | `fov` (horizontal degrees), `azimuth`, `elevation`, `distance`, `target`; `fstop`, `focus` for depth of field |
| `lights[]` | `type` (sun, point, spot, area), `power` (W, or W/m² for sun), `azimuth`, `elevation`, `distance`, `color`, `radius`, `spot_angle` (half angle), `blend`, `size` |
| `world` | `color` (linear), `strength` |
| `view`, `size`, `samples` | Standard or AgX, picture size, Cycles samples (denoised) |
| `objects[].fur` | Geometry Op Fur on that object: `density` (strands/cm²), `length`, `jitter`, `width`, `segments`, `gravity`, `max`, `seed`; `guide` {`points`, `spread`, `height`} adds an arc Curve to the guide input; `hair` {`melanin`, `redness`, `roughness`, `radial`, `shift` (degrees), `ior`} turns the strands' Material to model Hair (Cycles: Principled Hair, Chiang, melanin) and Fur's normals to Tangent |
| `objects[].detail` | `[scale, strength]`: Material `detailScale`/`detailStrength`; Blender gets the same whiteout normal blend |
| `objects[].region` | a name; the case then also reports FLIP and brightness per region (see the hero case) |

Azimuth runs from +X toward -Y and elevation up from the ground, as in Infinite's
Camera and Light nodes. Blender's Z-up (x, y, z) is Infinite's (x, z, -y).

## Unit mapping

`light_lines` in `harness.py` holds it. The sun, point and area lights land within
4% of Blender's brightness.

| Blender | Infinite |
|---|---|
| Sun, S W/m² | Directional, intensity S |
| Point / Spot, P W | hard falloff 1, intensity P / (16π) |
| Area, P W | normalized, intensity P / (4π), shadow radius = half the longer side |
| World colour C | env sky = horizon = ground = C (sRGB), ambient = sRGB(1/3) |
| Base colour (linear) | Material colour (sRGB) |

## Reading the result

- `flip`: NVIDIA FLIP mean, 0 = the same picture. Under 0.05 is hard to tell apart.
- `brightness_ratio`: Infinite's mean linear brightness over Blender's. A units or
  energy mismatch shows here first.
- `board.png`: Blender | Infinite | FLIP map, one row per case.

A case fails (exit 1) when its FLIP rises more than 0.01 over its baseline. After a
real improvement, run `--set-baseline` so the gate locks in the gain.

## Adding a case

Add a scene to `CASES` in `cases.py`, run it, look at the board, then set its
baseline. A new kind of input (a new light type, material input or shape) needs
both `blender_scene.py` and `to_patch` to learn it.

## The hero case (regions and frames)

`hero` is a room scene (bounced light, HDRI world and its sun, point/spot/area lights,
two textured Poly Haven models, glass/clearcoat/velvet spheres, the skinned Fox at three
animation phases, 50 instances, an emissive panel, DOF, AgX). Extra scene keys it uses:

- object `shape: "model"` with `asset` (a file in `assets/`, or `env` naming an
  environment variable that points at one) and `scale`, `location`, `clip`.
- object `instances`: a list of positions; the shape is stamped on each (Instance on Points).
- object `region`: one of `REGIONS` in `cases.py`. Blender writes an object-index pass
  (`mask_k.png`, 1 sample, no denoise); the same mask, eroded 1 px, scores both images.
- scene `frames`: a list of `{phase}` entries (Fox clip phase 0-1). One render per frame.
- `world.hdri` (+ `strength`, `rotation`, `sun_threshold`): an HDRI file from `assets/`.

Output: `hero_board_<k>.png` per frame (Blender | Infinite | FLIP | region mask, then a
per-region table) and `hero/mask_color_<k>.png`. The baseline for hero is a dict of
region to a per-frame list of FLIP; the gate fails if ANY region on any frame is more
than 0.01 over its baseline. `--set-baseline hero` leaves other cases untouched.

Render 3D has three light pins, so the hero uses point, spot and area plus the HDRI's
extracted sun as the fourth light. Asset sources and licences: `assets/SOURCES.md`
(`assets/fetch.py` re-downloads them).

## Physics cases (motion, not pictures)

```
python3 tools/blender-ab/physics.py --out /tmp/blender-ab-physics            # every physics case
python3 tools/blender-ab/physics.py --out /tmp/blender-ab-physics rb_drop    # one case
python3 tools/blender-ab/physics.py --out /tmp/blender-ab-physics --set-baseline
```

```
physics.py CASES ──scene.json──> blender_physics.py ──> truth.jsonl (per frame) + geo.glb
                             └──> physics.patch ──> inf.inf ──> Infinite --frame --geometry ──> inf.jsonl
truth.jsonl + inf.jsonl ──> error over time ──> results.jsonl, curve.png per case, board.png
```

Infinite's side is `--frame <patch> <t0,t1,...> <dir>/ --node 1 --geometry <file.jsonl>`: every
frame is stepped in order and the node's world-space vertices (or points) are written per time.

| Kind | Infinite node | Error (the gated number) | Also reported |
|---|---|---|---|
| `rigid` | Rigid Bodies | RMS distance of each body's centre from Blender's, metres | rotation error (deg), final positions |
| `cloth` | Cloth | mean distance of each vertex from its Blender vertex, metres | lowest point curve |
| `particles` | Particle System | RMS of the cloud's mean height, metres | peak height, alive count |

Scene keys: `kind`, `duration`, `gravity` (Blender Z-up vector; leaning gravity stands in for a
slope), `ground` (height or null), `objects[]` (`cube`, `sphere`, `grid`, or `model` with an `asset` glb
scaled so its tallest side is `size`, base on `location`; with `size`, `location`,
`rotation` in degrees, `subdivisions`), `colliders[]` (cloth: still shapes the sheet drapes over,
sent to Infinite as `collider.glb` into Cloth's collider pin), and `body` (rigid: `friction`, `bounce`,
`linear_damping`, `angular_damping`, `collider`), `cloth` (`pin` none/top/corners/edges,
`air_damping`, `vertex_mass`, `tension`, `shear`, `bending`) or `particles` (`rate`, `velocity`).

Unit mapping (`physics.patch` holds it):

| Blender | Infinite |
|---|---|
| Rigid damping d (Bullet: v × (1 − d) per second) | damping d' with e^(−10 d') = 1 − d |
| Rigid friction, restitution (ground = 1, Bullet multiplies) | friction, bounce |
| Cloth air damping a, vertex mass m (drag 0.01 a v, time in frames) | damping 1 − e^(−0.01 a fps / m / 120) per step, mass 1 |
| Cloth pin group | the same rule as Cloth's pin mode, on the rest bounds |
| Cloth bending b | bending √(0.02 b), so Blender's default 0.5 is Infinite's default 0.1 |
| Cloth collision distance 0.015 + collider outer thickness 0.02 | collider distance 0.035 |
| Collision cloth friction 5 (default) | collider friction 0.5 (default) |

`cloth_deer` needs a glb not kept in git (`DEER_GLB`, default `~/Downloads/Assets/deer.glb`) and is
skipped without it. `INFINITE_BIN` points the run at another build.

A case fails when its error rises more than 10% over `baseline_physics.json`.

## Sculpt cases (brush moves, not pictures)

```
python3 tools/blender-ab/sculpt.py --out /tmp/blender-ab-sculpt                  # every sculpt case
python3 tools/blender-ab/sculpt.py --out /tmp/blender-ab-sculpt drag_draw        # one case
python3 tools/blender-ab/sculpt.py --out /tmp/blender-ab-sculpt --set-baseline
python3 tools/blender-ab/sculpt.py --out /tmp/blender-ab-sculpt --reuse   # Blender's result kept, Infinite rerun
```

```
sculpt.py CASES ──case.json──> blender_sculpt.py prep (-b) ──> base.glb, base.blend, dabs.json
   dabs.json ──> blender_sculpt.py sculpt (a window, a few seconds) ──> truth.txt (points before/after)
   dabs.txt  ──> Infinite INFINITE_SCULPTTEST_STROKES ──> inf.txt (points before/after)
truth.txt + inf.txt ──> per-point move compared ──> err, gain; Workbench renders ──> board.png
```

Blender's brushes need a 3D view, so the sculpt step opens a Blender window. Each dab activates the
Essentials brush of the same kind, with radius and strength set on the brush (unified size off).

| Case | What it tests |
|---|---|
| `sculpt_<brush>` | one dab on a sphere (on a starting bump for Smooth, Pinch and Flatten) |
| `drag_<brush>` | one drag across the top: Blender a dab per its brush spacing, Infinite one per 0.15 radius as the 3D View does |
| `sculpt_deer` | drags on the owner's deer (`DEER_GLB`; skipped without it) |

| Number | Meaning |
|---|---|
| `err` | rms of (Infinite move − Blender move) / rms of Blender's move (0 = the same) |
| `gain` | Infinite's total move / Blender's (1 = as strong) |

Things Blender 5 needs that are easy to miss: `unprojected_size` is a diameter; the view matrices
must be refreshed (`rv3d.update()`) before the mouse is projected; Pinch and Flatten skip a stroke's
first sample, so a single dab gets a lead-in sample; a replayed stroke (`brush_stroke` exec) dabs at every
sample it is given and skips the brush's spacing, so the drag's samples are spaced by `brush.spacing` first.

Each drag is one stroke on the Infinite side too: its dabs land on the surface as the stroke began
(`SculptNode::BeginStroke`), as Blender's do, so a ridge rises out of the brush and levels off.
Infinite's brush strengths are fitted to the drags (what a user does). A case fails when its error
rises more than 10% over `baseline_sculpt.json`.

## Coverage and realism gaps (2026-11-10)

FLIP against Cycles, per engine (lower is better):

| Group | raster | live | settle |
|---|---|---|---|
| 31 coverage cases | 0.02–0.83 | ≤ 0.18 | ≤ 0.19 |
| tex_maps (after the texture port) | 0.10 | 0.49 → 0.08 | 0.48 → 0.06 |
| interior (after the sky-leak fix) | 0.83 | 0.80 → 0.16 | 0.19 |
| bed close-up | – | 0.35 → 0.23 | 0.31 |

The texture port gives Metal live and settle albedo, roughness, metallic and normal maps, plus sheen. glTF maps now
use REPEAT wrap, both in the app (SpawnGltfParts) and here.

Open gaps:
- Settle bed: the grazing 16° sun leaves the sheet darker than Cycles (brightness 0.70). This happens with no
  maps too, so the texture port is not the cause.
- Live has no depth of field, and its window patch is hard-edged.
- Still missing for close-up fabric: plush fuzz shells, true displacement in live/settle, sheen and coat on settle's
  bounces, alpha in shadow rays, and the new maps on the GL path tracer.

## Shading gaps filled (2026-11-10)

Metal live and settle now read every Material slot that GL reads, through the Mapping node:

| Feature | Where | Check |
|---|---|---|
| AO, emission, sheen, opacity, clearcoat, transmission maps | Metal live + settle | mat_alpha, mat_coat |
| Clearcoat lobe, specular level (F0) | Metal live + settle | mat_coat |
| Mapping (UV / generated / object, scale, rotate, offset, triplanar) | Metal live + settle | tex_tile, tex_detail (cube) |
| Detail normal: `detailScale`, `detailStrength` on Material | GL + Metal | tex_detail |
| Breakup (tile-repetition hiding) on Mapping | GL + Metal | tex_breakup, `tile_repeat.py` |

The detail normal reads the normal slot again at uv × `detailScale`, tilt × `detailStrength`, and whiteout-blends it on
top. No extra texture unit is needed, because all 16 of GL's units are in use. Breakup picks one of 8 tile offsets per
area from a slow noise and blends neighbours (after Quilez). Cycles has no twin for it, so `tex_breakup` is judged by
`tile_repeat.py` (the autocorrelation peak of the tile's dark spots), not by FLIP. Breakup lowers the repeat score from 0.34
to 0.21 in live and raster, and to 0.23 in settle.

FLIP against Cycles: tex_tile 0.06 live / 0.03 settle; tex_detail 0.25 live / 0.10 settle. Live's extra error is its
reflection glow on a glossy normal-mapped floor, which is there with detail off too. None of the 31 coverage cases got
worse.

## Bed realism (2026-11-10)

The `bed` case (the owner's photo: slept-in jersey sheet, fleece plush, low warm sun) was 1.2–1.3× too bright in both
engines, and settle's sunlit sheet was too dark before that. Three fixes:

| Cause | Fix | Where |
|---|---|---|
| A dropped glTF's linear `baseColorFactor` was copied into the sRGB colour control (too dark) | linear → sRGB on drop, emission too | StageDropHandling.cpp, `gltf_material` |
| Sheen added energy on top of an unscaled base | KHR_materials_sheen albedo scaling: base × (1 − max(sheenColor)·sheen·E(μ, r)), E as a 7×12 table | GL forward, Metal live + settle (incl. bounces) |
| AgX lifted darks up to 1.5× vs Blender: the old minimal 6th-order fit, decoded as piecewise sRGB | our own 8th-order fit to Blender 5.2's AgX measured on a 512-step grey ramp, decoded with the 2.4 power its Rec.1886 step uses; within 1.6/255 on grey | Render3DTonemap.h, MetalDevice.mm |

`bed`: FLIP 0.245 → 0.144 settle, 0.237 → 0.150 live; brightness 1.32 → 1.10 settle, 1.26 → 1.07 live (regions
0.98–1.08). What is left is mostly depth of field (Cycles' f/2.8; live has none) and live's hard sun-shadow edge.

Model objects take `"detail": [scale, strength]`: Infinite gets Material `detailScale`/`detailStrength`, Blender gets
the same whiteout blend spliced before the imported material's Normal Map node (`detail_normal`). The bed sheet uses
`[8, 0.6]`. `bed_close` is a 0.45 m look at the sunlit folds: the weave reads in all three with the same pattern and
brightness (crop mean 142 Cycles, 144 settle/live); Infinite's weave is a little crisper (high-pass σ 4.8 vs 3.4).

Coverage sweep (31 cases, live + settle) against the pre-fix baseline: no case got worse, none fail. Moved by AgX:
view_agx FLIP 0.071 → 0.031 live / 0.075 → 0.028 settle; glTF heroes (colour fix) hero 0.136 → 0.092 / 0.137 → 0.064,
deer 0.106 → 0.061 / 0.113 → 0.055; world_hdri settle 0.106 → 0.065. tex_maps (0.49 → 0.08) and interior (live
brightness 6.9 → 1.0) also dropped, but their baseline predates part of the shading-gaps work, so not all of that is this block.

## Depth of field and soft sun (2026-11-10)

| Cause | Fix | Where |
|---|---|---|
| `dofAperture` × 24 px / 1080 rows is the circle of confusion's diameter, but every path used it as a radius (2× too much blur) | halved: thin-lens radius and post blur radius | Render3DEngine.cpp, Render3DPathTraceCook.cpp, Render3DPost.cpp |
| settle (Metal) had no depth of field | thin lens in `pt_fs`: disc or bladed polygon on the camera's right/up, focused on the plane at `focusDist` along its forward | MetalDevice.mm (`PtU.lens/lr/lu/lf`) |
| live had no depth of field | `dof_fs` gather after the frame: per-pixel CoC from depth, 24–48 taps, a tap counts only where its own CoC reaches the pixel (no sharp-over-blur halos) | MetalDevice.mm (`DepthOfField`) |
| live's sun shadow was hard, settle's soft (sun angle 0.526° × shadow soft) | `sunVis`: 4 probe rays in a cross at 0.9 of the disc; where they disagree, 12 more on a hashed golden spiral (16 total) | MetalDevice.mm |

Four probes, not two: two opposite probes agree whenever they lie along the shadow edge, which snaps the penumbra to
0/1 dither. With a cross, one pair always spans the edge.

FLIP: dof 0.076 → 0.075 live, 0.058 → 0.057 settle; bed 0.150 → 0.139 live, 0.144 → 0.142 settle; bed_close
0.190 → 0.154 live. Raster's dof stays at 0.18 from a framing offset (its camera sits lower than Cycles'), which comes
before this block and isn't blur.

Coverage sweep (31 cases, live + settle) against the post-bed-realism run: no case moved by more than 0.004 FLIP,
none fail.

What is left on the plush is fur itself: see docs/plans/fur.

## Fur, hair, SSS and translucency (2026-11-11, Block C)

FLIP per engine after Block C (raster / live / settle). Lower is better; gates raster ≤ 0.20, live ≤ 0.15,
settle ≤ 0.05.

| case | raster | live | settle | note |
|---|---|---|---|---|
| `bed_fur` | 0.172 | 0.133 | 0.111 | fur ribbons over the plush |
| `bed_fur_close` | 0.339 | 0.227 | 0.230 | open: strand self-shadowing (PLAN 2.5) |
| `hair_long` | 0.126 | 0.091 | 0.073 | guide-combed strands, Hair model |
| `hair_melanin_0.2 / 0.5 / 0.8` | 0.141 / 0.137 / 0.135 | 0.118 / 0.085 / 0.072 | 0.071 / 0.065 / 0.066 | Chiang fibre BSDF; multi-scatter gain 4 (`HAIR_MS`) |
| `mat_subsurface` | 0.134 | 0.165 | 0.074 | settle random walk picks a channel by throughput; raster/live wrap widened by curvature × radius (gain 4) |
| `mat_translucent` | 0.024 | 0.088 | 0.028 | thin translucency, all engines |

No case got worse by more than 0.005 on any engine (full 43-case sweep, raster, live and settle). Every case on
every engine is now in `baseline.json` (`case`, `case_live`, `case_settle`), so the harness exit code guards all
three paths. Before/after sweeps: `sweep.py` (PLAN.md §5).
