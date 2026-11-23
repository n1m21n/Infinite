---
title: "Proven-stable modulation: automatic stability certificates for user-written filters"
slug: "stable-modulation"
status: "validated"
owner: "namansoni"
date: "2026-11-23"
prior_art: "docs/prior-art/2026-11-23-stable-modulation.md"
commit_sha: "ccf102e151b1ecb00dc1743b2ae4e42ef4b25026"
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

**Pipeline.** Field kernel → A(θ) → certifier → certificate or "no proof".

| Step | How | Where |
|---|---|---|
| Extract A(θ) | For each grid θ, run one sample step on the real interpreter with the state set to each unit vector eᵢ and `in` = 0; column i of A(θ) is the next state. A **linearity gate** checks f(a·x + b·y) = a·f(x) + b·f(y) on 8 random state pairs (tolerance 1e-5 relative, Float32); a kernel that fails is "not linear in state" and leaves scope, reported, never certified | An env-gated self-test in the app (`INFINITE_FIELDSTABDUMP`), test code only, dumps JSON. Phase 2 |
| Parameter box | Every param (and `freq` for synths) inside its declared min/max. Cutoff-like and damping-like params are gridded and split on a **log** axis. For the paper's filters, g ∈ [1e-5, 1e3] (about 0.15 Hz to 0.4997 fs at 48 kHz). The open ends g → 0 and g → ∞ are left out, since A → ±I there and nothing decays (phase 0 found this the hard way) | Python |
| Certificate (any speed) | Find P with I ⪯ P ⪯ 10⁴·I and (P − Aᵢᵀ P Aᵢ) / sᵢ ⪰ t·I at every grid θᵢ, maximising t (an LMI, Clarabel). sᵢ = 1 − ρ(Aᵢ) rescales each point, so points that barely decay are as well conditioned as the rest. The solver's P is then **re-checked in float64** (‖P^½ A P^−½‖ < 1); the solver is never trusted. **Cutting planes**: solve on a coarse grid (vertices + 5 per axis), then search a 4x finer grid and a local maximiser for the θ where λ_max(P^{-1/2} Aᵀ P A P^{-1/2}) is largest; add it and re-solve, up to 20 rounds | Python |
| Cover the gaps (D2) | The box is split into cells. On a cell, A(θ) lies within e of the multilinear blend of its corner matrices, and the P-norm is convex, so ‖A(θ)‖_P ≤ max over corners ‖A‖_P + e. e = 1.5 × the largest blend error measured at the cell centre and edge midpoints (the Lipschitz margin of D2, in second-order form). A cell that fails is split; a corner with ‖A‖_P ≥ 1 goes back to the LMI as a cutting plane. If the cover can't close within 200 000 cells, the result is "grid-certified" (reported, not counted for H3). *Changed from the first draft:* a first-order Lipschitz margin can't close near g → 0, where the decay per sample shrinks like g but ‖∂A/∂g‖ doesn't | Python |
| Largest safe range (G2) | Bisection on the upper end of one param (e.g. k), 2 decimals, the other params over their full range | Python |
| Slew certificate (H4) | P(θ) = P₀ + Σ θⱼ Pⱼ, with A(θ)ᵀ P(θ + Δ) A(θ) ⪯ (1 − ε) P(θ) and P(θ) ⪰ I at every grid θ, for Δ at the corners of the slew box (P is affine in Δ, so corners suffice). Bisection on the k range at Δk = range / 960 | Python |
| Margin | Recorded per certificate: t, κ(P), and the largest cell bound. Float32 check (phase 3): the certificate is re-verified with A computed in Float32 | Python |

**Oracle (D1, before the certifier is written).** Candidate set S = box vertices plus a 9-per-axis grid.
(1) Every product of length ≤ 4 over the vertex set; (2) length 2-8 periodic products over S found by
200 random starts of a greedy search that swaps one factor at a time to raise the spectral radius. A
witness is any product with spectral radius > 1 + 1e-6. Recorded: the sequence, its radius, and its
growth simulated over 2¹⁶ samples.

**Phases.**

