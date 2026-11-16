# Auto perf hunt: rules every run follows

Goal: pick ONE department (AREAS.md), go deep, FIND ways to cut frame time (FPS), CPU/GPU milliseconds, audio callback load and latency, open a GitHub issue per finding, then EXECUTE it: implement, A/B before vs after, and close the issue with the numbers. EXECUTE.md is the exact loop.

What the run may do: edit source in its own worktree, commit on its own `perf/<date>-<area>` branch, update local `main` by fast-forward only. What it never does: push, tag, open PRs, release, touch the live checkout's working tree, or force anything.

An issue is a small proposal: metric, baseline, cause, change, expected gain, risk, how to prove it. The same issue is later closed by the run with the measured result.

## Where to work
- Never touch /Users/namansoni/Infinite/repo's working tree (the owner's live checkout).
- Dedicated worktree /Users/namansoni/Infinite/worktrees/perfhunt (separate from the bughunt one). Base is LOCAL `main` (it can be ahead of origin/main with unpushed merged work; the live checkout is usually on a feature branch, so `main` is free to fast-forward):
  - First time: `git -C /Users/namansoni/Infinite/repo worktree add --detach /Users/namansoni/Infinite/worktrees/perfhunt main && git -C /Users/namansoni/Infinite/worktrees/perfhunt submodule update --init external/vst3sdk`
  - Every run: `git -C /Users/namansoni/Infinite/worktrees/perfhunt checkout --detach main` (fetch origin first only to see whether origin/main has commits main lacks; never pull into main), then `git checkout -b perf/<date>-<area>`.
  - Also needed for A/B: the persistent base worktree via `scripts/bench/base.sh` (`../infinte-base`; never delete it).
- State lives OUTSIDE the repo in /Users/namansoni/Infinite/perfhunt-state/: rotation.json, seen.txt, lessons.md, candidates.md, runs/<date>-<slot>.log.

## Machine safety (8 GB M2 Mac)
- Before any heavy step: `pgrep -fl "cmake --build|ninja|clang|ibuild|bench|Infinite"`. If anything shows, the heavy step is "not run: machine busy". Do NOT stop the slot: switch to static analysis (TECHNIQUES.md 2-3), and label every number from it `estimated`.
- Bench numbers from a busy or unfocused machine are worthless. A run tagged `unfocused=1` or `unpaced=1` is not a baseline (see internal/plans/perf/README.md trust rule). Swap above ~5 GB or another claude/ibuild process = not quiet.
- One build at a time, only via `python3 scripts/ibuild.py` (read `--help` and skill `build-discipline` first). Release build only for timing; dev build timings are not comparable. Pause the daemon for direct bench runs: `scripts/bench/run_all.sh --quiet`.
- Never run while the bughunt is building. No sub-agents, no other sessions, no broad pkill. Respect each tool's `--budget`. Time cap: 3 h per run (deep dive); stop earlier on the STOP rule in EXECUTE.md.
- Executing (build, A/B, tests) needs a quiet machine. Busy = investigation and issues only, no implementation that day.
- Keep disk low: build only in the perfhunt worktree; never create extra worktrees.

