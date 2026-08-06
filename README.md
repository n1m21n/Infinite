# Infinite

A node-based image and video compositor for macOS. Patch sources through
effects, colour grading and compositing modules, drive any parameter with
tempo-synced modulators, and export a PNG or record the result to video.

Architecturally it is a descendant of [BespokeSynth](https://github.com/BespokeSynth/BespokeSynth)'s
module system — a registry of node types, typed cables, and a pull-based
cook-once-per-frame DAG — repointed from audio buffers to GPU textures.

![node graph](docs/screenshot.png)

## Features

**75 node types across nine categories.**

| Category | Nodes |
|---|---|
| Source | Image, Video, Shape (10 primitives), Noise (6 kinds), Ramp (5 gradient types), Draw (paintable canvas), Formula (live GLSL, 16 presets) |
| Text | Text (any installed system font) |
| Effects | Blur family (gaussian/box/motion/radial), bloom, diffuse glow, unsharp mask, twirl, pinch-punch, ripple, lens distortion, displace, liquify, 6 glitch types, symmetry, kaleidoscope, mirror tile, halftone, sobel edge, edge outline, pixelate, noise, vignette, transform |
| Color | Brightness/contrast, levels, Curves (interactive spline editor), LUT, gradient map, channel mixer, HSL, invert, posterize, threshold, vibrance, black & white, colour balance, exposure |
| Compositing | Blend (31 modes), Layer Stack (4 reorderable layers), Switcher, Fit, outer glow, colour overlay, drop shadow |
| Feedback | Feedback (one-frame delay, makes cycles legal), Trails, Reaction-Diffusion |
| Mask | Remove Background (on-device Vision segmentation), Chroma Key, Luma Key |
| Resynth | Resynthesize — iterative generative resampler with an XY FX pad |
| Modulators | LFO, Random, Pattern (8-step), Math, Macro Knob, Macro XY, **Image Analyze**, **Audio Analyze** |
| Output | Output (PNG export + H.264 recording) |

- **Live 1:1 preview on every node**, including effects and compositing.
- **Modulation** — every slider has a pin. Patch a modulator into it and the
  value is driven live; modulated sliders turn amber. Modulators speak in 0..1
  and each destination maps that onto its own range, so one **Macro Knob** can
  sweep a blur radius, an opacity and a hue shift together. **Macro XY** gives
  two independent outputs from one pad, with a recordable, loopable path.
- **Transport** — global play/pause and BPM. Pausing freezes modulators, video
  playback and shader animation together, so a patch is deterministic.
- **Recording** — encode the output to an H.264 `.mov` via AVAssetWriter.
- **Any image or video format the OS can decode** — decoding goes through
  ImageIO/AVFoundation rather than a bundled decoder. Drag a file onto the
  canvas and the matching source node appears, already loaded.
- **Audio reactive** — the Audio Analyze node taps a live input and emits
  level, low/mid/high, onset and eight spectrum bands as modulators. Patch any
  of them into any slider and that parameter becomes audio-reactive.
- **Image Analyze** closes the loop the other way: brightness, contrast, RGB,
  saturation, motion and luminance centroid come out of an image as control
  values, so a video can drive a blur.
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

## Install

Download the DMG from [Releases](../../releases), drag **Infinite** to
Applications.

The app is **not notarized** (that needs a paid Apple Developer account), so
Gatekeeper will refuse the first launch. To open it:

**right-click the app → Open → Open**

You only need to do this once. Alternatively:

```bash
xattr -dr com.apple.quarantine /Applications/Infinite.app
```

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
| Duplicate | Cmd+C / Cmd+V |
| Delete | Select, then Delete or Backspace |
| Type an exact value | Double-click a slider |
| Add a file | Drag an image or video onto the canvas |

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
