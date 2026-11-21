---
title: "Aliasing-aware compilation for Field"
slug: "aliasing-aware-field"
status: "killed"
owner: "namansoni"
date: "2026-11-21"
prior_art: "docs/prior-art/2026-11-21-auto-oversampling-and-zdf.md"
commit_sha: "b5da3a78db82b924f000db3d0af62c30228e37b5"
---

# Aliasing-aware compilation for Field

Status: **killed on H2, 2026-11-21.** Per-subgraph oversampling saved 17.4% of CPU against a
pre-registered bar of 25% (section 10); break-even needs a resampler pair under 0.09 interpreter
instructions (section 14). **H1 is confirmed within its scope, 2026-11-21** (section 14): on two held-out
sets the author did not tune against (L, 32 literature kernels; G, 240 generated kernels), H1b passes on
each and the analysis picks the exact factor on 174 of 174 informative kernels against 8 for "8x if
nonlinear" and 37 for a class lookup (H3). **Delay lines are in scope since phase 5, 2026-11-21** (section 16): the pass now treats `delay(x, N)`
as a periodic cell shifted by N samples, scores 94 of 97 delay kernels in L, G and a new blind set D (H4
passes), and changes no phase-4 result. The scope is a steady tone; 3 of 32 literature kernels are still
declined (no periodic steady state). Soundness lemmas (phase 4c) not started. Phase 0: section 8. Phase 2b (live audio path) not started. Threats to validity: section 12.
Reproduction: `ARTIFACT.md`. Prior art: `docs/prior-art/2026-11-21-auto-oversampling-and-zdf.md`.
Hypotheses were first committed in d38d29b7, before phase 0 ran (c0c39088).

## 1. The problem

Nonlinear audio code (products, powers, `abs`, `clamp`, `floor`, comparisons, phase shapes) creates
harmonics above Nyquist that fold back as inharmonic aliasing. Today every tool, Infinite included,
fixes this by hand: a person picks an oversampling factor per node (Drive, Saturator 8x; Ring Mod 4x).
That choice does not compose. Infinite's own rig shows it (`src/app/selftest/AliasTests.cpp:264-285`,
gates set about 5 dB under what each case measured on 2026-10-14):

