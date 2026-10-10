# Extensions: optional packs installed from Settings

Status: Phase 1 built on `feature/extensions` (2026-10-10: framework, Settings tab, streaming download, EXTENSIONSTEST). Phase 2 planned; gap closure added 2026-10-10. Origin: cables.gl vetting. The owner wants heavy features (body tracking, full web rendering) as installable plugins, not core bloat.
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

## Framework: what Phase 1 already does, and what is still open

Built (`src/core/Extensions.*`, `Platform::HttpDownload`): catalog parse, SHA-256 verify, zip install with path-escape rejection, atomic replace, remove, progress + Cancel, Install from file.

| Gap | Fix | When |
|---|---|---|
| Nothing published to install | Publish `extensions.json` + a tiny test pack on the release (a `ship-infinite` step); CI job builds the catalog from `packs/*/` | Before Phase 2 |
| Pack built for a newer app than the one installed | `pack.json` gets `min_app` and `abi` (integer, bumped when the loader contract changes). Install refuses a pack whose `abi` the app does not know, with a plain message | Phase 2 start |
| Catalog is only as trustworthy as its host | SHA-256 stops corruption, not a hijacked release. Accept for now (same trust root as the app download); revisit with a detached signature if packs ever ship executable code from a third party | Note only |
| Removing/updating a pack while a node is using it | `Extensions::Subscribe(id, onUnload)`: consumers stop their worker and release files before the folder is swapped; the swap waits for them | Phase 2 start |
| Disk full / low space | Check free space against `size x 2` before download; clear message | Phase 2 start |
| Windows SmartScreen / antivirus on downloaded DLLs | Files written by our own process carry no Mark-of-the-Web; test on a clean Windows VM and note any prompt | Phase 2 test |
| macOS quarantine on a downloaded dylib | `NSURLSession` downloads from our own process are not quarantined; verify with `xattr` and `dlopen` on a fresh Mac. Hardened-runtime risk stays as listed below | Phase 2 test |
| Translations | New Settings strings go in the six i18n tables | Before release |
| Packs listed that do not apply | Already filtered by `PlatformKey()`; add `arm64` vs `x64` macOS split only if ORT needs it | Phase 2 |

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

### Output behaviour (what makes it usable as an instrument)
- **Two hands** are tracked at once (`hand` dropdown: left, right, either, nearest). Face: up to 2 faces, a `face` index param. Pose: one person.
- Coordinates are normalised 0..1 across the image, y up, with a **Mirror** toggle (on by default for a webcam) and a **Range** mapping so a small gesture can span the full 0..1 output.
- **Present** output (0/1) per tracked thing, plus a **Hold** time so a one-frame dropout does not snap a knob to 0. On loss the outputs ease to a rest value instead of jumping.
- Smoothing: One-Euro filter with two knobs, `smooth` and `responsive`.
- Handedness comes from the geometry (not the camera mirror), so `left` stays the user's left.
- A **latency readout** (ms from camera frame to output) and an **fps readout** sit on the node body.

### Not only a live camera
- The node takes any image input (`Video In`, a video file, NDI, Syphon), not only a camera. That makes it testable in CI and usable on recorded footage.
- **Export determinism:** when the app is rendering a movie offline, the node runs synchronously, one inference per exported frame, so a re-render gives the same result. Live camera input cannot be re-rendered; the node says so. "Record to lane" (via the existing gesture/timeline recording path; verify what `GestureRecorder` can capture before promising it) is the way to keep a performance.

### Performance contract
- Inference never runs on the render or audio thread; a stall in the model cannot drop audio.
- Budget: hands >= 20 fps on CPU only at 256^2 input, on a 2019-class laptop; >= 30 fps with the GPU provider (Core ML on macOS, DirectML on Windows; Linux stays CPU unless CUDA is found).
- Frames are dropped, never queued: the worker always takes the newest frame.
### Latency (user experience first)
Fps is not the target; **camera-frame-to-output milliseconds** is. A hand that feels late is unusable as an instrument even at 60 fps.

| Stage | Typical cost | What we do |
|---|---|---|
| Camera exposure + driver buffering | 33-80 ms at 30 fps, often the largest | Ask for the highest fps and the lowest-latency format the device offers; drop OS frame queues; report the camera's own timestamp |
| Capture to our buffer | 1-5 ms | One shared buffer, newest frame wins, no queue |
| Preprocess (crop, resize, normalise) | 1-4 ms | Crop only the tracked region (ROI); avoid copies; do it on the GPU where the provider allows |
| Detect (palm/body/face) | the expensive step | **Skip it while tracking**: run only the landmark model on the ROI predicted from the last frame; re-detect only on loss. Roughly halves the cost |
| Landmark inference | 2-15 ms depending on provider | Smallest model that passes the fidelity golden; fixed input shape; session warmed up at install, not on first use; tuned ORT options (graph optimisation, thread count, I/O binding) |
| Smoothing | adds lag by design | One-Euro filter (low lag on fast moves, steady when still); defaults chosen from measurement, not guessed |
| Publish to modulators | < 1 ms | Lock-free handoff of the latest values; the render and audio threads only read |

