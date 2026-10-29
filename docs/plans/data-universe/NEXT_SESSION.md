# Start here (written 2026-10-29, evening)

Goal: GOAL.md. Work reference for every model: IMPROVEMENT-PLAN.md. Status per bar: HANDOFF.md (newest sections at the end). Branch feature/sample-dots, nothing pushed.

## Running or just finished when this was written (check, then finish these first)
1. Wavetable render (`own_sweeps.py run --node Wavetable --count 6000 --shards 2 --budget 120`) stops by itself; a waiter then reruns
   knob_direction for Wavetable only and writes feel/knob_direction.json. Check: `git diff docs/plans/data-universe/feel/knob_direction.json`.
   If it did not run: `python3 -c "import sys,json;sys.path.insert(0,'tools/universe');import knob_direction as k;d=json.load(open(k.OUT));d['Wavetable']=k.run('Wavetable',5);json.dump(d,open(k.OUT,'w'),indent=1)"`
   Record in HANDOFF (bar >= 90%) and commit.
2. Role critic v2 renders -> ~/Infinite/training-data/audio/gen_parts_v2.jsonl: 5,100 at rest (seeds from 20000) then 2,550 with random
   dots (`--shaped`, seeds from 40000). Expect 7,650 rows: `wc -l` it. If short, delete the file and rerun both:
   `python3 tools/universe/role_critic_render.py render --seeds 10 --first 20000` and `... render --seeds 5 --first 40000 --shaped`.
3. Then two tests, both style-held-out, and record both in HANDOFF (IMPROVEMENT-PLAN.md section 1, steps 1-2):
   `python3 tools/universe/role_critic_render.py train --train-on rest --out docs/plans/data-universe/feel/role_critic_v2_rest.json` (does a critic trained with dots at rest hold when dots move?)
   `python3 tools/universe/role_critic_render.py train --train-on all --out docs/plans/data-universe/feel/role_critic_v2_all.json` (trained on both)
   Compare with feel/role_critic_render_oof.json (FRR 2.6%, catch 97.9%) to see what the note features added.

## Remaining work, in order (all approved by Naman)
| # | Item | State | Next action |
|---|---|---|---|
| 1 | Role critic (item 4) | own-audio critic MET: FRR 2.6%, catch 97.9% (out-of-fold cut-offs) | check against real instruments once frame features run on real audio; export the model for the app |
| 2 | Knob direction (item 3) | 6 engines 89.5-95.6; Wavetable short of data | rerun after the render above; then test held-out notes (C1/C5/C6, pp) for all engines |
| 3 | Words / tagging (C4) | 5 of 30 words testable | label words from ~40k preset names + tags (presets.jsonl, air_*), measured features; pack held out |
| 4 | Note model (item 2) | key 51%, chord +5.6 (bar +10), register MISS | compare n-gram vs sequence models on held-out MIDI; retry 372 MIDI files; bass-lift wasm rebuild + role_spread vs scratch baseline, then commit SampleStyles.cpp |
| 5 | Sound model (C1/C2) | R2 0.21, data-limited | more own-engine renders; one cross-engine model on full spectrograms, using renders/spectrum.jsonl and the own_*.jsonl mel windows |
| 6 | Note effects / audio effects | not measured | each note node input->output on corpus phrases; effects applied to renders (frame_features before/after) |
| 7 | Genre (C8) | at chance on axes | groove + frame features per style; generator renders give exact style labels |
| 8 | Valence (C5 V r 0.51-0.55) | needs outside data | download a public V/A dataset after a licence check (Naman OK'd); untrusted files read with python -I in their own folder |
| 9 | Instruments | String far from acoustic families | guitar body fit into String -> re-test string_families; then bowed, wind node, piano node (show designs first; prior art in docs/prior-art/physical-models-2026-10-29.md) |
| 10 | Wire dots to learned models | hand rules in SampleStyles.cpp (:162, :1030, :1038, :1420) | only after the bars above pass |
| 11 | Sample studio app panel | plan settled (summary in the old session; planner output) | build per plan: Shift+W, dock right/top/bottom, brand tokens ember-200/400, signal-300/500 |
| 12 | Leftovers | | pitch-from-audio harmonic fix; Grain Molder `path` in the param table; C6, C7, C9, C10; 40-pair blind test and 40 mood clips (need Naman); claims ledger in HANDOFF |

## New tools this session
- `tools/universe/wasm_render_parts.js`: render any part of any style with the live generator, audio streamed to stdout.
- `tools/universe/frame_features.py`: frame-by-frame measurement, reusable for real audio, effects and genre.
- `tools/universe/role_critic_render.py` (render / train), `role_critic_samples.py` (real-pack summary critic, MISS, kept as the baseline).

## Rules (from memory)
One heavy job at a time; ask before agent batches; build via scripts/ibuild.py (never bare -j); no Claude attribution in commits; commit per step;
no bar moves without Naman; record a miss as a miss; no push or deploy without asking; never read GPL/LGPL source.
