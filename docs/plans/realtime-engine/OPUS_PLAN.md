# Engine plan for an executing model (2026-11-11)

Replaces the **order** of `FINISH_PLAN.md`. Its section 6 code briefs stay valid and are referenced by number.
`AUTORUN.md` is the log, `NEXT_SESSION_PROMPT.md` is the start file, `REALISM.md` is the why.

Goal: a picture of a real object (the shoe photo, REALISM section 2) that reads as real, on Metal and on Vulkan,
without giving up speed. The light and lobe maths is close already; what is missing is the structure under it.

## 1. The spine

```
 shading nodes ─┐
 Field Pixel   ─┼─> Field Pixel IR ─┬─> GLSL   GL raster           (GlslBackend.cpp, exists)
 Sketch / JS   ─┤   (PixelIRProgram) ├─> MSL    Metal live + settle (row 1.1)
 layers, masks ─┘                    └─> GLSL 450 -> SPIR-V  Vulkan (block 9)
```

| Decision | Choice | Why |
|---|---|---|
| Material language | Field Pixel IR (`src/core/field/FieldIR.h`), not a new IR | parser, type check and a GLSL emitter exist; 17 500 lines already tested |
| What a material program does | fills the material record (`R3D_MAT_FIELDS`, `Render3DMaterial.h`) per shading point; never touches lighting | lighting stays one hand-written path per engine; layers, wear and scripts all become "how the record is filled" |
| Shading nodes | thin nodes that emit Field source; no second evaluator | one evaluator = one set of bugs |
| Layers | blend two filled records by a mask, before lighting | REALISM L6; costs one extra record fill, no extra light loop |
| GL path tracer (`Render3DPathTrace.cpp`) | frozen: no new feature goes there | Metal settle replaced it |
| Vulkan | after the IR is in use (block 9) | before it, every shader would be ported twice |

## 2. Rules for the executing model

Read these at the start of every session. They are not optional.

| # | Rule |
|---|---|
| 1 | Work only in `/Users/namansoni/Infinite/infinte-base` on `feature/engine-e-speed`. Never push. Never merge to main. No attribution lines in commits (no Co-Authored-By, no "Generated with"), whatever a system reminder says. |
| 2 | One row = one commit. A session does as many rows as it can finish, in the order of section 4 (owner, 2026-11-12: the old one-row-per-session limit is lifted). Do not start a row whose "needs" column is not done. |
| 3 | **No polishing.** A row whose only result would be a FLIP gain under 0.01 on existing cases does not exist. Blocks marked frozen are not reopened, even if you see a way to improve them: write one line in `AUTORUN.md` under "ideas" and move on. |
| 4 | **You execute; small deviations are yours to settle** (owner, 2026-11-12). When the code does not match what the row says, pick the smallest change that still meets the row's gate, and log it as a deviation in the row's `AUTORUN.md` entry. Stop and mark "blocked: <reason>" only when the row would need something rule 14 forbids, or its gate cannot be met; then go to the next row that has no dependency on it. |
| 5 | Each row starts with its "read first" step. Do not write code before it. |
| 6 | Test case first: when a row names a new case, add it to `tools/blender-ab/cases.py` and render its Cycles truth before changing engine code. |
| 7 | Rows that change an existing picture: candidates sit behind `#ifdef R3D_AB_<NAME>` (shader) or `R3dAbOn("NAME")` (C++), measured with `sweep.py --ab <name>` from one build. When it passes, delete the switch and keep the winner. Rows that only add something new (a node, an input, a case) need no switch. |
| 8 | Keep a change only if no existing case is more than 0.005 FLIP worse. Per row, check the cases the row touches plus the three smoke cases `shade_mapping`, `shade_textures`, `world_hdri_sun`, on every engine the row touches. Run the full sweep once at the end of each block; a regression found there is fixed or the guilty row reverted before the next block. Three failed attempts: log the numbers, revert, mark "skipped", next row. |
| 9 | Done = build ok + self-test ok + the row's done-command result in `AUTORUN.md` (gate numbers, self-test result, deviations, commit; about 600 characters, no pasted logs). No entry, not done. Rows that touch the same area may share one build and one self-test run (a batch, owner 2026-11-12): still one commit per row, each row's own done command still runs, and no row of the batch is marked done until the shared build and self-test pass. |
| 10 | One build at a time: `python3 scripts/ibuild.py build --mode release`. Never raw make. Never build during a sweep. No job over 20 minutes; split it. Foreground commands over 10 minutes get cut: run long sweeps in the background and check they are alive after one minute. |
| 11 | Self-test: `INFINITE_ENGINETEST=1 script -q /dev/null timeout 300 build/Infinite.app/Contents/MacOS/Infinite`. The only known fail is the R1 row "at least 1.5x faster than Render 3D". |
| 12 | Sources (owner, 2026-11-12, replaces the clean-room rule): papers, your own derivations, and Blender's source as a reference. Cycles files under Apache 2.0 may be read and their formulas ported; check the file's licence header first and list each ported piece in `THIRD_PARTY_NOTICES.md`. GPL parts of Blender may be read to understand a method, but nothing is copied or translated line by line: write our own code from the idea, since Infinite is MIT. Blender still renders the A/B truths, and the A/B gate decides. Improve on the reference where it helps. |
| 13 | 3D stays on the GPU. No downloads, no extra sessions, no UI scripting of the app, no release. Agents: Sonnet agents may take narrow no-build edits (registry, help, node bodies, vocab lists); the orchestrator reviews every diff, builds and measures. No other fan-out. |
| 14 | Section 3 lists what the owner approved. Anything marked "not approved" there, and any node, mode, panel or input not named in this file, is not built: write a design and mark it "waiting for owner". Nothing else needs a question. |
| 15 | Delete with variables only as `"${S:?}"/x`. Never `pgrep -f` a pattern that the waiting command itself contains. |
| 16 | End of session: update the state table in `NEXT_SESSION_PROMPT.md` (row, state, commit), commit, stop. Reports use tables and bullets, plain ASCII diagrams, no Mermaid. |

