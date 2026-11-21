---
description: "Can a compiler pick the oversampling factor for nonlinear audio code? A pre-registered study in Infinite's Field language: the alias prediction works on our test corpus, and per-subgraph oversampling missed its bar."
seo_title: "Aliasing-aware compilation for Field | Infinite Research"
keywords: "aliasing, oversampling, DSP compiler, nonlinear audio, anti-aliasing, Field, negative result"
series: "Infinite Research · Technical Report 1"
version: "1.0"
abstract: "Nonlinear audio code aliases, and the usual remedy, oversampling, is chosen by hand and does not compose: in our rig one saturator at 8x stays below −118 dB of alias, three in series measure −46 dB. We asked whether a compiler can choose the factor itself. Before measuring, we pre-registered two hypotheses with kill criteria: (H1) a static analysis predicts alias well enough to rank kernels (Spearman ρ ≥ 0.8) and (H2) choosing the factor per subgraph saves at least 25% of CPU against oversampling the whole kernel at the factor it needs. On a corpus of 47 kernels in Infinite's Field language, alias falls by about 6(k+1) dB per doubling of the rate, where k is the number of continuous derivatives, so kernels with jumps cannot be fixed by oversampling at all. A periodic steady-state analysis predicts alias with ρ = 0.994 and names the needed factor exactly for 43 of 43 kernels, but it is exact by construction for memoryless kernels and covered only 2 of 18 held-out presets, so H1 is not confirmed. H2 is killed: per-segment plans saved 17.4%, and on the motivating three-saturator chain the plan measured −43.0 dB against a predicted −80.5 dB, worse than a fixed 8x. Real half-band resamplers pass energy that the next stage folds back. What survives is a compile-time suggestion of one whole-kernel factor."
---
## Introduction

Nonlinear audio code (a `tanh`, a `clamp`, an `abs`, a product of two signals) creates harmonics above the Nyquist frequency. At a fixed sample rate they fold back into the audible band as inharmonic aliasing. The common remedy is oversampling: run the nonlinear code at a multiple of the sample rate, low-pass filter, and decimate. Antiderivative anti-aliasing (ADAA) reduces alias for a single memoryless nonlinearity without raising the rate [@parker2016; @bilbao2017], and has been extended to stateful systems [@holters2020], recurrent networks [@mikkonen2025] and polynomial approximations [@gabrielli2026]. All of these are applied by hand, one nonlinearity at a time.

In Infinite, a person picks the factor per node; the Drive and Saturator nodes run at 8x. That choice does not compose. In our rig one Saturator stage at 8x (with ADAA) stays below −118 dB of alias, while three of the same stages in series, in one 8x domain, measure −46 dB. Field, Infinite's language for user-written kernels, makes this worse: its kernels are arbitrary code and its sample path has no oversampling at all.

This report asks whether a compiler can read a kernel, predict how much it aliases at each factor, and choose the cheapest factor per part of the kernel that meets a target. The target throughout is −80 dB below 20 kHz, the one Infinite's Analog node uses. Our contributions are a measured smoothness law for alias against rate on a corpus of 47 kernels (Section 4.1), a periodic steady-state alias predictor and its evaluation (Section 4.2), and a negative result for per-subgraph rate choice with its cause (Section 4.3). The data, scripts and every figure's source are public (Section 9).

## Pre-registered hypotheses

Both hypotheses, with the numbers that would kill them, were committed to the repository (commit `d38d29b7`) before the first measurement. Spearman's ρ [@spearman1904] was chosen because a compiler needs the ranking of factors to be right more than the absolute dB.

<div class="table-wrap"><table>
<thead><tr><th></th><th>Claim</th><th>Metric and bar</th><th>Kill criterion</th></tr></thead>
<tbody>
<tr><td>H1</td><td>A static analysis predicts alias.</td><td>Over ≥ 40 kernels, Spearman ρ ≥ 0.8 between predicted and measured alias; median absolute error ≤ 10 dB.</td><td>ρ &lt; 0.8</td></tr>
<tr><td>H2</td><td>Choosing the factor per subgraph pays.</td><td>At equal measured alias, ≥ 25% less CPU than oversampling the whole kernel at the factor it needs, and it beats a fixed 8x on composed chains.</td><td>saving &lt; 25%</td></tr>
</tbody></table></div>

