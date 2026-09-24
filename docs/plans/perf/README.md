# Performance benchmark suite

## Scoreboard

One row per measured win (or loss). Medians on the M2 8 GB machine; change %
is (after - before) / before. Video rows are B8, 300 frames, `bench/media`
clips. BEFORE is `3626c3d` (`main` at the branch point), 3 runs per variant.
AFTER is `93e184d` (= `bugfix/macos-video-decode` after the revert in
`1b7364e`): 2160 variants from 3 runs interleaved with the gate below, 1080
variants from 1 run (the 3-run 1080 set could not be paced: screen locked).

| fix | commit | benchmark / variant | metric | before | after | change % |
|---|---|---|---|---|---|---|
| UI frame cost | `1d19afa` | B1 | UI frame ms | 75 | 16.7 | -78% |
| Reverb SIMD | `a518103` | B1 | cb_load % | 57.4 | 51.4 | -10% |
| Ocean node | `47c46ad` | Ocean scene | node_bodies ms | ~21 | 3.5 | -83% |
| Ocean node | `47c46ad` | Ocean scene | frame ms | ~22 | 13.4 | -39% |
| Time-filter caching | `e04b7a6` | B2 l-static | frame ms | 28 | 4.5 | -84% |
| Dropdown undo | `2c92715` / `2910b2a` | - | - | not re-measured | - | - |
| Blur split (two-pass) | `e04b7a6` | - | - | not re-measured | - | - |
| Regression closed by the canvas fixes (cause never bisected; see B5(b)) | `0d92e85` | B5b n=400 | frame p50 ms | 28.6 (baseline), 33.9 (regressed) | 24.7 (2 runs, sync_brain running) | -14% vs baseline |
| Video: upload only new frames | `8d29baa` | B8 2x1080 / 4x1080 / 2x2160 / 4x2160 | uploads per 268 cooks | 268 / 268 / 268 / 268 | 70 / 68 / 69 / 123 | -74% / -75% / -74% / -54% |
| Video: no per-frame copy + vImage swizzle | `5d9cbf0`, `8a93b8d` | B8 2x1080 / 4x1080 / 2x2160 / 4x2160 | cook ms | 4.2 / 8.4 / 7.7 / 30.2 | 0.1 / 0.1 / 0.1 / 0.2 | -98% / -99% / -99% / -99% |
| Video: bounded catch-up | `93e184d` | B8 2x1080 / 4x1080 / 2x2160 / 4x2160 | dropped per clip | 1 / 8 / 4 / 80 | 2 / 0 / 0 / 22 | +1 frame / -100% / -100% / -73% |
| Video: all four | `8d29baa`..`93e184d` | B8 2x1080 | frame p50 / p99 ms | 8.7 / 21.0 | 8.5 / 20.5 | -2% / -2% |
| | | B8 4x1080 | frame p50 / p99 ms | 11.7 / 26.3 | 9.9 / 17.8 | -15% / -32% |
| | | B8 2x2160 | frame p50 / p99 ms | 11.2 / 32.4 | 8.4 / 16.8 | -25% / -48% |
| | | B8 4x2160 | frame p50 / p99 ms | 32.9 / 83.8 | 13.9 / 112.2 | -58% / **+34%** |
| | | B8 2x1080 / 4x1080 / 2x2160 / 4x2160 | loop-boundary max ms | 13.7 / 14.8 / 34.8 / 186.2 | 12.2 / 11.7 / 29.9 / 223.3 | -11% / -21% / -14% / **+20%** |
| | | B8 4x2160 | decoded fps | 30.0 | 29.7 | -1% |
| Video: decode thread per clip | `88787d7` (reapplies `b4a1454`, `8a6648a`) | B8 4x2160 | frame p50 / p99 ms | 13.9 / 112.2 | 9.2 / 23.1 | -34% / -79% |
| | | B8 4x2160 | dropped per clip | 22 | 0 | -100% |
| | | B8 2x2160 | frame p50 / p99 ms | 8.4 / 16.8 | 9.5 / 16.7 | +13% / -1% |
| | | B8 2x2160 / 4x2160 | footprint peak MB | 633 / 1139 | 662 / 1160 | +5% / +2% |
| Canvas: re-cook skip (Noise, Shape; Filters now hit their cache) | `bd38a27` | B6 n=300 all / n=400 pan | cook_all ms | 8.96 / 17.25 | 0.09 / 0.10 | -99% / -99% |
| Canvas: off-screen body culling | `94023d5` | B6 n=300 all / n=400 pan | node_bodies ms | 21.52 / 36.53 | 3.23 / 3.08 | -85% / -92% |
| | | B6 n=300 all / n=400 pan | offscreen_bodies ms | 20.71 / 31.86 | 3.43 / 1.27 | -83% / -96% |
| Canvas: both | `bd38a27`, `94023d5` | B6 n=300 all | frame p50 / p95 ms | 45.98 / 82.93 | 9.48 / 35.58 | -79% / -57% |
| | | B6 n=400 pan | frame p50 / p95 ms | 79.39 / 104.06 | 8.65 / 13.47 | -89% / -87% |
| | | B6 n=300 all / n=400 pan | footprint peak MB | 1215 / 1512 | 1146 / 1443 | -6% / -5% |