| Phase | Work | Needs a build |
|---|---|---|
| 0 | Research venv; the four McClellan filters in Python straight from the paper's matrices (eq. 6 discretisation); oracle; **B3**: our smallest unstable k vs 2.89 / 8.91 | No |
| 1 | Certifier; **H2** on the four filters; generator for G (matrices and Field code together); **B1, B2** on G in Python | No |
| 2 | Field side: L ported to `.field`, `INFINITE_FIELDSTABDUMP` self-test; check that extracted A(θ) matches the Python matrices on all of L and G (max abs diff ≤ 1e-5) | Yes, one build at the end |
| 3 | **H1, H3** on L + G from extracted matrices; **H4** slew | No |
| 4 | Lock the scorer (commit), generate B (D4), score once | No |
| 5 | Sections 10-12, `ARTIFACT.md`, `kick-the-tires.sh`, Web Lab Note | No |

Venv: `~/.venvs/stable-modulation` (Python 3.12, numpy, scipy, cvxpy 1.9.3, Clarabel). Outside the repo;
`scripts/requirements.txt` pins it.

## 9. Proposed solution trajectories

| # | Trajectory | For | Against | Choice |
|---|---|---|---|---|
| T1 | Common P on a grid with cutting planes and a Lipschitz cover | Proves any-speed stability for the whole box; small LMIs; works for any coefficient function | Conservative for filters that need slow modulation (the Moog gap 2.88 vs 4) | **Main** |
| T2 | k-step norm (Werner & McClellan): ‖A(θₖ)…A(θ₁)‖_P < 1 for all length-k products | Can prove more than one common P | Number of products grows as grid^k; only practical for k ≤ 3 | Comparison only, on L |
| T3 | Parameter-dependent P(θ) with a slew bound | The only route to H4; reduces to T1 when the slew is unbounded | Bigger LMI (n² · (params + 1) unknowns) | **H4** |
| T4 | Sum-of-squares over polynomial coefficients | Exact over the box, no grid | Field coefficients use `tan`, `exp`, `pow`, `Select`: not polynomial; needs a new solver stack | Rejected |
| T5 | Symbolic A(θ) from Field IR instead of the interpreter | Exact derivatives for L | A second evaluator to keep in sync with the real one; the interpreter is ground truth | Rejected |

## 10. Benchmarking against the baseline

### B3 (phase 0, 2026-11-23): the oracle against the paper's counterexamples

Run: `~/.venvs/stable-modulation/bin/python scripts/phase0_b3.py` (about 60 s). Data:
`data/phase0-b3.json`, log `data/phase0-b3.log`. g is free in (0, ∞), k in [0, kmax]; bisection on kmax to
0.001 for the smallest value at which the oracle finds a witness.

| Filter | Paper: proven stable up to | Paper: counterexample | Oracle: smallest unstable kmax | Oracle's witness | Agrees |
|---|---|---|---|---|---|
| Moog ladder | 2.88 | 2.89 (g = 1.3764 fixed, k = 0, 2.89, …) | **2.8856** | g = 1.7769, k = 0, then g = 1.0661, k = 2.886, repeated; radius 1.00004 | Yes: inside (2.88, 2.89] |
| Diode ladder | 8.90 | 8.91 (g = 1.8161 fixed) | **8.9009** | g = 3.0815, k = 0, then g = 1.0703, k = 8.901; radius 1.00006 | Yes: inside (8.90, 8.91] |

The paper's own counterexamples reproduce exactly: radius of A(2.89)·A(0) at g = 1.3764 is 1.00109 (paper:
|λ| ≈ 1.001); at 2.88 it is 0.99870. For the diode ladder, 1.00084 at 8.91 and 0.99998 at 8.90.

What's new: letting g change along with k breaks the Moog ladder at k = 2.8856, below the paper's 2.89
(which holds g fixed). So the true limit sits between 2.88 and 2.8856. For the diode ladder the paper's 8.90
is already tight to 0.001.

