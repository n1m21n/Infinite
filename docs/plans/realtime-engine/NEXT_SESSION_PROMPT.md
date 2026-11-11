# Next session: start here

Paste this as the first message:

> Continue docs/plans/realtime-engine/FINISH_PLAN.md from the checkpoint in
> docs/plans/realtime-engine/NEXT_SESSION_PROMPT.md. Worktree /Users/namansoni/Infinite/infinte-base, branch
> feature/engine-e-speed. Same rules as before.

## Rules (unchanged)

- Work only in `/Users/namansoni/Infinite/infinte-base` on `feature/engine-e-speed`. Never push, never merge to main.
  No attribution lines in commits.
- One build at a time: `python3 scripts/ibuild.py build --mode release`. Never build during a sweep. No job over ~20 min.
- A candidate sits behind `#ifdef R3D_AB_<NAME>` (C++: `R3dAbOn("NAME")`), measured with `sweep.py --ab <name>`.
  Keep only if no case is more than 0.005 FLIP worse. Three failed attempts: log and skip.
- New nodes, modes, panels, UI: design doc only, "waiting for owner". Papers only, no GPL source. No downloads.
- Log every step in `AUTORUN.md`. Self-test after builds:
  `INFINITE_ENGINETEST=1 script -q /dev/null timeout 300 build/Infinite.app/Contents/MacOS/Infinite`
  (only the R1 speed row is a known fail).

## Where it stands (2026-11-11)

| Row | State | Commit |
|---|---|---|
| 1.6 settle multiscatter | done, default | 6f59e164 |
| 1.6 `ptfresnel`, `ggx` | fail the gate, stay switches (ggx: 3 attempts used) | — |
| 2.1 Toksvig, curvature widening | measured, dropped | — |
| 2.1 map mips on raster (bias -1) | done, default | 6f59e164 |
| 2.8 thin film (physical, all engines) | done, default | 6f59e164 |
| 2.9 tex_breakup out of gate | done | 85b3def7 |
| 2.9 emission proxy | skipped (raster already lit by emitters through probes) | — |
| 1.5 adaptive stop | built, knob only, off by default | WIP commit |
| 2.4 HDRI importance sampling | code written, never built | WIP commit |
| Tier B (2.2, 2.3, 2.5, 2.6, 2.7, 4.4, 4.5, 1.9, 3.1, 3.3, 1.4) | not started | — |
| Tier C | designs in `designs/`, waiting for owner | — |

## Do next, in this order

1. **Build** (the WIP commit's HDRI code has never compiled). Fix any shader error, run the self-test.
2. **2.4**: `sweep.py --engine settle --spp 16 --tag envis --base c1 --ab envis run world_hdri_sun world_hdri`
   (world_hdri_sun is new: its Cycles truth renders on first run). Gate: world_hdri_sun at 16 spp within 0.01 of
   its 256 spp FLIP; world_hdri and the other HDRI cases (`--touch hdri`) 0 worse. If it wins, remove the `#ifdef`.
   Watch: the miss weight uses `envMis`; the sky scale on the sample is `skyK` blended by the diffuse lobe's share.
3. **1.5**: limit 0.08 halves easy cases with FLIP at most +0.001 (0.02 and 0.04 never fire). Still to do: boat
   s/frame, image-to-image FLIP against the full count, then default `INFINITE_ENGINE_STOP` to 0.08.
4. Commit 1.5 and 2.4 as their own steps, update `AUTORUN.md`.
5. Tier B in plan order (briefs in `FINISH_PLAN.md` sections 6.9 to 6.15). Likely skips to state honestly: 1.9 Vulkan
   (weeks, and no Vulkan device here to verify).
6. End with the table of every step: done / skipped (why) / waiting for owner.

## Baselines in ~/.cache/infinite-sweep

| Engine | Tag | Notes |
|---|---|---|
| raster | `c1` | 44 cases, current code |
| settle 16 spp | `c1` | 44 cases, current code |
| settle 256 spp | `full` | 8 cases (cornell interior materials world_hdri mat_glass light_area bed deer) |
| live | `base2` / `c1` | only 5 touched cases re-run; hair cases are ~150 s each |

Sweep options go before the subcommand; in zsh pass case lists as arrays.
