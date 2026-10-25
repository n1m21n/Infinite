# src/gpl: GPL-3.0 modules (Infinite-Turbo full build)

Code here is derived from GPL-3.0 projects and is compiled only when CMake runs with
`-DINFINITE_ENABLE_GPL=ON` (the default `windows-vs2022` preset, `build-windows.bat`).
The MIT build (`build-windows.bat mit`, preset `windows-vs2022-mit`) leaves this folder out
entirely, and that is the build `release-windows.bat` publishes.

Rules:
- Every file starts with `// SPDX-License-Identifier: GPL-3.0-only` (or `GPL-3.0-or-later`)
  and names the project it comes from.
- Nothing outside `src/gpl/` includes a header from here except `main.cpp`, and only inside
  `#if INFINITE_GPL` blocks (node registration, body drawing, help text).
- Code outside this folder stays MIT (Infinite's licence) so it can go back upstream.
- GPL-2.0-only code cannot come in (incompatible with GPL-3.0). Non-commercial licences
  (CC BY-NC, Prosperity) never come in, in either build.
- Proprietary SDKs (NDI, DeckLink, NVIDIA Maxine) cannot link into a binary that contains
  this folder; they go through an out-of-process bridge.

| Module | Source | Licence |
|---|---|---|
| PunchFxNode | punch.c from SLOOP / Felucca, github.com/isod89/sloop-fm1 (Leo Kuroshita and contributors) | GPL-3.0-only |
| assets/drumkits-gpl (media) | SLOOP's 37 drum kits rendered to FLAC by its own engine (drums.c, drum_synth.c, tools/gen_drumkits.py) | GPL-3.0 material, full build only |
| Synth REF (`SynthRef/`: node, core loader, controller profiles, panel, adapters `cores/mvave_open_core.c` (SMK37 OPEN and FM1 OPEN) and `cores/sloop_smk/sloop_smk_core.c`) | Infinite-Turbo code that hosts instrument firmware cores (`docs/SYNTH-REF.md`) | GPL-3.0-only |
| SMK37 OPEN firmware (`SynthRef/cores/smk37_open/upstream/`, vendored unmodified) | M-VAVE, github.com/m-vave/smk37_open @ `b2d6bcbc` (`UPSTREAM.md`) | app, web engine, SDK headers: GPL-3.0-only WITH GPL-3.0-linking-exception; DX7 port of msfa and the 128 factory DX7 voices: Apache-2.0; Open303, DaisySP / Plaits, emu2149, SEA: MIT |
| FM1 OPEN firmware (`SynthRef/cores/fm1_open/upstream/`, vendored unmodified) | M-VAVE, github.com/m-vave/fm1_open @ `4c7496da` (`UPSTREAM.md`) | app, web engine, SDK headers: GPL-3.0-only WITH GPL-3.0-linking-exception; DX7 port of msfa and the 128 factory DX7 voices: Apache-2.0; Open303, DaisySP / Plaits, stmlib, emu2149, SEA: MIT |
| SLOOP SMK firmware (`SynthRef/cores/sloop_smk/upstream/`, vendored unmodified, plus six `generated/felucca_*.h` made by the firmware's own generators) | github.com/ChanceTheMaker/sloop-smk @ `08c82308` (branch integrate/sloop-2.6-smk; its `sloop/` is SLOOP 2.8 from github.com/isod89/sloop-fm1), Leo Kuroshita and contributors (`UPSTREAM.md`) | firmware: GPL-3.0-only; msfa FM6 core: Apache-2.0; DaisySP / Rings ports: MIT; Terminus font: OFL; samples: CC0. The all-rights-reserved Felucca artwork (icons.png, panel art, Hügelton pack) is not vendored |

Synth REF cores are built by a pinned llvm-mingw clang (20260922, fetched into the build tree
once, never shipped) into separate DLLs under `<exe>\synthref\cores`, and the host loads them
at run time through a C ABI (`SynthRefCoreApi.h`). The M-VAVE SDK binaries (`sdk/lib`,
`sdk/firmware`) are not used and not vendored: the firmware's open web engine replaces the
proprietary engine library. Vendoring scripts: `scripts/synthref_vendor_mvave.py` (SMK37 OPEN,
FM1 OPEN) and `scripts/synthref_vendor_sloop_smk.py` (SLOOP SMK).
