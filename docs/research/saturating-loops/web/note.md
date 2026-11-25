---
description: "Can a compiler prove that a filter with saturation in its feedback never blows up or sticks, however its knobs move? A pre-registered study in Infinite's Field language: the proof is automatic and was never wrong on {{n_witness}} kernels that can blow up or stick, but it covers only {{h3_B}} of stable kernels in a blind set, short of the 70% bar, and reaches Hélie's 5/3 for the Moog ladder only at moderate cutoffs."
seo_title: "Stability certificates for saturating Field filters | Infinite Research"
keywords: "nonlinear filters, saturation, Lur'e systems, absolute stability, Lyapunov, LMI, time-varying filters, Moog ladder, DSP compiler, Field, virtual analog"
series: "Infinite Research · Technical Report 3"
version: "1.0"
prereg_commit: "bbab01bf"
outcome: "Mixed. H1 (sound) holds on all {{n_kernels}} kernels, blind set included, and H4 (forgets its past) holds. H2 fails as registered, and H3 passes on the generated set ({{h3_G}}) but fails on the blind set ({{h3_B}}, bar 70%), so the coverage claim is not confirmed."
abstract: "Report 2 showed that a compiler can prove a linear filter cannot blow up under any modulation of its knobs. Real filters saturate: a tanh per ladder stage, a clipped integrator, a soft clipper in the feedback. Saturation can make things worse, not better: a loop that is stable for small signals can wind up and blow up for loud ones. We asked whether the same compiler pass extends to Field kernels whose loop is linear except for scalar saturators (tanh, a soft clipper, clamp, sin, abs). Before measuring, we pre-registered five hypotheses with kill criteria. The pass finds each saturator in the kernel's compiled code, cuts it out, reads the remaining linear system from Infinite's interpreter, and looks for one energy function that falls for every knob setting and every saturator gain allowed by the saturator's sector. On {{n_kernels}} kernels ({{n_L}} from the literature, 200 generated, 200 blind), the oracle finds a growing or stuck witness for {{n_witness}}; the pass certifies {{false_certs}} of them (H1). Linearising the saturators and running Report 2's certifier is unsound: it certifies {{b4_G}} generated kernels that wind up and blow up. The pass certifies {{h3_G}} of stable generated kernels but only {{h3_B}} of the blind set, which has larger kernels and more saturators, against a bar of 70%: H3 is not confirmed. Most certified kernels also provably forget their starting state, and none failed a two-start test (H4). On the tanh Moog ladder a Lur'e-Lyapunov certificate reaches Hélie's hand-proved 5/3 for cutoff ceilings up to g = {{helie_ceiling}}, but not over the port's full range, where the explicit filter itself self-oscillates below 5/3; H2 fails as registered. The pass runs in Python, as a research tool; it is not in the app."
---
## Introduction

Saturation is how analog-style filters sound like analog filters: a tanh at each stage of a Moog ladder, a clipper on a state-variable filter's integrator, a soft clipper in a feedback path. It is also the part of a filter that no shipped tool checks. Report 2 showed that a compiler can prove, from a kernel's code alone, that a filter linear in its state cannot blow up under any modulation of its knobs [@infinite_report2]. That proof does not cover a saturating loop, and the obvious shortcut (replace each saturator by its slope at zero and run the linear proof) is wrong: on generated kernels it certifies loops that are stable for small signals and blow up for loud ones ([[fig:windup]]).

<div data-fig="windup"></div>

Stability proofs for saturating filters exist for named circuits, written by hand. Hélie proved the continuous-time tanh Moog ladder stable for resonance k < 5/3 under time-varying cutoff and resonance, with an energy function built from the circuit [@helie2011]. Danish, Bilbao and Ducceschi gave port-Hamiltonian schemes for the Korg35 and Moog filters whose stability survives discretisation, with the Moog's proof limited to fixed resonance [@danish2021]. Chowdhury gave two nonlinear biquad structures that are stable when every nonlinearity has slope at most 1 [@chowdhury2020]. Each covers one structure. Field users and agents write their own.

