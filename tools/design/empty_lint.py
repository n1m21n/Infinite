#!/usr/bin/env python3
"""Empty lint: every empty panel, lane or wave says it the one way (Curve Editor's "No curve yet").

Scans src/app for EmptyState::Draw / DrawTo / DrawCaption / DrawInWindow and DrawTrimWave's empty caption. Each
message starts with a capital, is wrapped in T() so it translates, and never uses slash lists or dash fillers
("drop sample / click", "--"). The action goes in the hint or after a middle dot: "No sample · drop or click".
Exit 1 on any hit.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CALL = re.compile(r"(EmptyState::(?:Draw|DrawTo|DrawCaption|DrawInWindow)|DrawTrimWave)\s*\(")


def args_of(text, i):
    """The argument text of the call whose '(' is at i."""
    depth, j = 0, i
    while j < len(text):
        c = text[j]
        depth += c == "("
        depth -= c == ")"
        if depth == 0:
            return text[i + 1:j]
        j += 1
    return ""


bad, n = [], 0
for f in sorted((ROOT / "src/app").rglob("*.cpp")) + sorted((ROOT / "src/app").rglob("*.h")):
    text = f.read_text(errors="ignore")
    for m in CALL.finditer(text):
        if "inline" in text[max(0, m.start() - 40):m.start()] or "TrimWaveInput DrawTrimWave" in text[max(0, m.start() - 20):m.end()]:
            continue   # the definitions themselves
        a = args_of(text, m.end() - 1)
        line = text.count("\n", 0, m.start()) + 1
        lits = re.findall(r'(T\()?"((?:[^"\\]|\\.)*)"', a)
        for wrapped, s in lits:
            if s.startswith("##"):
                continue
            n += 1
            where = f"{f.relative_to(ROOT)}:{line}"
            if not wrapped:
                bad.append(f"{where}: '{s}' is not wrapped in T()")
            if s[:1].islower():
                bad.append(f"{where}: '{s}' starts lower-case; write it like 'No curve yet'")
            if " / " in s or s.strip() in ("--", "-", "—"):
                bad.append(f"{where}: '{s}' - one phrase, the action after ' · ' or in the hint")
print(f"empty lint: {n} empty-state messages, {len(bad)} problem(s)")
for b in bad:
    print(" ", b)
sys.exit(1 if bad else 0)