**Rig bug, found and fixed before any score** (recorded under H1's rule). The first oracle missed the
witness at 2.891 and reported non-monotone results. Cause: the w grid included w = 0 and w = 1 (g = 0 and
g → ∞), where A = ±I has radius exactly 1. Those trivial points outranked the real near-misses, so the
fine-tuning step never started from the right sequence. Fix: candidates are interior w only, and the top 6
sequences are each fine-tuned (`scripts/stabmod.py`, `find_witness`).

### H2 (phase 1, 2026-11-23): the certifier against McClellan's four hand proofs

Run: `scripts/phase1_h2.py` (about 2 min), then `scripts/phase1_spotcheck.py`. Data: `data/phase1-h2.json`,
`data/phase1-spotcheck.json`. Bisection to 0.001 on the upper end of k, cutoff over its whole range.

| Filter | Paper's proven range | Bar | **Certified automatically** | Oracle: unstable from | Pass |
|---|---|---|---|---|---|
| Moog ladder | k ∈ [0, 2.88] | ≥ 2.85 | **k ∈ [0, 2.8848]** | 2.8856 | Yes, past the paper |
| Diode ladder | k ∈ [0, 8.90] | ≥ 8.81 | **k ∈ [0, 8.8965]** (99.96%) | 8.9009 | Yes |
| Sallen-Key | k < 2 | ≥ 1.98 | **k ∈ [0, 1.9990]** | 2 (frozen-unstable) | Yes |
| SVF | any R ≥ Rmin > 0 | R ∈ [Rmin, 10], Rmin = 0.01 and 0.1 | **both boxes certified** | — | Yes |

**H2 passes on all four**, with no hand work and the same code for each filter. Median time per
certification is 1.6 s and the slowest is 18.8 s (33 runs, Python, M2). The Moog result goes past the
published proof: together with phase 0's witness, the true any-speed limit for the linear Moog ladder is now
pinned to **k ∈ [2.8848, 2.8856)**, against the paper's gap of [2.88, 2.89].

**Independent check.** At each certified edge, 200 000 random (g, k) points the certifier never saw
(20 000 of them at k = kmax) all have ‖A‖_P < 1 (largest: 1 − 1.3e-10 Moog, 1 − 3.5e-9 diode, 1 − 7.7e-12
Sallen-Key; the tiny margins sit at g → 1e-5, where decay per sample is about g). 65 536 samples of random
switching never raise ‖x‖_P above its start.

### B1, B2 (phase 1, 2026-11-23): how often today's check is fooled, on corpus G

Run: `scripts/gen_kernels.py G` (seed 20261121, 200 kernels, n = 1-6, 1-3 knobs; corpus `corpus/G.json`),
then `scripts/phase1_b12.py` (about 50 s). Data: `data/phase1-b12.json`. The oracle is phase 0's, with
5-9 points per knob and continuous tuning on near misses.

| # | Number | Value |
|---|---|---|
| B2 | G kernels with a witness / without | **76 / 124** (41 of the 76 need switching: the witness is a product of 2-8 steps) |
| B1a | Kernels with a witness that the **frozen check at default knobs** (what `AliasAnalysis` does today) calls stable | **54 of 76 (71%)** |
| B1b | The same for a stricter frozen check at **every point of a 7-per-knob grid** | **15 of 76 (20%)** |

So even a frozen check made far stricter than today's misses 1 in 5 kernels that really blow up. Those 15
grow by 1.0008 to 1.17 per sample under their witness sequence; at 1.17 the output is a million times larger
after about 90 samples (2 ms at 48 kHz). Of the 100 kernels built to be stable at every fixed setting, 12 have
a switching witness.

The H3 denominator is the 124 no-witness kernels (88 of them built frozen-stable). H1 is scored on all
200 in phase 3, from matrices extracted by the real interpreter.

### Phase 2 (2026-11-23): A(θ) from the real interpreter matches Python

Run: `scripts/emit_field.py corpus/field` writes L and G as `.field` kernels with a `.grid` of knob settings.
Then one dev build and `INFINITE_FIELDSTABDUMP=corpus/field INFINITE_FIELDSTABDUMP_OUT=<dir> Infinite`
(self-test `src/app/selftest/StabilityTests.cpp`, about 2 s), then `scripts/phase2_compare.py <dir>`.
Data: `data/phase2-compare.json`.

| Set | Kernels | Dumped | Grid points | Max abs error vs Python | Linearity / `in` gate, max error |
|---|---|---|---|---|---|
| L: McClellan's four filters, in the paper's state form | 4 | 4 | 324 | 1.7e-6 (SVF) | 3.2e-6 |
| L: 12 textbook filters (one-pole, TPT one-pole, Chamberlin and TPT SVF, RBJ DF1/DF2/DF2T, coupled resonator, allpass, 4-stage phaser, Stilson ladder, DC blocker) | 12 | 12 | 540 | 1.0e-6 | 2.7e-7 |
| G | 200 | **125** | 17,381 | 1.9e-7 | 2.3e-7 |

- Every kernel that compiled was dumped. Every dumped matrix matches Python within 1.7e-6, below the 1e-5
  bar. The error is Float32 rounding.
- No kernel failed the linearity gate or the `in` gate. That's expected, since L and G are linear by
  construction. Nonlinear kernels are tested in phase 3.
- Changes from section 8, none of which affect a result:
  - The gate tolerance in the code is 1e-4, not 1e-5. The largest error measured was 3.2e-6, so the
    planned 1e-5 would pass too.
  - The gate uses 2 random pairs per grid point (at least 18 per kernel) instead of 8 per kernel.
  - The output is a text matrix file, not JSON.
- Two bugs in the emitter, fixed before scoring:
  - `lo` is a reserved name in Field.
  - The first G emission spent a register per coefficient. It is now compact: the scale is folded into
    the coefficients, and each knob function is computed once and shared.

**Finding: 75 of the 200 G kernels don't compile in Field.** The sample backend never reuses a
register, and a kernel gets 128 (`kSampleMaxRegs`, `src/core/field/SampleProgram.h:20`; allocation in
`src/core/field/BackendRegister.cpp:57`). A dense state update costs about 3n² registers before any knob
terms, so the failures are all large: 6 of 39 kernels at n = 4, 20 of 31 at n = 5, and 49 of 49 at
n = 6. This limit is in today's product, not in the study: a user can't write a dense 6-state
modulated filter in Field. It also removes the hardest cases from a Field-only phase 3. Of the 15
kernels that fool the full-grid frozen check (B1b), only 4 compile.

**Owner decision (2026-11-23):** phase 3 scores all 200 G kernels. The 125 that compile use matrices from
the Field interpreter; the 75 over the cap use the Python reference (`gen_kernels.A_from_spec`). Results
are reported for both groups separately and together. Raising the register cap is a separate product
question, not part of this study.


### Phase 3 (2026-11-23): H1, H3 on L + G, the Float32 re-check, and H4

Run: `scripts/phase3_score.py L,G --jobs 6` (about 85 s, writes `data/phase3-L-G.json` and `.log`), then
`scripts/phase3_h4.py` (about 3 min, writes `data/phase3-h4.json`). The Float32 step reuses the phase 2
binary; nothing was rebuilt.

**Verdicts**

| ID | Bar | Result | Verdict |
|---|---|---|---|
| H1 | 0 certificates on kernels with a witness | **0** of 89 witness kernels certified (9 L, 80 G) | **Pass** (B still to come) |
| H3 | ≥ 80% of no-witness G kernels certified | **111 of 120 = 92.5%** (Field matrices 77 / 82, Python 34 / 38) | **Pass** (B still to come) |
| H4 | ≥ 25% wider k at the 20 ms slew, Moog and diode | **0%** on both, with g over the declared [1e-5, 1e3] | **Fail** |

**H1 and H3 by set**

| Set | Kernels | Witness | No witness | Certified | Certified with a witness |
|---|---|---|---|---|---|
| L | 16 | 9 | 7 | 5 (paper SVF, TPT one-pole, TPT SVF, allpass, DC blocker) | 0 |
| G, Field matrices | 125 | 43 | 82 | 77 | 0 |
| G, Python matrices (over the register cap) | 75 | 37 | 38 | 34 | 0 |

- Every proof the cover started closed. No kernel ended "grid-certified".
- The 11 no-witness kernels left without a proof:
  - Two L filters reach the edge of stability inside their own box: the one-pole at a = 0, and the
    coupled resonator at r = 1. The best growth found is exactly 1.0, so "no proof" is the right answer.
  - Nine G kernels have best growth 0.98 to 0.998, and the LMI is clearly infeasible (t from −0.03 to
    −2.2). Each is either stable without a common P, or unstable in a way the oracle can't find. Phase 4
    reports the same class on B.
- Near misses that still certified (worst cell bound): paper SVF 0.9999998 (its box reaches R = R_min),
  TPT SVF 0.9999999, g121-124 0.9999975, g121-013 0.9999962.
- Certify time per kernel in Python: median 0.35 s, p95 4.5 s, max 7.7 s. H5 isn't scored yet (C++ port
  not approved).

