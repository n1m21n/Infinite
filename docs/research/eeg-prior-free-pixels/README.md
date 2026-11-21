---
title: "How much real pixel information is in scalp EEG? Prior-free grid decoding, then physical sonification"
slug: "eeg-prior-free-pixels"
status: "pre-registered"
owner: "namansoni"
date: "2026-11-22"
prior_art: "Section 2 of this README (owner-checked list)"
commit_sha: "pending-measurement"
---

# How much real pixel information is in scalp EEG?

Sections 1-7 signed off by the owner on 2026-11-22 with all six recommended decisions (D1-D6). They are locked by this commit, before any data was downloaded.

## 1. Goal

| # | Goal | Output |
|---|---|---|
| G1 | Measure the largest grid size N at which image **brightness layout (L)** and **colour (a, b)** can be decoded from scalp EEG with **no pretrained image or vision model anywhere** | One number per channel: N\*\_L, N\*\_a, N\*\_b, with noise ceiling and five controls |
| G2 | Show *where* that information lives on the scalp and in time | Channel x time maps per cell |
| G3 | Turn decoded grids into sound with a **purely physical** mapping (no semantics) | True / decoded / control .wav for 20 test images, plus a blind listening check |
| G4 | Publish the result the same way whether it is positive or null | README 1-12, scripts, results CSV, figures, contact sheet |

## 2. Problem

**What is unknown.** Published EEG-to-image reconstructions look sharp because a pretrained generator (diffusion, CLIP, VAE) supplies the detail. Nobody has published how much layout, brightness and colour EEG carries *on its own*, measured against shuffled-EEG and spatial controls.

**Prior art (checked by the owner before this study; cited, not redone; not re-opened in this session).**

| Source | What it shows | Gap this study fills |
|---|---|---|
| arXiv 2609.22282 | Without pretrained visual weights, outputs were noise-dominated; authors say faithful pixel recovery is not supported | Did not measure *how much* coarse information survives, or at what grid size |
| Perceptogram, arXiv 2404.01250 | Linear EEG mapping, but reconstructs through CLIP + diffusion. Low-level signal at O1/Oz/O2, 100-180 ms; swapping O1/O2 flips the image left-right | The flip test was done through a generator; here it is done with no generator (control e) |
| arXiv 2510.26391 | EEG alone: PixCorr 0.080; extra conditioning raises it to 0.473 | 0.080 is the only "EEG alone" pixel number; it is a whole-image score, not a per-grid-size limit |
| arXiv 2611.19232 | Better EEG alignment gives little gain; the generator dominates | Motivates measuring the EEG side without the generator |
| (none found) | Prior-free pixel decoder with shuffled-EEG controls | This study |

**Background sources used for the method (standard, not prior art for the claim):**
- Gifford et al. 2022, NeuroImage, THINGS-EEG2 dataset paper (design, preprocessing).
- Haufe et al. 2014, NeuroImage: linear decoder weights must be converted to activation patterns before they are read as "where the signal is".
- Meijer 1992 (The vOICe): left-to-right image scan as sound. Only the scan idea is borrowed.

## 3. Scope

| In | Out |
|---|---|
| THINGS-EEG2 preprocessed release: 10 subjects, 17 posterior channels, 100 Hz | The 63-channel release (4.0 GB per subject); can be a follow-up if the 17-channel map looks cut off at the edge |
| CIELAB grid targets, N = 1, 2, 3, 4, 6, 8, 16 | Any pretrained image, vision, CLIP, VAE or diffusion model, including as a "comparison" |
| Ridge regression per subject (main decoder) | Cross-subject decoders (each subject is its own replicate) |
| Optional small from-scratch network, reported only if it beats ridge on the training validation split | Object category, semantics, captions, "dog = bark" style sound |
| Physical sonification of true / decoded / control grids | Any Infinite node, panel or app feature (separate approval if this goes well) |
| Contact sheet + .wav for 20 test images | Real-time or single-trial live BCI use (single-trial numbers are reported, but no live rig) |

## 4. Existing constraints

**Machine and process**