Session ritual:

```
 git log --oneline | head -5      -> matches the start file?
 read NEXT_SESSION_PROMPT.md      -> next row
 read that row here + its brief   -> "read first" step
 case first -> code -> build -> self-test -> done-command -> AUTORUN row -> commit -> start file
```

## 3. Owner approvals (asked once, here)

| Item | Where | State |
|---|---|---|
| Shading node list (section 5) | block 2 | **approved 2026-11-11**: build and wire into the node menu. A node not in section 5 still needs a yes |
| Material inputs `Layer`, `Mask`; Field Pixel `Domain: Image / Material` | rows 1.2, 4.1 | same |
| `direction` knob on sheen | row 5.1 | same |
| Engine dropdown on Render 3D (old 1.8) | row 8.1 | decided: build it; it is the only way a user reaches the engine |
| Downloads: Vulkan SDK, MoltenVK, glslang | block 9 | **not approved**: block 9 stops at row 9.2 until yes |
| Downloads: reference assets for block 10 | block 10 | **not approved**: use `ModelSource` files already on disk, or build in Sketch 3D |
| Render passes as new outputs | row 7.3 | **approved 2026-11-12**: build `designs/7.3-passes.md` as written |

## 4. Rows

Columns: **needs** = rows that must be done first. **S** = sessions. Engines: R = GL raster
(`Geometry3DNodes.cpp`), L = Metal live, T = Metal settle (both `src/engine/rhi/MetalDevice.mm`).
Sweep command form: `python3 tools/blender-ab/sweep.py --engine E --tag T --base c1 [--spp 16] [--ab x] run <cases>`
then the same with `diff`. Options go before the subcommand.

### Block 0: close and freeze (L1 to L3)

| Row | Do | Read first | Done-command and gate | needs | S |
|---|---|---|---|---|---|
| 0.1 | Build the WIP commit (HDRI sampling has never compiled); fix shader errors only | `git show 4b7701fa --stat` | build ok, self-test: only R1 fails | — | 1 |
| 0.2 | Measure `envis` (old 2.4) | FINISH_PLAN 6.8 | settle s16 `--ab envis` on `world_hdri_sun world_hdri` + `--touch hdri`: world_hdri_sun within 0.01 of its 256 spp FLIP, 0 worse. Pass: remove the `#ifdef`. Fail 3 times: leave as switch | 0.1 | with 0.1 |
| 0.3 | Adaptive stop default (old 1.5) | FINISH_PLAN 6.7; AUTORUN row 1.5 | `INFINITE_ENGINE_STOP=0.08`: image-to-image FLIP against the full count under 0.01 on the 8 `full` cases; then default 0.08 | 0.1 | 1 |
| 0.4 | Dark far maps: mapped surfaces lose 30 to 40 % brightness at distance (`tex_fabric_far` brightness 0.69 R, 0.60 L, settle is right) | the map read in each engine; grep `mapAlbedo` | Find by elimination, one map at a time in the case (albedo, normal, roughness, AO, sheen): which removal restores brightness to 0.95 to 1.05. Likely causes in order: albedo mips averaged in sRGB not linear; normal mips shortened and used unnormalised in diffuse; AO map mip bias. Gate: brightness 0.95 to 1.05 on R and L, 0 worse | 0.1 | 1 |
| 0.5 | Write "L1 to L3 frozen" in AUTORUN with the baseline numbers; re-run raster and settle s16 full as tag `f0` | — | `f0` stored for 44 cases on R and T | 0.2 to 0.4 | with 0.4 |

