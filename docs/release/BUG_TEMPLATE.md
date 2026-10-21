# Bug report template

One finding = one file: `docs/release/findings/<version>/<ID>.md`, created with `python3 tools/release/findings.py new --version <v> --category <A-L> --title "..." --severity S2`. The tracker reads the header block (the lines `Key: value` before the first blank line), so keep those keys and values exactly as listed.

```
ID:            B-<category letter>-<nnn>         e.g. B-D-004
Title:         <what is wrong, one line>
Severity:      S0 | S1 | S2 | S3 | S4            (reporter sets it; see PROTOCOL.md section 2)
Priority:      P1 | P2 | P3                      (triage sets it)
Category:      A..L
Status:        new | triaged | fixing | fixed | verified | waived | wontfix
Found by:      <tool name | Claude | owner name>
Date:          YYYY-MM-DD
Build:         <branch> @ <commit>, <OS>, <Release|Debug>
Regression:    new | present-in-<previous version> | unknown     (bisect if cheap)
Fix:           <commit hash, or ->
Verified by:   <who re-ran the repro on which build, or ->
Waiver:        <owner-signed reason and date, or ->
Test:          <the INFINITE_*TEST fixture or corpus entry that fails before and passes after, or ->
```

## Repro

1. Exact steps, or the `INFINITE_<NAME>TEST` command and environment.
2. Minimal patch attached as `.inf` (path).
3. Frequency: always | n of m runs | once.

## Expected

## Actual

(screenshot, log, crash report path)

## Blast radius

The 9 questions from the `bug-blast-radius` skill, answered. The answers decide which categories re-run after the fix.

## Notes

Triage reasoning, links to related findings, what a verifier must re-run.

---

Rules:
- No re-runnable repro, not fixed until a verifier has re-run it. A flaky bug is fixed only after N consecutive passes (A: 20, others: 5 unless the release file says otherwise).
- S0/S1 need a `Test:` before `Status: fixed`.
- A waiver needs the owner's name and date; Claude never fills `Waiver:` on its own.
- `Fix:` is a commit on a `bugfix/` branch; one commit per bug, regression test in the same commit.
