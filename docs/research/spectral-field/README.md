---
title: "Spectral Field: A Lock-Free STFT Bin-Kernel VM with Automatic Phase-Mode Selection"
slug: "spectral-field"
status: "pre-registered"
owner: "namansoni"
date: "2026-11-21"
prior_art: "pending: docs/prior-art/<date>-spectral-field.md not written yet (named so far: Laroche & Dolson 1999)"
commit_sha: "pending-measurement"
---

# Spectral Field: A Lock-Free STFT Bin-Kernel VM with Automatic Phase-Mode Selection

## 1. Problem & Signal / Execution Model
- **Problem:** A user-programmable spectral effect needs FFT/IFFT framing, phase handling and cross-bin reads, and each has to run in real time without audio-thread allocations. Spectral Field asks whether one per-bin register VM can hide the framing and pick the phase mode automatically. How existing environments handle this is for the prior-art survey to establish (not written yet); this README makes no claim about them.
- **Model:**
  - Streaming STFT at $f_s = 48\text{ kHz}$ (the only rate the self-test runs; other rates untested), FFT sizes $N \in \{512, 1024, 2048, 4096, 8192\}$ (`kMinLog2 = 9`, `kMaxLog2 = 13`), Hann analysis/synthesis window, 75% overlap ($\text{hop} = N / 4$, $\sum w^2 = 1.5$), zero-phase framing, latency $= N$ samples (`SpectralFieldEngine`).
  - Execution: Lock-free, allocation-free `SampleProgram` register VM (`SampleOp::{LoadBin, LoadBins, BinAt}`) executed once per bin ($N/2 + 1$ bins) on the audio thread (`Prepare()` pre-allocates on the main thread).
- **Assumptions:** `Float32` audio buffers and bin registers, round-to-nearest-even, stereo max (`kMaxChannels = 2`), real-time deadline $< 10.67\text{ ms}$ per 512-sample block at $48\text{ kHz}$.

## 2. Formal Definitions & Theoretical Bounds
- **Definitions:**
  - Let $W$ be the kernel's written-register set inferred statically from `SampleProgram`.
  - **Synthesis-mode rule:** If `phase` $\in W$, rebuild bin $k$ from $(\text{mag}_k, \text{phase}_k)$ (**raw mode**). Otherwise, select **vocoder mode**: bins with unmodified `freq` preserve analysis phase (exact reconstruction for magnitude-only kernels); modified-`freq` bins advance synthesis phase by $2\pi \cdot \text{freq}_k \cdot \text{hop} / f_s$ and propagate peak phases across valley-to-valley regions (Laroche & Dolson, 1999).
  - **Cross-bin & temporal state:** `BinAt` reads the previous frame's immutable analysis bank; `state.at(k)` reads the previous frame's double-buffered state bank, guaranteeing order-independent bin execution.
- **Proposition (Identity Reconstruction):** When `phase` $\notin W$ and `freq` is unmodified, Hann$^2$ 75% OLA reconstructs the input signal up to `Float32` round-off (measured in `SpectralFieldTests.cpp`).
- **Floating-Point & Numerical Caveats:** `Float32` FFT/IFFT accumulation and Hann$^2$ normalization introduce a round-off floor that has not been measured yet (the current self-test only gates identity error below $-90\text{ dB}$; Phase 0 records the measured value); extreme bin-frequency shifts near DC/Nyquist wrap modulo $f_s / 2$.

## 3. Pre-Registered Hypotheses & Kill Criteria
*(Locked before running `data/` benchmarks. Amended once before any measurement, see "Pre-registration log".)*

**What the self-test covers today versus H1/H2.** `SpectralFieldTests.cpp` runs at 48 kHz only. Its identity check uses noise at $N$ = 512, 2048 and 8192 (not 1024 or 4096, no sine or impulse) and passes at error below $-90\text{ dB}$; it does not record the measured value. Phase 0 must extend it to the full H1 grid and write the measured SNR to `data/` before H1 is scored.

| ID | Hypothesis | Metric & Statistical Bar | Kill Criterion | Held-Out Validation Plan |
|---|---|---|---|---|
| H1 | Automatic vocoder mode preserves exact reconstruction for mag-only / identity kernels | Identity round-trip SNR $\ge 95\text{ dB}$ at every $N \in \{512, 1024, 2048, 4096, 8192\}$, for sine, noise and impulse, in both raw and vocoder mode | SNR $< 90\text{ dB}$ at any supported $N$ | External dry stems (at least 10, not chosen by the author) |
| H2 | Per-bin register VM meets real-time budget with zero audio-thread allocations | 0 heap allocations in `ProcessBlock`; $\mu\text{s}$/block $< 25\%$ of $10.67\text{ ms}$ at $N = 2048, 8192$ | Any allocation in `ProcessBlock`, or block time $> 50\%$ budget | Held-out community Field presets ($N \ge 10$) |
| H3 | Identity phase-locking reduces transient smearing vs. raw phase-advance on pitch-shifted stems | Spectral-flux correlation between the shifted output and the dry stem: per-stem paired difference (locked minus raw), median > 0 with Wilcoxon signed-rank $p < 0.05$, $N \ge 10$ stems, octave-up shift | Median difference $\le 0$ or $p \ge 0.05$ | External musical transient stems (drums, plucked), not chosen by the author. A listening test (MUSHRA) is a possible follow-up, not part of H3 |

## 4. Phases & Gate Checklist
- [x] **Implementation:** `SpectralFieldEngine`, `AudioSpectralFieldNode`, `SpectralFieldTests.cpp` (`INFINITE_SPECTRALFIELDTEST=1`).
- [ ] **Phase 0 (Measure / Baseline):** Export CSV benchmarks (`data/spectral-bench.csv`) + `data/provenance.json` from `SpectralFieldTests.cpp`.
- [ ] **Phase 1 (Held-Out & Ablation):** Evaluate H1–H3 on external dry stems and community kernels vs. `pfft~`/Csound `pvs` baselines.
- [ ] **Phase 2 (Publish):** Complete `ARTIFACT.md`, compile DAFx LaTeX (`paper/`), publish interactive Web Lab Note (`website/research/spectral-field.html`).

## 5. Results & Observations
*(Pending CSV export from `SpectralFieldTests.cpp`.)*

## 6. Threats to Validity & Caveats
- **Internal validity:** Current `SpectralFieldTests.cpp` uses synthetic 48 kHz signals (`Lcg` noise and sine) and single-machine Apple Silicon timing. The zero-allocation check counts the macOS default malloc zone (`malloc_zone_statistics`): it sees nothing on Windows or Linux, and misses allocations from other zones.
- **External validity:** Community-authored arbitrary `BinAt` feedback kernels may stress cache locality differently than shipped presets.
- **Construct validity:** Identity SNR and $\mu\text{s}$/block do not measure perceptual transient quality on pitch-shifted material; H3 requires listening/transient-metric validation.

## 7. Pre-registration log
- 2026-11-21, commit 68b0b7c3: hypotheses first committed as written.
- 2026-11-21, before any measurement: H1 grid spelled out (every $N$, three signals, both modes); H3 narrowed to one metric (spectral-flux correlation, Wilcoxon) instead of "flux or MUSHRA"; unmeasured claims (other sample rates, round-off floor, other environments) removed. No data existed when these changes were made.
