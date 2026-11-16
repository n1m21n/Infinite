# Daily hunt: one task, every day 9 AM. Fixed checks + one rotating slot; every department and tool is hit every 22 days

Read COMMON.md, TECHNIQUES.md and DEPARTMENTS.md first.

## 1. Fixed checks (every day, ~10 min)
0. Run every command in the "Static grep patterns" list of lessons.md (skip if empty).
a. `tools/release/run_gate.sh --phase P1 --list`, then `tools/release/run_gate.sh --phase P1` (static checks only, no build). Every red row is a finding candidate; read its output.
b. `python3 tools/bughunt/code_lint.py`: only NEW hits vs code_lint_baseline.txt; confirm each in the code.
c. `git log --since="26 hours ago" origin/main --stat`: read every changed hunk. Does the change keep the department's contract (DEPARTMENTS.md)? Did the commit add or change a test with it? A behaviour change with no test change is worth a note.
d. `gh run list --branch main --limit 10`: for red runs, `gh run view <id> --log-failed`; file only real bugs, not flakes. Include Windows and Linux jobs.
e. `gh issue list --state open --limit 50`: for bug reports lacking a root cause, comment only if you found one.

## 2. Rotating slot (the day's slot, 15-60 min)
Read /Users/namansoni/Infinite/bughunt-state/rotation.json ({"next": N, "deferred": []}); default next=0. Do slot N, then write next=(N+1)%22.
- **Slots 0-13: one department** from DEPARTMENTS.md. Work its 5-step method: contract, run its harness, check the tests are real, probe behaviour, record. Read-only unless the harness needs a headless run (allowed when the machine is idle; no app build).
- **Slots 14-21: tool slots (heavy).** Run the playbook, cap 60 min. Before starting, check machine safety (COMMON.md). If the machine is busy, do NOT skip silently: add the slot number to "deferred", do the next department slot instead, and run deferred slots first on the next idle day. Say so in the summary if a tool slot stays deferred more than 7 days.

| N | Tool slot | Playbook |
|---|---|---|
| 14 | Audio nodes fuzz | WEEKLY_STABILITY.md steps 0-1 |
| 15 | Picture/3D nodes fuzz | WEEKLY_STABILITY.md steps 0 and 2 |
| 16 | Param fuzz + triage | WEEKLY_STABILITY.md steps 0 and 3 |
| 17 | Files and persistence | WEEKLY_STABILITY.md steps 4, 5, 9 |
| 18 | Selftests and sanitizers | WEEKLY_STABILITY.md steps 6, 7; step 8 (TSan) every second time this slot comes up |
| 19 | Perf, FPS, latency, leaks | WEEKLY_PERF_UI_PLATFORM.md steps 1, 2, 7 |
| 20 | UI, design, platform, sweeps | WEEKLY_PERF_UI_PLATFORM.md steps 3, 4, 5, 6 |
| 21 | Coverage, security, release, owner checklist | MONTHLY.md all steps |

The WEEKLY_* and MONTHLY.md files are playbooks for these slots, not separate schedules; ignore their "weekly/monthly" wording and time caps. This file's 60 min cap wins.

## 3. Findings
Two kinds, both filed per COMMON.md:
- **Bug:** code or harness result contradicts the contract. Label `bug`.
- **Test gap:** a contract line no test would catch. Label `test-gap` (create it), title `[tests] <department>: <contract line>`, with the proposed test. Max 3 test-gap issues per run; they do not count toward the 6 bug cap.
