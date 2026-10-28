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

Tool: `tools/universe/own_sweeps.py` (new, tool-first). Pieces that exist, to reuse, not rewrite:

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

## Step 3 - P3 on Analog (the real P3 bar)

```
p3_characteristics.py own_analog.jsonl            # group split = whole designs / folders held out
p3_characteristics.py own_analog.jsonl --frac 0.25 / 0.5    # learning curve, 3 points
```

| Result | Next |
|---|---|
| a characteristic at >= 0.6 | record it as passed; keep it in every later run |
| all rising with data | Step 4 (active learning) chooses the next 2,000, not more uniform draws |
| one stays flat | diagnose that axis first (silent rows, measurement, range of the knob) before any render |

## Step 4 - active learning (P3) - repeat until the bar or the curve flattens

Train 3 seeds; draw 20,000 candidate points (no render); render the 2,000 where the 3 models disagree most on the characteristic
means; refit; record the point on the curve. Stop rule: two rounds with < 0.02 gain on the group median -> write the miss and the
curve, go to Step 5 with what passed.

## Step 5 - reach test: Diva map -> Analog (P4 then P5 preview)

For 300 Diva presets (map rows), ask the Analog model for knobs that hit the Diva preset's characteristic point; render; measure the
distance. Bar (P5): closer than the nearest existing Analog render, and within the repeat noise for the axes that passed P3.
Tool: `p4_shared.py` for distances, plus a small search (random restarts over the model, no new library).

## Step 6 - String vs the library's acoustic sounds (instrument level, after Step 3 runs)

Same generator with `--node String` (8 params: exciter, position, tone, decay, stiffness, body, release, volume). Target points:
library samples whose names hold guitar, harp, pluck, violin, cello, piano (`features.jsonl`, names only). Report per family the
distance to the nearest String render. That number decides the owner's next design (better String, piano model, wind model, hybrid
attack) - shown to the owner, not built here.

## Parked, with what unparks each

| Item | Unparks when |
|---|---|
| axes 20-22 (space, vowel, glide) measured | Step 2 rows exist as wav (space needs release, glide a pitch tracker) |
| Omnisphere and other synths as map rows | Step 5 shows the map needs regions Diva lacks |
| owner's preset drive | a learning curve says more map rows help |
| piano / wind / resynthesis nodes | Step 6 numbers, then owner approval of a design |
| P6 maps (families, emotion rules) | P4 passes |
