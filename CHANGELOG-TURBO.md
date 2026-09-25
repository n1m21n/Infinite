# Infinite-Turbo (for Windows) - changelog

## 0.37.0-turbo (2026-09-25)

### Menus
- **Categories reorganised** (node browser, search popup, spawn menu), from what each node does:
  Source (Text joined it), **Video** (Video, VMPC, Video In, Syphon/Spout In), Effects (Resynthesize
  joined), **Utility** (Comment, Group, Null, Viewport), **Audio In / Out** (Audio In, Audio Out,
  Audio File), **Audio to Visual** (Audio Texture, Audio Color Ramp, Audio Displacement, Audio
  Ribbon - the old 2-node "Audio"), Audio Mix & Routing, Audio Effects & Plugins, Synths & Samplers,
  Notes & Sequencers, **Prediction**, Modulators (generators), **CV Tools** (Math, Compare, Invert,
  Range to Range, Smoothing, Mod Depth, Null Modulator, CV to Pitch), **Analysis** (Audio/Image
  Analyze, Audio to CV, Note to CV, Palette), **MIDI & OSC** (MIDI CC/Trigger, OSC Receive/Send,
  OSC to CV). Node type names are unchanged; old patches load and re-file their nodes.

### Added
- **Range mapper** per modulation binding (right-click a modulated parameter): in min/max with
  "capture" from the live signal, out min/max in the parameter's units, invert, reset. Saved in the
  patch (trailing tokens of the `mod` line; older builds ignore them).
- **Color markers** for nodes (right-click > Color marker): coloured band + frame, 8 colours,
  applies to the whole selection, saved in the patch (6th token of `flags`).
- **OSC to CV** (MIDI & OSC): 8 channels, each an address + argument index, input range, invert,
  smoothing, and learn (arm, move the control, the next address is assigned). 8 CV outputs.
- **Predictive Notes / Quantize / Velocity / Rhythm**, ported from upstream Infinite
  (NoteModel, NoteTheory): learn from a note chain and play or correct in that style. Their shared
  cross-session learning pools are saved in `%LOCALAPPDATA%\Infinite\prediction`.

### Changed
- **Noise**: five 4D types in the style of TouchDesigner's Noise TOP - Simplex 4D (new default),
  Perlin 4D, Ridged 4D, Turbulence 4D, Billow 4D. The image is a slice of 4D noise and time moves
  along the 4th axis (speed), so it morphs in place instead of scrolling sideways. New controls:
  translate x/y, drift x/y (optional scroll), z, rotate, exponent; octaves, lacunarity, gain, warp,
  contrast, brightness, seed and rgb noise apply too. The original 2D types are unchanged.

### Fixes
- **VST3 plugins now see the host transport** (play/stop, tempo, time signature, bar and beat
  position). Tempo-synced and sequenced effects (Glitch 2, Effectrix, synced delays/LFOs) used to
  read "stopped, no tempo" and pass the audio through dry.
- **OSC**: the listener only bound 127.0.0.1, so controllers on the LAN (phone, tablet, another
  PC) never reached it; it now binds all interfaces. Two OSC nodes on the same port fought over the
  socket (the second got nothing); one shared listener per port now. Bundles and every numeric
  argument (f, i, d, h, T/F) are decoded. OSC Receive's low/high accept any range (0..127, -1..1),
  it can pick an argument, and it shows the raw value and the last address received.

## 0.36.0-turbo (2026-09-25)

### Fixes
- **Fullscreen editor flicker** with a fullscreen output window: the editor window is now one
  pixel taller than the monitor (so it is never promoted to exclusive-style presentation), and
  output windows only re-assert "topmost" when they actually lost it (it was every 0.5 s).
- **Looper recorded late**: takes are shifted by the interface round-trip latency (input +
  output latency reported by the driver + one buffer), overdub too, and a manual offset
  (-100..+300 ms) sits on top. The node shows the compensation in use.
- **Audio Analyze** only accepted an Audio File node. Its single input is now an "audio" pin
  that takes any audio cable (synth, VST, Audio In, mixer, Audio File) and wins over the live
  input; old patches with an Audio File load unchanged.

### New node
- **VMPC** (Source): the MPC for video clips, a VJ clip launcher. 16 pads with one video each, hit
  with the mouse, a CV pin per pad or the note input (base note 36). Per pad: one shot, gate
  (loops while held) or loop toggle, trim in/out, speed (negative = reverse). Output: the clip of
  the last pad hit (transparent when nothing plays, or hold the last frame). Decoders stay open
  per pad, so a hit only seeks. Silent (use a Video node for a soundtrack). Load video / load
  folder go through the non-blocking dialogs.

