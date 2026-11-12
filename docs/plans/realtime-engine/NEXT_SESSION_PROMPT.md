# Next session: start here

Paste this as the first message:

> You are continuing the real-time engine in /Users/namansoni/Infinite/infinte-base (branch feature/engine-e-speed).
> Read docs/plans/realtime-engine/NEXT_SESSION_PROMPT.md, then docs/plans/realtime-engine/OPUS_PLAN.md sections 1
> and 2 in full. Do the next rows in the state table below, in order, as many as the session can finish, each following its "read first"
> step, its done-command and its gate. You execute the plan; you do not redesign it. Small mismatches between the
> row and the code: settle them with the smallest change that meets the gate and log the deviation. Blocked only
> when rule 14 forbids it or the gate cannot be met; then take the next row with no dependency on it. Rows in the
> same area may share one build and one self-test. No polishing: a frozen block is never reopened. Each row ends
> with a short entry in AUTORUN.md (numbers, deviations, commit; no pasted logs), the state table in this file
> updated, and a commit (no attribution lines, never push). Do not read AUTORUN_ARCHIVE.md.
> Report with one table: row, state, number, commit.

## The five rules that are broken most often

| # | Rule |
|---|---|
| 1 | As many rows per session as can be finished, one commit per row, in order (owner, 2026-11-12). Each row still meets its own done command before the next starts. |
| 2 | Lighter routine (owner, 2026-11-12: "relax the routines and sweeps and do the actual work"). Per row: build + self-test only if engine code changed; sweep only the cases the row touches (plus the 3 smoke cases only when shared shading code changed), one engine unless the row is engine-specific. No full sweep per block: one full sweep at the end of the plan. `AUTORUN.md` entry: one line, under 300 characters (result + deviations). Proof-only rows become a quick check inside the next feature row. A cached case costs about 3 s: the Blender truth is keyed on `TRUTH_VERSION` in `blender_scene.py`, bump it only when an old case's picture changes. |
| 3 | One build at a time, `python3 scripts/ibuild.py build --mode release`; never during a sweep; nothing over 20 minutes. |
| 4 | No Co-Authored-By or "Generated with" lines. Never push. Never merge to main. |
| 5 | Papers, own derivations and Blender's source as a reference (owner, 2026-11-12). Cycles files under Apache 2.0: formulas may be ported, with a line in `THIRD_PARTY_NOTICES.md`. GPL Blender code: read for the idea only, never copied or translated. Blender renders the A/B truths and the gate decides. See OPUS_PLAN rule 12. |

All 16 rules: `OPUS_PLAN.md` section 2.

## State (update at the end of every session)

| Row | State | Commit |
|---|---|---|
| 0.1 build WIP + self-test | done (no code change; envis compiles) | see log |
| 0.2 `envis` measure | done (default on; world_hdri_sun 0.458 -> 0.077) | see log |
| 0.3 adaptive stop default | done: default 0.05 (0.08 and 0.06 failed the image gate) | see log |
| 0.4 dark far maps | closed with a known gap (owner): grazing glint moved to row 5.7 | 0496f599 |
| 0.5 freeze + `f0` baseline | done: `f0` stored, R and T, 46 cases | see log |
| 1.1 to 1.5 material programs | done (details in `AUTORUN_ARCHIVE.md`) | — |
| 2.1 to 2.8 shading nodes | done (`Shade Attribute` not built: no vertex colour on Mesh) | see log |
| 3.1 to 3.5 script nodes | 3.1 to 3.5, 4.1 to 4.5, 5.1 to 5.6, 6.1 to 6.3, 7.1 done (5.7 after 8.2); **next: 7.2** | see log |
| 4.1 to 4.5 layers, relief, wear | open | — |
| 5.1 to 5.7 fibre, volumes, sky, lights, grazing glint | open | — |
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
| raster | `f0` | 46 cases, frozen L1 to L3 (row 0.5); `c1` is older |
| settle 16 spp | `f0` | 46 cases, frozen L1 to L3 (row 0.5); `c1` is older |
| settle 256 spp | `full` | 8 cases: cornell interior materials world_hdri mat_glass light_area bed deer |
| live | `base2` / `c1` | only 5 cases are current; a full live baseline is row 8.2 |

Row 0.5 creates `f0` (raster, settle); row 8.2 creates `f8` (live). From then on those are the bases.

## Handover 2026-11-12 (row 7.2 in progress, WIP commit)

| item | state |
|---|---|
| case | `tools/blender-ab/scale/carpet_pile.py out.inf [N] [size]`: Plane -> Distribute Points on Faces (density N on the unit plane, cap 200 k, pointSize 2) -> Instance on Points cloud slot (`geo 3 2 8`), cone shape |
| fixed | Instance on Points cloud path now applies the cloud's model matrix and the same random scale/rotation/align/offset as the mesh path (`PointPose`, GeometryOpNodes.cpp); cloud model is in the dirty check |
| not fixed | GPU-cloud path (GpuPointsGL vertex shader) still ignores the cloud model matrix |
| measured, L 384 px | 200 k copies at pointSize 0.05 (tiny): 5.3 s, 688 MB; at pointSize 2: 45 s, 706 MB. Gate (L 30 fps at 1080p, T under memory cap) is far off |
| next | profile where 45 s goes (likely one engine::Instance per copy + per-frame TLAS rebuild + per-copy BLAS work at large size); then compact per-copy records (matrix + prototype index), TLAS copies pointing at the prototype RInst via user id, cache the TLAS when nothing changed, per-instance random to Shade Geometry. Then self-test, AUTORUN 7.2 line, real "Engine 7.2" commit, then 7.3 design-only |
| rules | swap was 3.4 GB of 4 GB; 384 px tests, one render at a time, cap 3 min |
