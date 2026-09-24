# Performance benchmark suite

Governing spec: [`benchmark-suite.md`](benchmark-suite.md) (§1-§8). This
document is the living status: what's built, how to run it, the baseline
table, and bugs found while measuring (not fixed - this suite's rule is
measure only, see benchmark-suite.md §8).

Built on `feature/perf-benchmark-suite`, off `main` at `4d62140`.

## How to run

```bash
cmake --build build -j8
scripts/bench/run_all.sh                    # default suite, into bench/results/<machine>/<date>-<sha>.jsonl
scripts/bench/run_all.sh --soak             # + B7 (30min, when it exists)
scripts/bench/compare.py bench/baselines/m2-8gb.jsonl bench/results/.../<run>.jsonl
```

A single fixture can be run directly for fast iteration, same pattern as
`run-infinite-hygiene`'s tests:

```bash
INFINITE_BENCH_B5EMPTY=1 INFINITE_EXITAFTER=200 ./build/Infinite.app/Contents/MacOS/Infinite
INFINITE_BENCH_B5NODES=100 INFINITE_EXITAFTER=200 ./build/Infinite.app/Contents/MacOS/Infinite
INFINITE_BENCH_B1VOICES=24 INFINITE_BENCH_B1BUFFER=256 INFINITE_BENCH_B1SECONDS=10 \
   INFINITE_EXITAFTER=2000 ./build/Infinite.app/Contents/MacOS/Infinite
INFINITE_BENCH_B5AUDIOALONE=256 INFINITE_BENCH_B5AUDIOALONE_SECONDS=10 \
   INFINITE_EXITAFTER=2000 ./build/Infinite.app/Contents/MacOS/Infinite
INFINITE_BENCH_B2SCALE=l INFINITE_BENCH_B2ANIM=1 INFINITE_BENCH_B2GPUNODES=1 \
   INFINITE_EXITAFTER=160 ./build/Infinite.app/Contents/MacOS/Infinite
INFINITE_BENCH_B4SCALE=l INFINITE_BENCH_B4SHADOW=2048 INFINITE_BENCH_GPUTIMERS=0 \
   INFINITE_EXITAFTER=160 ./build/Infinite.app/Contents/MacOS/Infinite
# B3: leave Infinite in front for the whole run; `unfocused=1` in variant = discard
INFINITE_BENCH_B3=1 INFINITE_BENCH_B3BUFFER=256 INFINITE_BENCH_B3FRAMES=600 \
   INFINITE_EXITAFTER=620 ./build/Infinite.app/Contents/MacOS/Infinite
INFINITE_BENCH_B9SCENE=b2 INFINITE_BENCH_B9FRAMES=600 \
   INFINITE_EXITAFTER=660 ./build/Infinite.app/Contents/MacOS/Infinite
```

Every fixture prints exactly one `BENCH_JSON {...}` line (schema in
benchmark-suite.md §3) followed by a `<NAME> DONE` marker, then closes its
own window - `INFINITE_EXITAFTER` is a safety cap, not the real exit
mechanism, for fixtures that run on wall-clock time (B1) rather than a fixed
frame count (B5).

## Status: what's built vs. what the spec asks for

