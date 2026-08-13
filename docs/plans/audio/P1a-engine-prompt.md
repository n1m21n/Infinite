# P1 (session 1 of 2) — Audio engine core

Goal: stand up `src/audio/` — the `AudioEngine` singleton, the `AudioNode`/
`AudioBuffer` mix-in, `ParamMailbox`, `MeterRing`, and the
`INFINITE_DSPTEST` headless harness — with a **hardcoded** oscillator→gain
chain proving the whole plumbing works, both to the real device and to a
test buffer. This session does **not** build `DspMath` (PolyBLEP, SVF, RBJ
biquads, etc.) or `AudioVoice` — that's session 2. Where the full plan's
exit criterion says "osc→filter→out", this session's actual exit criterion
is **osc→gain→out**, because the filter is session 2's deliverable; don't
hand-roll a one-off filter here just to hit the letter of that phrase.

**Clean-room rule, verbatim:** do not open, read, grep, or reference
`/Users/namansoni/BespokeSynth`. Work only from this prompt.

**The two-object rule** (from the plan, applies to every later audio-node
phase, stated here for the record even though this session builds no real
`INode` subclasses yet): an audio node will eventually be **two objects** —
an `INode` on the main thread (UI, params, save/load, pins) owning an
`AudioNode` on the audio thread (`ProcessBlock` only). They communicate
through `ParamMailbox` and `MeterRing`, never by sharing mutable fields.
`CookIfNeeded` on an audio-backed `INode` will do **no DSP** — draining
meters and pushing dirty params only, budget < 5 µs. This session builds
the mailbox/ring/engine that rule depends on, without yet touching `INode`
or the node editor.

**Audio-thread prohibitions**, enforced starting now: no allocation, no
locks, no `dynamic_cast`, no `std::function`/`std::map`/`std::string`, no
GL, no ImGui, no file I/O, no `printf`. The render callback and everything
it calls (`AudioEngine::Process`, every `AudioNode::ProcessBlock`,
`ParamMailbox::Pop`, `MeterRing::Write`) must hold to this. Verify each new
function against this list before considering it done, not just at first
read.

## What already exists — build on this, don't duplicate it

P0 (a prior session) landed a working feasibility spike, already measured
and confirmed on this machine:
`sampleRate=44100 Hz, blockSize≈470 (variable — do not assume a fixed block
size), maxJitterMs≈0.96 (block period ≈10.7 ms, so comfortable margin),
FPS delta -1.24% (i.e. within noise) over a 65 s run against the heaviest
existing visual fixture`. Full writeup: `docs/plans/audio/P0-feasibility-prompt.md`.

The spike's code is the working reference for the ObjC++/AVFoundation
pattern — **read it, follow its shape, don't replace it**:

- `src/platform/Platform.h:171-181` — `AudioSpikeStats` struct,
  `AudioSpikeStart`/`AudioSpikeStop`/`AudioSpikeGetStats` declarations.
- `src/platform/Platform.mm:1685-1826` — the implementation.
  `AudioSpikeStart` (`:1712-1787`) shows the exact `AVAudioEngine` +
  `AVAudioSourceNode` setup: get the mixer's native format
  (`mainMixerNode outputFormatForBus:0`), attach + connect the source node,
  `prepare` + `startAndReturnError:`. `AudioSpikeStop` (`:1789-1814`) shows
  the required `@try`/`@catch` teardown — `AVAudioEngine` throws an
  `NSException` on bad teardown state instead of returning an error, and an
  uncaught one aborts the whole process; this is not optional ceremony,
  it's the only safe way to tear this down (see the comment at
  `Platform.mm:1797-1799`, matching the same hazard documented for
  `AudioFileClose` at `Platform.mm:1804-1809` in the older file-player
  code).
  Note in particular that the render block in the spike writes directly
  into `outputData->mBuffers[ch].mData` — i.e. the callback already gets
  **planar** (non-interleaved) per-channel `float*` buffers, not
  interleaved stereo. Design `AudioBuffer` (below) around that, matching
  reality rather than a textbook interleaved-buffer assumption.
- `main.cpp:11993-12055` (`INFINITE_AUDIOSPIKE` fixture) — shows the
  established pattern for measuring an audio feature against `gLastFrameMs`
  (`main.cpp:253`) and the wall-clock gating via `glfwGetTime()`. You will
  not need to replicate this for the DSPTEST harness (see below — DSPTEST
  is headless, no device, no render loop at all), but it's useful context
  for how this codebase's test fixtures are shaped.