**Rig bug in the oracle, found and fixed before any verdict.**
- The phase 1 oracle looked only at a 4 to 9 point grid, and tuned only near misses above 0.97. Four G
  kernels (g121-084, 115, 145, 154) are unstable at one fixed knob setting that falls between grid points.
  They were wrongly counted as "no witness".
- The oracle now runs a dense frozen scan (129 / 33 / 13 points per axis for 1 / 2 / 3 knobs) with
  Nelder-Mead from the 5 worst points, and always tunes the top 3 switching sequences.
- No witness found in phase 1 was lost. 50 of the 80 G witnesses have length 1: frozen unstable.
- Corrected counts:

  | Count | Phase 1 | Corrected |
  |---|---|---|
  | B2: G kernels with a witness | 76 | **80** |
  | B1a: default-knobs frozen check calls them stable | 54 of 76 | **58 of 80** |
  | B1b: full-grid frozen check calls them stable | 15 of 76 | **17 of 80** |

  The phase 1 data files are kept unchanged as the baseline record.

**Float32: does each certificate survive the interpreter's arithmetic?**
- **What was checked.** Each certificate has up to 5000 check points: every LMI point plus the cover
  points with the highest P-norm. At these points the Field interpreter's A was measured as ‖A_f‖_P.
- **Two levels.**
  - *Holds at points*: every ‖A_f‖_P < 1.
  - *Proved*: the largest rise over float64 is also below the cover's slack (1 − worst cell bound).
