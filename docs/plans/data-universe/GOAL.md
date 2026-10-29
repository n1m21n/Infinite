# Goal prompt (revised 2026-10-29, signed off by Naman: "take your decisions")

Ship Infinite's sample generator as a measured, learned, mood-aware system. Done only when every bar below is met on held-out data, recorded in docs/plans/data-universe/HANDOFF.md, and the result is live in the desktop app and on the website. The live Generate Samples UI/UX stays unchanged; only the engine improves. A miss is recorded as a miss; no bar moves without Naman's sign-off.

1. DATA: axes 23-31 for every MIDI file that has notes (747 empty files excluded; retry the ~372 timeouts and bugs); unassigned instruments < 5% (today 15.9%): measure every instrument that has audio on this Mac, assign Steam entries by tags plus nearest cluster; every audio node (13 engines, 24 note nodes, modulators, effect pairs) has measured rows.
2. NOTE MODEL: key accuracy >= 70% counting the relative key (today 51% exact / 70%); next-chord top-1 beats the most-frequent baseline by >= 10 points on held-out progressions (today +5.6); generated notes inside the corpus 5th-95th percentile for >= 90% of patches on tempo, density, rest and swing, and on register per role (each layer against its own instrument family).
3. KNOB MODELS: knob-direction >= 90% on held-out instruments on the reliable knob/axis pairs, for all 13 engines (5 met); dots ship only for axes that hold >= 85% on fresh voices (Tone and Tail today).
4. SOUND CLUSTERS AND CRITIC: cluster assignment >= 85% held-out, two seeds (met); a sound-side per-part critic judging role fit, held-out FRR <= 3%, catch >= 96%.
5. FEEL: E r >= 0.8 (met); V restated to AUC >= 0.75 on held-out listeners plus r reported (AUC 0.763 met); Naman agrees on >= 28 of 40 blind mood clips after I have listened to every clip first.
6. MIX: no rule-only rows; stems measured from our own renders; chain choices checked against finished-track targets.
7. WHOLE SAMPLE: the live node with the dots engine beats the unchanged live node in a blind test, preferred on >= 30 of 40 pairs (compose.py is retired); instrument-synthesis test: ~10 instruments rendered by the engines, machine-checked into the right cluster, then rated blind by Naman.
8. MATH + FOUNDATIONS: each model has an objective, baseline and falsifiable test with repeated-split mean +- sd; render determinism r >= 0.97; claims C1-C11 marked pass/fail with evidence (add C8-C11 to the claims table); B2/B3 run with the daemon paused (ask Naman to keep the Mac quiet).
9. DEPLOY: ship in the desktop app (scripts/deploy_desktop.sh) and on the website only changes that win the blind test; no regression in audio, speed or existing patches. Release only when Naman says so.

Rules: one heavy job at a time; ask before agent batches; chat corpora never pushed; no Claude attribution in commits; commit per step; finish each item completely, never report "partly"; report outcomes faithfully.
