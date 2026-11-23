---
title: "Proven-stable modulation: automatic stability certificates for user-written filters"
slug: "stable-modulation"
status: "signed-off"
owner: "namansoni"
date: "2026-11-23"
prior_art: "docs/prior-art/2026-11-23-stable-modulation.md"
commit_sha: "pending-measurement"
---

# Proven-stable modulation

**Signed off by the owner on 2026-11-23, as written, with D1-D4 as recommended. Sections 1-7 are locked
by this commit.** Sections 8-12 are filled after sign-off. Paired study: `docs/research/flicker-free-pixels`
(branch `feature/flicker-free-pixels`); both reuse the analysis in `src/core/field/AliasAnalysis.cpp`.

## 1. Goal

| # | Goal | Output |
|---|---|---|
| G1 | A compiler pass that, from a Field kernel's code alone, **proves** its filter cannot blow up under *any* modulation of its knobs inside their ranges, or says it can't | A certificate (a matrix P) per kernel, or "no proof" with the reason |
| G2 | Where no proof exists for arbitrary speed, find the **largest knob range** that is provable, and the **largest modulation speed** (slew per sample) that is provable for the full range | Two numbers per kernel: safe range, safe slew |
| G3 | Check the pass against the only published hand proofs (McClellan, DAFx26) | Our automatic ranges vs the paper's, filter by filter |
| G4 | Publish the result the same way whether it is positive or null | README 1-12, scripts, data, a Web Lab Note |

Product line this earns, if it holds: *"modulate anything, it never explodes"*, for user code and for code
written by agents.

## 2. Problem

**What fails.** A filter whose poles are inside the unit circle at every fixed knob setting can still blow
up when the knobs move. It is a known trap, with a two-line proof: Moog ladder, g = 1.3764, resonance k
alternating 0, 2.89, 0, 2.89 every sample grows without bound, although every frozen k up to 4 is stable
(McClellan 2026, Prop. 4.7). Musical modulation (envelopes, LFOs, audio-rate cables) does exactly this.

**Infinite today.** The only stability check is the frozen-knob one: `AliasAnalysis` computes the spectral
radius of the mean state Jacobian at default params (`loopRadius`, `src/core/field/AliasAnalysis.cpp:468`)
and calls < 1 "stable". That is the check the trap defeats. Field users and agents write their own
recurrences (`state` cells), so the fixed-filter hand proofs of shipped products don't cover them.

**Prior art** (full table: `docs/prior-art/2026-11-23-stable-modulation.md`):

| Source | What it shows | Gap this study fills |
|---|---|---|
| Laroche, JAES 55(6) 2007 | Two state-matrix criteria for time-varying recursive filters, applied per topology | By hand, per filter |
| Werner & McClellan, DAFx 2020in22 | Norm of a k-step product of state matrices < 1 is sufficient | Applied to known filters; no tool |
| McClellan, DAFx26 | Common quadratic Lyapunov functions (CQLFs): proofs for SVF and Sallen-Key; Moog k ∈ [0, 2.88], diode ladder k ∈ [0, 8.90], with counterexamples at 2.89 and 8.91. P found by hand or "random numerical search". Future work: slowly varying parameters | Not automatic, not for user code, no slew result |
| Control theory (CQLF via LMI, rate-bounded parameter-dependent Lyapunov functions) | The maths exists for general linear parameter-varying systems | Never joined to an audio language's compiler |
| Faust (library docs); Cmajor (searched, nothing found) | Feedback must contain a delay; safe ranges are documented in library comments (Faust ladder feedback "0 to 4") | No compiler check for stability under modulation was found |

Not found: any compiler, DSL or audio tool that certifies stability of user-written filters under
modulation. Absence in a search is not proof of absence.

## 3. Scope

