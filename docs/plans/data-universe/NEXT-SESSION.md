# Generate Samples: what is left (rewritten 2026-10-30 end of day, for the next session)

Plan: `PLAN-2026-10-30.md` (23 items, signed off). Results: `HANDOFF.md` (one entry per item; a miss is recorded as a miss).
Read `docs/DECISIONS.md` and this file first.

## Rules that bit last session
- Pro laptop, `main`, commit per step; no push or deploy without the owner (item 23).
- One heavy job at a time (8 GB Mac). Builds only through `python3 scripts/ibuild.py run -- nice -n 10 cmake --build build -j3`.
- Python: `~/Infinite/ml-venv/bin/python3 -I` (import numba first when `tools/universe` is on `sys.path`).
- Never commit `tools/seo/ledger.json`, `docs/plans/piano-roll/`, `external/vst3sdk`. Don't touch the `wt-piano-roll` worktree.
- A sweep that scores 0 test pairs usually means a knob is gated by another (Note Stack's `semiN` needs `enabledN`): check
  `SweepPrerequisitesFor` first.

## State per item
| Item | State |
|---|---|
| 1-4, 7-9, 11 | in HANDOFF (4 valence MISS, 7-8 String MISS, 9 Wind family MISS, 11 pitch MET except mallets) |
| 5-6 | Effect knob maps: 20 of 27 MET |
| 10 | Piano node shipped; family ratio 6.26, MISS |
| 12 | Nothing passed a family test, nothing added |
| 13 | String MET at base (0.846); Wavetable 0.23 after one round, MISS; Sampler not run. Stopped by the owner: not paying off |
| 14 | MISS stands at 79.3%. Repeat test: renders flip on only 5.6% of clear moves, so noise is not the limit; the one-direction-per-knob model is |
| 17 | 909 figures + MPC / SP-1200 swing in the Drums groove, MET, drum gate holds |
| 20, 22 | MET |
| 21 | Learner built; blocked on data (0 real takes). Rerun `learn_from_logs.py learn` after a few hundred owner takes |
| 15, 16, 18, 19 | **Not started.** Deprioritised on 2026-10-30: the owner asked to stop work that is not leading anywhere substantial |
| 23 | Ship: only when the owner says |

## If the owner picks the generator-sound items back up
| Item | What | Pointers |
|---|---|---|
| 15 | Synths low end | Wavetable and Analog lines under the motif, then refit the synths spectral gate (`spectral_gates.py`) so octave 3 can pass. Bar: synths centroid and low share toward the library. |
| 16 | Bass and kick click transients | SubLab transient samples in `~/Infinite/musical_datasets/6_sub_bass_and_kicks/sublab`. Bar: click measure against the library. |
| 18 | Textures merged with foley | Bar: textures against the library. |
| 19 | Diva closed loop | `vital_voice_bank.py --diva`. Diva rows are held out (`BankVoice` keeps `src == 0`) because they made leads and synths darker. Bar: no darker. |

## Item 23, ship (needs the owner)
Desktop deploy of what passed (`scripts/deploy_desktop.sh`, headless smoke with 0 fails). Before any push, reconcile `main`
with `origin/main`.

## Loose ends
- The starter profile may carry a little self-test noise (item 22 caveat).
- Nothing in the app reads `learning/profile/user_profile.json` or `learned_weights.json` yet.
- The Air (192.168.29.223) is idle; its round-b Wavetable rows are in its own `own_wavetable.jsonl`, not synced here.
- `/Applications/Infinite.app` (frozen sweep copy) is in the Trash as `Infinite-frozen-sweeps.app`.
- Data folders: `~/Infinite/training-data` (12 GB) and `~/Infinite/datasets/midi` (7.3 GB) are both still read (gate refits,
  sweeps, knob maps; MIDI extraction of arcs, CCs, non-4/4, orchestration and EMOPIA is not done). They can move to the WD
  Black if a symlink stays at each old path; nothing should be deleted.
