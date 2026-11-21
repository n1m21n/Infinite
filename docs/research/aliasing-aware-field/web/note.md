---
description: "Can a compiler pick the oversampling factor for nonlinear audio code? A pre-registered study in Infinite's Field language: the alias prediction works on our test corpus, and per-subgraph oversampling missed its bar."
kicker: "Lab note · negative result"
seo_title: "Aliasing-aware compilation for Field | Infinite"
keywords: "aliasing, oversampling, DSP compiler, nonlinear audio, anti-aliasing, Field, negative result"
---
<div class="rn-verdict">
<p class="mono rn-verdict-h">result</p>
<p><strong>Per-subgraph oversampling: killed.</strong> It saved 17.4% of CPU against a pre-registered bar of 25%, and on a chain of three saturators it did not beat a fixed 8x.</p>
<p><strong>Predicting alias from the program: holds on our own corpus, not confirmed.</strong> Rank correlation 0.994 and the needed factor exact for 43 of 43 kernels, but only 2 of 18 held-out presets could be analysed.</p>
</div>

## The problem

Nonlinear audio code (a `tanh`, a `clamp`, an `abs`, a product of two signals) makes harmonics above the Nyquist frequency. They fold back into the audible band as inharmonic aliasing. The usual fix is oversampling: run the code at a multiple of the sample rate, then filter and decimate. Today a person picks that multiple by hand, per node. Infinite's Drive and Saturator run at 8x.

A hand-picked factor does not compose. In our own test rig, one Saturator stage at 8x (with antiderivative anti-aliasing) stays below -118 dB of alias. Three of the same stages in series, in one 8x domain, measure -46 dB. Field makes this worse: Field kernels are arbitrary code written by users, and Field's sample path has no oversampling at all.

So the question was: **can a compiler read a kernel, predict how much it aliases at each factor, and pick the cheapest factor per part of the kernel that meets a target?** The target was -80 dB below 20 kHz, the same one Infinite's Analog node uses.

## What we committed to before running anything

Two hypotheses, each with a number that would kill it, were committed to the repository before the first measurement:

- **H1, the analysis predicts.** Across at least 40 kernels, predicted alias ranks like measured alias (Spearman ρ ≥ 0.8), median absolute error ≤ 10 dB. Kill: ρ < 0.8.
- **H2, per-subgraph choice pays.** At equal measured alias, choosing the factor per subgraph costs at least 25% less CPU than oversampling the whole kernel at the factor it needs, and beats a fixed 8x on composed chains. Kill: under 25%.

## Phase 0: measuring 47 kernels

We built a corpus of 47 kernels: polynomials, `tanh` and sine folders at several drives, hard clips, quantisers, naive oscillators, feedback loops, and six of Infinite's shipping presets. Each one ran through the real Field Effect node at 1x to 32x of 48 kHz, was decimated with Infinite's minimum-phase filter chain, and was measured with a 5 kHz tone.

<div data-fig="slopes"></div>

The clearest finding: **how smooth the nonlinearity is sets the slope.** Alias falls by about 6·(k+1) dB per doubling of the rate, where k is how many derivatives stay continuous. Jumps (`floor`, comparisons, `%`) fall 5 to 6 dB per doubling. Kinks (`abs`, `max`, hard clip) fall 12 to 13. A C1 function like x/(1+|x|) falls 17 to 18. Analytic functions such as `tanh` fall off a cliff once the rate clears a bandwidth that depends on drive.

Three things follow:

- **Oversampling cannot fix jumps.** No jump kernel, and neither the naive saw nor the naive square, reached -80 dB even at 32x. The best was -50 dB. For these a compiler should not oversample. It should say why and point to band-limited steps or antiderivative anti-aliasing instead.
- **The needed factor varies widely.** For -80 dB: 1x for 8 kernels, 2x for 9, 4x for 3, 8x for 6, 16x for 6, 32x for 5, and never for 7. One global factor wastes CPU on most kernels or fails the rest.
- **Shipping presets alias audibly at 1x.** Tube Overdrive measured -35 dB, Wavefolder -22 dB.

## Phase 1: predicting alias without rendering

The analysis runs over the compiled program. A kernel driven by one steady tone produces a periodic signal, so the pass computes that periodic steady state on one input period (8192 points) with the real interpreter. It then folds every harmonic analytically for every factor. Feedback through state cells is solved as a periodic fixed point. Above the grid's band, a power law fitted to the top two octaves extends the tail, which is what gets jump kernels right at 32x. The pass declines kernels whose output is not a function of the input phase: delay lines, tables, random numbers, beat-clock reads, and feedback with no stable periodic solution.

<div data-fig="explorer"></div>

Over 43 predicted kernels at 6 factors: Spearman ρ **0.994** (bar 0.8), median absolute error **0.0 dB** (bar 10), and the factor each kernel needs predicted exactly for **43 of 43**.