| In | Out |
|---|---|
| Sample-domain Field kernels (Field Effect, Field Synth) with `state` cells | Pixel / element / frame domains |
| Recurrences **linear in the state**: next = A(θ)·state + B(θ)·in, where θ = params (and per-voice `freq`) inside their declared min/max | Coefficients that depend on `in` (signal-dependent filters) |
| Any function of θ in the coefficients (`sin`, `pow`, `exp`, `tan` prewarp, `Select` on params, which makes a switched system) | Nonlinearity inside the loop (`clamp`, `abs`, saturation). Stretch only, see D4 |
| State dimension n ≤ 8 | `delay(x, N)` lines in the loop (state dimension N) |
| Report-only analysis (prints a certificate, changes nothing at runtime) | Showing the safe range on knobs, auto-clamping: each is a UI/runtime feature that needs owner approval later |
| Research solver in Python, offline | Shipped C++ filters (`FilterModels.h` etc.): they are not Field code |

## 4. Existing constraints

| Constraint | Consequence |
|---|---|
| M2, 8 GB | Small problems only (n ≤ 8, a few hundred LMIs per kernel); fine on CPU |
| No SDP solver installed (`cvxpy`, `scipy` missing from system Python) | A research venv with cvxpy + an open solver (D3); no app dependency until the in-app port is decided |
| Runtime is `Float32`; certificate is in `Float64` | Certificates need a decay margin (A^T P A ⪯ (1 − ε) P) so float round-off can't break them; ε recorded |
| Field ops at sample rate: no `tanh`, `sign`, `fract`, `atan` | Literature filters are ported with what exists; nonlinear ladders are out anyway |
| Clean room | Papers and docs only. Never read Kronos, Cmajor, SuperCollider or BespokeSynth source. McClellan's published P matrices are used only as reference values |
| Research standard (`docs/RESEARCH_STANDARD.md`) | Self-written corpus is calibration only; "confirmed" needs a held-out set the author didn't tune on |
| `AliasAnalysis` already evaluates the program on the real interpreter and computes state Jacobians | Reused for A(θ) extraction; no new evaluator |

**Threats known before data**
- A sampled parameter grid can miss a bad θ between grid points: when A is not affine in θ, the proof
  must add a Lipschitz margin, or the claim is limited to the grid (D2).
- The oracle (section 6) finds instability witnesses but can't prove their absence, so "no witness"
  kernels may still be unstable. Coverage is measured against them, soundness against witnesses only.
- CQLF is sufficient, not necessary: some stable kernels have no CQLF. That costs coverage, never soundness.

## 5. Hypothesis

A Field kernel linear in its state is a linear parameter-varying system. Finding one matrix P with
A(θ)ᵀ P A(θ) ⪯ (1 − ε) P for every θ in the knob box is a small convex problem (an LMI). If found, the
kernel is stable under **any** modulation, at any speed, inside the box. If not, a P(θ) that may change
by a bounded amount per sample proves stability for modulation up to a slew limit.

