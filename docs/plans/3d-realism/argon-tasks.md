# 3D realism: tasks for Argon

Two self-contained tasks from `Infinite_3D_Realism_Spec.md` (repo parent folder), Block 1 and the start of Block 2.
They touch files nobody else is editing. Claude does the shading fixes in Material / Render 3D after A1's numbers.

Rules for both (repo-wide):

- Build only through `scripts/ibuild.py`, one build at a time. The laptop is an M2 with 8 GB; two builds at once hang it.
- One branch per task, one commit per step. No "Co-Authored-By" or "Generated with" lines anywhere.
- Do not push and do not merge. Report the branch name and the results table back to the owner.
- Base: `main` once the "Close out 3D" merge has landed `feature/blender-ab-harness`. If `tools/blender-ab/` is not on main yet, branch from `feature/blender-ab-harness`.

---

## A1: realism cases in the Blender A/B harness (Python only, no C++)

Branch: `feature/realism-ab-cases`. Files: `tools/blender-ab/cases.py`, `tools/blender-ab/baseline.json`, and `blender_scene.py` / `harness.py` only where a new material input needs mapping.

Read `tools/blender-ab/harness.py` and `cases.py` first. The case format, the camera convention and the FLIP gate are already there; follow them.

Add these cases:

| Case | Scene | What it shows |
|---|---|---|
| `furnace_metal` | 5 spheres, base colour (1,1,1), metallic 1, roughness 0 / 0.25 / 0.5 / 0.75 / 1, no lights, world colour (1,1,1), no floor, view Standard | energy loss on rough metal: every sphere should vanish into the background |
| `furnace_dielectric` | the same with metallic 0 | energy gain or loss on rough plastic |
| `lineup` | gold (F0 1.0,0.77,0.34, rough 0.3), rough red plastic (rough 0.7), red car paint + clearcoat 1, velvet (sheen 1, rough 0.8), frosted glass (transmission 1, IOR 1.5, rough 0.3) on the grey floor, sun + grey world | each of the spec's lobes against Cycles |
| `cornell` | 2x2x2 m box open toward the camera, red left wall, green right wall, white rest, 0.5x0.5 m area light under the ceiling, one white sphere, one metal sphere | indirect bounce (raster will miss it; that is the baseline for the path tracer) |

Furnace metric, per sphere: mean linear luminance inside the sphere mask divided by the background. Print it per roughness for both apps. Cycles is the truth. Target for later: within 0.99-1.00.

Done when:

1. `python3 tools/blender-ab/harness.py --out /tmp/realism-ab furnace_metal furnace_dielectric lineup cornell` runs end to end.
2. `--set-baseline` records the four new FLIP scores in `baseline.json` and leaves the seven old ones unchanged.
3. A table goes back to the owner: case, FLIP, brightness ratio, furnace luminance per roughness (Infinite vs Cycles), plus the board PNGs.

Do not change any shading code to make a case pass. This task only records where Infinite stands today.

---

## A2: CPU BVH for the path tracer (new C++ files only)

Branch: `feature/pt-bvh`. Files: new `src/core/Bvh.h`, `src/core/Bvh.cpp`, a self-test in `src/app/selftest/` that follows the pattern of the existing self-tests, and the CMake entry. Touch nothing else.

What it builds (spec section G.2):

- BLAS per mesh: binned SAH, 16 bins, C_trav 1.0, C_isect 1.5, leaves of at most 4 triangles.
- TLAS over instances: world AABB, inverse 3x4 matrix, BLAS root index. Rebuilt whole each frame. Refit for a deformed BLAS (bottom-up union).
- Flattened for OpenGL 4.1 `samplerBuffer` (RGBA32F): 2 texels per node (min.xyz + left/first-prim as int bits; max.xyz + right/count as int bits), 3 texels per triangle (v0 + index, e1, e2 + material id). Children ordered so traversal can go near child first.
- A CPU `Intersect(ray)` that walks the flattened arrays exactly as the shader will, so the layout is tested before any GLSL exists.

