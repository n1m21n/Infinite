# Next session plan (written 2026-10-28)

Goal: a better sample generator, rated by the owner on WHOLE samples. Layers join only through the patch and the 22 axes (INTEGRATION.md).

## State now
- Feel dataset built (feel/dataset, 5 tables, 42.8k rows; mostly priors, README says which part is measured).
- Cue rule vs human labels: EMOPIA V .43, E .77; chord files V .41, E ~0. Not good enough to train on yet.
- Synth knob direction accuracy 63.5% Analog / 61.8% Oscillator (bar 90%). C3 listening 24/40 (bar 30). 3,894 instruments unassigned.

## Order of work (one block at a time, commit per step)
| # | Block | Done when |
|---|---|---|
| 1 | Add axes 23-31 to the measurer, MIDI side first (mode, tempo, swing, micro-timing, silence, band energies, LUFS/crest, width, section arc) | columns exist for all 294,582 MIDI files; spot-check 20 |
| 2 | Fit V/E/T weights on EMOPIA 1,078 + chord-file words (hold out 20%); owner signs off C5 rules | held-out r >= 0.8 on V and E, owner agrees on >= 70% of 40 clips |
| 3 | Render the 3,894 unassigned instruments, measure the 22, assign clusters | unassigned < 5% |
| 4 | Replace rule rows with measured: per-stem band energy of our renders -> Mix table; Harmonix Set -> section/genre targets | Mix table has no grade-C role-by-band rows |
| 5 | Measure what is missing: 13 synth nodes, 24 note nodes, modulators, effect pairs | Nodes/Knobs table: measured column covers all audio nodes |
| 6 | Per-part critic (hi-hat vs hi-hats) and re-run C3 on a fresh frozen set | owner agrees >= 30 of 40 |
| 7 | Remaining Steps 3-7, claims C1-C11 in EXECUTION.md; jitter fix, then B2/B3 benchmark with daemon paused | claims table updated |
| 8 | compose.py: notes + recipe + chain + mix in one patch; whole-sample blind test vs today's site generator | owner rating |

## Rules
Pause the daemon for benches; one heavy job at a time; ask before starting agent batches; chat corpora never pushed; no Claude attribution in commits (project rule). Air has more presets: pull only if a result needs them.
