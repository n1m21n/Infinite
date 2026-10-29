# Start here (written 2026-10-29, night)

Goal: GOAL.md. Work reference for every model: IMPROVEMENT-PLAN.md. Status per bar: HANDOFF.md (newest sections at the end, "2026-10-29 (night)").
Branch feature/sample-dots, nothing pushed. `tools/seo/ledger.json` has an unrelated uncommitted change: leave it.

## Running when this was written (check, then finish these first)
1. Role critic v2 renders, 4 shards (rest: `--first 20000 --seeds 10 --style-from 12/13 --style-step 2`; shaped: `--first 40000 --seeds 5 --shaped --style-from 0/1 --style-step 2`)
   into `~/Infinite/training-data/audio/gen_parts_v2_{rest0,rest1,shaped0,shaped1}.jsonl`; then they are appended to `gen_parts_v2.jsonl` (1,800 rows of styles 0-11 already there).
   Expect 7,650 rows. If a shard died, resume it with `role_critic_render.py render ... --style-from N --style-step 2 --rows <shard file>` from the first missing style, then append.
   Then both tests, recorded in HANDOFF against feel/role_critic_render_oof.json (FRR 2.6%, catch 97.9%):
   `python3 tools/universe/role_critic_render.py train --train-on rest --out docs/plans/data-universe/feel/role_critic_v2_rest.json`
   `python3 tools/universe/role_critic_render.py train --train-on all --out docs/plans/data-universe/feel/role_critic_v2_all.json`
2. MIDI axes full rerun after the re-strike fix (`midi_axes.py run --jobs 3`, then `counts`) -> `store/midi_axes.jsonl` (old output: `midi_axes.pre-restrike.jsonl`).
   Record counts in HANDOFF (item 1), then rerun what reads it: `note_spread.py`, `fit_ve.py`, `v_boost.py`; record any change.

## Remaining work, in order
| # | Item | State | Next action |
|---|---|---|---|
| 1 | Role critic (item 4) | MET on own audio (2.6% / 97.9%); v2 tests above | real-instrument check once frame features run on real audio (WD Black drive); export for the app |
| 2 | Knob direction (item 3) | six engines 89.5-95.6%; **Wavetable 75.0% MISS** | one-knob Wavetable renders (`own_sweeps.py`, raise the "one" share), rerun knob_direction; one heavy job at a time |
| 3 | Words (C4) | **5 of 28** words reach 0.75 (words_eval2) | Naman: may words the audio cannot carry (clean, soft, ...) be dropped? Then the made-sounds half |
| 4 | Note model (item 2) | key lenient 76.5% MET (Temperley); chord +14.3 MET; register MET; **density MISS** | Naman: density bar vs a pop corpus (options in HANDOFF); bass-lift diff sits in `git stash` |
| 5 | Sound model (C1/C2) | R2 0.21, data-limited | more own-engine renders; cross-engine model on spectrograms |
| 6 | Note / audio effects | not measured | input->output per note node; effects on renders, frame_features before/after |
| 7 | Genre (C8) | at chance on axes | groove + frame features per style, exact labels from generator renders |
| 8 | Valence (C5) | V r 0.51-0.55 | public V/A dataset after a licence check; untrusted files read with python -I in their own folder |
| 9 | Instruments | String far from acoustic families | guitar body into String, then bowed / wind / piano (designs first) |
| 10 | Wire dots to learned models | hand rules in SampleStyles.cpp | only after the bars above pass |
| 11 | Sample studio app panel | plan settled | build per plan |
| 12 | Needs Naman | | 40-pair blind test, 40 mood clips, C10 on people; the density and words questions above |

## Rules (from memory)
One heavy job at a time; ask before agent batches; build via scripts/ibuild.py (never bare -j); no Claude attribution in commits; commit per step;
no bar moves without Naman; record a miss as a miss; no push or deploy without asking; never read GPL/LGPL source.