Self-test (pass/fail):

- 10,000 random rays on a UV sphere, the Fox (`INFINITE_3DBENCH_FOX` path, see `docs/plans/3d-next/README.md`) and a 1M-triangle subdivided sphere: closest hit t and triangle id match brute force on every ray.
- TLAS: 3 instances with non-uniform scale and rotation; hits match brute force on transformed triangles.
- Refit after moving every vertex: still matches brute force.

Bench, one JSON line per row (M2, release build): build ms at 100k and 1M triangles, refit ms at 100k, TLAS rebuild at 1,000 instances, rays/s single-thread. The spec claims 100k in 8.3 ms and refit 0.25 ms per 100k. Report the measured numbers whatever they are; no tuning past the first working version.

Done when: the self-test passes, the bench table goes back to the owner, and the branch builds on macOS. The code is plain C++17 with no platform code, so it builds on Windows and Linux too.

Status: A1 and A2 done and checked (2026-11-03). A1 found a +1.8% energy gain on rough plastic, sheen too hot and clearcoat too dark; Claude fixes those in Material / Render 3D. Do not touch the Material, Render 3D or tonemap files while that runs.

---

## A3: A2 cleanup (same branch `feature/pt-bvh`, one commit)

Two things block merging A2:

1. `LoadFoxOrFallback` reads a hardcoded path in another session's temp folder. Read `INFINITE_3DBENCH_FOX` instead (see `docs/plans/3d-next/README.md`). If it is unset or the file is missing, skip the Fox row and print `fox: skipped (INFINITE_3DBENCH_FOX unset)`; the test still passes on the sphere rows.
2. The test starts from a static `BvhAutoDispatch` that reads env vars and calls `_Exit`. Register it as an ordinary self-test the same way the existing ones are registered (see the `run-infinite-hygiene` skill and `src/app/selftest/`), with the bench as a flag on that test. No static initialisers.

Done when: the self-test passes through the standard entry with and without `INFINITE_3DBENCH_FOX`, and `grep -rn "scratchpad\|/tmp/claude" src/` finds nothing.

---

## A4: path tracer prototype (new branch `feature/pt-prototype` off `feature/pt-bvh`, new files only)

Spec section G.3 (Option A: OpenGL 4.1 fragment shader, no compute). This is the path tracer that Render 3D will later switch to for final renders. Build it standalone first; wiring it into the Render 3D node comes after Claude's shading fixes land, so it can share the fixed BSDF.

Files: new `src/nodes/Render3DPathTrace.h/.cpp`, its GLSL (embedded string, like the other Render 3D shaders), a self-test, CMake. Do not edit any existing Render 3D, Material or tonemap file.

What it does:

- Uploads A2's flattened TLAS/BLAS/triangle arrays to `samplerBuffer` textures (RGBA32F), plus a per-material buffer (base colour, metallic, roughness, emission) and the scene's lights (up to `kMaxLights`, `src/nodes/Geometry3DNodes.h`).
- One fragment-shader pass per sample into an RGBA32F accumulation target (ping-pong), averaged progressively. Stack-based traversal near child first, the same walk as A2's CPU `Intersect`.
- BSDF for now: Lambert + GGX metal only, with next-event estimation on area and point lights and the uniform world colour as the miss. Max 4 bounces, Russian roulette after 2. Clearcoat, sheen and transmission wait for the shared BSDF.
- Sample budget per frame as a parameter, so the canvas stays live (M2 8 GB: a frame must not block the UI for more than ~16 ms).

Self-test (pass/fail):

