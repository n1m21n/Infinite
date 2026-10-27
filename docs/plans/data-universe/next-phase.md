# Next phase: from "collected" to "one linked model that uses audio, presets and mood"

Written 2026-10-27 in the owner's 12-section research format. Sections 1 to 7 are for sign-off before any work.
Companion files: [README.md](README.md) (tracker), [training-plan.md](training-plan.md) (C1-C6, G1-G5, B1-B10), [NEXT.md](NEXT.md) (the six steps in flight).
Approved by the owner on 2026-10-27: gathering the missing audio features, and a wavetable generator **to improve the model, not to ship as a feature**.

## 1. Goal

One system where **genre + mood + section** select **notes, instrument, sound and parameters together**, trained on all collected data (MIDI, audio, presets, wavetables, arcs), judged by the same held-out gates, and able to build its own playable sample instruments from the library.

```
(genre, mood vector, section position)        <- the shared key
        |
        +--> note models   drums | bass | chords | melody        (today: working, MIDI only)
        +--> instrument    sound cluster -> sample/preset choice  (today: not linked)
        +--> sound         spectrum, envelope, params             (today: audio unused)
        +--> mix           levels, space, spectrum balance        (today: not built)
```

## 2. Problem

| # | Problem | Evidence (2026-10-27) |
|---|---|---|
| P1 | The note models see only MIDI; audio, presets and wavetables never train anything | `models/shippable/current.json` trains on 8 MIDI row types only |
| P2 | The audio rows are thin: no pitch, key, tempo grid, MFCC, harmonic/percussive split, labels | `audio_features.py` writes bands, centroid, flatness, attack, decay, rms, onset groove |
| P3 | Only 15% of instruments are clustered from audio (2,154 of 14,161); C4 needs 60% | `sound_clusters.py` summary |
| P4 | Mood is a flat declared number per genre (valence, energy, danceability, brightness, contrast), not measured per row and not a model input | `atlas/genres.json` `pillars` and `mood` blocks |
| P5 | Drums fail B4 (AUC 0.794, bar 0.65) and B5 (entropy 0.664, bar 0.75) | `benchmark.py` |
| P6 | Colab does not reproduce the Pro's gates yet (Drive's mount misses `inbox.jsonl.gz`) | README row 2b+++ |
| P7 | Whether a preset can be joined to the sound it makes is untested | NEXT.md step 5 |

## 3. Scope

| Workstream | In scope | Out of scope |
|---|---|---|
| W1 Audio features v2 | New measurements on the existing 125k files; filename labels; embeddings on Colab | Re-reading files already measured for the old fields |
| W2 Sample-instrument builder | Dataset records of pitched multisamples (name, measured pitch, velocity layer, loop) | Shipping a playable sampler node (needs approval) |
| W3 Wavetable generator | Interpolation, PCA over harmonic curves, formula families, to enlarge training data for param/critic models | A wavetable feature in the app |
| W4 Mood layer | Per-row valence / arousal / energy / danceability / brightness / contrast from measurements; a shared coordinate system | UI |
| W5 Link layer | Conditional models genre+mood+section to instrument cluster, sound features, params | A new node (needs approval) |
| W6 Gates and retrain | B4/B5, Colab parity, retrain under G5 | Changing B1-B10 bars without a recorded reason |
| W7 Preset-to-sound join | One render test, then per-patch rows on the Air | |

## 4. Existing constraints

- 8 GB Macs: one heavy job at a time, 20-minute resumable chunks, builds only via `scripts/ibuild.py`.
- WD-Black is read only; raw audio never leaves the machine; only rows travel (licences).
- `--scope shippable` trains shipped models; library and plug-in rows guide critics and benchmarks only.
- Retrain only when C1-C6 hold; keep a model only if no gate gets worse (G5).
- New nodes, modes and panels need the owner's approval first.
- `main` is not pushed; no Colab retrain until parity is shown.

## 5. Hypotheses

| # | Hypothesis | If true |
|---|---|---|
| H1 | Pitch, key and tempo grid on the audio rows will lift audio-cluster coverage (C4) and make sample-instrument building possible | Rows carry `pitch_hz`, `key`, `bpm` for >= 80% of one-shots and loops |
| H2 | Filenames like "Piano A#" give free labels; measured pitch confirms them, so labelled multisamples are mostly reliable | Agreement between filename note and measured pitch >= 90% on files that have both |
| H3 | A shared (genre, mood, section) key lets the link layer predict instrument cluster and sound features better than genre alone | Held-out bits saved for cluster choice beats the genre-only baseline |
| H4 | Mood measured per row (not declared per genre) separates tracks inside a genre | Within-genre variance of measured valence/energy is larger than the noise of the measurement |
| H5 | Hat doubling in the C++ groove proposer causes B4/B5; sampling grooves from real clusters instead of rules plus a critic fixes both | Sim already reaches AUC 0.591; a cluster sampler reaches <= 0.65 with entropy >= 0.75 |
| H6 | Synthetic wavetables improve held-out param prediction only if they stay inside the real manifold | Param bits saved rises with synthetic data capped at a fixed share of real |

