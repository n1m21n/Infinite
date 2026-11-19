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
| A | PhysioNet 72.5% (avg-only 57%) · GIPSA 100% (90%) · HEROIC Muse 50% (37.5%) | **fail as written** (best region beats avg 26–50%); reframed: per-person LDA beats avg in 73% HEROIC / 88% PhysioNet sessions | not run (Block B) | real data in selftest [7] | **pass**: weights learned per person at calibration | not measured |
| B | — | — | proxy: **0.92** with the state strip · raw image 0.74 · raw audio 0.86 (126 pairs); human blind run **10/10 pass** (picture only, strip-led) | — | **pass**: image z-scored on own eyes-open calibration, audio on own baseline amplitude | not measured |
| C | — | — | starter on synthetic: calm alpha luma 0.41 vs focus 0.13–0.16, −13.6 vs −24.2 LUFS | **fail**: with cued-blink calibration, All blinks hit 90–98% at 1.1–2.2 false/min; Intentional 61–78% at 0.6–1.8 (see C detail) | pass (no new weights; Brain Sculpt learns by example) | **pass**: 0.10 ms mean, 0.19 ms worst per update (bar 2 ms) |

### Block A detail (2026-11-19)

Eyes-closed vs open, median held-out accuracy (split halves, 6 s smoothing):

| Data | Subjects | Averaged alpha | Per-person LDA | ≥80% avg → LDA |
|---|---|---|---|---|
| HEROIC (real Muse 2) | 64 sessions | 0.683 | 0.885 | 33% → 69% |
| GIPSA (gel) | 20 | 0.917 | 1.000 | 65% → 100% |
| PhysioNet (gel) | 91 of 109 | 0.606 | 1.000 | 26% → 86% |

Engine (C++) path on HEROIC, 30 s eyes-open + 30 s eyes-closed calibration, τ 3 s:
mirror mean 0.805, ≥80% in 52% of sessions. C++ matches the Python mirror within 1.4 points
on 7 of 8 sessions (sub001-remote differs by 7.8: blanking/filters are not mirrored).
Cross-day (HEROIC, day-1 weights on later sessions): avg 0.50, LDA 0.75, LDA + median re-centre 0.855.

Tools: `tools/bci/batch_validate.py` (M1/M2, wPLI), `calib_ab.py` (avg vs LDA, cross-day),
`state_mirror.py` (C++ mirror). Engine: 16 features → shrinkage LDA → pin 15 once calibrated.

### Block B detail (2026-11-19)

Brain Source gains two pins after the 24 modulators (old patches unchanged):

| Pin | What it carries | Normalised by |
|---|---|---|
| 24 `image` | 256 × 180 scrolling spectrogram: state strip (calibrated eyes-closed score, grey until calibrated) over AF7 AF8 TP9 TP10 × 1–40 Hz, 21.3 columns/s | per-bin median/MAD of the person's eyes-open calibration (running 30 s mean/dev before) |
| 25 `audio` | 4 voices, phase = 32 × EEG phase (10 Hz alpha → 320 Hz), level = EEG envelope / baseline; AF7+TP9 left, AF8+TP10 right | median calibration amplitude per channel |

Audio path: Butterworth HP 2 + LP 40 Hz → 65-tap Hamming Hilbert (125 ms) → lock-free ring → audio thread.
`.inf` now saves the audio cable's source pin (fixes Video Source audio reloading onto pin 0 too).

M3 2AFC proxy (last 10 s open vs last 10 s closed, same session; observer = brighter 8–12 Hz / more 256–384 Hz):

| Render | Pairs right |
|---|---|
| Raw spectrogram (5–95% range) | 0.730 |
| z-scored spectrogram | 0.738 |
| Audio pin | 0.857 |
| State strip (calibrated score) | **0.92** |

Human run (owner, 2026-11-19, picture only, read the top strip first): **10 / 10 → M3 pass.**
Blind set: `~/Infinite/datasets/eeg/m3_blind/` (sheet.png + 20 wavs, seed 7);
key kept apart in `~/Infinite/datasets/eeg/m3_key.json`. Mirror: `tools/bci/direct_pins.py`.