### Changed
- **Layout**: scale is anchored at the layer centre (x/y place the layer at scale 1); fit / fill
  also work with a custom size.
- **Super Mixer**: input gain knob per channel (+/-24 dB, pre-EQ, CV pin). It is the first knob
  row, so CV mappings made on the Super Mixer's knobs in 0.33-0.35 move by one row.
- **Projection**: transparent outside the warped image (option, on by default) and per-edge edge
  blend with width (0..1 of the image, default 0.30), S-curve and gamma, for overlapping
  projectors. Blend mode **alpha** (default: the edge fades out, for real blending in video
  mapping) or **black** (darkens RGB). **Antialiased outline** (on by default, 0.5-8 px): the warped
  border fades over a few pixels instead of a jagged triangle edge.
- Output windows composite the image's alpha over black, so transparent and alpha-blended edges
  show as a fade on the projector.

## 0.35.0-turbo (2026-09-25)

### Added
- **Canvas preview** (TouchDesigner-style): one node's output is drawn behind the node canvas,
  under nodes and cables. Ctrl+Shift+B toggles it for the selected node; also in the node's
  right-click menu (*Show behind the canvas*) and in the OUTPUT WINDOW section of Output /
  Projection / Layout params. **VIEW > Output behind the canvas**: scaling (fit / real pixels 1:1 /
  fill / stretch), darken veil, node card opacity, grid on/off. Geometry nodes go through Render 3D.
- **Fullscreen UI**: F11 in the editor switches the main window to borderless fullscreen on its
  current monitor (no display mode change) and back, restoring size and maximized state. Output
  windows keep their own F11.
- **VIEW menu**: side panel, fullscreen UI, canvas preview settings.

### Fixes
- **File dialogs no longer pause Infinite.** Open/save dialogs (samples, images, video, models,
  folders, patches, recording destination) ran on the render thread and froze the transport,
  sequencers, video and output windows until closed. They now run on their own thread; the result
  is applied at the start of a later frame (node-bound results are dropped if the node was deleted).
- **Dialogs open in front of the editor**, also in fullscreen: every dialog is owned by the main
  window (it used to open behind a fullscreen editor, reachable only with Alt+Tab). The editor
  ignores input while a dialog is up; playback and outputs keep running.
- Save in the "Unsaved Changes" prompt waits for the Save As dialog before continuing.

### Changed
- The top-bar SEARCH button is now **PANEL**, highlighted while the side panel is open; Ctrl+B
  toggles the panel.

## 0.34.0-turbo (2026-09-25)

### Fixes
- **Buffer underruns.** The render thread was registered with MMCSS "Games" at HIGH priority and
  competed with the JUCE audio thread; the call was removed (the audio thread keeps its own
  priority, FTZ/DAZ stay on).
- **Kontakt / Native Instruments not found.** The VST3 folder scan only accepted `.vst3` bundle
  folders; single-file `.vst3` plugins (how NI installs Kontakt and others) are now found too.
  Run `Rescan plugins` once.
- **MPC**: pad volume, pitch, pan and trim are drag fields, so double-click (or Ctrl+click) lets
  you type the value.

### Added
- **Build version in the app**: window title and FILE menu show the version and build date
  (`INFINITE_TURBO_VERSION` comes from `project(... VERSION)` in CMakeLists.txt).
- **MPC trim in/out** per pad: drag the handles on the waveform or type the start/end; saved in the
  patch; loading a new sample resets it.
- **OUTPUT WINDOW panel** in the parameters of Output, Projection and Layout: open, size in
  pixels (W/H + presets + source size), scaling mode, fullscreen, close. The right-click menu
  still has the same options.
- **Layout** node (Compositing): canvas of exact pixel size with 8 image inputs, each placed at its
  source's real pixel size (or a custom width/height), x/y in canvas pixels, scale, opacity,
  visibility; miniature editor (click to select, drag to move); 1:1 / fit / fill / centre;
  background colour + alpha; CV pins on x, y, scale and opacity of every layer.
- `GLUtil::DrawFullscreenQuad()` for nodes that composite into sub-rectangles.

