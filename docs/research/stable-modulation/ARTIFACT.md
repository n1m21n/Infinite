# Artifact Evaluation & Reproducibility: Proven-stable modulation

Follows **ACM Artifact Review and Badging v1.1**. Badges sought: Artifacts Available, Results Reproduced.

## 1. Provenance
- **Git Commit SHA:** `ccf102e151b1ecb00dc1743b2ae4e42ef4b25026` (produces every phase 0-4 output; per-phase commits in `data/provenance.json`)
- **Archival DOI (on release):** none yet; no paper submitted
- **OS / Hardware:** macOS 27.2.1 arm64, Apple M2, 8 GB RAM
- **Toolchain & Build Flags:** Python 3.12.13 venv at `~/.venvs/stable-modulation` (numpy 2.5.5, scipy 1.18.1, cvxpy 1.9.3, Clarabel 0.11.1; `scripts/requirements.txt`). The Float32 and A(θ) dump steps need one dev build of Infinite: `python3 scripts/ibuild.py build --mode dev` (Apple clang 21.0.0, CMake Debug, arm64)
- **Determinism / Seeds:** G seed 20261121, B seed 20261123 (`scripts/gen_kernels.py`); oracle searches use fixed numpy seeds. The LMI solver is deterministic on one machine; across machines P may differ in its last digits, and a verdict can only move for a kernel at the very edge of its box

## 2. Quick Verification ("Kick the Tires", about 2 minutes)
```bash
./scripts/kick-the-tires.sh
```
Reruns the oracle against McClellan's counterexamples (B3), the certifier at the published bounds (H2), and the locked scorer on all 16 L kernels plus 12 G and 12 B kernels (H1, H3), then compares every verdict with `data/`. With an Infinite binary it also re-checks the L certificates in Float32. Prints `0 failed` and exits 0 when everything matches; 51 checks, 100 s on the M2.

## 3. Full Reproduction (about 15 minutes)
```bash
./scripts/run_all.sh
```
Copies `scripts/` and `corpus/` to a temp folder, runs every phase there, and copies the outputs to `data-rerun/` (never over `data/`). Set `INFINITE_BIN` to a dev build for the dump and Float32 steps; without one they are skipped.

## 4. Claim-to-Artifact Matrix

| Claim | Command | Raw data | Runtime | Tolerance |
|---|---|---|---|---|
| B3: oracle finds Moog unstable from k = 2.8856 and diode from 8.9009, inside the paper's (2.88, 2.89] and (8.90, 8.91] (section 10) | `scripts/kick-the-tires.sh` (runs `scripts/phase0_b3.py`) | `data/phase0-b3.json`, `.log` | ~60 s | ±0.001 |
| H2: Moog k ≤ 2.8848, diode ≤ 8.8965, Sallen-Key ≤ 1.9990, SVF both boxes; all pass (section 10) | `scripts/phase1_h2.py` (bounds only: `scripts/kick-the-tires.sh`) | `data/phase1-h2.json`, `.log` | ~2 min | ±0.001 on each bound |
| H2 spot check: 200 000 unseen points, none with ‖A‖_P ≥ 1 | `scripts/phase1_spotcheck.py` | `data/phase1-spotcheck.json` | ~1 min | exact (no point ≥ 1) |
| B1, B2 on G, phase 1 baseline: 76 witnesses, B1a 54/76, B1b 15/76 (kept unchanged as recorded) | `scripts/phase1_b12.py` | `data/phase1-b12.json`, `.log` | ~50 s | exact counts |
| Phase 2: interpreter A(θ) matches Python within 1.7e-6 on 232 compiled kernels; 75 G and 109 B kernels exceed the 128-register cap | `scripts/emit_field.py corpus/field`, then `INFINITE_FIELDSTABDUMP=corpus/field INFINITE_FIELDSTABDUMP_OUT=<dir> Infinite`, then `scripts/phase2_compare.py <dir> [out.json]` | `data/phase2-compare.json`, `data/phase4-B-compare.json` | ~5 s | max abs error ≤ 1e-5; exact compile counts |
| H1 on L + G: 0 of 89 witness kernels certified; H3 on G: 111 of 120 (92.5%) (section 10) | `scripts/phase3_score.py L,G --jobs 6` | `data/phase3-L-G.json`, `.log` | ~85 s | exact counts |
| Float32 on L + G: G Field 77/77 proved; L 4/5 hold, 3/5 proved; 31/34 Python bounds proved | same, with `INFINITE_BIN` set | `data/phase3-L-G.json` (`f32` per row) | included | exact counts; rises ±1e-7 |
| Corrected baseline: G witnesses 80, B1a 58/80, B1b 17/80; B: 85, 77/85, 28/85 | `scripts/phase5_figdata.py` | `data/phase5-frozen.json` | ~10 s | exact counts |
| H4 fails as registered: 0% wider for Moog and diode; exploratory (cutoff ≥ 20 Hz) Moog 3.50 (+21%), diode 12.50 (+41%) (section 10) | `scripts/phase3_h4.py` | `data/phase3-h4.json` | ~3 min | ±0.01 on each bound |
| Held-out (blind set B, scored once after the lock at `f9180894`): H1 0 of 85; H3 102 of 115 (88.7%); Float32 55/55 Field proved, 41/47 bounds (section 10) | `scripts/gen_kernels.py B`, `scripts/emit_field.py corpus/field`, the dump, then `scripts/phase3_score.py B --jobs 6` | `data/phase4-B.json`, `.log`, `data/phase4-B-compare.json` | ~2 min | exact counts |
| The trap figure: Moog at g = 1.3764, k = 0, 2.89 alternating grows to 52 567 in 20 000 samples; frozen and 0, 2.85 decay | `scripts/phase5_figdata.py` | `data/phase5-trap.json` | ~1 s | ±1e-6 relative |
| Every figure and table in the Lab Note | `python3 tools/research/build_lab_note.py stable-modulation` (from the repo root) | `data/*.json` via `web/figures.py` | ~10 s | regenerated, never edited |