- Renders the A1 `cornell` scene headless at 512x512, 256 spp, writes a PNG, and runs it through the same comparison the harness uses against the Cycles image already in the A1 output. Report FLIP and the brightness ratio next to raster's 0.5837.
- Pass: FLIP below raster's score and visible colour bleed from the red and green walls onto the white sphere.
- Bench, one JSON line: ms per sample at 512x512 for cornell and for the Fox (if `INFINITE_3DBENCH_FOX` is set).

Done when: the self-test passes, the cornell board PNG and the bench line go back to the owner, the branch builds on macOS, and no existing file outside CMake changed.

Before starting A4 (A3 checked and accepted 2026-11-03, `4e5bd21b`):

- Merge local `main` into `feature/pt-bvh` first. Main now has the A/B harness, the realism cases and the shading fixes (`ddcee23d`); `feature/pt-bvh` does not.
- Render 3D now uses the exact sRGB curve, not gamma 2.2. The path tracer's output encode must use the same `toSrgb` as `Geometry3DNodes.cpp`, or the FLIP against Cycles is skewed.
- The raster cornell score to beat is now 0.583 (`tools/blender-ab/baseline.json`). Regenerate the Cycles truth with the current harness (`harness.py --out <dir> cornell`) rather than reading the old `/tmp/realism-ab` copy.
- The `INFINITE_HAS_PT_PROTOTYPE` guard in `Startup.cpp` is never defined. Drop the guard when A4 adds the files, so the test is always registered.

---

## A5: hero scene, every 3D aspect in one A/B case (Python + assets, no C++)

Branch: `feature/realism-hero-case` off local `main` (main has the harness since `ddcee23d`). Files: `tools/blender-ab/` only, plus the assets. Run it in parallel with A4; do not touch A4's files.

Why: every current case tests one thing. This one tests them together, and scores each part of the picture on its own, so a regression names what broke and the scorecard becomes the to-do list for the path tracer and the raster fixes.

Scene (one case, `hero`, 3 frames):

| Part | What it exercises |
|---|---|
| Room: floor + 2 walls, white and one coloured wall | bounced light, colour bleed |
| HDRI world (Poly Haven, CC0) + sun | environment light and reflections |
| Point + spot + area light, each placed to cast a visible shadow | shadows from more than one light |
| 1-2 textured Poly Haven models (CC0) | albedo, normal, roughness, metal, AO maps through the glTF path |
| Glass sphere, clear-coated paint, velvet | each lobe on top of the room light |
| The Fox (`INFINITE_3DBENCH_FOX`), skinned, at 3 frames of its clip | skinning over time |
| 50 small instances on the floor | instancing |
| Emissive panel | emission |
| Camera with DOF, AgX view | depth of field, tonemap |

Owner approved (2026-11-03) fetching the CC0 assets once from Poly Haven. Keep them under `tools/blender-ab/assets/` with a `SOURCES.md` (name, URL, licence). Keep the total under about 30 MB; if larger, keep them out of git and fetch them with a script instead.

Harness work this needs (map each new input on both sides, Blender and Infinite):

- An object `shape: "model"` with a file path, an HDRI world, the Fox with an animation frame, instances, an emissive material.
- More than 3 lights: `harness.py` exits at 3. Find why first. If it is the Render 3D node's light inputs, use 3 lights and let the HDRI's sun be the fourth, and say so in the report.
- Region masks: have Blender also write an object-index pass. Group the indices into regions (room, models, glass, coat, velvet, fox, instances, emissive) and score FLIP and the brightness ratio per region, plus the whole frame. The same mask is used on both images.
- `baseline.json` stores the hero scores per region and per frame. The gate fails when any region gets worse, not only the whole frame.

Done when:

1. `python3 tools/blender-ab/harness.py --out <dir> hero` runs end to end, all 3 frames.
2. A table goes back to the owner: region x frame, FLIP and brightness ratio, plus the 3 board PNGs and a mask PNG.
3. `--set-baseline` records it and the 11 existing cases are unchanged.

Do not change shading code to make a region pass. This records where Infinite stands; the worst regions are the next work.
