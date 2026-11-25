# Artifact Evaluation & Reproducibility: Saturating loops

Follows **ACM Artifact Review and Badging v1.1**. Badges sought: Artifacts Available, Results Reproduced.

## 1. Provenance
- **Git Commit SHA:** see `data/provenance.json` (`commit_sha` produces every phase 0-4 output; per-phase commits listed there)
- **Archival DOI (on release):** none yet; no paper submitted
- **OS / Hardware:** macOS 27.2.1 arm64, Apple M2, 8 GB RAM
- **Toolchain & Build Flags:** Python 3.12.13 venv at `~/.venvs/stable-modulation`, shared with Report 2 (numpy 2.5.5, scipy 1.18.1, cvxpy 1.9.3, Clarabel 0.11.1; `scripts/requirements.txt`). Report 2's certifier is imported by path from `docs/research/stable-modulation/scripts`. The Field dump needs one dev build of Infinite: `python3 scripts/ibuild.py build --mode dev` (Apple clang 21.0.0, CMake Debug, arm64)
- **Determinism / Seeds:** G seed 20261122, B seed 20261124 (`scripts/gen_sat.py`; B generated after the scorer lock at `2faf6290`); oracle searches and the two-start test use fixed numpy seeds. The LMI solver is deterministic on one machine; across machines P may differ in its last digits, and a verdict can only move for a kernel at the very edge of its box

## 2. Quick Verification ("Kick the Tires", about 25 seconds)
```bash
./scripts/kick-the-tires.sh
```
Reruns the locked phase 1 and phase 3 code on the 24 fastest G kernels, the two windup kernels (s122-067, s122-124) and the 12 fastest B kernels, and compares B4, T1, G1, the witness kind, G2 and the two-start test with `data/`. With an Infinite binary it also re-emits the 11 literature filters and four gate negatives as Field, dumps them with the saturators cut, and checks that L matches and that the three out-of-library loops are rejected. Prints `0 failed` and exits 0 when everything matches; 215 checks, 21 s on the M2.

## 3. Full Reproduction (several hours on the M2)
```bash
./scripts/run_all.sh
```
Copies `scripts/` and `corpus/` to a temp folder, runs every phase there, and copies the outputs to `data-rerun/` (never over `data/`). Needs an Infinite dev build (`INFINITE_BIN`, else `build-dev/` or `build/` at the repo root). Phase 1 on G and B is the long step; `JOBS` sets the worker count (default 4).

## 4. Claim-to-Artifact Matrix

| Claim | Command | Raw data | Runtime | Tolerance |
|---|---|---|---|---|
| Oracle check: on the tanh Moog the first witness lies within 0.001 of the frozen self-oscillation limit at every ceiling (B3) | `scripts/phase0_b3.py` | `data/phase0-b3.json` | ~5 min | ±0.001 |
| L: witness on 9 of 11; G1 certifies onepole-tanh and svf-clip, neither with a witness; B4 and the frozen check on L | `scripts/phase0_L.py`, `scripts/phase0_L_ceil.py` | `data/phase0-L.json` | ~10 min | exact verdicts |
| H2 fails as registered: T3 certifies k ≤ 1.1406 over the port's whole box g ≤ 0.785 (first witness 1.289); 5/3 certified for ceilings up to g = 0.5237 (section 10) | `scripts/phase0_t3.py`, `scripts/phase0_moog.py`, `scripts/phase0_L.py` (whole box) | `data/phase0-t3.json`, `data/phase0-moog.json`, `data/phase0-L.json` | ~15 min | ±0.001 on each bound |
| Phase 1 on G: 73 witnesses (25 grow, 48 stick); B1a 64/73, B1b 14/73; B4 certifies 2 that blow up | `scripts/phase1_G.py 4` | `data/phase1-G.json`, `.log` | ~3 h | exact counts |
| Phase 2: interpreter A, B, C, D with saturators cut match Python within 2.9e-7 on L 11/11 and G 127/127, and within 2.5e-7 on B 81/81 (119 over the register cap); gate rejects 3 out-of-library loops | `scripts/emit_sat.py corpus/field`, then `INFINITE_FIELDSTABDUMP=corpus/field INFINITE_FIELDSTABDUMP_OUT=<dir> INFINITE_FIELDSTABDUMP_SAT=1 Infinite`, then `scripts/phase2_compare.py <dir> [out.json]` | `data/phase2-compare.json`, `data/phase4-B-compare.json` | ~10 s | max abs error ≤ 1e-5; exact compile counts |
| H1 on G: 0 of 73 witness kernels certified; H3 on G: 94 of 127 (74.0%); H4: 94/94 with G2, 0 two-start failures; Float32 80/94 (section 10) | `scripts/phase3_score.py G 4` | `data/phase3-G.json`, `.log` | ~30 min | exact counts |
| Held-out (blind set B, scored once after the lock at `2faf6290`): H1 0 of 87 witness kernels certified; **H3 64 of 113 (56.6%), fails the 70% bar**; H4 58/64 with G2, 0 two-start failures; Float32 52/64; B4 certifies 5 witness kernels (section 10) | `scripts/gen_sat.py B`, `scripts/emit_sat.py corpus/field`, the dump, then `scripts/phase3_score.py B 4` | `data/phase1-B.json`, `data/phase3-B.json`, `data/phase4-B.log`, `data/phase4-B-compare.json` | ~3 h | exact counts |
| Why B falls short (exploratory, after the score): 10 of 14 B kernels with an infeasible T1 LMI (3 of 4 on G) have an unstable sector corner at a fixed knob setting | `scripts/phase5_figdata.py` | `data/phase5-corners.json` | ~1 min | exact counts |
| The windup figure: s122-067 at its witness knobs, no input, start 100 grows to 1.7e36 in 20 000 samples; starts 1 and 10 decay; s122-124 likewise | `scripts/phase5_figdata.py` | `data/phase5-windup.json` | ~5 s | ±1e-6 relative |
| Every figure and table in the Lab Note | `python3 tools/research/build_lab_note.py saturating-loops` (from the repo root) | `data/*.json` via `web/figures.py` | ~10 s | regenerated, never edited |