- **Leave `AudioSpike*` exactly as it is.** Don't refactor it to route
  through the new `AudioEngine` — it's a standalone throwaway kept
  deliberately so a future session can re-run the same raw measurement
  after this phase lands, to confirm the real engine's numbers still hold.

## 1. New Platform-layer bridge: a general render callback, not a hardcoded sine

`AudioSpikeStart` hardcodes a 440 Hz sine inside the ObjC render block. The
real engine needs the render block to call into `AudioEngine::Process`
instead — but the audio-thread prohibitions rule out `std::function` as the
bridge type. Use a plain C function pointer + opaque context, exactly the
shape CoreAudio itself uses:

In `Platform.h`, alongside the existing `AudioSpike*` declarations:

```cpp
// Render callback bridge: called on the real-time audio thread. Must obey
// every audio-thread prohibition in docs/plans/audio/P1a-engine-prompt.md -
// no allocation, no locks, no std::function/map/string, no GL/ImGui/file I/O.
// buffers is planar: buffers[ch][0..numFrames) for channel ch.
typedef void (*AudioRenderCallback)(float** buffers, int numChannels, int numFrames, void* userData);

bool AudioDeviceOpen(AudioRenderCallback callback, void* userData, double& outSampleRate, std::string& outError);
void AudioDeviceClose();
```

In `Platform.mm`, add this as a new block near (not replacing) the
`AudioSpike*` code, following the exact same attach/connect/prepare/start
and `@try`/`@catch` stop shape as `AudioSpikeStart`/`Stop`. The render
block itself becomes a thin adapter: unpack `outputData->mBuffers` into a
small fixed-size stack array of `float*` (cap channel count — 8 is plenty;
don't allocate a `std::vector` here, that's a heap allocation on the audio
thread) and call the C callback.

## 2. `src/audio/AudioBuffer.h` — non-owning block view

```cpp
struct AudioBuffer
{
   float** channels = nullptr; // planar, matches the platform layer's actual layout
   int numChannels = 0;
   int numFrames = 0;
};
```

No ownership, no allocation methods — it's a view over memory the caller
owns (either the platform layer's real buffers, or the DSPTEST harness's
scratch buffer).

## 3. `src/audio/AudioNode.h` — the process interface

```cpp
class AudioNode
{
public:
   virtual ~AudioNode() {}
   virtual void PrepareToPlay(double sampleRate, int maxBlockSize) {}
   virtual void ProcessBlock(AudioBuffer& buffer) = 0; // in place: read + overwrite
   virtual void Reset() {}
};
```

`ProcessBlock` processes in place — the engine runs a chain of nodes over
one shared scratch buffer, each node transforming what's already there.
This is deliberately simpler than a general multi-input mixing graph (that
generality belongs to the real node-editor cable system, P2/P3); P1's job
is proving the engine plumbing, not the graph topology.

## 4. `src/audio/ParamMailbox.h`/`.cpp` — lock-free main→audio, smoothed

Single-producer (main thread) / single-consumer (audio thread) ring of
`(int paramId, float targetValue)`, backed by a fixed-size array and
`std::atomic<size_t>` head/tail (classic SPSC ring — no locks, no
allocation once constructed). Fixed capacity is fine (e.g. 256 entries);
if the producer overruns it, drop the oldest pending write for that
`paramId` rather than blocking — document that choice in a comment, it's a
deliberate trade a UI slider spamming updates should never be able to stall
the audio thread.

Per-block smoothing: the audio-side consumer keeps a `float mCurrent[N]`
and `float mTarget[N]` per param slot, and each block moves `mCurrent`
toward `mTarget` with a one-pole coefficient computed from sample rate and
a ~5 ms time constant (`coeff = expf(-1.0f / (0.005f * sampleRate))`,
recomputed in `PrepareToPlay` when sample rate becomes known — this is
standard one-pole smoothing math, not sourced from anywhere). Expose
`float SmoothedValue(int paramId)` for a node's `ProcessBlock` to read
per-sample or per-block as it prefers.

## 5. `src/audio/MeterRing.h`/`.cpp` — lock-free audio→main

Same SPSC-ring shape, reversed direction: audio thread writes decimated
sample data (e.g. one peak value per N samples, not every sample — the
plan's later scope work depends on this being pre-decimated so `main.cpp`
never has to downsample 48 000 points/sec on the GPU/UI thread), main
thread drains it once per frame. Keep this generic (`Write(const float*,
int count)` / `Read(float* out, int maxCount)`) rather than
audio-node-specific; P3's Scope node will be the first real consumer.