| ID | Hypothesis | Metric and bar | Kill criterion | Held-out validation |
|---|---|---|---|---|
| H1 | **Sound**: the pass never certifies an unstable kernel | 0 certificates on kernels that have an instability witness | Any certified kernel with a witness (after ruling out a rig bug, recorded) | Sets L, G and blind set B |
| H2 | **Matches the hand proofs** automatically | Certified range ≥ 99% of McClellan's published one for each of: SVF (R ≥ R_min, R_min ∈ {0.01, 0.1}, any g), Sallen-Key (k ≤ 1.98, any g), Moog (k ≤ 2.85 of 2.88), diode ladder (k ≤ 8.81 of 8.90); and never past 2.89 / 8.91 | Any filter below 95% of the published range, or any certificate past a published counterexample | The four filters are ported from the paper's equations by a script, not tuned |
| H3 | **Covers** most stable user-style kernels | Certifies ≥ 80% of G kernels that have no witness | < 80% | Blind set B, scored once |
| H4 | **Slew certificates** extend the range for smooth modulation (McClellan's future work) | With slew limited to the full range over 20 ms at 48 kHz (Δk ≤ range / 960 per sample), the certified k range for Moog and diode ladder is ≥ 25% wider than the any-speed range | < 25% wider on either | Same two filters plus the 10 widest-gap kernels of B |
| H5 | **Cheap enough to run at compile time** (scored only if H1-H3 pass and the in-app port is approved) | Median ≤ 50 ms, p95 ≤ 250 ms per kernel on the M2, n ≤ 8, C++ solver | Median > 50 ms | All of L, G, B |

## 6. Baseline

Recorded once, before the pass is written, at a pinned commit. Not re-measured later.

**Corpora**

| Set | What | Size | Role |
|---|---|---|---|
| L | Literature filters ported to Field from published equations: one-pole, TPT SVF, Chamberlin SVF, Sallen-Key, linear Moog ladder (TPT), linear diode ladder, RBJ biquad in direct form 1 and 2 with modulated f and Q, coupled-form resonator, allpass phaser stage, Zavalishin one-pole ZDF, 4-stage phaser | ~15 | Calibration + H2 |
| G | Generated kernels: random linear state-space n = 1..6, coefficients as random affine / `sin` / `pow` / `exp` / `Select` functions of 1-3 params, half built to be frozen-stable everywhere | ≥ 200, fixed seed | Calibration + H3 |
| B | Blind: same generator with a different seed and the settings in D4, generated after the lock, scored once | ≥ 200 | Held-out for H1, H3 |

**Instability oracle (ground truth for "unstable").** For each kernel, sample θ at the box vertices plus a
grid, form products of up to 8 state matrices A(θ₁)…A(θₖ), and record a **witness** when a product has
spectral radius > 1 + 1e-6. A witness is a proof of instability: repeating that switching sequence grows
the state without bound. A second pass simulates 2¹⁶ samples under random and swept modulation, as a
sanity check only.

**Baseline numbers recorded**

| # | Number | Why |
|---|---|---|
| B1 | False-safe rate of today's frozen check (`loopRadius < 1` at every grid point) on kernels with a witness | The number the pass must take to 0 |
| B2 | Count of L and G kernels with a witness / without | Sets the denominators for H1, H3 |
| B3 | For the four McClellan filters: our oracle's smallest unstable k vs the paper's counterexamples | Checks the oracle against a published result before it judges anything |

## 7. Success metric to be achieved

| Hypothesis | Bar | Verdict rule |
|---|---|---|
| H1 | 0 false certificates on L + G + B | Any failure stops the study (after a rig-bug check) |
| H2 | ≥ 99% of each published range, none past a counterexample | All four must pass |
| H3 | ≥ 80% of no-witness G kernels certified, then the same on B | Confirmed only if B also passes |
| H4 | ≥ 25% wider k range at the 20 ms slew, Moog and diode | Both must pass |
| H5 | Median ≤ 50 ms, p95 ≤ 250 ms | Only after the in-app port is approved |

The study ships as research when H1-H3 pass. H4 is the novel extension; a failure there is reported, and
the H1-H3 claim still stands.

**Deliverables**
- `data/`: per kernel: witness, frozen-check verdict, certificate (P, ε, certified box, slew), timing.
- `scripts/`: kernel generator, oracle, certifier, table builder; `kick-the-tires.sh` under 5 minutes.
- A table of H2 against the paper, and a Web Lab Note.

## Decisions needed at sign-off

| ID | Decision | Recommendation |
|---|---|---|
| D1 | What counts as "unstable" | A product witness (spectral radius > 1 + 1e-6, length ≤ 8). Simulation blow-up only as a sanity check, since simulation can't prove anything |
| D2 | Kernels whose coefficients aren't affine in θ | Grid plus a Lipschitz margin computed from the grid spacing, so the proof covers the whole box. If the margin is too loose, report "grid-certified" separately and don't count it for H3 |
| D3 | Solver | Python venv with cvxpy and an open solver (Clarabel or SCS) for phases 0-2; an in-app C++ solver only if H1-H3 pass, as a separate approval |
| D4 | Blind set B and the nonlinear stretch | B uses seed 20261123, n up to 8 (G stops at 6), 30% `Select` kernels. Nonlinear loops (sector-bounded `clamp`/`abs`) stay out of this study; they become a follow-up if H1-H3 pass |

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
