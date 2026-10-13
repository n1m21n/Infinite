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