### Build
- `build-windows.bat` now only compiles the test build (`build\windows-vs2022\Release`);
  `build-windows.bat pack` creates `dist\` and the ZIP.

## 0.33.0-turbo (2026-09-25)

### Fixes
- **VST instruments never received notes.** The topology builder calls the slot-aware
  `AudioNode::SetNoteInbox(slot, ...)` with the node's real note-pin slot, and the default only
  forwarded slot 0. The Plugin node keeps audio at slot 0 and notes at slot 1, so DecentSampler,
  Kontakt & co. got nothing (same for Spectral Synth and Wave Terrain). The default now forwards
  any slot (single-note-pin nodes only get one call); Note Merge still overrides it.
- **VST3 scan and Native Instruments plugins**: a plugin that describes itself and then crashes
  while unloading at exit is now accepted (the scanner also exits without running teardown);
  the scan timeout went from 30 s to 180 s and a timeout no longer blocklists the plugin. The
  blocklist file moved to `PluginVST3Blocklist-v4.txt`, so plugins wrongly blocked by R31a are
  retried. VST2-only plugins (`.dll`) are still not supported.
- Save dialog: default extension `.inf` (same format/extension as upstream Infinite),
  case-insensitive check.

### Output windows
- Background and letterbox bars are black (were grey).
- Node menu > Output window > **Size (pixels)**: exact drawable size (W/H + Apply, presets, source
  size), correct under Windows display scaling.
- Output window > **Scaling**: Fit (letterbox), **Real pixels 1:1** (centred), Fill (crop), Stretch.

### New nodes
- **MPC** (Synths): 16 sample pads, modes one shot / gate (hold) / loop (toggle), note input
  (base note 36), a CV pin per pad, load sample / load folder, per-pad volume/pitch/pan, master.
- **MPC Out** (AudioUtility): one pad's own stereo output from an MPC.
- **Looper** (AudioUtility): REC / PLAY / DUB / CLEAR (all with CV pins), length in bars,
  sub-bar or free, sync to the bar grid, forward / reverse / ping-pong, loop on/off, thru and level.
- **Super Mixer** (AudioUtility): 16 channels, fader, pan, mute, solo, 3-band EQ per channel,
  master fader; every control has a CV pin.
- Engine: `kAudioMaxNodeInputs` 8 -> 16; the PDC scratch buffer is heap-allocated once per audio
  thread instead of a 1-2 MB `thread_local` array reserved for every thread in the process.
- `INode::ResolveAudioTaps()`: called before each topology publish (used by MPC Out).

## 0.32.1-turbo (2026-09-24)

### Performance
- **Link table rebuild** (every frame): owner lookups go through maps built once per frame
  instead of scanning all nodes with a `dynamic_cast` for every connected input (was O(N^2)
  casts per frame; now O(N)).
- **Shader binary cache**: linked programs are stored in
  `%LOCALAPPDATA%\Infinite\shadercache\<driver hash>\` and reloaded with `glProgramBinary`,
  so opening a patch no longer recompiles every node shader. A driver update uses a new folder;
  a rejected binary falls back to compiling. Disable with `INFINITE_SHADER_CACHE=0`.
- **MMCSS**: the render/UI thread registers as a "Games" task (`avrt`), for steadier frame
  pacing under background load.

### Not changed
- Spout receiver: the first `ReceiveTexture()` call only connects/checks the sender and does not
  copy; removing it could break new-frame detection. Left as is.

### Tests on Windows
- New skill `.claude/skills/run-turbo-tests` (PowerShell driver) replaces the macOS hygiene
  driver: build + screenshot + the self-test suite, results in `build\test-results\`
  (`summary.txt`, one log per check, `screenshot.png`). Detects `[CRASH]` (with hex exit code),
  `[HANG]` (timeout kill) and `[EMPTY]`.
- `test-windows.bat` at the repo root: `-Quick`, `-Only A,B`, `-SkipBuild`, `-Config Debug`,
  `-ShotOnly`.
- `known-baseline.md` lists the checks that fail by design.
- The other skills now point to `test-windows.bat` / `/run-turbo-tests`; the macOS `.sh` drivers
  were removed.

## 0.32.0-turbo (2026-09-24)

First Infinite-Turbo cut, on top of the Windows R31a port. Branch `turbo/windows-only`.
Nothing here has been compiled with MSVC yet: the changed sources were syntax-checked with a
MinGW cross compiler; the first `build-windows.bat` run is the real test.

### Windows-only
- Removed every macOS path: `Platform.mm`, `PluginVST3.mm/.h`, `PluginHandleInternal.h`,
  `scanner_main.mm`, `AudioFileWriter.mm`, CoreText `TextNode.cpp`, `external/syphon`,
  the Steinberg `external/vst3sdk` submodule (JUCE hosts VST3 on Windows), plist /
  entitlements / .icns / iconsets, `package.sh` (DMG), `website/`, the GitHub Pages workflow,
  macOS crash reports and the macOS-only skills `ship-infinite` and `plugin-host-hardening`.
- Resolved all `#if defined(__APPLE__)` / `#if defined(_WIN32)` blocks to their Windows branch
  (14 files).
