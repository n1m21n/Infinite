# Next session: finish the data universe, in this order, no detours

Read this, `README.md` (tracker) and `training-plan.md` (criteria C1-C6, gates G1-G5). Nothing else is in scope.
New ideas go to the bottom list and wait until step 6 is done.

## Setup already done (2026-10-27)

| Item | State |
|---|---|
| Drive mirror | `python3 -I tools/universe/sync_drive.py` (rows + code, verified, 0 failed). Re-run after any new rows. |
| Colab notebook | `Infinite_Universe_Trainer.ipynb` in Drive `Infinite-Cloud-Training/`; CPU runtime is enough |
| Colab MCP | `colab-mcp` added to user config (connected). Needs a Colab notebook open in the browser, then `open_colab_browser_connection` |
| Audio pass (WD-Black) | `features.jsonl` has 75,760 rows; `collect_check.md` still lists 52 folders missed (README says 18,015, training-plan says ~66,000: both stale, fix in step 3). Air render pass 3,510 rows, 4 failures |
| Disk | Pro 18 GB free, Air about 13 GB |
| `tools/universe/genres.py` | written, uncommitted, never completed a run (no `atlas/genres.json` yet) |

## The plan (each step ends with a number, then the next step)

1. **Genres run**: `python3 tools/universe/genres.py` (plain python3, not `-I`). Done = `atlas/genres.json` exists and the top genres look sane. Commit `genres.py`.
2. **Colab parity check** (benchmark only, NO retrain: training-plan rule 17). First re-run `sync_drive.py`: it used to skip `store/`, so the 1.34 GB `store/inbox.jsonl` was never on Drive (fixed 2026-10-27, gzips to `inbox.jsonl.gz`). Notebook now has `RETRAIN = False` and writes `results/colab_<date>.txt`. Done = Colab gates match Pro (8/10).
3. **Unread formats on WD-Black** (`.asd .cst .bwproject .nksn .nka .fxp .sfz .amxd`): one metadata-only reader. Done = `collect_check.py` shows 0 unread of those.
4. **Air leftovers**: 3,103 `.ffp`, Serum 2 knobs, Bitwig bodies; retry the 4 render failures on Pro. Done = rows pulled, atlas rebuilt.
5. **Per-patch render test** of one LABS or Omnisphere patch on the Air. Done = a yes/no on joining presets to sounds.
6. **Retrain** (set `RETRAIN = True` in the notebook) only when C1-C6 hold; keep the model only if no gate gets worse (G5). Then the full mix (README step 6) and node proposals (step 7, shown to Naman first).

## Rules for the run

- One step at a time, in order; update the README tracker at the end of each step, commit per step.
- Pause the daemon before benchmarks; one heavy job at a time (8 GB Mac); builds only via `scripts/ibuild.py`.
- Never delete datasets, builds or trash without a per-folder yes. `main` is not pushed.
- Park, don't chase: new ideas go below.

## Parked

- Learned audio embedding / sequence model on Colab GPU
- 2h C++ port (needs Naman's sign-off). CONFLICT to decide before step 6: README shows B4/B5 cannot reach 10/10 without 2h (critic reweighting alone: 0.815 -> 0.760, threshold <= 0.65), so either approve 2h or relax B4/B5 in training-plan.md
- Steps 2i, 2j, 2k
