# BCI strategy: from preset mandala to a learnable biological instrument

Branch `feature/bci-architecture` · written 2026-11-19 · sources: the 3 plan docs
(v1 verb taxonomy, v2 12-stage plan, bio→fundamental direction), the worktree code,
5 research briefs (sources listed at the end).

Sections 1–7 need sign-off before any build.

---

## 1. Goal

A headset wearer gets a visible and audible result **they can learn to control** on the
first try, built from raw per-region signals. There are no preset verbs in the path.

## 2. Problem

What exists today (verified in code):

```
4 sensors ──► averaged into 1 PSD ──► 4 mood axes ──► 11 weighted verbs ──► hand-written mandala
 (BciDspEngine.cpp:403  avgPsd += p*0.25)         (0.55*A + 0.45*H ...)     (bci_symphonic_patch.inf)
```

| Gap | Evidence |
|---|---|
| Regions erased | one averaged spectrum; per-channel PSD is computed, then thrown away |
| Verbs rest on the weakest signals | FAA/valence and arousal indices have low Muse test-retest reliability (Ratti 2017, n=5; Cannard 2021) |
| Coherence is contaminated | the code uses PLV, and zero-lag volume conduction inflates PLV; wPLI/ImCoh ignore it (Vinck 2011) |
| The output is our preset, not the brain | the frame is the same orange 5-petal flower whatever the input |
| Real-data proof = 1 person | PhysioNet S001 only; all other tests run against our own SyntheticCortex |
| Nothing committed | 22 modified + ~10 new files, all uncommitted in the worktree |

## 3. Scope

| In | Out (parked, with reason) |
|---|---|
| Per-region DSP (4 channels kept separate) + relationships | Attention steering by frequency tagging (SSVEP/c-VEP needs occipital electrodes, which Muse lacks) |
| 3 direct pins: Image, Audio, Geometry | Mind-reading claims (images, words, memories) |
| Batch validation on public data (100+ people) | Valence/emotion as a headline feature (unreliable on Muse) |
| One starter instrument patch + Brain Sculpt on raw features | Dyadic hyperscanning (needs 2 headsets; later) |
| Muse hardware test once a headset is available | PPG heart rate / HRV (no Muse validation found) |

## 4. Existing constraints

- M2 8 GB laptop: builds go through `scripts/ibuild.py`, one at a time, at the end of the plan.
- No headset on hand: everything is proven on replayed public EEG first.
- Muse physics: 4 dry sensors, forehead + behind the ears, low SNR. TP9/TP10 alpha is real but
  much weaker than on gel caps (Kleeva 2024: Muse vs gel reference r = 0.52–0.61).
- The worker thread already runs at 20/64 Hz off the audio and render threads. Keep it that way.
- New pins on Brain Source count as enhancements to an unshipped node. New nodes need owner approval.

## 5. Hypothesis

> Signals that are **reliable and learnable**, mapped **directly** (raw per region → pixel,
> pitch, vertex), give more felt agency than signals that are interpreted and averaged.

Supported by:

| Finding | What it implies |
|---|---|
| Lucier 1965 and Park 2013 felt like instruments because alpha is a *skill* (close your eyes, calm down) | Lead with alpha, not valence |
| Miranda's SSVEP music: coarse binary control felt playable, fine control did not | Gate first, then continuous |
| Untrained listeners heard seizures in audified EEG (~82–85% specificity) | Direct audification can be read by ear |
| Blink detection is above 98% accurate on Muse-class data (Agarwal 2020) | Blink is the reliable discrete gesture |
| Our S001 sheet: TP10 alpha ×4.63 vs averaged ×2.78 | Averaging dilutes the strongest region |

Reliability ranking from the research: **blink > alpha (eyes closed) > jaw/brow EMG > FAA/ERPs > SSVEP/ASSR/PPG**.
The 11-verb layer was built mostly on the bottom of that list.

## 6. Baseline (recorded once, 2026-11-19; not re-measured later)

| Measure | Value |
|---|---|
| Subjects validated on real EEG | 1 (PhysioNet S001) |
| Alpha 8–12 Hz, eyes closed / open, raw band power | AF7 ×2.33 · AF8 ×2.68 · TP9 ×2.58 · TP10 ×4.63 · **avg ×2.78** |
| Engine output on that data (bio→fundamental doc §5) | averaged alpha 0.233 → 0.402, coherence 0.474 → 0.535 |
| Output picture | the same preset mandala in both states (`bci_symphonic_frame.png`) |
| Direct pins | none (`GetOutputTexture() → 0`, no audio or geometry) |
| Coherence metric | PLV (volume-conduction biased) |

