# Execution plan from 2026-10-28 (follow in order, no redesign needed)

Serves the plan in `HANDOFF.md` (P1-P6, bars unchanged). Every step names the phase it serves; a step not on this list is not
done in the session. Decisions are written as rules below, so the session runs them, records the number and moves on.

## Rules that keep the original plan intact

| Rule | Meaning in practice |
|---|---|
| Phase order | P3 bar (each characteristic mean R2 >= 0.6 on the group split) before P5; P4 before P6. No skipping |
| Bars do not move | a miss is recorded with its learning curve, never re-scored with an easier bar |
| Data rule | no render without a step here that names the error it fixes and the test that shows it helped |
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
```

If a log is missing and nothing runs: rerun the missing line from Step 1 (commands there).

## Step 1 - close the Diva pilot (P3, data contract) - 10 min reading, no new renders

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

How sure: no claim counts until a render the model never saw confirms it.

| Claim | Test, written down before the render | Counts when |
|---|---|---|
| the model understands an axis | P3: group split, whole designs held out, 3 seeds | R2 >= 0.6 (bar unchanged) |
| the model knows what a knob does here | it predicts the change of a real one-knob or pair render before it happens | predicted vs measured change: sign right >= 90%, size r >= 0.7, on 200 held-out moves |
| the model can make a sound | P5: target point, the model's knobs, render, measure | closer than the nearest existing render, within repeat noise on passed axes |
| the critic knows good | held-out map rows vs held-out random renders | AUC >= 0.8, and the owner hears 20 of its top and bottom picks blind |
| creativity is real | `dials.md` checks: c = 0 reproduces typical, N and R rise with c, K stays above floor at c = 0.7 | all three hold on held-out contexts |

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
| P6 maps (families, emotion rules) | P4 passes |
| loading collected wavetables into the node | Step 5 shows the built-in tables miss a large part of their region; owner design first |