## Hard constraints from the owner (a proposal that breaks one is not filed)
| Rule | Meaning |
|---|---|
| Quality never degrades | Export and render output stays bit-identical or within a stated tolerance proven by `output_hash` / `tools/render-check.py` / `tools/determinism-check.py`. No "lower quality for speed" proposals. |
| Priority order | Audio > projector/output > canvas > previews. Rank by what the user feels in that order. |
| Video/3D stay on GPU | The render path is GPU by design; CPU belongs to audio. No "move render to CPU" pitches. |
| No new features | Speed-ups of what exists. No new nodes, modes or panels (those need the owner's approval first). |
| Scale to user base | Smallest change that moves the number. No new legacy or compat flags. |
| Three platforms | A macOS-only win says what Windows and Linux do (`windows-parity`, `linux-parity`). |
| Clean room | Read any open-source code (GPL included) to understand it; never copy, paste or transcribe it. Notes first in your own words, then implement fresh (TECHNIQUES.md 6, AGENTS.md rule 1). Books, papers, docs and discussions are fine. Read-only, generic queries only. |
| No debug tools in release | A profiler hook a proposal needs is `#ifndef NDEBUG` or env-gated. |

## Reading the repo (every run)
TECHNIQUES.md, AREAS.md, DAILY.md (this folder); /Users/namansoni/Infinite/perfhunt-state/lessons.md and candidates.md; internal/plans/perf/README.md (scoreboard, open items, Found while measuring, Top costs) and internal/plans/perf/benchmark-suite.md section 6 (targets); AGENTS.md; docs/DECISIONS.md; skills `run-infinite-hygiene`, `build-discipline`, `codebase-navigation`, `bug-blast-radius`, `invariant-interaction-audit`, `infinite-code-review` (real-time standard), `prior-art-scout`, `tool-first`, plus the ones AREAS.md names.

## Filing issues
- Dedup first: `gh issue list --state all --search "<keywords>" --limit 10`, grep seen.txt, and grep internal/plans/perf/README.md ("Found while measuring", Scoreboard). Already fixed or already closed as measurement artefact = do not file. Exists = comment with new numbers only.
- Labels (create if missing): `gh label create auto-perfhunt --color 0e8a16 2>/dev/null || true`; `perf`; `perf-candidate`; area `perf-audio`, `perf-latency`, `perf-canvas`, `perf-render`, `perf-projector`, `perf-media`, `perf-3d`, `perf-startup`, `perf-export`, `perf-field`, `perf-platform`, `perf-preview`; effort `effort-S`, `effort-M`, `effort-L`.
- `gh issue create --label perf --label perf-candidate --label auto-perfhunt --label perf-<area> --label effort-<S|M|L>`. Append "<issue url> | <title>" to seen.txt and one table row to candidates.md.
- Title: `[perf-<area>] what gets faster, by how much` e.g. `[perf-audio] Reverb: cache LFO bank per block, ~-0.1 ms/callback at 24 voices`.
- Body template: see section "Issue template" below.
- Limits per run: file up to 6 proposals, 2 `measure-gap` issues, 1 `perf-digest` (slot 15 only). EXECUTE at most 3 of them per run (best gain per risk first), all on the one run branch, one commit each. The rest stay open as `perf-candidate`. Group findings sharing a root cause.
- Evidence tiers: `measured` (bench/profile output, with command and numbers) > `traced` (arithmetic from code: loop counts x cost, allocation sizes, bytes copied per frame) > `estimated` (comparison to a similar proven win in the Scoreboard). Anything weaker is left out. The tier goes in the issue's `Evidence tier` line. After execution the issue gains a measured `Result` section (EXECUTE.md step 7).

## Issue template
```
**Metric**: <one: audio cb_load p99 % | frame p50/p99 ms | node body ms | latency ms | startup s | RSS MB | export fps>
**Priority tier**: audio | projector | canvas | previews
**Evidence tier**: measured | traced | estimated

## Today
<number now, command that produced it, bench id (B1..B10) or probe, build SHA, machine state (quiet? focused?)>

## Why it is slow
<file:line, the loop/alloc/copy/lock/redundant cook, with counts: calls per frame x cost>

## Proposed change
<smallest change, 3-8 lines, no code dump; what is vectorized, cached, skipped, batched, moved off the hot path>

## Expected gain
<number with the arithmetic; range low-high; which benchmark row moves; comparable past win from the Scoreboard>

## Research
<2-5 source URLs (paper, vendor doc, peer discussion) with one line each on the technique; or "none relevant" with the queries tried>

## Guard (quality + correctness)
<what must stay identical; the test that proves it: output_hash, DSPTEST SIMD-vs-scalar diff tolerance, rgbadiff.py, determinism-check; hot-path rules: no alloc/lock/IO on audio thread>

## Prove it
<A/B route: scripts/bench/ab.sh <bench>, keep-only-if-better gate, rounds, target row in bench/baselines/m2-8gb.jsonl>

## Blast radius
<other callers/siblings of the same pattern: grep command + file:line list; platform sides (mac/Win/Linux)>

## Cost and risk
<effort S (<1 h) / M (<1 day) / L (>1 day); risk low/med/high and why; files touched>

## Release fit
<standalone or bundle with <issue>; goes with perf Block <n>; safe for a patch release? y/n>

Found by: <slot, area, technique number, tool command>
```
Priority within a run: audio xruns/hot callback > latency > projector frame pacing > canvas frame > media > startup/RSS > previews.

## Executing and closing
See EXECUTE.md. Summary: implement smallest change, build Release via ibuild, `scripts/bench/ab.sh` before vs after (keep only if better past the tool's threshold), quality guard and area tests green, commit, fast-forward local `main`, comment the A/B table and SHA on the issue, close it. A change that does not beat noise is reverted, the issue gets the honest numbers, label `perf-rejected`, closed as not planned, and the class goes into lessons.md.

## Finish every run
Append to runs/<date>-<slot>.log and print: steps run, steps "not run" and why (never "pass"), issues opened (links), comments on existing, areas audited including clean ones, areas NOT covered, and the top 3 of candidates.md. Nothing worth proposing = say "no new candidates", open nothing.
