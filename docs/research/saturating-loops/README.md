---
title: "Saturating loops: stability certificates for modulated filters with saturation in the feedback"
slug: "saturating-loops"
status: "phase-0-framing"
owner: "namansoni"
date: "2026-11-23"
prior_art: "docs/prior-art/2026-11-23-saturating-loops.md"
commit_sha: "pending-measurement"
---

# Saturating loops

**Draft for sign-off. Sections 1-7 and decisions D1-D5 lock when the owner signs off; nothing is built or
run before that.** Sections 8-12 are filled after sign-off. Follows `docs/research/stable-modulation`
(Technical Report 2, validated 2026-11-23), which proved linear filters only; this is candidate study C1
from its section 12.

## 1. Goal

| # | Goal | Output |
|---|---|---|
| G1 | A compiler pass that, from a Field kernel's code alone, **proves** that a filter with saturators inside its feedback loop goes quiet when its input stops, under *any* modulation of its knobs inside their ranges, or says it can't | A certificate (P and the saturator multipliers) per kernel, or "no proof" with the reason |
| G2 | A stronger, optional proof: the filter **forgets its past** (two runs with the same input and different starting states converge), so it can't hang in a stuck oscillation or turn chaotic while it plays | A second certificate per kernel, or "no proof" |
| G3 | Check the pass against the published hand proofs for the nonlinear Moog ladder (Hélie, DAFx-11) | Our automatic range vs the paper's, plus where our own oracle finds the first failure |
| G4 | Publish the result the same way whether it is positive or null | README 1-12, scripts, data, a Web Lab Note (Technical Report 3) |

Product line this earns, if it holds: *"saturate and modulate anything, it never sticks or explodes"*, the
analog-style half of the promise Report 2 made for linear filters.

## 2. Problem

**What fails.** Report 2 certifies filters that are linear in their state. Real synth filters put a
saturator in the loop (`tanh` per ladder stage, a clipper on the resonance feedback) and the linear proof
no longer applies. Two new failures appear:

| Failure | What the user hears | Possible in a linear filter? |
|---|---|---|
| **Stuck oscillation**: fast modulation pumps energy in, the saturator stops it growing, and it rings on after the input stops | A whistle or buzz that won't die until the knob moves | No: linear filters either decay or blow up |
| **Unbounded growth**: a loop the saturator doesn't cover (a clipper on one path, a linear integrator on another) | A blow-up to NaN, like Report 2's trap | Yes |

The first is the new one. Report 2's own trap shows it: in the small-signal range a saturator has slope 1,
so the linear Moog's switching instability (k = 2.8856, Report 2) grows until the saturator caps it, then
holds. Saturation turns a blow-up into a stuck oscillation. It is louder than silence and quieter than NaN,
and no check in Infinite notices it.

**Infinite today.** The only check is the frozen-knob one (`loopRadius`, `src/core/field/AliasAnalysis.cpp`):
the spectral radius of the mean state Jacobian at default params. It knows nothing about modulation, and for
a saturating loop it only sees the slope at one operating point. Field users and agents write their own
saturators.

**Saturators in Field.** Field has no `tanh` at sample rate. Users build saturators from `clamp`, `min` /
`max`, `abs` (for example `x / (1 + abs(x))`), `exp` (a written-out `tanh`) and `sin`. So the pass must
recognise saturator *shapes*, not one op name.

**Prior art** (full table: `docs/prior-art/2026-11-23-saturating-loops.md`):

| Source | What it shows | Gap this study fills |
|---|---|---|
| Hélie, DAFx-11 | Continuous-time tanh Moog: stable under **time-varying** cutoff and feedback for r < 5/12 (k < 5/3) by its capacitor energy; the full r < 1 only for fixed r. The bilinear-transform simulation is not guaranteed dissipative past r = 5/12 | Continuous time, by hand, one circuit; the discrete code isn't proved |
| Danish, Bilbao, Ducceschi, DAFx20in21 | Port-Hamiltonian Korg35 and Moog with zero-input-stable discrete schemes; Korg35 holds under time-varying parameters, Moog does not extend (its energy depends on the resonance) | Per circuit, by hand; Moog under modulation open |
| Chowdhury, DAFx2020 | Two nonlinear biquad structures, Lyapunov-stable when every nonlinearity has slope ≤ 1 | Fixed structures; modulation not proved |
| Control theory: discrete Lur'e systems (sector and slope bounds as LMIs), Zames-Falb multipliers, LPV Lyapunov functions | The maths for x⁺ = A(θ)x + B(θ)φ(Cx) exists | Never joined to an audio language's compiler or to user code |

Not found: any compiler, DSL or audio tool that certifies a user-written saturating filter under
modulation. Absence in a search is not proof of absence.

## 3. Scope

