# Infinite — High-Payoff Improvement Roadmap

A prioritized backlog of improvements ranked by **real-world user payoff** (what musicians, DAW producers, live AV performers, and creative coders feel directly), grounded in the current `repo` codebase, `tools/bughunt`, `perfhunt-state/candidates.md`, and `docs/quality/SCOREBOARD.md`.

---

## Already Completed (2026-11-22 Baseline)

For context so work is not duplicated:
- **Stability (`tools/bughunt`)**:
  - Fixed `SIGSEGV` in Metal Live Engine `MetalRaster::MakeGpu` on empty mesh buffers (`src/engine/rhi/MetalDevice.mm`).
  - Bounded `furMaxStrands` (`1..400000`), `furSegments` (`1..32`), and guarded `NaN` length/density (`src/core/MeshFur.cpp`, `src/nodes/GeometryOpNodes.cpp`).
  - Clamped `Render3DNode::passMask` (`0..255`) and cleared `code_lint.py` to `0` new hits.
  - Suppressed auto-updater during fuzz runs (`src/core/UpdateCheck.cpp`), fixed `frame_fuzz.py` stdout drain, and fixed `fuzz_patch` linking `TileStore.cpp`.
- **Ableton / DAW VST3 Multi-Instance Phase 1**:
  - Closed-editor steady-state CPU dropped **~300×** (`~111%` → `<0.4%` per instance, 0 GL draw calls/swaps, pure `sleep_for` pacing when hidden, unthrottled offline bounces).
  - Peak RAM per closed VST3 instance dropped **-59.1%** (`293.7 MB` → `120.0 MB` via `64×64` launch window + lazy font/scanner loading).
- **Render, Canvas & Audio DSP Quick Wins**:
  - Cached 399 hot-path `glGetUniformLocation` calls via `GLUtil::UniformLoc` (`B4` `stages_cpu_ms.cook` **-11%**).
  - Skipped `paramSnapshot` `std::map` allocations when no expressions exist (`stages_cpu_ms.modulation` **-14% to -31%**) and made `StageLinks.cpp` pointer indexing lazy `O(N)`.
  - Optimized `ChorusKernel.cpp` (**-28.1%** per-sample CPU, `57 → 41 ns`, bit-identical `FXAUDIT`) and `PhaserKernel.cpp`.

---

## Track 1 — DAW VST3 Multi-Instance Phase 2 (Memory & Project Load Time)

- **User Payoff**: **Very High** — Producers running 10–20 Infinite instances in Ableton/Logic still pay `~120 MB` RAM and `~1–2 s` startup per instance (~1.3–2.4 GB total) because every instance spawns its own OS process with a GLFW/OpenGL context.
- **Root Cause**: 1-process-per-plugin-instance architecture (`src/plugin/shell/Shell.cpp`, `src/plugin/engine/HostAudio.cpp`).
- **Key Files**:
  - `src/plugin/shell/Shell.cpp`
  - `src/plugin/engine/HostAudio.cpp`, `src/plugin/engine/PluginView.cpp`
  - `src/app/Startup.cpp`
- **Implementation Options**:
  1. **Option A (Medium — Lazy GLFW/OpenGL Context)**: Start `--plugin-engine` without creating a GLFW window or OpenGL context at all until the first `kMsgView` open request (audio-only patches never touch AppKit/OpenGL/Metal). Expected: `120 MB` → **~35–50 MB** per instance, startup `< 300 ms`.
  2. **Option B (Large — Shared Multi-Instance Engine Process)**: One shared engine daemon hosts multiple isolated `Patch`/`AudioEngine` slots over IPC. Expected: 15 instances share one binary/GL/font footprint (**~250–400 MB total** instead of `1.8+ GB`), near-instant instance spawn.
- **Verification**:
  - `./build/plugin_engine_check` (all pass, offline determinism `max sample diff 0.0`).
  - Multi-instance benchmark: spawn 10 concurrent closed instances, measure total RSS, time-to-all-ready, and audio xruns under live 256-sample playback.

---

## Track 2 — macOS Live Audio Input-to-Sound Latency (`-15` to `-20 ms`)