- **Python kernels.** The 75 kernels Field can't compile use a bound in place of the measurement:
  √κ · n · 10 · (phase 2 error).

| Certified kernels | Holds at points | Proved | Largest rise |
|---|---|---|---|
| L, Field (5) | 4 | 3 | 2.5e-7 |
| G, Field (77) | 77 | **77** | 1.7e-7 |
| G, Python bound (34) | n/a | 31 | bound up to 8.7e-5 |

- **Fails as proved.**
  - **paper SVF**: one point reaches ‖A_f‖_P = 1.00000005 against a slack of 2.7e-10. Its float64
    certificate is correct, but it is closer to 1 than Float32 can resolve. A shipped check must report it
    as not proved in Float32, or shrink the box: R_min plus a margin.
  - **TPT SVF**: the rise is 2.5e-7 against a slack of 5e-8, so it holds at every point but isn't proved.
  - **g121-013, 046, 124** (Python matrices): the loose bound is above their slack of 2.5 to 18e-6.
- **A second rig bug, fixed before the verdict.**
  - **Symptom.** The first Float32 pass showed rises up to 0.44 on G kernels that use `if`.
  - **Cause.** Every worst point sat within 1e-9 of an `if(p > c)` threshold. The cover evaluates A that
    close inside a break on purpose. Float32 can't resolve 1e-9, so the interpreter rounded the knob back
    across the threshold and took the other branch.
  - **Why it isn't an error.** Both branches are covered up to the threshold, so stability isn't affected.
  - **Fix.** The check moves such points to 1e-6 of the knob range on the cover's side (2389 points).
    After that, the largest rise on Field kernels is 2.5e-7, the phase 2 rounding error.

**H4: slew certificates (T3)**