| Bench | Spec'd in §4 | Built this session | Notes |
|---|---|---|---|
| B1 Heavy audio | 12-32 voices, buffer sweep 64/128/256/512, cb_load p99/xruns | **Yes** (`INFINITE_BENCH_B1VOICES`) | 1-64 voices, Sampler/Wavetable/Oscillator cycled through Audio Filter→Wavetable Shaper→Delay→Reverb→Dynamics, summed through a Mixer tree, one shared LFO modulating every voice's Filter `mix`. Reports cb_load percentiles (raw, from the new `AudioLoadRing`) + xruns + main fps + RSS. |
| B1(stages) Per-DSP-stage audio breakdown | Not in original §4 - added to attribute B1's ~56-60% cb_load across its five per-voice stages (Filter/Drive/Delay/Reverb/Dynamics), needed before any SIMD/threading optimization decision | **Yes** (`INFINITE_BENCH_B1VOICES` stages breakdown) | Built into `AudioEngine::Process` via `AudioLoadRing mStageLoadHistory[kAudioStageCount]`. Measures per-stage CPU time without locks or allocations on the audio callback thread. Reports p50 callback load fraction per stage into `stages_cpu_ms`. |
| B2 Heavy visuals | Geometry→Render3D→10-30 compositing nodes→Output, static+animated | **Yes** (`INFINITE_BENCH_B2SCALE`, `INFINITE_BENCH_B2ANIM`) | Scales s (10 effects, 1.8k tris), m (20 effects, 7.3k tris + instanced points), l (30 effects, 12.4k tris + 8k instances). Measures frame_ms percentiles, tris count, CPU stage breakdown + GPU stage breakdown (from the GPU timer ring), memory RSS, and output RGBA8 hash. `anim=1` binds an LFO to the Twist, Camera and every effect, so the whole chain recooks each frame. `anim=0` swaps the one time-driven effect (Glitch reads `uTime`) for Emboss, so the chain caches after the first cook and `output_hash` is identical run to run. It measures the idle cost of a cached heavy patch, not render cost. `INFINITE_BENCH_B2GPUNODES=1` replaces the GPU `cook` stage with one GPU stage per node type (`render3d`, `bloom`, `gaussianblur`, ...), each the per-frame total over every instance of that type. |
| B3 Live performance | B1-lite+B2-lite+projector+MIDI+macros+Prediction; missed vsyncs, input-to-photon | **Yes** (`INFINITE_BENCH_B3`, `INFINITE_BENCH_B3LIVE`, `INFINITE_BENCH_B3SCALE`) | Composes B1-lite (8/16 voices) + B2-lite (animated compositing chain) + 1 projector window on Output + simulated MIDI note/CC injection + macro/gesture playback on Material + Prediction modulators (Drift, Moves, Predictive Modulator). Measures projector refresh/present interval percentiles/jitter stddev/missed vsync pct, audio load/xruns, input-to-photon latency (parameter twist injection to Output revision update), memory footprint and RSS. |
| B4 Complex 3D scenes | many objects/instancing/lights/shadows/materials/HDRI/ocean | **Yes** (`INFINITE_BENCH_B4SCALE`, `INFINITE_BENCH_B4SHADOW`, `INFINITE_BENCH_B4ANIM`, `INFINITE_BENCH_B4PASSES`) | One Render 3D at 1080p, 4x MSAA, ACES, straight into Output, with all four geometry slots busy: Ocean (resolution 96/160/256), cubes instanced on a sphere's faces (1k/5k/20k), a radial Array of metal tori (8/24/64), and a glass sphere (so the transmissive pass runs). Sun + point + spot light, a synthetic 1024x512 `.hdr` through the HDRI node, sun shadows at `off`/1024/2048/4096. `anim=1` plays the transport (the Ocean moves) and an LFO orbits the camera, bound by the slider's name at frame 4 because `Modulation::Bind` takes the UI's draw-order index. `anim=0` stops the transport, which otherwise runs from startup, so the scene caches and `output_hash` is stable. Reports `tris` as drawn (instances included) and `draw_calls`. `passes=1` splits Render 3D's GPU time into `r3d_shadow`/`r3d_opaque`/`r3d_transmissive`/`r3d_resolve`. |
| B5 Fundamentals | (a) empty patch, **(b) node-count scaling**, **(c) per-stage CPU+GPU split**, **(d) audio-thread-alone**, **(e) startup time**, **(f) load/save time**, **(g) undo-snapshot time** | **All of (a)-(g)** (`INFINITE_BENCH_B5EMPTY`, `INFINITE_BENCH_B5NODES`, `INFINITE_BENCH_B5STAGES`, `INFINITE_BENCH_B5AUDIOALONE`, `INFINITE_BENCH_B5STARTUP`, `INFINITE_BENCH_B5LOADSAVE`, `INFINITE_BENCH_B5UNDO`) | (a) zero-node floor, frame_ms percentiles + RSS, same 120-frame sampled window as (b) so the two are directly comparable. (b) 50/100/200/400 mixed nodes laid out on a grid (not stacked at origin - the flaw called out in benchmark-suite.md §2 against MIXEDSTRESSTEST/GEOMDENSITYTEST). Reports frame_ms percentiles + RSS. (c) same mixed-node grid as (b), wraps seven main-loop stages (`modulation`, `cook`, `node_bodies`, `editor_end`, `imgui_render`, `projectors`, `swap`) in `ConditionalStageTimer`s sampled over the same frame window, reports p50 CPU ms per stage into `stages_cpu_ms` (`stages_gpu_ms` still empty - blocked on the GPU timer ring below). (d) one Oscillator straight into Audio Out, no effects chain, buffer sweep 64/128/256/512 - isolates the audio callback's fixed per-block cost from B1's DSP-graph cost; reuses B1's wall-clock-window + `AudioLoadRing` pattern. (e) startup milestone timings from entry through first frame swap (`pre_window`, `window_gl`, `imgui_fonts`, `scanners_load`, `first_frame_render`, `total_to_first_frame`). (f)/(g) reuse (b)/(c)'s mixed-node grid (`INFINITE_BENCH_B5LOADSAVE=<n>`/`INFINITE_BENCH_B5UNDO=<n>`), fire once at `frameId==32`, and time the real patch I/O and undo paths back to back (`SavePatchTo`→`LoadPatchFrom`; `PushUndoCheckpoint`→`Undo`) via `Bench::ScopedStageTimer::NowMs()` - not synthetic serialize-only calls, so (f) includes whatever `ApplyPatchData`/field-graph remap does on load, and (g) includes the real `BuildPatchData`/`ApplyPatchData` round trip Undo takes. `stages_cpu_ms: {save, load}` / `{push_checkpoint, undo_restore}`. |
| B6 Canvas navigation | programmatic pan/zoom/drag, never OS-level UI scripting | No | "Missing today" per the doc; not started. |
| B7 Soak/thermal | 30min B3, long variant only | No | Depends on B3. |
| B8 Media I/O | video/camera/projector/Syphon-Spout | No | "Missing today" per the doc; not started. |
| B9 Memory footprint | B2/B4 at `l` scale | **Yes** (`INFINITE_BENCH_B9SCENE=b2\|b4`, `INFINITE_BENCH_B9FRAMES`, default 600) | Builds the B2 or B4 scene at scale l, animated, GPU timers off. Reports RSS and OS footprint (`phys_footprint` on macOS, `PrivateUsage` on Windows, VmRSS + VmSwap on Linux) at launch, after the build, at frames 32 and 152, and at the end. Also reports the peak over the whole run and a least-squares slope per 100 frames from frame 32. `gpu_est_mb` sums every texture, renderbuffer and buffer the GL wrappers allocated, split into textures, render targets, shadow maps, mesh buffers and instance buffers. Use footprint, not RSS, for leaks and headroom (see "Found while measuring"). |
| B10 Offline render/AV sync | Arrangement render of B3, realtime factor + drift | No | Depends on B3. |

