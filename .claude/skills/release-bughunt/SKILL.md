---
name: release-bughunt
description: The standing pre-release bug-hunt protocol for every Infinite release - categories A-L, severity S0-S4, entry/exit criteria, findings tracker and the serial gate runner. Use before tagging any release, when asked to "hunt bugs before release", "run the release gate", "is v0.X ready", "triage release findings", or when starting a new release's per-version file.
---

# Release bug hunt

## When to use (full scope)

Before any release tag, and whenever the owner asks whether a release candidate is ready. Also when a fix lands during a hunt (decide what re-runs), when a finding needs a severity or a waiver, or when starting the next release (create its `docs/release/<version>.md`).

## What owns what

This skill is a router. It does not copy the content it points to.

| Need | Owner |
|---|---|
| Categories A-L, severity, entry/exit, phases, roles, per-category checks and tools | `docs/release/PROTOCOL.md` |
| This release's pass bars, known issues, waivers, pending decisions | `docs/release/<version>.md` (v0.6: `docs/release/v0.6.md`) |
| Report format for one finding | `docs/release/BUG_TEMPLATE.md` |
| Run sheet for one release | `docs/release/CHECKLIST_TEMPLATE.md` copied to `docs/release/checklists/<version>.md` |
| Which tools exist / are not built | `docs/release/TOOLS.md` |
| The version's own gate numbers | that version's quality plan (v0.6: `docs/plans/v0.6-quality/README.md`, "Release gate") and `docs/quality/SCOREBOARD.md` |
| Running the sweeps and the driver | `run-infinite-hygiene`, the `*-sweep` skills |
| Impact analysis before each fix | `bug-blast-radius` |
| Release notes truth, version, publishing | `release-notes-audit`, `ship-infinite`, `scripts/release_version_gate.py` |
| Platform checks | `windows-parity`, `linux-parity`, `pillar-parity-audit`, `ci-failure-triage` |

If this skill and `PROTOCOL.md` disagree, `PROTOCOL.md` wins; if the protocol and a version's gate disagree on a number, the gate wins.

## Procedure

1. **Start a release.** Read `docs/NOW.md`. Copy the structure of `docs/release/v0.6.md` to `docs/release/<version>.md`; do step 0 (reconcile the gate with the protocol) and write the result there. Copy `CHECKLIST_TEMPLATE.md` to `docs/release/checklists/<version>.md`.
2. **Entry.** Check the six entry criteria in PROTOCOL.md section 4. Load known issues into the tracker: `python3 tools/release/findings.py new --version <v> ...`.
3. **P1.** `tools/release/run_gate.sh --phase P1` (static checks, no build, no app launch). `--list` first shows what would run.
4. **P2.** Confirm one build only: `pgrep -fl "cmake --build|make|clang"` shows nothing, the candidate binary is built (the runner never builds). Then `tools/release/run_gate.sh --phase P2`. It is serial and slow; run it in the background and wait for the notification (do not poll).
5. **P3 (quiet machine), P5 (owner), P4 (review).** Follow PROTOCOL.md section 6. Benchmarks and soak only with the owner's go and an untouched machine; never run them from this skill unasked. Do not start agent or session fan-out without the owner's explicit yes (usage rule in `AGENTS.md`).
6. **File findings.** One file per finding via the tracker; severity is the reporter's, S0/S1 and every waiver are the owner's call. A fix: `bug-blast-radius` first, then the fix commit with its regression test, then re-run the categories the blast radius names.
7. **Exit.** `python3 tools/release/findings.py exit --version <v>`, plus the run table and the version's gate. Report the decision to the owner; the owner accepts the candidate. Never tag, push or merge on your own.

## Rules that bite

- One build at a time; serial app-launching tests; no broad `pkill`.
- The runner wraps existing tools only. A check with no tool yet is `[new]` in PROTOCOL.md and `TOOLS.md`: say "not run" in the table, never "pass".
- Do not hard-code numbers into the per-release file when a baseline file owns them; link the file.
- A skipped or not-run check is reported as skipped. A red check is reported with its output, not softened.
- Keep the findings and runs under `docs/release/`; run logs in `docs/release/runs/` are local and not committed (ignored).
