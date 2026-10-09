# Extensions: optional packs installed from Settings

Status: planned (2026-10-08). Origin: cables.gl vetting. The owner wants heavy features (body tracking, full web rendering) as installable plugins, not core bloat.
Skills to load before building: `windows-parity`, `linux-parity`, `plugin-host-hardening` (loader hygiene), `ship-infinite`, `prior-art-scout` (pack hosting/updates).

## The model

```
Infinite core (always)            Extension pack (optional download)
──────────────────────            ─────────────────────────────────
nodes are registered              runtime libs + model/data files
node body shows "Install pack"    versioned manifest + SHA-256
loader finds pack at run time  ◄─ installed to AppPaths user-data/extensions/<id>/
```

The nodes always exist (patches open everywhere, nothing shows "unknown node"). Without the pack the node body shows a one-line hint plus an **Install** button. This is the existing NDI pattern: `src/platform/ndi/NdiRuntime.h` loads the NDI library at run time and `Ndi::StatusLine()` shows the install hint, so Infinite builds and runs with NDI absent.

Industry precedent: Blender 4.2+ "Get Extensions" in Preferences, Ableton Packs, VS Code extensions, TouchDesigner's NVIDIA Body Track (needs a separate NVIDIA SDK download).

## Settings → Extensions tab

New tab in `src/app/panels/SettingsWindow.cpp` (beside Appearance / Audio / Field Language):

| Column | Content |
|---|---|
| Pack | name, one-line purpose, size |
| Status | Not installed / Installed vX / Update available |
| Action | Install / Remove / Update |

- Manifest fetched from the GitHub release (same host as `src/core/UpdateCheck.cpp`). One JSON with per-OS/arch URLs + SHA-256.
- Download happens on a worker thread with a progress bar; files are verified before unpacking; the pack becomes active without a restart where the loader allows it.
- Offline install: "Install from file…" for studios without internet.

## Pack 1: Tracking (hand / pose / face → modulators)

**Engine decision: MediaPipe's models running in ONNX Runtime on all three OSes ("option C").**

| Question | Answer |
|---|---|
| Industry standard? | MediaPipe's models are the de facto open standard for real-time landmarks (TouchDesigner's community plugin, Notch/Unreal users, web creative coding). ONNX Runtime is the standard cross-platform inference engine. TouchDesigner's own built-in body tracker is NVIDIA-only (Windows + RTX). Ours would run on all three OSes |
| Why not Apple Vision on mac? | Remove Background already does that (Vision on mac, `u2netp.onnx` on Windows/Linux), and it's fine for a mask. Landmarks are different: Vision gives 19 body joints and 76 face points, MediaPipe gives 33 and 478, so a patch built on a Mac would break on Windows. Hands match (21 joints both); everything else doesn't |
| Bloat? | **Core: 0 MB.** Pack: models ≈ 15-20 MB (palm detect + hand landmark, pose detect + landmark "full", face detect + mesh). ONNX Runtime is **already shipped** on Windows (+DirectML) and Linux for Remove Background, so those packs are models only. macOS needs the ORT dylib (≈ 30 MB, Core ML provider) in the pack |
| Separate install? | Yes: Settings → Extensions → Tracking → Install. One click, no third-party installer, no Python |
| Licences | MediaPipe models Apache-2.0 (ship NOTICE like `assets/models/NOTICE.txt`), ONNX Runtime MIT. Both compatible with MIT |

### Prerequisites (in build order)
1. **Extensions framework** above (manifest, download, verify, AppPaths location, loader).
2. **macOS ONNX Runtime** packaging: the universal dylib with the Core ML provider (Win/Linux already done in `CMakeLists.txt:123-190`).
3. **Model conversion, offline, once**: MediaPipe `.tflite` → ONNX with tf2onnx, checked against MediaPipe's own outputs on a fixed set of test images. Committed as pack artefacts, never at user runtime.
4. **Pipeline glue** (the real work): palm/pose detector anchor decoding, rotated ROI crop, landmark model, landmark smoothing (One-Euro filter), tracking-skip-detection when confident. ≈ the part MediaPipe's graph normally does.
5. **Camera frames**: reuse `Video In` (camera exists on all three OSes: `Platform.mm`, `MediaWin.cpp`, `CameraLinux.cpp`). Inference runs on a worker thread at ≤ 30 fps, never the render or audio thread.
6. **Output shape**: one `Hand Track` / `Pose Track` / `Face Track` modulator node with **named outputs** (e.g. `index tip x/y/z`, `pinch`, `mouth open`, `head yaw`) chosen from a dropdown list rather than 21×3 raw pins, plus an optional landmarks overlay image. Uses multi-output modulator rules from `new-modulator-node`.

### Risks
- macOS currently ships **ad-hoc signed** (`ship-infinite`), so loading a downloaded dylib works. If the app is ever notarized with hardened runtime, packs must be signed with the same Team ID (or ship ORT inside the app on mac).
- GPU provider availability varies: CPU fallback must stay ≥ 20 fps for hands at 256² input.

## Pack 2: Web (HTML / CSS / JS rendered to a texture)

Phase **after** the JS Sketch node (`docs/plans/sketch/README.md`) proves people want code-drawn visuals.

- Engine: Chromium Embedded Framework (CEF, BSD) in off-screen mode → texture. Same approach as TouchDesigner's Web Render TOP.
- Size: ≈ 150-250 MB per OS, which is exactly why it's a pack.
- Node: `Web` source. Params become CSS variables (`--bass`) and a JS global, so modulation drives the page.
- Bonus: MediaPipe-web runs inside it for free, but the Tracking pack stays native for latency and no browser dependency.
- Known hard parts: CEF's helper sub-processes (macOS wants them inside the .app bundle; loading from outside the bundle needs care), GPU texture sharing per OS (start with CPU paint buffers), a strict sandbox (no file:// outside the patch folder, network off by default).

## Phases
1. Extensions tab + manifest + downloader + loader (ship with a tiny test pack).
2. Tracking pack: hands first (highest value, smallest models), then pose, then face.
3. Web pack (CEF), gated on Sketch adoption.