| Constraint | Consequence for this study |
|---|---|
| M2, 8 GB RAM, **13 GB free disk** (checked 2026-11-22) | Full data (11.4 GB) does not fit locally with headroom. All EEG work runs on **Colab**; only result CSVs and grids (a few MB) come back to the repo |
| No download, training or running before sign-off | This README only; sizes below come from OSF's file listing API (metadata only) |
| No push, no merge; commit explicit paths; no absolute home-folder paths in committed files | Scripts take paths as arguments; nothing absolute is committed |
| No Co-Authored-By / "Generated with" lines | Plain commit messages |
| Outputs, not scores | Every number ships with its grid pictures and its sounds |

**Data (sizes from the OSF API, not downloaded)**

| Item | OSF node | Size | Needed? |
|---|---|---|---|
| Preprocessed EEG, 17 channels, `sub-01.zip` ... `sub-10.zip` | anp5v | 10 x ~1.07 GB = **10.7 GB** | Yes |
| Preprocessed EEG, 63 channels | anp5v | 10 x ~4.04 GB = 40.4 GB | No |
| `training_images.zip` | y63gw | 655 MB | Yes (to compute targets) |
| `test_images.zip` | y63gw | 8.1 MB | Yes |
| `image_metadata.npy` + `LICENSE.txt` | y63gw | 0.7 MB | Yes |
| **Total requested** | | **~11.4 GB** (into the Colab VM, not Drive) | Ask before download |

**Dataset facts the design relies on (from the dataset paper; each is re-checked on the downloaded files before anything else runs)**

| Fact | Why it matters |
|---|---|
| Epochs -200 to 800 ms at 100 Hz | 0-500 ms window = 50 samples; pre-stimulus control (-200 to 0 ms) = 20 samples |
| Rapid serial presentation, ~5 Hz (100 ms on, 100 ms off) | The next image starts at 200 ms and 400 ms, inside the 0-500 ms window; its response is noise here (random order) but limits what late windows mean. The -200 to 0 ms window holds the *previous* image's response, which is unrelated under random order, so control (b) stays valid |
| Training: 16,540 images (1,654 concepts x 10) x 4 repeats; test: 200 images (200 new concepts) x 80 repeats | Test concepts never appear in training. Cross-validation groups by concept so validation mirrors the test split |
| Preprocessing may include baseline correction and/or noise normalisation | Baseline correction makes the pre-stimulus window mean-zero per trial; control (b) must be read with that in mind |

### Threats to validity known before data

