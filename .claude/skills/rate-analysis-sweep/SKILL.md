---
name: rate-analysis-sweep
description: Measure and gate Infinite's frame rate and audio rate behaviour on both macOS and Windows - the frame limiter hitting its target, idle-frame caching so a static patch stops re-cooking, the editor's per-frame hit-test cost as node count grows, audio callback jitter and xruns, and whether starting audio costs frame time. Use when the app feels slow or stutters, when a big patch freezes the editor, when the fps cap is ignored or overshoots, when audio clicks under load, when a static patch still burns full frame time, after touching the render loop, the frame limiter, cook caching, or the audio callback, or before a release as a performance regression gate.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
.claude/skills/rate-analysis-sweep/driver.sh
```

`--skip-build` to reuse the existing binary. Exit 0 means the frame limiter
held and the editor-cost scan produced measurements at every node count.

This sweep is deliberately half gate, half measurement. Only one thing here has
a defensible pass/fail threshold; the rest are numbers you read. Resist turning
a measurement into a graded threshold without deciding what the threshold
means on the slowest machine you support - a perf gate that fails on a loaded
laptop gets disabled, and then it gates nothing.

## The three rates, and what governs each

| rate | governed by | measured by |
| --- | --- | --- |
| frame rate | `gTargetFps` + `gVsync` frame limiter | `FPSTEST` (graded) |
| per-frame *work* | `CookIfNeeded` memoization + `NodeWorkCounter` | `CACHETEST` (read) |
| editor overhead | `ed::End()` -> `BuildControl`, scales with nodes+pins | edperf scan (read) |
| audio rate | device block size / sample rate, callback deadline | `AUDIOSPIKE`, engine `XrunCount()` (read) |

## The one graded check

`INFINITE_FPSTEST` turns vsync off at frame 2, measures the uncapped frame time
at frame 30, sets a 30fps target, and at frame 60 asserts the frame time landed
in **30-37ms** (`FRAME LIMITER OK`, else `SUSPECT - limiter missed its
budget`). It catches both failure directions: a limiter that does nothing
(frame time stays uncapped) and one that overshoots its sleep (frame time well
past 33.3ms, so the app quietly runs below the rate the user asked for).

If this fails on a machine that is otherwise busy, re-run it idle before
believing it - it is the one check here that a background build can knock over.

## Editor cost vs node count

`ed::End()` runs imgui-node-editor's hit-test pass, which walks **every live
node and every live pin, every frame**. It is the one editor cost that scales
with patch size, and when it blows up the app looks frozen rather than slow -
a spindump lands inside `BuildControl` and says nothing useful.

The driver measures it directly by spawning N nodes and reading the
`[edperf] frame=... nodes=... ed::End=...ms` line the render loop prints when
`INFINITE_EDPERFTEST` is set:

```bash
INFINITE_EDPERFTEST=500 INFINITE_EXITAFTER=40 build/Infinite.app/Contents/MacOS/Infinite | grep edperf
```

`INFINITE_EDPERF_TYPE` and `INFINITE_EDPERF_CAT` choose which node type gets
spawned (default `Gain` / `Utility`) - use them to measure a node with many
pins, which is the case that actually hurts:

```bash
INFINITE_EDPERFTEST=200 INFINITE_EDPERF_TYPE=Wavetable INFINITE_EDPERF_CAT=Audio \
  INFINITE_EXITAFTER=40 build/Infinite.app/Contents/MacOS/Infinite | grep edperf
```

The driver's table sweeps 1 / 50 / 200 / 500 and prints median and max ms. Read
it for **shape, not absolute numbers**: cost should grow roughly linearly with
node count. A superlinear jump between 200 and 500 is the regression.
`INFINITE_EDPERF=1` alone adds the same timing line to a normal session, which
is how you measure a real patch instead of a synthetic grid.

## Idle-frame caching

A static patch should stop doing work. `CACHETEST` prints
`work=<NodeWorkCounter> idle=<0|1> idleStreak=N` every frame; on a patch that
is not animating, `idleStreak` should climb. A streak pinned at 0 means
something re-cooks unconditionally every frame - usually a node whose
`CookIfNeeded` is not memoized on the frame id, or one driven by wall-clock
time instead of `Transport`. `compositing-pipeline-sweep`'s `check.py` is the
static gate for exactly that, so run it when this trace looks wrong.

Note the fixture only *observes*: it deliberately does not skip
`glfwSwapBuffers` or ImGui's own redraw, since those still have to run for
hover states and cursor responsiveness. "Idle" here means the node graph did no
work, not that the frame was free.

## Audio rate

Two separate things, neither in the default fixture list:

**Callback jitter and its cost to fps.** `INFINITE_AUDIOSPIKE` spawns a heavy
visual patch, runs for 65 seconds after starting a live synthesis callback at
frame 120, and prints `maxJitterMs`, callback count, and average frame time
before vs after audio started. It asserts jitter below one block period and an
fps delta under 5%, printing `AUDIOSPIKE OK` / `AUDIOSPIKE SUSPECT`. It is out
of the driver's list only because of its runtime - run it by hand when you
touch the audio callback or the render loop:

```bash
INFINITE_AUDIOSPIKE=1 INFINITE_EXITAFTER=100000 build/Infinite.app/Contents/MacOS/Infinite
```

**Xruns.** `AudioEngine::Instance().XrunCount()` is printed by
`INFINITE_AUDIOTEARDOWNSWEEPTEST` (see `audio-pipeline-sweep`). It is printed,
never asserted, and that is on purpose: a rising xrun count on an idle machine
is a real problem, and the same count on a laptop mid-build is noise. Read it
against a known-quiet baseline or not at all.

## Windows parity

Both rate paths differ by OS underneath, so this sweep is worth running twice:

- The frame limiter's sleep and the swap-interval call go through different OS
  timers. A limiter that lands at 33ms on macOS can sit at 40ms on Windows
  purely from timer granularity - `FPSTEST` is the check for that, and its
  30-37ms window is tight enough to notice.
- Audio block size and callback deadline come from CoreAudio on macOS and
  WASAPI on Windows, with different default block sizes. Compare
  `AUDIOSPIKE`'s printed `blockSize`/`sampleRate` line across the two before
  comparing their jitter numbers - the jitter bar is one block period, so it
  is not the same bar on both.
- The editor hit-test cost is portable C++ and should scale identically; a
  divergence there is a real finding.

## What this sweep does not cover

- **No GPU-time breakdown.** Everything is measured in wall-clock frame time.
  Which node's shader is expensive is not answered here; `GEOTEST` and
  `RENDER3DCACHESWEEPTEST` in `render-pipeline-sweep` print draw calls and
  upload counts, which is the nearest thing.
- **No memory growth check.** Nothing watches RSS over a long run.
- **No sustained soak.** The longest thing here is `AUDIOSPIKE`'s 65 seconds. A
  leak or a drift that takes ten minutes to show is invisible to this sweep.
- **Threshold honesty.** Only `FPSTEST` and `AUDIOSPIKE` assert anything. The
  edperf table and `CACHETEST` are traces; a green run of this driver means
  "the limiter works and the measurements ran", not "performance is fine".