No hypothesis is reported as confirmed without held-out validation; the corpus was written by the author of the analysis.

## Method

### Corpus and stimulus

The corpus has 47 Field kernels: polynomials, `tanh` and sine folders at several drives, hard clips, quantisers, naive oscillators, feedback loops, linear filters around a nonlinearity, and six of Infinite's shipping presets. Each kernel is driven by a 5 kHz sine (bin 1707 of a 16 384-point frame at 48 kHz, so one frame holds a whole number of periods) at amplitude 0.5.

### Measuring alias

Each kernel runs through the real Field Effect node at 1x, 2x, 4x, 8x, 16x and 32x of 48 kHz and is decimated with Infinite's minimum-phase filter chain. After warm-up, one steady-state frame is transformed. Every bin that is not a harmonic of the 5 kHz tone, below 20 kHz, counts as alias; the figure reported is its energy relative to the output's total, in dB. The needed factor is the smallest factor at or below −80 dB.

### Predicting alias without rendering

A kernel driven by one steady tone produces a periodic signal, so the pass computes that periodic steady state over one input period (8192 points) with the real interpreter, then folds every harmonic analytically for every factor. Feedback through state cells is solved as a periodic fixed point. Above the grid's band, a power law fitted to the top two octaves extends the spectrum, which is what gets jump kernels right at 32x. The pass declines kernels whose output is not a function of the input phase: delay lines, tables, random numbers, beat-clock reads, and feedback with no stable periodic solution. The idea of estimating a sufficient rate from the structure of a nonlinear system is old [@thornburg; @rbj2013]; interval analysis of signal ranges in Faust [@herrou2024] and its per-block rate work [@orlarey2024] are the nearest compiler-side ideas we found.

For a kernel with no memory driven by the rig's own tone, this prediction is exact by construction: sampling at the base rate samples the ideal periodic signal. The informative cases are kernels with state, phase accumulators, or a jump whose tail is extrapolated.

### Per-segment rate plans

The planner cuts a compiled kernel into at most four segments next to its nonlinear operations (at most three cuts, at most two values crossing each cut). Each segment runs at its own factor, 1x to 32x, and each crossing value goes down to the base rate and back up through Infinite's resampler. Plans are scored with the phase-1 analysis, weighted by how sensitive the output is to each crossing value, and the cheapest plan predicted at −80 dB or below is chosen. Every chosen plan is then rendered through the real resamplers, and **a plan only earns its saving if its measured alias meets the target**; otherwise the kernel is charged the whole-kernel cost.

CPU is a cost model, not a timing of the app's audio thread: nanoseconds per base-rate sample for one interpreter instruction, and for an up- and a down-sampler at each factor, from an isolated −O2 microbenchmark (best of five trials). The verdict run used one measurement of that model. To give the saving a spread, the model was re-measured in {{cost_n}} independent runs and the rig re-run under each (Section 4.3).

## Results

### Alias follows smoothness

<div data-fig="slopes"></div>

How smooth the nonlinearity is sets the slope ([[fig:slopes]]). Alias falls by about 6(k+1) dB per doubling of the rate, where k is how many derivatives stay continuous. Jumps (`floor`, comparisons, `%`) fall 4.9 to 6.1 dB per doubling, kinks (`abs`, `max`, hard clip) 11.8 to 12.9, and a C1 function such as x/(1+|x|) 17.4 to 17.9. Analytic functions such as `tanh` fall off a cliff once the rate clears a bandwidth that depends on drive. Three consequences follow.

