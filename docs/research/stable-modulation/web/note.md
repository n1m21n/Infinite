---
description: "Can a compiler prove that a user-written filter never blows up, however fast its knobs move? A pre-registered study in Infinite's Field language: the proof is automatic, never wrong on 174 kernels that can blow up, matches four published hand proofs, and covers 89% of stable kernels in a blind set. A slew-limited extension fails as registered."
seo_title: "Proven-stable modulation for Field filters | Infinite Research"
keywords: "time-varying filters, stability, modulation, Lyapunov, LMI, switched systems, DSP compiler, Field, virtual analog"
series: "Infinite Research · Technical Report 2"
version: "1.0"
prereg_commit: "5de4551b"
outcome: "H1–H3 confirmed · H4 failed"
abstract: "A filter that is stable at every fixed knob setting can still blow up when the knobs move: a linear Moog ladder whose resonance alternates between 0 and 2.89 every sample grows without bound, though it is stable at every fixed resonance up to 4. Infinite's compiler checks stability at one fixed setting only, and Field users and agents write their own filters. We asked whether a compiler can prove, from a kernel's code alone, that it cannot blow up under any modulation inside its knob ranges. Before measuring, we pre-registered four hypotheses with kill criteria. The pass reads each kernel's state matrix from Infinite's interpreter, finds one quadratic energy function that falls at every knob setting (a linear matrix inequality), re-checks it in float64 and covers the whole knob box with an adaptive cell split. On {{n_kernels}} kernels (16 from the literature, 200 generated, 200 blind), the instability oracle finds a witness for {{n_witness}}, and the pass certifies {{false_certs}} of them (H1). It matches all four of McClellan's hand proofs, and for the Moog ladder it proves k ≤ {{moog_cert}}, past the published 2.88, against an oracle witness at {{moog_unstable}} (H2). It certifies {{h3_G}} of the stable generated kernels and {{h3_B}} of the blind set, against a bar of 80% (H3). Today's frozen check, at the default knobs, calls {{b1a_B}} blind kernels that can blow up safe. Slew certificates (H4), which let the energy function depend on the knob when the knob moves slowly, add nothing as registered: while the cutoff can reach zero, the filter cannot decay fast enough to pay for any change in the energy function. With the cutoff held at 20 Hz or above, they widen the certified range by {{moog_slew20_pct}} (Moog) and {{diode_slew20_pct}} (diode), an unregistered result. The pass runs in Python, as a research tool; it is not in the app."
---
## Introduction

Musical modulation moves filter knobs fast: envelopes, LFOs and audio-rate cables change a resonance or a cutoff every sample. A recursive filter whose poles sit inside the unit circle at every fixed setting can still blow up under that motion. The best-known case takes two lines to show [@mcclellan2026]: a linear Moog ladder at cutoff g = 1.3764, with its resonance k switched between 0 and 2.89 every sample, grows without bound, although the same filter is stable for every fixed k below 4 ([[fig:trap]]).

<div data-fig="trap"></div>

Proofs that a filter survives modulation exist for named filters, written by hand [@laroche2007; @wishnick2014; @werner2022; @mcclellan2026]. Shipped tools rely on structure (topology-preserving transforms) and on ranges written in comments; the Faust library documents ladder feedback "between 0 and 4" [@faust_vaeffects]. In Infinite, the only check is a frozen one: the compiler's analysis computes the spectral radius of the state Jacobian at the default knobs and calls a value below 1 stable. That is the check the trap defeats. And Field, Infinite's language for user-written kernels, lets users and agents write their own recurrences, which no hand proof covers.

This report asks whether a compiler can read a kernel and prove, from its code alone, that the kernel cannot blow up under any modulation of its knobs inside their declared ranges, or say that it can't. The maths exists in control theory: one quadratic energy function that falls at every knob setting, found by a linear matrix inequality (LMI) [@boyd1994]. It has not been joined to an audio language's compiler. Our contributions are an automatic certifier for Field kernels that are linear in their state (Section 3), its evaluation against an instability oracle, four published hand proofs and a blind set (Section 4), and a negative result for slew-limited certificates with its cause (Section 4.5). The data, scripts and every figure's source are public (Section 9).

## Pre-registered hypotheses

All five hypotheses, with the numbers that would kill them, were committed to the repository (commit `5de4551b`) before the first measurement.