Proof sheet: `docs/plans/bci/proof/s001_region_sheet.png` (tool: `tools/bci/region_sheet.py`).
The eyes-closed 8–12 Hz stripe is visible on all 4 rows and strongest on TP10.

## 7. Success metrics

| # | Metric | Bar |
|---|---|---|
| M1 | Eyes-closed alpha rise detected on ≥1 region (closed/open > 1.5, 1/f-corrected) | ≥ 80% of the PhysioNet 109-subject baseline runs |
| M2 | Per-region beats average: best-region effect size > averaged effect size | in ≥ 70% of subjects |
| M3 | **Blind eye test**: from a 10 s render (Image pin + Audio pin), a viewer tells eyes-open from eyes-closed | ≥ 9 / 10 clips |
| M4 | Blink → toggle on replayed annotated blinks (EEG-Eyeblinks) | ≥ 95% hit, ≤ 1 false toggle/min |
| M5 | Zero presets in the direct path: no hand-tuned weights between raw signal and pin | code review |
| M6 | Real-time cost of the per-region DSP | < 2 ms per update on the M2 |

M3 is the "tangible" bar: if a stranger can't see or hear the difference, it isn't an instrument.

---

## 8. Methodology

```
             ┌──────────── BLOCK A: honest regions ────────────┐
 raw 4ch ──► │ per-channel Welch 2 s  → robust 1/f fit (3–40 Hz)│
 256 Hz      │ per-channel α/β above fit, bursts, Hjorth        │
             │ relations: front–ear, left–right, wPLI (6 pairs) │
             │ state age / dwell per region                     │
             └───────────────┬─────────────────────────────────┘
                             │
             ┌───────────────▼──── BLOCK B: direct pins ───────┐
             │ Image  : 4-row spectrogram + waveform texture   │
             │ Audio  : 4 voices, EEG × 32 (10 Hz α → 320 Hz)   │
             │ Geom   : (L−R, front−ear, sum) xyz + delay embed │
             └───────────────┬─────────────────────────────────┘
                             │
             ┌───────────────▼──── BLOCK C: the instrument ────┐
             │ "Alpha Bell" starter: close eyes → tone + bloom │
             │ blink → toggle · Brain Sculpt on raw features   │
             │ Muse hardware pass                              │
             └─────────────────────────────────────────────────┘
```

Validation loop (tool-first, Python mirror first, then C++):

| Step | Tool | Data |
|---|---|---|
| Per-region math in Python, batch over many subjects | `tools/bci/batch_validate.py` (new) | PhysioNet eegmmidb R01/R02 × 109 (~270 MB) |
| Cross-check on Muse hardware recordings | same | HEROIC (Muse 2, eyes open/closed, 86 MB) |
| Temporal + occipital comparison | same | GIPSA alpha (20 subjects, 105 MB) |
| Blink gesture | same | EEG-Eyeblinks (Muse/OpenBCI, 2,300 labelled blinks) |
| C++ engine matches Python within 5% | `INFINITE_BCI_CSV_*` selftest, extended to a folder | same CSVs |
| Picture + sound proof | headless render of the starter patch on replay | sheets + 10 s clips |

DSP choices (from the research brief):

| Item | Choice | Why |
|---|---|---|
| Aperiodic | robust log-log line fit, 3–40 Hz, iteratively drop peaks, knee off | O(N); specparam is too costly every update |
| Connectivity | wPLI + imaginary coherence; keep PLV only as a reference | volume conduction |
| Phase (later) | causal bandpass + endpoint-corrected Hilbert | 1 sample = 14° at 10 Hz; validate offline |
| Artifacts | per-channel thresholds + 20–45 Hz residual over the 1/f fit; no ICA | 4 channels is too few for ICA |
| Embedding | τ ≈ 6 samples, m = 3, on lightly filtered broadband | narrowband embeds as a plain circle |

## 9. Solution trajectories