- `CMakeLists.txt` rewritten for MSVC only: `/MP`, Release `/Oi /Ot /Gy /GS-`, `/OPT:REF /OPT:ICF`,
  `/Zc:__cplusplus`, option `INFINITE_ENABLE_AVX2` (ON), option `INFINITE_ENABLE_LTCG` (OFF).

### Name
- Executable `Infinite-Turbo.exe`, window title `... - Infinite-Turbo`, version resource 0.32.0.
- Package `dist\Infinite-Turbo-Windows-x64(.zip)`.
- User data stays in `%LOCALAPPDATA%\Infinite` so settings, plugin index, blocklist and models
  from R31a keep working.

### Performance and correctness fixes
- **Audio: denormal protection was OFF on Windows.** `AudioEngine::Process` guarded FTZ/DAZ with
  `__x86_64__`, which MSVC never defines. Reverb/filter/delay tails decaying into denormals could
  cost 10-100x CPU. Now always set.
- **Video decoder** (`PlatformWindows.cpp`): read-ahead cache in the playback direction, `grab()`
  for small forward jumps instead of keyframe seeks, reverse playback decoded in batches (one seek
  per batch instead of one per frame), D3D11VA/DXVA hardware decode requested (disable with
  env `INFINITE_VIDEO_HWACCEL=0`), frame buffers swapped/recycled instead of copied, BGR->RGBA
  CPU conversion removed.
- **Video / camera upload**: BGR uploaded directly (GPU swizzle), texture storage allocated only on
  size change (`glTexSubImage2D` otherwise). Video node now reports a real `TextureRevision`, so a
  paused clip no longer forces every downstream filter to re-render.
- **Camera**: requests MJPG (30 fps at 720p/1080p on most USB webcams), 1-frame capture buffer,
  mirror + flip in a single pass.
- **Remove Background**: no more synchronous full-resolution `glReadPixels`. Source is copied on the
  GPU (paired with its mask), a downscaled copy (max 512 px) is read back through PBO + fence,
  and the worker always processes the newest frame (the old queue could process stale frames).
  GPU copies are recycled, bounded even when inference fails.
- **Uniform-location cache**: all ~340 `glGetUniformLocation` call sites are transparently served
  from a per-program cache (`OpenGLHeaders.h` / `GLUtil.cpp`); re-link and delete invalidate it.
- **Frame limiter**: high-resolution waitable timer (`Platform::PreciseSleep`) instead of
  `sleep_for` + spin; frees most of a CPU core at capped FPS.
- **VSync with output windows**: exactly one VSync wait per frame. With projector windows open the
  projector owns VSync (tear-free projection paced by its display); otherwise the editor does.
- **Node/link lookup**: `FindNodeByIndex` / `FindLink` use validated position hints (O(1) hits).
- **OpenGL context**: requests 4.6 core (falls back 4.5 > 4.3 > 3.3 > 3.2); logs renderer and
  version to `Infinite.log`. Shaders unchanged (`#version 150`).
- File names in titles/menus: `find_last_of('/')` -> `find_last_of("/\\")` (12 sites) so Windows
  paths show just the file name.

### Setup scripts
- `build-windows.bat`: args `debug`, `fresh`, `nopack`; configures only when needed (no forced
  `--fresh` every build), all cores (`/m` + `/MP`), refuses to build while the app is running,
  no submodule step.
- `install-dependencies.bat`: vcpkg binary cache in `%LOCALAPPDATA%\InfiniteBuild\vcpkg-binary-cache`
  (reinstall/second checkout reuses compiled OpenCV etc.), no submodule step.
- `run-windows.bat`: new exe name, Debug fallback, above-normal priority, pauses only on error.
- `install-runtime.bat`: reuses models bundled in the ZIP, one checksum routine.
- `diagnose-windows.bat`: new names, reads any blocklist version.
- `scripts/package-*.ps1` and CI workflow renamed and cleaned of macOS files.
