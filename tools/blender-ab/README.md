# Blender A/B

Proves a 3D feature against Blender. The same scene goes through both apps,
and a number says how far apart the two pictures are.

```
python3 tools/blender-ab/harness.py --out /tmp/blender-ab            # every picture case
python3 tools/blender-ab/harness.py --out /tmp/blender-ab light_spot # one case
python3 tools/blender-ab/harness.py --out /tmp/blender-ab --set-baseline
python3 tools/blender-ab/harness.py --out /tmp/blender-ab --ao       # also the AO bake A/B
```

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