**Bottom line: this session delivered the shared instrumentation (`BenchReport`/
`PercentileRing`/`AudioLoadRing`, `Platform::ProcessRssMb`/`HwModelString` on
all three platforms, `run_all.sh`, `compare.py`) plus four working fixtures
(B1, B5a, B5b, B5d), all four confirmed by an actual end-to-end run producing
valid `BENCH_JSON`.** B1 and B5d both hit the same real bug while being built:
their setup code indexed `gNodes[idx]` using `GraphNode::index` as if it were
a `gNodes` vector position. `index` is a stable id from a monotonically
increasing counter (`gNextIndex`), not a vector slot - it only coincides with
position when no node has ever been removed in the session, which was true in
every previously-existing fixture this pattern was copied near but not here
(some prior startup/fixture activity had already removed a node by the time
these ran). The out-of-bounds read into `gNodes[]` happened to return a
zero-initialized `GraphNode` whose `node` is a null `unique_ptr`, so
`static_cast<AudioOutputNode*>(nullptr)->input.Connect(...)` wrote through a
null `this` - `EXC_BAD_ACCESS` at a small offset, confirmed via `lldb`
(`x10 == 0x0` at the crashing `str` instruction). Fixed by re-resolving every
captured index through `FindNodeByIndex()` instead of `gNodes[idx]`, matching
the pattern already used correctly elsewhere in `main.cpp` (line ~66029). Both
fixtures now run clean, including B1's two-level Mixer-tree path (>12 voices)
and B5d's full setup/measurement/teardown sequence.

A second, separate bug was found and fixed in `run_all.sh` itself (not
`main.cpp`) while producing the first full baseline run: B5(b)'s node-count
sweep passed `INFINITE_EXITAFTER=152`, exactly matching the fixture's own
internal `frameId==152` sample-window boundary. `frameId` increments at the
*bottom* of the main loop, after the fixture's `frameId==152` check runs near
the top, so `EXITAFTER=152` closes the window one iteration before that check
ever fires - no `BENCH_JSON` line, ever, at any node count. Confirmed by
direct reproduction (`EXITAFTER=152` failed 3/3 runs, `153+` passed every
time). Fixed by bumping `run_all.sh`'s `EXITAFTER` for this sweep to 160,
matching the margin every other fixture in the script already carries. Since then B1(stages), B2, B3, B4, all of B5, and B9 have been
built (see the table above). B6/B7/B8/B10 are not built yet. Each
needs its own platform work first (a canvas-automation entry point,
soak automation, offline-render integration). Nothing below claims
coverage this suite doesn't have.

## What still needs building, and what it needs first

Build order (audio tier first, per the suite's standing priority - Audio >
Projector/Output > Canvas > Previews):

1. **B1(stages)** - per-DSP-stage audio callback breakdown. **Built** (`INFINITE_BENCH_B1VOICES`).
   Attributes B1 callback load across stages (`synths`, `filter`, `shaper`, `delay`, `reverb`, `dynamics`, `mixer`).
2. **B5(f)/(g)** - load/save time, undo-snapshot time. **Built**
   (`INFINITE_BENCH_B5LOADSAVE`, `INFINITE_BENCH_B5UNDO`).
3. **GPU timer query ring**. **Built** (`Bench::GpuTimerRing` in
   `BenchReport.h`). `GL_TIME_ELAPSED`, a 4-deep query ring per stage, polled
   without blocking at the top of each frame, drained with one `glFinish` when
   the report is written. It probes support once and reports nothing if timer
   queries are missing, so Windows/Linux llvmpipe CI degrades to an empty
   `stages_gpu_ms` rather than failing. It wraps `modulation`, `cook`,
   `node_bodies`, `editor_end` and `imgui_render` only. `projectors` is left
   out because the projector loop calls `glfwMakeContextCurrent`, and query
   objects are per-context. `swap` is left out because it submits no GPU work
   of its own. Feeds B2 and B5(c). Each ring entry sums every interval a
   stage records in one frame, so the same ring also does per-node timing:
   `Bench::NodeGpuRing()` is non-null only while B2 runs with
   `INFINITE_BENCH_B2GPUNODES`, and `FilterNode`/`Render3DNode` wrap their
   own draw after pulling inputs, so the intervals never nest.
