# Blender bridge

Goal: Infinite's headless engine and Blender exchange models both ways, so a model made in Infinite
can be finished in Blender, and Blender's tools (sculpt, modifiers, geometry nodes, bake, import
of any format) can feed Infinite.

## Decision

Bridge by files, driven by two CLIs. No sockets, no add-on, no GUI on either side.

```
Blender (-b -P script.py)  --glTF/OBJ/PLY-->  Infinite  (Model 3D node, --frame/--render)
Blender (-b -P script.py)  <--glTF/OBJ--      Infinite  (mesh export: MISSING, see below)
```

Why files: both sides already have headless CLIs; glTF carries geometry, UVs and PBR textures; a
file is diffable and replayable; no live process to keep in sync. A live link (Blender MCP /
add-on) can sit on top later for interactive use; it is not the foundation.

## Verified facts

| Piece | Fact |
|---|---|
| Infinite import | `Model 3D` node, key `path`. OBJ, PLY, STL, USD, USDZ via ModelIO; glTF/GLB via cgltf (`src/core/GltfImport.cpp`): first mesh, first primitive, first material only, one UV set, no animation/skinning/cameras/lights |
| Infinite export of meshes | None found in `src/`. Only images and video leave the engine |
| Blender | 5.1.0 at `/Applications/Blender.app`; `Blender -b -P x.py -- args` runs headless; `bpy.ops.export_scene.gltf` works |
| Blender → Infinite | Proven (this branch): `make_suzanne.py` → `out/suzanne.glb` → `suzanne.inf` validates and renders with `--frame` |

## Plan (smallest first)

| Step | Work | Done when |
|---|---|---|
| 1 | **Blender → Infinite** helper: `tools/blender-bridge/to_infinite.py` takes any Blender-readable file or a `.blend`, applies modifiers, joins to one mesh (the importer takes the first mesh only), exports GLB, and prints the `Model 3D` patch snippet | any input becomes a rendering patch in one command |
| 2 | **Infinite → Blender** needs a mesh writer in the engine: CLI `--export-mesh <patch> <node-id> <out.glb|obj>` evaluating the geometry graph headless. Engine change under `src/` (Pro only, three-platform rule) | a patch's geometry node exports and reopens in Blender with equal vertex count |
| 3 | `tools/blender-bridge/roundtrip.py`: patch → export → Blender op (e.g. subdivide + bevel) → GLB → patch with `Model 3D`; check vertex counts and a frame at fixed t | one command, JSON last line |
| 4 | Agent use: the Tongue `ask`/`add` flow can name `Model 3D` + a Blender op list; only after step 3 | optional |

## Known limits

- glTF import is first-mesh-only: multi-object scenes must be joined on the Blender side (step 1).
- Materials survive as PBR maps only; Blender shader graphs bake to textures first.
- Animation, rigs and lights do not cross.
- The unit/up-axis convention (Blender Z-up, glTF Y-up) is handled by the glTF exporter; verify
  by render, not by trust.
- The Suzanne test frame looks small and slightly clipped at default camera; check `normalize`
  and camera framing in step 1 before calling the look final.
