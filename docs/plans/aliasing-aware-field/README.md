# Aliasing-aware compilation for Field (research plan)

Status: **phase 0 done 2026-11-21** (results in section 8). Phase 1 not started.
Prior art: `docs/prior-art/2026-11-21-auto-oversampling-and-zdf.md`.

## 1. The problem

Nonlinear audio code (products, powers, `abs`, `clamp`, `floor`, comparisons, phase shapes) creates
harmonics above Nyquist that fold back as inharmonic aliasing. Today every tool, Infinite included,
fixes this by hand: a person picks an oversampling factor per node (Drive, Saturator 8x; Ring Mod 4x).
That choice does not compose. Infinite's own rig shows it (`src/app/selftest/AliasTests.cpp:255-275`):

| Case | Alias below 20 kHz |
|---|---|
| Saturator, one stage at 50%, 8x + ADAA | about -118 dB |
| Same, three stages in series in one 8x domain | **-41 dB** |
| One stage inside the feedback routing | -69 dB |

Field makes it worse. Users write arbitrary nonlinear kernels, and the Field sample path has no
oversampling, ADAA or band-limited oscillator at all.

## 2. The research question

Can a compiler pass predict, from the program alone, how much each part of a kernel widens the
spectrum, and then pick the cheapest oversampling factor per subgraph that meets an aliasing
target? The prior-art scan found the ingredients scattered (the polynomial degree rule, Faust's
amplitude interval analysis, a 1998 CCRMA abstract, a graphics compiler that bandlimits shaders),
but no system that combines them for audio. The scan was not exhaustive.

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
| 1. Predict | The analysis pass over `SampleProgram` after `CompileBlock` (`src/core/field/BackendRegister.cpp:1400`), printing predictions, with no runtime change. Test H1. | Compiler, report only | Go or kill on H1 |
| 2. Execute | Rate regions in `RunSampleProgram` (`SampleRuntime.h:161`) using `MinPhaseOversampler`: per-voice oversampler state, scaled `sr`, and the latency reported. Test H2. | Audio path | Go or kill on H2; gated Field cases in the ratchet |
| 3. Loops (stretch) | Bandwidth inside `state`/`delay` feedback (a fixed point with widening, using loop gain), then the choice between ADAA and oversampling. This opens problem 2, zero-delay loops. | Compiler | Second paper or a documented limit |

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
- Clean-room: all ideas come from papers and discussions. No copyleft source was opened.

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
-33, Analog Tape Saturator -35, Wavefolder -22. Tube, Soft and Tape need 8x for -80 dB, Wavefolder 32x.

**Caveats found on the way**
- Float32 phase accumulators have a floor of about -101 dB (the sine-oscillator control), and pinch
  flattens at -90 dB. Targets below about -95 dB are limited by precision, not aliasing.
- Moog 4-Pole Ladder reads -22 dB at both 1x and 2x, then jumps to -60 dB at 4x. That looks like
  resonance or a limit cycle at an inharmonic frequency, which the metric counts as aliasing. Treat
  it as suspect until it has been checked.
- The corpus is hand-built by one person (me). For H1, phase 1 must also predict kernels written by
  other people (more presets, community devices) to avoid fitting the analysis to its own test set.
