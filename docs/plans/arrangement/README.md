# Arrangement Timeline — build plan

> **Overall: done (2026-10-09, owner).** The WP status in `overhaul-prompt.md` is the history.

> **Superseded in part.** `overhaul-prompt.md` in this folder replaces phases
> 3, 4, 5, 7 and 8 of the build order below and is the **live plan** — it
> carries the current WP status (WP0-WP3 done, WP4 next) and an *As built*
> section describing what actually landed. Read it first; use this file for the
> spec, the UX decisions and the codebase survey.

A DAW/video-editor style arrangement panel for Infinite. This document is the
spec plus the phased build order. Each phase is one branch, one commit, one
fresh session (same convention as `docs/plans/field/`). It lives in the repo at
`docs/plans/arrangement/README.md` (Step 1 commits it there).

Everything below "Codebase facts" was verified against the source. Line
numbers were re-checked at commit **`f5aa88c`** for every site Steps 1–2 touch;
numbers marked **`~`** are from the older survey at `706fb43` (main.cpp has
since grown ~+150–230 lines in the 24k–67k range) — **always re-grep by the
symbol named next to the number**, never trust a bare line number.

Repo root: `/Users/namansoni/infinte` (note the spelling).

---

## 1. What it is

A dockable panel holding N **streams** (lanes). Each stream is typed **audio**
or **video**. A stream holds **clips** — time ranges that reference one
existing graph node's output.

```
 ┌─ Arrangement ──────────────────────────────────────────────┐
 │ 0s      2s      4s      6s      8s     10s                  │
 │ │       │       │       │       │       │        ← ruler    │
 │ ▼ playhead (single, shared by all streams)                  │
 │─────────────────────────────────────────────────────────────│
 │ V1 │███ clip A ███│      │██ clip B ██│                     │
 │ V2 │        │████ clip C ████│                              │
 │ A1 │██ kick ██│      │██ kick ██│  (same node, two clips)   │
 │ A2 │████████ pad ████████│                                  │
 └─────────────────────────────────────────────────────────────┘
```

### Settled semantics

| Question | Answer |
|---|---|
| What's on a clip | A reference to a node **output** (node index + output slot); the clip windows that output live |
| Gaps (no clip) | Stream contributes nothing — silence (audio) / blank (video) |
| Playhead | One, shared across all streams = `Transport::Seconds()` |
| Video stacking | Top-down, Photoshop-style: **stream 1 = frontmost** |
| Per video stream | Blend mode + opacity (same 32-mode set Layer Stack has) |
| Per audio stream | Volume + pan fader |
| Same node, many clips | Allowed — same stream or different streams |
| Clip overlap within one stream | **Forbidden** — clips may butt, never overlap (enforced by the editor, Phase 3) |
| Clip length | Freely trimmable/stretchable, with snapping |
| Params/modulation | Keep running; a clip does not freeze its node |
| Nodes not in any clip | Keep computing if they already would; **Audio Out terminals are muted** while arrangement playback owns the output |
| Per-clip settings | Right-click menu: trigger mode (retrigger vs continuous), fade in/out (audio), gain, playback speed, loop-within-clip |
| Render | Offline/batch, blocks UI, deterministic, no user involvement |

### The one-audio-engine rule

There is exactly one audio device connection. The arrangement gates and mixes
inside the existing `AudioEngine`; it never spawns a second engine. Video has
no equivalent constraint — the canvas keeps previewing live.

---

## 2. Codebase facts this plan is built on

### 2.1 The offline render engine already exists

`OutputNode` has two independent capture paths:

| Path | Entry | Timing | Readback |
|---|---|---|---|
| Live record | `OutputNode::StartRecording` (`src/nodes/OutputNode.cpp:51`) | Real-time, paced to audio sample count | Async triple-buffered PBO |
| **Offline render** | `OutputNode::StartOfflineRender` (`src/nodes/OutputNode.cpp:237`) | **Fixed-step, decoupled from vsync/wall clock** | Synchronous `glReadPixels` |

The offline pump lives in `main()`'s frame loop, not `OutputNode.cpp`. Anchors
at `f5aa88c`: start `Transport::Instance().SetOfflineMode(true, …)`
(`main.cpp:29602`), per-frame `SetOfflineVideoTime(videoSec)` (`main.cpp:52202`),
stop `SetOfflineMode(false)` (`main.cpp:52261`), global `OfflineRenderState
gOfflineRender` (`main.cpp:871`). It already: steps `frameId` manually and cooks
every node with vsync off; drives audio in lockstep via
`AudioEngine::ProcessOffline`; waits (never drops) on encoder backpressure;
refuses to start with a hardware-driven source (`INode::IsHardwareDriven()`);
is A/V-synced by construction.