## 6. `src/audio/AudioEngine.h`/`.cpp` — the singleton

```cpp
class AudioEngine
{
public:
   static AudioEngine& Instance();

   bool Start(std::string& outError);
   void Stop();

   // Main thread only. Builds a new process list and atomically publishes
   // it; the previous list is retired (not freed) until the NEXT call to
   // SetTopology, guaranteeing the audio thread has finished any in-flight
   // callback against the old list before it's deleted - a topology swap
   // happens at user-interaction rate (patching cables), not per block, so
   // this one-generation grace window is cheap and sufficient.
   void SetTopology(std::vector<AudioNode*> nodes);

   double SampleRate() const;
   uint64_t XrunCount() const;

   // Main thread only: drains MeterRing, pushes any pending ParamMailbox
   // writes queued by node UI this frame. Does no DSP - see the two-object
   // rule above. Real INode integration (calling this from CookIfNeeded)
   // is P3's job; this session's DSPTEST harness calls it directly.
   void PumpMainThread();

private:
   static void RenderThunk(float** buffers, int numChannels, int numFrames, void* userData);
   void Process(float** buffers, int numChannels, int numFrames);

   struct ProcessList { std::vector<AudioNode*> nodes; };
   std::atomic<ProcessList*> mCurrent { nullptr };
   ProcessList* mRetiring = nullptr; // freed on the NEXT SetTopology call

   std::atomic<double> mSampleRate { 0.0 };
   std::atomic<uint64_t> mXrunCount { 0 };
   std::atomic<double> mLastCallbackMs { -1.0 };
};
```

Implementation notes, decided so you don't have to re-derive them:

- **Denormal guard (FTZ/DAZ).** This codebase has no existing
  architecture-conditional code (`grep -rn "__aarch64__\|__x86_64__"
  src/` returns nothing) and the build is a universal `arm64;x86_64`
  binary (`CMakeLists.txt:6-11`), so this is new territory — get it right
  rather than copying an x86-only snippet. Set it once, at the top of
  `Process()` is fine (cheap, and correct even though it's redundant across
  calls on the same thread):
  ```cpp
  #if defined(__x86_64__)
     _mm_setcsr(_mm_getcsr() | 0x8040); // FTZ (bit 15) | DAZ (bit 6)
  #elif defined(__aarch64__)
     uint64_t fpcr;
     __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
     fpcr |= (1ULL << 24); // FZ bit
     __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
  #endif
  ```
  Verify this actually compiles on both slices of the universal build (the
  `#if`/`#elif` should make the unused branch simply not compile for that
  arch — confirm by checking `cmake --build` doesn't warn about it, since
  both architectures are compiled in the same build).
- **Xrun counter.** `AVAudioSourceNode`'s render block has no direct xrun
  notification API (unlike raw AUHAL). Approximate it the same way the P0
  spike already measures jitter (`Platform.mm:1734-1743`): track wall-clock
  gap between `Process()` entries, compare to the expected gap
  (`numFrames / sampleRate`), and increment `mXrunCount` when the actual
  gap exceeds e.g. 1.5× expected. This is a judgment call, not a precise
  xrun count — say so in a comment, and leave the threshold as a named
  constant so it's easy to retune once real patches are running.
- **Topology swap** uses `std::atomic<ProcessList*>` with
  `memory_order_acq_rel` on the publish and `memory_order_acquire` on the
  audio thread's load — standard pointer-publish semantics, no need for
  anything fancier at this scale.
- `Start()` calls `Platform::AudioDeviceOpen(&AudioEngine::RenderThunk,
  this, sampleRate, err)` (the new function from step 1) and stores the
  returned sample rate.

## 7. `INFINITE_DSPTEST` harness

Headless, no device, no GL/GLFW window — this is different from every
other `INFINITE_*` fixture in `main.cpp`, which all run inside the real
render loop. Decide (and state your reasoning) whether to gate it as an
early `return`/`exit` path near the very top of `main()`, before window
creation, since it needs none of the GL/ImGui setup — that keeps it fast
and honest about being a pure-DSP test, not a rendering test that happens
to also touch audio. Follow the existing verdict-printf convention exactly
(a line containing `OK`/`SUSPECT`/`FAIL`, matching e.g.
`main.cpp:11963-11964`).

What it should prove for this session (hardcoded, not driven by real node
types):