<div class="table-wrap"><table>
<thead><tr><th></th><th>Claim</th><th>Metric and bar</th><th>Kill criterion</th></tr></thead>
<tbody>
<tr><td>H1</td><td>Sound: the pass never certifies a kernel that can blow up.</td><td>0 certificates on kernels with an instability witness, on L, G and the blind set B.</td><td>any such certificate (after a rig-bug check)</td></tr>
<tr><td>H2</td><td>It matches the hand proofs automatically.</td><td>≥ 99% of McClellan's published range for SVF, Sallen-Key, Moog and diode ladder; never past the published counterexamples.</td><td>any filter below 95%, or a certificate past 2.89 / 8.91</td></tr>
<tr><td>H3</td><td>It covers most stable user-style kernels.</td><td>≥ 80% of generated kernels without a witness certified, then the same on the blind set.</td><td>&lt; 80%</td></tr>
<tr><td>H4</td><td>Slew certificates widen the range for smooth modulation.</td><td>With k moving at most its full range in 20 ms, the certified k range of the Moog and diode ladders is ≥ 25% wider than the any-speed range.</td><td>&lt; 25% on either</td></tr>
<tr><td>H5</td><td>It is cheap enough for a compiler.</td><td>Median ≤ 50 ms per kernel in C++.</td><td>scored only if an in-app port is approved</td></tr>
</tbody></table></div>

The study ships as research if H1 to H3 pass. Because the author wrote the analysis and the first two corpora, "confirmed" requires the blind set B, generated after the scorer was locked (commit `f9180894`) and scored once.

## Method

### Kernels and their matrices

A Field kernel with `state` cells whose update is linear in the state is a linear parameter-varying system: x[n+1] = A(θ[n]) x[n] + B(θ[n]) u[n], where θ are its knobs inside their declared ranges. The coefficients may be any function of θ (`sin`, `pow`, `exp`, a `tan` prewarp, or `if` on a knob, which makes the system switched). Nonlinear loops and delay lines are out of scope.

A(θ) is read from the real interpreter, not from a second evaluator. A self-test in a dev build runs one sample step with the state set to each unit vector and the input at zero; column i of A is the next state. A linearity gate checks superposition on random state pairs, and a kernel that fails it leaves scope. Across 232 compiled kernels and 32 112 knob settings, the interpreter’s matrices match a Python reference within 1.7e-6 (Float32 rounding).

Three corpora were scored. L holds 16 filters ported from published equations: McClellan's four, in the paper's state form, and twelve textbook filters (one-pole, Chamberlin and TPT state-variable filters, three RBJ biquad forms with modulated cutoff and Q, a coupled resonator, an allpass and a 4-stage phaser, a Stilson ladder, a DC blocker). G holds 200 generated kernels (seed 20261121): random dense state updates of size 1 to 6 with 1 to 3 knobs, coefficients built from affine, `sin`, `pow`, `exp` and `if` terms, half of them built to be stable at every fixed setting. B, the blind set, uses the same generator with seed 20261123, sizes up to 8 and twice the share of `if` terms. Field's sample backend caps a kernel at 128 registers, and a dense update of size n costs about 3n² of them, so {{n_python}} of the 400 generated kernels don't compile. Those were scored on the Python matrices and are reported separately.

### Ground truth: an instability oracle

A product of state matrices A(θ₁)…A(θₖ) with spectral radius above 1 is a proof of instability: repeating that switching sequence grows the state without bound. The oracle searches for such a witness (radius > 1 + 1e-6, length ≤ 8): every product of length ≤ 4 over the box corners, a dense frozen scan, a greedy search over periodic products, and continuous tuning of the best candidates. A witness proves instability; the absence of one proves nothing, so H1 is scored on kernels with a witness and H3's denominator is the kernels without one. The oracle was checked against the paper's counterexamples first: it finds the Moog ladder unstable from k = {{moog_unstable}} and the diode ladder from {{diode_unstable}}, both inside the published (2.88, 2.89] and (8.90, 8.91].

### The certifier

The pass looks for one matrix P such that the energy xᵀPx falls at every knob setting: A(θ)ᵀ P A(θ) ≺ P for all θ in the box. If such a P exists, the kernel cannot blow up under any sequence of knob values, at any speed. On a grid of settings this is an LMI, solved with Clarabel through CVXPY [@cvxpy; @clarabel], with each point rescaled by its own decay so that settings that barely decay (cutoff near zero) are as well conditioned as the rest. The solver's P is never trusted: the norm ‖P^½ A P^−½‖ is re-checked in float64 at every point. Between grid points the box is covered by cells. On a cell, A(θ) lies within a measured margin of the blend of its corner matrices, and the P-norm is convex, so a cell closes when the largest corner norm plus 1.5 times the largest blend error measured at the cell centre and edge midpoints is below 1. A cell that fails is split; a corner whose own norm reaches 1 is added to the LMI as a cutting plane. Cells start split at every `if` threshold, so each sees one side of a jump.

Slew certificates (H4) let P depend on the knob, P(k), with the energy required to fall only for knob steps no larger than the slew limit. This is the standard rate-bounded parameter-dependent Lyapunov function [@boyd1994]; it reduces to the common P when the slew is unbounded.