| Trajectory | Pros | Cons | Verdict |
|---|---|---|---|
| **T1 Learnable Alpha Instrument** (per-region alpha → audified tone + spectrogram image; blink toggles) | most reliable signals; clear "close eyes → it sings" moment; descends from Lucier but direct and per-region | alpha is slow (~1 s); one skill only at first | **Lead** |
| T2 Raw mirror only (pins, no instrument) | purest "zero preset" | art history says passive mirrors read as visualisers | Part of T1, not on its own |
| T3 Brain Sculpt on raw features (shift-grab learns *your* mapping) | no preset weights; the user defines the mapping by example | needs separable states first (needs T1) | **Second**, after M1–M3 |
| T4 Frequency-tagged art (attention steering) | the real breakthrough if it worked | needs occipital electrodes; no Muse evidence | Park; revisit only with a 5th electrode (Muse aux port) |
| T5 Keep the verb layer as the main path | already built | built on unreliable signals | Keep as an optional layer only |

What-ifs:
- **M1 fails (< 80%)**: the data is too noisy at these sites. Fall back to blink + jaw EMG as the
  primary instrument and alpha as slow colour.
- **M3 fails on raw pins**: add *one* honest normalisation (per-subject z-score from calibration),
  not verbs.
- **A headset arrives early**: run Block C hardware tests in parallel with Block B.
- **Muse alpha is much weaker than on PhysioNet gel data** (likely, per Kleeva 2024): HEROIC sets
  the realistic bar, so read M1 on HEROIC before promising anything.

## 10. Benchmark vs baseline

To be filled after each block, linked to §6 and §7.

| Block | M1 | M2 | M3 | M4 | M5 | M6 |
|---|---|---|---|---|---|---|
| Baseline | 1/1 subject | n/a (avg only) | fail (same mandala) | synthetic only | fail (weights) | n/a |
| A | | | | | | |
| B | | | | | | |
| C | | | | | | |

## 11. Critique

- **The "breakthrough" claim is cut down to honest size.** The research rules out decoding attention
  or emotion on a 4-sensor headset. The breakthrough on offer is *agency*: a biological instrument
  that you learn, built on Infinite's real-time pins, where the art is raw signal rather than preset.
  That is new for creative tools, but it is not new neuroscience.
- **PhysioNet ≠ Muse.** Gel caps at 10-20 sites flatter us. HEROIC (real Muse 2) is the honest check.
- **The verb layer was over-built.** 11 weighted verbs and a 4-rule grammar rest on the least reliable
  signals. Keep them, but stop extending them until T1/T3 prove out.
- **Single-session learning is unproven.** No Muse alpha-neurofeedback effect size exists. M3 tests
  readability, not learning; learning needs a real user and a headset.
- **Uncommitted work is a risk.** The whole branch lives in the working tree.

## 12. Next steps

1. Owner signs off §1–7 (or edits the bars).
2. Commit the current worktree as the baseline snapshot.
3. Download the validation data (PhysioNet R01/R02 × 109 ≈ 270 MB, HEROIC 86 MB, GIPSA 105 MB,
   EEG-Eyeblinks): needs a yes.
4. Block A in Python (`batch_validate.py`), then C++ per-region DSP + wPLI. One build at the end of the block.
5. Block B pins, then render proof sheets and run the M3 blind test.
6. Block C starter patch; then Muse hardware when available. Ship / iterate / abandon decided here.

---

### Sources (from the research briefs; the briefs marked unverified items)

- Kleeva, Ninenko, Lebedev 2024, *Front Neurosci* 18:1326139: Muse vs gel alpha
- Ratti et al. 2017, *Front Hum Neurosci* 11:398: consumer EEG test-retest
- Krigolson et al. 2017, *Front Neurosci* 11:109: Muse ERPs (N200, reward positivity)
- Cannard, Wahbeh, Delorme 2021, IEEE BIBM: Muse FAA
- Agarwal & Sivakumar 2020, CHI: blink detection; dataset gnan.ece.gatech.edu/eeg-eyeblinks
- Väljamäe et al. 2013, ICAD: real-time EEG sonification review
- Lucier 1965 *Music for Solo Performer*; Park 2013 *Eunoia*; Dikker et al. 2017 *Curr Biol*
- Hill & Schölkopf 2012: auditory steady-state attention, too weak for single-trial BCI
- Donoghue et al. 2020 *Nat Neurosci* 23:1655: specparam
- Vinck et al. 2011 *NeuroImage* 55(4):1548: wPLI
- Cole & Voytek 2019 *J Neurophysiol* 122(2):849: waveform shape
- Mullen et al. 2015 *IEEE TBME* 62(11): ASR
- Datasets: HEROIC zenodo.org/records/11493897 · GIPSA zenodo.org/record/2348892 ·
  Muse decisions zenodo 8429739 · PhysioNet eegmmidb