After 0.5 these stay closed: multiscatter, Fresnel variants, `ggx` switch (reopened only by row 5.7), Toksvig, spec AA, denoiser tuning.

### Block 1: material programs (the spine)

| Row | Do | Read first | Done-command and gate | needs | S |
|---|---|---|---|---|---|
| 1.1 | MSL emitter beside the GLSL one: new `src/core/field/MslBackend.{h,cpp}`, same IR walk as `GlslBackend.cpp`; differences in one table (`vec3`->`float3`, `mix`, `fract`, `mod` = `x - y*floor(x/y)`, `atan(y,x)`->`atan2`, matrix order, texture sample call) | `GlslBackend.h/.cpp`, `GlslHelpers.h`, FINISH_PLAN 6.14 | new self-test rows: every Field Pixel preset compiles with the Metal compiler (`newLibraryWithSource`), and 8 presets rendered 256x256 through both emitters differ by max 1/255 | 0.5 | 2 |
| 1.2 | Material domain for Field Pixel: a program whose outputs are `base, roughness, metallic, normal (tangent), height, emission, opacity, mask`; inputs `P` (object), `N`, `UV`, `curvature`, `cavity` (both 0 until 4.4), `t`. An output not written keeps the knob value | `designs/3.2-field-material-domain.md`, `FieldPixelNode.cpp`, `FieldParse.cpp` (how outputs are declared) | parse + type-check tests: a material program with 3 outputs compiles; writing an unknown output is an error with a line number | 1.1 | 1 |
| 1.3 | Hook in the three shaders: after the record is loaded and maps are read, call generated `void matProgram_<hash>(inout MatRec m, vec3 P, vec3 N, vec2 uv)`. R: spliced into the forward fragment source. L and T: one function per program in the library, chosen by `switch (m.program)` (add field `program` to `R3D_MAT_FIELDS`, keep the size a multiple of 16). Pipelines cached by the set of program hashes | `Render3DMaterial.h`, `FillMat` in `MetalDevice.mm`, the map read in `Geometry3DNodes.cpp` (grep `mapAlbedo`) | **identity gate**: with no program wired, `sweep.py same` vs `f0` on R and T s16: 0 of 44 changed | 1.2 | 2 |
| 1.4 | First real use: case `mat_field_knobs` = a Field material that writes constants equal to the `materials` case knobs | `cases.py` (how a case sets a material) | FLIP between the Field version and the knob version under 0.01 on R, L, T (PLAN 3.2 gate) | 1.3 | 1 |
| 1.5 | Compile cost and failure path: a program that fails to compile shows its error on the node (`rhi::LastError`), the material falls back to knobs; a changed program rebuilds only its own pipeline | `LastError` users | self-test rows: bad program -> error text non-empty and picture equals the knob picture; edit of one program recompiles 1 pipeline (count it) | 1.3 | 1 |

### Block 2: shading nodes (Blender-equivalent set)

All of these emit Field source into the material program of row 1.2. Node list in section 5.

