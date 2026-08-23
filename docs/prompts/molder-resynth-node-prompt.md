# Add the Molder node — analysis / genome / additive-resynthesis synth

## What this is

A sample-mangling synth built on **spectral analysis → mutable genome →
additive resynthesis**, not on playback effects. You drop a sample in; the
node decomposes it into a bank of tracked harmonic partials plus a real
residual waveform, then a "roll" mutates a parameter genome and re-renders a
new sample from that model. Rolling again mutates further from the current
genome, so the node walks a space of related-but-different sounds rather than
producing one fixed transformation. The result is an ordinary playable sample
buffer on a `Synths`-category node with a note input.

The design is the author's own (a working version of it ships in their
BespokeSynth fork). **Do not read, grep, or copy from
`/Users/namansoni/BeSpokeMod` or `/Users/namansoni/Documents/BespokeSynth`** —
that tree is GPLv3 and Infinite is MIT. Everything you need is specified
below in prose and math; implement each algorithm from its primary reference
(de Cheveigné & Kawahara 2002 for YIN; Portnoff / Dolson for STFT
phase-vocoder instantaneous frequency; Serra & Smith 1990 for
sinusoids-plus-residual; Fletcher & Rossing for the stiff-string
inharmonicity law). This is a clean-room reimplementation, and it is also
better than the original in six specific ways listed in §4 — you are not
porting, you are building the improved version.

---

## 1. Two things to know before you design anything

### 1a. Do NOT port the XY effects pad

The original module wraps its resynth engine in an XY pad that cross-fades
ten live effects (distortion, pitch, reverb, freq shift, delay, stutter,
bitcrush, filter, repeater, reverse). **That pad exists because BespokeSynth's
author wanted an effects chain inside one module.** Infinite already has every
one of those as a first-class node in the `EffectDef` table
(`src/audio/EffectDefs.h` — Drive, Pitch Shifter, Reverb, Frequency Shifter,
Delay, Stutter, Bitcrush, Audio Filter, ...), plus `MacroNodes`' XY macro to
sweep them, plus a patch cable to route them. Rebuilding the pad inside this
node would be duplicating the host.

Ship the engine only. If the user wants the pad behaviour, that is
`Molder → Drive → Pitch Shifter → Reverb` with a Macro XY bound to three
params — which is strictly more capable, and is the reason Infinite is a
graph. This decision is already made; do not reopen it, and do not add a
"fx" section to the node.

### 1b. The FPS constraint dictates the thread architecture, not the DSP

Analysis is a full STFT over the whole sample plus per-frame partial
tracking. At hop 256 / FFT 2048, a 30-second sample is ~5,200 frames — on the
order of 100–300 ms of work. Infinite runs its UI, its GL render and its node
cook on one thread at 60 fps (16.6 ms/frame). **Doing analysis anywhere on
that thread drops 6–18 frames every time the user clicks roll**, and the
existing audio nodes' `CookIfNeeded` budget is < 5 µs
(`.claude/skills/new-audio-node/SKILL.md` §0.3).

So this node is a **three-thread** node, unlike every other audio node in the
codebase, which are two:

| Thread | Owns | Never does |
|---|---|---|
| Main | UI, params, `VisitParams`, waveform cache, publishing buffers | any FFT, any additive render |
| **Worker** (`std::thread`) | `AnalyzeSample()`, `MutateGenome()`, `RenderFromGenome()` | touch ImGui, GL, the audio node, or any main-thread field except through the handoff |
| Audio | reading a finished `Platform::SampleBuffer` — interpolate, envelope, pan | everything else |

The worker pattern already exists here, twice: **`src/audio/SampleScanner.cpp`
(thread spawn at line 141, main-thread `PollResults()` at line 221) and
`src/audio/PluginScanner.cpp` (line 95 / line 132)**. Copy that shape exactly:
worker writes into a `mPendingResult` it owns, flips one
`std::atomic<bool> mResultReady`, main thread's `CookIfNeeded` does a single
relaxed load and takes the result if set. Do not invent a different handoff,
and do not use a mutex on the cook path.

The finished buffer then goes to the audio thread through the **existing**
`SampleSlotT<Platform::SampleBuffer>` (`src/audio/SampleSlot.h`) — main thread
`Push()`, audio thread `SwapIn()` at the top of `ProcessBlock`, main thread
`DrainRetired()` once per cook. Three existing nodes already do this
(`ImageSpectralSynthNode.cpp:801`, `EquationNode.cpp:504`,
`WaveTerrainNode.cpp:575`). Do not hand-roll a fourth variant.

