# Infinite-Turbo (for Windows)

[![Original project](https://img.shields.io/badge/original-n1m21n%2FInfinite-181717?logo=github)](https://github.com/n1m21n/Infinite)
[![Discord](https://img.shields.io/badge/Discord-Infinite-5865F2?logo=discord&logoColor=white)](https://discord.gg/wpKdexvhn)

**Current version: 0.40.0-turbo** (shown in the window title and in the FILE menu, with the build date).

Infinite-Turbo is a **mod of Infinite**, the node-based audiovisual modular workstation by Naman Soni (realtime image and video processing, procedural 3D, audio synthesis, DSP, VST3 hosting, MIDI, OSC and cross-domain CV modulation). It is unofficial and Windows-only.

What "mod" means here:

- The core is Infinite: same node graph, same modules, same way of patching, and patches still use the `.inf` extension.
- On top of it, Infinite-Turbo adds Windows-specific fixes and tuning (audio thread priorities, VST3 hosting, output windows, build scripts) and new modules of its own: **Layout**, **MPC**, **MPC Out**, **VMPC**, **Looper**, **Super Mixer** and **OSC to CV**, plus the **Predictive** note modules ported from upstream Infinite.
- Patches that use Turbo-only modules do not open in upstream Infinite, and the two versions are not guaranteed to open each other's files.
- Problems found in Infinite-Turbo should be reported here, not to the upstream project.

Infinite-Turbo started from the Windows R31a community port (upstream commit [`788404a`](https://github.com/n1m21n/Infinite/commit/788404af4b378941394e2d5dcc45c5542cc903fd)). From 0.32 on it targets Windows 10/11 x64 only: every macOS code path was removed so the codebase can be tuned for performance, compatibility and usability on Windows. See [CHANGELOG-TURBO.md](CHANGELOG-TURBO.md).

> The original project was created by [Naman Soni](https://github.com/n1m21n). Infinite-Turbo is maintained by [Ricardo Palmieri](https://github.com/ricardopalmieri) / Noisetupi. It is not an official Infinite release.

![Infinite node graph](docs/screenshot.png)

## Quick start

| I want to... | Run |
|---|---|
| use a prebuilt ZIP | extract it to a writable folder, run `install-runtime.bat` once, then `run-windows.bat` |
| set up a dev machine (first time) | `install-dependencies.bat` (UAC, installs VS Build Tools, vcpkg, libraries, Windows ML, models) |
| compile the test build (default) | `build-windows.bat` (exe in `build\windows-vs2022\Release`) |
| compile + package (dist folder + ZIP) | `build-windows.bat pack` |
| debug build | `build-windows.bat debug` |
| reconfigure from scratch | `build-windows.bat fresh` |
| run the build | `run-windows.bat` |
| run the automatic tests | `test-windows.bat` (`-Quick` for a smoke run; results in `build\test-results\summary.txt`) |
| collect a startup report | `diagnose-windows.bat` (writes `Infinite-Turbo-diagnostic.log`) |

Do not run the application from inside the ZIP.

## What's new in Infinite-Turbo

| Area | Turbo addition |
|---|---|
| Canvas preview | any image node's output drawn behind the node canvas, under the patch (like TouchDesigner's background preview), with scaling, darken, node opacity and grid options |
| Live-coding view | the whole UI goes borderless fullscreen (F11); combine it with the canvas preview and a hidden side panel to patch on top of the image |
| Node menu | reorganised categories: Video, Utility, Audio In/Out, Audio to Visual, Audio Mix & Routing, Synths & Samplers, Notes & Sequencers, Prediction, CV Tools, Analysis, MIDI & OSC |
| MIDI learn | every parameter with a CV pin: Ctrl+M (or VIEW > MIDI learn mode), click the parameter, move a control. Right-click a parameter's pin: learn / clear / edit. Each mapping is device + channel + CC or note (identical controllers get "#2", "#3"), with "any device" / "any channel" for portable patches, continuous / momentary / toggle, invert, soft takeover and min/max in the parameter's units. VIEW > MIDI map lists every mapping and device (rescan for hot-plugged controllers). Saved with the patch |
| Range mapper | right-click a modulated parameter: in min/max (the part of the incoming signal used, with capture buttons) and out min/max in the parameter's own units, invert, reset |
| Color markers | right-click a node > Color marker: 8 colours, applied to every selected node; saved in the patch |
| OSC | OSC Receive and the new OSC to CV (8 addresses/arguments -> 8 CV outputs, learn, range, invert, smoothing) listen on the LAN too, decode bundles and all numeric arguments, and share ports |
| Prediction | Predictive Notes, Quantize, Velocity and Rhythm, ported from upstream Infinite: learn how you play, then play or correct in that style |
| File dialogs | open/save dialogs no longer pause playback, video or outputs, and always open in front of the editor (also in fullscreen) |
| Side panel | PANEL button in the top bar, Ctrl+B or VIEW > Side panel |
| Layout node | TouchDesigner-style canvas of exact pixel size (e.g. 1920 x 1080) with 8 image inputs. Each layer starts at the real pixel size of its source; set x/y in canvas pixels, scale (anchored at the layer centre) or an exact width/height, opacity; drag layers on the miniature; 1:1 / fit / fill / centre buttons; x, y, scale and opacity have CV pins |
| MPC node | 16 sample pads, one shot / gate / loop toggle, manual trim in/out (drag the handles on the waveform or type the values), per-pad volume, pitch and pan, CV pin per pad; MPC Out gives any pad its own output |
| VMPC node | the MPC for video: 16 pads with one video clip each (one shot / gate / loop, trim in/out, speed, reverse), triggered by mouse, CV pins or MIDI notes; outputs the clip of the last pad hit, plus its soundtrack on an "audio" output (clips without audio stay silent) |
| Looper node | REC / PLAY / DUB / CLEAR, bar-synced length, forward / reverse / ping-pong, round-trip latency compensation (auto + manual offset) |
| Super Mixer node | 16 channels with input gain, fader, pan, mute, solo and 3-band EQ, master fader, CV pin on every control |
| Projection node | transparent (alpha) outside the warped image, per-edge edge blend (left / right / top / bottom, width, curve, gamma) fading the alpha or darkening RGB, antialiased outline |
| Audio Analyze | its input takes any audio cable (synth, VST, Audio In, mixer, Audio File), besides the live device input |
| Output windows | exact size in pixels, Fit / Real pixels 1:1 / Fill / Stretch, black background; in the node's parameters (OUTPUT WINDOW section) and in the node's right-click menu |
| VST3 | MIDI reaches instrument plugins, longer scan timeout, single-file `.vst3` plugins found (Kontakt and other Native Instruments plugins) |
| Audio | no more competing MMCSS priority on the render thread (fewer buffer underruns), denormal protection on the audio thread |
| Ported from upstream (0.39) | Macro Slider / Bipolar Knob / Toggle / Trigger / NumBox / Radio Selector / Step Gate (MIDI-learnable from the node body), Keyboard (on-screen piano + laptop typing), Velocity to CV, Note Switcher, Drift (musical random walk), Audio Meter, alpha filters (show alpha, opacity, set alpha, alpha invert, alpha from luma, alpha levels, premultiply), Anti-Erase blend, Transform pivot, Material UV wrap, Phaser feedback, Flanger damping, Sampler loop crossfade, Forest Green theme with swatch previews, "#2" numbering for duplicate node titles, editable comments (hover and type, corner resize, font size), numpad view keys |
| Performance (0.40) | off-screen nodes skip their body, still image chains stop recooking, two-pass blurs, BGRA video upload, O(N) undo snapshots, background autosave |
| Ported in 0.40 | Slideshow, CV Recorder, Resonator Bank, Cycle Shaper, Spec Blur, FDN Reverb, live spectrum in Audio Filter / EQ (Shift-drag = Q), Explode by loose parts, Audio In channel / pair selection, Modulation matrix window (Shift+M) |
| Browser | star favourites in Modules / Samples / Media / Plugins (a FAVOURITES section on top of Modules), filter and sort per mode, right-click: favourite / Show in Explorer / copy path |
| Build | `build-windows.bat` builds only the test exe; `pack` makes the distributable; `test-windows.bat` runs the self-tests |

Full list per version: [CHANGELOG-TURBO.md](CHANGELOG-TURBO.md).

## Live coding: output behind the patch

1. Select the node whose image you want to see (Output, Projection, Layout, any image node).
2. Press **Ctrl+Shift+B** (or right-click the node > *Show behind the canvas*, or *SHOW BEHIND CANVAS* in its parameters).
3. Press **Ctrl+B** to hide the side panel and **F11** to put the UI in fullscreen.
4. Adjust in **VIEW > Output behind the canvas**: scaling (fit, real pixels 1:1, fill, stretch), darken, node opacity and grid.

Ctrl+Shift+B again turns the preview off. An output window can stay open on a projector at the same time.

### Shortcuts

| Keys | Action |
|---|---|
| F11 (editor focused) | UI fullscreen on/off |
| F11 (output window focused) | output window fullscreen on/off |
| Ctrl+B | side panel on/off |
| Ctrl+Shift+B | selected node behind the canvas on/off |
| Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S | new / open / save / save as |
| Ctrl+Z / Ctrl+Shift+Z | undo / redo |
| Ctrl+G / Ctrl+Shift+G | group / ungroup |
| Double-click a slider, knob or drag field | type a value |
| 1 / 3 / 7 / 0 over a 3D viewport (Ctrl for the opposite side) | front / right / top / three-quarter view |
| Z-row and Q-row letters over a Keyboard node | play notes (Musical Typing) |
| Hover a comment and type | edit the note |
| Shift+M | modulation matrix |

## Features inherited from the R31a port

### Native Windows build and runtime

- Windows 10 and Windows 11 x64 support.
- Visual Studio 2022, CMake presets and vcpkg manifest workflow.
- Automated dependency installation with UAC elevation and visible progress.
- Reproducible source and distributable package scripts.
- GUI executable without an additional terminal window.
- Runtime logging, startup diagnostics, crash reporting and autosave recovery.
- High-performance GPU preference for NVIDIA Optimus and AMD PowerXpress systems.

### Audio, MIDI and VST3

- JUCE-based WASAPI audio input and output.
- Configurable device, sample rate and audio buffer.
- Windows MIDI input, output and clock support.
- VST3 hosting with native plugin editor windows.
- Isolated VST3 scanner helper so a crashing plugin does not terminate Infinite.
- Plugin search folders, rescan, blocklist recovery, parameter mapping and state persistence.
- Plugin buffer renegotiation after audio engine restart.
- Invalid sample protection to prevent one plugin from corrupting the complete audio graph.
- Low-latency audio capture without the former multi-second input queue.

### Spout2 and external output

- Spout2 sender and receiver support.
- Windows nodes are displayed as `Syphon/Spout In` and `Syphon/Spout Out` while retaining patch compatibility.
- Resizable output windows for Viewport, Null and visual output nodes.
- F11 borderless fullscreen on the monitor containing the output window.
- Topmost projector output that remains visible while the main UI is operated.
- Hidden cursor in fullscreen output mode.

### Video, cameras and recording

- Video playback with synchronized video and audio outputs.
- Loop, reverse, speed, trim, restart and four externally triggerable cue points.
- Background video decoding to avoid blocking the interface.
- Background camera capture with explicit device activation.
- Aspect-ratio preservation in Projection and Fit workflows.
- Configurable output resolution through the visual graph.
- Asynchronous video recording and FFmpeg audio/video muxing.
- File chooser for recording destination and filename.

### DirectML and background removal

- U2Net and U2Net Human segmentation models.
- Windows ML and DirectML acceleration through DirectX 12.
- NVIDIA, AMD and Intel DX12 adapter support.
- Automatic selection of the high-performance GPU on hybrid systems.
- OpenCV DNN CPU fallback when DirectML is unavailable.
- Asynchronous inference that discards stale requests instead of blocking UI, audio or output.
- Adjustable mask rate, threshold, feather, edge contrast, background color and mix.

### Interface and workflow

- IBM Plex Sans interface font.
- Uppercase interface labels and restored top menu presentation.
- Category color system for 2D visual, 3D, audio, UI and utility nodes.
- Alphabetical node and selector lists.
- Collapsible category browser and automatic search focus.
- Resizable library and parameter sidebars.
- Image thumbnails in the media browser.
- Universal preview monitor control for visual and 3D nodes.
- Node bypass controls and CV inputs for applicable buttons and selectors.
- UI Button, horizontal slider, vertical slider and XY modulation nodes.
- Keyboard value entry by double-clicking sliders and knobs.
- Persistent audio, video, FPS, viewport and interface settings.
- GPU usage and VRAM indicators in the system status area.

### Nodes and sequencing

- Text rendering on Windows through an asynchronous native raster worker.
- Functional Feedback delay node with explicit loop-memory clearing.
- Note Sequencer with 1 to 128 steps, page navigation and exact note entry.
- Drum Sequencer expanded beyond the original 16-step limitation.
- Audio Texture visual modes and improved preview control.
- Video and visual nodes aligned to a consistent interaction pattern.

## Platform mapping

| Original macOS integration | Infinite-Turbo implementation |
|---|---|
| CoreAudio | JUCE and WASAPI |
| CoreMIDI | JUCE Windows MIDI |
| Audio Unit plugins | VST3 plugins |
| Syphon | Spout2 |
| AVFoundation video and camera | OpenCV, DirectShow and FFmpeg |
| CoreText / CoreGraphics text | Windows native text rasterization |
| Apple Vision segmentation | Windows ML, DirectML and OpenCV DNN |
| ModelIO import | Assimp |
| macOS output windows | Win32 multi-monitor output windows |

## Build from source

### Requirements

- Windows 10 or Windows 11 x64.
- Windows Package Manager (`winget`).
- Approximately 35 GB of free disk space for the first dependency build.
- Internet access during dependency installation.

### Clone

```bat
git clone https://github.com/ricardopalmieri/Infinite.git Infinite-Turbo
cd Infinite-Turbo
```

### Install build dependencies

```bat
install-dependencies.bat
```

Accept the UAC prompt. The installer prepares Git, CMake, Visual Studio Build Tools, vcpkg, OpenCV, Spout2, Assimp, Windows ML, DirectML, FFmpeg and the segmentation models.

The first installation can take a long time because large C++ dependencies may be compiled locally.

### Compile

```bat
build-windows.bat
```

This builds only the test executable:

```text
build\windows-vs2022\Release\Infinite-Turbo.exe
```

Use `build-windows.bat fresh` after changing the version in `CMakeLists.txt` or after a dependency update.

### Package (release)

```bat
build-windows.bat pack
```

Package outputs:

```text
dist\Infinite-Turbo-Windows-x64\Infinite-Turbo.exe
dist\Infinite-Turbo-Windows-x64.zip
```

Detailed build instructions and troubleshooting are available in [WINDOWS_BUILD.md](WINDOWS_BUILD.md).

## Runtime files and user data

Infinite-Turbo stores user data under `%LOCALAPPDATA%\Infinite` (same folder as the R31a port, so existing settings, plugin index and models keep working):

```text
Infinite.settings
Infinite.log
Infinite-startup.log
autosave recovery files
VST3 scan logs and blocklist
segmentation models
```

These files are not stored in the repository or inside patch documents.

## Troubleshooting

- Run `diagnose-windows.bat` if the application exits during startup.
- Use `Rescan plugins` after changing VST3 folders. Only VST3 is supported; VST2 plugins (`.dll`) are not.
- "Buffer underruns detected": raise the audio buffer size in the audio settings, and close other audio applications that use the same device.
- Confirm that Spout sender and receiver use the same GPU.
- Update the GPU driver if DirectML fails, then use the CPU fallback while investigating.
- Confirm that `ffmpeg.exe` is beside `Infinite-Turbo.exe` when recording or playing video audio.
- Avoid running the project from OneDrive-synchronized or read-only folders during the first build.

## Project structure

```text
src/core/       graph, patch, transport, modulation and rendering infrastructure
src/nodes/      visual, 3D, audio, MIDI, CV and UI nodes
src/audio/      audio engine, DSP, plugin scanning and file writing
src/platform/   Windows platform layer (JUCE, OpenCV, Spout2, Windows ML, Win32)
cmake/          platform resources
scripts/        source and runtime packaging
assets/         icons, fonts and example patches
```

## Upstream and contributing

The canonical upstream project is [n1m21n/Infinite](https://github.com/n1m21n/Infinite).

The R31a snapshot remains on the `windows/r31a-snapshot` branch. Infinite-Turbo is Windows-only and diverges from upstream; upstream changes are ported selectively.

Bug reports should include:

- Windows version.
- CPU and GPU.
- Audio interface and buffer size.
- Steps to reproduce.
- `%LOCALAPPDATA%\Infinite\Infinite.log` or the output from `diagnose-windows.bat`.

## Credits

- Original Infinite project: [Naman Soni](https://github.com/n1m21n).
- Windows port, Infinite-Turbo and testing: [Ricardo Palmieri](https://github.com/ricardopalmieri) / Noisetupi.
- Infinite's module architecture is a descendant of [BespokeSynth](https://github.com/BespokeSynth/BespokeSynth).
- Third-party projects include Dear ImGui, imgui-node-editor, GLFW, JUCE, OpenCV, FFmpeg, Spout2, Assimp, Windows ML, DirectML, stb and others listed in the build files.

## License

The original Infinite source is distributed under the MIT License. The Windows build links third-party components with their own terms, including JUCE, VST3, FFmpeg, OpenCV, Spout2, Assimp, Windows ML, ONNX Runtime and DirectML.

Review [LICENSE](LICENSE) and all applicable third-party licenses before redistributing a compiled binary. JUCE may require a commercial license or compliance with its open-source license, depending on how the binary is distributed.