| Type | Threat | Mitigation in the design |
|---|---|---|
| Internal | Global brightness drives every cell, so per-cell correlation can look "spatial" when it is not | Headline uses **layout-residual** targets (cell minus the image's own grid mean) for N >= 2; control (d) |
| Internal | O1/O2 swap alone may barely move a ridge decoder that also uses PO7/PO8, P7/P8 etc. | Add (e2): swap **all** left/right homologous pairs; (e) as asked is still reported |
| Internal | Pre-stimulus window has 20 samples, main window 50: unequal feature count | Also report a matched-length 0-200 ms window |
| External | THINGS photos mostly have a centred object, so cell-level layout varies less than in arbitrary scenes; low target variance caps any correlation | Report per-cell target variance next to each correlation map |
| External | 80-repeat test averaging is far cleaner than any live use | Report every metric at 1, 4, 10, 20, 40 and 80 averaged repeats |
| Construct | Image stats are computed as if the screen were sRGB; the lab monitor is not characterised | Targets are relative across images; state the assumption |
| Construct | Grid r is not "seeing"; it is a linear readout of contrast/colour energy | Wording in the write-up says "decodable", not "perceived" |
| Licence | THINGS images have their own licence; committing thumbnails may not be allowed | Committed contact sheet shows grids only until `LICENSE.txt` is read; source thumbnails stay local |

## 5. Hypothesis

Pre-registered. Locked at sign-off; not changed after data is seen.

| ID | Hypothesis | Pass bar | Kill / null criterion |
|---|---|---|---|
| H0 (gate) | Whole-image brightness (N = 1, L) is decodable | Group mean r beats shuffled and pre-stimulus controls, permutation p < 0.05 | Fails: stop decoding analyses at N = 1 for that channel, write up as a null (still a valid result) |
| H1 | Brightness **layout** survives to N\*\_L ≈ 3-4 | N\*\_L >= 3 | N\*\_L <= 2 |
| H2 | Colour carries less layout than brightness | N\*\_a < N\*\_L and N\*\_b < N\*\_L | Either colour N\* >= N\*\_L |
| H3 | Decoded layout is truly spatial | Lateral cells: r(decoded, true) > r(decoded, left-right-swapped true), p < 0.05 | No difference |
| H4 | Lateral layout depends on the hemisphere wiring | Swapping left/right channels at test lowers lateral-cell r and raises r with the mirrored image (tested for (e) and (e2)) | No change under (e2) |
| H5 | The sound keeps what the grid keeps | Blind listener picks the decoded sound over the mismatched one as "same image as the true sound" in >= 15 of 20 trials | <= 14 / 20; only run if H1 passes for at least N = 2 |

Expected size (a guess, written down so it can be wrong): N = 1 L group r ≈ 0.3-0.5 at 80 repeats; colour N = 1 r ≈ 0.1-0.2.

## 6. Baseline

Recorded **once**, in Phase 0, before the real decoder is fit on real labels. Frozen in `data/phase0-baseline.csv`; never re-measured.

**Fixed reference numbers (literature, no measurement needed)**

| Reference | Value | Used for |
|---|---|---|
| EEG-alone PixCorr (arXiv 2510.26391) | 0.080 | Context for our whole-image PixCorr at matching resolution (their PixCorr is at image resolution through a generator path; not directly comparable, stated as such) |
| Per-cell r significance at 200 test images | |r| > 0.139 (two-sided, p = 0.05, uncorrected) | Sanity floor; real thresholds come from permutation |
| 200-way identification chance | top-1 0.5 %, top-5 2.5 % | Identification metric |

**Controls (measured once in Phase 0, same pipeline, same subjects)**

| Control | How | What it rules out |
|---|---|---|
| (a) Shuffled labels | Train ridge on training EEG with image-to-target pairing shuffled (5 seeds), test as normal | Decoder fitting structure that is not tied to the image |
| (b) Pre-stimulus | Same decoder fit on -200 to 0 ms instead of 0-500 ms; plus matched-length 0-200 ms | Information that exists before the image appears |
| (c) Mean image | Predict the training-set mean grid for every test image | PixCorr/SSIM credit earned just by "objects are in the middle" (per-cell r is undefined for a constant, so (c) applies to whole-grid metrics) |
| (d) Left-right swapped truth | Score the real decoder against each test image's own grid mirrored left-right (centre column excluded for odd N) | Decoded "layout" that is really symmetric or global |
| (e) O1/O2 swap at test | Swap the O1 and O2 inputs, keep decoder fixed | Lateral information carried by O1/O2 |
| (e2) All homologous pairs swapped at test | Swap every left/right channel pair, keep decoder fixed | Same, without leakage through other lateral channels |

**Noise ceiling.** For each test image split the 80 repeats into two random halves of 40 (100 splits), decode each half with the trained decoder, correlate the two decoded outputs per cell across images, apply Spearman-Brown, take the square root. This is the best correlation the decoder's output could reach given EEG noise. It is decoder-dependent and is stated that way.

## 7. Success metric to be achieved

**Targets.** Each image (sRGB, assumed D65) is converted to linear RGB, area-averaged into an N x N grid (averaging light, as a blurred eye would), then converted to CIELAB. Each cell has L, a, b. Relative luminance Y is kept for the sound. For N >= 2 the **layout-residual** target is cell minus that image's grid mean (per channel); raw targets are reported too.

**Decoder.** Ridge, one per subject, input = 17 channels x 50 samples (0-490 ms) = 850 features, z-scored with training statistics; training EEG averaged over its 4 repeats. Lambda from a log grid (1e-2 ... 1e6), chosen by 5-fold cross-validation **grouped by concept** on training data only, one lambda per (N, channel). Closed form from one SVD per subject, so all N, lambdas and targets are cheap. Optional small network (a few conv layers over channels x time, from scratch, Colab GPU, 3 seeds): reported only if its validation r beats ridge by more than 2 seed standard deviations.

**Metrics on the 200 held-out test images**

| Metric | Definition | Reported at |
|---|---|---|
| Per-cell r | Pearson r across the 200 images between decoded and true value, per cell, per channel, per subject; group = mean over 10 subjects | Every N, raw and residual |
| PixCorr | Per image, Pearson r between decoded and true grid (L), mean over images | Every N; also after subtracting the mean image from both |
| SSIM | Gaussian SSIM (σ = 1.5, Wang 2004 constants) on L grids bilinearly upsampled to 64 x 64, same pipeline for every condition | N >= 4 (smaller grids have no structure for SSIM to measure) |
| Identification | Correlate the decoded grid vector (L, a, b z-scored) with all 200 true grids; top-1 and top-5 | Every N |
| Repeats curve | All of the above at 1, 4, 10, 20, 40, 80 averaged test repeats | Every N |

**Statistics.** Permutation test: shuffle the test-image pairing 10,000 times, the same permutation for all subjects, statistic = group mean r. Benjamini-Hochberg FDR q < 0.05 across cells within each (N, channel). Controls (a), (b) are compared by the same statistic; (d) and (e)/(e2) by paired permutation on the difference.

**Headline rule (proposed; see decision D3).** N\* for a channel is the largest N where (1) the median cell's group-mean residual r is above every control's, and (2) at least half of the cells survive FDR. The full r-vs-N curve with the noise ceiling is always shown, so the rule cannot hide anything.

**Sonification (physical only).** The grid is scanned left to right over 1 s, column c sounding during [c/N, (c+1)/N) with 5 ms crossfades.

| Image property | Sound property | Physics |
|---|---|---|
| Column | Time and stereo pan (equal power, left to right) | Scan, as in The vOICe |
| Hue | Pitch inside a register | Dominant wavelength λ from the CIE 1931 xy ray from D65 white through the cell's chromaticity to the spectral locus (400-700 nm); f\_light = c / λ; f\_audio = f\_light / 2^40. 700 nm → 390 Hz, 400 nm → 682 Hz |
| Row | Register (octave) | The bottom row divides by 2^42, each row up shares a 3-octave span to the top row (2^39). Same light, different power of two, so octave identity is kept |
| Brightness | Loudness | Acoustic intensity ∝ relative luminance Y (amplitude ∝ √Y). L\* ≈ cube root of Y and perceived loudness ≈ intensity^0.3, so perceived loudness follows perceived lightness with no tuning |
| Colourfulness | Tone vs noise | Excitation purity (distance from white over distance to the locus) sets the tone/noise mix. A grey cell has no dominant wavelength: it plays band-limited white noise in its register, as white light contains all wavelengths |
| Purples | Two tones | Purples have no single wavelength; they lie on the line joining the spectrum's red and violet ends because purple light **is** a mix of those ends. Recommended: play 390 Hz and 682 Hz (in the row's register) weighted by the position on that line (decision D4) |