| In | Out |
|---|---|
| Sample-domain Field kernels (Field Effect, Field Synth) with `state` cells | Pixel / element / frame domains |
| Loops of the form x⁺ = A(θ)x + B(θ)φ(C(θ)x + D(θ)·in) + E(θ)·in: linear in the state except for **scalar saturators** φᵢ, each applied to one linear combination | Saturators of two signals at once (`max(a, b)` of two states), products of states (ring mod, signal-dependent coefficients) |
| Saturator shapes from a fixed library with known sector and slope bounds (D2): `clamp`, `min`/`max` against a constant, `x/(1+abs(x))`, written-out `tanh` via `exp`, `sin` (slope ≥ −1, sector [−0.22, 1]), `abs` (sector [−1, 1]) | Unknown shapes: reported as "unrecognised saturator", never certified |
| Zero-input stability (G1) and convergence under input (G2), at any modulation speed, θ inside declared min/max | Bounded-input output-level claims (how loud it gets); the slew-limited extension (Report 2's H4 failed as registered; it is its own follow-up) |
| State dimension n ≤ 8, up to 4 saturators per kernel | `delay(x, N)` lines in the loop |
| Report-only analysis (prints a certificate, changes nothing at runtime) | Safe ranges on knobs, auto-clamping, an in-app solver: each needs owner approval later |
| Research solver in Python, offline, reusing Report 2's certifier, cover and dump | Shipped C++ filters (`FilterModels.h` etc.) |

## 4. Existing constraints

| Constraint | Consequence |
|---|---|
| M2, 8 GB | Small LMIs (n + m ≤ 12, a few hundred points); CPU is enough |
| Research venv exists (`~/.venvs/stable-modulation`: cvxpy 1.9.3, Clarabel) | Reused as is; no app dependency |
| Field is explicit: no implicit solves at sample rate | Literature filters are ported in explicit (or one-step linearised) form; their discrete stability can differ from the papers' continuous results. Reported, not hidden |
| Field has no `tanh`; 128-register cap in the interpreter | Saturators are written from `exp`, `abs`, `clamp`; dense n ≥ 5 kernels with several saturators may not run on the interpreter (Report 2: they fell back to Python matrices) |
| Runtime is Float32; certificates in Float64 | Same decay margin and Float32 re-check as Report 2 |
| Report 2's dump (`INFINITE_FIELDSTABDUMP`) extracts A(θ) by probing the interpreter, and gates out nonlinear kernels | It must learn to cut the loop at each saturator and probe A, B, C, D separately: test code only, one build |
| Clean room | Papers and docs only; the papers in the prior-art file are the reference values |
| Research standard | Self-written corpus is calibration only; "confirmed" needs a blind set scored once |

**Threats known before data**
- **Sector proofs are conservative.** A sector bound treats `tanh` as "any function between 0 and the
  identity", so it may fail on filters that are fine. That costs coverage, never soundness.
- **Witnesses are numerical.** For a nonlinear loop, a stuck oscillation is shown by a periodic orbit found
  numerically. D1 requires it to be refined to a tiny residual, so it is a near-proof, not a theorem.
- **Shape recognition.** A saturator written in an odd way is reported as unrecognised. Coverage counts it
  as a miss.

## 5. Hypothesis

A Field kernel whose loop is linear except for scalar saturators is a **Lur'e system** with time-varying
linear parts. If one quadratic function V(x) = xᵀPx, combined with a non-negative multiplier per saturator
(the S-procedure on its sector), decreases at every θ in the knob box, the kernel goes quiet under any
modulation (G1). If the same holds for the difference of two trajectories using the saturators' **slope**
bounds, it forgets its past (G2). Both are small LMIs on the same grid-and-cover machinery as Report 2.

| ID | Hypothesis | Metric and bar | Kill criterion | Held-out validation |
|---|---|---|---|---|
| H1 | **Sound**: the pass never certifies a kernel that can stick or blow up | 0 G1 certificates on kernels with a witness | Any certified kernel with a witness (after a rig-bug check, recorded) | Sets L, G and blind set B |
| H2 | **Matches or beats the hand proof** for the tanh Moog ladder under modulation | Certified any-speed k range ≥ Hélie's time-varying range, k < 5/3 (r < 5/12), on the explicit Field Moog with a saturator per stage; and never past the oracle's first witness | Certified range < 5/3, or any certificate past a witness | The filter is ported from published equations by a script, not tuned |
| H3 | **Covers** most stable-looking saturating kernels | Certifies ≥ 70% of G kernels that have no witness (D4) | < 70% | Blind set B, scored once |
| H4 | **Forgets its past** (G2) for most of those, soundly | Convergence certificates on ≥ 50% of G1-certified G kernels, and 0 of them fail the two-start oracle | < 50%, or any failure | Blind set B |
| H5 | **Cheap enough to run at compile time** (scored only if H1-H3 pass and an in-app port is approved) | Median ≤ 50 ms, p95 ≤ 250 ms per kernel on the M2, C++ solver | Median > 50 ms | All of L, G, B |

## 6. Baseline

Recorded once, before the pass is written, at a pinned commit. Not re-measured later.

**Corpora**

| Set | What | Size | Role |
|---|---|---|---|
| L | Literature saturating filters in Field from published equations: tanh Moog ladder (saturator per stage, explicit), Moog with one saturator on the feedback only, diode ladder with saturation, Chowdhury's two nonlinear biquads, Rossum-style saturating 2-pole, SVF with clipped integrators, saturating one-pole, Korg35-style 2-pole with feedback saturation, a clipped allpass phaser loop | ~12 | Calibration + H2 |
| G | Generated: Report 2's linear generator (n = 1..6, coefficients as affine / `sin` / `pow` / `exp` / `Select` of 1-3 params) plus 1-3 saturators from the shape library placed on loop paths, half built so the saturator-free version is frozen-stable everywhere | ≥ 200, fixed seed | Calibration + H3, H4 |
| B | Blind: same generator, different seed and the settings in D5, generated after the lock, scored once | ≥ 200 | Held-out for H1, H3, H4 |

**Oracle (ground truth for "not safe").** For each kernel, with zero input, search for a **witness**: a
periodic switching sequence of knob settings (length ≤ 8, from box vertices and a grid) and a starting state
whose trajectory either
- **grows**: ‖x‖ passes 10⁶ × ‖x₀‖ within 2¹⁶ samples (unbounded growth), or
- **sticks**: a periodic orbit of the switched map, refined by Newton's method to a residual below 1e-10,
  with amplitude above 1e-3 (a stuck oscillation; it never decays).

Starting points come from Report 2's linear witnesses (the saturators replaced by their small-signal slope)
and from 200 random starts per kernel. For G2, a **two-start witness** is two trajectories under the same
input and modulation whose difference stays above 1e-6 after 2¹⁶ samples.

**Baseline numbers recorded**

| # | Number | Why |
|---|---|---|
| B1 | False-safe rate of today's frozen check (`loopRadius < 1` at default params and at every grid point) on kernels with a witness | The number the pass must take to 0 |
| B2 | Count of L and G kernels with a witness / without, split by grows / sticks | Sets the denominators for H1, H3 |
| B3 | For the tanh Moog ladder: the oracle's smallest k with a witness. Expected at or just below Report 2's linear 2.8856, because a saturator has slope 1 near zero | Checks the oracle against a known result before it judges anything |
| B4 | "Linearise and hope": Report 2's certifier run on each kernel with every saturator replaced by the identity. How often that certificate is wrong (the kernel has a witness), and how often it misses | Shows whether the new pass is needed at all, or the linear one is already enough |

## 7. Success metric to be achieved

| Hypothesis | Bar | Verdict rule |
|---|---|---|
| H1 | 0 false certificates on L + G + B | Any failure stops the study (after a rig-bug check) |
| H2 | Certified k range ≥ 5/3 on the tanh Moog, none past a witness | Must pass |
| H3 | ≥ 70% of no-witness G kernels certified, then the same on B | Confirmed only if B also passes |
| H4 | ≥ 50% of G1-certified kernels get a convergence certificate, 0 two-start failures | B must pass too |
| H5 | Median ≤ 50 ms, p95 ≤ 250 ms | Only after an in-app port is approved |

The study ships as research when H1-H3 pass. H4 is the stronger claim; a failure there is reported, and the
H1-H3 claim still stands.

**Deliverables**
- `data/`: per kernel: saturators found and their bounds, witness (grows / sticks / none), frozen-check
  verdict, B4 verdict, G1 and G2 certificates (P, multipliers, margin, cover), timing.
- `scripts/`: generator, oracle, certifiers, table builder, `kick-the-tires.sh` under 5 minutes.
- The H2 table against Hélie, and a Web Lab Note (Technical Report 3).

## Decisions needed at sign-off

| ID | Decision | Recommendation |
|---|---|---|
| D1 | What counts as "not safe" | A witness as in section 6: growth past 10⁶ × ‖x₀‖, or a Newton-refined periodic orbit (residual < 1e-10, amplitude > 1e-3). Plain long simulation only as a sanity check |
| D2 | Which saturators are recognised | The fixed shape library in section 3, each with a sector and slope bound proved on paper and checked numerically on a log grid out to 10⁶. Anything else is "unrecognised saturator" and never certified |
| D3 | Which multiplier | Start with a diagonal sector multiplier per saturator (circle-criterion style, the simplest LMI). Add slope-restricted (Lur'e-Lyapunov / Zames-Falb-type) terms only for G2, and as an ablation on G1 if H3 misses its bar by under 15 points |
| D4 | H3 bar | 70%, below Report 2's 80%, because sector bounds are conservative. Say so in the paper; don't move it later |
| D5 | Blind set B | Seed 20261124, n up to 8 (G stops at 6), up to 4 saturators (G up to 3), 30% of terms with `Select` |

## 8. Best and optimised methodology
*(After sign-off.)*

## 9. Proposed solution trajectories
*(After sign-off.)*

## 10. Benchmarking against the baseline
*(After measurement.)*

## 11. Critique
*(After measurement.)*

## 12. Next steps
*(After measurement.)*