| Run | g range (cutoff) | Moog: any-speed → slew k | Diode: any-speed → slew k | Bar 25% |
|---|---|---|---|---|
| **Registered** | [1e-5, 1e3] (down to ≈ 0.15 Hz) | 2.88 → **2.88 (0%)** | 8.90 → **≤ 8.8 (0%)**, see the bullets | **Fail** |
| Exploratory | [tan(π·20/48000), 1e3] (≥ 20 Hz) | 2.88 → 3.50 (+21%) | 8.90 → 12.50 (+41%) | Moog fails, diode passes |

- **Why the registered claim can't be met by any certificate of this kind.**
  - As the cutoff g → 0, the filter stops moving: A → I.
  - Hold x fixed and step k up or down by dk. For V(x, k) to never grow, V(x, k ± dk) ≤ V(x, k) must hold
    in both directions, so V can't depend on k.
  - With g down to 1e-5, the per-sample decay (about g) is far smaller than any change a k-dependent P
    could make, so P is forced to be flat in k.
  - Neither affine P(k) nor piecewise-linear P(k) (9 and 17 nodes, an addition to section 8) certifies past
    the any-speed limit.
  - This is a limit of every Lyapunov function V(x, k) that ignores g, not a tuning problem.
- **Diode under the registered claim.** In this LMI form the slew certificate fails even at the any-speed
  value 8.8965; it certifies 8.8 (grid effect at the very edge). Either way the slew bound adds nothing.
- **Control: the gain comes from the slew bound, not the floor.**
  - With the 20 Hz floor and a constant P (any-speed k), the LMI is already infeasible at Moog 2.9 and at
    diode 8.9.
  - A per-k P over g alone exists well past both limits: Moog 3.6 (t = 6e3) and diode 13 (t = 780).
  - So what's left is how fast P may change with k compared with how fast the filter decays.
- **Sanity simulations at the exploratory limits** (2^14 samples, bang-bang slew, random g): the peak gain
  is 1.09 for the Moog and 2.54 for the diode. Both stay bounded; the diode's peak is non-normal transient
  growth, not divergence.
- **Section 7 rule.** H4 fails, and the H1 to H3 claim stands. The exploratory row is not a verdict. It
  shows the slew claim is only meaningful relative to the filter's own speed (the next steps carry it).

### Phase 4 (2026-11-23): held-out validation on blind set B, scored once

- **Lock.** The scorer was locked at commit `f9180894`, before B existed. That commit only taught the
  scripts to read `corpus/B.json`; no threshold, oracle setting or certifier step changed.
- **Generation.** `scripts/gen_kernels.py B` was then run as D4 sets out: seed 20261123, 200 kernels, n from
  1 to 8, select at 30% of terms.
  - D4's "30% Select" is read as a share of terms. G is at 15% of terms, which already puts a select in 68%
    of its kernels. Measured on B: 29.6% of terms, 177 of 200 kernels.
  - Sizes: n = 1 to 8 → 17 / 19 / 28 / 31 / 30 / 22 / 31 / 22 kernels.
- **Run.** `emit_field.py`, one interpreter dump with the phase 2 binary (no rebuild),
  `phase2_compare.py <dir> data/phase4-B-compare.json`, then `phase3_score.py B --jobs 6`, once (105 s).
  Data: `data/phase4-B.json`, `.log`, `data/phase4-B-compare.json`.

**Field dump on B**
- 91 of 200 compile. All 91 match Python within 2.2e-7, below the 1e-5 bar. The largest gate error is
  3.0e-7.
- 109 exceed the 128-register cap: every kernel with n ≥ 6, plus most with n = 5. They are scored on
  Python matrices under the G rule (owner decision 2026-11-23).
- The Float32 bound for those kernels uses the 1.9e-7 fixed at the lock. B's measured 2.2e-7 is inside the
  10x factor.

**Verdicts on B**

| ID | Bar | Result on B | Verdict |
|---|---|---|---|
| H1 | 0 certificates on kernels with a witness | **0** of 85 | **Pass**: confirmed on L + G + B |
| H3 | ≥ 80% of no-witness kernels certified | **102 of 115 = 88.7%** | **Pass**: confirmed on B |