### Block C detail (2026-11-19)

Starter patch `assets/examples/bci_alpha_bell.inf` (validates strict, laid out, commented):

```
 Brain Source ─ alpha ──► bloom intensity, Gain swell (−30..−6 dB)
      │  ├─ blink ─► Logic Gate (toggle) ─► spectrogram overlay on/off
      │  ├─ alpha ─► Brain Sculpt ─► bloom radius (learns your mapping)
      │  ├─ image ─► transform ─► Blend (screen) ─► Output
      │  └─ audio ─► Reverb ─► Gain ─────────────► Output audio
 FieldPixel bell (ring + core follow brain.alpha) ─► bloom ─┘
```

Proof (synthetic presets, headless): `proof/alpha_bell_calm.png` vs `proof/alpha_bell_focus.png`.

| Preset | Mean luma at 2 / 5 / 8 s | Loudness | Loudest band |
|---|---|---|---|
| CalmAlpha | 0.12 / 0.41 / 0.42 | −13.6 LUFS, peak −3.4 dBTP | 369 Hz |
| DeepFocus | 0.08 / 0.13 / 0.16 | −24.2 LUFS | 568 Hz |

Decisions: no Geometry pin (the starter didn't need one; revisit if a 3D patch asks).
Muse hardware pass: no headset here, so not run.

**M4: blink → toggle** (2026-11-19). EEG-EyeBlinks (Agarwal & Sivakumar 2019): OpenBCI Fp1/Fp2 at 250 Hz,
fed in as AF7/AF8. EEG-IO = 20 people, stimulus-cued blinks; VV / VR = 13 people each, natural blinks
while watching a video or reading. The replay goes through the real `BciDspEngine`, compiled
standalone (`tools/bci/blink_replay.cpp`, scored by `tools/bci/blink_m4.py`, about 20 s, no app build).
A hit is a Blink-pin edge within −0.25..+0.75 s of a label. Any other edge is a false toggle.

| Engine | Mode | IO hit / false/min | VV | VR |
|---|---|---|---|---|
| Before (positive pulses only) | All blinks | 0.80 / 2.1 | 0.76 / 3.8 | 0.62 / 2.1 |
| Before, sign flipped by hand | All blinks | 0.92 / 3.6 | 0.98 / 2.5 | 0.88 / 1.2 |
| **Now** (polarity vote + hysteresis) | All blinks | 0.87 / 2.7 | 0.91 / 2.4 | 0.89 / 1.7 |
| Polarity vote, no calibration | Intentional (default) | 0.71 / 1.8 | 0.41 / 0.7 | 0.33 / 1.0 |
| **Cued-blink calibration** | All blinks | 0.90 / 2.2 | 0.98 / 1.6 | 0.94 / 1.1 |
| **Cued-blink calibration** | Intentional (default) | 0.78 / 1.8 | 0.61 / 0.6 | 0.66 / 0.7 |

Bar: hit ≥ 0.95, false ≤ 1/min. **Fail** in every set and mode.

Engine changes kept:
1. **Blink polarity is learned.** It is a vote on the time spent past the threshold on each side, tau 60 s.
   Blinks are negative in this OpenBCI data; the old detector only saw positive pulses.
2. **Hysteresis.** A pulse now ends at 70% of the threshold. Before, one blink that dipped mid-way
   fired twice through the double-blink rule; most false toggles were these extra fires within a
   second of a real blink.

Selftest after the change: 351 passed, 0 failed.

Tried and dropped: skew-sign polarity (it swings on some people) and a 40% exit (more misses).

**Cued-blink calibration** (added after the first M4 run). Calibration gains a third phase, "Blink 5 times"
(12 s), after eyes open / closed. The largest blink-shaped pulses in it (40–350 ms wide at half height,
0.3 s apart) set the sign (majority of up to 8), the blink size and the width (median of 5). Once set,
polarity stops voting and the size stops adapting. Threshold = 0.5 × size (min 30 µV); Intentional fires on
≥ 0.8 × size, ≥ 0.8 × width, or a double blink. Replay: each person's first 5 labelled blinks are the
phase (`blink_m4.py --cal`), scored after it. Helps every set (Intentional VR 0.33 → 0.66) but still fails.
Two tuning rounds, then stopped. Selftest [10] BLINK_CALIB: 356 passed, 0 failed.

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
- **Block A: Muse is far weaker than gel.** Same pipeline: HEROIC M1 50% vs GIPSA 100%.
  Without calibration the Muse signal is close to a coin flip for half of people.
- **Block A: cross-day re-centre assumes 50/50 open/closed.** A user who keeps eyes open drifts the median.
- **Block A: one bad lead blocks calibration.** Artifact blanking is global, so a dead AF7 (HEROIC sub003)
  stops every update. The node now says so; per-channel blanking is the real fix.
- **Block A: the `alpha` pin changes meaning once calibrated** (raw alpha → eyes-closed score 0..1).
- **Uncommitted work is a risk.** The whole branch lives in the working tree.
- **Block B: the raw Muse spectrogram is not readable by eye** (0.74). M3 only passes through the
  calibrated strip, so M3 really measures the Block A classifier, drawn. Audio is the better raw channel.
- **Block B: M3 passed 10/10 on the picture alone, but the owner was told to read the strip first.**
  That proves the strip reads at a glance; it does not prove the raw spectrogram or the sound do.
- **Block B: Hilbert adds 125 ms** to the audio pin; fine for drones, too slow for rhythm.
- **Block B: Geometry pin deferred to Block C**, where the starter patch shows whether it earns a pin.
- **Block B: Python mirror skips artifact blanking**, as in Block A.
- **Block C: the proof is synthetic.** The worker runs on wall-clock time, so a headless render of a
  CSV replay isn't locked to render time; real-data proof stays in selftests [7] and the M3 sheet.
- **Block C: M4 fails.** Blinks fire, but 1–3 false toggles a minute make a toggle unusable.
  Most are second fires near a real blink, or eye movements. The fixed default threshold
  (135 µV median) fits nobody in particular.
- **M4 as written mixes two jobs.** A toggle should fire on deliberate blinks and ignore natural ones,
  but the VV/VR labels are natural blinks. The fair test is EEG-IO (cued blinks) in Intentional mode.
- **Cued calibration halves the gap, not closes it.** Natural blinks overlap cued ones in size and width,
  so a single-blink toggle can't separate them on amplitude alone. A double blink is the likelier toggle
  gesture, but no labelled double-blink data exists to score it.
- **Block C: nothing has touched a real headset.** Ship / iterate / abandon can't be decided before that.

## 12. Next steps

1. Owner signs off §1–7 (or edits the bars).
2. Commit the current worktree as the baseline snapshot.
3. Download the validation data (PhysioNet R01/R02 × 109 ≈ 270 MB, HEROIC 86 MB, GIPSA 105 MB,
   EEG-Eyeblinks): needs a yes.
4. Block A in Python (`batch_validate.py`), then C++ per-region DSP + wPLI. One build at the end of the block.
5. Block B pins, then render proof sheets and run the M3 blind test. **Done:** M3 10/10 by the owner.
6. Block C starter patch: **done** (`bci_alpha_bell.inf`, M6 pass). Open: M4 needs the EEG-Eyeblinks
   ship / iterate / abandon waits for a Muse session.
7. M4 fix: add a prompted-blink step to calibration (5 blinks on cue) that sets each person's blink
   threshold, width and polarity. Then re-score EEG-IO in Intentional mode, using each person's first
   5 cued blinks as the calibration. Bar unchanged. **Done:** helps (Intentional IO 0.71 → 0.78,
   VR 0.33 → 0.66) but still fails. Next: score a double-blink toggle on a Muse session with cued doubles.

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
