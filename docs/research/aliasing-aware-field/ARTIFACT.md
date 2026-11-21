# Artifact Evaluation & Reproducibility: Aliasing-aware Field

Follows **ACM Artifact Review and Badging v1.1**. Badges sought: Artifacts Available, Results Reproduced.

## 1. Provenance
- **Git Commit SHA:** `cb85f93235ffc3b8885efd846e176da8dca6ce3c` (reproduces every output; first-run commits per file in `data/provenance.json`)
- **Archival DOI (on release):** none yet; no paper submitted
- **OS / Hardware:** macOS 27.2.1 arm64, Apple M2, 8 GB RAM
- **Toolchain & Build Flags:** Apple clang 21.0.0; app built with `python3 scripts/ibuild.py build --mode dev` (CMake Debug, arm64); cost bench `clang++ -std=c++20 -O2`
- **Determinism / Seeds:** no RNG. The stimulus is a fixed sine; the analysis and every render are deterministic, so re-runs match to float rounding.

## 2. Quick Verification ("Kick the Tires", under a minute)
```bash
./scripts/kick-the-tires.sh
```
Runs phase 0 and 1 (47 kernels at 1x to 32x, with predictions) and writes `data-rerun/phase1-predictions.csv`.

## 3. Full Reproduction (a few minutes)
```bash
./scripts/run_all.sh
```
Writes every dataset under `data-rerun/` (never over `data/`) and prints the H2 verdict lines. The H2 cost line is passed in; `tools/field_rate_cost_bench.cpp` re-measures it on another machine (the command is in `run_all.sh`).

## 4. Claim-to-Artifact Matrix

| Claim | Command | Raw data | Runtime | Tolerance |
|---|---|---|---|---|
| Phase 0: slope per doubling by smoothness class; factor each kernel needs; shipping presets alias at 1x (section 8) | `scripts/kick-the-tires.sh` | `data/phase0-baseline.csv` (columns `alias_db` of `data/phase1-predictions.csv`) | ~20 s | ±0.1 dB per row |
| H1 on the calibration corpus: Spearman ρ 0.994, median abs error 0.0 dB, factor exact 43/43 (section 9) | `scripts/kick-the-tires.sh` | `data/phase1-predictions.csv` | ~20 s | ρ ±0.002; factors exact |
| H2 killed: aggregate saving 17.4%, median 19.5% (section 10) | `scripts/run_all.sh` | `data/phase2-h2-run.log` | a few minutes | ±0.5 percentage points at the same cost line |
| tanh x3: staged -43.0 dB vs fixed 8x -46.1 dB; ideal resamplers -74.0 dB (section 10) | `scripts/run_all.sh` (dump on) | `data/phase2-tanh3-dump.log` | included | ±0.5 dB |
| H2 saving over 10 independently measured cost lines: mean 15.8%, 95% CI 15.6 to 15.9 (t, n=10); tanh x3 never beats 8x (section 10) | `OUT=data-rerun python3 scripts/cost_repeats.py <bench log>` | `data/phase2-cost-bench.log`, `data/phase2-cost-repeats.csv`, `data/phase2-cost-repeats/run-NN.log` | ~2 min per run | each row ±0.5 pp at its own cost line |
| Listening clips for tanh x3 (1x -12.6, fixed 8x -46.1, staged -43.0, 32x -136.0 dB) | `INFINITE_FIELDRATE_AUDIO=<dir>` on the H2 run, then `python3 scripts/make_audio.py <dir> <log>` | `data/phase2-audio/*.f32`, `renders.log` (WAVs are derived) | included | ±0.5 dB; f32 bit-exact on the same build |
| Corner sweep: factor changes on 31 of 38 kernels (section 10) | `scripts/run_all.sh` | `data/phase2-run1-debugcost-oldscoring.log` (`FIELDRATE corner` lines) | included | exact |
| Held-out: 2 of 18 presets accepted, worst error 0.1 dB (section 11) | `scripts/run_all.sh` | `data/phase2-run1-debugcost-oldscoring.log` (`FIELDRATE held` lines) | included | exact counts, ±0.1 dB |
| Phase 4 coverage: 272 held-out kernels, 233 scored; L 18 of 32, G 215 of 240 (section 14) | `INFINITE_FIELDHELDOUT=$PWD/corpus/heldout INFINITE_FIELDHELDOUT_CSV=$PWD/data-rerun/phase4-heldout.csv ../../../build-dev/Infinite.app/Contents/MacOS/Infinite` (from the study folder), then `python3 scripts/score_heldout.py data-rerun/phase4-heldout.csv` | `data/phase4-heldout.{csv,log}`, `data/phase4-score.log` | ~5 min | exact counts |
| H1b on L: n=7 informative, ρ 1.000, median 0.0 dB, PASS; on G: n=167, ρ 0.994, median 0.0 dB, PASS (section 14) | same | `data/phase4-scored.csv`, `data/phase4-score.log` | included | ρ ±0.002; median ±0.1 dB |
| H3: exact factor 174/174 vs rule A 8, rule B 37; without the 68 "none" kernels 106/106 vs 8 and 9 (section 14) | same | `data/phase4-scored.csv` | included | exact counts |
| No measured-above-predicted gap above −125.9 dB; no lemma, so no soundness claim (section 14) | same | `data/phase4-heldout.log` | included | ±1 dB |
| H2 break-even: 25% only for a 2x pair under 0.09 instructions; tanh x3 never beats 8x (section 14) | `OUT=$PWD/data-rerun python3 scripts/break_even.py` | `data/phase4-break-even.csv`, `data/phase4-break-even.stdout.log`, `data/phase4-break-even/s-*.log` | ~2 min per s, ~20 min in all | ±0.5 pp per row |
| Phase 5, H4: delay coverage 94 of 97 (L 11/12, G 25/25, D 58/60); ρ 0.937, 0.991, 0.988; median 0.0 dB; exact factor 93/94 vs rule A 9, rule B 38 (section 16) | `INFINITE_FIELDHELDOUT=$PWD/corpus/heldout INFINITE_FIELDHELDOUT_CSV=$PWD/data-rerun/phase5-heldout.csv ../../../build-dev/Infinite.app/Contents/MacOS/Infinite` (from the study folder), then `python3 scripts/score_heldout.py data-rerun/phase5-heldout.csv` | `data/phase5-heldout.{csv,log}`, `data/phase5-scored.csv`, `data/phase5-score.log` | ~6 min | exact counts; ρ ±0.002 |
| Phase 5 no regression: 0 of 233 phase-4 kernels change (±0.01 dB, factor); calibration predictions byte-identical (section 16) | the run above, then compare with `data/phase4-heldout.csv`; `scripts/kick-the-tires.sh` and compare with `data/phase1-predictions.csv` | `data/phase4-heldout.csv`, `data/phase1-predictions.csv` | ~6 min | exact |
| Phase 5: 3 gaps above the float32 floor, all below −80 dB; 18 delay kernels blow up with factor "none" on both sides (section 16) | same | `data/phase5-heldout.csv` | included | ±1 dB |
| Run 1 history (Debug costs, old scoring: 0.8%) | not reproducible at this commit (scoring since replaced); kept as recorded | `data/phase2-run1-debugcost-oldscoring.{log,csv}` | - | - |

The corner sweep and held-out sections do not depend on the cost model or the plan scoring, so their run-1 output still stands; `run_all.sh` regenerates them.