- **Prediction:** an optional `lead ms` param extrapolates each output from its velocity to offset the pipeline lag. Default 0, off unless the user wants it; capped so it cannot overshoot wildly.
- **Budget:** processing (capture to publish) <= 30 ms with a GPU provider, <= 50 ms CPU-only, measured per OS. Camera latency is reported separately so users know what is theirs and what is ours.
- **Measurement:** every output carries the source frame's timestamp; the node shows `camera ms` and `process ms`. The spike also does one real glass-to-glass check per OS (a screen flash seen by the camera) to validate the timestamps.
- **Regression gate:** a latency number goes in `docs/plans/extensions/` per OS and provider; the tracking test fails if processing latency grows more than 20% from the recorded baseline.

- **Gate:** the model-conversion spike must report measured fps **and the latency table above** per OS and provider before any node UI is built. If CPU hands misses 20 fps, the fallback is a lighter palm/landmark model or a lower detection rate, not shipping slow.

### Test plan
| Test | How |
|---|---|
| Model fidelity | Converted ONNX vs MediaPipe reference output on a fixed image set; max landmark error under a stated pixel threshold; committed as a golden |
| Pipeline | `INFINITE_TRACKINGTEST`: still images in, expected landmarks out, no camera needed (the image-input path above) |
| Missing / broken pack | Node shows the Install hint, outputs read 0, no crash; corrupt model file gives a message, not a crash |
| Remove while running | Remove the pack with a node live: worker stops, node returns to the hint |
| Camera loss / switch | Unplug mid-run: "No camera", recovers on replug |
| Soak | 1 hour live, flat memory, no fps decay |
| Three OSes | `windows-parity`, `linux-parity`; Linux container rig for the CPU path |
| Patch portability | A patch saved with the pack opens without it, bindings intact |

### Risks
- macOS currently ships **ad-hoc signed** (`ship-infinite`), so loading a downloaded dylib works. If the app is ever notarized with hardened runtime, packs must be signed with the same Team ID (or ship ORT inside the app on mac).
- GPU provider availability varies: CPU fallback must stay >= 20 fps for hands at 256^2 input (see the gate above). Linux gets an optional CUDA/ROCm provider only if the user already has it; it is never part of the pack.
- Camera permission is already handled by `Video In`; the tracking node must not open a camera itself.

### Honest scope against TouchDesigner (what we will and will not match)
| | Decision |
|---|---|
| Same MediaPipe landmark quality, all three OSes, one-click install | **Match, and beat on cross-platform** |
| Speed on a strong GPU | **Camera-rate on Windows (DirectML, any GPU) and Apple Silicon (Core ML)**: these models are small and the camera is the limit. Lowest possible ms on RTX via TensorRT is **not a goal**. Linux without CUDA runs on CPU. Expected, not yet measured; the gate below decides what we claim |
| Kinect / depth cameras / Leap | **Out of scope.** Users bridge through OSC or NDI (both exist as nodes). Document the recipe in the node help |
| Multi-person pose | **Out of scope for v1** (MediaPipe pose is single-person). Revisit as its own pack only if asked |
| Segmentation | **Already covered** by Remove Background; no duplicate in this pack |
| Object detection / classification | **Not in this pack.** Candidate **Pack 3: Vision** (an ONNX detector such as a small YOLO) on the same framework, proposed only if wanted |
| Maturity | Closed only by the test plan above and real use |

## Pack 2: Web (HTML / CSS / JS rendered to a texture)

Phase **after** the JS Sketch node (`docs/plans/sketch/README.md`) proves people want code-drawn visuals.

- Engine: Chromium Embedded Framework (CEF, BSD) in off-screen mode → texture. Same approach as TouchDesigner's Web Render TOP.
- Size: ≈ 150-250 MB per OS, which is exactly why it's a pack.
- Node: `Web` source. Params become CSS variables (`--bass`) and a JS global, so modulation drives the page.
- Bonus: MediaPipe-web runs inside it for free, but the Tracking pack stays native for latency and no browser dependency.
- Known hard parts: CEF's helper sub-processes (macOS wants them inside the .app bundle; loading from outside the bundle needs care), GPU texture sharing per OS (start with CPU paint buffers), a strict sandbox (no file:// outside the patch folder, network off by default).

## Phases
1. **Done:** Extensions tab + catalog + streaming downloader + verify + loader (`feature/extensions`).
1b. Publish the catalog and a tiny test pack; framework items marked "Phase 2 start" above (abi/min_app, unload subscription, free-space check).
2. Tracking pack, each step gated:
   - 2a spike: macOS ORT packaging + model conversion + **measured fps/latency per OS** (gate: hands >= 20 fps CPU).
   - 2b `INFINITE_TRACKINGTEST` with still images and the fidelity golden.
   - 2c Hand Track node (two hands, outputs, mirror, hold, smoothing, readouts).
   - 2d Pose Track, then Face Track.
   - 2e export determinism and record-to-lane.
3. Web pack (CEF), gated on Sketch adoption.
4. Optional, only on request: Vision pack (detection), multi-person pose.
