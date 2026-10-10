#!/usr/bin/env python3
"""List AudioKnobRow blocks that hold no knob-like cell (only Dropdown/Checkbox/Button/Skip) but were built with the
full-knob default height, which leaves a knob-sized empty band under the row (the Looper's blank space at the bottom).
Usage: tools/audit_knobless_rows.py [--fix]   --fix rewrites the constructor to the compact form (n, 20.0f, 8.0f, false)."""
import re, sys, glob

FLAT = {"Dropdown", "Checkbox", "Button", "Skip", "End"}
fix = "--fix" in sys.argv
decl = re.compile(r'AudioKnobRow\s+(\w+)\(([^;]*)\);')
total = 0
for path in sorted(glob.glob("src/app/**/*.cpp", recursive=True) + glob.glob("src/app/**/*.h", recursive=True)):
    src = open(path).read()
    out, pos, changed = [], 0, False
    for m in decl.finditer(src):
        name, args = m.group(1), m.group(2)
        end = re.search(r'\b%s\.End\(\)' % name, src[m.end():])
        if not end:
            continue
        body = src[m.end(): m.end() + end.end()]
        calls = set(re.findall(r'\b%s\.(\w+)\(' % name, body))
        passed = re.search(r'[(,]\s*%s\s*[,)]' % name, body)   # handed to a helper that may add knobs
        if passed or not calls or not (calls - {"End"}) <= FLAT or (calls - {"End"}) == set():
            continue
        # constructor args beyond the cell count mean someone already chose a height
        parts = [a.strip() for a in args.split(',')]
        if len(parts) > 1:
            continue
        line = src[:m.start()].count("\n") + 1
        print(f"{path}:{line}: {name}({args}) only uses {sorted(calls - {'End'})}")
        total += 1
        if fix:
            out.append(src[pos:m.start()]); out.append(f"AudioKnobRow {name}({parts[0]}, 20.0f, 8.0f, false);"); pos = m.end(); changed = True
    if fix and changed:
        out.append(src[pos:]); open(path, "w").write("".join(out))
print(f"{total} knobless row(s)")
