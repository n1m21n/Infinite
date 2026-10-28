# Execution plan from 2026-10-28 (follow in order, no redesign needed)

Serves the plan in `HANDOFF.md` (P1-P6, bars unchanged). Every step names the phase it serves; a step not on this list is not
done in the session. Decisions are written as rules below, so the session runs them, records the number and moves on.

## Rules that keep the original plan intact

| Rule | Meaning in practice |
|---|---|
| Phase order | P3 bar (each characteristic mean R2 >= 0.6 on the group split) before P5; P4 before P6. No skipping |
| Bars do not move | a miss is recorded with its learning curve, never re-scored with an easier bar |
| Data rule | everything held is measured fully and losslessly (Step G, owner 2026-10-28); new renders need a step that names the error they fix |
| One heavy job | renders and training run one at a time on the Pro; the Air stays parked |
| Budgets | about 20 min per job; check rows appear within 3 min of starting; stop file for every render |
| Map vs learner | other synths and samples = map (measured, never shipped); Infinite's nodes = learner (owner, 2026-10-28) |
| New nodes | piano, wind, resynthesis, better String: design shown to the owner first, never built inside these steps |
| Commits | one per step, no attribution lines; chat corpora never pushed |
| Records | every number lands in `SOUND-MODEL.md` 4b (data) or `HANDOFF.md` status (phase), the same day |

## Step 0 - start of session (5 min)

```
cat docs/plans/data-universe/HANDOFF.md docs/plans/data-universe/EXECUTION.md
ls ~/Infinite/scratch_omni/diva_d1b.done ~/Infinite/scratch_omni/diva_d2w.done
grep -h "extra rows\|characteristic means" ~/Infinite/training-data/models/p3_d1b.log ~/Infinite/training-data/models/p3_d2w.log
pgrep -fl "preset_state|p3_characteristics"     # nothing heavy may be running before a step starts
python3 tools/universe/claims.py table              # once claims.py exists: today's state of C1-C7
```

If a log is missing and nothing runs: rerun the missing line from Step 1 (commands there).

## Step 1 - close the Diva pilot (P3, data contract) - DONE 2026-10-28: D1b 0.200, D2w 0.113 group; Diva stops for P3; first batch 30% one-knob / 70% base

Inputs: `p3_d1b.log` (diva_m1 + D1 + D1b, about 3,600 extra presets) and `p3_d2w.log` (one knob moved on 1,000 bases, 44 banks).
Rerun if needed (one at a time):

```
cd tools/universe; A=~/Infinite/training-data/audio
~/Infinite/ml-venv/bin/python p3_characteristics.py $A/diva_m1.jsonl --extra $A/preset_diva_d1_{0,1,2,3}.jsonl $A/preset_diva_d1b_{0,1,2,3}.jsonl
~/Infinite/ml-venv/bin/python p3_characteristics.py $A/diva_m1.jsonl --extra $A/preset_diva_d2w_{0,1,2,3}.jsonl
```

Record the group and random medians in the 4b pilot table, then apply:

| Result | Decision |
|---|---|
| D1b group median - 0.160 >= 0.04 | the curve holds; add the point; Diva rendering stops for P3 (the projection stays under 0.6) and continues only as map rows for P4 |
| D1b gain < 0.04 | the curve is flattening: same decision, stronger |
| D2w group >= D1 group (0.160) | sweep design in Step 2 = 70% one-knob moves on base points, 30% fresh base points |
| D2w group < 0.160 | sweep design = 30% one-knob moves, 70% fresh base points |
| any two results within 0.03 | rerun both with `--seeds 3` before deciding (1 seed is the noise level) |

Done when: the table has the two rows, HANDOFF status names the decision, commit.

## Step G - fill every data gap (owner 2026-10-28: no lossy, missing or thin data)

Runs before Step 2's first batch (G1, G2 decide the render format) and then beside the steps, one heavy job at a time. Each gap:
tool, output, check. Done = row in `HANDOFF.md` section 3 says "filled" with the count.