4. **B2 heavy visuals**. **Built** (see table and baseline).
5. **B4 complex 3D scenes**. **Built** (`INFINITE_BENCH_B4SCALE`, `INFINITE_BENCH_B4SHADOW`, `INFINITE_BENCH_B4ANIM`, `INFINITE_BENCH_B4PASSES`).
6. **B9 memory footprint**. **Built** (`INFINITE_BENCH_B9SCENE=b2|b4`, `INFINITE_BENCH_B9FRAMES`).
7. **B3 live performance**. **Built** (`INFINITE_BENCH_B3`, `INFINITE_BENCH_B3LIVE`, `INFINITE_BENCH_B3SCALE`).
- **B6 canvas navigation**: needs a programmatic pan/zoom/drag entry point
  into the node editor (`ed::` calls) exposed to a self-test fixture - none
  exists today. `main.cpp`'s existing `gDroppedFiles`/`gDropPos` self-test
  pattern (see `codebase-navigation`'s living map) is the closest precedent
  for "drive real interaction state from a fixture, not OS-level clicks."
- **B8 media I/O**: needs decode/upload/present timing hooks in the video and
  camera paths, plus a way to open 2-3 real projector windows headlessly.
- **B7/B10**: each composes B1+B2/B3 (+ soak duration for B7, + offline render for B10).

## Baseline

Recorded: `bench/baselines/m2-8gb.jsonl`, committed from a clean
`scripts/bench/run_all.sh` run on this machine (`Mac14,7`, Apple M2, commit
`9326563`), 13 `BENCH_JSON` lines, zero `FAIL`s, zero xruns anywhere. Default
windows throughout (60s B1 per buffer size, 30s B5d per buffer size).

| Bench | Variant | frame_ms p50/p95/p99 (ms) | audio cb_load p50/p99 (%) | xruns | nodes | RSS (MB) |
|---|---|---|---|---|---|---|
| B1_heavy_audio | 24 voices, buf=64 | 16.7 / 31.2 / 47.2 | 59.3 / 69.4 | 0 | 149 | 398.8 |
| B1_heavy_audio | 24 voices, buf=128 | 16.7 / 33.7 / 38.4 | 58.2 / 65.0 | 0 | 149 | 369.5 |
| B1_heavy_audio | 24 voices, buf=256 | 16.7 / 32.5 / 59.9 | 58.0 / 62.3 | 0 | 149 | 330.5 |
| B1_heavy_audio | 24 voices, buf=512 | 16.7 / 32.5 / 75.3 | 57.1 / 60.3 | 0 | 149 | 370.1 |
| B5_fundamentals_empty | - | 1.00 / 1.61 / 1.67 | n/a | - | 0 | 256.8 |
| B5_fundamentals_nodecount | n=50 | 4.42 / 4.53 / 4.58 | n/a | - | 50 | 287.1 |
| B5_fundamentals_nodecount | n=100 | 7.86 / 8.07 / 8.13 | n/a | - | 100 | 342.0 |
| B5_fundamentals_nodecount | n=200 | 14.7 / 15.3 / 15.5 | n/a | - | 200 | 369.6 |
| B5_fundamentals_nodecount | n=400 | 28.6 / 37.5 / 39.9 | n/a | - | 400 | 367.8 |
| B5_fundamentals_audioalone | buf=64 | n/a (no video loop) | 1.39 / 1.70 | 0 | 2 | n/a |
| B5_fundamentals_audioalone | buf=128 | n/a | 1.37 / 1.52 | 0 | 2 | n/a |
| B5_fundamentals_audioalone | buf=256 | n/a | 0.85 / 1.11 | 0 | 2 | n/a |
| B5_fundamentals_audioalone | buf=512 | n/a | 2.44 / 2.72 | 0 | 2 | n/a |

B2 rows, recorded 2026-09-24 on the same machine from a direct fixture run on
`feature/gpu-timer-query-ring` (not yet in `bench/baselines/m2-8gb.jsonl`;
re-record the baseline with `run_all.sh` before comparing against it):

| Bench | Variant | frame_ms p50/p95/p99 (ms) | GPU `cook` p50 (ms) | CPU `swap` p50 (ms) | tris | nodes |
|---|---|---|---|---|---|---|
| B2_heavy_visuals | s, anim | 16.7 / 25.5 / 59.6 | 12.7 | 10.8 | 1800 | 18 |
| B2_heavy_visuals | m, anim | 32.9 / 34.8 / 35.8 | 24.4 | 23.4 | 7308 | 32 |
| B2_heavy_visuals | l, anim | 46.8 / 50.2 / 51.6 | 36.0 | 33.7 | 12396 | 42 |
| B2_heavy_visuals | s, static | 4.0 / 5.7 / 7.6 | 0.16 | 2.2 | 1800 | 17 |
| B2_heavy_visuals | m, static | 4.1 / 7.1 / 7.7 | 0.14 | 2.3 | 7308 | 31 |
| B2_heavy_visuals | l, static | 4.5 / 7.0 / 7.6 | 0.13 | 2.4 | 12396 | 41 |

## Top costs per benchmark

- **B1** (24-voice heavy audio):
  - **Frame time**: Post-UI optimization, locked at 16.67ms (60 FPS vs previous 74.7ms / 13 FPS).
  - **Audio callback per-DSP-stage breakdown (B1 stages)**:
    - **`reverb`**: **39.42%** callback load (**67.9%** of entire DSP work). With 24 voices each having an inline Reverb instance, reverb dominates the audio thread cycles.
    - **`synths`** (Sampler / Wavetable / Oscillator): **5.07%** callback load (8.7% of total).
    - **`shaper`** (Wavetable Shaper / Drive / Bitcrush): **3.80%** callback load (6.5% of total).
    - **`filter`** (Audio Filter): **3.38%** callback load (5.8% of total).
    - **`dynamics`** (Compressor / Limiter): **2.96%** callback load (5.1% of total).
    - **`delay`** (Delay): **2.87%** callback load (4.9% of total).
    - **`mixer`** (Mixer tree): **0.56%** callback load (1.0% of total).
  - **Actionable Takeaway**: Future DSP SIMD/threading or algorithmic optimization should prioritize `ReverbNode` (68% of compute) before any other audio node.
  - **Acted on**: `ReverbKernel`'s 16-line FDN is now vectorized (NEON on
    arm64, SSE2 on x86_64, portable scalar fallback) - `feature/reverb-
    simd-vectorization`, commit `a518103`. Out of scope for this
    measure-only suite to build, but tracked here since it's the direct
    output of this benchmark's B1(stages) finding. DSPTEST's SIMD-vs-scalar
    numerical-equivalence check passed (max diff 2.98e-08, tol 1e-5).
    Clean re-run on 2026-09-24 (`20260924-064959-e04b7a6`, no indexer
    running, load average 3.4-4.9 from other apps):

    | buffer | cb_load p50 baseline | cb_load p50 now | reverb share now | xruns |
    |---|---|---|---|---|
    | 64 | 57.4% | 51.4% | 31.8% | 0 |
    | 128 | 56.8% | 50.1% | 31.4% | 0 |
    | 256 | 56.5% | 50.2% | 31.7% | 2 |
    | 512 | 56.3% | 49.0% | 31.2% | 0 |

    Reverb fell from 39.4% to ~31.5% of the callback. The earlier 8-68
    xruns came from the indexer; 2 xruns remain at buf=256 on a busy but
    indexer-free machine, so that row stays under watch. B1's UI frame p50
    also fell from ~75 ms to ~17 ms, from `1d19afa`'s UI hot-spot work.