Render rules: 48 kHz, 24-bit WAV; the three versions of one image share one gain so loudness differences stay real; decoded Lab values are clipped to L in [0, 100] and purity to [0, 1]. Grids sonified at N\*\_L (minimum N = 2).

**Listening check (H5).** 20 trials, random order: play the true sound, then two candidates: the decoded sound for that image and the decoded sound of a different test image (mismatched). The shuffled-control sound is rendered as asked, but it sits near the mean grid and sounds bland, so picking against it would be too easy. The owner picks blind. Pass >= 15 / 20 (binomial p = 0.021).

**Deliverables checklist (end of study)**
- `results/results.csv`: every metric x N x channel x condition x subject x repeats.
- Figures: r vs N with noise ceiling (L, a, b), per-cell r maps, channel x time maps (per-channel and per-50 ms-window refits, plus Haufe activation patterns).
- Contact sheet: true / decoded / control grids for 20 test images, and the matching 60 .wav files.

## Decisions needed at sign-off

| ID | Decision | Recommendation |
|---|---|---|
| D1 | Where the EEG work runs | Colab VM for download and all decoding (11.4 GB does not fit beside 13 GB free locally); only CSVs/grids come back |
| D2 | Headline target: raw cells or layout-residual cells | Residual for N >= 2, raw also reported |
| D3 | "Beats all controls" rule for N\* | Median cell above every control **and** >= 50 % of cells pass FDR |
| D4 | Purples | Two-tone mix of the red and violet ends (alternatives: complementary wavelength with a flag; wrap into 682-782 Hz to close the octave, which invents ultraviolet) |
| D5 | Listening-check foil | Mismatched decoded image, with the shuffled control rendered but not used as the foil |
| D6 | Download | ~11.4 GB (17-channel EEG for 10 subjects + image set) into Colab, after sign-off |

## 8. Best and optimised methodology

*(After sign-off.)*

## 9. Proposed solution trajectories

*(After sign-off.)*

## 10. Benchmarking against the baseline

*(After sign-off.)*

## 11. Critique

*(After sign-off.)*

## 12. Next steps

*(After sign-off.)*
