#!/usr/bin/env python3
"""README gate: every relative image and link must exist, and the README stays under 330 lines."""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
text = open(os.path.join(ROOT, "README.md"), encoding="utf-8").read()
refs = re.findall(r'src="([^"]+)"', text) + re.findall(r'\]\(([^)\s]+)\)', text) + re.findall(r'srcset="([^"]+)"', text)
bad = []
for r in refs:
    if r.startswith(("http://", "https://", "#", "mailto:")):
        continue
    if not os.path.exists(os.path.join(ROOT, r.split("#")[0])):
        bad.append(r)
lines = text.count("\n")
print(f"README: {lines} lines, {len(refs)} references")
for b in bad:
    print("missing:", b)
if lines >= 330:
    print("too long: must be under 330 lines")
sys.exit(1 if bad or lines >= 330 else 0)
