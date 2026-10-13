# Infinite-Turbo: licences and third-party notices

## Editions

| Edition | Built with | Contains | Licence of the binary |
|---|---|---|---|
| **MIT** (public release, `release-windows.bat`) | `build-windows.bat mit` | Infinite (MIT) + Turbo code (MIT) | MIT source; the binary links the libraries below under their own terms |
| **Full** (`build-windows.bat`) | default preset | the MIT edition + `src/gpl` (GPL-3.0 modules) | GPL-3.0 as a whole (it includes GPL-3.0-only code) |

The source of both editions is this repository. Code outside `src/gpl/` is MIT
(`LICENSE`, Copyright (c) 2026 n1m21n, Infinite; Turbo changes by Ricardo Palmieri / Noisetupi
under the same licence). Code inside `src/gpl/` is GPL-3.0 (`src/gpl/LICENSE`); see
`src/gpl/README.md` for the module list and the projects it comes from.

## Libraries linked into the executable

| Library | Licence | Notes |
|---|---|---|
| JUCE 8 | AGPLv3 or JUCE commercial licence | audio devices, MIDI, VST3 hosting. Distributing a binary without a JUCE licence means AGPLv3 terms: the full source must be available (it is, here) |
| FFmpeg (vcpkg, LGPL build) | LGPL-2.1-or-later | video and audio decoding |
| OpenCV | Apache-2.0 | image processing |
| Spout2 | BSD-2-Clause | texture sharing |
| Assimp | BSD-3-Clause | 3D model import |
| GLFW, GLEW, Dear ImGui, imgui-node-editor, stb, nlohmann/json | zlib / MIT / public domain | UI and GL |
| ONNX Runtime | MIT | background removal |
| Windows ML / DirectML | Microsoft redistributable licence | shipped next to the exe |
| VST3 SDK | MIT (since VST 3.8, Oct 2025) | |
| ASIO SDK | Steinberg open source licence (Oct 2025) | |
| signalsmith-stretch | MIT | time stretch |
| shine | LGPL-2.0 | MP3 encoding |

`ffmpeg.exe`, when present next to the exe, is a separate program with its own licence (most
Windows builds of it are GPL); it is not linked into Infinite-Turbo.

## Bundled media

| Folder | Source | Licence |
|---|---|---|
| `assets/drumkits/turbo-basic` | Infinite-Turbo | MIT |
| `assets/drumkits-gpl/sloop-*` (full build only) | SLOOP's 37 drum kits rendered with its own engine | GPL-3.0 material, see `assets/drumkits-gpl/README.md` |
| `assets/drum-patterns/groove-midi` | Magenta Groove MIDI Dataset (beat performances) | CC BY 4.0, Gillick, Roberts, Engel, Eck, Bamman (2019), see its CREDITS.txt |
| `assets/drumkits/cc0-*` | VCSL, VSCO-2 CE (Versilian Studios), Sonic Pi samples, as prepared by SLOOP | CC0 1.0, see `assets/drumkits/CC0-CREDITS.txt` |
| `assets/fonts` | IBM Plex Sans, Atkinson Hyperlegible | SIL OFL 1.1 |
