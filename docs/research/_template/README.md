---
title: "<Research Title>"
slug: "<slug>"
status: "phase-0-framing | pre-registered | measuring | validated | killed | published"
owner: "<name>"
date: "<YYYY-MM-DD>"
prior_art: "docs/prior-art/<yyyy-mm-dd>-<slug>.md"
commit_sha: "<pinned-sha-when-measured>"
---

# <Research Title>

## 1. Problem & Signal / Execution Model
- **Problem:** What fails today and why manual workarounds do not compose or scale.
- **Model:** Sample rate(s) $f_s$, signal/workload class, real-time budget, trusted computing base (TCB).
- **Assumptions:** Precision (`Float32`/`Float64`), rounding mode, FTZ/DAZ, input domain bounds.

## 2. Formal Definitions & Theoretical Bounds
- **Definitions:** Core mathematical/IR definitions.
- **Lemmas / Theorems:** Main bounds or soundness arguments (full proofs or machine-checked scripts in `proofs/`).
- **Floating-Point & Numerical Caveats:** Precision floors, denormals, non-associativity, interval widening limits.

## 3. Pre-Registered Hypotheses & Kill Criteria
*(Locked before running evaluation.)*

| ID | Hypothesis | Metric & Statistical Bar | Kill Criterion | Held-Out Validation Plan |
|---|---|---|---|---|
| H1 | | e.g., Spearman $\rho \ge 0.8$, MAE $\le 10$ dB | $\rho < 0.8$ | Community/external corpus ($N \ge \dots$) |
| H2 | | e.g., $\ge 25\%$ geometric-mean CPU reduction | $< 25\%$ | End-to-end patches |

## 4. Phases & Gate Checklist
- [ ] **Phase 0 (Measure / Baseline):** Deterministic rig + `data/phase0-baseline.csv` + `data/provenance.json`.
- [ ] **Phase 1 (Model / Predict):** Analysis/method tested against H1 on calibration + held-out corpus.
- [ ] **Phase 2 (Execute / Evaluate):** End-to-end implementation tested against H2 + ablations + baselines.
- [ ] **Phase 3 (Publish):** `ARTIFACT.md` verified, LaTeX PDF (`paper/`), Web Lab Note (`website/research/<slug>.html`).

## 5. Results & Observations
*(Generated from `data/` via `scripts/build_tables.py`; never hand-edited.)*

## 6. Threats to Validity & Caveats
- **Internal validity:** (rig bugs, numerical floors, limit cycles)
- **External validity:** (corpus bias, hardware/OS generalization)
- **Construct validity:** (proxy metrics vs perceptual/listening quality)
