# P0 — Audio feasibility spike

Goal: prove (with numbers, not estimates) that a live `AVAudioEngine`
synthesis callback can run alongside Infinite's GL/ImGui render loop without
audible glitches or a measurable FPS hit. This is throwaway code — a single
env-gated test fixture — not the start of the real audio engine. Do not
build any node types, `src/audio/` scaffolding, or param plumbing in this
pass; that's P1 onward.

**Clean-room rule, verbatim:** do not open, read, grep, or reference
`/Users/namansoni/BespokeSynth`. Work only from this prompt and standard
AVFoundation documentation.

## What's already confirmed in this codebase

- `src/main.cpp` is 14,798 lines (verified via `wc -l`; `ARCHITECTURE.md` is
  stale on this number — don't trust it, don't fix it here).
- Infinite already runs `AVAudioEngine` successfully in production, twice:
  - `src/platform/Platform.mm:1748-1828` (`AudioFileOpen`/`AudioFileClose`) —
    an `AVAudioEngine` + `AVAudioPlayerNode`, connected to
    `mainMixerNode`, with a **tap** installed via `installTapOnBus:0
    bufferSize:1024` for analysis. This backs the existing `AudioFileNode`
    (`src/nodes/AnalyzeNodes.h:91`).
  - `src/platform/Platform.mm:1593-1650` — a second `AVAudioEngine` reading
    live mic input via `AVAudioInputNode`, also tapped.
  - Both already coexist with the render loop at frame rate today, so engine
    creation and tap-based analysis are **not** the open question.
- **What is unproven**: a **synthesis source** — an `AVAudioSourceNode`
  whose render block *generates* samples on demand (as opposed to playing
  back a file or tapping input). No code in this repo does that yet. That's
  the one thing this spike needs to test.
- `AVFoundation` is already linked in `CMakeLists.txt:140` — no build changes
  needed for this phase.
- `Platform.mm` is compiled with `-fobjc-arc` (`CMakeLists.txt:119-120`) —
  any new ObjC/ObjC++ code you add there follows the same ARC rules as the
  existing handles (see the `@try`/`@catch` teardown at
  `Platform.mm:1798-1828` for why: `AVAudioEngine` throws an NSException on
  bad teardown state rather than returning an error, and an uncaught one
  aborts the whole process).
- The frame loop already tracks per-frame wall time in a global:
  `double gLastFrameMs` at `src/main.cpp:253` ("wall clock across the
  previous whole frame"), already read by other env-gated tests (e.g.
  `INFINITE_FPSTEST` at `src/main.cpp:11954-11966`). Use this directly —
  don't add a second timing mechanism.
- The test-harness convention (see `ARCHITECTURE.md` §6 and
  `run-infinite-hygiene`): every fixture is `getenv("INFINITE_<NAME>")`-gated
  in `main.cpp`, spawns a small graph or does its thing over N frames, and
  ends by `printf`-ing a line containing `OK`/`SUSPECT`/`FAIL`, then calling
  `glfwSetWindowShouldClose(window, GLFW_TRUE)` to exit (see
  `src/main.cpp:8411`, `:9179`, `:12114`, etc., for the pattern). Follow it
  exactly — do not invent a second test mechanism or a separate binary.
- `INFINITE_SHOWCASE4` (`src/main.cpp:7561-7580`) is the heaviest existing
  fixture: Reaction Diffusion at `stepsPerFrame = 24` feeding Curves,
  Shape, and Trails. Use it (or a graph at least as heavy) as the "heavy
  visual patch" for the FPS-delta measurement — it's a real, already-defined
  GPU load, not a synthetic one you'd have to justify.

## What to build

### 1. A throwaway synthesis spike in `Platform.mm`/`Platform.h`

Add two free functions to the platform layer (near the existing audio code,
`Platform.mm:1680` region), following the existing handle-struct pattern
(`AudioPlayerHandle` at `Platform.mm:1686-1698`) rather than global
`AVAudioEngine*` variables directly:

```cpp
// Platform.h
bool AudioSpikeStart(std::string& outError);
void AudioSpikeStop();
// Diagnostics captured by the render block, safe to poll from the main thread:
struct AudioSpikeStats { double sampleRate; int blockSize; double maxJitterMs; uint64_t callbackCount; };
AudioSpikeStats AudioSpikeGetStats();
```

Implementation (`Platform.mm`): a raw `AVAudioEngine` with an
`AVAudioSourceNode` (not a player) attached and connected to
`mainMixerNode`, whose render block writes a 440 Hz sine directly into the
output buffer (phase accumulator captured by the block, standard
`sin(2*M_PI*440.0*phase)` — this is public-domain math, not BespokeSynth
code). Inside the render block:
- record `frameCount` into a stats struct (block size actually delivered)
- record wall-clock time of entry (e.g. `mach_absolute_time()` converted to
  ms) and track the max gap between consecutive calls vs. the expected gap
  (`frameCount / sampleRate`) as `maxJitterMs`
