# 3D basics: rigging, materials, textures, sculpting

People who work in 3D ask for these four first. This plan fits each one to
Infinite instead of copying Blender's editors. Infinite is not a modelling
tool. Geometry is edited in nodes, every number can be modulated, and the
picture is live.

Order (as the owner asked): rigging → materials → textures → sculpting. Each
block is shown to the owner before it is built (new nodes and panels need
approval), lands on its own branch and merges into main.

## The one idea that ties them together

| Blender has | Infinite turns it into |
|---|---|
| Armature pose, shape keys, sculpt layers | **Modulatable layers.** A joint angle, a morph weight and a sculpt's strength are all params, so an LFO, an audio band or a MIDI knob can drive them. |
| Edit modes on one object | **A node per step.** Skin, Sculpt and Bake are pass-through nodes in the geometry chain. Upstream changes flow through them. |
| Material and texture editors | **The 3D View sidebar plus the 2D nodes Infinite already has.** Any image node (Noise, Texture, video, camera, Field Pixel) is a live texture. |

## What exists today (surveyed 2026-11-02)

| Area | Have | Missing |
|---|---|---|
| Rigging | Follow (parent), Look at, Motion offsets, Model 3D (OBJ, STL, PLY, USD, ABC through ModelIO; glTF through cgltf) | Skins, joints, weights, animation clips, morph targets (GltfImport.h: "No animation/skinning … no morph targets"), any bone or IK |
| Materials | Material node with a full Principled-style set (metal, rough, IOR, transmission, clearcoat, sheen, subsurface, iridescence, anisotropy, dispersion); glTF PBR maps | A Material tab in the 3D View; a preview ball; per-material split on import (first material only today); opacity and transmission maps; shipped presets |
| Textures | 8 map channels (albedo, rough, metal, normal/bump, AO, emission, clearcoat, sheen) fed by any 2D node; Mapping node (UV / generated / object, triplanar blend); Texture node (Voronoi, brick, wave, …) | Real UV unwrapping (projections that write UVs); baking a mesh into an image |
| Sculpting | Displacement node (texture or 3D noise), Smooth / Subdivide / Twist ops, Field Element node (code deforms), face selection travels on the mesh | Any brush in the 3D View |

## Block R: rigging (4 steps)

| Step | What | Decision |
|---|---|---|
| R1 ✅ | Skinned glTF import: joints, weights, clips and morph targets in Model 3D | Skinning on the CPU (linear blend, 4 weights per vertex), so geometry ops after Model 3D see the posed mesh. New params: Clip, Speed, Phase (scrub, 0-1 of the clip), Sync (seconds / one loop per N beats), and one weight per morph target (up to 8). "Free" time was dropped: a clip that runs while the transport is paused breaks the source-node rule. Built: core/Skinning, RIGTEST. |
| R2 ✅ | Pose in the 3D View | Bones drawn as an overlay on the selected model. Ctrl+Tab enters Pose (Blender's key, also Object > Pose Mode). Click a bone, R turns it about the view axis, Alt+R clears. Built as 16 pose slots on Model 3D (joint + X/Y/Z degrees, turned in the joint's own frame after the clip), each angle a modulatable param shown in the node body. G/S on bones are not built. On Windows and Linux, Ctrl+Tab may also open ImGui's window switcher, which is left as is for now. |
| R3 | IK | Chain length plus a target that is any 3D View item. Two bones solve exactly; longer chains use FABRIK. Optional pole item. Follows Blender's IK constraint fields: target, pole, chain length, weight. |
| R4 | Armature node for any mesh | A bone chain along a Curve node or a straight axis with N segments. Weights come automatically from distance with a smooth falloff (Blender's envelope). Bends tentacles, plants and cables. Pose and IK from R2/R3 work on it too. |

Not building: weight painting, NLA / action editor, bone custom shapes,
constraints other than IK (Follow and Look at already exist), skins in USD or ABC (glTF
is the open format, and Blender and Mixamo both export it).

## Block M: materials (4 steps)

| Step | What | Decision |
|---|---|---|
| M1 | Material tab in the 3D View sidebar | Edits the Material node that feeds the selected item. If there is none, "Add Material" inserts one on the canvas. Each map row shows the patched node's name, with a picker. |
| M2 | Preview ball | A small sphere render in the tab, redrawn only when the material changes. |
| M3 | Every material on import | A glTF or OBJ with several materials becomes one item per material, grouped, each with its own Material and texture nodes. Same rule for a dropped file (closes the block K gap). |
| M4 | Opacity and transmission maps, plus presets | Two more map channels. About 12 node presets made from measured values (gold, copper, glass, water, rubber, car paint, velvet, skin, ceramic, plastic, chrome, emissive). |

## Block T: textures (2 steps)

| Step | What | Decision |
|---|---|---|
| T1 | UV Project op in Geometry Op | Cube, cylinder, sphere and Smart Project (split by face angle, pack the islands). Writes real UVs, so a texture sticks to a moving or deforming mesh. Modelled on Blender's Smart UV Project, written fresh. |
| T2 | Bake node | Geometry in, image out, rendered in UV space: AO, curvature, position, normal, thickness. It re-bakes when the mesh changes. The image goes into any 2D chain and back into a Material map: mesh → image → 2D effects → mesh. |

Not building: texture painting, UDIM tiles, a UV editor with hand-placed seams.

## Block S: sculpting (2 steps)

| Step | What | Decision |
|---|---|---|
| S1 | Sculpt node plus brushes in the 3D View | A pass-through node that holds an offset per vertex, with its own subdivide level (0 to 3). Select its item, then use the header's Sculpt chip or Ctrl+Tab. Seven brushes: Draw, Smooth, Grab, Inflate, Pinch, Flatten, Crease. F sets the radius, Shift+F the strength, Ctrl inverts. X / Y / Z symmetry. A face selection upstream acts as the mask. Each stroke is one undo step. |
| S2 | Strength as a param | The node's Amount (0 to 2) blends the sculpt in. A sculpt becomes a shape key that audio or an LFO can drive. To layer sculpts, chain Sculpt nodes. |

If the input's vertex count changes, the node keeps its offsets, stops
applying them and shows "topology changed". Not building: dyntopo, multires
history, voxel remesh. Remesh is a candidate for a later Geometry Op.

## Reference and tests

- **Blender source** is read for the maths, never copied. It's no longer at
  ~/blender-ref, so it needs a shallow re-clone before R1 (owner's OK, since
  it's a download). The areas to read: armature deform, IK solver, shape keys,
  sculpt brushes, Smart UV Project.
- **Each block adds** a self-test (pose, IK, bake or brush result checked in
  pixels), sweep entries for every new param, and rows in
  test-checklist.md.
- **Every new param** is modulatable, then checked with the modulation sweep.
