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
and B5d's full setup/measurement/teardown sequence. The remaining benchmarks
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

Not yet recorded. `bench/baselines/m2-8gb.jsonl` should be a `run_all.sh`
output committed once B1 and B5(b) have been run end-to-end with `--full`
(default 60s B1 window) on this machine and the JSONL reviewed by hand for
sanity. This session ran B1/B5b directly during development
(shorter windows, for fast iteration) but has not yet done the committed
full-window baseline run - see "Current session status" below.

## Top costs per benchmark

Not yet measured for any benchmark - filling this in requires per-stage CPU
timing (`ScopedStageTimer`, wired into the main loop's cook/draw/composite
stages) which no fixture built this session actually calls. B5(c) is
specifically the sub-benchmark meant to produce this breakdown; it isn't
built yet.

## Found while measuring

Per §8: this suite measures, it does not fix. Anything found while building
a fixture goes here, not into a code change.

- (none recorded yet - B1/B5b haven't been run through their full committed
  windows on this machine as of this writing)

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
