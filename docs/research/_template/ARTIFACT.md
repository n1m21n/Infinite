# Artifact Evaluation & Reproducibility Roadmap

Follows **ACM Artifact Review and Badging v1.1** (*Artifacts Available, Evaluated — Functional/Reusable, Results Reproduced*).

## 1. Provenance
- **Git Commit SHA:** `<full-40-char-sha>`
- **Archival DOI (on release):** `<zenodo-doi-or-pending>`
- **OS / Hardware:** `<e.g., macOS 15.x arm64 / Apple M-series, 16 GB RAM>`
- **Toolchain & Build Flags:** `<compiler, version, -O3 / IEEE-strict>`
- **Determinism / Seeds:** `<global RNG seed, deterministic flags>`

## 2. Quick Verification ("Kick the Tires", < 5 min)
```bash
./scripts/kick-the-tires.sh
```
Expected outcome: verifies build, environment, and baseline smoke test within tolerance.

## 3. Full Reproduction
```bash
./scripts/run_all.sh
python3 scripts/plot_figures.py
python3 scripts/build_tables.py
```

## 4. Claim-to-Artifact Matrix

| Paper Claim / Figure / Table | Generating Script / Command | Raw Data Output | Expected Runtime | Numerical Tolerance |
|---|---|---|---|---|
| Table 1 (Baseline) | `scripts/run_all.sh --phase 0` | `data/phase0-baseline.csv` | ~20 s | $\pm 0.5$ dB |
| Figure 2 (H1 Correlation) | `scripts/plot_figures.py --fig 2` | `data/phase1-predictions.csv` | ~5 s | Exact from CSV |
| Table 2 (H2 CPU Speedup) | `scripts/run_all.sh --phase 2` | `data/phase2-bench.csv` | ~60 s | $\pm 5\%$ CPU variance |