- **Oversampling cannot fix jumps.** No jump kernel, and neither the naive saw nor the naive square, reached −80 dB even at 32x; the best was −50 dB. For these a compiler should not oversample but say why, and point to band-limited steps or ADAA.
- **The needed factor varies widely.** For −80 dB: 1x for 8 kernels, 2x for 9, 4x for 3, 8x for 6, 16x for 6, 32x for 5, and never for 7. One global factor wastes CPU on most kernels or fails the rest.
- **Shipping presets alias audibly at 1x.** Tube Overdrive measured −35 dB and Wavefolder −22 dB.

### The predictor

<div data-fig="scatter"></div>

Over 43 predicted kernels at six factors, the rank correlation is ρ = **0.994** (bar 0.8), the median absolute error **0.0 dB** (bar 10), and the needed factor is predicted exactly for **43 of 43** kernels ([[fig:scatter]]). The analysis costs about 4.6 M interpreter runs for the whole corpus, against 91 M for rendering every factor.

This number is weaker than it looks, for three reasons. First, the bar was set for an approximate analysis and is cleared trivially by the memoryless kernels, which are exact by construction. On the informative cases the error is still small: feedback (3 kernels) 0.0 dB, linear filters around a nonlinearity 0.0 to 0.5 dB, and the extrapolated tail for jumps 0.1 to 0.6 dB at 32x ([[fig:explorer]]). Second, the pass knows the stimulus exactly and a compiler does not: sweeping input amplitude (0.25, 0.5, 1.0) and frequency (0.5 to 5 kHz) changed the needed factor for 31 of 38 kernels, 29 of them upward. A shipped version would have to plan at the worst corner of a declared input range. Third, the corpus was written by one person. On 18 shipping Field Effect presets that were not in it, the pass accepted 2 (factor exact, worst error 0.1 dB) and declined 16: 7 read state tables, the rest have no periodic steady state under a steady tone. Two of eighteen cannot carry a general claim. **H1 is not confirmed.**

<div data-fig="explorer"></div>

The analysis also found a bug. Two presets came out 2 dB cleaner than measured; their `drive` knob held 3.0, not its declared 2.5, because when Field recompiles a parameter keeps its value by name, so a kernel inherits any same-named knob from the one before. Picking a preset in the app goes through the same path. The data uses corrected values; the app fix is not yet released.

### Per-segment oversampling

<div data-fig="h2"></div>

**H2 fails.** The aggregate saving is 17.4% (median 19.5% per kernel) against a 25% bar ([[fig:h2]]). Re-measuring the cost model in {{cost_n}} independent runs and re-running the rig under each gives {{cost_mean}}% (95% CI {{cost_lo}} to {{cost_hi}}), also below the bar ([[fig:costs]]). The repeats measured a cheaper interpreter instruction (about 2.7 ns, against 3.5 ns in the verdict line), which lowers the saving; the verdict line is kept as the pre-registered result. Staging pays where a cheap linear stage surrounds one nonlinearity, because the linear part runs at 1x. But a resampler pair costs as much as 6 to 14 interpreter instructions at the same factor, and most Field kernels are short; the fixed cost of each crossing eats most of what staging saves.

<div data-fig="costs"></div>

The three-stage `tanh` chain, the case that motivated the study, failed outright ([[fig:audio]]). The planner chose 2x, 4x, 8x and 1x segments at a modelled cost of 597 against 4272 for the whole kernel at 32x, and predicted −80.5 dB. Measured, it gave −43.0 dB, worse than a fixed 8x (−46.1 dB at cost 1048). Rendering the same plan with ideal brick-wall resamplers gives −74.0 dB, so about 31 of the 37 dB gap is the real half-band filter: its transition band passes energy around 20 to 28 kHz, which the next `tanh` folds back. The analysis treated each crossing as an ideal low-pass. Modelling each crossing's measured response would make these plans honest, but it would raise their factors, not the saving.

<div data-fig="audio"></div>

Staging also changes the sound of chains. Band-limiting between stages removes ultrasonic content that the next stage would otherwise have folded into harmonics. Against a 32x render of the whole kernel, staged chains differ by −33 to −46 dB in harmonic amplitude; whole-kernel oversampling at any factor differs by less than −140 dB.

