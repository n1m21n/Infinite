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
| Tracy v0.11.1 (optional, `INFINITE_ENABLE_TRACY`, off by default) | BSD-3-Clause | profiler client, both editions; fetched at configure time, not in release builds |
| Google Filament @ 13be3649 (techniques ported into `shaders/hq_brdf.glsl`, `shaders/hq_ibl.frag.glsl`, `shaders/hq_shadows.glsl`, `shaders/hq_ao_depth.comp.glsl`, `shaders/hq_gtao.comp.glsl`, `shaders/hq_ao_blur.comp.glsl`, `shaders/hq_taa.comp.glsl`, `shaders/hq_bloom_down.frag.glsl`, `shaders/hq_bloom_up.frag.glsl`, `shaders/hq_finish.frag.glsl`; no Filament code is linked) | Apache-2.0, © Google LLC | Render 3D HQ (0.71): BRDF, image-based lighting, DFG table, specular AA, shadow filtering, GTAO, TAA, AO terms, bloom and the ACES tonemap rewritten in GLSL 4.5; each ported file starts with the source path, commit and changes (`docs/RENDER-3D-HQ-FILAMENT.md` section 14). `shaders/hq_fxaa.frag.glsl` is written from Timothy Lottes' published FXAA 3.11 algorithm (NVIDIA), with no code copied |
| AMD FidelityFX Super Resolution 1 v1.0.2 (`ffx_a.h`, `ffx_fsr1.h`) | MIT, © 2021 Advanced Micro Devices | Render 3D's upscale (EASU + RCAS), both editions; the two headers are fetched at configure time and compiled into the exe (GLSL) and into Render3DTemporal.cpp (constants) |

`ffmpeg.exe`, when present next to the exe, is a separate program with its own licence (most
Windows builds of it are GPL); it is not linked into Infinite-Turbo.

## Synth REF firmware cores (full build only)

The MIT edition contains none of this. In the full edition, `src/gpl/SynthRef/` loads instrument
firmware at run time from separate DLLs (`<exe>\synthref\cores`), built from the sources below
by `CMakeLists.txt`. Details: `docs/SYNTH-REF.md`.