This report asks whether a compiler can certify, from a kernel's code, that a loop which is linear except for scalar saturators can neither blow up nor stick in an oscillation with no input, under any modulation of its knobs. A kernel like that is a Lur'e system, and control theory has the tools [@boyd1994]. Our contributions are a pass that finds saturators in Field's compiled code and extracts the Lur'e form from the real interpreter (Section 3.1), sector and Lur'e-Lyapunov certificates on Report 2's certifier (Section 3.3), and an evaluation against an oracle for growing and stuck trajectories, a hand proof and a blind set (Section 4).

## Pre-registered hypotheses

All five hypotheses, with the numbers that would kill them, were committed (commit `bbab01bf`) before the first measurement. One method decision was amended by the owner after phase 0, as recorded in the README: the Lur'e-Lyapunov certificate became the main method, with the sector-only certificate kept as its baseline.

<div class="table-wrap"><table>
<thead><tr><th></th><th>Claim</th><th>Metric and bar</th><th>Kill criterion</th></tr></thead>
<tbody>
<tr><td>H1</td><td>Sound: the pass never certifies a kernel that can blow up or stick.</td><td>0 certificates on kernels with a witness, on L, G and the blind set B.</td><td>any such certificate (after a rig-bug check)</td></tr>
<tr><td>H2</td><td>It matches or beats the hand proof for the tanh Moog ladder under modulation.</td><td>Certified any-speed k range ≥ Hélie's 5/3 on the explicit Field Moog with a saturator per stage, and never past the oracle's first witness.</td><td>certified range &lt; 5/3, or a certificate past a witness</td></tr>
<tr><td>H3</td><td>It covers most stable-looking saturating kernels.</td><td>≥ 70% of generated kernels without a witness certified, then the same on B.</td><td>&lt; 70%</td></tr>
<tr><td>H4</td><td>It proves most of them forget their past, soundly.</td><td>Convergence certificates on ≥ 50% of certified kernels, and none fails the two-start test.</td><td>&lt; 50%, or any failure</td></tr>
<tr><td>H5</td><td>It is cheap enough for a compiler.</td><td>Median ≤ 50 ms per kernel in C++.</td><td>scored only if an in-app port is approved</td></tr>
</tbody></table></div>

The study ships as research if H1 to H3 pass; whatever the result, it is published. Because the author wrote the analysis and the corpora, "confirmed" requires the blind set B, generated after the scorer was locked and scored once.

## Method

### Saturators, found in the compiled kernel

A Field kernel whose next state is x⁺ = A(θ)x + B(θ)w + (input), with w = φ(y) and y = C(θ)x, applied elementwise by scalar saturators φᵢ, is a Lur'e system with a time-varying linear part. The pass recognises a fixed library of shapes, each with a sector (the range of φ(y)/y) and a slope range proved on paper and checked numerically: tanh, the soft clipper y/(1 + |y|), and clamp (sector and slope [0, 1]); abs (sector and slope [−1, 1]); sin (sector [−0.217, 1], slope [−1, 1]). A self-test in a dev build walks the interpreter's compiled code, finds each shape on a value that depends on the state (Field has no tanh instruction, so tanh is matched as (e − 1)/(e + 1) with e = exp(2y)), and cuts it: its output becomes an injected input w and its argument a recorded output y. One sample step per unit vector of x and of w gives A, B, C and D. Report 2's linearity gate then checks that nothing nonlinear is left; a loop with a nonlinearity outside the library fails the gate and is reported out of scope, never certified.

### Kernels and ground truth

L holds {{n_L}} filters ported from published equations and written as Field kernels a second time from the same equations: the tanh and soft-clipped Moog ladders with a saturator per stage, Moog and diode ladders with saturation on the feedback only, a Korg35-style 2-pole, a saturating one-pole, two transposed-direct-form biquads with saturated states or output, a Rossum-style saturating 2-pole, a state-variable filter with a clipped integrator and a clipped allpass phaser loop. G holds 200 generated kernels: Report 2's linear generator (sizes 1 to 6, 1 to 3 knobs, coefficients from affine, `sin`, `pow`, `exp` and `if` terms) with 1 to 3 saturators on loop paths, on a state's whole update (a ladder stage) or where a state is read. B, the blind set, uses seed 20261124, sizes up to 8, up to 4 saturators and 30% of terms with `if`.

A kernel is "not safe" when the oracle finds a witness with zero input: a periodic switching of knob settings and a start whose trajectory **grows** past 10⁶ times its start within 2¹⁶ samples, or a periodic orbit of the switched nonlinear map, refined by Newton's method to a residual below 1e-10, with amplitude above 1e-3, that **sticks** and never decays. The oracle was checked first on the tanh Moog: at every cutoff ceiling its first witness lies within 0.001 of the filter's frozen self-oscillation limit, the known answer.