- **User Payoff**: **Very High** — Guitarists, vocalists, hardware synth players, and live AV performers monitoring through Infinite feel `15–20 ms` of excess capture latency immediately as slapback/sluggishness.
- **Root Cause**: Traced in `perfhunt-state/candidates.md` (#67) — excess capture ring-buffer prefill/safety margin in macOS AUHAL audio input path.
- **Key Files**:
  - `src/platform/Platform.mm` (AUHAL input/output callback and capture ring buffer)
  - `src/audio/AudioEngine.cpp`
- **Plan**:
  1. Measure current loopback latency (output click → input capture) at 64/128/256 buffer sizes.
  2. Align AUHAL input/output device buffer sizes and reduce capture ring-buffer target fill from excess multi-block padding to minimal drift-safe headroom (1 block + adaptive drift trim).
  3. Guard against underruns on aggregate/Bluetooth/USB interfaces with clean fallback.
- **Verification**:
  - Loopback latency measurement before/after (`-15` to `-20 ms` target).
  - `B1` audio xrun check + `tsan_selftests.sh` under TSan + 10-minute live input soak test.

---

## Track 3 — In-App AI Patch Agent "First-Try Magic" & Headless Engine

- **User Payoff**: **High** — Built-in chat/agent patch generation is a flagship differentiator; eliminating silent patches, disconnected sinks, clipping/NaN, and messy layouts makes users trust and rely on it daily.
- **Key Files**:
  - `src/core/chat/OpenAIAgent.cpp`, `src/core/chat/AgentRun.cpp`, `src/core/chat/ChatDiff.cpp`, `src/core/chat/PatchGrammar.cpp`, `src/core/chat/NodeFinder.cpp`
  - `src/app/selftest/Headless.cpp`, `src/core/InfiniteCli.cpp`
  - `tools/patch-layout.py`, `tools/mcp/`
- **Plan**:
  1. **One-shot `infinite verify <patch.inf>` verb**: Returns compact JSON with load errors, disconnected sinks, audio RMS/peak/LUFS/non-finite frame count, stuck note count, and non-blank frame check.
  2. **Self-verifying agent loop**: Run `verify` automatically after patch generation/edits; if silent, NaN, clipping, or blank, feed the structured diagnosis back for 1 automatic repair pass before showing the result.
  3. **Signal-lane auto-layout**: Upgrade `patch-layout.py` to separate audio lanes (Notes → Synths → FX → Mixer → Audio Out) and visual lanes (Generators → 3D/2D → Output) cleanly left-to-right.
  4. **Persistent headless worker mode**: Keep a warm headless process alive for repeated `verify` / render / fuzz calls (eliminating cold-start overhead for both agents and `param_fuzz.py`).
- **Verification**:
  - Run `FindEval` + prompt suite (20 representative AV/music prompts): track before/after first-try non-silent, non-blank, zero-error pass rate.

---

## Track 4 — Generative Music & Sample Creation (Musicality & Playability)

- **User Payoff**: **High** — Moves generative nodes from "interesting experiments" to tools musicians keep in real songs and live sets.
- **Key Files**:
  - Note generators: `src/nodes/PredictiveNotesNode.cpp`, `src/nodes/PredictiveRhythmNode.cpp`, `src/nodes/PredictiveVelocityNode.cpp`, `src/nodes/PredictiveQuantizeNode.cpp`, `src/nodes/FieldNotesNode.cpp`
  - Sample generator: `src/nodes/FieldSampleNode.cpp`, `tools/gensample_diversity.py`, `src/app/selftest/GenSampleTests.cpp`
  - Panels: `src/app/panels/PianoRollPanel.cpp`, `src/app/panels/DrumPanel.cpp`, `src/app/panels/SampleStudioPanel.cpp`
- **Plan**:
  1. **Shared Harmonic Context**: Patch-level scale/chord progression bus so bass, chords, melody, and arpeggiators stay harmonically locked without manual key-snap wiring on every track.
  2. **Phrase & Form Awareness**: Add 4/8/16-bar phrase positioning (turnaround fills, cadence resolution, density/tension curves) to `PredictiveNotes` and `PredictiveRhythm`.
  3. **Interactive Conditioning**: "Lock bar / regenerate selected notes / steer density & syncopation" controls directly in `PianoRollPanel` and `DrumPanel` with deterministic seed replay.
  4. **Generated Sample → Playable Instrument**: Auto-detect root pitch, clean zero-crossing loop points, and transients on `FieldSampleNode` outputs so one click drops them into `Sampler` / `Slicer` / `Granular` ready to play chromatically.
- **Verification**:
  - `INFINITE_NOTEAUDIT=1` (0 stuck/unpaired notes, grid error `<= 1.0` sample).
  - `GenSampleTests` + `tools/gensample_diversity.py` diversity metrics.

---

## Track 5 — Audible DSP Sound Quality & Analog Emulation

- **User Payoff**: **High** — Sound quality is the #1 reason producers choose a synth or effect over stock DAW plugins.
- **Key Files**:
  - `src/audio/dsp/ShapeResonatorKernel.cpp`, `src/audio/dsp/ConvolutionKernel.cpp`
  - `src/audio/dsp/FilterModels.h`, `src/audio/dsp/MatchedBiquad.h`
  - `src/audio/dsp/BlepOscillator.h`, `src/audio/dsp/AnalogPrimitives.h`
  - `src/audio/dsp/ClipTimeStretch.h`, `src/audio/dsp/SpectralPitchShifter.cpp`
  - `src/audio/dsp/PeakLimiter.h`, `src/audio/dsp/ReverbKernel.cpp`, `src/audio/dsp/ElectricPiano.h`
- **Plan**:
  1. **Fix remaining `FXAUDIT` flags**:
     - `Shape Resonator`: normalize default 1 kHz gain (`-29.0 dB` flag) and make release time sample-rate invariant across 44.1 / 48 / 96 kHz (`x1.24` drift flag).
     - `Convolution Reverb`: fix `x1.17` release time drift across 44.1 / 48 / 96 kHz.
  2. **Nonlinear ZDF Filter Saturation**: Add cheap in-loop saturation (tanh / Padé / 1-step Newton-Raphson) to ladder / MS-20 / diode models in `FilterModels.h` for authentic self-oscillation, bass retention, and audio-rate FM stability.
  3. **Phase-Locked Time/Pitch Stretching**: Pair `TransientDetect.h` with spectral peak phase-locking in `ClipTimeStretch.h` and `SpectralPitchShifter.cpp` to reduce phasiness on polyphonic audio and drums.
  4. **Mastering-Grade Limiting**: Add 4× oversampled true-peak detection and program-dependent release in `PeakLimiter.h`.
- **Verification**:
  - `INFINITE_FXAUDIT=1`, `INFINITE_SYNTHAUDIT=1`, `INFINITE_DSPTEST=1`, `INFINITE_ALIASTEST=1` (all green, zero NaN/denormal/click regressions).

---

## Track 6 — Live 3D, Field VM & Multi-Projector Show Reliability

- **User Payoff**: **High (for live AV performers)** — Eliminates dropped frames in heavy 10k-instance 3D scenes, Field Patcher audio/visual kernels, and multi-projector shows.
- **Key Files**:
  - 3D Instancing: `src/nodes/Geometry3DNodes.cpp`, `src/nodes/PointDistributionNodes.cpp`, `src/nodes/Render3DEngine.cpp`
  - Multi-projector pacing: `src/app/graph/Projector.cpp`, `src/app/frame/StagePresent.cpp`
  - Field VM: `src/core/field/` (`ElementVM::ExecuteBlock`, perf candidate #72)
- **Plan**:
  1. **10,000-Instance 3D Speed (`geo.instance_ms`)**: Close gap from `3.08 ms` to `<= 1.30 ms` (Blender baseline `1.26 ms`) by removing per-instance CPU matrix rebuild/upload overhead and batching GPU instance buffer updates.
  2. **Multi-Projector VSync (`bench.b8_window_p99_ms` & `missed_vsync_pct`)**: Close `33.21 ms` p99 (`<= 18.30 ms` target) and `9.36%` missed vsyncs (`<= 0.5%` target) by decoupling secondary projector swaps / reducing main-context lock contention.
  3. **Field `ElementVM::ExecuteBlock` (#72)**: Apply traced `-20%` to `-50%` inner-loop optimizations for faster Field Patcher and spectral Field performance.
- **Verification**:
  - `scripts/bench/ab.sh` on `B4`, `B8`, and `Field` benchmarks; `tools/quality/quality.py` scoreboard updates.

---

## Track 7 — Quality Gates, Sanitizer Coverage & Cross-Platform Parity

- **User Payoff**: **Medium–High (trust & stability)** — Prevents regressions before users hit them on macOS, Windows, or Linux.
- **Key Files**:
  - `docs/quality/SCOREBOARD.md`, `docs/windows-vst3-status.md`
  - `tools/bughunt/` (`code_lint.py`, `clamp_check.py`, `param_fuzz.py`, `tsan_selftests.sh`)
- **Plan**:
  1. **Measure the 12 unmeasured P8 scoreboard metrics**: `R5` 4,000-node canvas stress (`pan`, `select`, `delete_all`), `B3` live edit-storm (`<= 16.7 ms` max work, `0` xruns — release-gated), `B7` soak (RSS growth, xruns, thermal drop), and live 10k-instance 60 fps.
  2. **Expand P6 (Timeline & Arrangement) metrics**: Add automated regression checks beyond the current 2 metrics (clip automation, session-to-arrangement recording, loop/transport sync).
  3. **Runtime-verify Windows VST3 hosting**: Test Phases 1–3 on real Windows x64/ARM64 hardware with third-party VST3 plugins (editor HWND, resize, state save/restore, out-of-process scanner).
  4. **CI / Pre-push Lint Gates**: Run `tools/bughunt/code_lint.py` and `clamp_check.py` in CI; run periodic ASan+UBSan+Hardened `param_fuzz.py --kind signal|mod|text` and TSan `tsan_selftests.sh` sweeps.

---

## Track 8 — Panel & Canvas UX Polish

- **User Payoff**: **Medium–High (daily ergonomics)** — Smooths out everyday workflow friction across the major panels.
- **Key Files**:
  - `src/app/panels/PianoRollPanel.cpp`, `DrumPanel.cpp`, `MixerPanel.cpp`, `SampleStudioPanel.cpp`, `FieldPatcherPanel.cpp`, `ModMatrixPanel.cpp`, `CurveEditorPanel.cpp`, `FindOverlay.cpp`
  - `docs/node_param_audit.md`
- **Plan**:
  1. **Canvas Semantic Zoom / LOD**: Simplify node bodies at low zoom so large patches stay readable and fast.
  2. **Panel Power-User Upgrades**:
     - `PianoRollPanel`: Ghost notes from other tracks + synced scale highlighting.
     - `MixerPanel`: Master LUFS / true-peak metering + A/B snapshot morphing + bus folding.
     - `SampleStudioPanel`: Zero-crossing snap, auto-transient slice preview, direct drag-to-Sampler/Slicer.
     - `FieldPatcherPanel`: Inline scope/spectrum probes + error line/column jump.
     - `ModMatrixPanel`: Source/target search filter + live per-row modulation activity meters.
  3. **Parameter Audit Completeness**: Attribute the remaining 14 generic-path node types in `docs/node_param_audit.md` and audit the 1 remaining non-modulatable control.

---

## Recommended Execution Order

| Priority | Track | Effort | Expected User Impact |
|---|---|---|---|
| **1** | **Track 1 (Option A)**: Lazy GLFW/GL in DAW VST3 | M | Cuts 11-track Ableton RAM from ~1.3 GB to ~450 MB & fast project load |
| **2** | **Track 2**: macOS AUHAL capture latency (`-15..20 ms`) | M | Immediately felt by every live musician & AV performer |
| **3** | **Track 3**: Self-verifying AI Patch Agent + `infinite verify` | M | Huge jump in first-try patch quality & agent autonomy |
| **4** | **Track 5**: `FXAUDIT` fixes + ZDF filter saturation + true-peak limiter | M | Direct audible sound quality upgrade |
| **5** | **Track 6**: `geo.instance_ms` + `B8` multi-projector + Field VM | M | Rock-solid 60 fps live 3D & multi-screen shows |
| **6** | **Track 4**: Generative harmonic bus, phrasing & playable samples | M–L | Differentiating musicality in note & sample generators |
| **7** | **Track 7 & 8**: Scoreboard coverage, Windows VST3 runtime, UX polish | Ongoing | Long-term quality & daily polish |
