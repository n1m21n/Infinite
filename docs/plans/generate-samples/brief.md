# Generate Samples: brief (paste into a new session)

## Context
Infinite is a node-based live instrument for audio and visuals. The **Generate Samples** node (`GenerateSamplesNode`, extends `SamplerNode`)
renders a sample offline: part + seed -> style card -> `SampleStyles::Compile(Recipe)` -> hidden `.inf` patch -> `OfflineGraph` at 48 kHz,
then a quality gate (normalise to -14 LUFS / -1 dBTP, up to 3 redraws). The generator `(recipe, seed) -> audio` is hand-written and is NOT learned.
Today the only learning is `UserStyle` (the user's keep/reject clicks nudge card choice, tempo and knobs; trust n/(n+n0), capped at 0.9).

## Purpose
Make the node creative and ever-evolving: it should keep inventing music no human wrote, and get safer and better with every piece of data and every click.

## The problem
1. **Robustness.** The generator can produce mishaps (no kick, stuck 16th hits, notes off the key, wrong meter, clipped or silent renders). We need a trained critic that knows what music fundamentally is and what breaks.
2. **Creativity without a boundary.** The corpus must NOT become a template the generator imitates. The owner (2026-10-26): the data is "for robustness of the model to avoid mishappenings, not something the generator would follow". Synthetic and out-of-distribution music is welcome.
3. **Taste is personal.** Ears differ; only the user's own good/bad clicks can say what is good. The machine prior must start the personal layer, not replace it.
4. **Measuring it.** Bits-saved-per-cell is only an internal yardstick. No published benchmark exists for this task (GrooVAE, MusPy metrics, "Language Models are Drummers" use listening tests and per-paper metrics).

## Design (decided)
```
proposer (free: theory + synthetic + random + novelty reward)
   -> critic (trained on the corpus): hard validity | wide plausibility floor | novelty bonus
   -> user keep / reject (personal layer)
```
No LLM, no audio neural net first. Models are count tables over typed **pieces** (drum_groove, melody_contour, chord_prog, params, bass_seq, note_seq, glide_params, note_expression), each behind a QA gate (hard fail = rejected, soft = warn, duplicates rejected). A model replaces the last kept one only if no score got worse on held-out SOURCE FILES (hash mod 5; synthetic is never in the test set).
Never wire a learned model into `Compile()` as the sampling distribution. Any C++ change needs the owner's approval first.

## Data
- `~/Infinite/datasets/midi` (294k MIDI, 11 GB; Lakh full/clean, Tegridy, rock piano+drums, NES, POP909, Nottingham, ADL piano, Bach, drum-machine patterns, chord progressions/scales). Owner cleared the licensing of all of it (2026-10-26); sets are tagged `cleared-by-owner` and train both scopes.
- Linked, not copied, into `~/Infinite/training-data/inbox/` by `link_datasets.py`. Pieces live in `training-data/store/*.jsonl`, models in `training-data/models/<scope>/`.
- Also: Groove MIDI and E-GMD drum bars (public), 30 style cards (`tools/catalog/styles_music.json`), the user's own patches (knob values), theory-based synthetic pieces.

## Code (all in `tools/universe/`, uncommitted)
`pieces.py` gates and card extractors | `midi_ingest.py` inbox MIDI -> pieces (4/4 only, tempo kept, grid check, caps per source and per file) | `chord_pieces.py` progressions from filenames | `public_drums.py` | `patch_pieces.py` | `expression_pieces.py` | `synth_music.py` | `learn.py` count-table models + keep-only-if-better | `mix.py` Bernoulli-mixture drum styles (EM) | `link_datasets.py` | `train_all.py` one-command run. Plan doc: `docs/plans/generate-samples/training.md`.

## Results so far (held-out bits saved vs baseline)
| Model | Score | Note |
|---|---|---|
| Melody (prev interval + note in key) | +0.74 | key context did the work; chord guess adds nothing yet |
| Drum, per-cell with kick/snare/lane history | +0.023 | family labels useless: nearly all bars unlabeled |
| Drum, 32-style mixture | +0.068 | found with no labels; many styles had no kick -> data-quality bugs |
| Params | +1.997 | |
| Chord (next chord given previous, per mode) | not scored yet | 965 distinct progressions from 11,700 files |

Fixed this session: time signatures were ignored, off-grid hits mis-quantised, first bar of short patterns skipped, hat lanes wrongly rejected at 1.0 density, melody contexts replaced instead of extending each other, strict licence check.

## Next
1. Let the re-ingest (12,000 files per source, about 20 min) finish; retrain; re-run `mix.py`; score the chord model.
2. Build `critic.py`: hard validity + wide plausibility floor (percentile of real data) + novelty score.
3. Measure robustness: false-reject rate on real bars (near zero), mishap catch rate on deliberately corrupted bars, diversity of accepted synthetic output; plus a real-vs-generated classifier (about 50% = realistic).
4. Check drum gates against real drum data (density 0.6, lane 0.75, duplicates).
5. Later, with approval: use the critic as a filter on proposals inside `Compile()`; bass model; personal preference critic from keep/reject clicks; nightly retrain.

## Rules to respect
No Claude attribution lines in commits (project rule). Budget long jobs to about 20-35 min and check they are alive. Builds only through `scripts/ibuild.py`. Do not commit or touch C++ without being asked.