## 6. Baseline (recorded once, 2026-10-27; do not re-measure before the final comparison)

| Item | Value |
|---|---|
| Benchmark gates | 8 / 10 (shippable) |
| B1 held-out bits saved | drum +0.204, melody +0.813, bass +0.111, chord +0.058, params +1.997 |
| B4 drums AUC | 0.794 (bar <= 0.65) |
| B5 cluster entropy | 0.664 (bar >= 0.75) |
| Audio rows | 113,427 of ~125,779 paths; 52 folders listed as missed |
| Instruments | 14,161; audio-clustered 2,154 (15%), tag-assigned 9,751, none 2,256 |
| MIDI | 236,386 source files; rows: chord 873k, melody 705k, expression 705k, bass 486k, drum 407k; song arcs 180k |
| Genres | 69 in `atlas/genres.json`; five declared pillars each |
| Preset rows | Serum 2 626 (knobs), `.ffp` 4,810, formats metadata 22,194, plus Omnisphere, Diva, Hive and others |
| Models that read audio or presets | 0 |

## 7. Success metrics

| Workstream | Metric | Bar |
|---|---|---|
| W1 | Rows with pitch / key / tempo grid / MFCC / harmonic ratio | >= 95% of rows have the new fields; 100% of files measured or marked duplicate |
| W1 | Filename label coverage on one-shots | reported, with agreement rate vs measured pitch >= 90% |
| W2 | Pitched multisample instruments built | >= 500 instruments, each with >= 6 distinct measured pitches and no gap wider than a fourth; spot check by ear on 20 |
| W3 | Synthetic wavetables | pass a "real vs synthetic" classifier no better than AUC 0.60; param bits saved >= baseline |
| W4 | Mood vectors | every audio and MIDI row has valence / energy / danceability; correlation with the declared genre pillars >= 0.5 |
| W5 | Link layer | held-out bits saved for (instrument cluster, sound features) beats the genre-only baseline by >= 0.10 |
| W6 | Gates | 10 / 10 held for two consecutive retrains; Colab reproduces Pro within 0.02 per gate |
| W7 | Join test | one yes/no with a rendered file |

## 8. Methodology

**W1 Audio features v2 (numpy only, same chunked runner `audio_features.py --resume`).**
- Pitch: YIN or autocorrelation on the voiced middle of one-shots; confidence stored; octave errors checked against the filename note.
- Key and chroma: 12-bin chroma over loops, Krumhansl profiles for key and mode.
- Tempo and grid: onset envelope autocorrelation, tempo candidates, grid fit and swing (extends the existing groove code).
- Timbre: 13 MFCC means and deltas, spectral rolloff, flux, harmonic/percussive ratio, zero-crossing rate.
- Labels: parse filename and folder into instrument, note, velocity, round-robin, loop/one-shot, key, bpm; keep the raw tokens.
- Embedding: a learned audio embedding on Colab GPU. Model choice and any download are decided with the owner first (no pack downloads without a yes).

**W2 Sample-instrument builder.** Group files by pack and instrument token; read note names and measured pitch; assign velocity layers by rms; detect loops by envelope; write `samplers/built_instruments.jsonl` (rows only: names, pitches, layers, paths). Quality gate: coverage and gap rules in section 7.

**W3 Wavetable generator.** Frames are harmonic amplitude and phase vectors (already in `wavetables/`). Three samplers: linear and spectral morph between real frames; PCA with Gaussian sampling in the leading components; formula families (saw, square, pulse, formant, comb). Output tagged `synthetic: true` and capped as a share of training rows. Never enters `--scope shippable` unless the owner approves.

**W4 Mood layer.** One coordinate system for every row, derived by formula from measured data, so it can be audited:

| Axis | MIDI rows | Audio rows |
|---|---|---|
| valence | mode share, interval brightness, tempo | key mode, spectral brightness, tempo |
| energy / arousal | note density, velocity, register | rms, onset rate, flux |
| danceability | groove regularity, kick-on-beat share | beat strength, tempo stability |
| brightness | register centroid | spectral centroid |
| contrast | dynamics range | dynamics range |

These extend the five pillars already declared per genre (danceability, energy, valence, brightness, contrast, the Spotify-style audio features the project was built on). The `character-sim` emotion model (valence, arousal, stress, energy, dominance) is the richer target; the first cut uses the five and reserves two more axes for it. Declared genre values become a prior, measured values the evidence.