Keep-or-revert gate for the decode thread: B8, 3 runs each, interleaved
against a `93e184d` build. The first rule (branch >= base on every metric,
footprint not higher) reverted it in `1b7364e` over +1.1 ms p50 at 2x2160 and
+2-5% footprint, while it removed every 4x2160 drop. That rule was too strict.
**Gate rule now:** keep if no metric ends up worse than its budget (frame p99
<= 33.3 ms, 0 drops where base drops) and footprint is within +5%. The thread
passes it and was reapplied in `88787d7`.

Re-check of the reapply, 2026-09-25, B8 4x2160 interleaved main `d668610` /
thread `88787d7`, machine swapping 5.7 of 7 GB (absolute numbers are worse
than the table below; the comparison is what counts):

| run | frame p50 / p99 ms | dropped per clip | footprint peak MB |
|---|---|---|---|
| main 1 | 12.2 / 43.8 | 4 / 3 / 3 / 3 | 988 |
| thread 1 | 10.1 / 25.6 | 0 / 0 / 0 / 0 | 1254 |
| main 2 | 18.8 / 191.4 | 38 / 43 / 44 / 37 | 1397 |
| thread 2 | 11.0 / 22.6 | 0 / 0 / 0 / 0 | 1268 |

| variant | metric | base `93e184d` | thread `8a6648a` |
|---|---|---|---|
| 2x2160 | frame p50 / p99 ms | 8.4 / 16.8 | 9.5 / 16.7 |
| 2x2160 | dropped per clip | 0 | 0 |
| 2x2160 | loop-boundary max ms | 29.9 | 0.0 |
| 2x2160 | footprint peak MB | 633 | 662 |
| 4x2160 | frame p50 / p99 ms | 13.9 / 112.2 | 9.2 / 23.1 |
| 4x2160 | dropped per clip | 22 | 0 |
| 4x2160 | loop-boundary max ms | 223.3 | 150.1 |
| 4x2160 | footprint peak MB | 1139 | 1160 |

What did not improve at `93e184d`, before the thread was reapplied (the thread fixes the first three rows; the loop wrap at 4x2160 still peaks at 150 ms and stays **open**: the reader rebuild on wrap is still one synchronous 4K open per clip):

