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
| `pair` | two knobs moved together on a base point (filter x resonance, attack x decay, fm x detune, noise x cutoff) | 10% |

First batch: 2,000 renders (about 30 min at 4 shards; split into two 15 min jobs), stop file `~/STOP_OWN`.
Check: row count grows within 3 min; `characteristics.py test` still passes; 2 renders of the same point agree r >= 0.97.

## The self-learning loop (owner 2026-10-28: understand first, recipes come out of it)

The goal is not a recipe list. The model **understands** sound through the P1 characteristics, and from that understanding it
**invents its own recipes** by changing ingredients (engine, knobs, notes), keeps what is good and new, and learns from every miss.
Recipes are a by-product of understanding; creativity is the point. The map (Diva, Omnisphere, samples, wavetable data) is a
guardrail for "this is a real, good sound", never a template to copy (same rule as the MIDI corpus).

```
           +--------------------------- learns from every render -------------------------------+
           v                                                                                      |
 1 UNDERSTAND                 2 CHANGE INGREDIENTS            3 RENDER + MEASURE          4 JUDGE |
 forward model per engine     curiosity: where the models     Infinite headless,          critic: good? (map vs random)
 knobs + note -> the 22 axes  disagree (unknown) and where    characteristics.py          novelty: a new place in the space?
 + "what does this knob do    the space is empty (new);       (same rules as the map)     keep -> the sound space (atlas)
   here" (gradient)           mutate kept sounds, cross       miss = model was wrong  ----+  drop -> still training data
                              engines, pairs of knobs
```

| Part | What it is | Serves | Done / bar |
|---|---|---|---|
| Understand | forward model per engine (Step 3) + the knob-effect read-out: for any sound, which knob moves which axis and how far (finite differences on the model, checked by real one-knob renders) | P3 | P3 bar unchanged; read-out agrees in sign with real renders on >= 80% of checked moves |
| Change ingredients | proposals = ensemble disagreement (unknown) + empty cells of the space (new) + mutations of kept sounds (small knob moves, pairs, engine swap) | P3 data rule: every render is chosen because it teaches or explores | each round's renders raise the curve or fill cells; two flat rounds -> stop (Step 4) |
| Judge: good | critic trained on characteristic curves: map rows (human-made presets, samples) vs uniform random renders; plus hard checks (silence, clipping, DC, tuning off > 30 cents, stuck notes) | guardrail | held-out map rows ranked above random renders, AUC >= 0.8; the critic never sees knobs, only the shared space |
| Judge: new | the sound space as a grid over 6 axes (brightness, attack, decay, noisiness, weight, movement; 5 bins each) = MAP-Elites (Mouret and Clune 2015) / novelty search (Lehman and Stanley 2011); each cell keeps its best-judged sound with its recipe | P4 / P5 index | cells filled, and share of map-occupied cells reached, recorded per round |
| Learn from misses | every render is a training row; where the model predicted wrong, the next round draws more near it | P3 | the curve point per round in `SOUND-MODEL.md` 4b |

Making a sound (P5, after P3 passes): target = any point (a map sound, a word later, or "this pluck, but as a pad" = current point
plus a move). Start from the nearest kept sound of any engine, search its knobs on the forward model, render, measure; the miss
goes back into training. Moves are generative (target minus current), never a fixed list.

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

## Step 4 - change ingredients: active learning + curiosity (P3), repeat until the bar or the curve flattens

Per round, 2,000 renders: 50% where 3 seeds disagree most (from 20,000 unrendered candidates), 30% mutations of kept sounds into
empty cells, 20% uniform (keeps the curve honest). Refit, record the curve point and cells filled. Stop rule: two rounds with < 0.02
gain on the group median -> write the miss and the curve, go on with what passed. Tool: `own_sweeps.py --propose <model dir>`.

## Step 5 - judge: the critic and the sound space

Tool `tools/universe/sound_space.py` (new): `critic` (train / score, logistic or small MLP on characteristic summaries), `atlas`
(cell grid, best sound per cell, recipe = engine + knobs + note), `report` (cells filled, map cells reached, per engine). Runs on
rows that exist; no renders of its own. Critic bar: AUC >= 0.8 on held-out map rows vs held-out random renders.

Wavetable data as the oscillator-level map: measure each frame of `wt.jsonl` and `wt_synth.jsonl` (brightness, odd / even,
formant) into the same grid; report the share of their cells the 64 built-in tables reach. A large unreached share is the evidence
for loading table files into the node (owner design first).

## Step 6 - reach test: map -> Infinite engines (P4 then P5 preview)

For 300 map sounds (Diva, samples), start from the nearest kept sound, search knobs on the forward model, render, measure the
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
| loading collected wavetables into the node | Step 5 shows the built-in tables miss a large share of their cells; owner design first |
