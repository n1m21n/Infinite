# Artifact Evaluation & Reproducibility Roadmap — Spectral Field

Follows **ACM Artifact Review and Badging v1.1** (*Artifacts Available, Evaluated — Functional/Reusable, Results Reproduced*).

## 1. Provenance
- **Git Commit SHA:** `pending-measurement` *(pin 40-char SHA when running Phase 0 CSV export)*
- **Archival DOI (on release):** `pending`
- **OS / Hardware:** macOS arm64 (Apple Silicon)
- **Toolchain & Build Flags:** CMake Release / Dev build (`Infinite` headless test binary)
- **Determinism / Seeds:** Fixed `Lcg` seed `12345u` in `src/app/selftest/SpectralFieldTests.cpp`

## 2. Quick Verification ("Kick the Tires", < 5 min)
```bash
./scripts/kick-the-tires.sh
```
Runs `INFINITE_SPECTRALFIELDTEST=1` headless checks (identity round-trip, zero-allocation `ProcessBlock`, failed-compile recovery, latency/Mix, and 2048/8192 timing).

## 3. Full Reproduction
```bash
./scripts/run_all.sh
```

## 4. Claim-to-Artifact Matrix

| Paper Claim / Figure / Table | Generating Script / Command | Raw Data Output | Expected Runtime | Numerical Tolerance |
|---|---|---|---|---|
| H1 (Identity Round-Trip SNR) | `INFINITE_SPECTRALFIELDTEST=1` | `data/spectral-bench.csv` *(planned)* | < 10 s | $\pm 0.5\text{ dB}$ |
| H2 (Zero-Alloc & $\mu\text{s}$/block at 2048/8192) | `INFINITE_SPECTRALFIELDTEST=1` | `data/spectral-bench.csv` *(planned)* | < 10 s | $\pm 10\%$ CPU variance |
| H3 (Phase-Locking Transient Ablation) | `scripts/run_all.sh --ablation` *(planned)* | `data/phase-locking.csv` *(planned)* | ~30 s | $p < 0.05$ |