| # | Gap | Do | Output | Check |
|---|---|---|---|---|
| G1 | lossless renders of our engines | each render row stores: the full recipe (engine, every param, note, velocity; renders are deterministic, so the recipe *is* the lossless record, re-renderable any time), 128-band mel at 10 ms, the partial list (top 64 peaks: frequency, level, decay each), noise envelope in 32 bands, the release tail; velocities pp / mf / ff; notes C1-C6 spread; param ranges read from the node sources; wav kept for every frozen test row and listening set | `own_<engine>.jsonl` schema 3 | re-render of 20 recipes matches its stored row bit for bit (or r >= 0.999) |
| G2 | missing measurements | add to `characteristics.py`: partials (roughness, inharmonicity, odd / even, decay per partial), formants (21), glide by YIN (22), space from the release (20), vibrato and tremolo rate / depth, velocity response (brightness change pp -> ff), tuning in cents | new axis columns | `characteristics.py test` gets one synthetic check per new measurement (bell inharmonic, square odd-only, 808 glide down, dry vs reverb tail ...) |
| G3 | samples as full data | re-measure the 195k samples from the WD-Black drive: curves over time, partials, pitch by YIN, G2 measurements | `features_v3.jsonl` | 50 files measured twice identical; pitch matches the key in the file name on >= 90% where named |
| G4 | MIDI fully extracted | CCs (which, when, how fast), meters incl. non-4/4, tempo changes, instrumentation / orchestration, EMOPIA (raw MIDI kept until done) | `store/midi_full.jsonl` | counts per field reported; 20 files checked by hand |
| G5 | genres, subgenres | `atlas/genres.json` + tracks with genre: each genre as a region of the 22 axes plus structure (tempo, groove, chords, form) | `atlas/genre_regions.json` | claim C8 |
| G6 | node expressions, patch pieces | chains of nodes as the second learner level: ingredient = a node or Field expression in a chain; parsed from `expression_pieces`, `patch_pieces`, Ableton device chains | `store/chains.jsonl` | claim C9 (after C1, C2 pass on single engines) |
| G7 | user behaviour | from the local Prediction data: what a user keeps, undoes, tweaks; stays on the machine, never uploaded | per-user critic offset | claim C10 |
| G8 | mix data | from Ableton sets (`arcs/als*.jsonl`): track levels, pans, sends, device chains; from album and loop audio: balance, masking, crest, LUFS | `store/mix.jsonl` | counts reported; 10 sets checked by hand |
| G9 | audio embedding | a pretrained embedding (CLAP for audio + text, and a codec, EnCodec or DAC) on every render, sample and map row; model download needs the owner's yes | `embeddings/*.npy` | claim C11 |
| G10 | wavetables measured | every frame of `wt.jsonl`, `wt_synth.jsonl`, Vital tables on the 22 axes + partials (tables are single cycles, partials are exact) | `characteristics/wavetables.jsonl` | 64 built-in tables measured from the node agree with their source |

New claims from the gaps (same rules as C1-C7):

| # | Claim | Dataset | Frozen test set | Bar |
|---|---|---|---|---|
| C8 | understands genres and subgenres | genre regions (G5), tracks and loops with genre in name or folder | tracks of held-out packs | genre from axes + structure: top-3 accuracy >= 0.7; a reference check, not truth (owner 6.3) |
| C9 | knows what a node in a chain does | chains (G6), rendered with one node changed | 200 held-out chain moves | C2 bars on chains |
| C10 | learns a user's taste | local behaviour (G7) | the user's last 20% of actions | personal critic beats the shared critic on that user's keeps, AUC +0.05 |
| C11 | the axes hold the sound | embeddings (G9) of held-out rows | 2,000 rows | R2 of the embedding from the 22 axes, reported per round; each new axis must raise it; what stays unexplained is listened to and named |

## Step 2 - Analog sweep generator (P3 on the learner) - one session

