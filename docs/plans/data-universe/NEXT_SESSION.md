# Start here (written 2026-10-29, night)

**2026-10-29 status, gaps and plan per model: `STATUS-2026-10-29.md`.**

Goal: GOAL.md. Work reference for every model: IMPROVEMENT-PLAN.md. Status per bar: HANDOFF.md (newest sections at the end, "2026-10-29 (night)").
Branch feature/sample-dots, nothing pushed. `tools/seo/ledger.json` has an unrelated uncommitted change: leave it.

## Running when this was written (check, then finish these first)
1. Done: the dot-shaped critic renders and v3 (see HANDOFF).
2. Done: MIDI axes rerun (292,825 measured), note_spread (rest now 34% inside: new MISS) and v_boost (unchanged). See HANDOFF "2026-10-29 (late)".
3. Note effects (plan 8.2): no headless note dump exists (`--audio-summary` only). Route: note phrase -> note node -> sine Oscillator -> Audio Out, measure
   onsets and pitch from audio. Needs the release build (the dev build is ~10x slower); run when nothing else is heavy.

## Remaining work, in order
| # | Item | State | Next action |
|---|---|---|---|
| 1 | Role critic (item 4) | v3 **MET** (rest 1.3% / 98.5%, dots 2.9% / 98.0%); shipped in RoleCritic.cpp behind Generate Samples (redraws 8.9% -> 9.4%) | full app build + run; copy build-wasm to website/gen after Naman's yes; real-instrument check (WD Black drive) |
| 2 | Knob direction (item 3) | six engines 89.5-95.6%; **Wavetable 75.0% MISS**; across notes (knob_notes): Analog/Osc/FM hold, Metallic/String miss at C1/C5/C6 | one-knob Wavetable renders (`own_sweeps.py`, raise the "one" share), rerun knob_direction; one heavy job at a time |
| 3 | Words (C4) | **5 of 28** words reach 0.75 (words_eval2) | decided 2026-10-29: only those 5 drive generation, the rest stay unlinked; next the made-sounds half |
| 4 | Note model (item 2) | key lenient 76.5% MET (Temperley); chord +14.3 MET; register MET; **density MISS** | decided 2026-10-29: music not thinned, MISS stays; next a genre-matched (electronic) MIDI corpus; bass-lift diff sits in `git stash` |
| 5 | Sound model (C1/C2) | R2 0.21, data-limited | more own-engine renders; cross-engine model on spectrograms |
| 6 | Note / audio effects | not measured | input->output per note node; effects on renders, frame_features before/after |
| 7 | Genre (C8) | style critic on own renders top-3 79% (with tempo); real music untested | groove + frame features per style, exact labels from generator renders |
| 8 | Valence (C5) | V r 0.51-0.55 | public V/A dataset after a licence check; untrusted files read with python -I in their own folder |
| 9 | Instruments | String far from acoustic families | guitar body into String, then bowed / wind / piano (designs first) |
| 10 | Wire dots to learned models | hand rules in SampleStyles.cpp | only after the bars above pass |
| 11 | Sample studio app panel | design drafted: SAMPLE-STUDIO-DESIGN.md | Naman signs off the design, then build in its 5 steps |
| 12 | Needs Naman | | 40-pair blind test, 40 mood clips, C10 on people |

## Rules (from memory)
One heavy job at a time; ask before agent batches; build via scripts/ibuild.py (never bare -j); no Claude attribution in commits; commit per step;
no bar moves without Naman; record a miss as a miss; no push or deploy without asking; never read GPL/LGPL source.