- **B2** (heavy visuals, animated): GPU-bound at every scale. GPU `cook`
  (all node renders) is 12.7 / 24.4 / 36.0 ms at s / m / l, and the CPU
  `swap` stage roughly equals it, because swap is where the CPU waits for
  the GPU. CPU `cook` stays at 0.7-5.4 ms. The m and l scales cannot hold
  60 fps on a base M2. The per-node split (`gpunodes=1`) answers where it
  goes. At l-anim, of 33.8 ms: `bloom` 14.8, `gaussianblur` 7.8,
  `diffuseglow` 7.6 (three instances each), every other effect 0.1-0.5, and
  `render3d` only 1.0 (12.4k tris + 8k instances). **Bloom, Gaussian Blur and
  Diffuse Glow are ~92% of B2's GPU time** at s and l alike. All three are
  single-pass 2D kernels at full resolution: Bloom 11x11 = 121 texture
  reads/pixel, Blur and Glow 9x9 = 81. Their weights are
  `exp(-(x²+y²)/k)`, which splits exactly into two 1D passes (22 and 18
  reads). Bloom's bright-pass runs per sample before weighting, so it
  splits too. That needs multi-pass support in `FilterNode` and a 16F
  intermediate so `output_hash` only moves by float rounding.
  **Acted on** (`feature/filter-gpu-easy-wins`): `FilterDef::prePassBody`
  adds an optional horizontal pass into an RGBA16F intermediate (`uPass`), and
  all four blur-family defs (`gaussianblur`, `boxblur`, `bloom`,
  `diffuseglow`) now use it. B2 with `gpunodes=1`, GPU ms/frame, M2:

  | Scale (anim) | effects before | effects after | frame p50 before | frame p50 after |
  |---|---|---|---|---|
  | s | 10.8 | 2.8 | 19.7 | 9.8 |
  | l | 32.7 | 8.6 | 49.7 | 21.4 |

  Output check (`INFINITE_BENCH_DUMPRGBA` + `scripts/bench/rgbadiff.py`,
  static variants against the pre-change build): s differs in 0.3% of bytes,
  all by 1 step of 8 bits. m/l differ by up to 177 steps in 5-8% of bytes,
  but only because Threshold and Posterize sit downstream and turn a 1-step
  rounding change at their cut-off into a full jump. A 32-bit intermediate
  (tried and reverted) makes s bit-identical and m differ in 64 bytes by 1,
  which proves the two-pass math is exact. It costs 12.7 ms instead of 8.6 at
  l, so 16-bit was kept. The FilterNode refactor on its own (same defs, new
  code path) reproduced all three old static hashes exactly.
- **B5(b)** (node-count scaling): near-linear from 50 to 200 nodes (4.4ms to
  14.7ms, roughly 3.3x for 4x the nodes), then super-linear at 400 (28.6ms
  p50, 39.9ms p99 - the p95/p99 spread widens sharply too, 37.5/39.9 vs a
  tight 15.3/15.5 at n=200).
  The clean 2026-09-24 run reads 4.6 / 8.2 / 15.7 / 33.9 ms p50, so n=400 is
  +18% against the baseline and `compare.py` flags all four p99s. It is
  **not** from `e04b7a6`: alternating runs of that build and its parent
  `c3e5a6c` at n=400 give 33.2-35.5 against 33.4-33.8 ms. The fixture's
  Gaussian Blur nodes are unconnected, so the two-pass change never runs.
  The gap is either machine load (3-5 now, idle at baseline) or an earlier
  commit (`1d19afa`, `a518103`, `0aa8d46`). Settle it with an idle-machine
  bisect before re-recording `m2-8gb.jsonl`.