### Certificates

**G1 (goes quiet).** The sector certificate (T1) replaces each saturator by an unknown gain δᵢ in its sector and asks Report 2's certifier for one quadratic energy xᵀPx that falls for every knob setting and every δ in the box: A + B diag(δ) C is affine in each δᵢ, so the box's corners and Report 2's adaptive cell cover decide it [@infinite_report2]. Every corner must decrease strictly, δᵢ = 0 included, which is sound and conservative. On ladders a sector bound is provably not enough: for two tanh stages in cascade, no quadratic energy works even at a fixed cutoff. The Lur'e-Lyapunov certificate (T3) adds the saturators' integrals to the energy, V = xᵀPx + 2Σ qᵢ∫φᵢ, with sector and slope bounds as S-procedure terms, solved as an LMI with Clarabel through CVXPY [@cvxpy; @clarabel]. T3's corners are exact only for knob-affine kernels with tanh, soft or clamp shapes; elsewhere a T3 pass is recorded as grid-only and not counted. G1 is T1 or an exact T3.

**G2 (forgets its past).** Two runs under the same input and modulation differ by e⁺ = (A + B S C)e, with each Sᵢ inside φᵢ's slope range (mean-value theorem). The slope certificate (T4) is T1 on that box. For tanh, soft, clamp and abs the slope range equals the sector, so T4 is T1's problem and its verdict carries over; sin gets its own run. A two-start test checks every certified kernel: two random starts, the same random input up to +12 dBFS and the same random modulation (held for 1 to 512 samples), 32 768 samples, failure if the difference has not fallen below 1e-6.

**Float32.** A certificate is a float64 statement; the interpreter computes in Float32. An entry error ε in A, B, C moves each corner matrix by a bounded amount, so the P-norm moves by at most √κ(P)·n times that. With a safety factor of 10, the certificate holds in Float32 if that is below the cover's slack. ε is the kernel's measured Field error.

## Results

### The Field side

<div data-fig="field"></div>

Every kernel the interpreter compiles matches the Python reference to Float32 rounding ([[fig:field]]): all {{n_L}} literature filters, written separately from their equations, and every compiled generated kernel. Kernels over the interpreter's 128-register cap ({{over_G}} of G, {{over_B}} of B; a dense update of size n costs about 3n² registers) are scored on the Python model. Three loops with a nonlinearity outside the library (exp(s) − 1, s/(1 + s²), and a clip with an offset) fail the linearity gate and leave scope; the same loop with `clamp` passes at 3.7e-8.

### Linearise and hope is unsound

On G, the oracle finds a witness for {{wit_G}} kernels ({{grows_G}} grow, {{sticks_G}} stick). Today's frozen check calls {{b1a_G}} of them safe at the default knobs, and {{b1b_G}} even on a full grid. Report 2's certifier on the linearised kernel certifies {{b4_G}} of them. Both fail the same way, at a fixed knob setting: the saturator sits on a path that stabilises the loop. Small signals see slope 1 and decay; large signals see slope near 0, the stabilising path is cut, and the loop that remains is unstable ([[fig:windup]]). That is integrator windup, found by the oracle without being looked for. A state 20 to 25 dB over full scale is enough, which a hot input into a resonant loop reaches. T1 rejects both kernels, because δ = 0 is one of its corners. On B, linearise-and-hope certifies {{b4_B}} kernels with a witness.

### Soundness and coverage (H1, H3)

<div data-fig="verdicts"></div>

H1 holds on all three sets: of {{n_witness}} kernels with a witness, the pass certifies {{false_certs}} ([[fig:verdicts]]). H3 passes on G ({{h3_G_frac}}, {{h3_G}}) and **fails on the blind set** ({{h3_B_frac}}, {{h3_B}}), so the coverage claim is not confirmed. Every G and B certificate came from T1: T3's exact corners apply to almost no generated kernel, and where T3 passes on a grid it is not counted. On the literature set the pass certifies the one-pole and the clipped state-variable filter, the two filters without a witness; every other literature filter can stick inside its declared box, and none of those is certified. T1 takes {{t_median_G}} per kernel at the median on G and {{t_p95_G}} at p95, in Python on an M2.

