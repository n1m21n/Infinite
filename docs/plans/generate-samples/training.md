# Generate Samples: Critic, Proposer & Learning Plan

The generator `(recipe, seed) -> audio` is not an imitator of the training corpus.
Corpus models exist **for robustness of the model to avoid mishappenings, not as a template the generator follows**.
Everything in the offline model store is count tables and Bernoulli mixtures over typed pieces (no LLM, no audio neural net).

## Architecture

```
Proposer (free: theory + Euclidean/cross-cluster synthesis + random + novelty reward)
        |
        v   K symbolic candidates (< 1 ms)
Critic (trained on corpus: critic.py)
  1. Hard structural & DSP validity gates (deterministic, zero mishaps)
  2. Wide plausibility floor (calibrated at P3 of held-out real music)
  3. Anti-cliché ceiling + structural novelty bonus (syncopation, melodic arc, card distance)
        |
        v
User Taste Layer (UserStyle contextual preference head over 16 musical features)
        |
        v
SampleStyles::Compile(Recipe) -> OfflineGraph (48 kHz) -> Audio Master Gate (-14 LUFS / -1 dBTP)
```

## Offline Training Flow

```
MIDI files (inbox, symlinks)      patches, cards, public drums, chords, synthetic theory
        |                                   |
   midi_ingest.py / pieces.py / patch_pieces.py / chord_pieces.py / public_drums.py / synth_music.py
        |                 typed pieces + QA gates (hard fail = rejected, soft = warn, duplicates rejected)
        v
   store/*.jsonl  ---->  mix.py (32 latent drum styles) + learn.py (drum, melody, bass, chord, params)
                           |                 kept only if no score regresses on held-out SOURCE FILES
                           |                 (hash mod 5; synthetic pieces never enter the test set)
                           v
                  models/<scope>/current.json + drum_mix.json
                           |
                           v
                  critic.py + proposer.py + benchmark.py (6-gate verification suite)
```

Run: `python3 tools/universe/train_all.py` and `python3 tools/universe/benchmark.py`. Safe to repeat.

## Scopes (licence)

| Scope | Trains on | Used for |
|---|---|---|
| `shippable` | owner, cleared-by-owner, CC0, public domain, MIT, CC-BY | anything that ships in the app |
| `local` | all of the above plus third-party, NC, SA, GPL, unknown | this machine only |

The owner cleared the licensing of the whole `datasets/midi` corpus on 2026-10-26 (tagged `cleared-by-owner`), so it trains both scopes.

## Pieces and Models

| Piece | Source | Model | Score (bits saved vs baseline) |
|---|---|---|---|
| `drum_groove` (8 lanes x 16) | Groove, E-GMD, DMP, Lakh/Tegridy ch.10, cards, patches | 32-cluster EM mixture (`mix.py`) + conditional backoff (`learn.py`: cluster/family x lane x step -> kick/snare -> own lane 1 step back) | `+0.169` conditional / `+0.129` mixture |
| `melody_contour` (16 notes) | Lakh, POP909, Nottingham, NES, ADL, Bach (`role != bass`) | interval given previous interval, scale degree in key, harmonic function (`T/PD/D`) + contour arc | `+0.800` |
| `bass_seq` (16 steps) | cards, patches, synthetic, MIDI bass lines (`role == bass`) | degree/onset given step, strong beat, previous degree | `+0.145` |
| `chord_prog` (2..8 chords) | `chord_progressions` (11.7k files, 965 distinct progressions) | next Roman numeral given previous numeral, per mode (`Major`, `Minor`, `Modal`) | `+0.424` |
| `params`, `glide_params` | every node's knobs in hand-made patches | per node/knob quantiles (`[P5, P95]`) + 16-bin histogram | `+1.997` |
| `note_expression` | MIDI velocity, pitch-bend, pressure, CC74 | velocity & expression contour statistics | n/a |

## The 6-Gate Benchmark Suite (`benchmark.py`)

A model or critic change is accepted only when all 6 quantitative gates pass on held-out source files:

1. **B1 — Held-Out Compression (`bits_saved`)**: Every model (`drum`, `drum_mix`, `melody`, `bass`, `chord`, `params`) must be non-decreasing vs the last kept model on unseen source files (`hash % 5 == 0`).
2. **B2 — Real-Music False-Reject Rate (`FRR <= 3.0%`)**: Fraction of held-out real test pieces rejected by `critic.py` (hard gates + `P3` plausibility floor).
3. **B3 — Mishap Catch Rate (`MCR >= 96%`, `100%` on critical mishaps)**: Recall of `critic.py` on deliberately corrupted pieces (`no_kick`, `stuck_16ths`, `off_key_strong`, `unmusical_leaps`, `wrong_meter`, `kick_snare_clash`, `silent_filter`, `random_noise`).
4. **B4 — Structural Realism Two-Sample AUC (`Struct-AUC in [0.45, 0.60]`)**: Cross-validated classifier AUC between held-out real pieces and critic-accepted proposer output on style-agnostic structural invariants (key clarity, metric grounding, phrase resolution, density balance).
5. **B5 — Out-of-Distribution Novelty & Diversity (`QD-Score`)**: Card-escape rate (`>= 85%` of accepted pieces have normalized distance `>= 0.25` from all 30 style cards), pairwise diversity (`>=` real test diversity), and cluster entropy (`>= 0.80`).
6. **B6 — Simulated User Adaptation Speed (`Taste-Regret@20`)**: Simulated user personas clicking Keep/Reject reach `>= 75%` keep-rate within 20 clicks using the 16-feature contextual preference model.