| Case | Rig gate, alias below 20 kHz |
|---|---|
| Saturator, one stage at 50%, 8x + ADAA | -118 dB |
| Same, three stages in series in one 8x domain | **-41 dB** (measured -46 dB, per the rig's comment) |
| One stage inside the feedback routing | -69 dB |

Field makes it worse. Users write arbitrary nonlinear kernels, and the Field sample path has no
oversampling, ADAA or band-limited oscillator at all.

## 2. The research question

Can a compiler pass predict, from the program alone, how much each part of a kernel widens the
spectrum, and then pick the cheapest oversampling factor per subgraph that meets an aliasing
target? The prior-art scan found the ingredients scattered (the polynomial degree rule, Faust's
amplitude interval analysis, a 1998 CCRMA abstract; a graphics compiler that bandlimits shaders was
found only in a search listing and not read), but no system that combines them for audio. The scan was not
exhaustive.

## 3. The idea

An abstract interpretation over the sample program. Each SSA register carries three facts:

1. **Amplitude interval.** Needed because `tanh`-like behaviour, `pow` and `clamp` widen the
   spectrum depending on how hard they are driven.
2. **Bandwidth bound.** For a polynomial of degree d with input bandwidth B, the output is at most d·B.
   Composition multiplies degrees, which is why the three-stage chain fails at a fixed 8x.
3. **Decay class.** For non-polynomial ops, how fast the harmonic tail falls: analytic (exponential),
   a kink such as `abs` or `clamp` (about -12 dB/octave), or a jump such as `floor`, comparisons or
   `%` (about -6 dB/octave).

From these the pass estimates the energy that folds below 20 kHz at factor N (folded components
land at N·fs - f and survive decimation when they are under 20 kHz). It then picks the smallest N
in {1, 2, 4, 8} that meets the target, the same -80 dB the owner set for Analog.

A useful by-product: for jump-class ops, oversampling is close to hopeless (8x buys only about
18 dB at -6 dB/octave). The pass can say so and point to BLEP/ADAA instead of burning CPU.

## 4. Hypotheses, success bar, kill criteria

- **H1, the analysis predicts.** Across a corpus of at least 40 Field kernels, predicted alias
  ranks like measured alias (Spearman ρ ≥ 0.8), with a median absolute error ≤ 10 dB.
  **Kill:** ρ < 0.8. The bound is not informative and the project stops after phase 1.
- **H2, per-subgraph choice pays.** At equal measured alias, choosing the factor per subgraph costs at least
  25% less CPU than oversampling the whole kernel at the worst-case factor, and it beats a fixed
  8x on composed chains like the three-stage case. **Kill:** under 25% saving. That would mean whole-kernel
  oversampling with the predicted factor is enough. That is still a shippable feature, but not this
  research claim.

## 5. Phases

| Phase | What | Changes the app? | Ends with |
|---|---|---|---|
| 0. Measure | Add Field Effect and Field Synth cases to `AliasTests.cpp`, plus an offline oracle that renders a kernel at 16x/32x to get "true" alias. Build the kernel corpus. | Test code only | Baseline numbers per kernel |
| 1. Predict | The analysis pass over `SampleProgram` after `CompileBlock` (called at `src/core/field/BackendRegister.cpp:1415`), printing predictions, with no runtime change. Test H1. | Compiler, report only | Go or kill on H1 |
| 2. Execute | Rate regions in `RunSampleProgram` (`SampleRuntime.h:161`) using `MinPhaseOversampler`: per-voice oversampler state, scaled `sr`, and the latency reported. Test H2. | Audio path | Go or kill on H2; gated Field cases in the ratchet |
| 3. Loops (stretch) | Bandwidth inside `state`/`delay` feedback (a fixed point with widening, using loop gain), then the choice between ADAA and oversampling. This opens problem 2, zero-delay loops. | Compiler | Second paper or a documented limit |
| 4. Held-out and foundations | Independent held-out sets, simple-rule baselines, the H2 break-even cost, and soundness lemmas for the bound (section 13). Pre-registered 2026-11-21. | Test code and corpus only | **4a, 4b done (section 14):** H1b passes on L and G, H3 passes, break-even below 0.09 instructions. 4c (lemmas), sets P and A not run |
| 5. Delay lines | `delay(x, N)` as a periodic cell with lag N; blind set D (section 15). Pre-registered 2026-11-21. | Analysis pass only | **Done (section 16):** H4 passes, 94 of 97 delay kernels scored, no regression |

Phases 0 and 1 are where the research claim is won or lost; they are small and safe.

## 6. Known risks

- **Interpreter cost.** The VM is one `switch` per sample per voice. Running a region k times multiplies
  that, so H2 may need a cheaper interpreter for oversampled regions.
- **Feedback through a region.** A `state` or `delay` loop that crosses an oversampled region picks up
  the oversampler's 5 to 6 samples of latency, which changes the sound. Loops must stay entirely
  inside or entirely outside a region.
- **Modulation.** The rig's metric misreads modulation sidebands as aliasing (its own header says so),
  so the corpus needs static drive for H1 and a separate modulated set.
- **Missing operations.** `tanh`, `sign`, `fract` and `atan` are not available at sample rate today
  (`unknown function`). Adding them would widen the corpus. That is separate from this plan.

## 7. Output

- A DAFx paper (submissions usually open in spring), from phases 0 to 2.
- A user feature in Field: kernels that alias less without anyone choosing a factor, plus a
  compile-time note when a kernel will alias and why.
- Clean-room: all ideas come from papers and discussions. No other project's source code was opened.

## 8. Phase 0 results (2026-11-21)

Run: `INFINITE_FIELDALIASTEST=1 INFINITE_FIELDALIAS_CSV=<file> Infinite` (about 20 s, dev build).
Corpus: 47 kernels in `src/app/selftest/AliasTests.cpp` (`FieldAliasCorpus`), each rendered by the
real Field Effect node at F x 48 kHz, F in {1, 2, 4, 8, 16, 32}, decimated with the min-phase chain,
measured with the rig's metric (5 kHz tone at -6 dBFS; oscillators at 1.75 kHz). Raw numbers:
`data/phase0-baseline.csv`. Controls (linear gain, one-pole, sine oscillator) read clean.

**1. Smoothness order predicts the slope almost exactly.** Alias falls by about 6·(k+1) dB per
doubling of F, where k is how many derivatives the nonlinearity keeps continuous:

| Class | Example | Measured dB per doubling | Law |
|---|---|---|---|
| jump (k = -1) | floor quantise, comparator, `%` wrap, naive saw/square | 4.9 to 6.1 | 6 |
| kink (k = 0) | `abs`, `max`, hard clip, naive triangle, wrapped phase into `sin` | 11.8 to 12.9 | 12 |
| C1 (k = 1) | `x / (1 + abs(x))` | 17.4 to 17.9 | 18 |
| analytic | tanh, sin folder | a cliff: falls to the floor once F clears a drive-dependent bandwidth | - |

So the "decay class" fact in section 3 is not a guess any more: it can be read off the program's
operations, and it sets the slope. The level at 1x depends on drive, which is what the amplitude
interval is for.

**2. The degree rule is right about where aliasing starts and pessimistic about how much.** At a 5 kHz
tone, degree 5 reaches 25 kHz, which folds above 20 kHz, and it measures clean. Degree 7 folds to 13 kHz
and reads -71 dB. A chain of three cubics (degree 27) is clean at 2x, though the degree bound asks for
4x, because the high-order terms carry little energy. Phase 1 needs magnitudes, not only degree.

**3. Composition is the real problem, as in the Saturator.** A tanh at drive 4 needs 4x for -80 dB.
Three such stages in series need 32x (-46 dB at 8x).

**4. Oversampling cannot fix jumps.** All 4 jump kernels and the naive saw and square stay above -80 dB
even at 32x (best -50 dB). 7 of 44 nonlinear kernels never reach the target. For these the compiler
should not oversample. It should report the cause and point to BLEP/ADAA (phase 3).

**5. The factor each kernel needs for -80 dB varies widely:** 1x: 8, 2x: 9, 4x: 3, 8x: 6, 16x: 6,
32x: 5, never: 7. A single global factor would waste CPU on most kernels or fail the rest. This is the
premise of H2.

**6. The shipping presets alias audibly today.** At 1x: Tube Overdrive -35 dB, Soft Saturation
-35, Analog Tape Saturator -37, Wavefolder -22. Tube, Soft and Tape need 8x for -80 dB, Wavefolder 32x.
(Corrected in phase 1: the first run measured Soft Saturation, Tape and Moog with knob values left over
from the node's start-up preset, see section 9. The CSV now holds the corrected run.)

**Caveats found on the way**
- Float32 phase accumulators have a floor of about -101 dB (the sine-oscillator control), and pinch
  flattens at -90 dB. Targets below about -95 dB are limited by precision, not aliasing.
- Moog 4-Pole Ladder reads -29 dB at both 1x and 2x, then jumps to -67 dB at 4x. That looks like
  resonance or a limit cycle at an inharmonic frequency, which the metric counts as aliasing. Treat
  it as suspect until it has been checked (phase 1's analysis also calls it unstable, section 9).
- The corpus is hand-built by one person (me). For H1, phase 1 must also predict kernels written by
  other people (more presets, community devices) to avoid fitting the analysis to its own test set.

## 9. Phase 1 results (2026-11-21)

**H1 holds on the calibration corpus** (not confirmed: see section 11). Over 43 predicted nonlinear kernels x 6 factors (258 points, both sides floored at
-100 dB): Spearman rho **0.994** (bar 0.8), median absolute error **0.0 dB** (bar 10), p90 0.1 dB.
The factor each kernel needs for -80 dB is predicted exactly for 43 of 43. One kernel (Moog) gets no
prediction, by design (below). Raw numbers: `data/phase1-predictions.csv`. Same command as phase 0;
predictions print under each measured row.

| Class | n | median abs error | p90 |
|---|---|---|---|
| poly, analytic, kink, chain, feedback, preset | 204 | 0.0 dB | 0.0 dB |
| jump | 24 | 0.1 dB | 0.3 dB |
| osc | 30 | 0.1 dB | 5.7 dB |

**What was built, and how it differs from section 3.** `src/core/field/AliasAnalysis.{h,cpp}`,
report only, called from the rig. It does not implement the interval/degree/decay-class algebra of
section 3. Instead it uses the exact domain that algebra approximates: a kernel driven by one tone
produces a periodic signal, so the pass computes that periodic steady state on a grid of one input
period (8192 points), using the real interpreter. From the grid spectrum it folds every harmonic
analytically, for every factor, in closed form. In more detail:
- State cells are solved as a periodic fixed point. The one-sample delay is applied as an exact
  phase shift per harmonic, and a Newton step is preconditioned with the mean Jacobian. Linear
  filters converge in 1 to 2 iterations and nonlinear feedback in 5 to 15.
- Phase accumulators are recognised: the self-Jacobian is 1, and a ramp locked to f0 is checked to
  be a fixed point.
- Above the grid's usable band, a power law fitted to the top two octave bands extrapolates the
  tail. This is what makes jumps at 32x come out within 0.6 dB.
- The pass declines kernels whose output is not a function of the input phase: delay lines,
  tables, `n`, rand, beat clock, declared inputs. It also declines feedback with no stable periodic
  solution.

**Read the result honestly.**
- For memoryless kernels driven by the rig's own tone, the prediction is exact by construction:
  sampling at fs samples the ideal periodic signal. The bar was set for an approximate analysis,
  and it is cleared trivially here. The informative parts are these:
  - feedback (3 kernels, 0.0 dB error);
  - linear state around a nonlinearity (Tube, Tape: 0.0 to 0.5 dB);
  - accumulator oscillators;
  - the extrapolated tail for jumps (0.1 to 0.6 dB at 32x).
- The osc p90 of 5.7 dB is the measurement floor, not the model. Float32 phase accumulators put a
  floor of about -100 dB on the rig (pinch flattens at -90 dB), so where the prediction goes below
  that floor, the measurement cannot follow.
- Moog 4-Pole Ladder: declined as "periodic solution unstable (loop radius 1.004)". The rig shows
  -29 dB at both 1x and 2x, which aliasing alone does not do. Both point to a limit cycle at the
  preset's defaults. This has not been confirmed by rendering with no input.
- The pass knows the stimulus: one sine, its amplitude, its frequency. A compiler does not. This is
  the gap that matters for phase 2. The factor choice must hold over an input amplitude range and
  a frequency range, so the pass has to be run at the worst corners, or bounded by the section-3
  algebra using this pass as its oracle. H1 as written does not test this.
- The corpus is still hand-built. Held-out kernels written by other people have not been run yet.

**Cost.** Analysis used 4.6 M program runs (6.7 s in a Debug build) for all 47 kernels at 6 rates.
Rendering and measuring the same thing took 91 M runs (16.7 s). A memoryless kernel costs 8192 runs
per rate. Memoryless kernels without `sr` need only one rate (not yet exploited). That is about 1/120
of one 32x render of the rig's length, the compile-time-measurement baseline. Stateful kernels cost
1 to 15 times more.

**A bug the analysis found in the rig, and probably in the app.** Soft Saturation and Tape were
predicted about 2 dB cleaner than measured. A literal `drive = 2.5` matched exactly, and a declared
`param float drive = 2.5` did not. A probe showed the kernel received 3.0. The cause:
- `FieldSampleNode` is born on preset 0 (Tube Overdrive, `drive = 3.0`).
- `Apply()` keeps a param's value across recompiles by name (`ParamTable::Reconcile`).
- So any later kernel that declares `drive` inherits 3.0.

The rig now resets each declared param to its default after `Apply()`. Phase-0 numbers for Soft
Saturation, Tape and Moog were corrected, and every other row reproduced to the hundredth of a dB.
The app's preset picker (`LoadPreset`: code, then `Apply()`) goes through the same path. So picking
a Field preset probably keeps any same-named knob values from the previous kernel instead of
loading the preset's own values. Not fixed here; it needs a separate check.

**Decision: go to phase 2**, with two changes to its plan:
- run the pass at the corners of an input amplitude and frequency range rather than at one tone;
- use the per-register periodic signals the pass already computes to attribute aliasing to
  subgraphs, which is the input H2 needs.

Before phase 2 claims anything, run held-out kernels (community devices, Field Synth presets).

## 10. Phase 2a results (2026-11-21)

**H2 fails. The kill criterion applies.** Choosing the oversampling factor per segment saves
**17.4%** of CPU in aggregate over 28 kernels (bar: 25%), median 19.5% per kernel. On the
three-stage tanh chain the staged plan does **not** beat a fixed 8x. Per section 4, this means
whole-kernel oversampling at the factor phase 1 predicts is the shippable result, and the
per-subgraph claim is not made.

**What was built (phase 2a: offline, no audio-path change).**
- `src/core/field/RatePlan.{h,cpp}`: splits a compiled `SampleProgram` into contiguous segments
  at cuts next to nonlinear ops (at most 3 cuts, at most 2 values crossing, no state cell across a
  cut). Each segment runs at 1, 2, 4, 8, 16 or 32x. Values that cross a cut go down to the base rate
  and up again through `MinPhaseOversampler` (16x and 32x as a cascade through 8x). Block constants
  are recomputed in each segment; `in` is read once and crosses like any value.
- The chooser scores a plan by the alias of each segment, analysed with the phase-1 pass on its
  band-limited input waveform. Each crossing's alias is referred to the output by the mean-square
  sensitivity of the output to that value (central difference through the analysis), and the
  segments are summed as powers. It takes the cheapest plan predicted at -80 dB or below.
- `AliasAnalysis` gained input/capture registers, `SpectrumFromWave` and `BandLimitWave`.
- The rig (`INFINITE_FIELDRATEPLAN=1`, `src/app/selftest/AliasTests.cpp`) runs every plan through
  real resamplers, measures it, and counts its cost only if the **measured** alias meets -80 dB;
  otherwise the kernel is charged the whole-kernel cost. This keeps a mispredicted plan from
  earning a saving.
- Cost model, in ns per base sample, from `tools/field_rate_cost_bench.cpp` built at -O2 (best of
  5): 3.5 per instruction; up 2/4/8x 11/23/47, down 10/23/49; 16x and 32x derived from the cascade.
  The Debug build's own timings over-charge resamplers about 10x against the interpreter's 2x, so
  they are not used for the verdict (run history below).

**Results (`data/phase2-h2-run.log`).** 44 kernels; 16 skipped (clean at 1x, or no factor within
32x reaches -80 dB). Savings by class:

| Class | kernels | typical plan | saving |
|---|---|---|---|
| analytic (tanh, sin fold) | 6 | 1x stage, 2–4x around the nonlinearity, 1x tail | 21–47% |
| kink (rational, abs, max, hard clip) | 7 | same shape at 8–32x | 7–37% |
| poly (d7) | 1 | 1x / 2x / 1x | 9% |
| osc | 3 | the oscillator at full factor, 1x tail | 9–18% |
| presets | 4 | Soft Saturation 32%, Wavefolder 47%, Tube and Tape 0% (no legal cut) | 0–47% |
| feedback | 3 | no cut outside the loop | 0% |
| chain (poly d9, d27, clip then cubic, tanh x3) | 4 | 1x / 2x / 1x; tanh x3 below | 24, 19, 16, 0% |

Savings exist wherever a cheap linear stage surrounds one nonlinearity: the linear stage runs at
1x. But a resampler pair costs as much as 6 to 14 instructions at the factor in question, and most
Field kernels are short. The fixed cost of crossing eats most of what staging saves.

**The tanh x3 chain, and why the planner was wrong about it.** The whole kernel needs 32x (cost
4272). The planner chose 2x | 4x | 8x | 1x (cost 597), predicted -80.5 dB; it measures -43.0 dB, so
it is charged the whole cost. A fixed 8x measures -46.1 dB at cost 1048. Diagnosis
(`INFINITE_FIELDRATE_DUMP`, `data/phase2-tanh3-dump.log`):
- The cuts sit just before each stage's `exp`. The value crossing them is 8·tanh(4x): close to a
  square wave of amplitude 8, with large harmonics right up to the base Nyquist.
- Rendering the same plan with **ideal** resamplers (FFT brickwall at the base Nyquist) measures
  -74.0 dB. So about 31 of the 37 dB gap comes from the real half-band chain. Its transition band
  and images pass energy around 20–28 kHz that the analysis assumed removed. The next `exp` folds it.
  The remaining 6.5 dB is the analysis itself.
- On the 27 other planned kernels, staged measurement matched prediction to within a few dB,
  because their crossing values are not wideband at high level.
- Fix, not done: model each crossing with the resampler's measured response instead of an ideal
  low-pass. On a harmonic grid this is exact, because the images of harmonics of f0 are harmonics of
  f0 too. It would make the chain plans honest. It would raise their factors and so would not
  raise the saving.

**Staging changes the sound of chains.** Band-limiting between stages removes ultrasonic
content that a later stage would have folded back in-band *harmonically*. Against the node's 32x
render, the staged poly d9 / d27 chains deviate by -46 / -45 dB in in-band harmonic amplitude, and
tanh x3 by -33 dB. Whole-kernel oversampling at any factor deviates by under -140 dB. Neither is
"wrong" (the 32x render is itself one choice of what the chain means), but staging is not
transparent, and a user would hear the difference on chains.

**The planner must hold over an input range (the phase-1 decision).** At input amplitude
0.25/0.5/1.0 × f0 0.5/1/2/5 kHz, the needed factor changes somewhere for 31 of 38 kernels, and the
worst corner needs more than the nominal tone for 29. A compiler that picks a factor from one tone
under-oversamples most kernels. Any shipped version must plan at the worst corner of a declared
input range.

**Held-out kernels (the phase-1 decision).** 18 of the app's Field Effect presets that the
corpus did not include: 2 accepted (Sidechain Ducker, Noise Gate), factor predicted exactly, worst
error 0.1 dB. 16 declined: 7 read state tables, and the rest have no periodic steady state under a
steady tone (modulated or time-varying effects, Bitcrusher's sample-and-hold). Dynamic Auto-Wah
just misses: residual 2.1e-6. Declining is correct for these, but it means the pass covers few real
effects: held-out coverage is 2 of 18. Field Synth presets were not run (no `in`).

**Run history, reported in full.**
1. Debug-build cost model, alias scored relative to each value's own fundamental: saving 0.8%
   (`data/phase2-run1-debugcost-oldscoring.{log,csv}`). Plans with a nonlinearity at 1x were
   badly mispredicted (abs mix predicted -151 dB, measured -33): abs(in) has no fundamental, so a
   relative score is meaningless.
2. Output-referred, sensitivity-weighted absolute scoring; factors extended to 32x; -O2 cost
   model: 17.4% (the verdict run).
3. Plus a sign-domain check (reject a cut where band-limiting makes a one-signed value cross zero):
   no plan changed, same 17.4%. It stays in as a guard.
Each change was made because a measured plan disagreed with its prediction, never to move the
saving. The bar was fixed before run 1.

**Cost-model spread (added after the verdict).** The verdict used one run of the cost bench. The
bench was then run 10 more times, 15 s apart (`data/phase2-cost-bench.log`), and the H2 rig re-run
under each measured line (`scripts/cost_repeats.py`, `data/phase2-cost-repeats.csv`). Aggregate
saving: mean 15.8%, 95% CI 15.6 to 15.9 (Student t, n = 10), range 15.3 to 15.9%; the staged tanh x3
plan beat fixed 8x in 0 of 10 runs. The repeated lines measured a cheaper instruction
(about 2.7 ns, against 3.5 in the verdict line) and cheaper 2x crossings, which moves the saving
down, not up. The verdict line is kept as the pre-registered result; the CI is the honest spread.
Every value is well under the 25% bar, so the kill stands either way.

**Listening clips.** One steady-state period of each tanh x3 render (1x, fixed 8x, staged, 32x)
is in `data/phase2-audio/` as float32 (`INFINITE_FIELDRATE_AUDIO=<dir>`), with 2 s WAVs made by
`scripts/make_audio.py` (-18 dBFS peak, 10 ms fades). Measured alias: -12.6, -46.1, -43.0, -136.0 dB.

**Threats to validity.**
- *Internal.* Costs come from an isolated -O2 microbenchmark, not from the app's audio thread;
  the interpreter's real per-instruction cost depends on the op mix. A cheaper resampler would
  raise the saving; how far was not measured. The interpreter is a scalar register VM; a compiled backend would make instructions
  cheaper and crossings relatively dearer, which lowers the saving.
- *External.* The corpus is hand-built (plus 2 accepted held-out presets). Long real kernels, where
  one nonlinearity sits inside lots of linear code, would save more; the app's presets are short.
- *Construct.* Equal measured alias uses one tone at 5 kHz, 0.5 amplitude, and a -80 dB target.
  The corner sweep shows the factor depends on the stimulus.

**Provenance.** Machine: Apple M2, 8 GB, macOS 27.2.1. Build: `scripts/ibuild.py build --mode dev`
(Debug, arm64) in `worktrees/aliasing-aware-field`; parent commit 8820229c, results from the commit
that adds this section. Command:
`INFINITE_FIELDRATEPLAN=1 INFINITE_FIELDRATE_COST=0,3.5,11,23,47,10,23,49 ./build-dev/Infinite.app/Contents/MacOS/Infinite`.
Cost line: `clang++ -std=c++20 -O2 -Isrc -Isrc/core -o /tmp/rate_cost_bench tools/field_rate_cost_bench.cpp`.
The analysis and renders are deterministic (no RNG).

**Decision.** Kill the per-subgraph claim (H2). Phase 2b (rate regions on the live audio path)
is not started. What survives and is worth shipping, if the owner wants it: the phase-1 pass as a
compile-time **whole-kernel** factor suggestion for Field Effect, planned at the worst corner of a
declared input range, plus the preset-param bug fix found in phase 1. Phase 3 (loops) is
independent of H2 and stays open.

## 11. Held-out validation

The standard (docs/RESEARCH_STANDARD.md section 3) allows a hand-built corpus for calibration only.
Held-out kernels run so far: 18 Field Effect presets that the corpus did not include
(`data/phase2-run1-debugcost-oldscoring.log`, lines starting `FIELDRATE held`).
- 2 accepted (Sidechain Ducker, Noise Gate): factor predicted exactly, worst error 0.1 dB.
- 16 declined by design: 7 read state tables; the rest have no periodic steady state under a steady
  tone (modulated or time-varying effects, sample-and-hold).
- Not run: Field Synth presets (no `in`), community kernels.

So H1 is **not confirmed**: 2 held-out kernels cannot carry a claim about Field kernels in general.
H2 is killed on the calibration corpus already, so its held-out run would not change the verdict.

**Superseded by phase 4 (section 14):** 272 held-out kernels in two pre-registered sets, 233 scored.
H1 is now confirmed for kernels the pass accepts.

## 12. Threats to validity

- **Construct.** For memoryless kernels driven by the rig's own tone, the phase-1 prediction is exact
  by construction (sampling the ideal periodic signal), so H1's bar is cleared trivially for them.
  The informative evidence is feedback, linear state around a nonlinearity, accumulators and the
  extrapolated jump tail (section 9). Alias is measured with one tone (5 kHz)
  at one amplitude; the corner sweep (section 10) shows the needed factor moves with the stimulus.
  The rig's metric counts modulation sidebands and limit cycles as aliasing (Moog). Kernels with a float32 phase accumulator
  cannot measure below about -100 dB (the accumulator's rounding is itself inharmonic); memoryless
  and filter kernels measure down to about -138 dB.
- **Internal.** H2's CPU is a cost model (ns per base sample from an isolated -O2 microbenchmark),
  not measured audio-thread time; run 1 used Debug-build costs and is reported, not used for the
  verdict. One machine (Apple M2).
- **External.** One author wrote the calibration corpus; phase 4 adds 272 held-out kernels (section 14),
  but L was still written by the same person (from papers, without looking at the analysis or data), and
  G comes from a grammar the author wrote. Since phase 5 delay lines are in scope (section 16); D comes from a
  grammar the same author wrote, and 3 literature kernels (a Yeh pedal and two semi-implicit diode
  clippers) are still declined. Kernels are
  short; long kernels with one nonlinearity inside much linear code would favour staging more.

## 13. Phase 4 pre-registration: held-out sets and foundations (2026-11-21)

Written before any phase-4 kernel exists. Section 11 left H1 *not confirmed*: the corpus has one
author, and memoryless kernels are exact by construction (section 12). Phase 4 tests H1 on kernels the
author did not choose and asks whether the analysis beats trivial rules.

**Freeze.** The analysis (`src/core/field/AliasAnalysis.{h,cpp}` and `RatePlan.{h,cpp}`) is frozen at the commit that adds this
section (fd6c55ca). Each held-out set is committed under `corpus/heldout/<set>/` before it is scored, so the
history shows it was not tuned against. Changing the analysis after scoring needs a new dated entry
here and a rescore of every set.

**Sets.**
- **L, literature (primary, ≥ 30 kernels).** Nonlinearities and nonlinear filters taken from published
  papers and textbooks the author has opened (waveshapers, clippers, wavefolders, saturating ladders and
  similar), written as Field kernels with one citation each. Equations only; no source code.
- **G, generated (primary, ≥ 200 kernels).** Random kernels from Field's sample-rate grammar, seed
  20261121, by a generator committed before it is run. At least half must contain `state` or `delay`,
  so memoryless kernels cannot dominate.
- **P, permissive code (optional).** Nonlinearities from MIT/BSD/ISC/Apache/zlib-licensed projects,
  ported with the licence notice kept. No copyleft source.
- **A, blind LLM (supplementary).** Kernels written by a model from another vendor, given only the
  Field syntax and the task "musical distortion and synthesis kernels", never the analysis. Reported
  separately and never counted toward *confirmed*.

**Scoring.** The same rig, tone (5 kHz, amplitude 0.5) and -80 dB target as phases 0 to 2. A kernel the rig
declines (no periodic steady state, or reads state tables) is listed with its reason; coverage is a
reported number, never a silent drop. All bars apply to the *informative* in-scope kernels (not
memoryless), per set.

**Hypotheses.**
- **H1b, the prediction generalises.** On L and on G separately: Spearman ρ ≥ 0.8 between predicted and
  measured alias, and median absolute error ≤ 10 dB. **Kill:** either set misses either bar; H1 stays
  *not confirmed* and the paper says so. If fewer than half of L is in scope, the claim is scoped to
  the in-scope class by name.
- **H3, it beats simple rules.** The analysis picks the minimal factor that meets the target on ≥ 80%
  of informative kernels in L ∪ G, and strictly more often than either baseline: rule A, "8x if the
  kernel has any nonlinear op, else 1x"; rule B, a lookup from smoothness class to the class's median
  factor in phase 0. **Kill:** a baseline matches or beats it; then the contribution is the
  measurement rig, not the analysis.
- **Bound soundness (phase 4c).** For each class the lemmas cover, the prediction is claimed as an upper
  bound on alias, not only a ranking. Lemma targets: a polynomial of degree n multiplies bandwidth by
  at most n; a kink (abs, clip) gives harmonics falling as 1/k²; an analytic map such as tanh on a
  bounded input gives geometric decay with a stated rate. Each lemma is checked symbolically in
  `proofs/`, with its float32 caveats. Any held-out kernel in a covered class that measures more than
  3 dB above its predicted alias is a counterexample and is reported.

**Descriptive (not a hypothesis).** The H2 break-even: the resampler-pair cost, in interpreter
instructions, at which the aggregate saving of section 10 would reach 25%. This turns the kill into a
condition that holds on other machines, resamplers or a compiled backend.


## 14. Phase 4 results (2026-11-21)

Analysis frozen at fd6c55ca (unchanged). G and the scorer were committed in 001ad1f0 and L in c5a84260,
both before any scoring. One run of `INFINITE_FIELDHELDOUT` on a dev build of c5a84260, then
`scripts/score_heldout.py`. Raw: `data/phase4-heldout.{csv,log}`, `data/phase4-scored.csv`, `data/phase4-score.log`.

**Coverage.** 272 kernels, 233 scored, 39 declined.

| Set | Kernels | Scored | Declined: delay lines | Declined: no periodic steady state | Memoryless (reported only) | Informative |
|---|---|---|---|---|---|---|
| L, literature | 32 | 18 (56%) | 12 | 2 (l-14, l-15: semi-implicit diode clippers, loop radius 1.9) | 11 | 7 |
| G, generated | 240 | 215 (90%) | 25 | 0 | 48 | 167 |

L not scored: l-12, 13 (Yeh pedals), 14, 15 (diode clippers), 16 (pre-filter), 20 to 22 (Timoney
configurations), 26 to 31 (every ADAA kernel). More than half of L is in scope, so the pre-registered
scoping rule does not fire; the claim is still stated for kernels without delay lines, because that is
what was tested. The 7 informative L kernels are the state-only filters: Buchla 259 filtered, three
Huovilainen ladders, two Chamberlin SVFs, Parker's Moog-like ladder.

**H1b passes on both sets.**

| Set | n informative | Spearman ρ (bar 0.8) | median abs error (bar 10 dB) | Verdict |
|---|---|---|---|---|
| L | 7 | 1.000 | 0.0 dB | PASS |
| G | 167 | 0.994 | 0.0 dB | PASS |

Memoryless, reported only: L n=11, ρ 1.000, 0.0 dB; G n=48, ρ 0.961, 0.1 dB.

**H3 passes.** Exact minimal factor for −80 dB on L ∪ G informative (n=174): the analysis 174 (100%),
rule A ("8x if nonlinear") 8 (5%), rule B (class → phase-0 median factor; poly 1, analytic 2, kink 16,
jump none, feedback 2) 37 (21%). 68 of the 174 never reach −80 dB within 32x (runaway or limit-cycling
feedback in G), and both sides say "none". Without them: analysis 106 of 106, rule A 8, rule B 9.

**Large errors are all on kernels that never get clean.** 34 informative kernels have a per-kernel median
error over 10 dB; every one of them has measured factor "none". 29 of them predict an absurd +2840 to
+2992 dB (a diverging steady-state solve that was not declined), as do 12 memoryless G kernels. They rank correctly and pick the right
factor ("none"), but their dB values mean nothing, and the median hides them.

**Bound soundness: no claim.** 86 nonlinear kernels measure more than 3 dB above prediction at some
factor. In every case the measurement sits at the rig's float32 floor: none is above −125.9 dB, against
a floor of about −138 dB (section 12) and a target of −80 dB. No lemma was written (phase 4c not started),
so no class is covered and none of these counts as a counterexample by the pre-registered rule.

**Break-even (descriptive).** `scripts/break_even.py` scales the resampler costs by s
(`data/phase4-break-even.csv`). The aggregate saving reaches 25% only between s = 0 and s = 0.0156,
where a 2x pair costs 0.09 interpreter instructions; at s = 0 (free crossings) it is 25.1%. The three-stage
tanh chain never beats a fixed 8x at any s. H2's kill therefore holds on any plausible machine or
resampler: a crossing would have to cost less than about 1/10 of one interpreter instruction.

**What this does and does not show.**
- It shows the steady-state predictor generalises from the author's corpus to 233 kernels the author
  did not tune against, and that it does far better than either simple rule at choosing a factor.
- It does not show the analysis is cheap or approximate: it runs the real interpreter on one period
  (section 9), so it is closer to an exact solver for a single steady tone than to the interval algebra
  of section 3.
- The stimulus is one tone at one amplitude; the corner sweep (section 10) showed the needed factor moves
  with the stimulus.
- Delay lines (and so ADAA, which needs the previous input) are declined. A delay of N samples is a phase
  shift of a periodic signal, so this looks fixable, but the analysis is frozen; changing it needs a new
  dated entry and a rescore (section 13).
- Not run: sets P (permissive code) and A (blind LLM), and the phase-4c lemmas.

## 15. Phase 5 pre-registration: delay-line coverage (2026-11-21)

Written after phase 4 was scored and before the analysis changes. Signed off by the owner on 2026-11-21.
This is the dated entry section 13 requires for changing the frozen analysis.

**Problem.** The pass declines every kernel with `delay(x, N)` (section 14): 37 of 272 held-out kernels,
including every ADAA kernel in L. Their alias was never measured, because the hook only renders kernels the
pass accepts, so their results are still unseen.

**Change (scope).** Only `src/core/field/AliasAnalysis.cpp`. A delay line becomes one more periodic cell
whose lag is N samples instead of state's 1: its value on the grid is its input's periodic signal shifted by
N·φ, the same exact band-limited shift state already uses, and the Newton step uses a per-cell shift
e^{-i h N φ}. Programs without delay lines run the existing code path unchanged. Tables, `n`, `rand`,
the beat clock and declared inputs stay declined. `RatePlan.*` is not changed; H2 is not rescored, because
its corpus has no delay lines.

**New set D (blind).** 60 kernels from `scripts/gen_heldout_d.py`, seed 20261122, every one with a delay
line: feed-forward combs, a delayed input inside a nonlinearity, feedback closed through `delay(y, N)`,
the first-order ADAA shape, and two delays around a nonlinearity with a one-pole. G's grammar and
guards; lengths 1 to 480. Committed with this section, before the analysis change and before scoring.

**Baseline (fixed, phase 4 at d3f1e2bc).** Delay coverage 0 of 37; every number in section 14.

**H4, delay lines are predicted as well as state.**
- Coverage: at least 80% of the delay kernels in L, G and D are scored. **Kill** under 50%.
- On the informative delay kernels of L, G and D separately: Spearman ρ ≥ 0.8 and median absolute error
  ≤ 10 dB (H1b's bars).
- On the pooled delay kernels: the exact minimal factor on ≥ 80%, and strictly more often than rule A and
  rule B (H3's rule).
- **No regression:** every kernel scored in phase 4 and every calibration kernel gets the same predicted dB
  (±0.01) and factor.

**Scoring.** Same rig, tone and target. `scripts/score_heldout.py` (extended in this commit) prints H4;
H1b and H3 for L and G are reprinted on the same numbers. Changing the analysis again after scoring needs
another dated entry here and a rescore.

## 16. Phase 5 results: delay lines (2026-11-21)

The change pre-registered in section 15, in `src/core/field/AliasAnalysis.cpp` only. Each `delay(x, N)`
is lowered to a store into a new periodic cell and a load from it, with lag N; the cell's value on the
grid is its input shifted by N·φ, and Newton uses the shift e^{-i h N φ} per cell. Only lag-1 cells can
be phase accumulators. Programs without delays take the old path. D was committed in 35adec8b before the
change. One run of `INFINITE_FIELDHELDOUT` over L, G and D on a dev build, then
`scripts/score_heldout.py`. Raw: `data/phase5-heldout.{csv,log}`, `data/phase5-scored.csv`, `data/phase5-score.log`.

**H4 passes.**

| Delay kernels | Total | Scored | Informative n | Spearman ρ (bar 0.8) | median abs error (bar 10 dB) | Verdict |
|---|---|---|---|---|---|---|
| L | 12 | 11 | 11 | 0.937 | 0.0 dB | PASS |
| G | 25 | 25 | 25 | 0.991 | 0.0 dB | PASS |
| D | 60 | 58 | 58 | 0.988 | 0.0 dB | PASS |
| Pooled | 97 | 94 (97%; bar 80%, kill < 50%) | 94 | | | PASS |

Exact minimal factor on the pooled delay kernels: the analysis 93 of 94 (99%), rule A 9, rule B 38. PASS.

**No regression.** All 233 kernels scored in phase 4 give the same predicted dB (±0.01) and factor; the
calibration corpus prediction file is byte-identical to `data/phase1-predictions.csv`.

**Reprinted on the same run** (L and G, all kernels): L 29 of 32 scored, informative n=18, ρ 0.963;
G 240 of 240, n=192, ρ 0.994; both median 0.0 dB, PASS. H3 on L ∪ G informative (n=210): analysis 209
(100%), rule A 13, rule B 44, PASS. Every ADAA kernel in L (l-26 to l-31) is now scored.

**Not scored (5).** All "no periodic steady state": l-12 (Yeh distortion pedal, loop radius 1.09), l-14
and l-15 (semi-implicit diode clippers, 1.93 and 1.84; already declined in phase 4), d-038 and d-054.
The three L kernels are implicit circuits solved with a one-step update; this is the zero-delay-loop
problem, not a delay-line one.

**The one factor miss is conservative.** l-27 (Parker's first-order ADAA hard clip) measures factor 4;
the analysis predicts it never reaches −80 dB, plateauing near −72 dB from 4x up while the measurement
falls to −117 dB. It would oversample too much or warn wrongly, never too little. Not investigated:
the analysis is frozen.

**Blow-ups still predict absurd dB.** 18 delay kernels (12 in D, 6 in G) have outputs that grow
without bound: measured "alias" reaches +25 to +143 dB and the prediction +800 to +2992 dB. In every one
both sides say factor "none", so the decision is right and the dB values mean nothing; in all, 63 of 327
scored kernels are like this (45 in phase 4). They count as scored under the pre-registered rule. Without
them delay coverage would be 76 of 97 (78%), just under the 80% bar; a pass that declined unbounded
outputs would be more honest than one that prints +2992 dB.

**Bound soundness: still no claim.** 108 kernels measure more than 3 dB above prediction at some factor.
105 sit at the rig's float32 floor (worst −121.1 dB). Three are above it, all with delays and all below the
−80 dB target, so no factor changes: d-053 (+6.9 dB at −103.8 dB), g-137 (+9.2 dB at −123.0 dB) and
l-29, Smith's cubic ADAA (+5.3 dB at −88.2 dB, 4x). No lemma covers these classes (phase 4c not started).

**What this shows.** The same steady-state solve covers delay lines and ADAA, on 94 kernels including
a blind set of 60, with no change to anything scored before. It does not show behaviour under real
(non-sinusoidal) input, it does not cover implicit (zero-delay) circuits, and its handling of unbounded
outputs is a decision without a number.