Why B falls short (exploratory, after B was scored). B allows bigger kernels (up to 8 states) and up to {{mmax_B}} saturators. Coverage drops with the saturator count: on B, {{h3_m1_B}} kernels with one saturator are certified, against {{h3_mmax_B}} with {{mmax_B}}. The unproved kernels split two ways. Some sit at the edge, where the cover does not close: the 18 such G kernels have a sampled radius of 0.94 to 0.98, and a tenfold cell budget closed none of them. The larger group on B is the sector certificate's blind spot. It must hold for every gain in each saturator's sector, chosen independently and held forever, so it also checks corners where one saturator passes signal at full gain while another is cut to zero. For {{corner_B}} B kernels whose LMI is infeasible outright ({{corner_G}} on G), such a corner is unstable at a fixed knob setting, so no sector-only certificate can exist; yet the oracle finds no witness, because a real tanh cannot hold those gains apart. More saturators mean more such corners. Closing that gap needs a certificate that knows the saturator's shape, which is T3, and T3 does not yet count on generated kernels.

### The tanh Moog ladder (H2)

<div data-fig="moog"></div>

H2 fails as registered. Over the port's full declared cutoff range, up to g = 0.785 (fs/8), T3 certifies k ≤ {{moog_t3_full}} for the whole box under any modulation (the figure's per-ceiling sweep gives {{moog_t3_row}} at that ceiling), short of Hélie's 5/3 ([[fig:moog]]). The cause is the port, not the method: the oracle finds the explicit filter sticking from k = {{moog_wit_full}} at fs/8, below 5/3, so no sound certificate can reach 5/3 over that range. For cutoff ceilings up to g = {{helie_ceiling}}, T3 certifies 5/3 automatically, from the kernel's matrices alone. The certificate it finds is Hélie-like: P comes out near zero and every qᵢ is positive, so V is essentially 2Σ qᵢ ln cosh(yᵢ), a sum of per-stage energies. At low cutoff a gap remains (k ≤ {{moog_t3_low}} certified against a first witness at {{moog_wit_low}}); it is the same conservatism as a single quadratic energy on the linear ladder, so T3 loses little on top of Report 2.

### Forgetting the past (H4) and Float32

H4 passes on both sets: {{h4_G_frac}} certified G kernels and {{h4_B_frac}} certified B kernels also get a convergence certificate, and none fails the two-start test ({{two_fail_G}} and {{two_fail_B}} failures). The high rate is partly structural. For tanh, soft, clamp and abs, slope and sector ranges are equal, so the convergence certificate is the same problem as the stability one; only kernels with a sin needed a separate run, because sin's slope range [−1, 1] is wider than its sector. On G all {{g2own_G}} such runs certified; on B, {{g2own_B}} of {{sin_g1_B}} did, and the other six are the B kernels without a convergence certificate. A convergence certificate means the filter's output depends on its input and modulation, not on where it started: no hysteresis, no hidden state from a click an hour ago.

<div data-fig="float32"></div>

The Float32 bound holds on {{f32_G}} G certificates and {{f32_B}} B certificates ([[fig:float32]]). The rest are "not proved in Float32", not unstable: their covers close with a slack of at most {{f32_fail_slack}}, too little for the bound, a worst case over every entry scaled by the condition number of P. Report 2 measured the interpreter's P-norm directly at check points; this study used the bound only.

## Discussion

The step from linear to saturating loops costs little where the saturator acts like a gain in a known range, and it catches a failure the linear view hides. Sector certificates are enough for most small generated kernels, but the blind set shows where they run out: with more saturators, the sector box contains gain combinations no real saturator can hold, and some of them are unstable. They also cannot prove a ladder. Ladders need an energy function that knows the saturator's integral, which is what Hélie wrote by hand, and the pass finds it automatically for the Moog. The useful product finding is the windup case: a check that linearises saturators, which is what a careful author would try first, calls blowing-up kernels safe, and so does today's frozen check. The next step is the one both shortfalls point to: give T3, the shape-aware certificate, a cell cover so it counts on any kernel, then rescore a fresh blind set.

## Threats to validity