1. Build a two-node chain: a hardcoded sine-oscillator `AudioNode` (reuse
   the exact same phase-accumulator math as the P0 spike, not copied from
   anywhere else) into a hardcoded gain `AudioNode` that scales the buffer
   by a `ParamMailbox`-driven gain parameter.
2. Call `AudioEngine::SetTopology({osc, gain})` — note this exercises the
   *engine's* processing path without opening the real device; add a
   `Process()`-equivalent entry point that DSPTEST can call directly with a
   scratch `AudioBuffer`, since DSPTEST explicitly should not touch
   `Platform::AudioDeviceOpen` (no real device in a headless/CI-style run).
3. Render N blocks (pick a fixed `numFrames`, e.g. 512, and enough blocks
   to cover a few cycles of a 440 Hz tone at a fixed test sample rate, e.g.
   48 000 Hz) into a scratch buffer and assert on the samples: peak
   amplitude matches the configured gain, and the waveform's zero-crossing
   rate matches 440 Hz within tolerance (a simple, deterministic way to
   verify frequency without an FFT).
4. Push a `ParamMailbox` gain change mid-run (simulating a main-thread UI
   edit) and assert the output ramps smoothly across the following block
   rather than stepping instantly — this is the direct precursor to the
   plan's later `AUDIOPARAMSWEEPTEST` sweep (P4), so get the assertion
   shape right now: check the *first sample* of the block right after the
   change differs only slightly from the last sample before it (proving
   smoothing, not an instant jump).
5. Verify `MeterRing` : have the gain node (or a trivial pass-through
   meter tap) `Write()` into a `MeterRing` during `ProcessBlock`, then
   `Read()` from the main-thread side after the run and assert the count
   and rough shape (e.g. non-zero, roughly matches expected peak) match.

Print one verdict line per check plus an overall summary line, same
convention as every other fixture.

## 8. Build wiring

`CMakeLists.txt`:
- Add `src/audio/AudioEngine.cpp`, `src/audio/ParamMailbox.cpp`,
  `src/audio/MeterRing.cpp` to the `add_executable(Infinite ...)` source
  list (`CMakeLists.txt:38-91`) — follow the existing flat-list style, no
  glob.
- Add `src/audio` to `target_include_directories`
  (`CMakeLists.txt:107-112`), alongside the existing `src src/core
  src/nodes src/platform`.
- No new framework links needed — `AVFoundation` is already linked
  (`CMakeLists.txt:140`) and this session's Platform-layer addition reuses
  it exactly as `AudioSpike*` already does.

## Build and verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```
Confirm it compiles clean on the universal build (both `arm64` and
`x86_64` slices) — this is exactly the kind of change (arch-conditional
inline asm) that can silently only get tested on one slice if you're not
careful; if you can't cross-check the x86_64 slice's codegen directly,
at minimum confirm the build produces no warnings from the new
`AudioEngine.cpp` about the `#if`/`#elif` branches.

Then run the new headless test:
```bash
INFINITE_DSPTEST=1 build/Infinite.app/Contents/MacOS/Infinite
```
Report every printed verdict line verbatim. All must read `OK` (or your
chosen convention) — if anything is `SUSPECT`/`FAIL`, that's the actual
finding of this session, not something to paper over before reporting.

Do not run `run-infinite-hygiene` for this session — nothing here touches
`INode`, `Patch`, or the node editor yet (that starts at P2/P3). Do not run
the P0 `INFINITE_AUDIOSPIKE` fixture either unless you suspect this
session's changes affected it (they shouldn't — it's untouched code) —
if you do re-run it to sanity-check, report those numbers too.

## Out of scope (explicitly deferred)

- `DspMath` (PolyBLEP, TPT SVF, RBJ biquads, one-pole, dB↔lin, equal-power
  pan, fast tanh) and `AudioVoice` (voice allocator + ADSR) — session 2 of
  P1.
- Any real `INode`/audio-node C++ classes, node-editor cable types, or
  `main.cpp` node-registration changes — P2/P3.
- Wiring `AudioEngine::PumpMainThread()` into the real per-frame render
  loop — there's nothing real for it to pump yet; that lands when P3 adds
  actual audio-backed `INode`s.
- Don't try to make the xrun counter precise — the approximation described
  above is intentional; a precise one needs a raw AUHAL callback with real
  overload flags, which is out of scope unless the P0-style numbers from
  this session's testing show the approximation is actively misleading.
