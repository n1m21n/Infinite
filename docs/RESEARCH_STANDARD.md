# Infinite Research & Proof Standard

This document defines Infinite's institutional standard for conducting, proving, packaging, and publishing technical research across audio DSP, compilers (Field), real-time rendering, and systems engineering.

It aligns Infinite's internal **Lane R (Research)** workflow (see [`.claude/skills/infinite-lifecycle/SKILL.md`](../.claude/skills/infinite-lifecycle/SKILL.md)) with:
- **DAFx / AES / SIGPLAN (PLDI/POPL)** mathematical and empirical rigor
- **ACM Artifact Evaluation v1.1** reproducibility standards
- **Distill / Quarto** interactive web publishing alongside formal LaTeX papers

---

## 1. Standard Project Layout

Every formal research track lives under `docs/research/<slug>/` (or links cleanly to its code/test rig):

```text
docs/research/<slug>/
├── README.md            # Problem, signal/execution model, pre-registered H1/H2 + kill criteria, findings
├── ARTIFACT.md          # ACM AE v1.1 roadmap: Claim -> Script -> Figure/Table -> Tolerance
├── paper/               # LaTeX/Quarto/MyST manuscript, bibliography (IEEEbib.bst), compiled PDF
├── proofs/              # Full mathematical derivations, Lean 4 / Alive2 / Gappa / SymPy verification
├── corpus/              # Benchmark kernels, test signals, dry audio stems, or workloads
├── data/                # Immutable raw CSVs/JSONs + provenance.json (never hand-edited)
├── scripts/             # kick-the-tires.sh (<5 min), run_all.sh, plot_figures.py, build_tables.py
└── results/             # Auto-generated figures and LaTeX/HTML tables (regenerated from data/)
```

---

## 2. Mathematical Rigor & Proof Tiers

All theoretical claims must state **exact assumptions** before stating any bound or theorem:
- **Signal / Execution Model:** Sample rate(s) $f_s$, signal class (bandlimited, stationary, transient), real-time buffer/latency budget, and trusted computing base (TCB).
- **Numerical Model:** IEEE-754 precision (`Float32` vs `Float64`), rounding mode (round-to-nearest-even), subnormal/denormal handling (FTZ/DAZ vs IEEE), and FMA behavior.

### Proof Structure
1. **Formalize:** `Definitions` $\rightarrow$ `Lemmas / Propositions` $\rightarrow$ `Main Theorem(s)`.
2. **Main body vs. Appendix:** Keep intuition, theorem statements, and concise proof sketches in the main text; place full step-by-step derivations or machine-checked scripts in `proofs/` (or `\appendix`).
3. **Mandatory Floating-Point Caveats:** Explicitly address failure of associativity/distributivity, interval dependency, underflow/precision floors (e.g., `Float32` phase-accumulator floor), and mixed-precision cast boundaries.

---

## 3. Empirical Evaluation & Pre-Registration

### Pre-Registration (Before Running Experiments)
Every `README.md` must lock:
- **Hypotheses (`H1`, `H2`, ...):** Falsifiable statements with exact target metrics.
- **Kill Criteria:** Explicit numerical thresholds at which the hypothesis is rejected and the line of work stops or pivots.
- **Baselines:** Fair, tuned state-of-the-art baselines (not unoptimized strawmen).

### Statistical & Reporting Norms
- **Non-parametric / Bounded Scales:** For ranked or bounded metrics (e.g., MUSHRA $[0, 100]$, Spearman rank correlations), report **medians and IQR**, non-parametric tests (Spearman $\rho$, Friedman omnibus + Wilcoxon signed-rank post-hoc with Holm/Bonferroni correction), test statistics, $N$, and exact $p$-values.
- **Speedups & Latency:** Use **geometric means** for speedup ratios across benchmarks, report full distributions (box/violin plots or percentiles), and include confidence intervals.
- **Ablations:** Isolate each design choice/pass and report sensitivity.
- **Held-Out Validation Gate:** Self-authored corpora are valid for Phase 0/1 calibration only. A hypothesis cannot be marked **confirmed** until tested on held-out / independent inputs (community patches, external datasets, or independent baselines).
- **Threats to Validity:** Separate **Internal** (rig bugs, float variance), **External** (corpus/hardware generalization), and **Construct** (proxy metric vs perceptual reality) validity.

---

## 4. ACM Artifact Evaluation v1.1 & Provenance

Every published claim must be backed by `ARTIFACT.md` and `data/provenance.json`:

- **Immutable Identifiers:** Exact Git **commit SHA** (never a mutable branch name), container/toolchain digest if applicable, and archival DOI (e.g., Zenodo) on release.
- **Environment Pins:** OS, compiler/version, build flags (e.g., `-O3 -ffast-math` vs IEEE-strict), CPU/GPU/RAM, and global RNG seeds.
- **Zero Manual Copy-Paste:** `scripts/run_all.sh` produces raw CSVs in `data/`; `scripts/plot_figures.py` and `build_tables.py` generate every figure and table directly from `data/`.
- **Two-Script Entry:**
  - `scripts/kick-the-tires.sh` (<5 minutes): verifies build, environment, and 1–2 smoke cases.
  - `scripts/run_all.sh`: reproduces the full dataset and all figures/tables.
- **Automated Gate:** Run `python3 tools/research/research_lint.py` (passes with exit `0` when frontmatter, hypotheses/kill criteria, `ARTIFACT.md`, and `data/provenance.json` are valid).

---

## 5. Dual Publishing (Formal Paper + Interactive Web Note)

Infinite publishes research in **two synchronized forms**:

1. **Formal Paper (`paper/`):**
   - Compiled with the target venue's official LaTeX template (e.g., **DAFx** 2-column, max 8 pages, `IEEEbib.bst`), with static vector figures and an Artifact Appendix.
   - Preprints archived on **arXiv** (`cs.SD` / `eess.AS`); artifacts archived on **Zenodo** with a persistent DOI.
2. **Interactive Web Lab Note (`website/research/<slug>.html`):**
   - Rendered in Infinite's site design (`data-mode="midnight"`, `TechArticle` JSON-LD, KaTeX math, `<link rel="alternate" type="application/json">` pointing to `data/`).
   - Includes **interactive audio/visual demos** (A/B players, parameter sliders, or `embeds/`), collapsible proof drawers, explicit caveats, and links to the pinned commit SHA / PDF / DOI.

---

## 6. Clean-Room & Citation Integrity

- Never open, read, grep, or cite copyleft/GPL source code (Kronos, Cmajor, SuperCollider, BespokeSynth).
- In `docs/prior-art/`, mark every source explicitly as `OPENED` or `Seen in search only (do not cite as verified)`.
