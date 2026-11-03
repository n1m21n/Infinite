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
| `drag_<brush>` | one drag across the top: Blender spaces its own dabs, Infinite one per 0.15 radius as the 3D View does |
| `sculpt_deer` | drags on the owner's deer (`DEER_GLB`; skipped without it) |

| Number | Meaning |
|---|---|
| `err` | rms of (Infinite move − Blender move) / rms of Blender's move (0 = the same) |
| `gain` | Infinite's total move / Blender's (1 = as strong) |

Things Blender 5 needs that are easy to miss: `unprojected_size` is a diameter; the view matrices
must be refreshed (`rv3d.update()`) before the mouse is projected; Pinch and Flatten skip a stroke's
first sample, so a single dab gets a lead-in sample.

Infinite's brush strengths are fitted to the drags (what a user does). The cross-section still differs:
Blender's drag ridge has a flat top, Infinite's is peaked. A case fails when its error rises more than
10% over `baseline_sculpt.json`.
