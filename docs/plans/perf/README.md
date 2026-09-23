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
| B2 Heavy visuals | Geometry→Render3D→10-30 compositing nodes→Output, static+animated | No | Needs the GPU timer query ring (§below) for `stages_gpu_ms` - not built this session. |
| B3 Live performance | B1-lite+B2-lite+projector+MIDI+macros+Prediction; missed vsyncs, input-to-photon | No | Depends on B1+B2 fixtures existing first, plus a projector-window self-test fixture (none exists today). |
| B4 Complex 3D scenes | many objects/instancing/lights/shadows/materials/HDRI/ocean | No | Same GPU-timer dependency as B2. |
| B5 Fundamentals | (a) empty patch, **(b) node-count scaling**, (c) per-stage CPU+GPU split, **(d) audio-thread-alone**, (e) startup time, (f) load/save time, (g) undo-snapshot time | **(a)/(b)/(d)** (`INFINITE_BENCH_B5EMPTY`, `INFINITE_BENCH_B5NODES`, `INFINITE_BENCH_B5AUDIOALONE`) | (a) zero-node floor, frame_ms percentiles + RSS, same 120-frame sampled window as (b) so the two are directly comparable. (b) 50/100/200/400 mixed nodes laid out on a grid (not stacked at origin - the flaw called out in benchmark-suite.md §2 against MIXEDSTRESSTEST/GEOMDENSITYTEST). Reports frame_ms percentiles + RSS. (d) one Oscillator straight into Audio Out, no effects chain, buffer sweep 64/128/256/512 - isolates the audio callback's fixed per-block cost from B1's DSP-graph cost; reuses B1's wall-clock-window + `AudioLoadRing` pattern. (c)/(e)/(f)/(g) not started. |
| B6 Canvas navigation | programmatic pan/zoom/drag, never OS-level UI scripting | No | "Missing today" per the doc; not started. |
| B7 Soak/thermal | 30min B3, long variant only | No | Depends on B3. |
| B8 Media I/O | video/camera/projector/Syphon-Spout | No | "Missing today" per the doc; not started. |
| B9 Memory footprint | B2/B4 at `l` scale | No | Depends on B2/B4. |
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
matching the margin every other fixture in the script already carries. The
remaining benchmarks
(B2/B3/B4/B6/B7/B8/B9/B10, and B5's c/e/f/g sub-benchmarks) are not yet built -
each is its own multi-hour session given the platform work some of them need
(GPU timers, a canvas-automation entry point, projector/MIDI harnesses,
offline-render integration). Nothing below claims coverage this suite doesn't
have.

## What still needs building, and what it needs first

- **GPU timer query ring** (`GL_TIME_ELAPSED`, read back N frames later,
  guarded for GL 4.1 support - macOS has it, confirm before assuming Windows/
  Linux llvmpipe does). Blocks B2, B3, B4, B9's GPU numbers. Not started.
- **B5(c)/(e)/(f)/(g)**: each is small and independent of the above - next
  lowest-effort work here.
- **B6 canvas navigation**: needs a programmatic pan/zoom/drag entry point
  into the node editor (`ed::` calls) exposed to a self-test fixture - none
  exists today. `main.cpp`'s existing `gDroppedFiles`/`gDropPos` self-test
  pattern (see `codebase-navigation`'s living map) is the closest precedent
  for "drive real interaction state from a fixture, not OS-level clicks."
- **B8 media I/O**: needs decode/upload/present timing hooks in the video and
  camera paths, plus a way to open 2-3 real projector windows headlessly.
- **B3/B7/B10**: each composes B1+B2 (+projector/MIDI/Prediction for B3, +
  soak duration for B7, + offline render for B10) - blocked on B2 existing.

## Baseline

Recorded: `bench/baselines/m2-8gb.jsonl`, committed from a clean
`scripts/bench/run_all.sh` run on this machine (`Mac14,7`, Apple M2, commit
`9326563`), 13 `BENCH_JSON` lines, zero `FAIL`s, zero xruns anywhere. Default
windows throughout (60s B1 per buffer size, 30s B5d per buffer size).

| Bench | Variant | frame_ms p50/p95/p99 (ms) | audio cb_load p50/p99 (%) | xruns | nodes | RSS (MB) |
|---|---|---|---|---|---|---|
| B1_heavy_audio | 24 voices, buf=64 | 75.1 / 98.0 / 216.3 | 57.4 / 66.0 | 0 | 149 | 301.6 |
| B1_heavy_audio | 24 voices, buf=128 | 74.6 / 97.8 / 215.6 | 56.8 / 62.7 | 0 | 149 | 329.6 |
| B1_heavy_audio | 24 voices, buf=256 | 74.7 / 99.8 / 214.2 | 56.5 / 59.8 | 0 | 149 | 343.8 |
| B1_heavy_audio | 24 voices, buf=512 | 74.0 / 101.3 / 213.5 | 56.3 / 59.5 | 0 | 149 | 289.2 |
| B5_fundamentals_empty | - | 1.00 / 1.61 / 1.67 | n/a | - | 0 | 256.8 |
| B5_fundamentals_nodecount | n=50 | 4.42 / 4.53 / 4.58 | n/a | - | 50 | 287.1 |
| B5_fundamentals_nodecount | n=100 | 7.86 / 8.07 / 8.13 | n/a | - | 100 | 342.0 |
| B5_fundamentals_nodecount | n=200 | 14.7 / 15.3 / 15.5 | n/a | - | 200 | 369.6 |
| B5_fundamentals_nodecount | n=400 | 28.6 / 37.5 / 39.9 | n/a | - | 400 | 367.8 |
| B5_fundamentals_audioalone | buf=64 | n/a (no video loop) | 1.39 / 1.70 | 0 | 2 | n/a |
| B5_fundamentals_audioalone | buf=128 | n/a | 1.37 / 1.52 | 0 | 2 | n/a |
| B5_fundamentals_audioalone | buf=256 | n/a | 0.85 / 1.11 | 0 | 2 | n/a |
| B5_fundamentals_audioalone | buf=512 | n/a | 2.44 / 2.72 | 0 | 2 | n/a |

## Top costs per benchmark

Per-stage CPU/GPU breakdown (`stages_cpu_ms`/`stages_gpu_ms`) is not yet
measured for any benchmark - that requires `ScopedStageTimer` wired into the
main loop's cook/draw/composite stages, which no fixture built this session
actually calls. B5(c) is specifically the sub-benchmark meant to produce this
breakdown; it isn't built yet. What the baseline above does show without that
instrumentation:

- **B1** (24-voice heavy audio): frame_ms p99 (~214-216ms) is roughly 3x p50
  (~74-75ms) at every buffer size - a heavy tail, likely UI/editor cost
  spiking independently of the audio thread, since `cb_load` (the audio
  callback's own load) stays flat and low (56-66%) and barely moves across
  the buffer sweep. Buffer size has almost no effect on either frame_ms or
  cb_load here - the DSP graph's per-block cost dominates over per-block
  overhead at every size tested.
- **B5(b)** (node-count scaling): near-linear from 50 to 200 nodes (4.4ms to
  14.7ms, roughly 3.3x for 4x the nodes), then super-linear at 400 (28.6ms
  p50, 39.9ms p99 - the p95/p99 spread widens sharply too, 37.5/39.9 vs a
  tight 15.3/15.5 at n=200). Worth a closer look once B5(c) exists to say
  which stage stops scaling linearly past ~200 nodes.
- **B5(d)** (audio-thread-alone, one Oscillator, no effects): cb_load is
  under 3% at every buffer size, confirming B1's ~57-66% load is almost
  entirely the 24-voice effects chain, not fixed per-callback overhead.

## Found while measuring

Per §8: this suite measures, it does not fix. Anything found while building
a fixture goes here, not into a code change.

- (none recorded yet against the *app* - the only two issues found this
  session were both in this suite's own harness code: a null-`this` crash in
  the B1/B5d fixture setup, and an `EXITAFTER` fencepost bug in `run_all.sh`'s
  B5(b) sweep, both described above and both fixed as this session's own
  deliverable, not app bugs)

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