| Component | Source | Licence | Notes |
|---|---|---|---|
| SMK37 OPEN V01, M-VAVE's open firmware for the SMK-37 Pro (`app/`, `web/`, `sdk/include/*.h`) | github.com/m-vave/smk37_open @ `b2d6bcbcd724adb13bcf85e44cfa16bb2c8153a8`, vendored unmodified in `src/gpl/SynthRef/cores/smk37_open/upstream/` (86 files; `UPSTREAM.md` lists the SHA-256 of each) | GPL-3.0-only WITH GPL-3.0-linking-exception, Copyright (c) 2026 珠海笙科智能科技有限公司 (M-VAVE) | the open web engine is built natively; the proprietary `sdk/lib` and `sdk/firmware` are not vendored and not used |
| DX7 FM core (`web/synth/dx7*.c/.h`), C port of MSFA / Dexed msfa | github.com/google/music-synthesizer-for-android, github.com/asb2m10/dexed (revisions in the upstream `THIRD_PARTY.md`) | Apache-2.0 | Copyright 2012, 2013 Google Inc.; Copyright 2016-2025 Pascal Gauthier (Dexed msfa note-model changes). Modified by M-VAVE, 2026: C port, fixed-point arithmetic, block processing, voice management and MIDI controls |
| 128 factory DX7 voices (`app/factory_dx7.inc`, `app/factory_dx7_packed.inc`) | extracted by M-VAVE from the FM1 OPEN resource `FM-1_015.fwsc` (upstream resource notice retained) | Apache-2.0 | modified by M-VAVE, 2026: names and positions kept, twelve out-of-range values in eleven voices clamped to standard DX7 limits |
| Open303 (`third_party/synth/bass303*`) | github.com/RobinSchmidt/Open303 | MIT | Copyright (c) 2009 Robin Schmidt (Open303); C adaptation, 2026 M-VAVE |
| DaisySP 808 and pluck, from Plaits (`third_party/synth/drum808*`, `pluck*`) | github.com/daisyaudio/DaisySP, Mutable Instruments Plaits | MIT | Copyright (c) 2020 Electrosmith, Corp.; Copyright (c) 2016 Emilie Gillet (Plaits); 2026 M-VAVE |
| SVF filter (`third_party/synth/dsp_math.h`) | stmlib | MIT | Copyright (c) 2014 Emilie Gillet (stmlib); math helpers 2026 M-VAVE |
| emu2149 PSG (`third_party/synth/emu2149.*`) | github.com/digital-sound-antiques/emu2149, v1.42 | MIT | Copyright (c) 2014 Mitsutaka Okazaki (emu2149; source copyright 2001-2022); the file header reads Copyright (C) 2001-2022 Mitsutaka Okazaki; adapted by M-VAVE, 2026-10-09 |
| SEA codec (`web/synth/sea_decode.c`, `sea_encode.h`, `sample_metadata.inc`) | github.com/Daninet/sea-codec | MIT | Copyright (c) 2025 Dani Biró (SEA); modifications (c) 2026 M-VAVE |
| FM1 OPEN V14, M-VAVE's open firmware for the FM-1 (`app/`, `web/`, `sdk/include/*.h`); the same family and layout as SMK37 OPEN, built by the same adapter | github.com/m-vave/fm1_open @ `4c7496daac04a95ff10d1afdcc8f9ead2dbabc41` (2026-10-21), vendored unmodified in `src/gpl/SynthRef/cores/fm1_open/upstream/` (81 files; `UPSTREAM.md` lists the SHA-256 of each) | GPL-3.0-only WITH GPL-3.0-linking-exception, Copyright (c) 2026 珠海笙科智能科技有限公司 (M-VAVE) | the open web engine is built natively; `sdk/lib`, `sdk/firmware`, `sdk/include/libc`, `app/board` and the tools are not vendored and not used |
| FM1 OPEN: DX7 FM core (`web/synth/dx7*`), C port of MSFA / Dexed msfa | github.com/google/music-synthesizer-for-android, github.com/asb2m10/dexed (revisions in `fm1_open/upstream/THIRD_PARTY.md`) | Apache-2.0 | Copyright 2012, 2013 Google Inc. (file headers); Modified for FM1 by M-VAVE, 2026: C port, fixed-point and block processing |
| FM1 OPEN: 128 factory DX7 voices (`app/fm_factory.inc`) | M-VAVE's original V15 factory DX7 bank, extracted from the bundled V15 recovery image (`THIRD_PARTY.md`) | Apache-2.0 | losslessly encoded by M-VAVE, 2026-10-19 |
| FM1 OPEN: Open303 (`third_party/synth/bass303*`), DaisySP / Plaits 808 and pluck (`drum808*`, `pluck*`), SVF (`dsp_math.h`, stmlib), emu2149 PSG, SEA codec (`web/synth/sea_decode.c`, `sample_metadata.inc`) | github.com/RobinSchmidt/Open303, github.com/daisyaudio/DaisySP, stmlib, github.com/digital-sound-antiques/emu2149, github.com/Daninet/sea-codec | MIT | Copyright (c) 2009 Robin Schmidt (Open303), 2026 M-VAVE (C adaptation); Copyright (c) 2020 Electrosmith, Corp, Emilie Gillet, 2026 M-VAVE (808, pluck); Copyright (c) 2014 Emilie Gillet (SVF), 2026 M-VAVE (math helpers); emu2149 v1.42, FM1 adaptation 2026-10-09 (the notice is in `THIRD_PARTY.md`); Copyright (c) 2025 Dani Biro, modifications (c) 2026 M-VAVE (SEA) |
| SLOOP SMK 2.6 (SLOOP 2.8), the community firmware for the SMK-37 Pro: SMK runtime, SLOOP 2.8 core, the firmware's browser engine (`firmware/`, `sloop/firmware/`, `web/audio/`) | github.com/ChanceTheMaker/sloop-smk @ `08c823081d9ab394077767a0604df7799ad449b0` (branch integrate/sloop-2.6-smk; its `sloop/` is SLOOP 2.8 from github.com/isod89/sloop-fm1 @ `b77bf55`), vendored unmodified in `src/gpl/SynthRef/cores/sloop_smk/upstream/` (83 files; `UPSTREAM.md` lists the SHA-256 of each) | GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Copyright (C) 2026 Andre Rezemini for `sloop/firmware/src/cpu1.h` and `scratch.h` (github.com/andrerezemini/sloop-lab) | its own engines, screen and panel; built as one translation unit from the firmware's own browser engine |
| SLOOP SMK: FM6 engine (`sloop/firmware/src/fm6_core.c`), C port of msfa from Dexed | github.com/asb2m10/dexed (msfa) | Apache-2.0 | Copyright 2012 Google Inc.; Copyright 2016-2025 Pascal Gauthier; C port for Felucca (integer only), 2026: Leo Kuroshita, Hügelton Instruments (Dexed itself is GPL-3.0; only msfa is used) |
| SLOOP SMK: physical models (`sloop/firmware/src/phys_dsp.c`, `phys_symp.c`) | DaisySP (Electrosmith), Rings (Emilie Gillet, github.com/pichenettes/eurorack) | MIT | Copyright (c) 2020 Electrosmith, Corp, Emilie Gillet (`phys_dsp.c`); Copyright (c) 2015 Emilie Gillet (`phys_symp.c`); ported to fixed-point C for Felucca, 2026: Leo Kuroshita, Hügelton Instruments |
| SLOOP SMK: `generated/felucca_*.h` (font, samples, drum kits, FM6 patches, tables, icons) | produced by the firmware's own `sloop/tools/gen_*.py`; font: Terminus Bold 8x16 (`ter-u16b`); samples: Versilian Studios VSCO-2 CE, VCSL, Sonic Pi (SCRCH) as prepared by SLOOP (`upstream/sloop/LICENSING.md`) | font SIL OFL 1.1; samples CC0 1.0 | the icons table comes from a blank atlas: the all-rights-reserved Felucca assets (`icons.png`, panel art, Hügelton Sample Pack, Copyright (C) 2026 Hügelton Instruments) are not vendored and not used, so the knob-card icons are blank. `upstream/LICENSING.md` (the older root text of sloop-smk) disagrees with `upstream/sloop/LICENSING.md`; the stricter one is followed |
| llvm-mingw 20260922 (clang, ucrt, x86_64) | github.com/mstorsjo/llvm-mingw; downloaded at configure time into the build tree, SHA-512 pinned in `CMakeLists.txt` | LLVM: Apache-2.0 with LLVM exceptions; mingw-w64 runtime: its own permissive licences | build-time tool only, not shipped. The core DLL can contain objects from the toolchain's C runtime |