**Do not build an offline renderer.** Phase 7 generalizes this pump. Limits it
must address:
- Resolution is inherited from `mInput.Width()/Height()` (~`OutputNode.cpp:247`).
  A composite must pick and lock its own canvas size.
- Duration is `OutputNode::offlineDurationSeconds` (`OutputNode.h:214`, an int).
  Timeline length should replace it.
- One take at a time (global `gOfflineRender`). Fine for one final file.

Muxers: macOS AVAssetWriter in `src/platform/Platform.mm`; Windows Media
Foundation `IMFSinkWriter` in `src/platform/win/MediaWin.cpp` (not `PlatformWin.cpp`).

### 2.2 Audio: the topology is terminal-seeded — this shapes Phase 5

`AudioEngine::RunTopology` (`src/audio/AudioEngine.cpp:171`) runs every node
**in the published topology**, then sums every `AudioTerminal` into the device
buffer (loop at `AudioEngine.cpp:280-311`; per-block
`capture->enabled` check at `:296`).

> **Correction vs. the first draft.** "Runs every node unconditionally" is only
> true of nodes that are *in* the topology. `RebuildAudioTopology()`
> (`main.cpp:29119`) seeds the walk from three places only: every
> `AudioOutputNode` (these become the terminals), every node with a connected
> note input, and every node whose `RequiresAudioProcessing()` is true
> (`main.cpp:~29192`). **An audio node that isn't wired to an Audio Out is
> never processed and has no buffer.** And `AudioTerminal` (`AudioEngine.h:56`)
> only ever wraps an Audio Out's buffer — so a per-terminal gate can mute
> Audio Outs, but it **cannot select which upstream node is audible**.

Consequence for Phase 5 (see §3): clip-referenced nodes must become extra
**seeds** of `RebuildAudioTopology` plus per-stream **arrangement terminals**;
the topology is rebuilt when the *arrangement is edited* (rare, main thread),
never at clip boundaries during playback. Clip on/off and fader gain inside a
running topology are per-block atomics, on the `capture->enabled` precedent.

