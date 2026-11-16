# Daily perf hunt: one department per day, deep, then execute. Every area is hit every 16 days

Read COMMON.md, TECHNIQUES.md, AREAS.md, EXECUTE.md first. Schedule it at a different hour from the bughunt (needs a quiet Mac).

## 1. Fixed checks (every day, ~10 min, read-only)
0. Run every command in the "Static grep patterns" list of perfhunt-state/lessons.md (skip if empty).
a. Regression watch: `git log --since="26 hours ago" origin/main --stat`. For each change touching src/audio, src/app/frame, src/engine, src/platform or a node body: did it add per-frame/per-callback work (alloc, copy, loop, redundant cook, extra pass)? Read the hunk and count. A perf regression is a `bug`-style finding: file it as `[perf-<area>] regression:` with the commit SHA, labels `perf`, `auto-perfhunt`, no `perf-candidate`.
b. `gh run list --branch main --limit 10`: note CI wall time trend per OS job; a job whose time rose >25% vs the previous 5 runs gets a note (build time is not product perf, so only comment on an existing issue or put it in the log).
c. `python3 scripts/bench/compare.py` is run only inside a slot with a quiet machine; here just check `bench/results/*/` for runs newer than the baseline and read any with a flagged regression (tool's own rule).
d. `gh issue list --label perf-candidate --state open`: if the owner closed or commented on one, read why and add it to lessons.md (rejected classes are not re-proposed).

## 2. Rotating slot (deep dive, up to 3 h)
Read perfhunt-state/rotation.json ({"next": N, "deferred": []}); default next=0. Do slot N from AREAS.md, then write next=(N+1)%16.
- If the slot needs a bench and the machine is busy: do the static trace instead (TECHNIQUES.md 2-4), mark every number `traced`/`estimated`, put the slot number in "deferred" so the next idle day re-runs it with a measurement to upgrade the evidence tier of the open issues from that slot. Say so in the summary if a slot stays deferred more than 7 days.
- Depth rule: stay in this one department. Do NOT stop after 1-2 findings. Continue until (a) 6 proposals + 2 measure-gap issues filed, or (b) at least 8-12 distinct source files of the area are audited and every audited-clean file is in the run log. Follow each cost down to its root (profile, count, read the callers) instead of listing surface symptoms.
- Then EXECUTE (section 2b).
- Every candidate gets the template from COMMON.md in full: today number, why, change, gain with arithmetic, guard, prove-it route, blast radius, cost/risk, release fit.

## 2b. Execute (EXECUTE.md)
Rank the filed proposals by gain / risk. Execute the top 3 at most, in order: baseline (A) -> implement -> `ab.sh` A/B -> guards -> commit -> fast-forward local `main` -> comment result -> close issue. A proposal that fails the A/B is reverted and closed `perf-rejected` with the honest numbers; that is a valid outcome, not a failure. Machine busy or no fixture: no execution that day; issues stay open for the next idle day, and `deferred` records it.

## 3. Finding types
| Type | Label | When |
|---|---|---|
| Proposal | `perf-candidate` | A change that reduces ms, raises FPS or lowers latency, with numbers and a guard |
| Regression | `perf` only | A recent commit made a measured/traced path slower |
| Measure gap | `perf-gap` (create) | A hot path with no benchmark or counter; title `[perf-gap] <area>: no way to prove <metric>`; body names the fixture to add (reuse BenchReport.h, `INFINITE_BENCH_*` pattern) |
| Digest | `perf-digest` | Slot 15 only |
| Outcome labels | `perf-done`, `perf-verified`, `perf-rejected`, `needs-owner` | Set by EXECUTE.md step 7 |

## 4. Close the run
Update rotation.json, seen.txt, wins.md, candidates.md (one row per new issue, marked done with real numbers once executed), lessons.md (one line per class + grep), and runs/<date>-slot<N>.log (files audited incl. clean, commands, raw numbers). Raw bench output stays in the log file, never in an issue with user patch data.