- **B5(d)** (audio-thread-alone, one Oscillator, no effects): cb_load is
  under 3% at every buffer size, confirming B1's ~57-60% load is almost
  entirely the 24-voice effects chain, not fixed per-callback overhead.
- **B5(f)/(g)** (load/save, undo), swept on the clean run, ms:

  | n | save | load | undo push | undo restore |
  |---|---|---|---|---|
  | 50 | 3.14 | 1.93 | 0.35 | 0.77 |
  | 100 | 1.32 | 1.60 | 0.45 | 0.89 |
  | 200 | 1.44 | 3.14 | 0.51 | 1.02 |
  | 400 | 2.19 | 10.22 | 0.90 | 1.76 |

  Save and undo scale about linearly and stay far under a frame. **Load is
  super-linear**: 3.3x going from 200 to 400 nodes, the same knee B5(b)
  hits at n=400. At 400 nodes 10 ms is still invisible to a user, but the
  curve points at an O(n²) step in load (likely a per-node lookup across
  all nodes). Single samples only; n=50's save is a cold-start outlier.

- **B4** (complex 3D), M2, `gputimers=0` except the `passes=1` rows:

  | Variant | tris | frame p50 | frame p99 | CPU `node_bodies` |
  |---|---|---|---|---|
  | s, shadow 2048 | 144k | 4.0 | 9.1 | 3.0 |
  | m, shadow 2048 | 540k | 9.2 | 14.1 | 8.2 |
  | l, shadow 2048 | 1.57M | 22.4 | 26.7 | 21.2 |
  | m, shadow off / 1024 / 4096 | 540k | 9.1 / 9.2 / 9.3 | 13.5 / 13.5 / 15.9 | 8.1-8.2 |
  | l, static (cached) | 1.57M | 1.3 | 6.1 | 0.4 |

  GPU split (`passes=1`, ms/frame): s opaque 2.2, shadow 0.6, glass 0.16;
  l opaque 10.7, shadow 1.5, glass 0.14. MSAA resolve is under 0.05.
  Every scale is **CPU-bound, not GPU-bound**: at l the GPU needs ~12.5 ms
  but the frame takes 22.4, and `node_bodies` (21 ms) is almost all of it.
  A `sample` profile puts the bulk of it in
  `OceanNode::RebuildIfNeeded` → `MeshOps::Ocean` → `RecalculateNormals` →
  `BuildWeldMap`, a `std::map` keyed on quantized positions, rebuilt every
  frame while the transport plays. The ocean is one shared grid with no
  seams, so the weld finds nothing to merge. Summing face normals straight
  over the index buffer would give the same result without the map. It
  runs from Ocean's mini-viewport preview only because that is the first
  caller each frame. Shadow quality costs almost nothing (1.5 ms GPU at
  4096, hidden behind the CPU), and so do 20k instances (one instanced
  draw). Draw calls stay at 4 because Render 3D has four slots and every
  slot is one draw.
  **Acted on** (`feature/ocean-normals-no-weld`): `MeshOps::Ocean` now
  accumulates face normals directly across the index buffer and normalizes
  per vertex without calling `RecalculateNormals` / `BuildWeldMap`. Since
  the ocean is a single continuous indexed grid with no coincident duplicate
  vertices or hard seams, the output normals are mathematically equivalent
  (max diff 0.00e+00, verified in `INFINITE_PATHOCEANTEST` across resolutions
  16/96/256 and chop 0..1.2) while eliminating the O(V log V) `std::map` allocations.
  At scale `l` (256 res, 1.57M tris drawn), CPU `node_bodies` drops from
  ~39.6 ms to ~3.5 ms (~11x speedup), and frame p50 drops from ~43.7 ms to
  ~13.4 ms on Apple M2 (`INFINITE_BENCH_B4SCALE=l INFINITE_BENCH_B4SHADOW=2048 INFINITE_BENCH_GPUTIMERS=0`):

  | Run | frame p50 before | frame p50 after | frame p99 before | frame p99 after | node_bodies before | node_bodies after |
  |---|---|---|---|---|---|---|
  | 1 | 42.2 ms | 12.8 ms | 98.9 ms | 20.3 ms | 38.6 ms | 3.7 ms |
  | 2 | 40.6 ms | 12.9 ms | 135.7 ms | 25.4 ms | 36.8 ms | 3.8 ms |
  | 3 | 48.4 ms | 14.6 ms | 105.9 ms | 28.5 ms | 43.4 ms | 3.1 ms |
  | **Avg** | **43.7 ms** | **13.4 ms** | **113.5 ms** | **24.7 ms** | **39.6 ms** | **3.5 ms** |

  The "before" column looks inflated. The clean B4 run before this change
  read 22.4 ms p50 and 21.2 ms `node_bodies` at the same settings, so the
  realistic gain is ~22 to ~13-14 ms p50 and ~21 to ~3.5 ms `node_bodies`.
