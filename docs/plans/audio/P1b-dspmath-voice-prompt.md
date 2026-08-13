# P1 (session 2 of 2) — DspMath, AudioVoice, and three standards items

Goal: add `src/audio/DspMath.h` (PolyBLEP oscillator, TPT/Zavalishin SVF,
RBJ biquad cookbook filters, one-pole, dB↔lin, equal-power pan, fast tanh)
and `src/audio/AudioVoice.h`/`.cpp` (round-robin + oldest-steal voice
allocator + ADSR), then upgrade the P1 exit criterion from session 1's
"osc→gain→out" to the full plan's **"osc→filter→out"** by swapping
`DspTest::GainNode` for a filter built on the new SVF. This session also
closes out three items from
`docs/plans/optimization/research-implementation-map.md` §1 that are
explicitly gated on `src/audio/` existing and must land before P2: the
real-time-safety comment block (1.1), the audio/visual cook-rate decision
(2.1), and a verified go/no-go on RTSan (1.3).

**Clean-room rule, verbatim:** do not open, read, grep, or reference
`/Users/namansoni/BespokeSynth`. Implement every DSP algorithm from its
primary reference (Zavalishin's TPT SVF paper/book, the RBJ Audio EQ
Cookbook, PolyBLEP's original band-limited-step papers), not from any
existing codebase's source.

## What already exists — read before writing anything

Session 1 landed and is verified working (`INFINITE_DSPTEST` passes clean,
confirmed by running it just now):
```
DSPTEST peak amplitude: expected 0.500  got 0.5000  OK
DSPTEST zero-crossing freq: expected 440.0 Hz  got 435.94 Hz  OK
DSPTEST gain smoothing: |delta| = 0.0253 (instant jump would be ~0.4)  OK
DSPTEST meter ring: expected 12 entries  got 12  OK
```
- `src/audio/AudioBuffer.h` — the planar, non-owning buffer view.
- `src/audio/AudioNode.h` — the `PrepareToPlay`/`ProcessBlock`/`Reset`
  interface. `ProcessBlock` runs in place on a shared scratch buffer.
- `src/audio/AudioEngine.h`/`.cpp` — the singleton: `Start`/`Stop`,
  `SetTopology` (atomic pointer-publish with one-generation-retired
  cleanup), `ProcessOffline` (the headless entry point DSPTEST uses),
  `Process`/`RenderThunk` (the real-time callback — already sets the
  FTZ/DAZ denormal guard for both `x86_64` and `aarch64` at the top of
  `Process`, `AudioEngine.cpp:90-99`; you do not need to repeat this in
  `DspMath` or `AudioVoice`, it's already covering every node in the
  chain).
- `src/audio/ParamMailbox.h`/`.cpp` — **just fixed this session** (see
  below), now a flat `std::atomic<float>[kMaxParams]` "latest value wins"
  array, not a ring. Use `Push`/`SmoothedValue`/`SetImmediate` exactly as
  `DspTest::GainNode` does at `main.cpp:6657-6693` — that's the reference
  usage pattern for any new node needing a smoothed param (an ADSR's
  target level, a filter's cutoff, etc.).
- `src/audio/MeterRing.h`/`.cpp` — correct SPSC ring (producer only
  touches `mTail`, consumer only touches `mHead`) — use this as the
  reference shape if `AudioVoice` needs any audio→main reporting (e.g. a
  voice-active count for future UI); it is **not** what `ParamMailbox`
  should have used, see next paragraph.

### A bug found and fixed in this session's prep, not by you — for context only

`ParamMailbox` originally used a head/tail ring (mirroring `MeterRing`'s
shape) for the main→audio param path. That was a real bug: `Push`'s
overrun-drop path (`if (next == head) mHead.store(...)`) had the
**producer** (main thread) writing the **consumer**-owned `mHead` index,
which `Pop()` (audio thread) also wrote — two threads mutating the same
index breaks the single-consumer contract the ring depends on, and could
corrupt the ring or lose/duplicate a pending value under real concurrent
load (a single-threaded test run, like `INFINITE_DSPTEST`, would not have
caught this — it only manifests under actual thread interleaving). It's
already fixed: `ParamMailbox` is now the flat atomic-array design
`docs/plans/optimization/research-implementation-map.md:37-45` recommended
in the first place ("a single `std::atomic<T>`-guarded index swap... if the
only payload is latest-value-wins, true for most knob-driven params").
Nothing for you to do here — just don't reintroduce a ring for a
continuous param path in the new code this session adds.

## 1. `src/audio/DspMath.h` — free functions and small stateful structs

Header-only is fine here (small, inlinable, no `.cpp` needed) unless a
piece genuinely needs private state that doesn't belong in a caller's
struct. Implement, each against its named primary source:

- **PolyBLEP oscillator.** Band-limited sawtooth/square via polynomial
  band-limited step correction at discontinuities — the standard technique
  from Välimäki & Huovilainen's band-limited oscillator work. A stateful
  `struct PolyBlepOsc { double phase, phaseInc; float Sine(); float
  Saw(); float Square(float pulseWidth); void SetFrequency(double hz,
  double sampleRate); };` matches how `DspTest::SineOscNode` already shapes
  a phase-accumulator oscillator (`main.cpp:6630-6655`) — mirror that
  shape so a later P3 session can lift this in directly.
- **TPT (topology-preserving transform) SVF** — Zavalishin's "The Art of
  VA Filter Design," the 1-pole-integrator-per-state trapezoidal design
  (sometimes called the "Zavalishin SVF" or "cytomic SVF"). Outputs
  low/high/band/notch simultaneously from one set of state variables per
  sample — this is what session 1's exit criterion needs swapped in for
  `GainNode` (see step 3).
- **RBJ biquad cookbook** — Robert Bristow-Johnson's Audio EQ Cookbook
  coefficients (peaking, low/high shelf, low/high/band-pass, notch,
  all-pass) as a `struct Biquad { float b0,b1,b2,a1,a2; float
  x1,x2,y1,y2; float Process(float in); void SetPeaking(double freq,
  double q, double gainDb, double sampleRate); /* ...other Set* per type */
  };` (direct form I is fine at audio rates here; don't over-engineer to
  direct form II unless a coefficient-modulation artifact actually shows
  up later).
- **One-pole smoother** — same math `ParamMailbox::SmoothedValue` already
  uses (`ParamMailbox.cpp` — `target + (current - target) * coeff`,
  `coeff = expf(-1/(timeConstant*sampleRate))`); factor it out as
  `OnePole` here so `ParamMailbox` and future DSP code share one
  implementation instead of two copies of the same formula drifting apart.
  Update `ParamMailbox.cpp` to call it once you've added it — small,
  mechanical, do it in this session since you're touching both files
  anyway.
- **dB↔lin** — `LinearToDb`/`DbToLinear`, standard `20*log10`/`10^(db/20)`.
- **Equal-power pan** — `void EqualPowerPan(float pan /*-1..1*/, float&
  leftGain, float& rightGain)` using the standard quarter-sine-wave law
  (`sin`/`cos` of `(pan+1)*pi/4`).
- **Fast tanh** — a rational/polynomial approximation (e.g. the common
  Padé-style `x*(27+x*x)/(27+9*x*x)` clamped form) for cheap saturation;
  cite whichever concrete approximation you pick in a one-line comment so
  its error bound is traceable later, don't just drop unexplained magic
  constants.

## 2. `src/audio/AudioVoice.h`/`.cpp` — voice allocator + ADSR

```cpp
class Envelope // ADSR, audio-rate
{
public:
   void SetSampleRate(double sr);
   void SetADSR(float attackMs, float decayMs, float sustainLevel, float releaseMs);
   void NoteOn();
   void NoteOff();
   float Process(); // advances one sample, returns 0..1
   bool IsActive() const; // false once fully released
};

class VoiceAllocator // fixed voice count, round-robin + oldest-steal
{
public:
   explicit VoiceAllocator(int maxVoices);
   int NoteOn(int midiNote, float velocity); // returns voice index (steals oldest if all busy)
   void NoteOff(int midiNote);               // releases the matching voice's envelope, doesn't free the slot yet
   // ... whatever accessors a synth built on top of this needs to read
   // per-voice note/velocity/envelope state each block
};
```
Fixed-size arrays (`maxVoices` decided at construction, no
allocation in `NoteOn`/`NoteOff`/per-block use) — this is audio-thread code
by the time a real synth calls it in P3, so it inherits every prohibition
`AudioNode::ProcessBlock` already carries. `NoteOn`/`NoteOff` here don't
need to be literally called from the real-time thread in *this* session
(there's no MIDI wiring yet), but design them as if they will be, since
that's the actual P3 usage.

Steal policy: round-robin first (cycle through voices, prefer one that's
already inactive), fall back to stealing the oldest active voice if every
voice is busy — matches the plan's own wording exactly
(`docs/plans/audio/README.md`, P1 bullet list).

## 3. Upgrade the DSPTEST fixture to the real exit criterion

Session 1 shipped `DspTest::GainNode` (`main.cpp:6657-6693`) as a
stand-in because `DspMath` didn't exist yet. Now that TPT SVF exists,
replace it (or add alongside — your call, but the plan's exit phrase is
literally "osc→filter→out", so at least one variant of the DSPTEST chain
should end in a real filter) with a `FilterNode : public AudioNode`
wrapping the new SVF, low-pass mode, cutoff driven through
`ParamMailbox` exactly like `GainNode` drove gain. Assert something a
filter actually proves that a gain stage can't: e.g. sweep the cutoff
below the 440 Hz test tone's frequency mid-run and assert the output
amplitude drops (the tone gets attenuated), which a gain-only chain has no
way to exercise. Keep the existing gain-smoothing and meter-ring
assertions from session 1's chain either in the same fixture or a second
`INFINITE_DSPTEST`-gated block — don't delete working coverage to make room
for the new one.

## 4. Real-time-safety comment block (optimization doc 1.1)

Add a comment block — not new code, a documentation artifact the doc
explicitly asks for — at the top of `AudioNode.h`, above the class, stating
the render-thread constraint list explicitly: no allocation, no locks with
unbounded wait, no syscalls, no unbounded loops, in addition to the
`dynamic_cast`/`std::function`/`map`/`string`/GL/ImGui/file-I/O/`printf`
list already there. `AudioNode.h` already has a version of this comment
(see the current header) — extend it to include the "no unbounded loops"
and "no syscalls" clauses verbatim from the optimization doc rather than
leaving them implicit.

## 5. Audio/visual cook-rate decision (optimization doc 2.1) — a decision to write down, not code

This is explicitly flagged 🔴 "must be decided before P2 starts" in the
optimization doc, and P2 is the very next phase after this one, so it has
to happen now. The question: when a real audio node (P3) reads a *visual*
modulator's value (an LFO driving both a visual param and an audio
param, say), does it read the last value the visual graph cooked on the
main/render thread (TouchDesigner-style — one render-frame of latency, but
trivially real-time-safe: it's just reading a value already sitting in
memory), or does it trigger the visual modulator to cook synchronously from
the audio thread (Houdini-style — zero extra latency, but means arbitrary
main-thread node code, potentially allocating/locking/touching GL state,
could run on the real-time audio thread)?

**Given every constraint this phase has been enforcing** (no allocation,
no locks, no GL on the audio thread), triggering a synchronous cook from
the audio thread is not just harder, it's disallowed by rules this exact
document has been asserting as load-bearing since session 1 — so the
decision is TouchDesigner-style: **an audio node reads the last value a
visual modulator produced on the main thread, written into a plain
`std::atomic<float>` the main thread updates once per render frame and the
audio thread reads (never writes) each block.** This is state that any
future `IModulator`-implementing node (`Modulation.h`) can be made to
publish alongside its existing `Value01()`, once P3 gives audio nodes a
concrete way to reference one. Write this decision down as a short section
in `docs/plans/audio/README.md` (append, don't rewrite the existing
document) or as a new `docs/plans/audio/cook-rate-decision.md` — your call
which reads better, but it must be **written somewhere a P2/P3 session
will find it**, not left only in this prompt.

## 6. RTSan (optimization doc 1.3) — verified go/no-go, not a build change

**Checked, not assumed:** the toolchain actually building this project is
Apple Clang (`clang --version` → `Apple clang version 21.0.0`, from
`/Library/Developer/CommandLineTools`), and it does **not** support RTSan:
```
$ clang++ -fsanitize=realtime test.cpp -o test
clang++: error: unsupported option '-fsanitize=realtime' for target 'arm64-apple-darwin27.0.0'
```
Upstream RTSan (`-fsanitize=realtime`) needs a vanilla LLVM/Clang build
(19+), not Apple's fork, and no such toolchain is installed on this
machine (`brew list llvm` → not installed). This is not a CMake flag away
— it needs a separate compiler entirely, which means either a second build
configuration pointed at a Homebrew LLVM install the user would have to
install first, or CI running on a different toolchain than local dev.

**Do not attempt to wire this up in this session.** Instead: confirm this
finding is still accurate (`clang --version`, retry the one-line compile
check above — toolchains do get upgraded), and if still blocked, note it
plainly in whichever doc you wrote the cook-rate decision into (§5) as "not
available on this machine's toolchain; revisit if/when a
Homebrew-LLVM-based build config exists," rather than silently dropping
the optimization doc's 🟡 item. That keeps a future session from
re-discovering the same dead end.

## 7. Pure Data patch-shape skim (optimization doc 1.5) — five minutes, before P2's node list is locked

The optimization doc flags this as "skim [the MSR 2024 'Opening the Valve
on Pure-Data' paper] before finalizing which audio node types ship in
P2/P3" — a sanity check on the §3 node consolidation in
`docs/plans/audio/README.md`, not an implementation task. If you have web
access in this session, spend five minutes on it and note in the same doc
from §5 whether anything about the plan's 30-node consolidation looks off
against real patch-shape data (fan-out depth, feedback prevalence,
subpatch nesting). If you don't have web access, say so explicitly and
leave this as an open item for whoever writes the P2 prompt — don't
fabricate a finding.

## Build wiring

`CMakeLists.txt`: add `src/audio/AudioVoice.cpp` to the source list
(`DspMath.h` is header-only, no `.cpp` to add unless you end up needing
one). No new framework links.

## Build and verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```
Confirm clean on the universal build, then:
```bash
INFINITE_DSPTEST=1 build/Infinite.app/Contents/MacOS/Infinite
```
Report every verdict line verbatim, same as session 1. All prior session-1
checks (peak amplitude, zero-crossing frequency, gain smoothing, meter
ring — or their equivalents if you restructured the fixture) must still
read `OK`; the new filter-sweep check must too.

Do not run `run-infinite-hygiene` — still nothing here touches `INode`,
`Patch`, or the node editor. That starts at P2.

## Out of scope (explicitly deferred)

- Any real `INode`/node-editor integration, cable types, `main.cpp`
  node-registration — P2/P3, unchanged from session 1's scope note.
- Wiring the cook-rate decision's `std::atomic<float>` publish path into
  any real `IModulator` — that's P3's job once audio-backed `INode`s
  exist; this session only needs the decision written down and the
  mechanism named, not built.
- Installing Homebrew LLVM or setting up a second build configuration for
  RTSan — flagged as blocked, not actioned, per §6.
- 4–6-operator FM, microtuning, MPE — out of scope for the whole audio
  project per `docs/plans/audio/README.md` §8, not just this session.
