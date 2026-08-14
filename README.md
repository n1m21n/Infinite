# Infinite

A node-based image and video compositor for macOS. Patch sources through
effects, colour grading and compositing modules, drive any parameter with
tempo-synced modulators, and export a PNG or record the result to video.

Architecturally it is a descendant of [BespokeSynth](https://github.com/BespokeSynth/BespokeSynth)'s
module system — a registry of node types, typed cables, and a pull-based
cook-once-per-frame DAG — repointed from audio buffers to GPU textures.

![node graph](docs/screenshot.png)

## Features

**130+ node types across eleven categories.**

| Category | Nodes |
|---|---|
| Source | Image, Video, Shape (10 primitives), Noise (6 kinds), Ramp (5 gradient types), Texture (Voronoi/Brick/Magic/Wave/Musgrave), Draw (paintable canvas), Formula (live GLSL, 16 presets) |
| Text | Text (any installed system font) |
| Effects | Blur family (gaussian/box/motion/radial), bloom, diffuse glow, unsharp mask, twirl, pinch-punch, ripple, lens distortion, displace, liquify, 6 glitch types, symmetry, kaleidoscope, mirror tile, halftone, sobel edge, edge outline, pixelate, noise, vignette, transform |
| Color | Brightness/contrast, levels, Curves (interactive spline editor), LUT, gradient map, Color Ramp (up to 32 stops), channel mixer, HSL, invert, posterize, threshold, colour balance, exposure, Color Adjustments (all-in-one grading chain) |
| Compositing | Blend (31 modes), Layer Stack (4 reorderable layers), Switcher, Fit, outer glow, colour overlay, drop shadow |
| Feedback | Feedback (one-frame delay, makes cycles legal), Trails, Reaction-Diffusion |
| Mask | Remove Background (on-device Vision segmentation), Chroma Key, Luma Key |
| 3D | Geometry (8 primitives), Model 3D (obj/ply/stl/usd), Text 3D (extruded glyphs), Ocean (Gerstner waves), Transform, Array, Subdivide (Loop), Smooth (Taubin), Mirror, Screw, Solidify, Extrude, Wireframe, Triangulate, Normals, Explode, Twist, Join Geometry, Material, Null 3D, Mesh to Points/Edges/Faces, Instance on Points, Wrap (cylindrical/spherical arc-length-preserving bend whose radius follows the target's size, tuned by a radius scale multiplier - or nearest-surface conform), **Particle System**, **Cloth**, Camera, Light (directional/point/sun/ambient), **HDRI** (equirectangular .hdr/.exr environment - background, reflections and ambient light for Render 3D), Render 3D |
| Resynth | Resynthesize — iterative generative resampler with an XY FX pad |
| Modulators | LFO, Random, Pattern (8-step), Path (6 shapes), Math, Macro Knob, Macro XY, **Image Analyze**, **Audio File**, **Audio Analyze** |
| Audio / Note | **Wavetable** synth, **MIDI Notes** (live MIDI input), **Envelope**, **Gain**, **Audio Out**, **Mixer**, **Splitter**, and 15 effect nodes — Audio Filter, Dynamics, Delay, Reverb, Drive, Stereo, Pitch Shifter, Chorus, Flanger, Phaser, Bitcrush, Transient Shaper, Stutter, Ring Mod, Formant Filter |
| Output | Output (PNG export + H.264 recording, with an optional audio track) |

- **Live 1:1 preview on every node**, including effects and compositing.
- **Modulation** — every slider has a pin. Patch a modulator into it and the
  value is driven live; modulated sliders turn amber. Modulators speak in 0..1
  and each destination maps that onto its own range, so one **Macro Knob** can
  sweep a blur radius, an opacity and a hue shift together. **Macro XY** gives
  two independent outputs from one pad, with a recordable, loopable path.
- **Physically based shading** — Cook-Torrance GGX with Fresnel and energy
  conservation, emission, and a procedural sky/horizon/ground environment that
  metal actually reflects. Lighting runs in linear space and is tonemapped
  (ACES) on the way out, with dithering to kill banding.
- **Antialiasing** — multisampled 3D rendering up to 8x, automatically stepped
  down when a large export would not fit in the memory budget.
- **Patches** — save, save as, open and a recents list, in a line-based text
  format that stays readable and diffable.
- **Simulation** — a particle system and a position-based-dynamics cloth/soft
  body solver. Both step on the transport at a fixed timestep, so pausing
  genuinely freezes them and rewinding resets them; a patch looks the same
  every time rather than depending on how long it was left running.
- **Interactive viewport** — drag the Render 3D preview to orbit, scroll to
  zoom, and frame the whole scene with one button.
- **Transport** — global play/pause and BPM. Pausing freezes modulators, video
  playback and shader animation together, so a patch is deterministic.
- **Recording** — encode the output to an H.264 `.mov` via AVAssetWriter.
- **Any image or video format the OS can decode** — decoding goes through
  ImageIO/AVFoundation rather than a bundled decoder. Drag a file onto the
  canvas and the matching source node appears, already loaded.
- **Audio reactive** — feed it a live input or an **Audio File**, and it emits
  level, low/mid/high, onset and eight spectrum bands as modulators. Patch any
  of them into any slider and that parameter becomes audio-reactive.
- **Image Analyze** closes the loop the other way: brightness, contrast, RGB,
  saturation, motion and luminance centroid come out of an image as control
  values, so a video can drive a blur.
- **3D** — Geometry nodes emit meshes down their own kind of cable. Chain them
  through **Geometry Op** (transform, array, subdivide, solidify, extrude,
  wireframe, triangulate, normals, explode, twist), scatter them with
  **Instance on Points**, light them with separate **Camera** and **Light**
  nodes, and rasterise with **Render 3D** — which hands the result back to the
  2D graph as an ordinary image.
- **Instancing is one draw call.** 642 instances of a cube is 70,000 triangles
  in a single `glDrawElementsInstanced`, and meshes are cached so operators
  only re-run when a parameter actually changes.
- **Resynthesize** — feeds each generation back into itself, so the image
  evolves away from the original. An XY pad sweeps between four named mutation
  effects, and its path can be recorded, looped and played back.
- **Feedback** — a one-frame delay node makes graph cycles legal, which is what
  Trails and Reaction-Diffusion are built on.
- **Remove Background** — on-device subject or person segmentation via Vision.
  No model download, no network, no API key.
- **Draw** — paint straight onto the node with six procedural brushes, an
  eraser, and optional image underneath. Strokes can be recorded and replayed
  as an animation, in time with the transport.
- **Audio / note engine** — a second, dedicated cable type and DAG runs
  alongside the image graph, built on a Bespoke-style pull-based audio
  callback. **Wavetable** is the synth source, **MIDI Notes** brings in a
  live MIDI controller with the transport clock-synced to it, and 15 effect
  nodes (filter, compression, delay, reverb, drive, stereo imaging, pitch
  shift, chorus, flanger, phaser, bitcrush, transient shaping, stutter, ring
  mod, formant filter) chain after it — each rendered as a real
  plugin-style control surface with knobs, switches and a live visualizer
  rather than a generic parameter list. Every audio param is patchable by
  the same modulators that drive the image graph, so a **Macro Knob** or an
  **LFO** can sweep a filter cutoff exactly like it sweeps a blur radius.

## Install

There is currently no prebuilt download — build it yourself with the steps in
[Build from source](#build-from-source) below, then drag the resulting
`Infinite.app` to Applications.

The build is a universal binary (Apple Silicon and Intel) and is self-contained
— it links nothing outside the system frameworks, so it does not need Homebrew
or anything else installed.

It is **ad-hoc signed but not notarized**, because notarizing requires a paid
Apple Developer account. macOS therefore blocks it on first launch with
*"Infinite cannot be opened because the developer cannot be verified"*. This is
expected, and it is a signature problem, not a broken download.

**To open it the first time: right-click (or Control-click) the app → Open →
Open.** After that it launches normally like any other app.

If macOS instead says the app is **damaged and should be moved to the Bin**,
that is the quarantine flag rather than actual damage:

```bash
xattr -dr com.apple.quarantine /Applications/Infinite.app
```

There is no way around this short of notarization; anyone distributing this
further should expect to explain the same step.

## Build from source

Requires CMake 3.16+ and Xcode command line tools. Everything else is either
vendored in `external/` or fetched by CMake.

```bash
git clone https://github.com/n1m21n/Infinite.git
cd Infinite
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
open build/Infinite.app
```

To build the DMG:

```bash
./package.sh
```

## Using it

| Action | How |
|---|---|
| Add a node | Right-click or double-click the canvas, then type to filter |
| Connect | Drag from a node's `out` dot to another node's input dot |
| Modulate a param | Drag a modulator's `out` onto the small dot beside any slider |
| Pan | Drag empty canvas |
| Rubber-band select | Shift + drag |
| Duplicate | Cmd+C / Cmd+V, or **Shift+D** in place |
| Select several | **Shift + drag** a box, then move / duplicate / delete as a group |
| Delete | Select, then Delete or Backspace |
| Type an exact value | Double-click a slider |
| Add a file | Drag an image or video onto the canvas |
| Bypass a node | Click the power icon beside the eye — the node is skipped and its input passes through |

The usual signal flow is **Source → Effects → Color → Compositing → Output**,
but nothing enforces it; any output can feed any input.

Canvas settings (snap, grid size, zoom speed) live in the **Menu**, along with a
full **Help / module reference** describing every node.

## Architecture

```
src/core/       INode, ImageCable, NodeFactory, Transport, Modulation, GLUtil
src/nodes/      one file per node family
src/platform/   macOS shims: file dialogs, image decode, video decode, recording
```

- `INode` is the interface every node implements: `CookIfNeeded(frameId)` plus
  an output texture. Cooking is pull-based and memoised per frame, so a node
  feeding several consumers renders once.
- `FilterNode` is one class driven by a declarative table (`FilterDefs.cpp`).
  Adding an effect is a table entry — a name, a fragment shader body and a
  parameter list — not a new class.
- Rendering is OpenGL 3.2 core with GLSL 150 fragment passes over a shared
  fullscreen quad.

## Licence

MIT — see [LICENSE](LICENSE).

Vendored dependencies: [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
[imgui-node-editor](https://github.com/thedmd/imgui-node-editor) (MIT),
[stb](https://github.com/nothings/stb) (public domain).
[GLFW](https://github.com/glfw/glfw) (zlib) is fetched at configure time.