<details class="rn-drawer"><summary>Why the H1 number is weaker than it looks</summary>
<p>For a kernel with no memory, driven by the rig's own tone, the prediction is exact by construction: sampling at the base rate samples the ideal periodic signal. The bar was set for an approximate analysis and is cleared trivially for those kernels. The informative cases are feedback (3 kernels, 0.0 dB error), linear filters around a nonlinearity (0.0 to 0.5 dB), phase-accumulator oscillators, and the extrapolated tail for jumps (0.1 to 0.6 dB at 32x).</p>
<p>The pass also knows the stimulus exactly. A compiler does not. When we swept input amplitude (0.25, 0.5, 1.0) and frequency (0.5 to 5 kHz), the needed factor changed for 31 of 38 kernels. A shipped version would have to plan at the worst corner of a declared input range.</p>
<p>And the corpus was written by one person. On 18 shipping Field Effect presets that were not in it, the pass accepted 2 (factor exact, worst error 0.1 dB) and declined 16: 7 read state tables, the rest have no periodic steady state under a steady tone. Two of eighteen cannot carry a general claim, so H1 stays unconfirmed.</p>
</details>

The analysis also found a bug. Two presets came out 2 dB cleaner than measured. A probe showed their `drive` knob held 3.0, not its declared 2.5: when Field recompiles, a parameter keeps its value by name, so a kernel inherits any same-named knob from the one before. Picking a preset in the app goes through the same path. The corrected numbers are in the data. The app fix is still to be released.

## Phase 2: per-segment oversampling, and why it failed

The planner cuts a compiled kernel into up to four segments next to its nonlinear operations. Each segment runs at its own factor (1x to 32x), and values crossing a cut go down to the base rate and up again through Infinite's resampler. It scores each plan with the phase-1 analysis and takes the cheapest one predicted at -80 dB or below. Then every plan is rendered through the real resamplers. **A plan only earns its saving if its measured alias meets the target**; otherwise the kernel is charged the whole-kernel cost.

<div data-fig="h2"></div>

**H2 fails: 17.4% aggregate saving (median 19.5% per kernel) against a 25% bar.** Staging pays where a cheap linear stage surrounds one nonlinearity, because the linear part runs at 1x. But a resampler pair costs as much as 6 to 14 interpreter instructions at the same factor, and most Field kernels are short. The fixed cost of each crossing eats most of what staging saves.

The three-stage `tanh` chain, the case that motivated the study, failed outright. The planner chose 2x, 4x, 8x and 1x segments and predicted -80.5 dB. Measured, it gave -43.0 dB, worse than a fixed 8x (-46.1 dB). Rendering the same plan with ideal brick-wall resamplers gives -74.0 dB. So about 31 of the 37 dB gap is the real half-band filter: its transition band passes energy around 20 to 28 kHz, which the next `tanh` folds back. The analysis treated each crossing as an ideal low-pass. Modelling each crossing's measured response would make these plans honest, but it would raise their factors, not the saving.

Staging also changes the sound of chains. Band-limiting between stages removes ultrasonic content that the next stage would otherwise have folded back as harmonics. Against a 32x render of the whole kernel, staged chains differ by -33 to -46 dB in harmonic amplitude. Whole-kernel oversampling at any factor differs by under -140 dB.

<details class="rn-drawer"><summary>Every run, including the ones that did not count</summary>
<p><strong>Run 1</strong> used Debug-build timings as the cost model and scored alias relative to each value's own fundamental: 0.8% saving. The relative score was meaningless for values with no fundamental (abs of the input), and Debug timings over-charged the resamplers about 10x.</p>
<p><strong>Run 2</strong>, the verdict run, scored alias at the output, weighted by how sensitive the output is to each crossing value, and used an -O2 microbenchmark for costs: 17.4%.</p>
<p><strong>Run 3</strong> added a guard against cuts where band-limiting makes a one-signed value cross zero. No plan changed.</p>
<p>Each change was made because a measured plan disagreed with its prediction, never to move the saving. The 25% bar was fixed before run 1.</p>
</details>

## What survives

The per-subgraph claim is dropped, and phase 2b (rate regions on the live audio path) will not start. What is worth shipping is the simpler result: the phase-1 pass as a compile-time suggestion of a **whole-kernel** oversampling factor for Field Effect, planned at the worst corner of a declared input range, with a plain message when a kernel has a jump that oversampling cannot fix. Feedback loops (bandwidth through `state` and `delay`) are a separate question and stay open.

## Threats to validity

- **Construct.** Alias is measured with one tone at one amplitude, and the needed factor moves with the stimulus. The rig's metric counts modulation sidebands and limit cycles as aliasing (the Moog ladder preset reads -29 dB at 1x and 2x for that reason and was excluded). Float32 phase accumulators floor the rig at about -100 dB.
- **Internal.** H2's CPU figures come from a cost model (nanoseconds per base-rate sample from an isolated -O2 microbenchmark), not from timing the app's audio thread. A cheaper resampler would raise the saving. A compiled backend would make instructions cheaper and crossings relatively more expensive, which would lower it. One machine (Apple M2).
- **External.** One author wrote the corpus, and held-out coverage is 2 of 18 presets. Kernels are short. Long kernels with one nonlinearity inside a lot of linear code would favour staging more.

## Prior art and clean room

The ingredients exist separately: the polynomial-degree bandwidth rule, Faust's amplitude interval analysis, and antiderivative anti-aliasing for single nonlinearities. We found no system that combines them to choose oversampling factors for audio, though our search was not exhaustive. Every idea here came from papers and public discussions. No other project's source code was opened.

## Reproduce it

The study lives in the Infinite repository under `docs/research/aliasing-aware-field/`. `scripts/kick-the-tires.sh` reruns phases 0 and 1 in under a minute and writes `data-rerun/phase1-predictions.csv`, which matched the committed file exactly on our machine. `scripts/run_all.sh` reruns everything. The table linking each claim to its command, data file and tolerance is in `ARTIFACT.md`.
