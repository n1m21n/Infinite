# Release run sheet: <version>

Copy to `docs/release/checklists/<version>.md`. Fill one row per check as it runs; the run sheet is the record the exit decision reads. Check catalog and pass bars: `PROTOCOL.md` section 7; this release's numbers, waivers and decisions: `<version>.md`.

Release candidate: branch `<branch>` @ `<commit>`   Build: `<path, Release>`   Date started: `<date>`
Previous release (A/B and legacy base): `<tag>` built by `scripts/bench/base.sh`

## Entry criteria (PROTOCOL.md section 4)

| # | Criterion | Done | Evidence |
|---|---|---|---|
| 1 | Feature freeze | | |
| 2 | All blocks merged into the candidate branch | | |
| 3 | Clean Release build of this exact commit, hash visible | | |
| 4 | `driver.sh --fast` green | | |
| 5 | Baselines current | | |
| 6 | Tracker initialised, known issues loaded | | |

## Run table

`tools/release/run_gate.sh` fills P1 and P2 rows into `docs/release/runs/<timestamp>/results.tsv`; copy the pass/fail summary here. Hand and quiet-machine rows are filled by the person who ran them.

| Cat | Check | Phase | Owner | Result | Run folder or note | Findings |
|---|---|---|---|---|---|---|
| A | | | | | | |
| B | | | | | | |
| C | | | | | | |
| D | | | | | | |
| E | | | | | | |
| F | | | | | | |
| G | | | | | | |
| H | | | | | | |
| I | | | | | | |
| J | | | | | | |
| K | | | | | | |
| L | | | | | | |

Result values: `pass`, `fail`, `waived`, `skipped (why)`, `not run`.

## Per-node screenshot judging (category I)

One row per node per item of the checklist in PROTOCOL.md section 8; a failure becomes a finding.

| Node | 1 symmetry | 2 gaps | 3 dimming | 4 mod-dot/mix, contrast | 5 open state | 6 pins | 7 neighbours | Finding |
|---|---|---|---|---|---|---|---|---|

## Exit criteria (PROTOCOL.md section 4)

Run `python3 tools/release/findings.py exit --version <v>` and paste its output.

| # | Criterion | Met | Evidence |
|---|---|---|---|
| 1 | 0 open S0 and S1 (or signed waivers) | | |
| 2 | Open S2 at or below the cap, each with a release-notes line | | |
| 3 | Every category's pass bars met or waived | | |
| 4 | Final full run on the final commit | | |
| 5 | Windows pass on the final commit | | |
| 6 | `release-notes-audit` clean, version gate passes | | |
| 7 | The version's quality gate holds | | |
| 8 | Regression test for every S0/S1 fix | | |

Decision: `<accept | stop>`   By: `<owner>`   Date: `<date>`
