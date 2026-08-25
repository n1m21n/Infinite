# Infinite Windows R31a

[![Windows x64](https://github.com/ricardopalmieri/Infinite/actions/workflows/windows-build.yml/badge.svg?branch=windows%2Fr31a-snapshot)](https://github.com/ricardopalmieri/Infinite/actions/workflows/windows-build.yml)
[![Original project](https://img.shields.io/badge/original-n1m21n%2FInfinite-181717?logo=github)](https://github.com/n1m21n/Infinite)
[![Discord](https://img.shields.io/badge/Discord-Infinite-5865F2?logo=discord&logoColor=white)](https://discord.gg/wpKdexvhn)

Infinite is a node-based audiovisual modular workstation combining realtime image and video processing, procedural 3D, audio synthesis, DSP, plugin hosting, MIDI, OSC and cross-domain CV modulation.

This branch contains the complete community Windows R31a port. It was developed from upstream commit [`788404a`](https://github.com/n1m21n/Infinite/commit/788404af4b378941394e2d5dcc45c5542cc903fd) and tested as a standalone Windows 11 application.

> The original project was created by [Naman Soni](https://github.com/n1m21n). The Windows R31a port was developed and tested by [Ricardo Palmieri](https://github.com/ricardopalmieri) / Noisetupi. This branch is provided for testing, collaboration and eventual upstream integration.

![Infinite node graph](docs/screenshot.png)

## Download

Download the latest prebuilt Windows package from:

**[Infinite Windows R31a Releases](https://github.com/ricardopalmieri/Infinite/releases/tag/windows-r31a)**

The prebuilt package contains `Infinite.exe`, runtime files, FFmpeg, segmentation models, documentation and launch helpers.

### Run the prebuilt version

1. Download `Infinite-Windows-R31a-x64.zip`.
2. Extract the complete ZIP to a writable folder.
3. Run `install-runtime.bat` once.
4. Run `run-windows.bat` or open `Infinite.exe`.

Do not run the application from inside the ZIP.

## Windows R31a highlights

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

| Original macOS integration | Windows R31a implementation |
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

### Clone the Windows branch

```bat
git clone --branch windows/r31a-snapshot --recursive https://github.com/ricardopalmieri/Infinite.git
cd Infinite
```

### Install build dependencies

```bat
install-dependencies.bat
```

Accept the UAC prompt. The installer prepares Git, CMake, Visual Studio Build Tools, vcpkg, OpenCV, Spout2, Assimp, Windows ML, DirectML, FFmpeg and the segmentation models.

The first installation can take a long time because large C++ dependencies may be compiled locally.

### Compile and package

```bat
build-windows.bat
```

Build outputs:

```text
dist\Infinite-Windows-x64\Infinite.exe
dist\Infinite-Windows-x64.zip
```

Detailed build instructions and troubleshooting are available in [WINDOWS_BUILD.md](WINDOWS_BUILD.md).

## Runtime files and user data

Infinite stores Windows user data under `%LOCALAPPDATA%\Infinite`:

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
- Use `Rescan plugins` after changing VST3 folders.
- Confirm that Spout sender and receiver use the same GPU.
- Update the GPU driver if DirectML fails, then use the CPU fallback while investigating.
- Confirm that `ffmpeg.exe` is beside `Infinite.exe` when recording or playing video audio.
- Avoid running the project from OneDrive-synchronized or read-only folders during the first build.

## Project structure

```text
src/core/       graph, patch, transport, modulation and rendering infrastructure
src/nodes/      visual, 3D, audio, MIDI, CV and UI nodes
src/audio/      audio engine, DSP, plugin scanning and file writing
src/platform/   macOS and Windows platform implementations
cmake/          platform resources
scripts/        source and runtime packaging
assets/         icons, fonts and example patches
```

## Upstream and contributing

The canonical upstream project is [n1m21n/Infinite](https://github.com/n1m21n/Infinite).

Windows R31a is preserved as a tested snapshot. Compatibility work with newer upstream commits should happen in a separate branch so this reference build remains reproducible.

Bug reports should include:

- Windows version.
- CPU and GPU.
- Audio interface and buffer size.
- Steps to reproduce.
- `%LOCALAPPDATA%\Infinite\Infinite.log` or the output from `diagnose-windows.bat`.

## Credits

- Original Infinite project: [Naman Soni](https://github.com/n1m21n).
- Windows R31a port and testing: [Ricardo Palmieri](https://github.com/ricardopalmieri) / Noisetupi.
- Infinite's module architecture is a descendant of [BespokeSynth](https://github.com/BespokeSynth/BespokeSynth).
- Third-party projects include Dear ImGui, imgui-node-editor, GLFW, JUCE, OpenCV, FFmpeg, Spout2, Assimp, Windows ML, DirectML, stb and others listed in the build files.

## License

The original Infinite source is distributed under the MIT License. The Windows build links third-party components with their own terms, including JUCE, VST3, FFmpeg, OpenCV, Spout2, Assimp, Windows ML, ONNX Runtime and DirectML.

Review [LICENSE](LICENSE) and all applicable third-party licenses before redistributing a compiled binary. JUCE may require a commercial license or compliance with its open-source license, depending on how the binary is distributed.
