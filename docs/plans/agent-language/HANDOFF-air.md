# Handoff to the Air (2026-10-14 ~17:45): finish the language benchmark

The full run is a plain background job on the Air (it keeps going when the Pro is off). Worktree
`~/infinte-wt/agent-language-pilot`, results in `tools/agentlang/out/`. Last seen: held-out B0/B0p/B1 done
for Sonnet, Opus, Haiku; T7 running (Sonnet 10, Opus 8, Haiku 2 of 30). Sonnet's dev arms were skipped on
purpose (not in the gate). The job ends by writing `out/final.json` (the verdict). Do not restart it.

## Do, in order
1. Wait for `out/final.json` (`ls`; `pgrep -f harness.py` empty = finished). If a `claude -p` has run over
   30 minutes, kill only that process; the harness records it and moves on; re-run it afterwards.
2. `git merge --ff-only pro/agent-language-p2` (the Pro pushed it; adds docs and film tools only).
3. `python3 tools/agentlang/report_runs.py out/full.runs.jsonl` (per-arm table, paired ratios B1/B0p and B1/B0)
   and read `out/final.json` (T4 first-try validity, T7 decode-back, VERDICT per model).
4. Append a "Phase 2 results" section to `docs/plans/agent-language/README.md` under Results: the
   per-model table, T4, T7, VERDICT (KEEP-GOING / STOP / INCONCLUSIVE), spend, the skipped dev arms, the
   Opus stalls (one session 48 min, API side, no effect on tokens). Commit, no attribution lines.
5. Do not merge to main or push.
6. Tell the owner: verdict, the numbers behind it, and that the film dialect (`FILM.md`) goes ahead only if
   KEEP-GOING; `film_check` is already shipped either way.

## Rules
Only the Pro merges `main` and pushes `origin`. No Claude attribution anywhere. Show results as tables.
