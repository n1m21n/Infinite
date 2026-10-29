# Start here (written 2026-10-29, evening)

Goal: GOAL.md. Status per bar: HANDOFF.md (newest sections at the end). Branch feature/sample-dots, nothing pushed.

## Running or just finished when this was written
- Wavetable render: `own_sweeps.py run --node Wavetable --count 6000 --shards 2 --budget 120` (log in the old session's scratchpad). It stops by itself.
  Then: `python3 tools/universe/knob_direction.py --engines Wavetable` and commit feel/knob_direction.json. Re-runs resume (content hashes).

## Remaining work, in order (all approved by Naman)
| # | Item | State | Next action |
|---|---|---|---|
| 1 | Role critic (item 4) | own-audio critic: catch 98.4% MET, FRR 6.1% MISS | thresholds from out-of-fold probs over all training styles; retest |
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
