# Brief: agent language, Phase 2 pilot (Air, runs `claude -p`, spends the Air account's quota)

Two-laptop rules: work only in this worktree, commit per step,
do not merge or push. No Co-Authored-By or "Generated with" lines anywhere.
Read `docs/plans/agent-language/README.md` (the design and the gate).

Step 0: `git merge --no-edit pro/agent-language-p2` (the Pro put this branch on this laptop; it carries your Phase 1 plus the harness).

## Do (one command each, results are JSON on the last stdout line)

1. Pick the binary: `export INFINITE_APP=<path>`; use `$HOME/infinte/build/Infinite.app/Contents/MacOS/Infinite`
   (else `/Applications/Infinite.app/Contents/MacOS/Infinite`). Never run it with `--help` or without a
   mode flag, and never `pkill` Infinite. Check: `python3 tools/agentlang/validate_all.py` must print `"failed": 0`.
   If it does not, STOP and write the output to `docs/plans/agent-language/PILOT.md`.
2. Pilot = 5 dev tasks `b01,b04,t01,t04,f01`, all three arms, one model `claude-sonnet-5-5`, cap $0.6 per session
   (15 sessions, at most $9):
   `for A in B0 B0p B1; do python3 tools/agentlang/harness.py --arm $A --tasks b01,b04,t01,t04,f01 --model claude-sonnet-5-5 --cap-usd 0.6; done`
   If the first session returns "Not logged in" or any auth error, STOP and say so; do not work around it.
3. `python3 tools/agentlang/report_runs.py`. Write the table and the ratios to `docs/plans/agent-language/PILOT.md`
   with these lines: the verdict rule (stop if B1 is not at least 20 percent cheaper than B0p on `fresh` tokens,
   paired on tasks both passed), what each arm failed on (read the failed `oracle.results` in `out/runs.jsonl`), and
   whether any failure is a harness fault (wrapper bug, wrong stat) rather than a model miss.
4. Fix harness faults only (never `tasks/`, never `FROZEN.sha256`), re-run only the affected tasks, and say so in PILOT.md.

## Do not
Run more than the 15 sessions plus re-runs of harness faults; touch `src/`; edit frozen files; start the full run. Stop after PILOT.md.