**Drawing is the other FPS cost and it is bigger than the DSP.** Follow
`PaulStretchNode`'s precomputed waveform cache exactly
(`src/nodes/PaulStretchNode.h:66-70`, built in `FinishBuffer` at
`PaulStretchNode.cpp:700`): 256 min/max buckets computed once when a buffer is
published, never per frame. The partial-spectrum visualizer draws at most 128
bars, likewise from a cache rebuilt only when a new render lands.

---

## 2. Node shape and wiring — exact sites

`INode` + `IAudioSource`, mirroring `SamplerNode` (`src/nodes/SamplerNode.h:34`):

- **Slot 0** — `NoteInputSlot(0)` → `noteInput`, label `"notes"`.
- **Slot 1** — `AudioInputSlot(1)` → `audioInput`, label `"record in"`.
  (Note and audio pins share one slot index space — answering both from slot 0
  is a known trap, see the skill's §4.)
- Stereo audio out via `GetAudioNode()`.

Registered name: **`Molder`**, category **`Synths`**.

> This is a judgment call and it is the one I'd flag. `Resynth` is **already
> taken** by the image node (`src/nodes/ResynthNode.h`, registered at
> `src/main.cpp` in `RegisterNodes()`), and `NodeFactory::DuplicateNames()`
> will fire at runtime if you reuse it. `Molder` matches the author's own
> naming for this engine. If they'd rather, `Genome` is free and describes
> the mechanism better. Pick `Molder` unless told otherwise; do **not** pick
> `Resynth`, `Sample`, `Spectral` (collision risk) and remember the category
> string must be one whitespace-free token or `Patch.cpp`'s `>>` read
> corrupts the save file.

Wiring sites, all in `src/main.cpp` (line numbers verified against the current
36,311-line file — the ranges in `ARCHITECTURE.md` are stale, it still claims
9,146 lines, so trust these and re-grep rather than trusting that doc):

| What | Where |
|---|---|
| `#include "nodes/MolderNode.h"` | with the others, ~line 117 (`PaulStretchNode.h` is there) |
| `REGISTER_NODE(MolderNode, Molder, "Synths")` | `RegisterNodes()` at line 2349; put it next to `PaulStretchNode` at line 2491 |
| display-name lowercasing | the `if (name == ...)` chain at line 295-305 — add `if (name == "Molder") return "molder";` only if you want a non-default string; the default lowercases already, so this is optional |
| `ReloadDerivedState` | line 3425-3445 — add `if (auto* m = dynamic_cast<MolderNode*>(node)) m->ReloadFromPath();` alongside the `PaulStretchNode` line at 3436. **Required**, or a loaded patch shows the file name but has no audio |
| `IsAudioBodyNode` | line 5754 — already generic off the interfaces, verify no edit needed |
| `DrawMolderBody` + dispatch branch | write the body near `DrawPaulStretchBody` (line 8484); add the `else if (auto* n = dynamic_cast<MolderNode*>(...))` branch in `DrawAudioNodeBody` at line 13688 (the `PaulStretchNode` branch is line 13706) |
| sample drag-drop onto an existing node | line 34755-34782, the `FindNodeUnderCanvasPoint<T>` chain — add a `MolderNode` arm so dropping a sample on it swaps the file |
| help text | `SpecificNodeHelpText` at line 16662 — one sentence, existing voice |
| DSP fixture | model on `RunPaulStretchFixture()` at line 20939, called at line 23067 |
| `CMakeLists.txt` | `src/audio/dsp/MolderDsp.cpp` near line 192; `src/nodes/MolderNode.cpp` near line 197 |

Everything else — connect/disconnect, cycle detection, patch save/load, undo,
copy/paste, pin counting, topology rebuild — is generic. `InputCountFor` is
**not** a site any more; it probes `AudioInputSlot`/`NoteInputSlot`. If you
find yourself adding this node's name to any of those, you built it wrong.

**Two docs the skills reference no longer exist in this repo:**
`docs/plans/audio/README.md` and `docs/plans/audio/audio-node-ui-system.md`
were removed. The `new-audio-node` and `audio-node-ui` skills still cite them.
The surviving source of truth for the UI grammar is the **`v3 audio node
layout` block in `src/main.cpp` starting at line 5329** — `BeginAudioBody`,
`BeginAudioSection`, `AudioKnobRow`, `AudioSlider`, `KnobFloat`. Read that
block, don't go looking for the missing markdown.

---

## 3. The engine

Put all of it in `src/audio/dsp/MolderDsp.h`/`.cpp` as free functions and POD
structs with no dependency on `INode`, ImGui or GL, so the worker thread can
call it and the DSP fixture can test it headlessly. `MolderNode.cpp` owns the
thread and the UI; `MolderDsp` owns the math.

### 3a. Analysis — `Analyze(const float* mono, int len, double sr, Analysis& out)`

Runs once per source sample, on the worker.

1. **Global pitch + confidence, via YIN.** Cumulative-mean-normalized
   difference function over the loudest 8192-sample window (find it with a
   subsampled energy scan, stride `len/32`). Search lags for 40 Hz–1500 Hz.
   Take the **first** dip below a 0.15 threshold, not the global minimum —
   that's the octave-error guard. Then apply an explicit octave correction:
   for `multiple` in {2, 3}, if `norm[bestLag*multiple] < norm[bestLag]*1.15`,
   take the longer lag. Confidence is `1 - norm[bestLag]`.
   **Do not use plain autocorrelation.** It is maximal at the shortest lag for
   any bass-heavy signal (at 50 Hz, 29 samples of shift barely moves the
   waveform, so it self-correlates ~0.9) and will report ~1500 Hz for every
   kick drum. This is the single most important correctness detail in the
   whole node.
2. **STFT.** 2048-point Hann, hop 256. Step the hop to 512 above 5 s of
   material and 1024 above 20 s — the extra resolution only matters on
   one-shots and it is a linear cost multiplier on long files. Frames are
   **centred** on `frame*hop` (start at `frame*hop - fftSize/2`, zero-pad past
   both ends) so overlap-add covers sample 0 at full amplitude; otherwise the
   attack — exactly where a drum lives — reconstructs quiet.
   Use vDSP for the transform (`#include <Accelerate/Accelerate.h>`,
   `vDSP_create_fftsetup` / `vDSP_fft_zrip`); `PaulStretchNode.cpp:112` and
   `:366` show the real-to-complex setup and `vDSP_ctoz` packing already used
   here. Create the setup once, on the worker, never per frame.
3. **Per-frame f0 tracking.** Parabolic peak interpolation on the magnitude
   spectrum within `[0.55, 1.8] × trackedF0`, gated on the peak being at least
   10% of the frame max. Glide 50/50 toward the new estimate, clamped to
   `±1 octave` of the global f0. This is what reproduces a kick's downward
   pitch sweep instead of smearing 48 partials across it.
4. **The voicing gate — two stages, both required.**
   - *Periodicity*: normalized autocorrelation at the single lag
     `sr/trackedF0` over the frame, with a silence gate (`energy/n < 1e-8` →
     0). Cheap, one lag.
   - *Harmonic salience*: sum spectral energy in a `±radius` window **on** each
     harmonic `f0·(h+1)`, and in the same-width window **between** them at
     `f0·(h+1.5)`. `salience = on / (on + between)`; 0.5 means the harmonics
     are no better populated than the gaps, i.e. noise. Map
     `(salience-0.5)*2` through a smoothstep over `[0.05, 0.25]` and multiply
     into the periodicity.
     A cymbal's metallic resonances *are* periodic — plain autocorrelation
     reports 30%+ on a hi-hat — so periodicity alone is not enough. Both
     windows sit adjacent in frequency, so the sample's overall spectral tilt
     cancels between them, which is precisely what a wideband spectral-flatness
     measure gets wrong (a hi-hat's steep slope reads as "peaky" to flatness).
   - Below a **global** confidence of 0.25, force voicing to 0 for every
     frame. Hat, clap, field recording: the residual simply *is* the sample,
     which is the correct answer.
   - `radius = clamp(trackedF0/binHz * 0.4, 1, 8)` — a **spacing-proportional**
     capture width, not a fixed ±2 bins. At f0 = 50 Hz a fixed ±2 bins spans
     ±43 Hz against 50 Hz spacing, so neighbouring partials overlap and every
     one reads the same bogus amplitude.
5. **Partial extraction** (see §4 for what's new here). Per frame, per partial:
   peak magnitude in the capture window, scaled by voicing; mark those bins
   `claimed`.
6. **Residual — keep the actual waveform, not band energies.** Attenuate every
   claimed bin by `(1 - voicing)`, **leave the phases untouched**, inverse-FFT,
   overlap-add. This is the whole reason a click survives as a click:
   amplitude-per-band synthesis can reproduce a transient's spectrum but never
   its phase alignment, so an impulse comes back out as a noise burst. When
   voicing is ~0 for a frame, skip the inverse transform entirely and
   overlap-add the windowed input with a matching gain — on polyphonic
   material that's every frame, and it halves the analysis cost.
7. **Scaling is fixed and analytic, never normalized-by-observation.** A
   Hann-windowed sine of amplitude A reads as `A·N/4` in the magnitude
   spectrum, so partial envelopes get `4/N`. Residual OLA gets
   `4·hop/N²` (undoing the inverse transform's `N/2` and the Hann window sum's
   `N/(2·hop)`).
   **Do not normalize the partial bank so that its loudest partial matches
   some fraction of the source peak.** That divides by a near-zero value on
   barely-tonal material and ignores that N partials *sum* — the additive
   stack then drowns the residual, the final peak-normalize crushes the real
   audio away, and every genome converges to the same synthetic timbre
   regardless of what was dropped in. This is a bug that has actually shipped
   in the author's earlier version; the analytic constant is the fix.

### 3b. Genome

```
struct Genome {
   float partialAmp[kMaxPartials];   // multiplier, 1.0 = as analysed
   float bandAmp[kNumBands];         // residual EQ, 1.0 = untouched
   float noiseAmount, tonalAmount;
   float attackScale, decayScale, decayTilt;
   float brightnessTilt;             // -1..1
   float inharmonicity;              // scales measured deviation, see 4c
   float harmonicStretch;            // 0.80..1.20, freq = f0·(h+1)^stretch
   float pitchShiftSemitones;
   bool  reverseResidual;
};
```

Baseline is all-1.0/0.0 and **must reconstruct the source** — that is the
fixture assertion in §6.

`Mutate(Genome&, float strength, Rng&)`:
- Reversion first: pull every field 10% toward baseline *before* this
  generation's jitter. Without it, a partial knocked to zero can never come
  back and repeated rolls become a one-way ratchet into silence.
- Per-partial jitter is **log-normal** (`amp *= exp(uniform(-s, s))`) so it
  stays positive and is symmetric in dB.
- The structural moves are what make a roll sound like a *different sound*
  rather than a slightly-different one; per-partial jitter alone averages out
  perceptually. Include: keep-every-Nth-partial decimation (hollow/clarinet),
  amplitude shuffling between partials (reads as a formant shift), outright
  partial dropouts, a real ±10-semitone pitch walk, residual reverse.
  Each of those fires with probability proportional to `strength`.
- `strength` = `chaos` param, scaled `[0.25, 1.3]`.

**The RNG must be a seeded xorshift stored on the node, not a global.** See
§4f — this is a real improvement over the original and it is load-bearing for
save/load.

### 3c. Render — `Render(const Analysis&, const Genome&, std::vector<float>& out)`

Per output sample:
- Interpolate the two bracketing analysis frames for `f0` and each partial's
  amplitude.
- Skip a partial entirely if `genome.partialAmp[h] < 1e-4` **or**
  `analysis.partialPeak[h] < 1e-5`. Checking only the genome means polyphonic
  material (voicing 0 → every envelope 0) still runs every oscillator to
  produce silence, which is the dominant cost of rolling a full song.
- **Hard Nyquist skip at `0.45·sr` per partial per sample.** Without it,
  partials above Nyquist fold back as broadband garbage indistinguishable from
  white noise, and it gets worse the more partials you have.
- Hoist per-partial frequency ratio and amplitude weight out of the sample
  loop — they depend only on the genome, never on time. Two `powf`s per
  partial per sample is the difference between an instant roll and a
  multi-second one.
- Residual: read through the same time warp, cubic-interpolated, then apply
  the band EQ **additively** (`res + Σ band(res)·(gain-1)`) so an all-1.0
  genome passes the residual through bit-exact instead of through N
  overlapping bandpasses that ripple its spectrum.
- **Time warp must be length-preserving and monotonic.** Precompute a
  2049-point curve mapping output time → source time: linear from 0 to a pivot
  at the peak frame, then `pivot + u^decayScale · (total - pivot)` after it.
  The naive form (`t/attackScale`, then `pivot + (t-pivot)/decayScale`) runs
  the read position off the end of the buffer at low decayScale and then holds
  the final sample — silence, mid-render, which reads as a dropout.

---

## 4. Six things this version does that the original does not

These are the answer to "how do we make it harmonically rich". Implement all
six; they are not optional polish.

**4a. Seed partial phases from the analysis instead of from zero.**
The original starts every oscillator at phase 0 and integrates, so the
additive stack's attack is a zero-phase impulse — it sounds like an organ
chord regardless of source. Capture `atan2(imag, real)` at each partial's
peak bin **at the peak frame**, and seed `phase[h]` with it (back-propagated
to sample 0 by `-2π·f_h·t_peak`). Costs one float per partial and it is the
single biggest "sounds like the sample" improvement available.

**4b. Take partial frequency from the phase derivative, not the magnitude
peak.** Standard phase-vocoder instantaneous frequency: for a bin at frame
`n`, unwrap `Δφ = φ_n - φ_{n-1} - 2π·binFreq·hop/sr` into `(-π, π]` and add
back, giving the true frequency inside the bin. A magnitude peak is quantized
to `sr/N` = 21.5 Hz at 2048/44.1k, which is a quarter-tone at 200 Hz and
destroys exactly the small detunings that make an instrument sound alive.

**4c. Store measured inharmonicity, don't only synthesize it.**
Keep `measuredRatio[h] = f_measured[h] / f0` from analysis rather than
assuming `h+1`. The genome's `inharmonicity` then scales *deviation from the
measured ratio*, so 0 reproduces the real instrument's stretch (a piano's
stiff-string law `f_n = n·f0·√(1+Bn²)`, a bell's non-integer modes) and >1
exaggerates it. The original could only add synthetic stretch to a perfect
harmonic series, which is why every roll drifted toward the same
generic-metallic character.

**4d. Split the residual in two.** One residual level knob boosts hiss and
transients together. Classify each analysis frame by **spectral flux**
(sum of positive magnitude differences vs the previous frame) — high-flux
frames are transient, the rest are steady. Build two residual buffers with a
crossfaded mask and give the genome separate `transientAmount` and
`noiseAmount`. Now "more air, same attack" and "same air, harder attack" are
both reachable, and percussion has somewhere real to evolve.

**4e. Preserve formants across pitch shift.** Store the spectral envelope
(peak-interpolate across partial magnitudes at the peak frame, or a 30-order
real cepstrum — the peak interpolation is cheaper and adequate here). After a
pitch shift moves `f0`, re-weight each partial's amplitude by the **original**
envelope evaluated at the partial's **new** frequency. Without this, shifting
up chipmunks the timbre; with it, a voice shifted an octave still sounds like
the same voice at a different pitch. This is what separates it from a
resampler.

**4f. Make a roll reproducible.** The original draws from a global RNG, so a
saved patch cannot reproduce its own sound — reload it and the genome is
whatever was serialized, with no way to get back to it or step forward
identically. Store an explicit `uint32_t seed` plus `int generation` on the
node, run a local xorshift32 seeded from `seed`, and replay `generation`
mutations deterministically on load. Two consequences, both good:
`VisitParams` saves **two integers** instead of ~150 floats, and "roll back
one" becomes trivially implementable (re-run `generation-1` mutations from
the seed) rather than impossible.

**4g (cheap, do it if the body has room).** Alternate partials get ±2–4 cents
of fixed micro-detune and a small alternating pan. The additive stack stops
being a dead-centre mono spike and gains width. Two lines in the render loop.

---

## 5. UI — the controls, and the cutting rule

`.claude/skills/new-audio-node/SKILL.md` §0.5 is binding: **Tier 1 only,
~8 controls max excluding the visualizer, one processing mode.** KHS Audio's
Delay (7 controls) is the bar, not a design document's full feature table.
The engine above has ~15 exposable genome fields; most of them must **not**
become knobs — they are mutation targets, not user controls.

Ship exactly these:

| Control | Range | What it is |
|---|---|---|
| `chaos` | 0..1 | mutation strength for the next roll |
| `pitch` | -24..+24 st | offset on top of the genome's own walk |
| `tone` | 0..1 | tonal-vs-residual balance (drives `tonalAmount`/`noiseAmount` as a crossfade) |
| `air` | 0..1 | steady-residual level (4d) |
| `snap` | 0..1 | transient-residual level (4d) |
| `stretch` | 0..1 | scales genome `inharmonicity` + `harmonicStretch` together on one knob |
| `time` | 0..1 | one knob mapped to attack/decay warp (centre = unwarped) |
| `level` | 0..2 | output |

Buttons: **roll**, **iterate** (feed the render back in as the new source and
re-analyse — the Lucier "I Am Sitting In A Room" move, where the synthesis
model progressively eats the source; hold the genome fixed and let only the
re-analysis change, so it's deterministic), **reset**, **load**.
Readouts, not knobs: `seed`, `generation`, the analysed `f0`, and the
harmonicity percentage.

Visualizer: one full-width area, waveform (256-bucket cache) with the playhead
overlaid; a partial-spectrum strip of ≤128 bars underneath it. Both rebuilt
only when a new buffer is published, never per frame. Cap redraw at 30 Hz and
collapse by default — 20 audio nodes each drawing 1024 ImGui segments costs
more than every synth in the patch combined.

Body width `kAudioNodeWidth` (440). Do not use `kAudioWideWidth`.

---

## 6. Tests — write them with the node

Add `INFINITE_MOLDERTEST` as a fixture in `src/main.cpp`, modelled on
`RunPaulStretchFixture()` (line 20939, called at 23067). It must run headless
with no audio device, print a verdict line ending `OK` or containing `FAIL`,
and assert:

1. **Pitch.** Synthesize a 220 Hz sawtooth, 2 s. `Analyze` reports f0 within
   1%. Then a 55 Hz sine — still within 1% (this is the autocorrelation trap;
   a broken detector reports ~1500 Hz here and the test must catch it).
2. **Voicing gate.** White noise → `globalVoiced < 0.25` and every frame's
   voicing is 0. A hi-hat-like burst (noise through a 6 kHz highpass with an
   exponential decay) → same. A sawtooth → voicing > 0.5 across its sustain.
3. **Reconstruction — the strong one.** With the **baseline** genome (all
   1.0 / 0.0), `Render` of the analysis must match the source to better than
   **−20 dB RMS error**. This is the assertion that catches every scaling
   mistake in §3a.7 at once, and the original version never had it.
4. **Silenced partials.** With every `partialAmp = 0`, the render equals the
   residual alone within −40 dB.
5. **Determinism (4f).** Same seed + same generation count → bit-identical
   genome. Save → load → the genome replays identically.
6. **No aliasing.** Render at `pitchShift = +24` and assert energy above
   `0.45·sr` is below −60 dB relative to the peak — the Nyquist skip.

Then confirm the node is picked up by the two **generic** sweeps rather than
writing per-node versions: `AUDIOPARAMSWEEPTEST` (every `VisitParams` param
survives save/load and reaches the audio thread within one block) and
`AUDIOTEARDOWNSWEEPTEST` (spawn, wire into a running graph, delete
mid-playback, keep rendering — no crash, zero xruns).

**One teardown hazard specific to this node**: the worker thread may be
mid-render when the node is deleted. The destructor must set a
`std::atomic<bool> mAbort`, which the worker checks at the top of each
analysis frame and each render block, then `join()`. Do **not** detach the
thread and do not let the worker touch any node field after abort — a detached
worker writing into a freed `mPendingResult` is a use-after-free that
`AUDIOTEARDOWNSWEEPTEST` will find intermittently and that will be very
unpleasant to debug. `SampleScanner`'s destructor is the pattern.

---

## 7. Done when

1. `cmake --build build -j"$(sysctl -n hw.ncpu)"` compiles clean.
2. Spawned from the palette it shows 2 pins with labels `notes` / `record in`,
   and its body renders at 440 px with a non-empty readout strip.
3. Dropping a sample on it analyses in the background — **the frame-time
   graph in the menu bar shows no spike**, verify this explicitly, it is the
   whole point of §1b — and rolling produces an audibly different sample each
   time, playable from a `MIDI Notes` node into `Audio Out`.
4. Params survive save → load → undo → copy/paste → delete unchanged, and a
   reloaded patch reproduces the same rolled sound (4f).
5. Deleting it mid-playback and mid-analysis does not crash and logs zero
   xruns.
6. `INFINITE_MOLDERTEST` prints `OK`, all six assertions.
7. `/run-infinite-hygiene` passes.
8. `ARCHITECTURE.md`'s "Audio / note node system" section gains a line for it.

Report each of the eight explicitly.

## Out of scope — do not do these

- The XY effects pad and its ten live effects (§1a). Not a partial
  implementation either; none of it.
- Path recording / replay of a pad position. That's `MacroNodes`' job.
- Touching `ResynthNode` (the image node) in any way. Different node,
  different subsystem, similar name.
- Restoring the deleted `docs/plans/audio/` documents. Note their absence in
  the report if the skills' broken references get in your way, but don't
  reconstruct them as part of this task.
