# Aliasing-aware compilation for Field (research plan)

Status: **plan, waiting for owner sign-off.** Nothing is built. Read-only scoping done 2026-11-21.
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