- **B9** (memory footprint, scale l, animated, 600 frames, M2 8 GB). First run,
  RSS only, before `footprint_*` existed:

  | Scene | tris | draw calls | RSS start (frame 2) | RSS built | RSS end | RSS slope /100f | GPU est. | frame p50/p99 |
  |---|---|---|---|---|---|---|---|---|
  | b2 | 12,396 | 2 | 148.5 MB | 280.8 MB | 74.2 MB | -20.3 MB | 471.7 MB (render targets 469.6, mesh 2.1) | 20.6 / 29.3 ms |
  | b4 | 1.57M | 4 | 362.7 MB | 313.3 MB | 133.6 MB | -4.0 MB | 125.3 MB (render targets 90.9, mesh 16.7, shadow 12.0, textures 4.0, instances 1.6) | 17.3 / 27.0 ms |

  That run reported a peak below the built reading (b2: 155 < 281), because
  the peak started at frame 2 and never saw the build. Fixed: `start` is now
  process launch and the peak covers launch, build and every frame. The
  follow-up check (200 frames, indexer running, so only a sanity check)
  shows why footprint had to replace RSS:

  | Scene | RSS built | RSS end | footprint built | footprint end | footprint slope /100f |
  |---|---|---|---|---|---|
  | b2 | 271 MB | 62 MB | 245 MB | 901 MB | +3.5 MB |
  | b4 | 279 MB | 100 MB | 287 MB | 704 MB | +5.0 MB |

  Footprint is 0.7-0.9 GB and includes GPU allocations, which share memory
  with the CPU on Apple silicon. That is ~9-11% of an 8 GB machine for one
  heavy patch. The small positive slope needs a clean 600-frame run on an
- **B3** (Live performance fixture: B1-lite 8 voices + B2-lite 10 effects +
  projector window + MIDI + gesture/macro + 3 Prediction modulators, buffer 256).

  **The first recorded B3 baseline and audio ladder are void.** Three fixture
  faults made every number meaningless:

  | Fault | Effect on the old numbers | Fix |
  |---|---|---|
  | Audio engine never started (stale saved output device, -10875) | Every audio row read 0 xruns / 0.00% load, a vacuous PASS; the whole ladder is empty | Bench forces the system default device; a stopped engine now emits `audio: null` and no audio targets |
  | Projector opened on top of the canvas | Canvas occluded, so its vsync stopped pacing the loop; frame and projector numbers are unpaced | Canvas and projector are laid out side by side on the primary display, and the canvas is focused |
  | Canvas behind another app | Same as above | The run is tagged `,unfocused=1` in `variant` (with a stderr line) if the canvas ever loses focus |

  Target changes: `input_to_photon_le_2_frames` now needs ≥20 samples and
  **max** ≤2 frames (was p50). Zero samples is `input_to_photon_frames: null`,
  never 0. The dry run reports `i2p_dryrun_no_false_samples` in place of the
  latency target, so a probe self-check can no longer read as a latency PASS.

  Verification runs after the fix (300 frames, scale s, load avg 5.5, **not a
  baseline**):

  | Run | variant | frame p50 | swap stage p50 | audio p99 / xruns | i2p p50 / max (n) |
  |---|---|---|---|---|---|
  | canvas focused for frames 2-205 | `scale=s,unfocused=1` | 15.9 ms | 2.0 ms | 37% / 0 | 1.0 / 1.0 (13) |
  | canvas behind Safari throughout | `scale=s` (before the flag existed) | 10.6 ms | 1.7 ms | 37% / 0 | 1.0 / 1.0 (13) |
  | dry run | `scale=s,i2pdryrun=1` | 10.1 ms | - | 37% / 0 | null, dry-run check PASS |

  With the canvas in front, the loop paces at 60 Hz (15.9 ms). Behind another
  app it runs free at ~10.5 ms. 300 frames give only 13 i2p samples, so the
  i2p target needs the default 600 frames. Still to do: record the baseline
  and re-run the ladder with 600 frames, on an idle machine, with Infinite
  left in front for the whole run.

## Found while measuring

Per §8: this suite measures, it does not fix. Anything found while building
a fixture goes here, not into a code change.

- Harness bugs, fixed as part of this suite: a null-`this` crash in the
  B1/B5d fixture setup, and an `EXITAFTER` fencepost bug in `run_all.sh`'s
  B5(b) sweep (both described above). B2's first version had a static variant
  whose `output_hash` changed every run because Glitch reads `uTime`. Fixed
  by swapping Glitch for Emboss in the static variant.
- **B3 Input-to-photon latency revision churn**: The original probe logic reported
  2.0 frames every run because the animated scene kept changing the Output texture
  revision on every single frame, making any probe appear immediately acknowledged on
  the next frame. Fixed by pausing LFO/Prediction/Gesture drivers around each probe
  and swapping time-driven Glitch for Emboss in B3 visuals. Dry run
  (`INFINITE_BENCH_B3I2PDRYRUN=1`) verified 0 false detections.
- **Projector presents are unpaced (product).** `OpenProjectorWindow` sets
  the projector context to swap interval 0 ("the main window owns vsync"), so
  the only thing pacing projector output is the canvas window's own vsync.
  macOS stops vsync-blocking a swap on an occluded window. So when the canvas
  is covered (by the projector itself on a single display, which is where it
  opens by default at main-window pos + 60, or by any other app), the whole
  loop and the projector run unpaced: ~10 ms frames on a 60 Hz panel, with
  tearing and wasted GPU. This is a live-performance risk on single-display
  setups. Not fixed here.