- **What "certified" means.** As in Report 2, T1 is exact at its LMI points and its float64 re-check, and the cover between points rests on a measured margin, not an interval bound. T3 on the Moog is exact per cutoff cell up to a numerical tolerance of 1e-9 that exists because the energy has exact null directions.
- **T3 barely counts on generated kernels.** Its exact corners need knob-affine terms and a knob-free C, which almost no generated kernel has. A cell cover for T3's form would let it count; it was not built.
- **The oracle is one-sided.** A kernel that can stick or grow in a way the oracle misses sits in H3's denominator. Its witnesses are knob-switching sequences with zero input; a failure that needs a particular input signal is not searched for.
- **The blind-set analysis came after the score.** The explanation for B's shortfall (sector corners, saturator count) was found after B was scored and is exploratory; no verdict was changed by it.
- **H4's pass is mostly inherited.** Where slope equals sector, G2 is G1's problem. The two-start test is the independent evidence.
- **Who wrote the kernels.** L was ported by the author; G and B come from one generator the author wrote. No real user kernels or shipped presets were scored.
- **Matrices from Python.** {{over_G}} G and {{over_B}} B kernels exceed Field's register cap and were scored on the Python model, with the set's largest measured error in their Float32 bound.
- **Shapes outside the library.** A saturator written any other way is out of scope; the gate rejects it, so this costs coverage, not soundness.

## Related work

Hélie's energy argument for the Moog ladder [@helie2011] is the hand proof H2 is scored against. Port-Hamiltonian schemes keep a circuit's passivity through discretisation [@danish2021]; they are built per circuit, and for the Moog the argument does not extend to time-varying resonance. Chowdhury's nonlinear biquads are stable by construction when slopes are at most 1 [@chowdhury2020]. Time-varying linear filters, and the quadratic Lyapunov machinery this study extends, are covered in Report 2 and its references [@laroche2007; @mcclellan2026; @infinite_report2]. Lur'e systems with sector and slope-restricted nonlinearities are standard in control, as LMIs [@boyd1994]. We found no compiler or audio tool that certifies user-written saturating loops; absence in a search is not proof of absence.

## Conclusion

A compiler can certify that a modulated Field kernel with saturators in its loop neither blows up nor sticks, from its code, soundly in every case we could test: no false certificate on {{n_kernels}} kernels. It does not yet certify enough of them; coverage held on the generated set and fell to {{h3_B}} on the blind set, below the bar we set. The shortcut of linearising the saturators is unsound, and the failure is ordinary windup. For ladders, sector bounds are not enough, and a Lur'e-Lyapunov energy reaches the published hand proof where the filter itself allows it. The pass is a Python research tool; putting it in Infinite's compiler is a separate decision.

## Reproducibility

The study is in the Infinite repository under `docs/research/saturating-loops/`. `scripts/kick-the-tires.sh` reruns the locked code on a slice of G and B, re-dumps the literature filters from a dev build and runs the gate's negative test, comparing every verdict with the committed data. `scripts/run_all.sh` reruns every phase into `data-rerun/` (several hours on an M2). `ARTIFACT.md` links each claim to its command, data file and tolerance. Raw data is never edited; every figure and table here is generated from it.

<details class="rn-drawer"><summary>Appendix A. Rig bugs, and changes from the registered method</summary>
<p><strong>Method (owner, after phase 0).</strong> Sector-only certificates provably fail on two-stage tanh cascades, so the Lur'e-Lyapunov certificate (T3) became the main G1 method, with T1 kept as its baseline. On G and B every certificate came from T1.</p>
<p><strong>H2 scoring range (owner, after phase 0, numbers seen).</strong> H2 is scored over the Moog port's full declared cutoff range, up to fs/8. The 0.524 crossover is reported as a finding and not used to pass H2.</p>
<p><strong>Oracle, phase 0.</strong> The first stick search settled for 8192 samples from 0.01 and tried five repeat counts; it missed witnesses by up to 0.32 in k. Settling for 2¹⁷ samples from 0.3, with repeat counts from the orbit's rational rotation, fixed it before any score.</p>
<p><strong>Cell budget.</strong> Report 2's cover ran with 20 000 cells, not 200 000, to fit the M2. The 18 G covers that did not close were rerun at 200 000; none closed.</p>
<p><strong>T1 corners.</strong> Every corner, δ = 0 included, is held to strict decrease, which is more conservative than the non-strict rule in the registered method.</p>
</details>
