# Infinite

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="art/brand/readme/readme-dark.png">
  <img src="art/brand/readme/readme-light.png" alt="Infinite: a DAW for you. Visuals, music and code on one canvas." width="100%">
</picture>

[![Discord](https://img.shields.io/badge/Discord-Join%20Community-5865F2?logo=discord&logoColor=white)](https://discord.gg/GMeQTyJqkH)
[![YouTube Tutorial](https://img.shields.io/badge/YouTube-Watch%20Tutorial-FF0000?logo=youtube&logoColor=white)](https://www.youtube.com/watch?v=vXRjrDhSq24&t=1421s)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Windows%20%7C%20Linux-blue)](https://github.com/n1m21n/Infinite)
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)
[<img src="https://trendshift.io/api/badge/trendshift/repositories/187223/daily?language=C%2B%2B" alt="n1m21n%2FInfinite | Trendshift" width="250" height="55"/>](https://trendshift.io/repositories/187223?utm_source=trendshift-badge&utm_medium=badge&utm_campaign=badge-trendshift-187223)

**Infinite is a free, open-source node workstation for visuals, music and interactive art.** Pictures, 3D, sound and notes run on one node canvas, and every cable carries one kind of signal. It runs on macOS, Windows and Linux, and headless from the command line.

*For VJs, generative artists, musicians, motion designers and creative coders. A free alternative in the spirit of TouchDesigner, VCV Rack and Max/MSP, with a DAW-style Timeline and Session built in.*

![Infinite with several panels open around the node canvas](docs/screenshots/v1/hero-panels.png)

---

## Download

[![macOS](https://img.shields.io/badge/Download-macOS-000000?style=for-the-badge&logo=apple&logoColor=white)](https://github.com/n1m21n/Infinite/releases/latest/download/Infinite.dmg) [![Windows x64](https://img.shields.io/badge/Download-Windows%20x64-0078D4?style=for-the-badge&logo=windows&logoColor=white)](https://github.com/n1m21n/Infinite/releases/latest/download/Infinite-windows-x64.zip) [![Windows ARM64](https://img.shields.io/badge/Download-Windows%20ARM64-0078D4?style=for-the-badge&logo=windows&logoColor=white)](https://github.com/n1m21n/Infinite/releases/latest/download/Infinite-windows-ARM64.zip) [![Linux](https://img.shields.io/badge/Download-Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://github.com/n1m21n/Infinite/releases/latest/download/Infinite-x86_64.AppImage)

Always the newest release, with checksums and notes on the [Releases page](https://github.com/n1m21n/Infinite/releases/latest). Installation steps for each platform are under [Installation](#installation). The app is in English, Spanish, German, Chinese, Japanese and Russian. New here? Watch the [video walkthrough](https://www.youtube.com/watch?v=vXRjrDhSq24&t=1421s).

---

## How Infinite thinks

| Idea | What it means in the app |
|---|---|
| **One graph, four kinds of signal** | Pictures, 3D geometry, audio and notes or control each have their own typed cable, so anything can modulate anything without adapters. |
| **Live by default** | The patch always runs. Design while it plays, then render, share or perform the same file. There is no edit and run split. |
| **Modulation is first-class** | Every modulation lives in one table, the [Mod Matrix](#mod-matrix-and-performance), which you can read and edit. |
| **Write what a node does** | [Field](#field-and-code) is Infinite's language. It compiles to the same nodes, and the Field Patcher shows the code as boxes and wires, both ways. |
| **One file everywhere** | The app, the headless `infinite` command and the MCP server for agents all read and write the same `.inf` text file. |

Start from a working patch instead of an empty canvas. The **Showcase** is a full piece that plays on its own, and 15 **Starters** each teach one idea in 3 to 6 nodes.

---

## What's new in 2.0

| Feature | What it lets you do |
|---|---|
| **2D View** | Paint, vector and text layers with 28 adjustments, and layered PSD and ORA import and export. |
| **Real-time 3D** | Render 3D runs on a Metal engine with a path tracer, richer materials, fog, sky and more light types. |
| **Field Patcher** | Edit Field kernels as boxes and wires, synced with the code. |
| **Mixer and Drums** | A hardware-desk Mixer and an 8-lane Drum Sequencer, with a Session view that follows the Timeline. |
| **Mod Matrix and Performance** | Read every modulation in one table, and play a patch from a page of knobs, faders and pads. |
| **Headless, CLI and MCP** | A wider `infinite` command, headless rendering and 12 MCP tools for agents. |

Full list: [v2.0 release notes](docs/release/v2.0-notes.md).

---

## A look inside

<table>
<tr>
<td width="50%"><img src="docs/screenshots/v2/crop/mod-matrix.png" alt="Mod Matrix table with every modulation in the Showcase patch" width="100%"><br><sub><b>Mod Matrix:</b> every modulation in the patch, editable in one table.</sub></td>
<td width="50%"><img src="docs/screenshots/v2/crop/performance.png" alt="Performance page with labelled knobs and faders" width="100%"><br><sub><b>Performance:</b> knobs and faders that play the patch live.</sub></td>
</tr>
<tr>
<td width="50%"><img src="docs/screenshots/v2/crop/mixer.png" alt="Mixer channel strips with meters" width="100%"><br><sub><b>Mixer:</b> channel strips, meters and a master bus.</sub></td>
<td width="50%"><img src="docs/screenshots/v2/crop/field-patcher.png" alt="Field Patcher showing Field code as boxes and wires" width="100%"><br><sub><b>Field Patcher:</b> Field code as boxes and wires, synced with the text.</sub></td>
</tr>
</table>

---
## Nodes

### The node canvas

Everything is a node, and every cable carries one kind of thing: pictures, 3D geometry, audio, notes or control signals. Patch left to right, right-click to add, and anything with a dot beside it can be modulated. Infinite has **372 node types** on macOS; the [Node library](#node-library) lists them by domain, and the [Node Reference Manual](Infinite_Node_Reference_Manual.pdf) covers most of them.

| Domain | What it covers |
|---|---|
| **Pictures and video** | A GPU texture pipeline (GLSL, OpenGL 3.2 Core): images, video and camera, Syphon, Spout and NDI sources, shapes, noise and Field Pixel shaders, 32 blend modes, blur, bloom, glitch, halftone, curves, LUTs, reaction-diffusion, feedback loops and on-device background removal. The Showcase frames are four seconds of the bundled patch, rendered headless. |
| **3D** | Procedural meshes, imports (USD, OBJ, PLY, STL, glTF), booleans, Voronoi and Delaunay, instancing on points, particles, PBD cloth and soft bodies, metaballs, Camera and Light nodes, and PBR rendering (Cook-Torrance GGX, ACES, 32-bit HDRI). The [3D View panel](#3d-view) composes them into one scene. |
| **Sound** | A sample-accurate, pull-based audio graph on the same BPM transport as the pictures. Synths and physical models (Analog, FM, Wavetable, Metallic, String, **Piano**, **Wind**, Granular, PaulStretch, Molder, Sampler, Slicer, 8-track Drum Sequencer, Looper), a full effects rack, AU and VST3 plugin hosting with native windows and modulatable parameters, and **Generate Samples**, a player whose sample is made rather than loaded: pick one of 60 music styles and a part (all, synths, pads, plucks, percussion or drums), press generate, and a loop of 1, 2, 4 or 8 bars is rendered from Infinite's own instruments. It saves as a recipe, not as audio. |
| **Notes and control** | Note sequencers, arpeggiator, chorder, quantizer and MIDI in and out on one side; LFOs, envelopes, random, pattern CV, audio analysis (8-band FFT, onsets), image analysis and camera tracking on the other. Any source can drive any parameter: a shader control, a 3D transform, a synth knob or a hosted plugin's parameter. |

### Node library

<details>
<summary>Node library: 372 node types by domain</summary>

Infinite has **372 node types** on macOS (377 are registered; five internal types are hidden from the spawn menu):

| Domain | Key nodes |
|---|---|
| **2D and video** | Image, folder Slideshow, Video (hardware-accelerated), Syphon In / Spout In, NDI In, Paint/Draw canvas, GLSL Formula editor, Sketch (JavaScript-drawn images, with SVG animation), Shapes (SDF), Noise and Ramp (gradient) |
| **2D FX and grading** | Blur, Bloom, Glitch (6 modes), Twirl, Ripple, Displace, Halftone, Curves (RGB/Luma splines), Dither, Color Ramp, .cube LUTs, Gradient Map, Palette extraction |
| **Compositing and masks** | Blend (32 modes), Layer Stack, Remove Background (on-device ML segmentation), Chroma/Luma Key, Feedback loop, Reaction Diffusion, Resynthesize |
| **3D geometry and FX** | Primitives, USD/OBJ/PLY/STL/glTF/GLB import, 3D Text, 3D Curves, Distribute Points in Grid / on Faces, Sketch 3D (JavaScript-built meshes), Mesh to Edges / Faces / Points, Delaunay Mesh, Voronoi Cells, Decimate, Curve Ops, Armature, Smooth, Array, Instancing, Metaballs, Point Trails |
| **3D scene and render** | 3D View, Camera (orbit/perspective/ortho), Light, 32-bit HDRI Environment, PBR Materials, ACES tonemapping, multisampled antialiasing |
| **Synths and sound** | Wavetable, Metallic modal resonator, String, Piano, Wind, Generate Samples, Granular, PaulStretch, Molder / Grain Molder (spectral resynthesis), Sampler, Slicer (onset-detecting sample chopper), 8-track Drum Sequencer, 16-pad MPC sampler, Looper (record and overdub, tempo-synced, latency-compensated), multi-waveform Oscillator |
| **Field language** | Field Modifier, Field Primitive, Field Effect, Field Synth, Field Pixel, Field Notes, Field Graph (see [below](#field-and-code)) |
| **Notes and MIDI** | Piano Roll, live MIDI input and clock, Arpeggiator, Note Sequencer, Chorder, Note Strum, Bouncing Balls (physics notes), Humanizer, Quantizer, MIDI File player, MIDI Out (hardware and virtual ports, CC and clock) |
| **Audio FX and plugins** | **AU / VST3 Plugin Host**, Filters, EQ, Dynamics, Limiter, Delay, Reverb, Drive/Saturation, Pitch and Frequency Shifters, Chorus, Phaser, Formant |
| **Modulators and analysis** | LFO, Random, Pattern, Envelope, Math, XY Pad, Audio Analyze (8-band FFT, onset), Image Analyze (luminance, motion), Hand / Face / Pose Track (camera to modulators, via the Tracking pack) |
| **Output and I/O** | Spatial Mixer (3D headphone mix with head tracking), PNG snapshot, H.264/MOV recording with synchronised audio, Syphon Out / Spout Out, NDI Out |

</details>

---

## Panels

Panels dock to the edges of the canvas and open from the rail or a key. Every shortcut is listed in the app under **All shortcuts**.

| Panel | Key | What it does |
|---|---|---|
| 2D View | Shift + 2 | Layer and node compositor: paint, vector and text layers, 39 tools, 32 blend modes. |
| 3D View | Shift + 3 | Final stage of a 3D patch: arrange objects, look through cameras, render stills and animation. |
| Field Patcher | Shift + F | Box-and-wire editor for Field nodes, synced with the code. |
| Mixer | Shift + L | Hardware-desk channel strips, LED meters and a Master bus with LUFS and true-peak readouts. |
| Drums | Shift + J | 8-lane step sequencer with trig conditions, probability and groove-matched kits. |
| Sample Studio | Shift + W | Slice a sample by transient, grid or equal parts, then Convert it to a canvas node. |
| Piano Roll | Shift + R | Note editor for MIDI lanes, snapped to the project scale, with Humanize, Strum and Generate. |
| Session | Shift + S | Clip launcher for live sets: scenes, quantised launch and follow actions. |
| Timeline | Shift + T | Arrangement lanes, markers and a loop range, with Record and Render Take. |
| Mod Matrix | Shift + M | Every modulation in one table: source, destination, range, curve, invert and live signal. |
| Performance | Shift + P | Pages of knobs, faders, XY pads and step gates, with MIDI learn on every control. |
| Terminal | Ctrl + \` | A real shell docked on any side, for the `infinite` command and your own tools. |

## Plugins and connections

Infinite plays with the rest of your setup:

- **AU and VST3 plugin hosting**: Audio Unit (macOS) and VST3 (macOS, Windows, Linux), with native plugin windows and modulatable parameters. Scan controls and the list of scanned plugins live in Settings > Plugins.
- **MIDI and OSC**: live MIDI input and clock, MIDI Out to hardware and virtual ports (notes, CC, clock), MIDI learn on every Performance control, and OSC Send and Receive nodes.
- **Video to other apps**: Syphon (macOS) and Spout (Windows) zero-copy video in and out, and NDI in and out (needs the free NDI runtime).
- **Files**: PNG snapshots, H.264/MOV recording with synchronised audio, WAV, FLAC and MP3 export, `.mid` import, and USD, OBJ, PLY, STL and glTF models.

## Field and code

Every other node in Infinite does one fixed thing: a Blur node blurs, a Delay node delays. **Field** nodes are different. You write a short kernel, and Field works out where it runs from what the code touches.

```
col = vec3(uv.x, uv.y, 0.5)              // once per pixel, on the GPU: a shader
P.y += sin(P.x * 4.0 + t) * 0.2          // once per point in a mesh: a geometry deformer
out = in * gain                          // once per audio sample: a DSP effect
```

That is the one idea underneath the language: a **kernel**, run once per element of a **domain**, forever. You never declare where it runs; the domain, and so the backend it compiles to, is inferred.

| Node | Domain | What it does |
|---|---|---|
| **Field Modifier** | element | Per-vertex kernel over existing geometry: modifies `P`, `N`, `uv`, `Cd` and custom attributes |
| **Field Primitive** | element | Generates point geometry from scratch (Circle, Spiral, Grid Lattice, Fibonacci Sphere, Helix, Torus Knot) |
| **Field Effect** | sample | Per-sample audio DSP kernel; `in`/`out`, `state` for per-voice memory, `param` for knobs |
| **Field Synth** | sample | Polyphonic note-driven synth voice on the same sample compiler, with `freq`/`gate` in place of an audio input |
| **Field Pixel** | pixel | Per-pixel kernel compiled straight to GLSL; `state` cells with `[wrap]` support feedback and stencils |
| **Field Notes** | sample | Per-sample note kernel: `note(pitch, vel, len)` sends notes out, and `tick(div)`, `deg(d)`, `rand()` and `beat` read the transport |
| **Field Graph** | graph | Runs once at edit time: `emit()`/`connect()`/`set()`/`place()` mount and wire real Infinite nodes into a bundle ("Instrument Mode" collapses it to one box; "Unpack to Canvas" expands it again) |

Any element, pixel or sample kernel can declare **dynamic pins** in its code, adding real, cable-wireable, save-safe inputs and outputs without touching C++. Finished devices (kernel, params and presets) save and share as portable `.field` files. The full reference is in the [Field Language Manual](Field_Language_Manual.pdf).

Field is not the only way to write code in Infinite. **Sketch** draws images with JavaScript (with SVG animation), **Sketch 3D** builds meshes with JavaScript, the **GLSL Formula** node is a live shader editor, and **expressions** (`infinite expr`) drive any parameter from a formula. `infinite code new | check | watch | docs | examples` writes and checks all of them against the real compilers.

---

## Headless, CLI and agents

Infinite also runs with no window: pictures, video and sound straight from a `.inf` text file on the command line, the same engine the `infinite` command uses. See the [Headless User Guide](docs/headless/Infinite_Headless_User_Guide.pdf) ([Markdown](docs/headless/GUIDE.md)).

```bash
Infinite --validate hello.inf                                   # check a patch
Infinite --frame hello.inf 0,1,2 pics/ --contact-sheet sheet.png  # pictures at chosen seconds
Infinite --render hello.inf hello.mp4 --duration 4 --fps 30     # video with sound
```

### The `infinite` command

Inside the Terminal panel (Ctrl + \`), or from any shell, `infinite` drives the open patch:

```bash
infinite graph                 # look at the nodes and cables
infinite add Analog            # edit: add, connect, set, mod, expr, undo
infinite play                  # play, tempo, seek, session, timeline
infinite shot selected node.png  # see and hear: shot, frame, render, audio
```

Run `infinite guide` for the full reference. With no app running, every verb that takes a `.inf` file runs headless.

### Agents and MCP

For AI coding assistants there is an MCP server, [`tools/npm/infinite-engine`](tools/npm/infinite-engine) (npm: `infinite-engine`), and a Claude plugin described in [`tools/mcp/README.md`](tools/mcp/README.md).

```bash
# Add Infinite to Claude Desktop, Cursor and Windsurf:
npx -y infinite-engine init
```

| MCP tool | What it does |
|---|---|
| `infinite_synthesize_field_synth` | Renders WAV audio from an inline Field Synth kernel |
| `infinite_synthesize_field_notes` | Renders algorithmic note sequences and Euclidean rhythms from a Field Notes kernel |
| `infinite_process_field_effect` | Processes audio through an inline Field Effect kernel |
| `infinite_render_field_pixel` | Renders a Field Pixel shader to a PNG |
| `infinite_render_field_primitive` | Renders parametric surfaces from Field Primitive equations |
| `infinite_render_field_modifier` | Deforms a mesh with an inline Field Modifier kernel |
| `infinite_render_sketch3d` | Renders JavaScript-built 3D geometry to a PNG |
| `infinite_validate_patch` | Checks a `.inf` patch against the engine |
| `infinite_render_video` | Renders an MP4 from a `.inf` patch |
| `infinite_compare_frames` | Compares two pictures (PNG, `.inf` or patch text) and says whether they match, in plain words |
| `infinite_find_knob` | Finds which node and param move gives a wanted picture change, from measured renders |
| `infinite_node_effects` | Gives the measured effect of an Effects or colour node: what adding it does, and what each param does |

Tool schemas and prompts: [`tools/npm/infinite-engine/README.md`](tools/npm/infinite-engine/README.md) and [`AGENT.md`](AGENT.md).

### AI skill files

Infinite ships reference files you can hand to any AI assistant, or drop into `~/.claude/skills/`, so it writes correct patches and code first time. Settings > Terminal installs all three at once (**Install All AI Skills**) or the patch authoring file alone; the Field Language and Expression Globals pages each have an **Install AI Skill** button for their own file. In the Terminal, `infinite guide` and `infinite code docs <language>` print the same references.

---

## Manuals and shortcuts

| Manual | What it covers |
|---|---|
| [Node Reference Manual](Infinite_Node_Reference_Manual.pdf) | Every node, with its pins and parameters |
| [Field Language Manual](Field_Language_Manual.pdf) | Syntax, domains, domain-transfer operators, reserved words, the node and file format reference |
| [Headless User Guide](docs/headless/Infinite_Headless_User_Guide.pdf) | Rendering and checking patches from the command line |

## Installation

### Pre-built app (macOS, Windows and Linux)
Download the latest build from [GitHub Releases](https://github.com/n1m21n/Infinite/releases): `Infinite.dmg` for macOS, `Infinite-windows-x64.zip` / `Infinite-windows-ARM64.zip` for Windows (or the `Infinite-Setup-windows-x64.exe` / `Infinite-Setup-windows-ARM64.exe` installer), or `Infinite-x86_64.AppImage` for Linux.

**macOS** (Infinite is not notarised by Apple, so the first launch needs one extra step)

With Homebrew: `brew install --cask n1m21n/tap/infinite` (the first-launch step below still applies).

1. Open the DMG and drag `Infinite.app` to Applications.
2. Double-click `Fix & Open Infinite.command` in the DMG and press `y`. It clears the download flag on Infinite only, then opens it.
   - Or by hand: try to open Infinite once, then **System Settings > Privacy & Security > Open Anyway** (macOS 15 and later), or right-click `Infinite.app` > **Open** (older macOS).
   - Or in Terminal: `xattr -dr com.apple.quarantine /Applications/Infinite.app`

**Windows**
1. Unzip the `x64` or `ARM64` build for your CPU.
2. Run `Infinite.exe`. The build is not code-signed, so Windows SmartScreen may say "Windows protected your PC": click **More info**, then **Run anyway**. It asks once per download.

**Linux**
1. `chmod +x Infinite-x86_64.AppImage`
2. Run `./Infinite-x86_64.AppImage`. It bundles the static AppImage runtime, so no `libfuse2` install is needed. In a container or on a system with no `/dev/fuse`, use `./Infinite-x86_64.AppImage --appimage-extract-and-run` instead.

### Build from source

#### macOS
Requirements: **CMake 3.16+** and **Xcode Command Line Tools**.

```bash
git clone https://github.com/n1m21n/Infinite.git
cd Infinite
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
open build/Infinite.app
```

#### Windows
Requirements: **CMake 3.16+** and **Visual Studio 2022/2026** (MSVC v143+, Desktop development with C++).

```powershell
git clone https://github.com/n1m21n/Infinite.git
cd Infinite
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\Infinite.exe
```

#### Linux
Requirements: **CMake 3.16+**, a C++20 compiler (Clang or GCC), Ninja, and the dev packages in `tools/linux/deps-apt.sh` (X11, GLFW, ALSA and fontconfig headers). x86_64, glibc 2.35 or newer (Ubuntu 22.04+ or equivalent) to match the published AppImage's floor.

```bash
git clone https://github.com/n1m21n/Infinite.git
cd Infinite
tools/linux/deps-apt.sh   # installs build and runtime deps via apt
tools/linux/build.sh      # configures and builds build-linux/Infinite
./build-linux/Infinite
```

---

## Architecture

The engine is split into `core` (node DAG, transport, modulation), `nodes`, `audio`, `arrange` (Timeline, Session, Piano Roll), `app` (canvas, panels, headless and RPC) and `platform` (CoreAudio, WASAPI and ALSA backends). See [ARCHITECTURE.md](ARCHITECTURE.md) and [docs/CODE_STANDARDS.md](docs/CODE_STANDARDS.md).

## Limits

The Linux build has the same node graph and file format as macOS and Windows, with these platform gaps:

- No Syphon/Spout equivalent, so no zero-copy video I/O with other apps.
- No Audio Unit (AU) hosting: VST3 only (same as Windows).
- No LV2 plugin hosting.
- No complex-script text shaping (no HarfBuzz): Latin-script text renders correctly, but right-to-left and complex scripts (Arabic, Devanagari and others) may not.
- VST3 plugin editor windows need X11 or XWayland; a pure-Wayland session without XWayland cannot show plugin GUIs.
- x86_64 only: no arm64 build is published.

---

## Contributing

Bug reports, node ideas and pull requests are welcome: [GitHub Issues](https://github.com/n1m21n/Infinite/issues), the [Discord community](https://discord.gg/GMeQTyJqkH), and [CONTRIBUTING.md](CONTRIBUTING.md). Pull requests follow [docs/CODE_STANDARDS.md](docs/CODE_STANDARDS.md).

---

## Prior art and acknowledgements

The tools and papers Infinite builds on are named in [ACKNOWLEDGEMENTS.md](docs/ACKNOWLEDGEMENTS.md), and the people who contributed are credited in [CONTRIBUTING.md](CONTRIBUTING.md#community-and-contributors).

---

## Cite and credit

If you write about, review or build on Infinite, please credit it as: *Infinite, by Naman Soni (n1m21n), https://infinitedevices.org*. GitHub's "Cite this repository" button uses [CITATION.cff](CITATION.cff).

---

## Licence

Infinite's source code is licensed under the [MIT License](LICENSE).
Default builds include VST3 plugin hosting linking the [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (MIT since 3.8). The default Linux build is GPLv3 because of its FFmpeg/x264 encoder; see `LICENSE`.