## Discussion

The per-subgraph claim is dropped, and the planned next phase (rate regions on the live audio path) will not start. What is worth shipping is the simpler result: the phase-1 pass as a compile-time suggestion of one **whole-kernel** factor for Field Effect, planned at the worst corner of a declared input range, with a plain message when a kernel contains a jump that oversampling cannot fix. Two things would change the H2 verdict: much cheaper crossings (a resampler costing a few instructions), or much longer kernels with one nonlinearity inside a lot of linear code. Feedback loops, where bandwidth circulates through `state` and `delay`, are a separate question and remain open.

## Threats to validity

- **Construct.** Alias is measured with one tone at one amplitude, and the needed factor moves with the stimulus (Section 4.2). The metric counts modulation sidebands and limit cycles as alias; the Moog ladder preset reads −29 dB at 1x and 2x for that reason and was excluded. Kernels with a float32 phase accumulator (the naive oscillators) cannot measure below about −100 dB, because the accumulator's rounding is itself inharmonic; memoryless and filter kernels measure down to about −138 dB. Predictions and measurements are both floored at −100 dB for scoring.
- **Internal.** H2's CPU figures come from a cost model, not from timing the audio thread. A cheaper resampler would raise the saving; a compiled backend would make instructions cheaper and crossings relatively dearer, which would lower it. The cost model's spread across runs is reported ([[fig:costs]]); the model itself was measured on one machine (Apple M2).
- **External.** One author wrote the corpus, and held-out coverage is 2 of 18 presets. Kernels are short.

## Related work

ADAA [@parker2016; @bilbao2017] and its extensions [@holters2020; @mikkonen2025; @gabrielli2026] attack alias at a single nonlinearity without raising the rate; they are complementary to choosing a rate, and the right tool for the jump kernels oversampling cannot fix. The rule that a polynomial of degree d needs d times the bandwidth is folklore; a public statement of it is [@rbj2013]. Estimating a sufficient sampling rate for a nonlinear system was posed as a research topic in [@thornburg]. Faust computes signal intervals for fixed-point formats [@herrou2024] and has explored per-block rates [@orlarey2024]. We found no system that combines a rate analysis with oversampling choice for audio, though our search was not exhaustive. Every idea here came from papers and public discussions; no other project's source code was opened.

## Conclusion

A compiler can predict how much simple nonlinear audio code aliases, and the rate at which alias falls is set by smoothness. Splitting a kernel across rates does not pay with real resamplers at Field's kernel sizes. The useful product is one factor per kernel, chosen at compile time, with a warning when no factor will do.

## Reproducibility

The study is in the Infinite repository under `docs/research/aliasing-aware-field/`. `scripts/kick-the-tires.sh` reruns phases 0 and 1 in under a minute and writes `data-rerun/phase1-predictions.csv`, which matched the committed file exactly on our machine. `scripts/run_all.sh` reruns everything; `scripts/cost_repeats.py` reruns the cost-model repeats and `scripts/make_audio.py` makes the listening clips. `ARTIFACT.md` links each claim to its command, data file and tolerance. Raw data is never edited; every figure and table here is generated from it.

<details class="rn-drawer"><summary>Appendix A. Every H2 run, including those that did not count</summary>
<p><strong>Run 1</strong> used Debug-build timings as the cost model and scored alias relative to each value's own fundamental: 0.8% saving. The relative score was meaningless for values with no fundamental (the absolute value of the input), and Debug timings over-charged the resamplers about tenfold.</p>
<p><strong>Run 2</strong>, the verdict run, scored alias at the output, weighted by how sensitive the output is to each crossing value, and used the −O2 microbenchmark for costs: 17.4%.</p>
<p><strong>Run 3</strong> added a guard against cuts where band-limiting makes a one-signed value cross zero. No plan changed.</p>
<p>Each change was made because a measured plan disagreed with its prediction, never to move the saving. The 25% bar was fixed before run 1.</p>
</details>