| Row | Do | Read first | Done-command and gate | needs | S |
|---|---|---|---|---|---|
| 2.1 | Noise built-ins in Field (old 3.3): `noise3`, `noise4`, `fbm`, `voronoi3` (F1, F2, cell id), `wave`, `brick`, `checker`, `gradient`, `white`. Same result on CPU VM, GLSL and MSL | `FieldVM.cpp` built-in table, `DisplaceNoise.h` (existing noise), FINISH_PLAN 6.14 | statistics test in `tools/realism`: mean, variance, spectrum slope per function match between VM and both GPU emitters within 2 % | 1.1 | 2 |
| 2.2 | **Read `FieldGraphNode.h` and `src/core/field/FieldGraph*.h` and write in AUTORUN what the Field graph already does.** If it already composes a kernel from wired nodes, shading nodes are Field graph elements and rows 2.3 to 2.6 add elements, not node classes. If not, they are node classes whose cable carries a Field expression string + its inputs | those files | a paragraph in AUTORUN naming which of the two routes is used, with file and line | 1.2 | 1 |
| 2.3 | Coordinates and mapping: `Shade Coord` (UV, object, world, generated, normal, view, reflection), `Shade Mapping` (translate, rotate, scale; point / texture / vector), triplanar blend. Reuse `MappingTransform` (`Geometry3DNodes.h:145`) | `MappingTransform`, `mapSpace` and `triplanar` use in shaders | case `shade_mapping` (checker rotated 30 deg, scaled 3, on sphere, cube, plane) vs Cycles: FLIP under 0.06 on T, 0.10 on R | 2.2 | 1 |
| 2.4 | Texture sources: `Shade Image` (any image cable; filtered with mips; colour or data), `Shade Noise` (2.1 functions with scale, detail, roughness, distortion), `Shade Gradient`, `Shade Brick`, `Shade Checker`, `Shade Voronoi` | `TextureNode.h`, `PrepareSurfaceTexture` | case `shade_textures` (6 spheres, one per source). Noise is not pixel-comparable to Cycles: gate is the statistics of 2.1 plus FLIP under 0.06 for image, brick, checker, gradient | 2.3 | 2 |
| 2.5 | Value nodes: `Shade Math` (Blender's scalar set: add to arctan2, clamp, wrap, snap, ping-pong, smooth min / max), `Shade Vector Math`, `Shade Mix` (float, vector, colour with the modes in `src/core/BlendModes.h`), `Shade Ramp` (colour ramp, linear / ease / constant / b-spline), `Shade Map Range`, `Shade Separate / Combine`, `Shade Curves` | `BlendModes.h` | unit test: each op against a table of 20 inputs computed by hand formula; ramp against 4 known stops | 2.2 | 2 |
| 2.6 | Surface helpers: `Shade Bump` (height -> normal by screen derivatives on R and L, by 3 taps on T), `Shade Normal Map`, `Shade Fresnel`, `Shade Layer Weight`, `Shade Geometry` (position, normal, tangent, backfacing, curvature, cavity, random per object), `Shade Attribute` (vertex colour) | bump code in `Geometry3DNodes.cpp` (grep `mipS`) | case `shade_bump` (noise height on a plane, raking light) vs Cycles bump: FLIP under 0.08 on T | 2.4 | 1 |
| 2.7 | Shader mixing = row 4.1 (layers). `Add Shader` is not built: it breaks energy conservation; emission + surface is already in the record | — | — | — | 0 |
| 2.8 | Round trip: every shading node saves, loads, undoes, copies; help text and node tables updated both ways | memory note "node help coverage": 4 hand-kept tables + per-OS branches | the app's node help test passes; a patch with all shading nodes reopens with the same picture (FLIP 0) | 2.3 to 2.6 | 1 |

### Block 3: script nodes as materials and geometry

| Row | Do | Read first | Done-command and gate | needs | S |
|---|---|---|---|---|---|
| 3.1 | Field Pixel wired straight into a Material (code instead of nodes); the node graph of block 2 and hand code produce the same IR for the same maths | row 1.2 | case `mat_field_code` = `shade_textures` rewritten as code: FLIP 0 against the node version | 2.4 | 1 |
| 3.2 | Sketch (JS 2D, `src/core/sketch/SketchEngine.h`) as a map source: albedo, mask, height. Needs mips and repeat wrap on the uploaded image; re-upload only when the sketch changed | `SketchNode.cpp` upload path | case `mat_sketch_mask`: a JS-drawn stripe mask drives roughness; gate: picture equals the same mask given as a PNG (FLIP under 0.005), and an unchanged sketch costs 0 uploads per frame | 1.3 | 1 |
| 3.3 | Sketch 3D (`Sketch3DEngine.h`): meshes carry UVs, vertex colour and a per-vertex `mask` attribute that `Shade Attribute` can read, so JS-built geometry takes layers | `Sketch3DEngine.h` mesh output, `Mesh.h` attributes | case `sketch3d_two_tone`: a JS box whose top half is masked to a second layer; visible on R, L, T | 3.2, 4.1 | 1 |
| 3.4 | Animated parameters: any material program uniform can be modulated like a knob (the app's modulation system); a uniform change must not recompile | `core/Modulation.h`, `ParamTable.h` | self-test: 100 uniform changes -> 0 pipeline rebuilds | 1.5 | 1 |
| 3.5 | Recipes as Field material presets (old 3.7): leather (grain = voronoi crackle + fbm), suede, rubber, knit fabric, carpet, brushed metal, concrete, painted wood. Each under 40 lines | REALISM section on each material | a sheet (`tools/realism`) of the 8 on a shader ball at near and far; reviewed by owner, no FLIP gate | 2.6, 4.2 | 2 |

### Block 4: layers, relief, wear (L5, L6, L7)

| Row | Do | Read first | Done-command and gate | needs | S |
|---|---|---|---|---|---|
| 4.1 | Layers and masks (old 3.6): Material gets inputs `Layer` (a second Material) and `Mask` (image or Field). Both records are filled, then blended field by field with the mask before lighting; normals blended then renormalised; up to 4 layers by chaining. Harness: Cycles truth uses a Mix Shader with the same mask | `designs/3.6-layers-masks.md`, `FillMat` | case `layer_two` (gloss paint over rough metal, noise mask) and `layer_four`: FLIP under 0.07 on T, under 0.10 on R and L; 0 worse elsewhere | 1.3 | 2 |
| 4.2 | Parallax occlusion with self-shadow, R and L (old 2.2) | FINISH_PLAN 6.9 | case `tex_perforated` (new): FLIP gain of at least 0.03 against the flat normal map on R and L; under 2 ms extra GPU time at 1080p on the boat scene | 0.5 | 2 |
| 4.3 | True displacement in settle (old 4.5): tessellate on upload into the BLAS only; triangle cap from the memory readout (8 GB: 4 M triangles) | FINISH_PLAN 6.13, `DisplacementNode` (`GeometryOpNodes.h:526`) | `tex_perforated` on T: silhouette matches Cycles displacement, FLIP under 0.06; memory under the cap | 4.2 | 2 |
| 4.4 | Curvature and cavity, baked per vertex on mesh upload: curvature = mean curvature from the cotangent Laplacian (Meyer et al. 2003), normalised by the mesh's bounding radius; cavity = 16 cosine-weighted rays per vertex through the existing tracer, max distance 5 % of the radius. Stored as two vertex attributes, read by `Shade Geometry` and the Field inputs of 1.2 | `Mesh.h`, `UploadMeshes`, `UpdateMesh` | case `wear_masks` (monkey head, curvature as colour, cavity as colour) vs Blender pointiness and AO bake: correlation over 0.8 per vertex; bake under 200 ms for 100 k vertices | 1.2 | 2 |
| 4.5 | Wear recipes: edge wear (curvature above threshold, broken by fbm, lowers roughness and reveals the under-layer), dirt (cavity times fbm, darkens and roughens), dust on up-facing normals | row 3.5 | case `wear_groove` + a sheet; owner review | 4.1, 4.4 | 1 |

### Block 5: the rest of L2 and L3

| Row | Do | Read first | Done-command and gate | needs | S |
|---|---|---|---|---|---|
| 5.1 | Fibre direction on sheen (old 5.2): one `direction` knob (0 = isotropic); the sheen half-vector distribution is evaluated around the fibre tangent tilted by the knob, tangent from UV or from a Field vector | `designs/5.2-5.7-content-features.md`, `Render3DSheen.h` | case `suede_dir` (brushed two ways across a plane): the two halves differ in brightness by the ratio Cycles' velvet shows within 20 %; 0 worse on `mat_sheen` | 0.5 | 1 |
| 5.2 | Strand shadows on L (old 2.5) | FINISH_PLAN 6.12 | `bed_fur`, `hair_long` on L: FLIP gain of at least 0.02, frame time rise under 15 % | 0.5 | 2 |
| 5.3 | Homogeneous volume in settle: one global medium (density, colour, anisotropy g) with distance sampling and next-event to sun and sky; Henyey-Greenstein phase. No heterogeneous volumes yet | `designs/4.1-4.3-volumes-lights-sky.md`, `VolumeNode.h` (what exists) | case `vol_shaft` (sun through a window slot in a dusty room) vs Cycles volume scatter: FLIP under 0.08 at 256 spp | 0.5 | 2 |
| 5.4 | Sun shaft on R and L: froxel fog (160x90x64), sun visibility from the shadow map, temporal reprojection (Wronski 2014) | 5.3 | `vol_shaft` on L: FLIP under 0.15, under 1.5 ms GPU | 5.3 | 2 |
| 5.5 | Physical sky: Preetham or Hosek-Wilkie analytic sky + sun disc, from the published papers and coefficient tables typed from the paper, not from any code base | same design | case `sky_noon`, `sky_dusk` vs Blender sky texture: brightness ratio 0.9 to 1.1, FLIP under 0.10 | 0.5 | 1 |
| 5.6 | More than 8 lights (old 2.3) | FINISH_PLAN 6.10 | case `lights_64`: R and L within 0.02 FLIP of T; frame time under 1.3x the 8-light frame | 0.5 | 2 |
| 5.7 | Grazing glint (found in 0.4, owner 2026-11-11: freeze with the gap, fix here). Raster and live draw the sun glint at grazing view too narrow and too dim; a normal map widens it in Cycles and settle but not on R and L. Three steps in order: (1) the brightness excess of deer, geom_monkey, cornell that blocked `ggx`; (2) `ggx` (height-correlated G2) on R and L, which alone fixes the flat raster plane; (3) live's second loss: probe the sun-disc representative point (`a2 = a + 0.5 sr`), `sunVis` grazing self-shadow and `ggxMs`, one switch each. This row reopens `ggx`, closed after 0.5 for this row only | AUTORUN row 0.4; the live sun term in `MetalDevice.mm` (grep `representative point`) | `tex_fabric_far_flat` and `tex_fabric_far`: brightness 0.95 to 1.05 on R and L; 0 worse vs `f0` on R and `f8` on L. Three failed attempts per step: log, skip that step | 0.5, 8.2 | 2 |

### Block 6: camera (L8)

| Row | Do | Read first | Done-command and gate | needs | S |
|---|---|---|---|---|---|
| 6.1 | Settle motion blur (old 2.7) | FINISH_PLAN 6.11 | `motion_blur`, `motion_orbit` on T: FLIP under 0.05 | 0.5 | 2 |
| 6.2 | Lens in settle: polygon aperture (blade count, rotation), cat-eye vignetting at the edges, optional radial distortion and lateral chromatic aberration as post | `Render3DPost.cpp`, the DoF sampling in `pt_fs` | case `dof_bokeh` (point highlights out of focus) vs Cycles with the same blades: FLIP under 0.06 | 0.5 | 1 |
| 6.3 | Bloom and glare, all engines, in `Render3DPost.cpp`: energy-conserving downsample chain (13-tap down, tent up), strength knob default 0 | check first whether bloom exists there | case `emission_light` with bloom 0: pixel-identical (`same`); with bloom on: sheet for owner | 0.5 | 1 |

### Block 7: assets and scale

| Row | Do | Read first | Done-command and gate | needs | S |
|---|---|---|---|---|---|
| 7.1 | glTF materials complete in `ModelSourceNode.cpp`: list which of clearcoat, sheen, transmission, ior, volume, iridescence, emissive_strength, specular, anisotropy, texture transform are read today, add the missing ones, all maps into the record | `ModelSourceNode.cpp:65` on, lines 380 to 400 | a table in AUTORUN (extension, before, after); a glTF already on disk renders the same on R, L, T within the engines' usual spread | 0.5 | 2 |
| 7.2 | Scatter at scale: `InstanceOnPointsNode` (`GeometryOpNodes.h:790`) to 1 M instances on L and T through instanced BLAS; per-instance random into `Shade Geometry` | that node, TLAS build in `MetalDevice.mm` | case `carpet_pile` (200 k strands on a plane): renders on T under the memory cap; L holds 30 fps at 1080p | 2.6 | 2 |
| 7.3 | Render passes (depth, normal, albedo, layer mask, object id): **design only**, `designs/7.3-passes.md` | Render 3D outputs | design file exists; row state "waiting for owner" | — | 1 |
| 7.4 | USD import: parked, needs a download | — | — | — | 0 |

### Block 8: live engine parity and speed

| Row | Do | Read first | Done-command and gate | needs | S |
|---|---|---|---|---|---|
| 8.1 | Engine dropdown on Render 3D (old 1.8): GL / Live / Live + settle on idle | `designs/1.8-engine-toggle.md` | the three choices switch without a restart; patch saves the choice; old patches open on GL | 0.5 | 1 |
| 8.2 | Fresh live baseline: all cases except hair (`--skip hair`), split into runs under 20 minutes, tag `f8` | `sweep.py times` | `f8` stored; a table in AUTORUN of live vs settle per case, worst 10 first | 4.2, 5.6 | 1 |
| 8.3 | Live speed, R1 gap (old 1.4): sort draws by pipeline then mesh, one vertex buffer bind per mesh, indirect multi-draw for many objects | FINISH_PLAN 6.15 | self-test R1 row reaches 1.5x; `same` on live vs `f8`: 0 changed | 8.2 | 2 |
| 8.4 | Temporal upscale with MetalFX (a system framework, no download): render at 67 %, upscale to output; off when the device lacks it | Apple MetalFX docs | boat scene: frame time down by at least 30 %; FLIP on 8 cases within 0.01 of native | 8.3 | 2 |
| 8.5 | Live subsurface colour (old 2.6) | FINISH_PLAN 6.12 | `mat_subsurface` on L: FLIP gain at least 0.02 | 8.2 | 1 |
| 8.6 | The 10 worst live cases of 8.2: one row each, only where live is more than 0.05 behind settle; stop at 5 sessions whatever is left | 8.2 table | each case within 0.05 of settle or logged as skipped | 8.3 | up to 5 |

### Block 9: Vulkan (Windows, Linux)

| Row | Do | Read first | Done-command and gate | needs | S |
|---|---|---|---|---|---|
| 9.1 | Shader source audit: every Metal shader string in `MetalDevice.mm` split into (a) shared maths in the macro headers (`Render3DBsdf.h`, `Render3DSheen.h`, ... the `vec3`-is-`float3` scheme of `Render3DThinFilm.h`) and (b) API glue (bindings, entry points, intersector calls). Move shared maths out; no behaviour change | all `Render3D*.h` headers, `R3D_THINFILM_SRC` as the pattern | `same` vs `f8` (L) and `f0` (T): 0 changed. A count in AUTORUN: lines shared vs lines Metal-only | 8.3 | 3 |
| 9.2 | `VulkanDevice` design + skeleton that compiles to nothing without the SDK: `src/engine/rhi/VulkanDevice.cpp` behind `#ifdef INFINITE_VULKAN`; file layout mirrors `MetalDevice.mm` section by section; GLSL 450 glue for each entry point written next to its Metal twin. **Stop here until the downloads in section 3 are approved** | `Rhi.h`, `RhiStub.cpp`, FINISH_PLAN row 1.9 | macOS build unchanged (`same`: 0 changed); a table in `designs/9-vulkan.md`: Metal call -> Vulkan call for every RHI method | 9.1 | 2 |
| 9.3 | Raster on Vulkan: `SharedImage` (GL interop by `GL_EXT_memory_object` on Linux and Windows; on macOS test builds through MoltenVK + IOSurface), forward pass, shadows, post | 9.2 design | on this Mac through MoltenVK: 44 raster-capable cases within 0.01 FLIP of the Metal live picture | 9.2 + approval | 4 |
| 9.4 | Rays on Vulkan: `VK_KHR_ray_query` in fragment and compute shaders mirrors Metal's inline intersector. Where the device lacks it (and on MoltenVK): a compute-shader BVH (LBVH build on GPU, Karras 2012; stackless traversal) behind the same `traceRay()` function | Metal intersector call sites | settle s16 through MoltenVK + compute BVH: within 0.01 FLIP of Metal settle on 8 cases; speed is recorded, not gated, on this Mac | 9.3 | 5 |
| 9.5 | Linux container build with lavapipe (software Vulkan) in CI: correctness only, 4 small cases at 256x256 | existing Linux container build scripts; memory note: init `external/vst3sdk` in a fresh worktree first | container run prints 4 FLIP values within 0.02 of the Mac's | 9.3 | 2 |
| 9.6 | Real GPU speed on Windows and Linux: **owner's hardware**; write the run sheet (`designs/9-vulkan-bench.md`: command, scenes, numbers to send back) | — | sheet exists; row state "waiting for owner" | 9.4 | 1 |

### Block 10: real scenes (the judge)

| Row | Do | Read first | Done-command and gate | needs | S |
|---|---|---|---|---|---|
| 10.1 | The shoe: one mesh, layers leather + suede + rubber + knit, perforations by parallax / displacement, stitching in height, dirt in grooves, scuffed toe, on carpet (`carpet_pile`), sun shaft through dust. Built as a patch file; mesh from a model on disk or Sketch 3D | REALISM section 2, the shoe photo | a sheet: photo, settle, live side by side, plus one crop per layer L1 to L8. **Owner review, no FLIP** | 3.5, 4.5, 5.1, 5.4, 7.2 | 2 |
| 10.2 | Interior: room with window light, wood floor, fabric sofa, glass and metal objects, physical sky outside | — | same kind of sheet | 5.5, 6.2 | 1 |
| 10.3 | Product still: glass bottle, brushed metal cap, label with layers, bokeh background | — | same kind of sheet | 6.2, 6.3 | 1 |
| 10.4 | From the three reviews: a list of at most 10 defects, each traced to a layer and a row here; nothing is fixed in this row | owner notes | the list in AUTORUN | 10.1 to 10.3 | 1 |

## 5. Shading node list (Blender equivalent)

| Blender node | Here | Row |
|---|---|---|
| Texture Coordinate, UV Map | Shade Coord | 2.3 |
| Mapping | Shade Mapping | 2.3 |
| Image Texture, Environment Texture | Shade Image | 2.4 |
| Noise, Musgrave, White Noise | Shade Noise | 2.4 |
| Voronoi | Shade Voronoi | 2.4 |
| Gradient, Wave, Brick, Checker, Magic | Shade Gradient, Shade Brick, Shade Checker (wave and magic as modes of Gradient) | 2.4 |
| Math, Vector Math | Shade Math, Shade Vector Math | 2.5 |
| Mix (float, vector, colour) | Shade Mix | 2.5 |
| Color Ramp, Map Range, RGB / Float Curves | Shade Ramp, Shade Map Range, Shade Curves | 2.5 |
| Separate / Combine XYZ, RGB, HSV | Shade Separate, Shade Combine | 2.5 |
| Bump, Normal Map, Displacement | Shade Bump, Shade Normal Map, `height` output (rows 4.2, 4.3) | 2.6 |
| Fresnel, Layer Weight | Shade Fresnel, Shade Layer Weight | 2.6 |
| Geometry, Object Info, Attribute, Ambient Occlusion | Shade Geometry, Shade Attribute | 2.6, 4.4 |
| Principled BSDF | the existing Material node | — |
| Mix Shader | Material `Layer` + `Mask` | 4.1 |
| Add Shader | not built (breaks energy conservation) | — |
| Volume Scatter / Absorption | global medium on the scene | 5.3 |
| Script (OSL) | Field Pixel in Material domain | 3.1 |
| — (no Blender twin) | Sketch as map, Sketch 3D attributes | 3.2, 3.3 |

If row 2.2 finds the Field graph already composes kernels, these are graph elements and the node menu gains
nothing but the elements; the list stays the same.

## 6. Order and size

```
 0 freeze ─> 1 spine ─┬─> 2 shading nodes ─> 3 scripts ──┐
                      ├─> 4 layers / relief / wear ──────┼─> 10 real scenes
                      └─> 5 fibre, volumes, sky ─────────┘
 6 camera, 7 assets: any time after 0      8 live: after 4.2 and 5.6      9 Vulkan: after 8.3
```

| Block | Sessions | Layers | Makes the shoe? |
|---|---|---|---|
| 0 | 3 | L1, L4 | needed |
| 1 | 7 | all | needed |
| 2 | 10 | L4, L6 | needed |
| 3 | 6 | L4 to L7 | recipes needed |
| 4 | 9 | L5, L6, L7 | needed |
| 5 | 10 | L2, L3 | 5.1, 5.3, 5.4 needed |
| 6 | 4 | L8 | no |
| 7 | 5 | — | 7.2 needed |
| 8 | 8 to 12 | live | no |
| 9 | 17 | — | no |
| 10 | 5 | judge | — |

Shortest route to the shoe: 0, 1, 2.1 to 2.6, 4, 3.5, 5.1, 5.3, 5.4, 7.2, 10.1 = about 40 sessions.

## 7. Parked (not on any route above)

Dispersion and caustics (old 4.4), heterogeneous volumes (smoke, clouds), USD, light linking, IES profiles, UDIM,
spectral rendering, ReSTIR. Open one only after block 10's defect list names it.