- **A stale saved output device silences audio (product).**
  `Infinite.audio-settings` stores the device as a raw CoreAudio
  `AudioObjectID`. Those IDs are reassigned when a device is replugged (the
  headphones here went from 108 to 117). `AudioDeviceOpen`'s comment says an
  invalid ID "simply fails ... and the output AudioUnit is left on ... the
  system default". In fact `startAndReturnError` fails with
  `kAudioUnitErr_FailedInitialization` (-10875), and the engine does not
  start at all. The user gets no audio after replugging headphones until they
  re-pick the device. The bench now forces device 0 (system default) without
  touching the saved setting. Not fixed here.
- **Windows MIDI Injection Audit**: `Platform::MidiInjectBytes` in `src/platform/win/MidiWin.cpp`
  packs `data[0] | (data[1]<<8) | (data[2]<<16)` into `DWORD_PTR param1`, exactly matching
  WinMM's `MIM_DATA` callback structure. All message routing goes through `HandleShortMessage`
  guarded by `gState.mutex` and `gState.ringMutex`, ensuring thread safety when invoked from
  the benchmark runner. (Audited statically; cannot run on macOS).
- A time-driven filter (Glitch, Add Noise, Displace, Liquify) recooks every
  node below it every frame. B2 l-static went from 28 ms to 4.5 ms p50 when
  its three Glitch nodes were replaced. While the transport plays that is
  correct, because the image really changes. While it is stopped it was
  waste: `FilterNode` skipped its cache for any `uTime` filter, even though
  transport time only advances while playing. **Fixed** on
  `feature/filter-gpu-easy-wins`: the uploaded time value is now part of
  `FilterNode::Signature`, so a stopped transport caches like any other patch.
- `mem.rss_mb` at the end of a run is often *lower* than at the start on this
  8 GB machine (for example B2 s-anim 208 to 52 MB). macOS compresses and
  pages out memory under pressure, so RSS is not a reliable footprint number
  here. B9 now reports `phys_footprint` (`task_vm_info`) as `footprint_*`. At
  B2 l, RSS reads 62 MB while the OS charges the app 901 MB.
- **B2's render targets take 470 MB** (B9, scale l): about 28 full 1080p
  16-bit buffers, roughly one per effect, plus the two-pass blur
  intermediates. Effects keep their own full-size output buffers and don't
  share or reuse them, so memory grows linearly with chain length. That
  matters most on the 8 GB machines. Not fixed (measure only).
- **GPU timer queries distort `frame_ms` on macOS.** Apple's Metal-backed GL
  makes `glEndQuery(GL_TIME_ELAPSED)` flush the context and block until the
  GPU catches up (`sample`: `glEndQuery_Exec` → `flushContext` →
  `semaphore_wait`). That removes CPU/GPU overlap. B4 l-anim runs at 43 ms
  p50 with timers and 23-25 ms without, and the CPU `swap`/`imgui_render`
  stages shrink from 15/2.4 ms to 0.5/0.07. Every B2 `frame_ms` above was
  recorded with timers on, so B2's absolute frame times are inflated. Its
  before/after comparisons still hold, because both sides were timed the
  same way. `INFINITE_BENCH_GPUTIMERS=0` now turns the queries off and adds
  `gputimers=0` to the variant. B4's sweeps use it, and only the `passes=1`
  run keeps timers on. B2's runs still time the GPU; a timers-off B2 sweep
  is needed before its frame numbers go in a new baseline.
- **HDRI background seam** (visual bug, not perf): Render 3D's env-background
  shader samples the equirect map with `texture()`, so at the `atan` wrap,
  where u jumps from 1 to 0, the implicit derivatives select the smallest mip
  and draw a one-pixel line of the image's average colour straight up the
  sky. It showed in B4's first render. The fix is to take the u derivative
  from whichever of `fract(u)` and `fract(u + 0.5)` is continuous at that
  pixel (`textureGrad`), or `textureLod` at 0 for the background. The
  reflection path already uses `textureLod`.
- The transport starts playing at launch, so any fixture that wants a still
  frame has to stop it. B2's static variant does not, which is harmless only
  because Emboss replaced Glitch and nothing else in B2 reads time.
- `.git/hooks/post-commit` starts `tools/semi-brain/4_engine/sync_brain.py
  --sync` in the background after every commit. It uses ~4 cores for ~5
  minutes, and B1 runs during it showed 8-68 xruns instead of 0-2. Never run
  `run_all.sh` right after a commit: check that no `sync_brain` process is
  running first.

## Windows/Linux

`Platform::ProcessRssMb()`/`HwModelString()` have implementations on all
three platforms (macOS via `task_info`/`sysctlbyname`, Windows via
`GetProcessMemoryInfo`/registry `SystemProductName`, Linux via
`/proc/self/status`/`/sys/devices/virtual/dmi/id/product_name`) and
`BenchReport.cpp` is added to `COMMON_SOURCES` in `CMakeLists.txt`, so B1 and
B5(a)/(b)/(d) should compile on Windows/Linux CI. Not verified by an actual CI
run as of this writing - the fixtures were only run on this macOS machine.
GPU-timer numbers, once the query ring exists, should be marked "not
meaningful" on Windows/Linux CI per benchmark-suite.md §5.6 (no real GPU
there).
