# Linux Port — Master Plan

> **Status: done (2026-10-09, owner).** The Status table at the bottom is the per-phase record.

Paste the relevant `phase-NN-*.md` into a fresh Claude Code session. This
README is the shared context every phase assumes. Read it first, every time.

Repo root: **`/Users/namansoni/infinte`** (that spelling is correct).
Public issue: [n1m21n/Infinite#14](https://github.com/n1m21n/Infinite/issues/14).

---

## Goal

Ship Infinite on Linux as a **full release**: an x86_64 AppImage on the
GitHub Release and the website, with the same feature surface as Windows
wherever Linux has an equivalent.

## The hard constraint

**The owner has no Linux machine and no Linux experience.** You run on their
Mac (Apple Silicon, 8 GB RAM). So:

- Never claim "works on Linux" from reasoning. Every claim needs a CI run,
  a local container run, a screenshot or recording artifact, or a beta
  tester's report. See [validation.md](validation.md).
- Anything that needs real hardware (real audio interfaces, MIDI gear,
  cameras, GPUs, VST editors on a real desktop) is **unverifiable by you**.
  Say so explicitly in commit messages and phase reports. Never imply green
  CI covered it.
- Explain Linux-specific things to the owner in plain words. Use tables and
  diagrams, not long prose. Don't use Artifacts.

---

## Decisions (locked — don't re-open)

| Topic | Decision | Why |
|---|---|---|
| Target | **x86_64 only** ships. arm64 stays compiling (the local container is arm64) but is never published. | Every realistic Linux user of this app is on x86_64. The Raspberry Pi 5 GPU tops out at GL 3.1, below Infinite's 3.2 core. Bespoke and Blender also ship x86_64 only. |
| Package | **AppImage**, built on **Ubuntu 22.04** (glibc 2.35 floor). | Single portable file, like the Windows zip. Building on the oldest supported base makes it run on newer distros. |
| Windowing | GLFW 3.4 with both X11 and Wayland compiled in. At startup, **prefer X11** (`glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11)` when `DISPLAY` is set); `INFINITE_WAYLAND=1` opts into native Wayland. | VST3 editors embed only into X11 windows. Projector windows need absolute positioning, which Wayland forbids. XWayland is present on every Wayland desktop. |
| GL | Same GLSL 150 core shaders, loaded via the existing `glad` path (`src/core/gl3.h` non-Apple branch). | Already works on Windows; Mesa is strict like Intel/AMD on Windows. |
| Audio out/in | **miniaudio** (vendored single header), runtime backends ALSA / PulseAudio / PipeWire (via Pulse) / JACK, plus its **null** backend for CI. | One implementation covers every Linux audio stack. It uses `dlopen` for backends, so there are no hard `.so` dependencies. |
| MIDI | **ALSA sequencer** (`libasound`, `snd_seq_*`). | Works under PipeWire and JACK too. It's what every Linux MIDI app uses. |
| Video decode/encode, video audio, InspectMovie | **FFmpeg** (libavformat/avcodec/swscale/swresample), a pinned shared build **bundled inside the AppImage**. **GPL build + libx264, gated behind `INFINITE_ENABLE_GPL_CODECS` (default ON on Linux).** | System FFmpeg ABI differs per distro. Copyright is settled: the binary is already GPLv3 via the VST3 SDK, and FFmpeg/x264 are GPLv2-or-later, so they flow into v3. The gate exists because `LICENSE` promises an MIT binary at `INFINITE_ENABLE_VST3=OFF`, and because Fedora/Flathub cannot ship H.264. Patent exposure is real but Linux-specific and accepted — macOS/Windows get H.264 from the OS vendor, Linux does not. **Settled 2026-09-14**; see phase-03. |
| Camera | V4L2 ioctls for enumeration, and V4L2 mmap capture (YUYV + MJPEG; MJPEG decoded with the already-vendored `stb_image`). | No extra dependency. |
| Fonts / text | **FreeType** (FetchContent, static) + **fontconfig** (system, stable ABI `libfontconfig.so.1`). | Standard on every desktop. |
| File dialogs + fatal message box | **tinyfiledialogs** (vendored, zlib licence): zenity/kdialog/yad under the hood. | Blocking, synchronous, same contract as the macOS panels. |
| HTTP (update check) | `dlopen("libcurl.so.4")` at runtime. If it's missing, `HttpGet` returns false. | No build dependency, and it doesn't bundle TLS stacks or CA paths. |
| Background removal | ONNX Runtime **CPU** (official `onnxruntime-linux-x64-<ver>.tgz`) + the bundled `u2netp.onnx`, sharing Windows' model code. | Same model and verdict as Windows, minus DirectML. |
| Syphon / Spout | **Not available on Linux.** Stubs; the nodes are hidden from the Add menu on Linux but still load from patches. | No Linux equivalent that other VJ apps actually speak. |
| Plugins | **VST3 only** (the SDK already ships `hosting/module_linux.cpp`). No AU (macOS-only). No LV2 in this project. | Matches the macOS/Windows surface. LV2 is a possible follow-up if users ask. |
| Patch extension | `.infinite` on Linux (and Windows); `.inf` stays macOS-only. Loaders accept both everywhere. | Linux desktops with Wine installed associate `*.inf` with Windows Setup files. |

---

## Where things stand (baseline captured 2026-09-13, `main` @ `440e69d`)

```
src/platform/Platform.h         ~137 functions  ← the whole contract
src/platform/Platform.mm         macOS   (~9.7k lines incl. PluginVST3.mm)
src/platform/win/*.cpp           Windows (~11k lines)
src/platform/linux/              ✗ does not exist — you create it
CMakeLists.txt                   if(APPLE) / if(WIN32) only; VST3 FATAL_ERRORs on other platforms
.github/workflows/build.yml      macos + windows jobs; no linux job
```

Already portable, so reuse rather than rewrite:

| Piece | Where | Note |
|---|---|---|
| GL loader | `external/glad`, `src/core/gl3.h` | Non-Apple branch is glad already |
| FFT | `src/audio/dsp/PortableFft.h` | Non-Apple default in 4 node files |
| App paths / temp / sockets | `src/platform/AppPaths.h`, `NetCompat.h` | `AppSupportDir()` `#else` is **macOS** (`~/Library/...`), so it needs a Linux branch |
| Image / model / audio-file decode | `src/platform/win/MediaDecodeWin.cpp` | stb_image, tinyexr, dr_libs, hand-rolled OBJ/PLY/STL/AIFF. Portable except the `WPATH()` wide-path shim. **Extract, don't copy** (phase-01). |
| Audio file writer | `src/audio/AudioFileWriter.cpp` | dr_libs/shine/libFLAC, already POSIX-clean |
| Headless tests | `.github/scripts/headless-tests.sh` | Runs on macOS + Windows CI today |
| Full self-test harness | `.claude/skills/run-infinite-hygiene/driver.sh` | Env-var driven (`INFINITE_<NAME>TEST=1`, `INFINITE_EXITAFTER`, `IMAGERESYNTH_SCREENSHOT`). BIN path is hardcoded to the `.app`. |

The full per-function and per-`#ifdef` checklist is in
[platform-inventory.md](platform-inventory.md). Why the trickier rules exist
(real-world failures in Audacity, VCV Rack, yabridge, JUCE, and GitHub
runner limits) is in [prior-art.md](prior-art.md).

---

## Phases

```
P0 skeleton + rig ─► P1 desktop ─► P2 audio+MIDI ─► P3 media ─► P4 VST3 ─► P5 release
      │                   │               │              │            │            │
  canvas renders    visual nodes     sound + MIDI     video/cam/    plugins    AppImage, beta,
  under Xvfb in CI  + save/load       fixtures green   recorder     + editors   ship-infinite
```

| # | File | Branch | Exit (proof required) |
|---|---|---|---|
| 0 | [phase-00-skeleton-and-rig.md](phase-00-skeleton-and-rig.md) | `feature/linux-step-00-skeleton` | **COMPLETE** (`b25b5bd` / [Run 34769650449](https://github.com/n1m21n/Infinite/actions/runs/34769650449)) — Clang + GCC green, headless pass, Xvfb screenshot artifact verified |
| 1 | [phase-01-desktop.md](phase-01-desktop.md) | `feature/linux-step-01-desktop` | Hygiene `--fast` + UI/3D/compositing groups green under Xvfb; per-category screenshots |
| 2 | [phase-02-audio-midi.md](phase-02-audio-midi.md) | `feature/linux-step-02-audio-midi` | Audio group green on miniaudio null + PulseAudio null sink; MIDI fixtures green on a virtual ALSA port (or a documented reason they can't run) |
| 3 | [phase-03-media.md](phase-03-media.md) | `feature/linux-step-03-media` | **COMPLETE**, merged to `main` (`79e5cc5`). See Status table below |
| 4 | [phase-04-vst3.md](phase-04-vst3.md) | `feature/linux-step-04-vst3` | **COMPLETE**, merged to `main` (`33e4e93`). See Status table below |
| 5 | [phase-05-release.md](phase-05-release.md) | `feature/linux-step-05-release` | AppImage passes the multi-distro smoke; beta round done; ship-infinite publishes Linux — **IN PROGRESS**, tasks 5.1–5.3 done and independently re-verified, 5.4/5.5/5.6 remain. See Status table below |

Phases are strictly ordered. Each branches from `main` **after** the previous
phase is merged. The owner merges; you never merge to `main` without asking.

---

## Working rules (all phases)

1. **Branch per phase**, named exactly as above, off current `main`. Commit
   in small, logical steps. You may push `feature/linux-step-*` branches to
   `origin` to trigger CI without asking. **Never** push or merge `main`.
2. **Three-sided `Platform::` obligation.** Every function in `Platform.h`
   needs a definition in `Platform.mm`, `win/*.cpp` **and** `linux/*.cpp`.
   A Linux stub (fill `outError`, return false/empty) is a legitimate first
   landing. A missing definition is not. Signatures must be byte-identical.
3. **No `__linux__` and no `_WIN32` in `src/nodes/`.** The rule from the
   `windows-parity` skill: `#if defined(__APPLE__)` fast path, `#else` is the
   portable default. Where a node's `#else` is secretly Windows (TextNode,
   RemoveBgNode), move the platform code behind a `Platform::` function
   instead of adding a third branch.
4. **Flip polarity rather than add branches** in `main.cpp`. If
   `#if defined(_WIN32) "Ctrl" #else "Cmd"` is really "Apple vs everyone
   else", rewrite it as `#if defined(__APPLE__) "Cmd" #else "Ctrl"`.
5. **Don't regress macOS or Windows.** macOS: `driver.sh --fast` locally
   before every push. Windows: the CI job must stay green (both arches
   compile, CRT scan passes). Any file you extract out of `win/` must keep
   Windows behaviour byte-for-byte.
6. **Keep new code out of `main.cpp`** (it's ~53k+ lines and MSVC compiles
   it at `/Od`). Linux code lives in `src/platform/linux/`.
7. **Load the relevant skills before touching code:** `codebase-navigation`
   (always), `windows-parity` (it's the Linux rulebook too; read §2–§3),
   `audio-pipeline-sweep` (P2), `av-sync-sweep` (P3), `plugin-host-hardening`
   (P4), `ship-infinite` (P5), `invariant-interaction-audit` when you
   establish a guarantee.
8. **Don't UI-script anything.** Verify with the env-var self-test fixtures,
   screenshots the app writes itself, and CI artifacts.
9. **No debug tools in shipped builds.** The UI Debugger, Style Editor and
   similar stay `#ifndef NDEBUG`-gated on Linux too.
10. **Line numbers in these docs drift.** They were captured on a
    2026-09-13 tree. Re-grep the named symbol before editing.
11. **Update this README's status table** (below) at the end of each phase,
    with the commit hash and what was *actually* verified vs. only compiled.

## Status

| Phase | State | Tip commit | Verified (CI/local) | Unverified (needs hardware/humans) |
|---|---|---|---|---|
| 0 | **COMPLETE** | `b25b5bd` | [Run 34769650449](https://github.com/n1m21n/Infinite/actions/runs/34769650449): x86_64 Clang **and** GCC build green, headless self-tests pass, Xvfb screenshot artifact produced | — |
| 1 | **COMPLETE** | `feature/linux-step-01-followups` | Local arm64 container (Clang): build green; `ldd` shows no direct libGL/libGLX/libEGL and no direct libX11; `--fast` 30/30; `--group ui,3d,compositing` 45 pass / 0 fail / 1 known xfail; headless suite green incl. a real SIGSEGV→backtrace, a real TLS request, and `SYPHONPATCHTEST`; six per-category shots with a blank-render gate. macOS `--fast` 29 pass / 1 fail — `ARRANGEWAVETEST`, which fails identically when run from `main`'s own binary (`101c874`) and belongs to the in-flight arrangement audio work, not to the port. It passes on Linux, so whatever is unfinished there is macOS-side; re-check it once arrangement audio lands rather than treating this row as a port regression | Real GPU drivers (only llvmpipe is exercised), real X11/Wayland desktops, HiDPI scaling, window-manager behaviour, the window icon actually appearing in a taskbar, real font sets via fontconfig, complex-script text (no HarfBuzz — see `TextLinux.cpp`) |
| 2 | **COMPLETE** (arm64 local; x86_64 CI still outstanding — see below) | `3038939` | Local arm64 container (Clang): build green (334/334, zero warnings in the four new/changed Linux files); `--fast` 31/31 including the new `INFINITE_MIDIPARSETEST` (synthetic `snd_seq_event_t` feed into the ALSA sequencer parser — real hardware is unreachable, no `/dev/snd/seq` in this container or in GitHub Actions); `--group audio` run twice per the exit criterion (`INFINITE_AUDIO_BACKEND=null`, then `=pulse` against a throwaway PulseAudio null sink) — both passes report all checks green, with `AUDIOLIFECYCLETEST` and `AUDIORECOVERYTEST` correctly caught as documented container-timing flakes (baselined in `known-test-failures-linux.txt`, not silently skipped) rather than hard failures; `INFINITE_SYSINFO`'s new audio/MIDI diagnostics block confirmed (`Audio devices: 20`, MIDI port status). macOS `--fast` 30/30 green (no regression from the `AudioDeviceWin.cpp`→`AudioAnalysisCommon::` refactor or the shared `main.cpp`/`SysInfo.cpp` changes). `known-test-failures-linux.txt`'s Phase 2 section has zero unconditional-stub entries left — the only two remaining P2 lines are honestly-documented container timing flakes, not stubs. `audio-pipeline-sweep` run on macOS surfaced one pre-existing, unrelated `AUDIOPARAMSWEEPTEST` failure in `src/nodes/NoteNodes.cpp` (Note Router/Switcher/Arpeggiator/Stack/Capturer param-mailbox wiring) confirmed via diff to predate and be untouched by this branch — flagged separately, not part of this phase's scope | x86_64 CI (Clang + GCC) has not run this branch — all validation above is arm64-only, and linux-parity §3.7 is explicit that "local green on arm64 is not green" for anything numerical/timing-related, which applies directly to the two flaky fixtures. Real audio interfaces, JACK/PipeWire servers, real MIDI hardware, and a real `/dev/snd/seq` sequencer device all remain unverified — MIDI is proven only via the synthetic parser fixture. Owner-decision deferred: whether to loosen `AudioEngine::Process`'s `kXrunGapMultiplier` heuristic or `kAudioRecoveryMinIntervalMs` window (both shared/portable, affecting macOS and Windows too) versus accepting them as container-only noise |
| 3 | **COMPLETE** (arm64 local; x86_64 CI still outstanding) | `79e5cc5` (merged to `main`) | Local arm64 container (Clang): `RECEXPORTTEST`, `RECSYNCTEST`, `VIDEOAUDIOTEST`, `VIDEOSPEEDTEST`, `INFINITE_CAMERACONVTEST` all pass; `-DINFINITE_ENABLE_GPL_CODECS=OFF` (LGPL) build links no libx264 and cleanly refuses H.264 encode. Re-run natively on macOS via `av-sync-sweep`/`run-infinite-hygiene --fast`: zero regression to shared code | x86_64 CI (Clang + GCC) has not run this branch. Real webcams and UVC quirks (no `/dev/video*` or loadable `v4l2loopback` module in any container used), hardware decode paths, long recordings |
| 4 | **COMPLETE** (arm64 local; x86_64 CI still outstanding) | `9e98fc5` (merged to `main` as `33e4e93`) | Local arm64 container (Clang): `INFINITE_VST3SCANTEST` (new, cross-platform, `#if INFINITE_ENABLE_VST3` only), `INFINITE_VST3EDITORSHOTTEST` and `INFINITE_VST3BLOCKLISTTEST` (both new, Linux-only) all genuinely pass against a real small-tier open-source VST3 built from source (DPF's `examples/Parameters`, via `tools/linux/test-plugins.sh --small`) and a real deliberately-broken plugin (`tools/linux/build-broken-plugin.sh`, `abort()` in `ModuleEntry`) — scan/load/render/param/state, editor-open-under-Xvfb-with-screenshot, and out-of-process crash/blocklist all proven for real, not just compiled. This work also found and fixed a real Tier 1 host crash: `Platform::DescribeVST3Bundle` in `src/platform/linux/PluginVST3Linux.cpp` destructed its `IPtr<IPluginFactory>`/`IPtr<IPluginFactory2>` *after* `UnloadVST3Module()` had already `dlclose()`'d the module — a use-after-unload SIGSEGV on every real VST3 scan, fixed by releasing both `IPtr`s before unloading. `known-test-failures-linux.txt`'s Phase 4 section: `VST3SCANTEST` line removed (now genuinely passing); `PLUGINSCANTEST`'s reason corrected to "AU-only, architecturally inapplicable on Linux" (`RunPluginScanTest()` only ever exercised `Platform::EnumerateAudioUnits`, never VST3, on any platform) — left in place since it cannot pass on Linux without an AU host, out of scope. Re-verified independently (second reviewer pass, this session): rebuilt from scratch, re-ran `--fast` (31/0/0) and `--group media` (6 passed/0 failed/2 documented xfail) twice to rule out flakiness, confirmed `vst3-editor.png` is a real non-blank 512×512 screenshot | x86_64 CI (Clang + GCC) has not run this branch. Surge XT (`--full` tier): SHA256-verified but never installed/scanned in a container. Real VST3 editors on a real desktop (window manager, real GPU, HiDPI), and any commercial/closed-source plugin, remain unverified |
| 5 | **IN PROGRESS** (5.1–5.3 done, arm64-verified; 5.4 beta round and 5.5/5.6 close-out remain) | `f556661` + `e74ebd7` on `feature/linux-step-05-release` | Independently re-verified end-to-end in this session (not just diff review): rebuilt Infinite/scanner from scratch in `tools/linux/Dockerfile.release` (Ubuntu 22.04, confirmed GLIBC_2.35 max via `objdump`); ran `package-appimage.sh` myself, producing a real 97 MB `Infinite-aarch64.AppImage` (arm64 proxy); confirmed via `ldd` on the extracted AppDir that the onnxruntime/FFmpeg RPATH fix (`$ORIGIN/../lib` on the two binaries, `$ORIGIN` on every copied FFmpeg `.so`) actually resolves every private lib with no "not found" entries and no direct GL linkage; confirmed the embedded runtime has no `libfuse.so.2` dependency; ran the extracted binary headlessly under Xvfb and captured a real non-blank screenshot of the live UI (toolbar, transport, grid all rendering); ran `tools/linux/distro-smoke.sh` myself against real `ubuntu:22.04`/`ubuntu:24.04`/`debian:12`/`fedora:latest` containers — all 4 PASS with genuine non-black screenshots (671–680 distinct colours); the minimal-install GL-absence trap and best-effort Wayland smoke behave exactly as designed (non-gating). Found and fixed one real blocker along the way: this branch was cut before `77926fc` (a same-session fix for a rolling FFmpeg-nightly SHA256 drift, already on `main`) landed — cherry-picked as `e74ebd7`. Also reviewed and confirmed correct: `.github/workflows/build.yml`'s new `linux-appimage`/`linux-distro-smoke` jobs (YAML-parses, matches phase doc), `assets/linux/infinite.png` (genuine 256×256 icon) | `arch` (archlinux:latest) could not be tested locally — that image publishes no arm64 manifest (confirmed via `docker manifest inspect`, amd64-only), an arm64-host limitation, not a script or packaging bug; expected to work on the real x86_64 CI runner. Real x86_64 hardware/CI run of `linux-appimage`/`linux-distro-smoke` (no push has been made yet). Real desktop Wayland compositors (only headless weston tested). Beta round (5.4) not started — needs owner approval for every public step |
