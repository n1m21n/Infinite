---
name: turbo-branch-workflow
description: Branching, gate and hand-over workflow for Infinite-Turbo (Windows) work toward 0.70 on the turbo/next integration branch. Use before starting any feature, refactor or fix on Infinite-Turbo, when deciding where a change goes (turbo/next vs a hotfix on turbo/windows-only), before declaring a task done, and before any commit, merge, tag or push. Replaces upstream's git-branch-workflow (which targets Naman's main and macOS paths).
---

# turbo-branch-workflow

Strategy, milestones and checklist: `docs/plans/NEXT-STRATEGY.md`. Item IDs (F1, P3, X1...):
`docs/plans/NEXT-BACKLOG.md`. Read both before starting a task.

## Branches

- `turbo/windows-only`: stable (v0.62.0). Hotfixes only.
- `turbo/next`: integration branch of 0.70. Never develop directly on it once commits are
  allowed; each task gets `feature/<id>-<slug>` (e.g. `feature/p1-compute-backend`) cut from
  `turbo/next`, merged back with `git merge --no-ff`.
- A hotfix on `turbo/windows-only` is merged into `turbo/next` the same day.
- Milestone tags are local (`v0.70.0-m1`); final merge `turbo/next` -> `turbo/windows-only` and
  tag `v0.70.0` only with the owner's explicit OK.

**Current owner instruction (2026-10-22): nothing is committed yet.** All work stays as local,
uncommitted changes on `turbo/next`. Do not create commits, feature branches with commits,
merges, tags or pushes until the owner says so. Check the project memory for newer instructions.

## Parallel work

At most 3 writing sessions at once, each in its own worktree, on disjoint folders
(`src/pop` | `src/app` | `src/rtx` | `src/nodes/Render3D*` | `src/platform` + video). Never two
sessions in `src/app` (main.cpp fragments) at the same time.

## Gate (a task is done only when all hold)

1. Release build of the full edition. Also the MIT edition (`test-windows.bat -Mit`)
   when the task touches CMake, `src/gpl`, `src/rtx` or ASIO.
2. `test-windows.bat` full suite: no failure outside `run-turbo-tests/known-baseline.md`.
3. patchcompat corpus: no ok -> crash/hang.
4. `test-windows.bat -PerfOnly -SkipBuild` (perf.ps1, A/B against the frozen reference build in
   `build/perf-ref`): no gated metric > 10% and > 0.5 ms worse. Refresh `build/perf-ref` only when
   the owner accepts a new cost level, and say so in the hand-over.
5. CPU -> GPU migration of a PointZ operator: equivalence env var run (max diff 0 or a
   documented tolerance) and its row in `docs/pointz-gpu-status.md`.
6. Review with `turbo-code-review` (plus `code-review` at medium, high for P1, P2, A1, A2, MV).
7. `CHANGELOG-TURBO.md`, `TODO-TURBO.md` and the checklist in NEXT-STRATEGY.md updated.
   Version numbers change only at the final merge (0.70.0).

Builds: the owner allowed Claude to build and run the tests on turbo/next. Close nothing the
owner has open: if `Infinite-Turbo.exe` is running, ask before building.

## Hand-over

Answer in Portuguese: what changed (files), what the gate showed (pass/fail counts, perf
deltas), what was not verified, and what the owner should look at in the app.