The full licence texts (GPL-3.0 with the linking exception, MIT, Apache-2.0) and the exact
upstream notices are in `src/gpl/SynthRef/cores/smk37_open/upstream/LICENSE.md` and
`upstream/THIRD_PARTY.md`, the same two files under `cores/fm1_open/upstream/`, and, for SLOOP SMK,
`cores/sloop_smk/upstream/LICENSE`, `upstream/LICENSING.md` and `upstream/sloop/LICENSING.md`,
which stay with the source. Product names identify compatible formats; "M-VAVE", "FM-1" and
"SMK-37" belong to their owners, and no endorsement by M-VAVE, the SLOOP / Felucca authors or the
authors above is implied.

DX7 `.syx` banks that a user imports with LOAD FILE are not bundled and not covered by these
licences; their provenance is the user's responsibility (`docs/SYNTH-REF-BANCOS.md`).

## Bundled media

| Folder | Source | Licence |
|---|---|---|
| `assets/drumkits/turbo-basic` | Infinite-Turbo | MIT |
| `assets/drumkits-gpl/sloop-*` (full build only) | SLOOP's 37 drum kits rendered with its own engine | GPL-3.0 material, see `assets/drumkits-gpl/README.md` |
| `assets/drum-patterns/groove-midi` | Magenta Groove MIDI Dataset (beat performances) | CC BY 4.0, Gillick, Roberts, Engel, Eck, Bamman (2019), see its CREDITS.txt |
| `assets/drumkits/cc0-*` | VCSL, VSCO-2 CE (Versilian Studios), Sonic Pi samples, as prepared by SLOOP | CC0 1.0, see `assets/drumkits/CC0-CREDITS.txt` |
| `assets/fonts` | IBM Plex Sans, Atkinson Hyperlegible, Inter (Infinite Dark theme) | SIL OFL 1.1 (`OFL-Inter.txt` for Inter) |
| `src/audio/dsp/HrtfKemarData.cpp` | MIT Media Lab KEMAR HRTF measurements (Gardner and Martin, 1994) | no usage restrictions; cite the authors |
| `assets/note-patterns`, built-in Pattern Player bank | Infinite-Turbo | MIT |

## NDI SDK headers

`external/ndi/include` - NDI(R) SDK headers, used to load the NDI runtime at run time (never bundled).
License: `external/ndi/SDK-LICENSE.txt`. NDI(R) is a registered trademark of Vizrt NDI AB.