**W5 Link layer.** Two stages. (a) Count baseline: P(instrument cluster | genre, mood bin, section tenth) from atlas rows and the arc tables, then P(sound feature bins | cluster), then preset parameter ranges per cluster. (b) Matrix link: context vector x = [genre | mood | section]; scores s = Vt(Ux) with low rank 8-32 (bilinear genre x mood optional), one W with three output blocks (cluster, sound features, params); numpy least squares / short SGD. Must beat (a) by >= 0.10 bits held-out.

**W6 Gates.** Sim-first for drums (`groove_sim.py`, A/B in `eval/groove_ab`): cluster sampler versus rules plus critic; owner sees the A/B before any C++ port (the 2h port needs sign-off). Colab: notebook now fetches the inbox table through the Drive API; parity run in progress.

**W7 Join test.** One `.inf` with an `AudioEffects Plugin` node carrying `plugin_state`; render with `--audio-summary`; compare the rendered spectrum to the preset's stored tags. Needs a real state chunk from the Air.

## 9. Proposed trajectories

| Option | Pros | Cons | What-if it fails |
|---|---|---|---|
| A. Features first, then link layer (recommended) | Everything else depends on the new fields; cheap, numpy only | A long second pass over 125k files | Cap per-chunk time; fall back to fields that survive (pitch, tempo first) |
| B. Embedding first on Colab | Rich features without hand design | Download and GPU time, opaque, hard to audit | Use hand features only |
| C. Link layer on today's rows | Fast, shows the shape | Starved of pitch, key and mood, results mostly tag-driven | Treat it as a smoke test only |
| D. Drum fix before everything | Unlocks 10 / 10 | Needs the 2h C++ sign-off; independent of the rest | Relax B4/B5 in `training-plan.md` and record why |
| E. Wavetable generator now | Approved, self-contained | Gain unproven for the model until W5 exists | Keep as data augmentation only |

Recommended order: A, then W4 (mood) and W2 in parallel, then W5, with W3 slotted whenever a heavy slot is free, and W6 / W7 interleaved because they are small.

## 10. Benchmarking against the baseline

To be filled when each workstream finishes, as a table with the section 6 value, the section 7 bar and the new value. No baseline numbers are re-measured before then.

| Item | Baseline (6) | Bar (7) | After |
|---|---|---|---|
| (filled per workstream) | | | |

## 11. Critique

- **Hand features may not beat embeddings.** If W5 gains stay below 0.10 bits, the embedding trajectory (B) gets its turn.
- **Mood by formula is still a guess.** The correlation check against declared pillars catches gross errors but not taste; the owner listens to a sample of high and low valence tracks before the axes are trusted.
- **Filenames can mislead.** Hence the measured-pitch agreement check and the "reported, not assumed" labelling.
- **Synthetic wavetables can poison the model.** The share cap and the real-vs-synthetic classifier guard it; they stay out of the shippable scope by default.
- **The link layer may be dominated by data volume.** Only 15% of instruments have audio today; W1 and W7 matter more than modelling cleverness.
- **Colab is a distraction if it keeps costing days.** Parity is needed only before a GPU retrain; the Pro result is the reference.
- **I am unsure which four Spotify pillars the owner means.** The data carries five declared pillars (danceability, energy, valence, brightness, contrast); the first three are Spotify's audio features. W4 starts from those five and adds axes only with the owner's say.

## 12. Next steps (in order)

| # | Step | Needs | Done when |
|---|---|---|---|
| 1 | Finish the in-flight run: Colab parity result, audio pass chunks to 0 missed, step 5 render test | nothing from the owner | README rows updated; yes/no on the join |
| 2 | Sign-off on sections 1 to 7 of this file | owner | recorded below |
| 3 | W1: write `audio_features2.py` (pitch, chroma, key, tempo grid, MFCC, harmonic ratio, filename labels), test on 200 files, then run in 20-minute chunks | step 2 | section 7 W1 bars |
| 4 | W4 mood layer over MIDI and audio rows; listen check on a sample | step 3 for audio | W4 bar |
| 5 | W2 sample-instrument builder | step 3 | W2 bar |
| 6 | W5 link layer, count-based, benchmarked against genre-only | steps 3, 4 | W5 bar |
| 7 | W3 wavetable generator and the augmentation test | free heavy slot | W3 bar |
| 8 | W6: drum cluster sampler in the sim, A/B shown to the owner; retrain only if C1-C6 hold and G5 passes | owner decides on the 2h port | 10 / 10 twice |
| 9 | Node proposals, shown to the owner before anything is built | steps 6, 8 | `node-proposals.md` approved |

Decision rule: ship, iterate or abandon each workstream at its section 10 table; abandon if the bar is missed twice with a recorded reason.

### Sign-off log

| Date | Sections | By | Note |
|---|---|---|---|
| 2026-10-27 | 1-7 | Naman | Approved in chat. W5 uses count baseline then low-rank matrix link. |
