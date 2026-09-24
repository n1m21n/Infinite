# Peer Repositories for Prior Art Search

Curated peer repos grouped by domain. Consult this list first when searching for how other projects solved platform, architectural, audio, visual, or packaging problems before widening to global search.

**Licence tags decide what you may read.** Infinite is MIT.
- **Permissive** (MIT, BSD, zlib, public domain): source, diffs, code search and DeepWiki are all fine. Cite the licence.
- **copyleft** (GPL, AGPL, dual GPL/commercial): **discussions only.** You may read issue and PR threads, commit *messages*, release notes and docs/wiki prose, and cite them for the concept. You may **not** open source files or diffs, run `gh search code`, grep.app or DeepWiki against them, or paste their code. If a code-search hit lands in a copyleft repo, don't open it; note the repo and go to its issues instead.
- A repo that isn't listed: check its licence (`gh api repos/OWNER/REPO --jq .license.spdx_id`) before reading any code.

## Node-based Realtime Visuals

- `tooll3/t3` (redirects to `tixl3d/tixl`) [MIT]: ImGui + node graph + realtime GPU pipeline; closest architectural peer to Infinite in stack and paradigm.
- `cables-gl/cables` [MIT]: WebGL node-based visual programming environment; real-world dataflow and operator graph conventions.
- `hydra-synth/hydra` [AGPL-3.0 · copyleft]: Live-coding modular video synth; signal-driven visuals and GLSL generative pipeline.
- `thedmd/imgui-node-editor` [MIT]: Reference ImGui node canvas, link routing, multi-selection, and pin interaction patterns.
- `Nelarius/imnodes` [MIT]: Minimalist ImGui node editor; clean link state management and pin interaction model.

## Modular & Audio Applications

- `BespokeSynth/BespokeSynth` [GPL-3.0 · copyleft]: Realtime modular synthesizer and node environment.
- `VCVRack/Rack` [GPL-3.0 · copyleft]: Modular virtual Eurorack; lock-free audio thread scheduling, cable/port patching, and module lifecycle.
- `DISTRHO/Cardinal` [GPL-3.0 · copyleft]: Virtual modular synthesizer plugin and standalone (Carla/Rack integration and cross-platform builds).
- `surge-synthesizer/surge` [GPL-3.0 · copyleft]: Modern open-source hybrid synth; cross-platform DSP/SIMD, packaging, and CI workflows.
- `LMMS/lmms` [GPL-2.0 · copyleft]: Open-source DAW; ALSA/Pulse/JACK audio driver backends and multi-format plugin hosting.
- `Ardour/ardour` [GPL-2.0 · copyleft]: Professional digital audio workstation; realtime Linux audio/MIDI latency, thread isolation, and engine design.
- `zrythm/zrythm` [AGPL-3.0 · copyleft]: Modern DAW; PipeWire/JACK integration, desktop environment integration, and audio scheduling.
- `mtytel/vital` [GPL-3.0 · copyleft]: Modern visual wavetable synth; OpenGL rendering inside audio plugins and Linux display compatibility.

## Plugin Hosting

- `falkTX/Carla` [GPL-2.0 · copyleft]: Production multi-format plugin host (VST2/VST3/LV2/CLAP/AU); Linux X11/Wayland window embedding and bridge architecture.
- `robbert-vdh/yabridge` [GPL-3.0 · copyleft]: Modern Linux Wine/native VST bridge; authoritative reference on Linux VST3 run loops, X11 event dispatch, and throttling.
- `juce-framework/JUCE` [AGPL-3.0/commercial · copyleft]: De-facto standard audio framework; reference Linux VST3 host and client implementation, X11 event loop integration.
- `steinbergmedia/vst3sdk` [MIT (since 3.8; older releases GPL-3.0/proprietary)]: Official VST3 SDK; module loading (`ModuleEntry`/`GetPluginFactory`), Linux `IRunLoop`, and host contracts.
- `Tracktion/tracktion_engine` [GPL-3.0/commercial · copyleft]: Real-world production DAW engine; plugin host lifecycle, scan/sandboxing, and audio graph processing.

## Cross-Platform Shipping & Packaging

- `audacity/audacity` [GPL · copyleft]: Multi-platform audio editor; Linux AppImage packaging, GLVND/OpenGL runtime dependencies, and audio backends.
- `musescore/MuseScore` [GPL-3.0 · copyleft]: Large-scale desktop app; multi-distro AppImage packaging, font rendering, and platform abstraction.
- `obsproject/obs-studio` [GPL-2.0 · copyleft]: Production realtime video/audio capture; Wayland/PipeWire/OpenGL/X11 platform layers and CI builds.
- `WerWolv/ImHex` [GPL-2.0 · copyleft]: ImGui + GLFW desktop hex editor; modern Docker-based AppImage packaging and multi-distro release workflows.
- `blender/blender` [GPL · copyleft]: Production 3D/GL application; Linux ABI compatibility, static bundling conventions, and GL context handling.

## Video, Capture & OpenGL

- `mpv-player/mpv` [GPL-2.0+ (LGPL build option) · copyleft]: High-performance video player; Linux display backends (X11/Wayland), GL/EGL context management, and frame pacing.
- `glfw/glfw` [zlib]: Windowing and input library used by Infinite; X11/Wayland backend differences, cursor/scaling/event contracts.
- `ocornut/imgui` [MIT]: Core UI library used by Infinite; docking, viewport management, and keyboard/mouse event dispatch.
- `Syphon/Syphon-Framework` [BSD]: macOS inter-app frame sharing protocol implemented by Infinite.
- `leadedge/Spout2` [BSD-2-Clause]: Windows inter-app texture sharing protocol implemented by Infinite.

## Core Libraries Used by Infinite

- `mackron/miniaudio` [Public domain / MIT-0]: Audio playback and capture engine used by Infinite; ALSA, PulseAudio, JACK, WASAPI, and CoreAudio backends.
- `nothings/stb` [Public domain / MIT]: Single-file image loading and writing libraries used across Infinite's asset pipeline.
- `syoyo/tinyexr` [BSD-3-Clause]: HDR OpenEXR loading and saving library used in Infinite's image pipeline.
- `microsoft/onnxruntime` [MIT]: Machine learning runtime used by Infinite's neural network and inference nodes.
