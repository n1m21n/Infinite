# Implementation prompt — Motion Track node

> Paste this whole file as the first message of a fresh Claude Code session on Infinite.

## 0. Before writing any code

Load these skills, in this order:

1. `git-branch-workflow` — this work goes on `feature/motion-track-node`, branched off `main`.
2. `codebase-navigation` — you must find *every* hand-maintained registration site, not the first grep hit.
3. `new-effect-node` — this node is an image-in/image-out node and follows that family's wiring.
4. `new-modulator-node` — it is *also* an `IModulator` with multiple outputs; both apply.
5. `node-ui-pillars` — non-negotiable for the node body.
6. `windows-parity` — the analysis pass decodes video off the render thread; macOS and Windows decoders differ.

Read first, before designing anything:

- `src/nodes/RemoveBgNode.h` / `.cpp` — the worker-thread + latest-only-request + `mResultReady` pattern, and `Platform::SubjectMask`.
- `src/nodes/PathNode.h` — the house answer to "how does a node move an object": **modulator outputs, not a transform cable.**
- `src/nodes/AnalyzeNodes.h` (`ImageAnalyzeNode`) — an image node that emits `centroidX` / `centroidY` / `motion` as modulator outputs. Closest existing sibling.
- `src/nodes/DrawNode.h` (`VisitParams` + `EncodeRecording`) — how a long per-frame array is persisted through the 5-type `ParamVisitor`, and the undo-bloat warning written on it.
- `src/platform/Platform.h` lines ~138-170 — `VideoOpen` / `VideoFrameAt` / `VideoDecodeIsCatchingUp`. The header already anticipates "an offline analysis pass".

## 1. What to build

A **Motion Track** node (Analyze category, next to Image Analyze).

```
   video ──▶┌──────────────────────┐──▶ image  (source + tracking overlay)
            │     Motion Track     │──▶ x        (0..1)
            │  [ Analyze ] 62% ▓▓  │──▶ y        (0..1)
            └──────────────────────┘──▶ scale    (0..1, 1.0 == size at init)
                                     ──▶ rotation (0..1 across -180..180)
                                     ──▶ confidence (0..1)
```

- **One image input**, one image output, and **five modulator outputs**.
- **One object, the dominant one.** No multi-track, no track list, no per-track UI.
- **Offline analysis**, triggered by a button — not a per-frame tracker. It walks the whole clip once on a worker thread and stores a track table; playback is then a table lookup, deterministic and scrub-safe.

### Deliberate deviation from the original idea — read this before you start

The request described a second **"object" input cable** that the node would move. **Do not build that.** `PathNode` is the precedent and its class comment states the reason explicitly: it emits position as ordinary modulator outputs "rather than inventing a transform cable, which means it patches into any parameter at all". Same here — dragging `x`/`y` onto a Shape's `posX`/`posY` is one gesture, works with 2D *and* 3D *and* non-spatial params (a blur radius, a filter cutoff), and needs zero new cable machinery. Keep the visual affordance the request actually wanted — the **overlay ring on the video** — that part stays.

## 2. The algorithm

No OpenCV in this repo (`external/` has no CV library) and none is to be added. Everything below is hand-written C++ over CPU RGBA8 frames. This is the same hybrid design Blender's tracker uses (SAD for a coarse pass, KLT for refinement).

### 2a. Init — find "the major object" with no user input

```
frame at analyseStart
      │
      ├─ Platform::SubjectMask(pixels, w, h, MattingMode::Subject, mask)   ← already exists, both OSes
      │        │
      │        ├─ ok  ─▶ largest 8-connected component of mask ─▶ its bbox ─▶ INIT BOX
      │        └─ fail ─▶ fallback: 3-frame temporal difference, threshold, largest blob ─▶ INIT BOX
      │
      └─ manual override: user drags a box on the node's preview ─▶ INIT BOX
```