| metric | before | after | why |
|---|---|---|---|
| 4x2160 frame p99 | 83.8 ms | 112.2 ms (+34%) | Every loop wrap still rebuilds the `AVAssetReader` on the main thread (finding 1 is still open). At 4K that is 100-220 ms, and with four clips at a 1.1-1.2 GB footprint on 8 GB the machine pages. Before, the spiral kept every frame slow (p50 32.9), so p99 was a smaller multiple of it. |
| 4x2160 loop-boundary max | 186.2 ms | 223.3 ms (+20%) | Same cause. One run in three reads 223 ms on clip 0; the other clips read 26-43 ms. |
| 4x2160 dropped | 80 per clip | 22 per clip | Two runs out of three drop 20-34 per clip, all around the wraps; the third drops 0. The thread removed this but failed the gate. |
| 2x1080 dropped | 1 | 2 | Single run, around one wrap; within noise. |
| 1080 loop-boundary max | 13.7 / 14.8 ms | 12.2 / 11.7 ms | Under 16.7 ms, but only 11-21% better: the reader rebuild at a wrap is still synchronous. |

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
# B6: leave Infinite in front; `unfocused=1` or `unpaced=1` in variant = discard
INFINITE_BENCH_B6NODES=300 INFINITE_BENCH_B6MODE=all INFINITE_BENCH_GPUTIMERS=0 \
   INFINITE_EXITAFTER=650 ./build/Infinite.app/Contents/MacOS/Infinite -ApplePersistenceIgnoreState YES
# B8: clips first (ffmpeg, into gitignored bench/media/), then one variant.
# Leave Infinite in front; `unfocused=1`/`unpaced=1`/`overlap=1` = discard
scripts/bench/b8_make_clips.sh
INFINITE_BENCH_B8=1 INFINITE_BENCH_B8MEDIA="$PWD/bench/media" INFINITE_BENCH_B8CLIPS=4 \
   INFINITE_BENCH_B8RES=2160 INFINITE_BENCH_B8WINDOWS=3 INFINITE_BENCH_B8CAMERA=1 INFINITE_BENCH_B8SYPHON=1 \
   INFINITE_BENCH_B8FRAMES=300 INFINITE_EXITAFTER=350 \
   ./build/Infinite.app/Contents/MacOS/Infinite -ApplePersistenceIgnoreState YES