Tool: `tools/universe/own_sweeps.py --node <Engine>` (new, tool-first), one tool for every engine (owner 2026-10-28: every
sound must be makeable by Infinite's own engines). Engines, all note-driven, `Synths` category (`Infinite --describe`):

| Engine | Params | What it reaches | Sweep order |
|---|---|---|---|
| Oscillator | 22 | single waves, unison, filter, ADSR: the simplest recipe | 1 (smallest, fastest proof of the tool) |
| Analog | 33 | subtractive: basses, leads, pads, plucks; noise knob | 2 |
| Wavetable | 73 (two layers a / b) | 64 built-in tables x 8 frames: formant, vocal, bell, glass, reed, brass | 3 |
| FM | 35 (4 operators) | bells, e-pianos, metallic, basses | 4 |
| Metallic | 15 | struck metal, percussion | 5 |
| String | 8 | plucked, bowed, struck strings | 6 (also Step 6) |

Noise: there is no standalone audio noise node (the `Noise` type is the picture node); noise comes from Analog's `noise` knob.
Wavetable data (`training-data/wavetables`: 1,432 tables x 64 frames, 20,000 synthetic harmonic spectra, 126 Vital tables): the
node plays only its 64 built-in tables, so this data cannot be swept yet. Use it now as the oscillator-level map (measure each
frame's brightness, odd/even, formant); loading a table file into the node is a new capability, shown to the owner first.

Pieces that exist, to reuse, not rewrite:

| Need | Reuse |
|---|---|
| patch with notes, batch render in one process | `render_pass.write_notes`, `render_pass.render_states` (pass whole patches instead of plug-in states) |
| row format that `sound_data.load` reads (schema 2, knobs, mel, notes C3 / C4 / C3f) | `preset_state.py` flush(): same fields, `knobs` = the values we set, `params_hash` = hash of the key list |
| bank / group for the split | `src = "own:analog:<design>/<id>"`; design name is the bank, so a whole design can be held out |
| measuring | `characteristics.from_mel` through `p3_characteristics.py` unchanged |

Node facts (`Infinite --describe Analog --json`): category `Synths`, input `notes`, 33 params: volume, freq, glide, pitchBend,
fine1, semi1, oct1, fine2, semi2, oct2, pw1, osc1Vol, osc2Vol, voices, spread, fm, detune, oscMix, sub, noise, cutoff, resonance,
drive, keyTrack, attack, decay, sustain, release, wave1, wave2, filterType, sync, analog. `--describe` gives no ranges: read them
from `src/nodes/AnalogNode.cpp` / `AnalogSynthCore.h` (min, max, log or linear) and write them into the tool as one table.
Fixed, not swept: volume, freq, pitchBend, voices (1), glide (0). Swept: the other 28.

Patch per render (validate the first one with `Infinite --validate`, then batch):

```
infinite-patch 1
node 1 Notes Keyboard
  id keys
end
node 2 Synths Analog
  id syn
  f cutoff <v> ... one line per swept knob (tag from --describe: f float, i int)
end
node 3 Utility Audio Out
  id out
end
aud out audio syn
note syn 1 keys
```

Designs, each a folder name (= bank):

| Design | What | Count |
|---|---|---|
| `base` | base points: every swept knob drawn uniformly in its range (log for times and frequencies) | per Step 1 mix |
| `one` | one knob moved on a base point to a value drawn the same way (|move| > 10% of range) | per Step 1 mix |
| `pair` | two or more knobs moved together on a base point; first batch uniform, later rounds where the models disagree most about the combined effect | 10% |

First batch: 2,000 renders (about 30 min at 4 shards; split into two 15 min jobs), stop file `~/STOP_OWN`.
Before Step 3: build `claims.py` and run `claims.py freeze` for C1, C2 and C3 (their test sets are drawn from the first batch and
the map rows, then never trained on); C4-C7 are frozen when their step starts.
Check: row count grows within 3 min; `characteristics.py test` still passes; 2 renders of the same point agree r >= 0.97.

## The self-learning loop (as agreed with the owner; nothing in it is hand-picked)

Built only from what the owner already approved, so later sessions do not reinvent it:

| Agreed (where) | What it means here |
|---|---|
| Map vs learner ("correct you cracked it", this plan) | other synths, presets, samples, wavetables = map of real, good sounds; Infinite's engines learn, without limit |
| Five ways to learn what each ingredient does (owner "okay good") | 1 one-knob tests, 2 sweeps, 3 knob combinations, 4 render where the model is unsure, 5 the DSP math of our own engines as a prior |
| Moves are generative, no limit (owner) | a move = target point minus current point on the 22 axes; the model finds the knobs. No list of moves, no fixed recipes |
| Creativity, defined (`dials.md`, approved 2026-10-27) | novelty N + reach R (rare knobs, rare values), above the critic floor K; variety V reported; freedom dial c |
| Understand through the characteristics (owner, P1) | every judgement is made on the 22 axes, never on knob names |
| Everything connected to everything (owner) | one model over all engines and all knobs; no hand-made pairs, cells or groups |

What is learned and what is not. The rule: **nothing about sound is decided by hand; only the bars, the budgets and the axes' math are**.

| Part | Learned how | Not allowed |
|---|---|---|
| Ingredient -> sound | a nonlinear network per engine (M1 trunk: knobs + note -> 22 axis curves), with a DSP prior for our engines (way 5): the known part of the response (e.g. cutoff -> centroid, envelope times -> attack / decay) is an input feature, the network learns the rest | a straight-line (linear) map, one effect per knob |
| What each knob does | read off the network along the whole knob range at the current sound (way 2, a curve, not a slope); combinations from the network's mixed response (way 3) | a fixed table "attack up = softer" |
| Which combinations to test | the pairs and groups where the models disagree most about the combined effect | fixed pairs (filter x resonance ...) |
| What to render next | expected information gain: candidates where 3 seeds disagree most (way 4), drawn by the freedom dial so rare knobs and values get their share | fixed ratios of designs |
| What is good | critic learned from the map vs random renders, on the axes only (dials.md floor K); hard checks (silence, clipping, DC, tuning) | hand rules of taste |
| What is new | novelty N and reach R from `dials.md`, measured against everything rendered so far | a hand-cut grid of the space |
| Clusters (pluck, pad, bass ...) | regions found in the data (density of the map on the 22 axes), named afterwards from preset and sample names | categories decided first |

How sure: every claim has its own dataset and bar, in the claims registry below. No claim counts until a render the model never saw
confirms it.

```
  map (Diva, Omnisphere, samples, wavetables)        learner (Oscillator, Analog, Wavetable, FM, Metallic, String)
        |  measured on the 22 axes                        |  rendered without limit, chosen by the model
        v                                                 v
  critic: what is good  ----------------->  network: ingredients -> 22 axes  (+ DSP prior)
  clusters: found, not given                      |  unsure where? -> render there (way 4)
                                                  |  predicts each change before it is rendered -> checked
  target = any point, or current + a move ------> search knobs on the network -> render -> measure
                                                  '-- every miss is new training data ----------------'
```

## Claims registry: what the model claims, and the numbers that prove it (owner 2026-10-28)

Each claim has one dataset, a test set frozen before any training touches it, one metric, one bar and the step that produces it.
Tool: `tools/universe/claims.py` (new) with `freeze` (writes `eval/splits/claims.json` once; a frozen set is never retrained on),
`predict` (stores the model's predictions with a hash and time before the render), `score` (renders exist -> metric -> pass / miss),
`table` (prints this registry with today's numbers into `HANDOFF.md` status).

| # | Claim | Dataset (held now or made in the step) | Frozen test set | Metric | Bar | Step |
|---|---|---|---|---|---|---|
| C1 | understands each characteristic axis | `own_<engine>.jsonl` renders; `diva_m1.jsonl` as the cross-check | whole designs held out (group split), and the 7 Diva banks of `sound_data.split` | R2 per axis mean, 3 seeds | >= 0.6 on the group split (P3, unchanged) | 3, 4 |
| C2 | knows what each ingredient does (one knob, combinations) | `one` and `pair` designs of `own_sweeps.py` | 200 moves (100 single, 100 combined) never trained on | prediction stored before render vs measured change | direction right >= 90%; size r >= 0.7 | 4 |
| C3 | knows what a good sound is | map rows (`diva_m1`, `features.jsonl` samples, Air samples) vs uniform random renders | 1,000 map rows + 1,000 random renders, held out | AUC on the 22 axes only | AUC >= 0.8; owner blind listen: of its 20 top and 20 bottom picks, owner agrees on >= 30 of 40 | 5 |
| C4 | understands characteristics in words (warm, glitchy, dark, spacious ...) | words in preset and sample names, folders, tags; `atlas/character_matrix.json` for word -> nodes | presets of held-out banks, per word with >= 30 examples | word from the 22 axes, AUC; word -> made sound | AUC >= 0.75 per word (M4, unchanged); asked for a word, >= 70% of 50 made sounds are scored as that word | 5, then 6 |
| C5 | understands moods and emotions | the owner's emotion rules on the axes (P6; draft from the owner's stance: arousal from loudness, density, attack, brightness; valence from tension, roughness, brightness; Russell's valence-arousal plane), signed by the owner | 500 held-out renders; 50 made sounds per mood | (a) rule on predicted axes vs rule on measured axes; (b) asked for a mood, where the made sound lands | (a) r >= 0.8 per dimension; (b) >= 80% in the asked region; owner blind listen >= 70% agree | P6 |
| C5r | mood reference check (never truth, owner section 6.3) | `mood/song_mood.jsonl` (203k rows, valence / energy), its 965 chord files named by mood, the owner's albums `albums/n1m21n_tracks.jsonl` | fixed 2,000-row sample | agreement of the rule with each reference | reported, no bar; disagreement > 30% goes to the owner, the rule does not change by itself | P6 |
| C6 | is creative | renders from 20 held-out contexts at c = 0, 0.5, 1 | the 20 contexts | `dials.md`: N, R, K, V | c = 0 is typical (N < 0.1); N and R rise with c; K above floor at c = 0.7; owner hears c = 0 / 0.5 / 1 | 5, 6 |
| C7 | can make a sound | targets from the map | 300 targets (150 Diva, 150 samples) | distance of the render to the target on the 22 axes | closer than the nearest existing render, within repeat noise on passed axes (P5, unchanged) | 6 |

Rules that make the numbers trustworthy:

| Rule | Why |
|---|---|
| test sets frozen by `claims.py freeze` before the first model of that claim; a file hash is checked on every score | no leakage, no quiet re-picking |
| predictions written before the render (`claims.py predict`) | the model cannot be scored on what it already saw |
| 3 seeds for every number; a difference under the seed spread is "no change" | 1 seed is the noise level (about +-0.03) |
| every claim beats a named baseline (k-NN in knob space, random knobs, nearest existing render) | a bar passed by the baseline proves nothing |
| owner listening is blind (order shuffled, no labels) and stored in `eval/listen/` | perception checks stay honest |
| a miss is written with its learning curve, never re-scored with an easier bar | bars do not move |

## Step 3 - understand: P3 on each engine (the real P3 bar)

```
p3_characteristics.py own_<engine>.jsonl                      # group split = whole designs held out
p3_characteristics.py own_<engine>.jsonl --frac 0.25 / 0.5    # learning curve, 3 points
```

| Result | Next |
|---|---|
| a characteristic at >= 0.6 | record it as passed; keep it in every later run |
| all rising with data | Step 4 chooses the next 2,000, not more uniform draws |
| one stays flat | diagnose that axis first (silent rows, measurement, range of the knob) before any render |

Order: Oscillator (proves the tool), Analog, then the others in the Step 2 table, one engine per run.

## Step 4 - change ingredients: the model picks the next renders (P3), until the bar or the curve flattens

Per round, 2,000 renders chosen by the model: from 20,000 unrendered candidates (drawn with the freedom dial c = 0.5, so rare knobs
and values are present), take the ones with the largest disagreement of 3 seeds on the 22 axes, single knobs and combinations alike;
200 uniform draws stay in every round as the honest check. Before rendering, each model writes its predictions; after, record the
prediction test (sign, size) and the P3 curve point. Stop rule: two rounds with < 0.02 gain on the group median -> write the miss and
the curve, go on with what passed. Tool: `own_sweeps.py --propose <model dir>`.

## Step 5 - judge: good and new

Tool `tools/universe/sound_space.py` (new): `critic` (train / score on axis summaries, map vs random renders), `novelty` (N and R
of `dials.md` for any render against all renders so far), `clusters` (regions found in the map's density on the 22 axes, named
from preset and sample names afterwards), `report`. Runs on rows that exist; no renders of its own. Bars in the "how sure" table.

Wavetable data as the oscillator-level map: measure each frame of `wt.jsonl` and `wt_synth.jsonl` (brightness, odd / even,
formant) into the same space; report how much of their region the 64 built-in tables reach. A large unreached share is the evidence
for loading table files into the node (owner design first).

## Step 6 - reach test: map -> Infinite engines (P4 then P5 preview)

For 300 map sounds (Diva, samples), start from the nearest rendered sound of any engine, search knobs on the forward model, render, measure the
distance. Bar (P5): closer than the nearest existing render, and within the repeat noise on the axes that passed P3. Misses are
rendered rows for the next Step 4 round. Tool: `p4_shared.py` for distances, `sound_space.py` for the start points.

## Step 7 - String vs the library's acoustic sounds (instrument level, after Step 3 runs)

Same generator with `--node String` (8 params: exciter, position, tone, decay, stiffness, body, release, volume). Target points:
library samples whose names hold guitar, harp, pluck, violin, cello, piano (`features.jsonl`, names only). Report per family the
distance to the nearest String render. That number decides the owner's next design (better String, piano model, wind model, hybrid
attack) - shown to the owner, not built here.

## Parked, with what unparks each

| Item | Unparks when |
|---|---|
| axes 20-22 (space, vowel, glide) measured | Step 2 rows exist as wav (space needs release, glide a pitch tracker) |
| Omnisphere and other synths as map rows | Step 6 shows the map needs regions Diva lacks |
| owner's preset drive | a learning curve says more map rows help |
| piano / wind / resynthesis nodes | Step 7 numbers, then owner approval of a design |
| P6 maps (families, emotion rules; C5 needs the owner's signed rules) | P4 passes |
| loading collected wavetables into the node | Step 5 shows the built-in tables miss a large part of their region; owner design first |