## Results

### The hand proofs, automatically (H2)

<div data-fig="h2"></div>

H2 passes on all four filters with the same code and no hand work ([[fig:h2]]). For the Moog ladder the certificate goes past the published proof. Together with the oracle's witness, which moves the cutoff along with the resonance, the any-speed limit of the linear Moog ladder is now pinned to k ∈ [{{moog_cert}}, {{moog_unstable}}), where the paper left [2.88, 2.89]. On 200 000 random settings the certifier never saw, no point has a P-norm of A at or above 1.

### Today's check is fooled

<div data-fig="frozen"></div>

On the generated set, {{wit_G}} of 200 kernels can blow up, and the frozen check at the default knobs calls {{b1a_G}} of them safe; on the blind set it calls {{b1a_B}} safe ([[fig:frozen]]). A far stricter frozen check, at every point of a 7-per-knob grid, still misses {{b1b_G}} and {{b1b_B}}: their witnesses need the knobs to move, and no frozen check can see that. The worst of the missed kernels grow by 17% per sample under their witness, a million times louder in about 90 samples (2 ms at 48 kHz).

### Soundness and coverage (H1, H3)

<div data-fig="verdicts"></div>

<div data-fig="growth"></div>

H1 holds on all three sets: of {{n_witness}} kernels with a witness, the certifier certifies {{false_certs}} ([[fig:verdicts]]). H3 passes on G ({{h3_G_frac}}, {{h3_G}}) and is confirmed on the blind set ({{h3_B_frac}}, {{h3_B}}). Every cover the certifier started closed; no kernel ended with only a grid proof. [[fig:growth]] shows the shape of the result: certified kernels all sit left of 1, witnesses all sit right of it, and the kernels the pass leaves unproved without a witness sit just below 1 (worst growth 0.95 to 0.998), where every LMI is clearly infeasible. Each of those is either stable without a common quadratic energy function, or unstable in a way the oracle did not find. Coverage falls with size: on B, 73 of 76 kernels of size up to 5 are certified, and 29 of 39 of size 6 to 8. Two literature filters are left without a proof for the right reason: the one-pole at a = 0 and the coupled resonator at r = 1 reach the edge of stability inside their own boxes. Certification takes {{t_median}} per kernel at the median and {{t_p95}} at p95, in Python on an M2.

### Float32

<div data-fig="float32"></div>

A certificate is a float64 statement, while the interpreter computes in Float32. At up to 5000 check points per certificate, the interpreter's own matrices were measured ([[fig:float32]]). Every measured G and B certificate holds and is proved, with a largest rise of 1.7e-7. Two literature filters show the limit: the paper's SVF and the TPT SVF are certified with a margin smaller than Float32 can resolve, because their boxes reach the edge of stability (R = R_min). A shipped check must report them as not proved in Float32, or shrink the box. For the kernels that don't compile, a loose bound stands in for the measurement, and 9 of 81 certificates are not proved by it.

### Slew certificates (H4)

<div data-fig="h4"></div>

H4 fails as registered ([[fig:h4]]). With the cutoff free down to 1e-5 (about 0.15 Hz), a slew limit adds nothing to either filter. The cause is structural, not a tuning problem. As the cutoff goes to zero, the filter stops moving: A → I, and the energy can fall by only about g per sample. Hold the state fixed and step k up or down: for the energy never to rise, V(x, k ± Δk) ≤ V(x, k) must hold both ways, so near g = 0 any change of P with k must be paid for by a decay that is not there. Any energy function V(x, k) that ignores the cutoff is forced flat in k, and so collapses to the any-speed certificate. Affine and piecewise-linear P(k), with 9 and 17 nodes, both fail past the any-speed limit.

With the cutoff held at 20 Hz or above, an unregistered exploratory run, the slew limit widens the certified range to k ≤ {{moog_slew20}} for the Moog ladder (+{{moog_slew20_pct}}, short of the bar) and to {{diode_slew20}} for the diode ladder (+{{diode_slew20_pct}}). A control run shows the gain comes from the slew limit, not from the floor: with the floor and a constant P, the LMI is already infeasible at Moog 2.9 and diode 8.9. Simulations at those limits stay bounded.

## Discussion

A compiler can prove that a user-written linear filter survives any modulation of its knobs, from its code alone, and the proof costs well under a second for most kernels. On the blind set the pass is never wrong where it can be checked, and it certifies nearly nine in ten of the kernels with no known instability, against today's frozen check, which waves through nine in ten of the kernels that do blow up. For hand-written filters it is at least as good as a person: it reproduces four published proofs and tightens one.

