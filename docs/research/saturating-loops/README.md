---
title: "Saturating loops: stability certificates for modulated filters with saturation in the feedback"
slug: "saturating-loops"
status: "pre-registered"
owner: "namansoni"
date: "2026-11-23"
prior_art: "docs/prior-art/2026-11-23-saturating-loops.md"
commit_sha: "pending-measurement"
---

# Saturating loops

**Signed off by the owner on 2026-11-23, as written, with D1-D5 as recommended. Sections 1-7 are locked
by this commit.** Sections 8-12 are filled after sign-off. Follows `docs/research/stable-modulation`
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
| State dimension n ≤ 8, up to 4 saturators per kernel (up to 5 for L; amended 2026-11-23, see section 8) | `delay(x, N)` lines in the loop |
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
| D3 | Which multiplier | Start with a diagonal sector multiplier per saturator (circle-criterion style, the simplest LMI). Add slope-restricted (Lur'e-Lyapunov / Zames-Falb-type) terms only for G2, and as an ablation on G1 if H3 misses its bar by under 15 points. **Amended by the owner 2026-11-23 after phase 0:** the Lur'e-Lyapunov term (T3) is the main G1 method; sector-only (T1) is kept as the baseline it is compared with |
| D4 | H3 bar | 70%, below Report 2's 80%, because sector bounds are conservative. Say so in the paper; don't move it later |
| D5 | Blind set B | Seed 20261124, n up to 8 (G stops at 6), up to 4 saturators (G up to 3), 30% of terms with `Select` |

## 8. Best and optimised methodology

**Pipeline.** Field kernel → saturators found → A(θ), B(θ), C(θ) → vertex matrices → Report 2's certifier
→ certificate or "no proof".

**Key step: the sector set is a polytope.** With zero input the loop is x⁺ = A x + B w, wᵢ = φᵢ(yᵢ),
y = C x. A saturator in sector [αᵢ, βᵢ] means wᵢ = δᵢ yᵢ for some δᵢ ∈ [αᵢ, βᵢ] that can change every
sample. So every trajectory is also a trajectory of the switched linear system x⁺ = (A + B Δ C) x, where
Δ = diag(δ). For a quadratic V, V(x⁺) is convex in Δ, so the worst case is at a corner of the δ box. G1 is
therefore Report 2's common-P certificate over the **2ᵐ vertex matrices** A(θ) + B(θ) Δᵥ C(θ) at every
grid θ. This is exact for "quadratic V, sector bounds only", the class D3 asks for first. D3's diagonal
multiplier LMI is the same class written as an S-procedure; it is run as a cross-check (T2) and can never
prove more.

| Step | How | Where |
|---|---|---|
| Find saturators | Match the shape library (table below) in the kernel's IR on every path inside the state loop. Scaling is folded into B and C (`a·φ(b·y)` → φ with B·a, C·b). A non-linear op on a loop path that isn't in the library makes the kernel "unrecognised saturator", reported, never certified | Phases 0-1: the generator emits matrices and Field code together. Phase 2: C++ in the dump self-test (test code only) |
| Extract A, B, C | Extend `INFINITE_FIELDSTABDUMP`: each recognised saturator is cut, its output replaced by an injected value wᵢ and its argument yᵢ recorded. One sample step per unit vector of x (w = 0) gives A and C; one per unit vector of w (x = 0) gives B. A **linearity gate** on the cut kernel (Report 2's, 8 random pairs, 1e-5) catches anything the matcher missed | Phase 2, one build |
| Vertex matrices | Mᵥ(θ) = A(θ) + B(θ) Δᵥ C(θ) for the 2ᵐ corners of ∏[αᵢ, βᵢ]; m ≤ 5 gives ≤ 32 per grid point | Python |
| G1 certificate | Report 2's LMI, cutting planes and cell cover, unchanged, on every (θ, v) pair. The cover is per vertex: Mᵥ(θ) is a fixed linear map of (A, B, C), so the corner blend bound holds for each v | Python |
| Saturators that reach slope 0 | `tanh`, `x/(1+abs(x))` and `clamp` have αᵢ = 0. At δᵢ = 0 that saturator passes nothing, and a stage that relies on it for damping holds its state: strict decrease is impossible there. Rule: the δᵢ = 0 vertices get the **non-strict** inequality ‖Mᵥ‖_P ≤ 1, every other vertex the strict one. Then V never grows, every trajectory is bounded, so each δᵢ stays above a floor δmin > 0 along it. The convex blend then puts weight ≥ ∏ δmin/βᵢ on the strict all-upper corner, and V decays: global asymptotic stability from any start | Python |
| If the non-strict vertices can't close the cover | The cover's margin e > 0 can push a vertex that sits exactly at 1 over it. Fallback, reported separately: an **amplitude-bounded** certificate. Starting with ‖x‖ ≤ R (R = 10⁴, +80 dB over full scale), the state stays in V ≤ λmax(P)·R², so |yᵢ| ≤ Yᵢ = ‖cᵢ‖·R·√κ(P) and the sector floor is αᵢ = φᵢ(Yᵢ)/Yᵢ > 0. Every vertex is then strict. Y depends on P, so P and the floor are iterated until they agree (at most 5 rounds). It counts for H3 only if the owner approves (see the amendment below) | Python; found in phase 0 on the Moog |
| G2 certificate (H4) | Two trajectories under the same input and modulation, difference e: e⁺ = (A + B S C) e with Sᵢ in the **slope** range of φᵢ (mean-value theorem). Same vertex certificate over the slope box. Slopes that reach 0 get the same non-strict rule. The floor comes from the reachable set for inputs ‖in‖ ≤ U = 4 (+12 dBFS), bounded with G1's P. `clamp` has slope exactly 0 when clipped, so it gets no floor: its G2 holds only if its slope-0 vertices are strict on their own | Python |
| Largest safe range (H2) | Report 2's bisection on k, 4 decimals, cutoff over its full range | Python |
| Margins | t, κ(P), largest cell bound, δmin; Float32 re-check of the extracted matrices, as in Report 2 | Python |

**Shape library (D2).** Each bound is proved on paper in `scripts/shapes.md` and checked numerically on a
log grid out to 10⁶ before use.

| Shape in Field | φ | Sector [α, β] | Slope range |
|---|---|---|---|
| `clamp(y, -c, c)`, c > 0 | hard clip | [0, 1] | [0, 1] |
| `min(y, c)` / `max(y, -c)`, c > 0 | one-sided clip | [0, 1] | [0, 1] |
| `y / (1 + abs(y))` | soft clip | [0, 1] | (0, 1] |
| `(exp(2*y) - 1) / (exp(2*y) + 1)` and its rearrangements | tanh | [0, 1] | (0, 1] |
| `sin(y)` | fold | [−0.2172, 1] | [−1, 1] |
| `abs(y)` | rectifier | [−1, 1] | [−1, 1] |

`min`/`max` against a constant of the wrong sign don't pass through 0, so they aren't sector-bounded:
unrecognised.

**Oracle.** As in section 6 (D1). Two additions to make "sticks" a real check:
- A periodic orbit of the switched map (period p ≤ 8) is found by Newton's method on x = F_σ(x), with F_σ
  the composed sample steps for the switching sequence σ. A Newton step that won't reduce the residual
  below 1e-10 in 50 iterations is no witness.
- The orbit's amplitude must exceed 1e-3, so the trivial orbit x = 0 can't count. Its stability (attracting
  or not) is recorded but not required: an unstable stuck orbit still means V can't decrease everywhere.

**Literature filters (L).** Ported by script from the published equations, never tuned. The tanh Moog is
used in two explicit forms: forward Euler, yᵢ⁺ = yᵢ + g(tanh(yᵢ₋₁) − tanh(yᵢ)), y₀ = in − k·y₄; and the
one-pole-per-stage form with g/(1+g), tanh taken at the previous state. Both have 5 saturators (the input
and each stage). Our results are on these discrete forms; Hélie's 5/3 is for the continuous circuit. That
difference is reported, not hidden.

**Amendment (owner, 2026-11-23).** Section 3 said "up to 4 saturators per kernel". Hélie's tanh Moog, the
H2 filter, needs 5, so L may have up to 5 saturators. G (≤ 3) and B (≤ 4) are unchanged.

**Phase 0 findings on the tanh Moog** (`scripts/phase0_moog.py`, `data/phase0-moog.json`; g from 1e-4 to the ceiling, k from 0):

| Cutoff ceiling g | Frozen limit k | First linear switching witness k | Linear common-P certificate k | tanh, sector-only (T1) |
|---|---|---|---|---|
| 0.05 | 3.632 | 3.632 | 2.737 | no proof at k = 0 |
| 0.2 | 2.810 | 2.810 | 2.343 | no proof at k = 0 |
| 0.4 | 2.096 | 2.096 | 1.908 | no proof at k = 0 |
| 0.785 (fs/8) | 1.288 | 1.285 | 1.274 | no proof at k = 0 |

- **T1 cannot work on ladders.** Sector-only quadratic certificates fail on a two-stage tanh cascade at one
  fixed cutoff, with no feedback at all. The proof: with stage 2's saturator at slope 0 and stage 1's at 1,
  V non-increasing forces P₁₂ = P₂₂; with stage 1 at slope 0 it forces P₁₂ = 0; so P₂₂ = 0, impossible.
  The +80 dB fallback doesn't rescue it: with |y| ≤ 10 it proves k < 0.04, with |y| ≤ 100 nothing.
  Hélie's 5/12 proof needs more than a sector bound per saturator. This makes the Lur'e-Lyapunov term (T3,
  V = xᵀPx + Σ qᵢ ∫φᵢ) necessary for G1, not an ablation. The owner made T3 the main method (D3 amended, 2026-11-23).
- **The explicit Moog's own limit falls with cutoff.** Frozen at fs/8 it is unstable from k = 1.288, below
  Hélie's 5/3, so H2's bar can't be met over the full cutoff range by any sound method. Raised with the
  owner after D3.

**Phase 0 findings, T3 on the tanh Moog** (`scripts/t3cert.py`, `scripts/phase0_t3.py`, `data/phase0-t3.json`).
T3 as built: V = xᵀPx + 2Σ qᵢ ∫φᵢ over the stage saturators, with sector, slope, cross-pair slope and
ratio bounds (φ(c·y) lies between φ(y) and c·φ(y) for odd shapes concave on y > 0) as S-procedure terms.
P and q are shared over the knob box; the multipliers are per cutoff cell (24 log cells). Inside a cell the
form is convex in g and linear in k, so the cell's corners are exact: no Lipschitz margin.

| Cutoff ceiling g | T3, tanh Moog, any speed | Linear Moog, common P (Report 2 method) | Frozen limit |
|---|---|---|---|
| 0.05 | 2.652 | 2.737 | 3.632 |
| 0.2 | 2.332 | 2.343 | 2.810 |
| 0.4 | 1.907 | 1.908 | 2.096 |
| 0.785 (fs/8) | 1.154 | 1.274 | 1.288 |

- **Hélie's 5/3 is certified for cutoff ceilings up to g = 0.524.** Above it the explicit filter's own
  limit falls below 5/3.
- **H2 scoring range (owner, 2026-11-23, decided after these numbers were seen):** H2 is scored over the
  port's full declared cutoff range, g up to 0.785 (fs/8). On this phase-0 run that is 1.154 < 5/3, so H2
  is expected to fail as registered; the 0.524 crossover is reported as a finding, not used to pass H2.
- **The certificate is Hélie-like.** P comes out ≈ 0 (min eigenvalue < 1e-8) and every qᵢ > 0, so V is
  essentially 2Σ qᵢ ln cosh(yᵢ). It grows without bound, so the proof is global.
- **Soundness checks at every certified k:** 200 random-switching runs of the real nonlinear filter from
  starts up to 100 all decay (worst final/initial ratio ≤ 8e-23); the stick search finds nothing; a dense
  re-check inside each cell stays ≤ 7.5e-10 relative, inside the declared tolerance of 1e-9. That
  tolerance exists because the form has exact null directions; it is numerical, not a proof.

**Phase 0, baseline B3** (`scripts/phase0_b3.py`, `data/phase0-b3.json`): the oracle's smallest k with a
stuck-oscillation witness on the explicit tanh Moog. A witness is a knob sequence inside the box and a
Newton-refined periodic orbit of the real filter with zero input. One knob value in the sequence is tuned
inside its range so the orbit closes after N repeats; N comes from the best rational approximations of
the settled orbit's rotation (denominators up to 4096). *Changed from the first try:* settling for 8 192
samples from 0.01 and N from 5 multiples missed witnesses by up to 0.32 in k; settling for 2¹⁷ samples from
0.3 with rational N fixed it.

| Cutoff ceiling g | T3 certified | Oracle's first witness | Frozen limit | Witness orbit |
|---|---|---|---|---|
| 0.05 | 2.652 | 3.632 | 3.632 | 740 repeats, residual 7e-12, amplitude 0.005 |
| 0.2 | 2.332 | 2.810 | 2.810 | 128 repeats, residual 7e-11, amplitude 0.011 |
| 0.4 | 1.907 | 2.096 | 2.096 | 959 repeats, residual 6e-11, amplitude 0.006 |
| 0.785 | 1.154 | 1.289 | 1.288 | 149 repeats, residual 1e-14, amplitude 0.034 |

- The oracle agrees with the known answer (the frozen limit, where the Moog must self-oscillate) to 0.001,
  so it is fit to judge certificates. No witness exists at any T3-certified k.
- Saturation turns every linear blow-up into a stuck oscillation, as section 2 predicted.
- The open gap (2.652 vs 3.632 at low cutoff) is the same common-P conservatism as Report 2's linear Moog
  (2.737 there); T3 loses little on top of it.

**Phase 0, L and B2** (`scripts/lit.py`, `scripts/phase0_L.py`, `scripts/phase0_L_ceil.py`,
`data/phase0-L.json`, about 8 min). Eleven filters from published equations, zero input, each with its
declared knob box. B4 = Report 2's vertex certificate with every saturator replaced by the identity; T1 =
sector vertices; T3 = Lur'e-Lyapunov, the main G1 method. The oracle is the general form of B3's:
Report 2's linear witness search on the small-signal matrix plus the box corners, 0.3, 30 and 200 random
starts (amplitude 1e-2 to 1e2) run for 2¹⁶ samples, then a Newton orbit (locked when it rotates).

| Filter | Saturators | Box | Frozen check | B4 | T1 | T3 | Witness |
|---|---|---|---|---|---|---|---|
| moog-tanh | tanh × 5 | g ≤ 0.785, k ≤ 4 | unsafe | no proof | no proof | no proof | sticks |
| moog-soft | soft × 5 | same | unsafe | no proof | no proof | no proof | sticks |
| moog-fbsat | tanh on feedback | same | unsafe | no proof | no proof | no proof | sticks |
| diode-fbsat | tanh on feedback | g ≤ 0.4, k ≤ 16 | unsafe | no proof | no proof | no proof | sticks |
| korg35-sat | tanh on feedback | g ≤ 0.5, k ≤ 2.5 | unsafe | no proof | no proof | no proof | sticks |
| onepole-tanh | tanh × 1 | g ≤ 1 | safe | certified | certified | certified | none |
| tdf2-statesat (after Chowdhury) | tanh × 2 | a1 ∈ [−1.85, 0], a2 ∈ [0.9, 0.98] | safe | no proof | no proof | no proof | sticks |
| tdf2-outsat | tanh × 1 | same | safe | no proof | no proof | no proof | sticks |
| rossum-2pole | tanh × 1 | same | safe | no proof | no proof | no proof | sticks |
| svf-clip (Chamberlin) | clamp on bp | f ∈ [0.01, 0.5], q ∈ [0.5, 1.5] | safe | certified | certified | no proof | none |
| phaser-clip | clamp on feedback | a ∈ [−0.9, 0.9], fb ≤ 0.95 | safe | not run | not run | no proof | sticks |

**B2 on L:** 9 with a witness (9 sticks, 0 grows), 2 without. Every witness was re-run for 2000 periods
and did not decay.

**Largest certified resonance ceiling** (the other knobs at their box top), against the oracle's first
witness (bisection step 1/256 of the range) and the frozen limit:

| Filter | Knob | T3 certified | Oracle at T3 ceiling | First witness ≤ | Frozen limit |
|---|---|---|---|---|---|
| moog-tanh | k | 1.141 | none | 1.297 | 1.281 |
| moog-soft | k | 1.141 | none | 1.297 | 1.281 |
| moog-fbsat | k | 1.250 | none | 1.293 | 1.281 |
| diode-fbsat | k | 3.813 | none | 9.383 | 9.344 |
| korg35-sat | k | 1.455 | none | 1.504 | 1.499 |
| phaser-clip | fb | 0.434 (grid only) | none | 0.507 | 0.948 |

- **H1 holds on L:** no certificate (B4, T1 or T3, at the full box or at a ceiling) on a kernel or box with
  a witness.
- **B1, the frozen check, is false-safe on 4 of 4** frozen-safe filters that have a witness: both TDF-II
  biquads, the Rossum 2-pole and the phaser all stick under switching at knob values that are each stable
  when frozen. That is the whole case for the study.
- **B4 is never wrong on L and never beaten.** Linearise-and-hope certified exactly the two filters with no
  witness. Whether the saturator-aware pass is *needed* is decided on G, not here.
- **T3 misses svf-clip, which T1 and B4 prove.** Its C has one row on two states, so the rule added in
  phase 0 asks T3 for strict decay in |x| too (`x_strict`, section 8): ΔV ≤ −t|w|² alone does not give
  x → 0 when C is not injective. T1 survives because it gets strictness from the gain floor of a bounded
  orbit, not from the form. **Proposed, needs owner sign-off:** score G1 as "certified" when T1 or T3 proves it, since both are sound.
- **Ceilings sit close to the first witness** except on the diode (3.81 vs 9.38: common-P conservatism, as
  on the Moog at low cutoff) and the phaser (not multi-affine: corners are not exact, so it is grid-only
  and not counted).
- *Rig bug found and fixed:* the first pass reported a stuck orbit inside moog-tanh's certified box. It
  was float64 absorption: Newton walked to a state of size 2.7e12, where the per-sample increments
  (about 1e-4) round away, so the state looked fixed. A sticks witness now also needs amplitude ≤ 1e6 and,
  at every step, fl(x⁺) − x equal to the true increment to 1e-6 relative (`genuine` in `satloops.py`).
  After the fix: no witness at any certified ceiling, and all 9 real witnesses still pass.

**Phases.**

| Phase | Work | Needs a build |
|---|---|---|
| 0 | Reuse the venv. L in Python straight from the papers' equations; the oracle with the Newton orbit search; **B3** (the Moog's first witness k vs 2.8856); **B2** on L; check whether the non-strict vertices close on the Moog | No |
| 1 | Vertex certifier on Report 2's code; **H2** on the Moog; the T2 multiplier cross-check; generator for G (matrices and Field code together); **B1, B2, B4** on G | No |
| 2 | Field side: L as `.field` files with the saturator shapes; the dump learns to cut at saturators; extracted A, B, C match Python on all of L and G (max abs diff ≤ 1e-5) | Yes, one build at the end |
| 3 | **H1, H3** on L + G from extracted matrices; **H4** (G2) | No |
| 4 | Lock the scorer (commit), generate B (D5), score once | No |
| 5 | Sections 10-12, `ARTIFACT.md`, `kick-the-tires.sh`, Web Lab Note (Technical Report 3) | No |

Venv: `~/.venvs/stable-modulation`, reused unchanged; `scripts/requirements.txt` pins it.

## 9. Proposed solution trajectories

| # | Trajectory | For | Against | Choice |
|---|---|---|---|---|
| T1 | Sector vertices + common P, on Report 2's certifier and cover | Exact for its class; no new solver code; inherits Report 2's tested cover and Float32 check | Provably fails on ladders (phase 0) | Baseline (was main; D3 amended) |
| T2 | Diagonal S-procedure multiplier LMI (D3 as written) | One LMI per point, the textbook circle-criterion form | Never proves more than T1; only a cross-check | Cross-check |
| T3 | Slope-restricted Lur'e-Lyapunov function, V = xᵀPx + Σ qᵢ ∫φᵢ | Uses slope bounds; the only route found for ladders | Larger LMI; V is not quadratic, so the cell cover needs new work | **Main (G1)**, owner 2026-11-23 |
| T4 | Slope vertices + common P on the difference system | The direct G2 route; same code as T1 | Slope floors of 0 need the amplitude argument; `clamp` mostly gets no proof | **Main (G2)** |
| T5 | Zames-Falb / IQC multipliers | The least conservative known tests for slope-restricted loops | Built for time-invariant linear parts; our A(θ) switches every sample. We have not opened those papers | Rejected for this study |
| T6 | Per-circuit energy functions (Hélie, port-Hamiltonian) | Proofs for real circuits | By hand, one circuit at a time; not a compiler pass | Reference values only (H2) |

## 10. Benchmarking against the baseline
*(After measurement.)*

## 11. Critique
*(After measurement.)*

## 12. Next steps
*(After measurement.)*
