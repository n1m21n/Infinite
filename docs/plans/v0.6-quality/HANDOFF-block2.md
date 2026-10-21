# v0.6 Block 2 handoff (feature/v06-sound), 2026-10-15

Branch `feature/v06-sound`, last Block 2 commit `6fcbbd64` (2.38). Nothing pushed, main not merged. This file is not committed (HANDOFF-*.md are never committed).

## State

| Item | State |
|---|---|
| 2.1-2.33 | committed, one commit each (2.34 Linear-phase EQ is deferred, row 171 in the plan) |
| 2.35 Show set | done 6e4613f9 |
| 2.36 Show safety | done f619f11f |
| 2.37 Plugin GL editor | code in 40fd4e77, **row stays open**: unverified on Windows |
| 2.38 Bar-quantized launch | done 6fcbbd64 |
| End of block | this file; data_ledger lint = 5 stale (below); builtins-kit refresh **not run** (blocked, below) |

## Open items per row

- **2.36**: the quit watchdog also cuts a legitimately slow quit at 4 s. The check reuses each node's `Issue()` only (no separate scan of text params). Panic is a latch.
- **2.37**: (1) compile on Windows; (2) rig test VST3 whose editor makes its own GL context on a child HWND; (3) the owner's Auto-Tune Pro check; (4) the Windows UI-thread watchdog (minidump, close a recovered stall with a badge). The root cause is from reading the code, never reproduced.
- **2.38**: the launch grid is a global Transport setting, not saved in patches; no MIDI/button trigger for it; no countdown display; the Launch card in the Show Set window was not checked by hand. Play from stopped is never delayed. Show Set, tests, patch load and offline use the immediate calls.

## End-of-block checks

- `data_ledger.py lint`: 0 without a consumer, **5 stale** (av-bench, builtins-kit, load-bench, project-bench, stress). All five are stale because Block 2 changed audio sources. Re-run each on a quiet machine; the commands are printed by `python3 tools/re-survey/data_ledger.py lint`.
- builtins-kit refresh (`builtins_kit.py infinite` then `analyse ... --out ~/re-output/builtins-kit/infinite.md`): the auto-mode classifier refused it because it overwrites files in `~/re-output`. Needs the owner's yes, or run it by hand with `INFINITE_BIN=build-check/Infinite.app/Contents/MacOS/Infinite`.
- Build on macOS at the 2.38 commit: `cmake --build build-check -j4` exit 0; `driver.sh --fast` 53 passed, 0 failed, 0 xfail; TRANSPORTTEST and SHOWSETTEST OK.

## Rules (unchanged)

One commit per item, explicit paths, no attribution lines, never push. Do not commit `tools/roadmap/*`, `docs/node_param_audit.md`, `external/vst3sdk`, HANDOFF-*.md, `docs/handoff/`, `docs/plans/two-laptops/`, other sessions' scripts, AGENTS.md, the NOW.md Laptops hunk, or the 18-line hunk in StageTestsB.cpp.

## Carried into the final report (from the 2.35 handoff)

- **ALIASTEST xfails**: Cycle Shaper sine @500 -90.2 vs -105; Analog FM -47.9 vs -54.
- **Test status**: RECEXPORTTEST's stale baseline now passes; PLUGINDRAGTEST is flaky; SHAPERESONATORTEST and FIELDGRAPHTEST are not in the driver.
- **Block 1 merge is blocked**: the owner runs `git merge --no-ff bugfix/v06-audio-correctness` in the main checkout.
- **Still to run**: 1.7 and 1.14 need quiet-Mac runs.
- **Not compiled on Windows/Linux**: 2.6, 2.15-2.17, 2.24-2.26, 2.31-2.33, 2.35, and now 2.36 (Panic and watchdog paths), 2.37, 2.38 (shared code, low risk). 2.31 changed NetCompat.h and the OscControl sockets; 2.35 touches MidiWin, MidiLinux and both platform dialog files.
- **Audits**: FXAUDIT Dynamics click +9.0 dB; MODAUDIT FRATE Envelope.
- **Known gaps**: Sampler has no Note Expression; `INFINITE_NO_FMA` moves macOS scalar output by a few ulps; the FM default patch has a -39 dB non-harmonic component; 2.24's 64-tracks-at-70% gate is not met on 4P+4E; three unwrapped i18n literals (BusyLine.h:50, ArrangeRender.cpp:558 and :578).
- **Notes 2.25-2.33**: 2.25 the diode is darker than the ladder by design; 2.26 pinch aliases at -50.6 dB; 2.31 OSC is loopback only, no token; 2.32 a high note with formant 0 gets quieter, by design; 2.33 String|release xfails the param sweep (the rig sends no note-off).
- **Operational**: a `pkill -f "cmake --build build"` from another session also kills `build-check` builds and leaves orphaned makes; check `pgrep -fl make` before rebuilding.

## After the wrap-up (2026-10-15)

- Block 1 (`bugfix/v06-audio-correctness`) was merged into `main` as a4827273 in `~/Infinite/worktrees/main`. Not pushed.
- The builtins-kit was refreshed (6 synths, 11 effects). Backup at `~/re-output-backups/builtins-kit.bak-2026-10-15`. 4 ledger kits are still stale: av-bench, load-bench, project-bench, stress.
- `main` has a stash "pre-block1-merge 2026-10-15: other session's NOW/HISTORY/roadmap edits" holding `docs/NOW.md`, `docs/releases/HISTORY.md` and `tools/roadmap/{audit.json,metrics.jsonl,quests.jsonl}`. It was left unapplied by the owner's choice because those files are regenerated. The roadmap session can pop or drop it.