- increment a callback counter

Use `std::atomic` for the fields the render block writes and the main
thread reads — the render block runs on a real-time audio thread, same
constraint the plan's P1 states for the eventual real engine (no locks, no
allocation in the block itself once running). This spike is throwaway, but
getting torn down/glitching from a bad handoff would invalidate its own
measurement, so keep that one constraint.

`AudioSpikeStop()` must mirror the `@try`/`@catch` teardown at
`Platform.mm:1810-1821` for the same reason documented there.

### 2. An env-gated fixture in `main.cpp`

Add `INFINITE_AUDIOSPIKE`, mirroring the structure of `INFINITE_SHOWCASE4`
(spawn the heavy patch) plus `INFINITE_FPSTEST`'s use of `gLastFrameMs`
(measure it). Suggested shape, adapt to fit the surrounding `if/else if`
chain starting at `main.cpp:6764`:

- Spawn the `INFINITE_SHOWCASE4` graph unconditionally so the FPS
  measurement has real load from frame 0 — don't gate it behind a second env
  var.
- Track `gLastFrameMs` into a running min/max/sum starting at frame 0.
- At a fixed frame (e.g. frame 120, a few seconds in at 30fps target) call
  `AudioSpikeStart()`. Assert it succeeded — if it fails, `printf` the error
  and treat the run as `FAIL`, don't silently continue.
- Keep accumulating `gLastFrameMs` stats separately for "before audio" vs
  "after audio" frames.
- Run for long enough to matter — this spike is explicitly about a 60s
  glitch-free claim, so don't settle for the ~60-frame windows other
  fixtures use. Gate the end on wall-clock time (`glfwGetTime()`, already
  used elsewhere in `main.cpp`, e.g. `:14758`) reaching roughly 65s after
  `AudioSpikeStart()`, not a frame count — frame count at an uncapped or
  variable rate doesn't map to real seconds.
- On completion: call `AudioSpikeGetStats()`, `printf` sample rate, actual
  block size(s) seen, max jitter, callback count, and the before/after
  average `gLastFrameMs` with the delta and delta-as-percent. Then
  `AudioSpikeStop()`, then a verdict line and `glfwSetWindowShouldClose`.
- Verdict condition (encode as the printed OK/SUSPECT, adjust thresholds if
  your measurement shows the plan's own "within noise" framing needs a
  number — say what you picked and why): FPS delta before/after audio
  under ~5%, zero xruns/dropped-callback evidence (jitter reasonable
  relative to block period), and it actually ran the intended ~60s without
  the audio engine erroring out.

### 3. Manual listening pass

The FPS numbers only prove the visual side is unaffected — they say nothing
about whether the sine itself glitches (clicks, dropouts). After the
automated fixture passes, run the built app **interactively** (not through
the hygiene driver's headless mode) with `INFINITE_AUDIOSPIKE=1` set, let it
run its ~65s, and listen. Report what you heard, plainly — "clean" or
describe exactly what glitched and when (e.g. "one click right at
`AudioSpikeStart()`", which would point at the connect/start sequence, not
the render block itself).

## Build and verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Confirm it compiles clean (this touches `Platform.mm`, an Objective-C++
file under ARC — a mistake here is a compile error, not a runtime crash,
which is good, but check the actual build output rather than assuming a
clean diff compiles).

Then run the fixture:

```bash
INFINITE_AUDIOSPIKE=1 build/Infinite.app/Contents/MacOS/Infinite
```

Report the printed numbers verbatim in your summary — sample rate, block
size, max jitter, callback count, before/after `gLastFrameMs` averages and
delta%, and the manual listening result. These numbers are the actual
deliverable of this phase; the plan's later phases (P1 engine design in
particular) get revised if they're bad.

Do **not** run the full `run-infinite-hygiene` suite for this phase — this
spike doesn't touch any existing node type, `INode`, `Patch`, or the
connect/disconnect paths, so the regression surface the suite exists to
catch isn't in play yet. That requirement starts at P2.

## Out of scope (explicitly deferred to later phases)

- Any `src/audio/` files, `AudioEngine`/`AudioNode`/`ParamMailbox` classes —
  that's P1.
- Any node-editor changes, new spawnable node types, cable types — P2/P3.
- Deciding definitively between `AVAudioSourceNode` and a raw AUHAL output
  unit for the *real* engine — this spike's numbers inform that choice, but
  don't over-engineer the spike itself to explore both; if
  `AVAudioSourceNode` already gets clean numbers, that's the answer, and
  trying a second implementation path here is scope creep on a throwaway
  test.
- Leave the `AudioSpikeStart`/`Stop`/`GetStats` functions in place after
  this phase (don't delete them once the numbers are captured) — a future
  session may want to re-run this exact measurement after P1/P2 land, to
  confirm the real engine's numbers still hold.
