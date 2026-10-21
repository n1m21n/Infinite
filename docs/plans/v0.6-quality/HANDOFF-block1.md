# v0.6 Block 1 handoff (bugfix/v06-audio-correctness)

Updated 2026-10-13. The session-local note isn't committed. Read `docs/NOW.md` first, then the plan at `docs/plans/v0.6-quality/README.md` (60 items, main 6723f618).

## Rules carried by this branch

- One commit per item, subject `v0.6 1.x: ...`. No attribution lines. `git add` with explicit paths only.
- Build with `cmake --build build -j6`. An unbounded `-j` froze the owner's laptop. Run driver.sh with `--skip-build` (its own build uses -j8).
- Run benches serially: one Infinite run at a time, and none while re-survey-finish benches.
- No push and no merge to main without the owner asking. Run `verify-gate` before the merge.
- Don't commit these (they aren't ours): `tools/roadmap/{audit.json,metrics.jsonl,quests.jsonl}`, `external/vst3sdk`, and the regenerated NOW.md facts block.
- Merging main: in NOW.md keep our claim row, take main's lines, then run `python3 tools/lifecycle/now.py`.

## Block 1 status

| Item | State | Result / next |
|---|---|---|
| 1.1 Export latency | done | PDC drain |
| 1.2 Audio Filter click | done 189b6858 | 17.3 → 0.3 dB |
| 1.3 Bitcrush rate spread | done eccce82b | 5.38 → 0.0 dB; the 6 kHz probe note is in registry.tsv |
| 1.4 Metallic rate spread | done 8101f4b8 | 2.93 → 0.14 dB |
| mailbox priming | done a497cb34 | a slot's first value lands without ramping up from 0 |
| 1.5 AV react latency | done cc5265e9 (main merged after) | 49.96 → 16.6 ms, frame error 0 (reactive-file 61.4 → 28.1) |
| 1.6 Transient-aware stretch | done c2f7d0d1 | drums: extra onsets 14 → 0, timing 30 → 3.3 ms, smear 8.5 → 4 ms; tonal unchanged; crest 2.5 dB recorded as F20 |
| 1.7 unmeasured P8 metrics | waiting | 12 now (R5 x4, B7 x3, live x2, B3 edit x3). All need Infinite's window focused: runs from a background shell while the owner works report `unfocused=1` and don't count. Run with the Mac left alone: `caffeinate -dimsu scripts/bench/run_all.sh --soak --r5 --quiet --only B3,B6,B7`. re-survey-finish (B1-B10 into scoreboard) is still claimed |
| 1.8 Node audit coverage | done 55f36a04 | families were swept by L1-L7; re-run gives GEO PASS 22 / COMP PASS 2, all accepted; row emptied |
| 1.9 Default levels | done 0531853e | Dynamics threshold -18 -> 0 dB (sweep prereqs {threshold,-30}, incl. `mix` via ExtraParamDefs); Delay/Reverb were sine-coherence artefacts (pink: +0.17/+0.40); row fx.insert_gain_db, F21 |
| 1.10 Chorus/Flanger THD -1 | done db820427 | probe artefact (F22): kernels null to -143 dB between -1 and -12 dBFS (new row fx.hot_null_db); dry+wet peaks 1.26/1.21 and the integer WAV writer clamps. builtins_kit marks it with †. Real fix is 1.13 |
| 1.11 Synth defaults | done 084631cd | Field Synth default: cubic soft clip (alias -44 -> -83 dB at MIDI 108), even detune; rest by design (F23). Metallic: probe artefact (F24). New SYNTHAUDIT alias row for Field Synth |
| 1.12 RC-20 host crash | done 7d537ae3 | not a host bug: the plugin copy null-derefs in its own code at creation; auval and Apple's AU host die at the same frames (Tier 3). host_audit now has a `Whose` column and only fails on `host?` crashes |
| 1.13 Master clipping | done 79c8e6b4 | engine counts master samples over full scale (live + offline); Audio Out shows `clipping +x dB` for 3 s; Timeline renders end with an "Audio clipped" notice; load_bench trims by 1/N (16 tracks peak 0.75). Follow-up (review after verify-gate): the notice read the last over block's peak, now the worst of the take (held peak, reset per render). Output node's own Record/Render now append `(audio clipped, +x dB over)` to the status line, measured on the recorded samples (2026-10-14; RECSYNCTEST). Audio Out's own Record now shows `last take clipped, +x dB over` after Stop (7f002bee, AUDIOPDCTEST). New wording is user-visible: mention to owner |
| 1.14 Edit without a hitch | done 83a7040c, one confirmation left | bench `INFINITE_BENCH_B3EDITS=1` (in run_all.sh); late edits 13/75 -> 0-1/73, xruns 0. Fix: compiled shader stages cached by source in `GLUtil::CompileProgram` + effect shader prewarm one a frame (`FilterNode::WarmNextShader`). Three more runs 2026-10-13 22:10 were all `unfocused=1` (0 xruns; late edits 11, 3, 2, one 203 ms audio_connect outlier once), so they don't count. Left: re-run on a quiet, unlocked screen (measured with screen locked + load avg 6-10; first Blur add was 1-2 ms over) |
| 1.0 Competitor specs | done c64eb6fe | `param_diff.py`, 12 specs in `specs/`; `diff.py claims` also lints the `~/re-output` summaries |
| 1.0b Data ledger | done d87d70d7 | `data_ledger.py lint` 0 without a consumer / 0 stale; `community_patches.py --smoke` 27/27; verify-gate runs both. All eight Infinite kits re-run 2026-10-13 evening (builtins-kit included, so that end step is done). host_audit exits 1 on `clipped`/`silent` flags, not on crashes (0 unexplained) |
| end | done except merge | verify-gate run (audio+video 25 passed, ui+compositing 51 passed; ledger staleness clock fixed in b20cd26e); claim dropped; merge question is in NOW.md "Waiting on owner"; now.py run. Not merged, not pushed |

