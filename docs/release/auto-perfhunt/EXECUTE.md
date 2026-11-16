# Execute loop: from finding to closed issue (one run = one area, deep)

Order of the day: investigate the area deeply (AREAS.md 6 steps) -> file every finding as an issue -> rank by gain per risk -> execute up to 3 -> close each with measured numbers.

## 0. Preconditions (all must hold, else investigation only)
- Machine quiet (COMMON.md pgrep check, swap < ~5 GB, screen unlocked, no other claude/ibuild process). Run benches with `scripts/bench/run_all.sh --quiet` style daemon pause, `caffeinate -dimsu`, app focused (a run tagged `unfocused=1`/`unpaced=1` is not a result).
- `git -C /Users/namansoni/Infinite/repo status` of the live checkout is not needed; you never touch it. The perfhunt worktree is clean on `perf/<date>-<area>` off local `main`.
- The finding passes the hard constraints (COMMON.md). Needs a new feature, a quality trade or a behaviour change = not executed; label `needs-owner`, leave open.

## 1. Baseline (A)
1. `scripts/bench/base.sh main` builds/reuses the base app (A) at the exact commit you branched from. Record the SHA.
2. Pick the bench whose code path the change touches (not all of them). Run the baseline fixture 1x now to confirm it is trusted and to record the "Today" number in the issue (comment, not edit history).
3. For an area with no fixture: build the smallest `INFINITE_BENCH_*` fixture first (reuse `BenchReport.h`, `BenchFrameTail.h`; env-gated, not in the shipped path) and commit it as its own commit ("bench: fixture for X"). A change without a fixture is not executed.

## 2. Implement (B)
- Smallest change that does what the issue says. No refactors, renames, or drive-by edits. Follow the node/skill rules for the area (`new-*-node`, `field-*`, `windows-parity`, `linux-parity` for every Platform:: touch, `infinite-code-review` real-time standard: no alloc/lock/IO on the audio thread).
- If the hot loop changes numerically (SIMD, polynomial, table): keep a scalar/reference path and add or extend the equivalence check (DSPTEST pattern: max diff vs reference under a stated tolerance). Exact-preserving changes (caching, skipping, hoisting) must be bit-identical.
- Read `build-discipline`, then `python3 scripts/ibuild.py` (Release, one build at a time, adaptive -j). Never raw cmake.

## 3. Prove (A/B)
`scripts/bench/ab.sh <baseApp> <branchApp> auto -- INFINITE_BENCH_<id>=1 ...` (interleaved, up to 3 pairs; 30% move = 1 pair is enough; |change| < 5% is noise). Metric = the area's metric in AREAS.md (`AB_KEY` set accordingly).
| Result | Action |
|---|---|
| Better past the tool's rule, target row improved or unchanged elsewhere | Keep. Go to 4. |
| Noise (<5%) or within the tool's no-change rule | Not a win. Revert, `perf-rejected`, record in lessons.md. |
| Worse on a sibling bench the diff touches | Revert or narrow the change once; second failure = STOP. |
Never use your own threshold. Report median table with change %, builds' SHAs, variant, trusted/focused flags.

## 4. Guards (all must pass before commit)
- Quality: `output_hash` unchanged (anim=0) for render paths; `tools/render-check.py` / `tools/determinism-check.py`; `scripts/bench/rgbadiff.py` for pixel diffs; for audio the equivalence diff and `tools/render-check.py` peak/DC/silence.
- Tests: `driver.sh --group <area>` for the area touched (per skill run-infinite-hygiene), plus the area's `*-sweep` driver. Failures already in `known-test-failures.txt` are not yours (PLUGINDRAGTEST flake: report and move on). A new failure = fix once or revert.
- Cross-platform: Windows/Linux twins updated or the issue says explicitly "mac-only; twin X unchanged because Y" and a follow-up issue is filed.
- Bug-blast-radius: every sibling listed in the issue is either changed in the same commit or listed as a follow-up.

## 5. Commit
One commit per issue on the run branch. Message: `perf(<area>): <what> (<before> -> <after> <unit>, B<id>)` then `Closes #<n>`-style line as text only (no push, so no auto-close). No attribution trailers (project rule: no "Generated with Claude Code", no Co-Authored-By). Commit hooks run; never `--no-verify`.

## 6. Land on local main (no push)
- `git -C /Users/namansoni/Infinite/repo branch --show-current` must NOT be `main` (else main is checked out in the live checkout: leave the branch, label `perf-verified`, keep the issue open).
- Otherwise: fast-forward only: `git -C /Users/namansoni/Infinite/repo fetch . perf/<branch>:main`. If it refuses (main moved), rebase the run branch on main, rerun step 4 tests + one A/B pair, then retry once; still refused = leave open as `perf-verified`.
- Never push, tag, or edit origin. The owner pushes and releases ("closed" = merged to main; "shipped" = in a tagged release, tracked separately).

## 7. Close the issue
`gh issue comment <n>` with:
```
**Result (measured)**: <metric> <before> -> <after> (<change %>), bench B<id>, <rounds> interleaved pairs, trusted+focused
| variant | A (base <sha>) | B (<sha>) | change % |
|---|---|---|---|
**Guard**: output_hash unchanged | equivalence max diff <x> (tol <y>) | tests: <driver groups> pass
**Landed**: commit <sha> on local main (not pushed) | or: verified on branch perf/<name>, merge pending
**Siblings**: changed <list> | follow-ups #<n>
```
Then `gh issue close <n> --reason completed` and add label `perf-done` (landed) or `perf-verified` (branch only: leave OPEN). Update candidates.md (row marked done with real numbers) and add one row to a new table `Perf wins` in perfhunt-state/wins.md (date | commit | bench | before | after | %), formatted like the README Scoreboard so the owner can paste it there. Do not edit internal/plans/perf/README.md yourself.

## 8. STOP rules
- 2 failed attempts at the same step (untrusted run twice, A/B noise twice, tests red twice) = STOP that issue, record the evidence in the issue and the run log, move to the next candidate.
- Machine turns busy mid-run = finish the current step, stop executing, leave the branch as is, note "executed up to step N".
- Never leave a half-applied change on the run branch: each commit is complete and gated, or reverted.
- Never fabricate a gate: a number from a busy/unfocused/unpaced run is "untrusted", the issue stays open.