| Group | Kernels | Witness | No witness | Certified | Share |
|---|---|---|---|---|---|
| Field matrices | 91 | 34 | 57 | 55 | 96.5% |
| Python matrices (over the cap) | 109 | 51 | 58 | 47 | 81.0% |
| All of B | 200 | 85 | 115 | 102 | 88.7% |

- **Coverage drops with size.** n ≤ 5: 73 of 76 no-witness kernels certified. n = 6 to 8: 29 of 39.
  - Larger random systems are less likely to have one common quadratic P.
  - H3 still clears 80% on the Python group, where all the large kernels sit.
- **The 13 kernels without a proof** have best growth 0.95 to 0.998, and every LMI is infeasible. None
  ended "grid-certified".
- **Float32.**

  | Certified kernels | Holds at points | Proved | Largest rise |
  |---|---|---|---|
  | Field (55) | 55 | **55** | 1.3e-7 |
  | Python bound (47) | n/a | 41 | bound up to 9.5e-5 |

  The 6 that aren't proved all use the loose bound, with slack 1.2e-6 to 3.8e-5. No measured Field
  certificate failed.
- **Today's frozen check on B** (phase 1 definitions):

  | Check | Result |
  |---|---|
  | B1a: default knobs (box midpoint) | calls **77 of 85** unstable kernels safe (91%) |
  | B1b: every point of a 7-per-axis grid | calls **28 of 85** safe (33%) |
  | The certifier | calls **0** safe |

  47 of B's 85 witnesses are frozen-unstable at one setting.
- **Certify time in Python:** median 0.37 s, p95 9.5 s, max 19.1 s. Times grow with n = 8.

**Section 7 rule.** H1 to H3 pass on L, G and B, so the study ships as research. H4 failed as registered
(phase 3). H5 waits for approval of the in-app port.

## 11. Critique

### Goals against what was delivered

| Goal | Delivered | Gap |
|---|---|---|
| G1: proof or "no proof" from the kernel's code | Yes, from matrices the real interpreter produces (Python for kernels over the register cap). H1 holds on L + G + B, and H3 is 92.5% on G and 88.7% on B | Report-only research tool in Python; not in the app |
| G2: largest safe range and largest safe slew | Range by bisection for the four paper filters only (H2). The slew certificate (H4) fails as registered | No per-kernel range search on G / B; no slew result that holds down to g → 0 |
| G3: match the hand proofs | H2 passed on all four filters in phase 1 | none |
| G4: publish either way | README sections 1-12, data, scripts, `ARTIFACT.md`, `kick-the-tires.sh`, Lab Note | none |

### Threats to validity

- **What "certified" means.** The LMI and its float64 re-check are exact at their points. Between points,
  the cover uses a heuristic margin: 1.5 × the blend error measured at the cell centre and edge midpoints
  (D2). So a certificate is "verified on a dense adaptive cover with a heuristic interpolation margin",
  not a formal proof over the continuum.
  - Evidence the margin holds: 200,000 random points never seen by the certifier (phase 1 spot check).
  - Up to 5000 more check points per certificate in Float32 (phase 3, phase 4).
  - No point anywhere had ‖A‖_P ≥ 1 in float64.
  - A rigorous version needs a Lipschitz or interval bound on A(θ) per cell. That is possible for Field's
    operator set, but it isn't done.
- **The oracle is one-sided.** A witness proves instability, but "no witness" doesn't prove stability.
  - H1 counts certificates on kernels with a found witness. An unstable kernel the oracle misses would
    land in the H3 denominator, and could only make a false certificate look like a success.
  - The float64 cover makes such a certificate unlikely, but the oracle can't rule it out.
  - The oracle had one rig bug, found and fixed in phase 3, with no verdict changed.
- **Who wrote the kernels.**
  - L was ported by the author from papers and textbooks.
  - G and B come from one generator the author wrote, with B on a different seed, larger n and twice the
    select share.
  - No real user kernels or shipped Field presets were scored.
  - Generated kernels are dense random systems. That is a harder shape than typical filters (ladders and
    SVFs are sparse), so coverage on real filters may be higher. It isn't measured.