## Code review of the branch (2026-10-14, infinite-code-review, read of all 38 src files' diffs)

No blockers, nothing to fix. Checked and clear: shader stage cache eviction can't delete a stage in use (vertex and fragment maps are separate; contexts share objects); `M_PI` in ClipTimeStretch is covered by the project-wide `_USE_MATH_DEFINES`; transient/envelope vectors are written once at decode and only read on the audio thread; KeepTransients, NoteMasterOvers, the mailbox priming and both kernel changes do no allocation or locking. Consider only: the modulation pass now does two `dynamic_cast`s per node per frame (main thread; about 0.1-0.2 ms at 4000 nodes, unmeasured); Bitcrush still holds 0 until its first hold instant (same as before the branch).

## Sweeps (2026-10-14, HEAD 3b-series after 7f002bee)

av-sync 19/19, modulation 9/9, hygiene audio+ui 55 passed. audio-pipeline 17/18 and compositing-pipeline 4/5 each fail one check that fails identically on main 3ef67425: the audio-pipeline driver reads AUDIOPARAMSWEEPTEST's raw verdict instead of `audio-param-sweep-expected.txt` (hygiene's audio group applies it and passes), and IMAGERESYNTH_SELFTEST counts 46 texture-less node types (notes, utility, some 3D) as failures. Not this branch's; flagged as a separate task.

## 1.5 cause and fix

- `ApplyModulationAndPalette` (src/app/frame/CookFrame.cpp) reads every modulator's `Value01()` before the cook loop. Audio Analyze and Audio File set their levels and onset envelope in `CookIfNeeded`, so bindings saw the previous frame's analysis: one frame late, live and offline.
- The fix splits the level read into `RefreshAnalysis(frameId)` (once per frame; the cook calls it too). The modulation pass calls it on non-bypassed Audio Analyze and Audio File nodes before any binding is read.
- Tried and reverted: pumping offline audio before the cook. It's a no-op, because the previous frame's pump already reaches this frame's start.
- Regression: `av_bench.py --self-test` end-to-end check, frame error {0} and ≤45 ms.

## 1.6 cause and fix

- Signalsmith's 120 ms window spreads each hit: pre-echo ~half a window early (~135 ms at 2x), plus flams and smear.
- `DetectTransients` (src/audio/dsp/TransientDetect.h) finds hit starts and a 2.5 ms peak envelope once, where the file is decoded: in the import worker (ArrangeMediaImport) and in the decode cache. It isn't run on the main thread at open. `OpenFromDecoded` only falls back to detecting when `envelopeHop == 0` (the sweep tone).
- `ClipTimeStretch::KeepTransients` caps the output at 1.41x the source envelope (removes pre-echo), then plays each hit's first 25 ms unstretched at the pitch ratio and fades back over 30 ms.
- Probe: stretch_kit now emits QUALITY rows (stretch.extra_onsets, timing_ms, crest_err_db). The crest reference is the clipped ideal, because the 24-bit write clips. The crest gap is F20: a spectral stretch keeps decays proportional (accepted).

## Current (2026-10-13, second session)

Nothing uncommitted for 1.0-1.14. HEAD a6582524: after the 1.13 held-peak fix (38e754be) builtins-kit and load-bench were re-run (ledger lint 0 stale, quality check pass, numbers unchanged). Any further edit under `src/audio` makes those two stale again. After 1.14: `driver.sh --skip-build --auto` 69 passed; `--group compositing,3d,ui` 73 passed. Next: the 1.14 quiet-screen confirmation, then the end steps. 1.7 still waits on re-survey-finish.

1.14 bench notes: an unfocused or locked window is unpaced (frame about 12 ms, variant gets `unfocused=1`); the edit verdict then uses the whole frame as work time. Run it with the screen unlocked and nothing else building.