`Platform::SubjectMask` is the single biggest reason this is buildable without a CV dependency — it is on-device Vision on macOS and a bundled ONNX model on Windows, and "subject" is exactly "the major object". Use it for **init only**, never per-frame (see `RemoveBgNode`'s comment on why it is far too slow for video rates).

### 2b. Per-frame tracking

Per analysed frame, in order:

| Stage | What | Why |
|---|---|---|
| 1. Predict | box centre += last velocity | keeps the search window small under fast motion |
| 2. Coarse | NCC (normalized cross-correlation) of the template over a `searchScale`×box search window, on a 1/4-res gray pyramid level | Blender's "SAD" pass; brute force, cannot get lost |
| 3. Refine | pyramidal Lucas–Kanade on up to `featureCount` Shi-Tomasi corners inside the box, 3 pyramid levels, 4 iterations | Blender's KLT pass; sub-pixel accuracy |
| 4. Fit | median-based similarity fit (tx, ty, scale, rotation) over surviving corners, discarding outliers > 2× median residual | one bad corner must not drag the box |
| 5. Score | `confidence` = peak NCC of the fitted box against the template | the number the UI shows and the `confidence` output emits |
| 6. Adapt | `template = lerp(template, currentPatch, adapt)` only if `confidence > minConfidence` | template drift is the #1 tracker failure mode; gating on confidence is the standard guard |
| 7. Lost | `confidence < minConfidence` → mark frame `lost`, coast on predicted velocity, widen the search window by 1.5× for the next N frames | a brief occlusion should not end the track |

Motion model is a param (`Position` / `Position + Scale` / `Position + Scale + Rotation`) — stages 4-5 solve only the enabled components. Default `Position + Scale`.

Grayscale: `0.299r + 0.587g + 0.114b`, computed once per frame into a float plane; every stage reads that plane, never the RGBA.

### 2c. Analysis loop and threading

Follow `RemoveBgNode`'s worker exactly, with one difference: this is a **run-to-completion job**, not a latest-only slot.

- Worker opens its **own** `Platform::VideoHandle` on the source clip's path (`VideoSourceNode::LoadedPath()`), so it never touches the render thread's decoder or its position.
- Step forward by `1.0 / sampleFps` seconds (param, default 30). There is no frame-rate accessor in `Platform.h` — sampling at a fixed step is the only option, so say so in a comment.
- **Windows:** `VideoFrameAt` returning false means "not yet", not "never" — loop on `VideoDecodeIsCatchingUp` before treating a frame as missing. On macOS it decodes synchronously and this loop is a no-op. This is the one place the analysis pass will silently produce an empty track on Windows if you skip it.
- Publish progress as an atomic float; the button becomes a progress bar.
- Destructor sets an abort flag and **joins** the worker before any member is freed (`MolderNode`'s rule — a delete mid-analysis must not use-after-free).
- Changing the source clip, or the source node being disconnected, invalidates the track.

### 2d. Storage

Per-frame samples `{t, x, y, scale, rotation, confidence, lost}` in a `std::vector`, persisted through `v.Text("track", encoded)` using `DrawNode::EncodeRecording`'s shape.

**Read DrawNode's warning before choosing the encoding**: `PushUndoCheckpoint` saves every node's params on *every* graph edit, and the undo stack is 200 deep. A 5-minute clip at 30 fps is 9,000 samples — a naive `"%f,%f,%f,%f,%f;"` per sample is ~350 KB, ×200 undo levels. So:

- quantize to 16-bit fixed point per field,
- delta-encode against the previous sample,
- drop samples that are within `1e-3` of a linear interpolation of their neighbours (keyframe decimation — a static object collapses to two samples),
- base64 the result.

Playback reads the table by `Transport` time with linear interpolation between samples, so scrubbing, looping and reverse all work and re-render identically.

## 3. Params

| Param | Type | Default | Notes |
|---|---|---|---|
| `analyze` | button | — | starts/cancels the worker; shows progress + "tracked N frames, 3 lost" |
| `initMode` | dropdown | Auto (subject) | Auto (subject) / Auto (motion) / Manual box |
| `motionModel` | dropdown | Position + Scale | Position / + Scale / + Scale + Rotation |
| `searchScale` | float | 2.0 | search window as a multiple of the box, 1.2–6.0 |
| `featureCount` | int | 40 | corners tracked inside the box, 8–200 |
| `minConfidence` | float | 0.55 | below this the frame is `lost` |
| `adapt` | float | 0.15 | template update rate; 0 = never adapt |
| `smooth` | float | 0.0 | post-hoc smoothing of the track curve, in samples |
| `sampleFps` | float | 30 | analysis step |
| `offsetX` / `offsetY` | float | 0 | attach-point offset from the box centre, in output-normalized units |
| `showOverlay` | bool | true | |
| `overlayStyle` | dropdown | Ring | Ring / Box / Crosshair / Ring + trail |
| `overlayColor` | color | cyan | |
| `overlaySize` | float | 1.0 | multiple of the tracked box |

Every one of these goes through `VisitParams` with a stable key. `searchScale`, `featureCount`, `minConfidence`, `adapt`, `sampleFps` and `initMode` are **analysis inputs** — changing one marks the track stale (a subtle "re-analyze" hint on the button), it does **not** silently re-run.

## 4. UI

Obey `node-ui-pillars` — this is a hard gate, not advice. Layout:

```
┌─ Motion Track ─────────────────────────┐
│  [ image preview with live overlay ]   │   ← draggable box in Manual mode
│  ┌──────────── Analyze ─────────────┐  │   ← becomes a progress bar while running
│  tracked 412 frames · 6 lost · 0.81 avg │
│  init [Auto (subject) ▾]  model [Pos+Scale ▾] │
│   (search)  (features)  (confidence)   │   ← knob row, evenly spaced
│   (adapt)   (smooth)    (fps)          │
│  ☐ overlay   [Ring ▾]  ■   (size)      │
└────────────────────────────────────────┘
```

Overlay is drawn into the output FBO by the node's own shader pass (a ring/box SDF over the passthrough image), **not** by ImGui — it must survive into Output, recording and Syphon, which is the entire point of "the user sees what is being tracked".

## 5. Wiring (all hand-maintained — `codebase-navigation` lists these)

- `src/nodes/MotionTrackNode.h` / `.cpp`
- `CMakeLists.txt` source list
- `NodeFactory` registration + node-type name string
- the add-node menu / palette entry (Analyze category)
- `CategoryColors`
- the four cable-wiring chains in `main.cpp` (image input, image output, **and** the modulator-output chain — `PathNode` and `ImageAnalyzeNode` show both sides)
- `main.cpp` node-body draw dispatch
- the 167-node-type round-trip self-test's type table

## 6. Exit criteria — machine-checkable, not "it looks right"

1. `run-infinite-hygiene` passes, including the full node-type save/load round trip.
2. `cable-logic-sweep` — the image pins and all five modulator pins are reachable and survive save/load.
3. `modulation-sweep` — each of the five outputs can drive a knob, and the binding survives save/load/undo/duplicate/paste.
4. `node-param-audit` — every param above round-trips.
5. A new self-test: load a synthetic clip with a known-trajectory moving square, analyze, assert the `x`/`y` outputs match the ground truth within 2% for every frame. **This is the only thing that proves the tracker actually tracks** — do not skip it and do not replace it with a screenshot. The test must pin `initMode` to **Manual box** (or Auto-motion), never Auto-subject — see §8, the segmentation models differ per platform and an auto-init test would be red on one OS and green on the other for reasons that have nothing to do with the tracker.
6. Delete the node mid-analysis in the self-test harness — no crash, no leaked thread.
7. `windows-parity` — walk §8 point by point and confirm each one.

## 8. Windows / macOS parity — every divergence, and what to do about it

`windows-parity`'s one-abstraction rule holds: **no `_WIN32` anywhere in `MotionTrackNode.cpp`.** Everything below is handled by choosing the right `Platform::` call and the right defaults, not by branching on the OS.

| # | Divergence | Consequence | Required handling |
|---|---|---|---|
| 1 | **Decode is async on Windows, synchronous on macOS.** `MediaWin.cpp` runs each `VideoHandle` on its own decode thread with a ~4-frame prefetch; `Platform.mm` decodes inside the call. | The analysis worker steps faster than real time, so on Windows `VideoFrameAt` returns false meaning *"not yet"*. Skipping this yields a **perfect track on your Mac and an empty one on Windows**. | Loop: `while (!VideoFrameAt(h,t,px) && VideoDecodeIsCatchingUp(h)) { sleep 1ms; }`, then treat a final false as end-of-clip. On macOS the loop never spins. Analysis path only — never the render thread. |
| 2 | **`MattingMode` is ignored on Windows.** `PlatformWin.cpp:745` takes `MattingMode /*mode*/` and always runs U²-Net salient-object; macOS Vision honours Subject vs Person. | Auto-init picks a **different box on the two platforms** for the same clip. Not a bug to fix — a fact to design around. | Don't expose a Subject/Person choice on this node (only `Auto (subject)`). Never assert on auto-init geometry in a test (see exit criterion 5). Note in the node's tooltip that auto-init is a starting guess and Manual box is the reproducible one. |
| 3 | **First `SubjectMask` call constructs the ORT session** (model load + DirectML provider probe) — seconds, not milliseconds. macOS Vision has no equivalent cold start. | A one-off stall at the head of the first analysis. | Already fine: init runs on the worker, and progress is published before the mask call so the bar appears immediately rather than after a silent multi-second gap. Do not call `SubjectMask` from `CookIfNeeded`. |
| 4 | **DirectML may not register**, falling back to CPU U²-Net. | Cold start and per-call cost vary hugely between machines. | Surface `Platform::MattingBackend()` in the analysis status line exactly as `RemoveBgNode` does, so a slow init is explained rather than mysterious. |
| 5 | **Two decoders on one file.** The source node holds a handle; the analysis worker opens a second. On Windows that is a second decode thread + second ~32 MB prefetch + second software decoder; on macOS a second `AVAssetReader`. | CPU and memory roughly double during analysis, worse on Windows. | Acceptable — it is bounded and transient. Close the worker's handle in the same scope that finishes the pass, including on the abort path. Do **not** try to share the source node's handle; its position belongs to the render thread. |
| 6 | `MFStartup` / `CoInitializeEx` are per-handle and refcounted inside `MediaWin.cpp` (`VideoOpen` on a fresh thread initialises COM for that thread itself). | None, but only because the platform layer already does it. | Just call `Platform::VideoOpen` from the worker. Do **not** add COM init to the node — that is exactly the `_WIN32` leak the one-abstraction rule forbids. |
| 7 | **Paths.** Windows needs wide paths; the UTF-8 `std::string` conversion lives in the platform layer. | A hand-rolled path would break on non-ASCII filenames on Windows only. | Pass `VideoSourceNode::LoadedPath()` straight through. Never manipulate the path string in the node. |
| 8 | **GLSL 330 is strict on Windows drivers** — implicit int→float, missing `precision`-free constructs and unused-varying elision that Apple's compiler forgives are hard errors there. | The overlay shader compiles on macOS and fails to link on Windows, giving a black or passthrough image with no overlay. | Write the overlay pass through `GLUtil::CompileProgram` in the same style as the existing filter shaders; explicit `float` literals (`1.0`, not `1`), explicit casts, no unused uniforms. |
| 9 | **Frame orientation.** Both platforms deliver RGBA8 *bottom-up* (already flipped for GL) — macOS flips in `Platform.mm`, Windows in `MediaWin.cpp`. | None, provided you don't flip again. | State the convention in a comment: row 0 is the **bottom** of the picture, so tracker `y` increases upward, and the `y` modulator output is emitted in that same convention as the rest of the graph. |
| 10 | **Float/codec determinism.** Different decoders, different colour conversion, different FP contraction → the same clip analysed on the two platforms gives slightly different tracks. | A patch analysed on a Mac must not silently re-analyse (and change) when opened on Windows. | The track table is persisted in the patch (§2d) and is authoritative on load. `ReloadFromPath`-style reopening of the clip must **not** trigger analysis. Only the Analyze button does. |

Anything not in this table is genuinely identical on both platforms: `SubjectMask`'s call shape, the worker-thread pattern, `std::thread`, the tracker math (plain scalar C++ — no x86 intrinsics; `external/sse2neon` exists but is not needed here), the `ParamVisitor` encoding, and the overlay's FBO plumbing.

## 9. Explicitly out of scope

Multiple tracks · planar / 4-point corner-pin tracking · camera solve / 3D solve · stabilization (invert-the-track) · object detection by class · any ML model beyond the already-present `Platform::SubjectMask` · any new third-party dependency.
