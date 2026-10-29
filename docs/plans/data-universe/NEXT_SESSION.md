# Start here (written 2026-10-29)

Goal: GOAL.md (cleared on 2026-10-29; re-set with `/goal <text of GOAL.md>` if wanted). Status per bar: HANDOFF.md, last section "Session wrap". Last commit: 097688f2.

## State in one table
| Bar | Today | Next action |
|---|---|---|
| 1 data | unassigned 15.9% (bar <5%); ~372 MIDI files not retried | retry 365 timeouts (raise 20 s) + 7 dtype bugs; assign Steam by tags+nearest cluster; measure instruments with audio |
| 2 notes | key 51% exact / 70% incl. relative; chord +5.6 (bar +10); register/density per role missed | lift bass octave + density per role; ships only if it wins blind test |
| 3 knobs | Analog 89.5, Osc 91.6, FM 91.6, Metallic 95.6, String 92.8, Sampler 90.9; Wavetable none (silent) | focus ONLY on the 7 main synths (owner, 2026-10-29): Analog, Wavetable, Oscillator, Metallic, String, FM, Sampler |
| 4 critic | note-side done; sound-side role critic missed | more measured audio per instrument |
| 5 feel | E, V met; C4 word AUC: dark .749, punchy .726, hard .524, clean .500, tight .507 | label words from own render knobs, add measured features; drop un-audible words only with Naman's sign-off |
| 6-9 | mix, blind tests, determinism, B2/B3, deploy: not started | after 1-5 |

## First three jobs
1. Wavetable silence: scan one knob at a time (tools/universe/own_sweeps.py: param_table, recipe, render_batch; peak of o0.wav) -> fix ranges -> `own_sweeps.py run --node Wavetable --count 2000 --budget 20` -> rerun knob_direction.py -> commit feel/knob_direction.json.
2. Word labels from own renders (Analog first), rerun tools/universe/words_eval.py.
3. MIDI retries (item 1).

## Fixed this session (do not redo)
- patch_text now escapes newlines (Field Synth `code` works; peaks .29-.41); Slicer pinned to sliceBy 1; Grain Molder saves `path` (src/nodes/GrainMolderNode.cpp) but its `path` is not in the param table so sweeps still render silent. Field Synth/Slicer/Partials/Grain Molder are out of the main set; Field Synth is for custom-code sounds, later.

## Rules (from memory)
One heavy job at a time; ask before agent batches; build via scripts/ibuild.py; deploy via scripts/deploy_desktop.sh; no Claude attribution in commits; commit per step; chat corpora never pushed; no bar moves without Naman; release only when he says.
