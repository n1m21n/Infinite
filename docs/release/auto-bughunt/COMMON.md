# Auto bug hunt: rules every run follows

Goal: FIND bugs and open GitHub issues. Never fix. Never edit source, commit, push, branch, merge, open PRs or tag.

## Where to work
- Never touch /Users/namansoni/Infinite/repo's working tree (the owner's live checkout, may hold unsaved work).
- Work in the dedicated worktree /Users/namansoni/Infinite/worktrees/bughunt:
  - First time: `git -C /Users/namansoni/Infinite/repo worktree add --detach /Users/namansoni/Infinite/worktrees/bughunt origin/main && git -C /Users/namansoni/Infinite/worktrees/bughunt submodule update --init external/vst3sdk`
  - Every run: `git -C /Users/namansoni/Infinite/repo fetch origin` then `git -C /Users/namansoni/Infinite/worktrees/bughunt checkout --detach origin/main`
- State and logs live OUTSIDE the repo: /Users/namansoni/Infinite/bughunt-state/ (rotation.json, runs/<date>-<task>.log, seen.txt). Create it if missing.

## Machine safety (8 GB M2 Mac)
- Before any heavy step: `pgrep -fl "cmake --build|ninja|clang|ibuild|bench|Infinite"`. If anything shows, skip the heavy step, say "skipped: machine busy", continue with read-only steps.
- One build at a time, only via `python3 scripts/ibuild.py` (read its --help first). One app-launching test at a time. No broad pkill. Start no sub-agents or other sessions.
- Respect each tool's own time cap (--budget). Stop a step early when it repeats a bug class already filed.
- Keep disk low: build only in the worktree; never create extra worktrees.

## Reading the repo
Read TECHNIQUES.md and DEPARTMENTS.md (same folder; they name the repo skills in .claude/skills/<name>/SKILL.md that you must open and follow) and /Users/namansoni/Infinite/bughunt-state/lessons.md (if it exists) every run. Read AGENTS.md, docs/release/PROTOCOL.md (categories A-L, severity S0-S4) and the skills named in each task. PROTOCOL.md wins over anything here.

## Filing issues
- Dedup first: `gh issue list --state all --search "<keywords>" --limit 10` and grep /Users/namansoni/Infinite/bughunt-state/seen.txt. If it exists, comment with the new evidence only.
- Only bugs with evidence: file:line, the wrong code, and a concrete trigger (input, param value, steps, or tool output). No speculation, no style, no refactors.
- Max 6 new issues per run. Group findings with the same root cause into one issue.
- Ensure labels: `gh label create auto-bughunt --color fbca04 2>/dev/null || true`; also add a category label `cat-A`..`cat-L` (create the same way) and `S0`..`S4`.
- `gh issue create --label bug --label auto-bughunt --label cat-<X> --label S<n>`. Append "<issue url> | <title>" to seen.txt.
- Title: `[area] short symptom`. Body: Severity + why | Where (file:line, node/feature) | What is wrong | How it triggers | Blast radius (grep for the same pattern elsewhere; see skill bug-blast-radius) | One-line suggested fix | Found by (task, step, tool command).
- Priority: crash > NaN/hot audio > data loss > wrong or degraded output > rest. S0/S1 get a `[S0]`/`[S1]` prefix in the title.

## Finish every run
Append to runs/<date>-<task>.log and print: steps run, steps skipped and why ("not run", never "pass"), issues opened (links), duplicates commented, areas covered, areas NOT covered. No bugs found = say "no new bugs", open nothing.
