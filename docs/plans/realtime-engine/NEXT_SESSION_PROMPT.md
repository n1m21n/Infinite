# Next session: start here

Paste this as the first message:

> You are continuing the real-time engine in /Users/namansoni/Infinite/infinte-base (branch feature/engine-e-speed).
> Read docs/plans/realtime-engine/NEXT_SESSION_PROMPT.md, then docs/plans/realtime-engine/OPUS_PLAN.md sections 1
> and 2 in full. Do the next row in the state table below: exactly one row, in order, following its "read first"
> step, its done-command and its gate. You execute the plan; you do not redesign it. If the row is unclear or the
> code does not match it, log what you found in AUTORUN.md, mark the row blocked, and take the next row with no
> dependency on it. No polishing: a frozen block is never reopened. Finish by pasting the done-command output
> into AUTORUN.md, updating the state table in this file, and committing (no attribution lines, never push).
> Report with one table: row, state, number, commit.

## The five rules that are broken most often

| # | Rule |
|---|---|
| 1 | One row per session. Do not "also quickly" do the next one. |
| 2 | No row is done without its done-command output in `AUTORUN.md`. |
| 3 | One build at a time, `python3 scripts/ibuild.py build --mode release`; never during a sweep; nothing over 20 minutes. |
| 4 | No Co-Authored-By or "Generated with" lines. Never push. Never merge to main. |
| 5 | Papers only. Blender renders truths as a black box; its source is never read. |

All 16 rules: `OPUS_PLAN.md` section 2.

## State (update at the end of every session)

| Row | State | Commit |
|---|---|---|
| 0.1 build WIP + self-test | done (no code change; envis compiles) | see log |
| 0.2 `envis` measure | done (default on; world_hdri_sun 0.458 -> 0.077) | see log |
| 0.3 adaptive stop default | done: default 0.05 (0.08 and 0.06 failed the image gate) | see log |
| 0.4 dark far maps | **blocked**: not a map read; grazing glint (G2 on raster, + an unprobed live sun term). Waits for owner: freeze with gap, or reopen `ggx` | see log |
| 0.5 freeze + `f0` baseline | open | — |
| 1.1 to 1.5 material programs | open | — |
| 2.1 to 2.8 shading nodes | open | — |
| 3.1 to 3.5 script nodes | open | — |
| 4.1 to 4.5 layers, relief, wear | open | — |
| 5.1 to 5.6 fibre, volumes, sky, lights | open | — |
| 6.1 to 6.3 camera | open | — |
| 7.1 to 7.3 assets, scale | open | — |
| 8.1 to 8.6 live | open | — |
| 9.1 to 9.6 Vulkan | open; 9.3 on waits for download approval | — |
| 10.1 to 10.4 real scenes | open | — |

Done before this plan (commits 85b3def7, 6f59e164, 4b7701fa): settle multiscatter, physical thin film on all
engines, map mips on raster, `tex_breakup` out of the gate, adaptive stop knob (off), HDRI sampling written but
never built. Dropped after measuring: Toksvig, curvature widening, emission proxy. Still switches: `ptfresnel`, `ggx`.

## Commands

| What | Command |
|---|---|
| Build | `python3 scripts/ibuild.py build --mode release` |
| Self-test | `INFINITE_ENGINETEST=1 script -q /dev/null timeout 300 build/Infinite.app/Contents/MacOS/Infinite` (only the R1 speed row fails) |
| Sweep | `python3 tools/blender-ab/sweep.py --engine raster\|live\|settle [--spp 16] --tag T --base c1 [--ab name] run [cases]`, then the same with `diff` |
| Pixel identity | `python3 tools/blender-ab/sweep.py --engine E [--spp 16] --tag T --base B same` |
| Which cases a change touches | `python3 tools/blender-ab/sweep.py pick GROUP` |

Options go before the subcommand. In zsh pass case lists as arrays. Hair cases take about 150 s each on live.

## Baselines in ~/.cache/infinite-sweep

| Engine | Tag | Notes |
|---|---|---|
| raster | `c1` | 44 cases, current code |
| settle 16 spp | `c1` | 44 cases, current code |
| settle 256 spp | `full` | 8 cases: cornell interior materials world_hdri mat_glass light_area bed deer |
| live | `base2` / `c1` | only 5 cases are current; a full live baseline is row 8.2 |

Row 0.5 creates `f0` (raster, settle); row 8.2 creates `f8` (live). From then on those are the bases.
