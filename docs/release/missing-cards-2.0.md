# Missing node cards for 2.0

Branch `release/2.0-cards`. Source: `infinite --describe` from `build/Infinite.app` (377 registered types) minus the five types `IsUserSpawnable()` hides (Delete Selected, Transform Selected, Extrude Selected, Group, Field Graph) = **372 spawnable**.

## Method

A node has a card when a card or chapter row name, after lower-casing and dropping non-alphanumerics, equals the node's type name. Sources: `tools/manual/cards_base.json` (cards and rows) and `tools/manual/cards.py` (chapters 21-28, which already imports `cards_v06.py` and `cards_everyday.py`).

**Result: 62 spawnable nodes have no card under their own name.** The brief said 69. I could not reproduce 69 under any definition tried: exact names give 104, exact plus the `ALIASES` map in `tools/manual/shots.py` gives 89, normalised names give 62, normalised plus aliases gives 47, and the `gen-agent-docs.py` source set gives 65. This list uses the normalised-name definition because it is the one that matches a card id. The 15 marked as covered by a group card are the ones the alias map ties to a shared card; they get their own card too, so the Node Reference and `infinite docs` can find them by their own name.

## The missing nodes

| Node (type name) | Destination chapter | Covered only by group card |
|---|---|---|
| Ring | 21 More Shapes & Slideshows | - |
| Line | 21 More Shapes & Slideshows | - |
| Arrow | 21 More Shapes & Slideshows | - |
| Shadertoy | 1 2D Sources | - |
| VMPC | 1 2D Sources | - |
| Geometry | 28 Mesh Primitives & Operators | Geometry Primitives |
| Plane | 28 Mesh Primitives & Operators | - |
| Cube | 28 Mesh Primitives & Operators | - |
| Sphere | 28 Mesh Primitives & Operators | Geometry Primitives |
| Icosphere | 28 Mesh Primitives & Operators | - |
| Torus | 28 Mesh Primitives & Operators | Geometry Primitives |
| Torus Knot | 28 Mesh Primitives & Operators | - |
| Helix | 28 Mesh Primitives & Operators | - |
| Ocean | 10 3D Simulation, Scene & Render | Ocean Surface |
| Feedback 3D | 10 3D Simulation, Scene & Render | - |
| Cloth | 10 3D Simulation, Scene & Render | Cloth Simulation |
| Intersect | 28 Mesh Primitives & Operators | - |
| Difference | 28 Mesh Primitives & Operators | - |
| Array | 9 3D Points, Instancing & Mesh Ops | - |
| Select | 9 3D Points, Instancing & Mesh Ops | - |
| Inset | 9 3D Points, Instancing & Mesh Ops | - |
| Fur | 9 3D Points, Instancing & Mesh Ops | - |
| Point Trails | 9 3D Points, Instancing & Mesh Ops | - |
| Sweep | 9 3D Points, Instancing & Mesh Ops | - |
| 3D View | 10 3D Simulation, Scene & Render | - |
| Shade Coord | 8 3D Building Blocks & Materials | - |
| Shade Mapping | 8 3D Building Blocks & Materials | - |
| Shade Checker | 8 3D Building Blocks & Materials | - |
| Shade Noise | 8 3D Building Blocks & Materials | - |
| Shade Voronoi | 8 3D Building Blocks & Materials | - |
| Shade Gradient | 8 3D Building Blocks & Materials | - |
| Shade Brick | 8 3D Building Blocks & Materials | - |
| Shade Image | 8 3D Building Blocks & Materials | - |
| Shade Math | 8 3D Building Blocks & Materials | - |
| Shade Vector Math | 8 3D Building Blocks & Materials | - |
| Shade Mix | 8 3D Building Blocks & Materials | - |
| Shade Ramp | 8 3D Building Blocks & Materials | - |
| Shade Map Range | 8 3D Building Blocks & Materials | - |
| Shade Separate | 8 3D Building Blocks & Materials | - |
| Shade Combine | 8 3D Building Blocks & Materials | - |
| Shade Curves | 8 3D Building Blocks & Materials | - |
| Shade Bump | 8 3D Building Blocks & Materials | - |
| Shade Normal Map | 8 3D Building Blocks & Materials | - |
| Shade Fresnel | 8 3D Building Blocks & Materials | - |
| Shade Layer Weight | 8 3D Building Blocks & Materials | - |
| Shade Geometry | 8 3D Building Blocks & Materials | - |
| gaussianblur | 3 2D Image Effects | Gaussian / Box Blur |
| boxblur | 3 2D Image Effects | Gaussian / Box Blur |
| twirl | 3 2D Image Effects | Twirl, Pinch/Punch & Ripple |
| pinchpunch | 3 2D Image Effects | Twirl, Pinch/Punch & Ripple |
| ripple | 3 2D Image Effects | Twirl, Pinch/Punch & Ripple |
| bloom | 3 2D Image Effects | Bloom & Diffuse Glow |
| diffuseglow | 3 2D Image Effects | Bloom & Diffuse Glow |
| Scene 2D | 6 Compositing & Layering | - |
| Feedback | 7 Feedback, Trails & AI Vision | Feedback & Trails |
| Trails | 7 Feedback, Trails & AI Vision | Feedback & Trails |
| Field Modulator | 20 The Field Language | - |
| Moves | 24 Looper, Resonators, Meters & Prediction | - |
| Wavetable | 11 Audio Synthesizers | Wavetable Synth |
| Generate Samples | 11 Audio Synthesizers | - |
| Piano Roll | 14 MIDI & Note Shaping | - |
| Spectral Field | 20 The Field Language | - |

## Source and --describe disagreements

1. **Shade Noise `roughness`.** The body (`src/app/bodies/GeometryParams.cpp`, slider 0..1) and `ShadeNoiseNode` (`src/nodes/ShadeNodes.h`, default 0.5) both have it, but `--describe` lists no `roughness` row for Shade Noise. The card follows the source.
2. **Mesh-op `op` range.** `--describe` reports `op` range 0..19 with 20 options, which omits the three deprecated ops (Delete, Transform and Extrude Selected, indices 14-16). The source enum (`src/nodes/GeometryOpNodes.cpp`, `kOpNames`) runs 0..22. Inset's default op (20) and Fur's default op (22) fall outside the describe range, and option positions after index 13 do not line up with saved indices.
3. **Stale help text.** The Select help text names Delete Selected, Transform Selected and Extrude Selected as the consumers of its selection. Those nodes are deprecated (`IsSpawnable` is false); the replacement is the mesh operator with Selection only on. The card uses the replacement.
4. **Unlabelled units.** Trails `rotate` and Wavetable `detune` have no unit in `--describe`. The cards describe them only as far as the source confirms.

## Discovery vocabulary

`src/core/chat/NodeVocab.gen.h` already has an entry for each of the 62 types (generated from `--describe` by `scripts/node_vocab.py`), so the agent finder can already rank them. That file is generated C++, so it was not regenerated.