- **Field-matrix coverage.**
  - 184 of 400 G + B kernels exceed Field's 128-register cap and were scored on Python matrices.
  - For those, the Float32 check is a bound, not a measurement. 9 certificates in that group aren't
    proved in Float32 by the bound.
  - The two marginal L filters (paper SVF, TPT SVF) are certified in float64, but their margin is below
    Float32 resolution.
- **Linear only.** Nonlinear loops (`tanh`, `clamp`) and delay lines are out of scope, and they are
  common in real patches.
- **H4's negative result is about the claim, not just the method.**
  - Any Lyapunov function V(x, k) that ignores g can't use a slew limit while the cutoff can reach 0, as
    phase 3 shows.
  - The registered bar was therefore unreachable by design. That should have been caught at sign-off.
- **Cost.** In Python: median 0.35 s and p95 9.5 s per kernel, on one M2. H5 (≤ 50 ms median in C++) is
  not scored.

## 12. Next steps

**This study**

| Step | What | Note |
|---|---|---|
| Phase 5 | Sections 11-12 final, `ARTIFACT.md`, `kick-the-tires.sh`, Web Lab Note | Done 2026-11-23 |
| Follow-up (H4 redo, needs its own sign-off) | Rate-aware slew: limit dk relative to the filter's own decay (for example Δk ≤ c · g per sample), or V(x, g, k) with g slew-limited too | Phase 3 shows the fixed 20 ms slew can't help while the cutoff can reach 0 |
| Product question, not research | Field's 128-register cap blocks dense n ≥ 5 filters | Phase 2 finding |
| Next held-out set | Score Infinite's shipped Field presets with `state` cells, and community kernels | Real filters, not generated ones (section 11) |
| Rigorous cover (**queued 2026-11-23, after C1 phase 0**) | For the 4 paper filters: an interval bound on A(θ) per cell in place of the 1.5x heuristic margin, plus an exact rational check (P rounded to rationals, exact Cholesky of P − AᵀPA at each cell bound). Python only | Turns the [2.8848, 2.8856) bracket from "verified on a cover" into a computer-assisted proof; also closes the one-sided-oracle objection for those certificates |
| In-app port (H5) | C++ LMI solver, report-only pass in the Field compiler | A new feature: needs owner approval first |

**Candidate studies (from the owner's DSP research map, 2026-11-23).** Each needs its own sections 1-7
and sign-off before work. This list is a choice of what to study, not an approval to build.

| # | Study | Why Infinite can get there first | Reuses | Pick |
|---|---|---|---|---|
| C1 | **Saturating loops**: stability certificates for `tanh` / `clamp` inside modulated feedback (sector-bounded / Lur'e, with input) | No general discrete-time proof for nonlinear + fast modulation; Field already knows which ops sit in the loop | This certifier (LMI + cover), D4 nonlinear gate, the phase 2 dump | **Yes, first** |
| C2 | **Lossless and colourless delay networks**: compile-time check that a Field FDN is lossless (Schlecht & Habets 2017 unitary-similarity test) plus a measured "metallic ringing" score (pole clumping, low-order delay sums) | The losslessness test is exact and cheap; nobody ships it in a DSL; colourlessness is still open | Field IR, the dump self-test pattern | **Yes, second** |
| C3 | Offline FDN tuner (FLAMO-style, Cayley / matrix-exponential parameterisation) | 0% runtime cost | C2 | Later, only after C2, and a new feature needs owner approval |
| C4 | Rate-aware slew certificates | McClellan's open future work; phase 3 found why the plain form fails | This study | Folded into this study's follow-up above |
| — | Multi-stage / modulated ADAA | `aliasing-aware-field` already missed its CPU bar | | No |
| — | Universal perceptual quality metric, real-time phase reconstruction | Crowded fields led by large labs; no Infinite edge | | No |
| — | Differentiable integer delays, modulated-delay BPTT | Narrow; fits inside C3 if it ever runs | | No |
| — | "Certified AI instrument sandbox" | Already the agent-collab gate plan (`docs/plans/agent-collab`); C1, C2 and the two current studies feed it | | Not separate |