`MixerNode` (`src/nodes/AudioNodes.h:65`; DSP ~`AudioNodes.cpp:92-249`) is the
fader template — per-slot gain/pan/mute/solo atomics, `DspMath::EqualPowerPan`,
`ParamMailbox`-smoothed gain. (The first draft called it `AudioMixerNode`;
that's only a forward declaration.)

`ParamMailbox` (`src/audio/ParamMailbox.h`) is scalar-float-only. For richer
state the precedent is `AudioEngine::SetTopology`'s publish pattern: immutable
snapshot built on the main thread, `std::atomic<T*>::exchange`, old one retired
a generation later.

Node teardown: `RemoveNodeByIndex` (`main.cpp:29668`) retires the node into
`gRetiredNodes` tagged with `AudioEngine::CurrentGeneration()`. **The
arrangement must never hold a raw node pointer across a deletion** — hold
indices and re-resolve.

### 2.3 Retrigger: no interface-level mechanism

No `Seek`/`Retrigger` on `INode` or `AudioNode`; `AudioNode::Reset()` takes no
position. What exists is **node-specific**:
- `SamplerNode::TriggerPreview(float frac)` (`SamplerNode.h:103`) — main-thread-
  set, audio-thread-read atomic slot. Best template for "start from X now".
- `GranularNode::Seek(float frac)` (`GranularNode.h:82`) and
  `PaulStretchNode::Seek(float frac)` (`PaulStretchNode.h:60`). *(The first
  draft said no Seek exists anywhere — these two do, and Phase 6 should wrap
  them rather than reimplement.)*
- `VideoSourceNode` uses a >250 ms position-jump heuristic to detect seek/loop.

`Transport::TriggerReset()` (`Transport.h:27`) is consumed only by Field
`state` cells (plus one self-test call) and is global — unusable for per-clip
retrigger. Phase 6 is still the highest-risk phase.

### 2.4 Blend modes are reusable; Layer Stack itself is not

`src/core/BlendModes.h/.cpp`: `BlendModes::Names()` = 32 modes (0–31), ending
in **Erase (30)** and **Anti-Erase (31)**, which are alpha-only and handled by
the caller. `BlendModes::kBlendGLSL` is spliced into consumers' shaders.

`LayerStackNode::FragSrc()` (`LayerStackNode.cpp:11`) is the composite formula
to copy, but Layer Stack is hard-capped at `kSlots = 4` (`LayerStackNode.h:13`).
The stream compositor writes its own runtime-N shader on the same pattern.

> **Order trap:** Layer Stack loops slot 0→3 as base→frontmost. Spec says
> **stream 1 = frontmost** — reverse the mapping or the loop.

### 2.5 Getting a node's picture

- 2D nodes: `node->CookIfNeeded(frameId); node->GetOutputTexture()` — memoized
  per `frameId`.
- **Multi-output image sources** exist (`FieldPixelNode`'s aux texture output;
  `CableRecord::srcOutput`), so a clip must store an output slot, not just a node.
- 3D/geometry nodes have no texture. `DrawViewportPanelCard` (`main.cpp:24630`)
  renders them via `NodeViewport` + `SharedViewportCamera`. A geometry clip
  needs the same.

Classification: `CanShowInViewportPanel` (`main.cpp:24565`) holds the audio test
the timeline needs: `dynamic_cast<IAudioSource*>(n) && !dynamic_cast<VideoSourceNode*>(n)`.
**`VideoSourceNode` is both** an `IAudioSource` and a texture producer (image
and audio on *separate outputs*) — the one node allowed on either stream type,
and the reason `srcOutput` is part of the clip record.

### 2.6 Waveform data exists; a shared drawing helper does not

`SamplerNode` caches a 256-bucket min/max waveform (`kWaveformCacheSize`).
The min/max polyline draw is copy-pasted at 5+ sites in `main.cpp` (~`10368`,
`10518`, `10656`, `10771`, `13515`). Phase 8 factors out `DrawWaveform()`.
No Scope node exists.

### 2.7 Panels: five mandatory wiring sites

Hand-rolled docking; Modulation Matrix is the reference. Three docked panels
exist today (Mod Matrix, Viewport Panel, Perf Panel), so this is the fourth.

1. **Global state block** — `gModMatrixOpen`/`gModMatrixDock` at `main.cpp:969-970`
   (0=bottom/1=right/2=left/3=top). Session UI state: **never** serialized,
   **never** in undo.
2. **`DrawModMatrixTable()`** (`main.cpp:25146`) + **`DrawModMatrixDocked()`**
   (`main.cpp:25690`). Dock-side right-click switcher just above (`~25670-25682`)
   is a manual rect hit-test, not `BeginPopupContextWindow`.
3. **Per-frame space reservation — inline in `main()`**, starting at
   `const bool matrixBottom = …` (`main.cpp:53826`). Each panel gets
   Bottom/Right/Left/Top booleans, then a clamp block subtracts every *other*
   open panel's footprint. A fourth panel adds a term to all three existing
   formulas plus its own. Top/bottom panels cost one extra `ItemSpacing.y`.
4. **Three toggle sites** — settings-menu checkbox, top-bar
   `TopBarIconToggle` (toggle at `main.cpp:53743`), raw key check (~`66310`).
5. **Shortcuts help table** — `kShortcuts[]` (`main.cpp:28497`), documentation only.

### 2.8 Patch format and undo

Hand-parsed, one-tag-per-line format (grammar at `src/core/Patch.h:16-61`).
**Forward-compatible by construction**: `Patch::Read`'s tag chain has no final
`else` — an unknown tag just matches nothing (`Patch.cpp:649`); missing
trailing tokens leave fields at their defaults. Version is checked one-way
(`Patch.cpp:348-352`). **`Patch::Read` fails on a file with zero `node`
records** (`Patch.cpp:672-676`) — relevant to test fixtures.

`Patch.cpp` already has `DoubleToString` (`%.17g`, `Patch.cpp:50-55`, used for
gesture `timeSec`) alongside `FloatToString` (`%.9g`).

**Parent+child template** is `perf`/`perftarget`: `PerfRecord` (`Patch.h:154-179`)
**owns** `std::vector<PerfTarget> targets` (nested in memory); on disk each
`perftarget <perfIndex> …` line back-references the parent by index
(write `Patch.cpp:276-299`, read `Patch.cpp:606-624`). Streams/clips use this
exact shape: **clips nested inside `StreamRecord`**, not a flat vector with a
`streamIndex` field — so reordering/deleting a stream can never leave a clip on
the wrong lane (the parallel-index bug class this codebase has hit before).

**Node references are indices, and indices are reused.** `ApplyPatchData`
(`main.cpp:31930`) calls `NewPatch()` (which resets `gNextIndex = 1`), respawns
every node, builds `remap`, and resolves every record through `resolve(savedIndex)`
(~`main.cpp:31990`). **Every Undo/Redo goes through this**, so a node's index
changes on every undo.

**Undo is full-snapshot**: `UndoEntry { Patch::Data patch; GestureRecorder::PlaybackMap gestures; }`
(`main.cpp:30827`), cap 200. `PushUndoCheckpoint()` (`main.cpp:32221`) is called
**before** a mutation; `PushUndoSnapshot()` (`main.cpp:32202`) takes a snapshot
captured earlier (drag coalescing: `gDragStartSnapshot` at mouse-down,
~`main.cpp:67241`).

> **Putting streams inside `Patch::Data` gets save/load, undo, autosave and
> index remapping for free — but not deletion or File→New.** Two live-state
> sites need explicit code, because they don't go through `ApplyPatchData`:
> - `RemoveNodeByIndex` (`main.cpp:29668`) cleans up per-node state inline —
>   `Modulation::UnbindAllFor`, `PaletteBinding::UnbindAllFor`,
>   `GestureRecorder::ClearForNode` — with a comment explaining that indices
>   are reused so stale keys re-attach to strangers. Clips need the same line.
> - `NewPatch()` (`main.cpp:30867`) clears every index-keyed store. Clips must
>   be cleared there too. *(`gPerfElements` is not cleared in either place
>   today — a pre-existing latent bug; don't copy it.)*

Copy/paste uses `ClusterClipboard` (`main.cpp:5995`), not `Patch::Data`, so
clips are unaffected by node copy/paste (intended). `PatchJson::ToJson`
(`src/core/PatchJson.cpp`, RemoteControl `get_graph`) mirrors `Patch::Data`
and should get the new records too.

### 2.9 What does not exist anywhere

- No timeline / arrangement / clip / stream / lane concept.
- No time-range / interval structure in `src/core/`.
- No interface-level `Seek`/`Retrigger` (only node-specific, §2.3).
- **No `Transport` seek.** Only `Rewind()`, `SetPlaying()`, offline
  `SetOfflineVideoTime()`. Click-to-seek on the ruler (Phase 2) needs a new
  `Transport::Seek(double seconds)` that keeps the audio sample clock
  (`AdvanceAudioClock`) consistent.
- No struct-passing `ParamMailbox`; no per-block mute on `AudioTerminal`.
- No shared waveform helper; no Scope node.
- No independently-settable `OutputNode` resolution.
- No four-panel layout precedent.

---

## 3. Build order

One branch each, `feature/arrange-step-NN-<slug>`, merged to `main` per
`git-branch-workflow`. Ordered by dependency, then risk.

| # | Phase | Why here | Risk |
|---|---|---|---|
| 1 | Data model + serialization | Everything references it; testable headless | Low |
| 2 | Panel shell + Transport seek | UI chrome; ruler, lanes, playhead, **seek** | Low–Med |
| 3 | Clip editing | Add/delete/drag/trim/snap, undo coalescing, right-click settings | Medium |
| 4 | Video compositing | N-stream shader, top-down order, blend+opacity | Medium |
| 5 | Audio routing + faders | Topology seeds + arrangement terminals + Audio Out mute | **High** |
| 6 | Retrigger | New per-node mechanism; wraps existing node-specific Seeks | **Highest** |
| 7 | Offline render integration | Needs 4+5; generalizes the existing pump | Medium |
| 8 | Polish | Waveform thumbnails, fades, speed, loop-in-clip | Low |

Each phase ends with a clean build, a named `INFINITE_*TEST` fixture registered
in `.claude/skills/run-infinite-hygiene/driver.sh`, and the hygiene driver
green. Phases 5–6 also pass `audio-node-sweep` and `audio-pipeline-sweep`;
Phase 7 passes `av-sync-sweep`; Phase 2 passes `panels-sweep`.

### Phase detail

**1 — Data model.** `Patch::StreamRecord` owning `std::vector<Patch::ClipRecord>`;
`stream`/`clip` tags on the `perf`/`perftarget` model; seconds as `double`
via `DoubleToString`; live state `gArrangeStreams` next to `gPerfElements`;
cleanup in `RemoveNodeByIndex` and `NewPatch`; `PatchJson` parity;
`INFINITE_ARRANGETEST`. Full executable prompt: `step-01-data-model-prompt.md`.

**2 — Panel shell.** All five wiring sites (§2.7). Ruler, lane headers, stream
add/remove/reorder (reorder moves the whole `StreamRecord` — clips ride along),
shared playhead = `Transport::Seconds()`, horizontal zoom/scroll, and
`Transport::Seek(double)` for ruler clicks. No clips drawn. Exit: docks all
four sides, resizes, coexists with the other three panels without squeezing
the canvas below its floor; `panels-sweep` green.

**3 — Clip editing.** Drag a node onto a stream to create a clip (reject
audio→video and vice versa via §2.5's gate; `VideoSourceNode` allowed on both,
with `srcOutput` picking image vs audio). Move, trim either edge, snap (grid,
clip edges, playhead). Right-click per-clip settings. Undo coalescing per §2.8
(`gDragStartSnapshot` pattern; scalar widgets checkpoint on
`IsItemDeactivatedAfterEdit()`). Exit: one undo entry per gesture; no edit can
produce a negative-length clip or an overlap within a stream.

**4 — Video compositing.** Runtime-N shader splicing `BlendModes::kBlendGLSL`,
`LayerStackNode::FragSrc()` formula, **reversed order** (§2.4). Clip→texture via
§2.5 with `NodeViewport` fallback for geometry. Needs the canvas-resolution
decision (§4). Exit: two streams with different blend modes composite
pixel-identical to the equivalent hand-built Layer Stack.

**5 — Audio routing + faders.** *(Redesigned — see §2.2.)*
1. `RebuildAudioTopology` gains a fourth seed set: every node referenced by a
   clip on an audio stream (resolved from `gArrangeStreams` by index).
2. One **arrangement terminal per audio stream**: sums its clip nodes' output
   buffers × per-clip gain × stream gain/pan (`MixerNode` DSP template) into
   the device buffer.
3. Per-block atomics (on the `capture->enabled` precedent): per-clip *active*
   and gain, per-stream gain/pan, and one **Audio Out mute** flag gating the
   existing terminals while arrangement playback owns the output.
4. Main thread computes active clips from `Transport::Seconds()` each frame and
   stores floats; the audio thread only reads them. Arrangement edits trigger
   `RebuildAudioTopology()`; clip boundaries never do.
Exit: transport running → only clip-selected audio is audible, even for a
clip node wired to no Audio Out; stopping restores normal live output;
`AUDIOTEARDOWNSWEEPTEST` passes with a clip referencing a node deleted
mid-playback.

**6 — Retrigger.** Per-node "clip just started at offset X" on the
`SamplerNode::TriggerPreview` atomic-slot model, wrapping `GranularNode::Seek`
/ `PaulStretchNode::Seek` where they exist, driven by a main-thread clip-start
edge. **Do not** use `Transport::TriggerReset()`. Continuous is the default;
retrigger is hidden in the menu for nodes that can't seek (§4). Exit: a kick
sample on two clips attacks twice; a continuous clip does not restart.

**7 — Offline render.** Generalize the pump: per stepped frame resolve active
clips, composite (4), route audio (5), feed the OutputNode instead of its single
cable; duration from timeline length; explicit locked canvas resolution. Keep
the hardware-source refusal and backpressure handling untouched. Exit:
multi-minute render stays A/V synced; `av-sync-sweep` green.

**8 — Polish.** Factor `DrawWaveform()` out of its 5 copies (§2.6) and use it
for thumbnails. Fades, per-clip gain, speed, loop-within-clip.

---

## 4. Open design questions

Answer before the phase that needs them:

- **Phase 2 — `Transport::Seek` semantics.** Seek while playing: jump the
  audio clock immediately, or queue for the next block? Recommend: set a
  pending-seek atomic, applied by `AdvanceAudioClock` at the block boundary.
- **Phase 4/7 — canvas resolution.** Recommend an explicit per-arrangement
  setting, added then as a new `arrange <w> <h>` tag (forward-compatible; no
  need to add it in Step 1).
- **Phase 5 — transport stopped.** Recommend: Audio Out mute only while the
  transport is playing *and* the arrangement has at least one clip; otherwise
  normal live behaviour.
- **Phase 6 — nodes with no meaningful seek.** Recommend hiding retrigger in
  their right-click menu rather than silently no-op'ing.