What it does not do is equally clear. Slow modulation does not buy a wider range unless the certificate also knows how fast the filter itself decays; a fixed 20 ms slew limit means nothing to a filter whose cutoff can be 0.15 Hz. The useful form of the slew claim is relative: a step in k no larger than some multiple of the cutoff, per sample. That is a new hypothesis, not a rescue of this one.

## Threats to validity

- **What "certified" means.** The LMI and its float64 re-check are exact at their points. Between points, the cover's margin is a measured heuristic (1.5 × the blend error at the cell centre and edge midpoints), not a bound. A certificate is "verified on a dense adaptive cover", not a formal proof over the continuum. Evidence that the margin holds: 200 000 unseen random points in the hand-proof check, up to 5000 more per certificate in Float32, and no point anywhere with a P-norm of A at or above 1. An interval bound on A(θ) per cell would make it a proof; it was not done.
- **The oracle is one-sided.** An unstable kernel the oracle misses would sit in the H3 denominator, and a false certificate on it would count as a success. The float64 cover makes that unlikely; the oracle can't rule it out. It had one rig bug (a grid too coarse to see four frozen-unstable kernels), found and fixed before any verdict.
- **Who wrote the kernels.** L was ported by the author; G and B come from one generator the author wrote. No real user kernels or shipped presets were scored. Generated kernels are dense random systems, a harder shape than typical sparse filters, so coverage on real filters may be higher; it is not measured.
- **Matrices from Python.** {{n_python}} of 400 generated kernels exceed Field's register cap and were scored on Python matrices, with a bound in place of the Float32 measurement.
- **Linear only.** Nonlinear loops (`tanh`, `clamp`) and delay lines, common in real patches, are out of scope.
- **H4's bar was unreachable by design.** The registered claim fails for every energy function that ignores the cutoff; that should have been caught before sign-off.

## Related work

Laroche gave two state-matrix criteria for time-varying recursive filters, applied per topology [@laroche2007]; Rabenstein and Czarnach showed that stability depends on the state-space structure as well as the coefficients [@rabenstein1985]. Wishnick proved the state-variable filter stable under modulation [@wishnick2014]. Werner and McClellan extended Laroche's norm criterion to products of state matrices over several steps [@werner2022]. McClellan used common quadratic Lyapunov functions for trapezoidally discretised filters, with hand-found or randomly searched P for four filters and counterexamples at the edge, and named slowly varying parameters as future work [@mcclellan2026]. The LMI and parameter-dependent Lyapunov machinery is standard in control [@boyd1994]. We found no compiler, DSL or audio tool that certifies stability of user-written filters under modulation; absence in a search is not proof of absence. Infinite's first report studied a different compile-time analysis on the same language [@infinite_report1].

## Conclusion

Stability under modulation can be checked by a compiler, automatically and soundly in every case we could test, for any Field kernel that is linear in its state. Today's frozen check cannot do this, and on generated kernels it misses most of the ones that blow up. Slew-limited certificates do not help while the cutoff can reach zero; a rate-aware form is the next question. The pass is a Python research tool; putting it in Infinite's compiler is a separate decision.

## Reproducibility

The study is in the Infinite repository under `docs/research/stable-modulation/`. `scripts/kick-the-tires.sh` reruns the oracle against the paper's counterexamples, the certifier at the published bounds and the locked scorer on 40 kernels in about two minutes, and compares each verdict with the committed data. `scripts/run_all.sh` reruns every phase into `data-rerun/` (the Float32 step needs a dev build of Infinite). `ARTIFACT.md` links each claim to its command, data file and tolerance. Raw data is never edited; every figure and table here is generated from it.

<details class="rn-drawer"><summary>Appendix A. Rig bugs, and changes from the registered method</summary>
<p><strong>Oracle, phase 0.</strong> The first oracle's grid included the cutoff ends g = 0 and g → ∞, where A = ±I has radius exactly 1; those trivial points outranked the real near misses. Candidates are now interior only. Found before any score.</p>
<p><strong>Oracle, phase 3.</strong> A 4-to-9-point grid missed four generated kernels that are unstable at one fixed setting between grid points. The oracle now runs a dense frozen scan with local tuning. The baseline counts moved from 76 to 80 kernels with a witness; no witness was lost and no verdict changed.</p>
<p><strong>Float32 check.</strong> The first pass showed rises up to 0.44 on kernels with `if`. Every such point sat within 1e-9 of a threshold, below Float32 resolution, so the interpreter took the other branch. Points are now moved to 1e-6 of the knob range on the cover's side.</p>
<p><strong>Cover margin.</strong> The registered first-order Lipschitz margin can't close near g → 0, where decay per sample shrinks like g but the derivative of A doesn't; the second-order blend margin replaced it before H2 was scored.</p>
<p><strong>H4.</strong> Piecewise-linear P(k) (9 and 17 nodes) was added to the registered affine P(k) once affine failed; it fails the same way.</p>
</details>
