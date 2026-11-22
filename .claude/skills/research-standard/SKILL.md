---
name: research-standard
description: "Formalize Infinite research, mathematical proofs, reproducible artifacts, and dual Web/DAFx publishing. Use for Lane R research, benchmarks, technical lab notes, papers, or validating hypotheses."
---

# Infinite Research & Proof Standard

## When to use (full scope)

Use this skill whenever starting, executing, reviewing, or publishing work on **Lane R (Research)** or any engineering claim that requires mathematical proof, empirical benchmarking, or public technical publication (DAFx/AES/PLDI papers, `website/research/`, `website/benchmarks.html`, or technical blog notes).

Full human-readable specification: [`docs/RESEARCH_STANDARD.md`](../../docs/RESEARCH_STANDARD.md).
Templates: [`docs/research/_template/README.md`](../../docs/research/_template/README.md) and [`docs/research/_template/ARTIFACT.md`](../../docs/research/_template/ARTIFACT.md).

## Non-Negotiable Rules

1. **Clean room.** Infinite is MIT. Licensing follows AGENTS.md rule 1: open-source code of any licence (GPL included) may be read to understand a technique, never copied or transcribed. A paper or web note cites only published papers, abstracts, documentation and public discussions, never other projects' source files, and the prior-art survey explicitly separates `OPENED` sources from `Seen in search only (do not cite as verified)`.
2. **Pre-register before running.** Every hypothesis (`H1`, `H2`, ...) must have an explicit numerical metric and **kill criterion** written in `README.md` *before* running the evaluation. Report killed/negative results transparently.
3. **No self-authored-only validation.** A hand-built corpus may be used for Phase 0/1 calibration, but no hypothesis is marked **confirmed** without held-out / independent validation (e.g., community patches, external datasets, or independent baselines).
4. **Immutable data & provenance.** Raw CSVs/JSONs in `data/` are never hand-edited. Every figure and table in a paper or web note must be generated programmatically from `data/` and accompanied by provenance (exact Git commit SHA, build flags, OS/hardware, RNG seed, CLI command).
5. **Dual-tier proof honesty.** Never claim generality beyond what was mathematically proved or empirically tested. Explicitly document IEEE-754 Float32/Float64 caveats, denormals, precision floors, and Threats to Validity.

## Five-Phase Workflow

### Phase A — Frame & Prior Art
1. Run `prior-art-scout` and save `docs/prior-art/<yyyy-mm-dd>-<slug>.md`.
2. Define the **Signal / Execution Model** ($f_s$, signal class, real-time budget, trusted computing base) and **Assumptions** (rounding mode, input bounds, FTZ/DAZ).
3. Scaffold `docs/research/<slug>/` from `docs/research/_template/`.

### Phase B — Formalize & Pre-Register
1. Write **Definitions $\rightarrow$ Lemmas/Propositions $\rightarrow$ Main Theorem(s)** with explicit scope and floating-point caveats. Place full derivations or machine-checked proofs (Lean 4 / Alive2 / Gappa / SymPy) in `proofs/`.
2. Lock **H1, H2, ...** with exact statistical bars (e.g., Spearman $\rho$, MAE, median + IQR, Friedman + Wilcoxon with Holm/Bonferroni for perceptual/MUSHRA, geometric mean + 95% CI for speedups) and **Kill Criteria**.

### Phase C — Measure & Package Artifact (ACM AE v1.1)
1. Run deterministic experiments via `scripts/run_all.sh` (plus a <5 min `scripts/kick-the-tires.sh` sanity check).
2. Store raw outputs in `data/` alongside `data/provenance.json`.
3. Fill out `ARTIFACT.md` with the **Claim → Script → Figure/Table → Tolerance** matrix.

### Phase D — Held-Out Validation & Threats to Validity
1. Validate on held-out / external inputs.
2. Document **Internal**, **External**, and **Construct** threats to validity and all observed caveats/bugs.
3. Run `python3 tools/research/research_lint.py docs/research/<slug>` and confirm exit code `0`.

### Phase E — Dual Publish (Formal Paper + Interactive Web Note)
1. **Formal Paper (`paper/`):** Compile with the target venue's LaTeX template (e.g., DAFx 2-column, max 8 pages, `IEEEbib.bst`) with static vector figures and an Artifact Appendix; archive on arXiv (`cs.SD` / `eess.AS`) and Zenodo (DOI).
2. **Web Lab Note (`website/research/<slug>.html`):** Publish an interactive HTML note (Distill-style: KaTeX math, live audio/visual `embeds/` or A/B players, collapsible proof drawers, `TechArticle` JSON-LD, `<link rel="alternate" type="application/json">` to a copy of `data/` under `website/research/<slug>/data/`, and pinned commit SHA / DOI). Page shell from `website/blog/`; modes per `docs/brand/BRAND.md` 2.2 (Paper for reading); `tools/brand/brand_lint.py` before shipping.