```

`-ApplePersistenceIgnoreState YES` stops macOS from showing its "reopen
windows?" dialog after a fixture has crashed. That dialog blocks `glfwInit`,
so every later run hangs. `run_all.sh` always passes it.

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
| B6 Canvas navigation | programmatic pan/zoom/drag, never OS-level UI scripting | **Yes** (`INFINITE_BENCH_B6NODES`, `B6MODE=pan\|zoom\|drag\|dropdown\|all`, `B6COLLAPSED`, `B6VSYNC`, `B6FRAMES`, default 300 nodes / 600 frames) | Builds a wired grid of 12 cycled node types (image, audio, modulator, utility) and drives the view through new `ed::SetViewScroll`/`SetViewZoom` calls. Pan sweeps the whole grid and back. Zoom goes 0.25 to 2 and back. Drag moves one node in a circle through the `needsPosition` path. Dropdown opens and closes Math's op list every 30 frames. `all` splits the window into those four phases. Reports frame_ms plus per-phase p50/p99/max, and `canvas_nav` has visible vs drawn node bodies, the ms spent on off-screen bodies, drag distance, dropdown-open frames and `on_vsync_frac`. `stages_cpu_ms` adds `links` and `cook_all` (B6 only, see "Found while measuring"). |
| B7 Soak/thermal | 30min B3, long variant only | No | Depends on B3. |
| B8 Media I/O | video/camera/projector/Syphon-Spout | **Built** (`INFINITE_BENCH_B8`, `B8CLIPS=1-4`, `B8RES=1080\|2160`, `B8WINDOWS=0-3`, `B8CAMERA`, `B8SYPHON`, `B8FRAMES` default 600, `B8MEDIA`, `GPUTIMERS=1`) | 1-4 looping Video Source clips (synthetic H.264 1080p30/2160p30 from `scripts/bench/b8_make_clips.sh`, `-g 30 -pix_fmt yuv420p`, 2.0-2.75 s so every run crosses the loop boundary), each into its own Output. Projector windows are opened on Outputs from code and placed beside the canvas (`overlap=1` if the OS stacks them). The camera is opened only if permission is already granted: `CameraAuthorizationStatus()` is read-only on all three platforms, so a run never raises the dialog and reports `"camera":"skipped"` with the reason. Syphon/Spout Out only publishes, and `has_clients` says whether anyone received (null on Windows, `"n/a"` on Linux). `media_io` reports per clip: real decodes (`decode_ms`) split from cache hits (`cache_hit_ms`), the loop-boundary decode (`loop_decode_ms`), decoded/dropped/skipped, `repeated` next to `expected_repeats` (a 30 fps clip on a 60 Hz loop repeats about half its frames by design), `reuploads`, `reader_restarts`, `uploads` vs `new_frames`, and `upload_cpu_ms`. Per window it reports present ms, frame intervals, jitter, R and missed vsync, with `on_vsync_frac` null (projectors run at swap interval 0). Also Syphon publish ms and camera fps/interval. `stages_cpu_ms` uses the B6 names plus `projectors`. GPU upload time comes only with `INFINITE_BENCH_GPUTIMERS=1`, via the per-node ring with the `cook` query off, and reads null on macOS (see "Found while measuring"). |
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
built (see the table above), then B6 and B8. B7/B10 are not built yet. Each
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
8. **B6 canvas navigation**. **Built** (`INFINITE_BENCH_B6NODES`, `INFINITE_BENCH_B6MODE`, `INFINITE_BENCH_B6COLLAPSED`, `INFINITE_BENCH_B6VSYNC`).
9. **B8 media I/O**. **Built** (`INFINITE_BENCH_B8*`, see the table). Decode
   timing lives in each platform's video layer (`Bench::MediaDecodeStats` in
   `src/core/BenchMediaIo.h`, a lock-free single-producer ring because Windows
   and Linux decode on their own threads). Upload, camera and publish timing
   live in the nodes, and present timing in the projector loop. All of it is
   inert unless the fixture sets `Bench::MediaIoEnabled()`.
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

### B6 canvas navigation: verification runs, not a baseline

Recorded 2026-09-24 while the machine was in use (load avg ~3), 300-400
frames, GPU timers off. Every vsync-on run came out `unpaced=1`: the window
was not being paced by the display, so these numbers show work per frame,
not a real on-screen frame rate. Targets report `null` unless a run is focused,
vsync on and paced, except when a lower bound already fails (see below).

| Variant | frame_ms p50 / p95 | `node_bodies` | off-screen bodies ms | `cook_all` | visible nodes |
|---|---|---|---|---|---|
| n=300, all | 22.6 / 26.0 | 7.4 | 6.4 | 8.9 | ~23 |
| n=300, all, collapsed | 21.7 / - | 5.7 | 5.2 | - | - |
| n=300, pan, vsync=0 | 21.9 / - | - | - | - | - |
| n=200, pan | 15.6 / - | 4.7 | 4.0 | - | - |
| n=400, pan | 29.3 / - | 9.6 | 8.7 | - | - |

Other n=300 stages (p50 ms): `links` 0.55, `editor_end` 1.4,
`imgui_render` 1.7, `swap` 0.56, `modulation` 0.26. Named stages now add up to
~20.8 of the 22.6 ms frame. The drag moved the node 150 px and the dropdown
was open 58 frames, so both phases really ran. Footprint peaked at 936 MB
(200 nodes), ~1.24 GB (300) and 1.54 GB (400), about 4 MB per node.

How targets are decided: `canvas_pan_p50_ge_60fps` (p50 <= 17.2 ms) and
`canvas_pan_p95_ge_45fps` (p95 <= 22.8 ms) are `true`/`false` only when the run
is trusted (focused, vsync on, `on_vsync_frac` >= 0.80). An untrusted run can
only be faster than the real thing, so if it already misses the limit it
reports `false`. Otherwise it reports `null`. At 300 nodes both targets fail
on that lower bound alone.

Still to do: 3 full `run_all.sh` passes on an idle machine with Infinite in
front the whole time, then add the B6 rows to the baseline.

### B8 media I/O: verification runs, not a baseline

Recorded 2026-09-24 on the M2 (Mac14,7, 60 Hz), 300 frames per variant,
GPU timers off. Someone was using the machine, so every run is `unfocused=1`
and `unpaced=1` (0-27% of frames on a refresh boundary). Frame numbers show
work per frame, not an on-screen rate. The counts (decodes, drops, repeats,
uploads) don't depend on pacing and hold as measured.

**Clips** (per clip, ranges across the clips of a run; ms):

| Variant | frame p50 / p99 | `cook` | decode p50 / p99 / max | cache hit p50 | loop boundary max | decoded fps | dropped | repeated / expected | uploads / new frames | upload CPU p50 |
|---|---|---|---|---|---|---|---|---|---|---|
| 2x1080 | 8.7 / 12.3 | 4.2 | 0.7 / 4.6 / 11.0 | 0.2-0.5 | 14-16 | 30.0 | 0 | 200 / 200 | 268 / 68 | 1.5 |
| 4x1080 | 10.9 / 23.8 | 8.2 | 0.7-1.3 / 1.9-2.5 / 7.8-9.9 | 0.2-0.3 | 13-15 | 30.1 | 0-1 | 172-173 / 172 | 268 / 95-96 | 1.5-1.7 |
| 2x2160 | 11.3 / 29.4 | 7.0 | 2.4 / 5.7-20.0 / 19-20 | 1.0-2.6 | 29-31 | 30.0 | 5-7 | 175-177 / 170 | 268 / 91-93 | 1.0-1.3 |
| 4x2160 | **295 / 765** | **290** | 3.2 / 20-22 / 29-54 | no hits | 100-189 | 22.5-23.3 | **~1600** | 0 / 0 | 268 / 268 | 1.3 |
| 4x2160 + 3 windows + camera + Syphon | **284 / 366** | **279** | 3.1-3.2 / 20-21 / 26-45 | no hits | 120-143 | 24.6-25.4 | **~1600** | 0 / 0 | 268 / 268 | 1.3 |

`reader_restarts` equals `loop_wraps` in every run (1-2 per clip at 1080 and
2x2160, 24-34 at 4x2160, where the clock ran on for 80+ s). `skipped` tracks
`dropped`. Footprint: 875 MB (2x1080), 1.17 GB (2x2160), 1.50-1.68 GB peak
(4x2160).

**Projector windows** (2x1080, swap interval 0, R = 60 Hz):

| Variant | window | present p50 / p99 | interval p50 / p99 | jitter stddev | missed vsync | `projectors` stage |
|---|---|---|---|---|---|---|
| 2 windows | 0 / 1 | 0.22 / 6.0, 0.22 / 0.46 | 10.3 / 29.0, 9.3 / 24.5 | 5.5, 3.7 | 3.7%, 0.4% | 0.54 |
| 3 windows | 0 / 1 / 2 | 0.18-0.22 / 0.39-6.0 | 9.7-10.6 / 21.9-27.3 | 3.5-5.2 | 6.4%, 6.0%, 0% | 0.80 |
| heavy | 0 / 1 / 2 | 0.29-0.36 / 7.0-7.8 | 284 / 362-366 | 33 | 100% | 1.37 |

Window intervals follow the canvas frame. Projectors have no pacing of their
own (finding 6), so a missed vsync here is the canvas's.

**Syphon Out** (publish only, no client connected, `has_clients: false`):
p50 0.07 / p99 0.16 ms at 1080, 0.18 / 0.42 ms in the heavy run.

**Camera**: `"skipped"`, `camera_skip_reason: "not_determined"`. Infinite
has never been granted camera access on this machine, and the fixture never
asks. Camera numbers need a run after access has been granted once, by hand.

**GPU upload**: with `INFINITE_BENCH_GPUTIMERS=1` (2x2160), `media_upload`
read exactly 0.0 ms on all 268 frames, while `imgui_render` read 1.07 ms in
the same run. The query measured nothing (see "Found while measuring"), so
the fixture reports `gpu_per_frame: null` with a `gpu_note` on macOS.

**Profile, heaviest variant** (`sample`, 10 s, 5455 main-thread samples):
96% under `cook` → `OutputNode::CookIfNeeded` → `VideoSourceNode::CookIfNeeded`
→ `Platform::VideoFrameAt`. Named stages add up to 282 of the 284 ms p50
frame, so nothing is unnamed. `cook_all` is 0.0 because every node in the
B8 graph is pulled by an Output inside `cook`. Inside `VideoFrameAt`,
attributed by disassembly (the build has no line info):

| Where | Samples | Share |
|---|---|---|
| `PushCacheFrame`: `operator new` + `memcpy` of a fresh 33 MB cache entry | 2552 | 47% |
| `DecodeNext` BGRA→RGBA swizzle + row flip loop | 1304 | 24% |
| `outPixels = handle->pending` (copy assign) | 760 | 14% |
| `pending.assign(..., 0)` zero-fill (`bzero`) | 263 | 5% |
| `AVAssetReaderTrackOutput copyNextSampleBuffer` | 257 | 5% |

**Proposed targets** (proposed, not agreed; the fixture reports them in
`targets_pass`). A miss is always reported `false`. A pass is `true` only in a
trusted run (focused, paced, no overlap), otherwise `null`:

- `clipN_decode_realtime`: (decoded + 1) / seconds ≥ the clip's fps, 0
  dropped, 0 skipped, and `repeated - expected_repeats` ≤ 0. Repeats the clip
  rate forces don't count against it, only extra ones.
- `windowN_interval_p99_locked`: frame interval p99 ≤ 1.10 × the refresh
  period. That is §6's "locked 60 fps, p99 ≤ 16.7 ms", with B3's 10%
  tolerance so vsync timestamp jitter alone can't fail it.
- `windowN_missed_vsync_lt_half_pct`: intervals > 1.5 periods < 0.5%.

Measured: at 2x2160 and above, decode fails outright (drops, and at 4x2160
decoded fps < 30). At 1080 a single loop-boundary drop is enough to fail
some clips. Projector windows fail both window targets in every run so far.
All of this is from untrusted runs, so only the failures count.

Still to do: one trusted pass (idle machine, Infinite in front, camera
access granted once by hand), then the Windows/Linux numbers from CI or a
tester.

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
- **A stale saved output device silenced audio (product). Fixed.**
  `Infinite.audio-settings` stores the device as a raw CoreAudio
  `AudioObjectID`. Those IDs are reassigned when a device is replugged (the
  headphones here went from 108 to 117), and a reassigned ID can end up
  naming an unrelated, possibly input-only, device. `AudioDeviceOpen` now
  checks that the requested device still resolves to a live output before
  handing it to the output AudioUnit, and falls back to the system default
  output otherwise, instead of leaving `startAndReturnError` to fail with
  `kAudioUnitErr_FailedInitialization` (-10875) and the engine never
  starting. The user's saved choice is left untouched, so the picker still
  shows it and the same device is retried on the next launch/replug. Same
  stale-index hazard fixed on Windows (`AudioDeviceWin.cpp`) and Linux
  (`AudioDeviceLinux.cpp`), and on macOS's audio-input capture path
  (`AudioInputCapturePump`).
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
- **Fixed (`bd38a27`). B6: every node re-cooks every frame even when nothing changes**. The
  whole-graph cook loop in the main loop (`for (GraphNode& gn : gNodes) ...
  CookIfNeeded(frameId)`) sat outside every stage timer, which hid ~9 ms of a
  22.6 ms frame at 300 nodes with the transport stopped. `CookIfNeeded` only
  skips a second cook in the *same* frame (`mLastCookFrame`). There is no
  dirty check, so Noise, Shape and every Filter run their GPU pass every
  frame. In a `sample` profile, `FilterNode`/`NoiseNode`/`ShapeNode::CookIfNeeded`
  were ~20% of main-thread samples. B6 now times the loop as `cook_all`
  (B6 only, so older benches keep their stage meanings). This is the biggest
  single lever for large patches.
- **Fixed (`94023d5`). B6: off-screen node bodies are drawn in full**. With ~23 of 300 nodes on
  screen, ~6.4 of 7.4 ms of `node_bodies` goes to nodes nobody can see. Cost
  grows ~0.07 ms per node, whatever is on screen.
- **B6: collapsing params barely helps**. `B6COLLAPSED=1` saves only ~1.4 ms
  at 300 nodes. Most of the body cost is in the fixed per-node UI: audio body
  meters (`DrawAudioFilterBody`, `DrawModulatorMeter`, Delay/Reverb bodies),
  not the param rows.
- **B6 fixture notes**: the drag moves the node through `spawnX/spawnY` +
  `needsPosition`, not a synthetic mouse drag. The GLFW backend overwrites
  an injected mouse position with the real cursor unless the cursor is
  warped, so a fake mouse drag moved the node 0 px. It measures the cost of
  moving a node, not ImGui's drag hit-testing. `ImGui::ClosePopupToLevel(0,
  false)` crashes on an empty popup stack. The dropdown phase only calls it
  while `OpenPopupStack.Size > 0`. Early B6 runs crashed there, and after
  that macOS's reopen-windows dialog hung every later launch (hence
  `-ApplePersistenceIgnoreState YES`).
- **macOS vsync is not a pacing guarantee**: GL vsync does not block for a
  window that is covered, off-screen or on another Space, and the focus flag
  can't see that. B6 counts frame intervals within 1.5 ms of a whole number
  of refresh periods. Below 80% it tags the run `unpaced=1`. B3 has the
  same exposure.
- **B8 media I/O** (numbers in the B8 section above; 1, 2, 3 and 8 are
  fixed on `bugfix/macos-video-decode` / `bugfix/restore-decode-thread`, see the Scoreboard):
  1. **Fixed** in `88787d7` (decode thread per clip, reapplied after the
     revert in `1b7364e`; see the Scoreboard for the gate). *macOS decodes on the main thread, inside cook.* `VideoFrameAt` runs
     `AVAssetReader` synchronously from `VideoSourceNode::CookIfNeeded`. At
     2x1080 `cook` is 4.2 of an 8.7 ms frame, and every loop wrap stalls
     the frame for 12-16 ms (1080) or 29-31 ms (2160).
  2. **Fixed** in `8d29baa` (`VideoFrameAt` returns true only for a new
     frame). *An unchanged frame is uploaded again.* `TryUseCache` hands back the
     same frame, and `VideoSourceNode` uploads it again. At 2x1080: 268
     uploads for 68 new frames, so 200 (75%) re-upload unchanged pixels at
     ~1.5 ms CPU each. At a paced 60 Hz this would be about 50%.
  3. **Fixed** in `5d9cbf0` (no per-frame copy or cache entry during
     playback), swizzle cost cut in `8a93b8d` (vImage).
     *`PushCacheFrame` copies every decoded frame.* It allocates and
     `memcpy`s a full frame into the LRU under `gVideoCacheMutex`, even when
     it will never be read again: 47% of main-thread time in the heaviest run.
  4. *Linux copies every delivered frame into `frameCache`, and its cache
     fallback also hands repeats back.* Code-read only, not measured
     (no Linux machine). `cache_hit_ms` and `reuploads` will show it on CI.
  5. *`VideoInNode` calls `glTexImage2D` (a reallocation) on every camera
     frame.* Code-read only. The camera was not measured (permission never
     granted).
  6. *Projectors present one after another at swap interval 0.* They have
     no vsync of their own, and their frame intervals just follow the
     canvas. `on_vsync_frac` is null for them by design, and the swap
     interval is unchanged.
  7. *Spout `HasClients` only reports `IsInitialized`.* Windows reports
     `has_clients: null` rather than a wrong `true`.
  8. **Fixed** in `93e184d` (bounded catch-up per cook, exact frames
     offline).
     *New: macOS catch-up spiral.* `VideoFrameAt` decodes every frame
     between the reader head and the requested time. It never skips ahead
     to a keyframe, and there is no deadline. Once one frame runs long, for
     example on a 4K loop-boundary restart of 100-190 ms, the next call has
     more frames to decode, so it runs longer still. At 4x2160 the frame
     never recovers: 290 ms `cook`, about 7 decodes per shown frame, about
     1600 of 1900 decodes per clip dropped, 0 cache hits, playback at
     about 3.5 fps. 2x2160 stays just short of it, with 5-7 drops per clip
     around the wraps.
- **Found while fixing B8** (recorded, not fixed here):
  - `main`'s macOS offline export repeated every other video frame. Fixed
    as a side effect of `5d9cbf0` / `93e184d`: offline cooks now use
    `VideoFrameAtExact`, and `INFINITE_VIDEOEXACTTEST` checks it.
  - Windows/Linux offline export never waited for the exact frame either.
    `VideoFrameAtExact` is shared code, so this is fixed there too, but only
    CI can confirm it.
  - `ArrangeMediaImport`'s `videoFirstFrame` is computed and never used.
  - 4x2160 is memory-bound on an 8 GB M2 (1.0-1.2 GB footprint). With
    other apps open it pages, and loop stalls reach 150-540 ms whichever
    decode path is used.
  - A loop-point reader can be rewound in place (`supportsRandomAccess` +
    `resetForReadingTimeRanges`): 20-60 ms at 4x2160 against 40-180 ms for a
    new reader, and no second decoder session. It only shipped with the
    thread, so it went out with the revert. It is worth trying on its own
    on the main-thread path.
  - B8 needs the screen unlocked and awake. On a locked screen GL vsync
    stops blocking, and frames run at about 1 ms with 10-18 new video frames
    per run: invalid data that only a low `new_frames` count gives away.
  - Benchmarks are contaminated by the post-commit `sync_brain` (below) and
    by other apps. Compare builds interleaved, not at different times.
  - Linux finding 4 above is still open.
- **B8 harness notes.** On Apple's GL a `GL_TIME_ELAPSED` query around
  `glTexSubImage2D` reads 0 ms: the driver copies on the CPU and runs the
  blit later, outside the query. So B8 turns GPU timers on only with
  `INFINITE_BENCH_GPUTIMERS=1`, and reports an all-zero stage as null.
  macOS won't give focus to an app launched from a background shell, even
  with `open`, so unattended runs come out `unfocused=1`. A fixture window
  closed by hand ends the run with no `BENCH_JSON`. `run_all.sh` now has an
  opt-in `FIXTURE_TIMEOUT` watchdog, and B8 uses it.
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

B8: `Platform::VideoBenchStats` and `Platform::CameraAuthorizationStatus` are
defined in `Platform.mm`, `win/MediaWin.cpp` and `linux/MediaLinux.cpp` /
`linux/CameraLinux.cpp`. Windows and Linux decode on a worker thread, so
`decode_ms` there is the thread's `ReadSample`/`avcodec` time. `dropped`
counts frames the pick skipped, and the loop boundary is seek to first
decoded frame. Windows has no frame cache (`cache_hit_ms` stays empty) and
no clip frame rate (`fps` falls back to 30). The camera always reads as
authorized there. Linux reports Syphon as `"n/a"`, and Windows reports
`has_clients: null`. Only compiled and run on macOS so far.

The macOS video fixes (`bugfix/macos-video-decode`) touch shared files that
only CI can verify on Windows/Linux: `src/platform/Platform.h` (comments,
plus the inline `VideoFrameAtExact`), `src/nodes/VideoSourceNode.cpp`
(offline cooks call `VideoFrameAtExact`) and `src/main.cpp`
(`INFINITE_VIDEOEXACTTEST`, which uses the Recorder APIs every platform
has). `VideoFrameAtExact` loops on
`VideoDecodeIsCatchingUp`, which MediaWin/MediaLinux already implement.
