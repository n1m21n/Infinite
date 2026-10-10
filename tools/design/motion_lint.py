#!/usr/bin/env python3
"""Motion lint (plan 4b): nothing over 200 ms, one source of timing, every duration on the tempo grid.

  - every tokens.json motion_ms entry is <= 200 (tooltip_delay is a wait, not a motion);
  - every non-zero motion_ms entry is a note value at 120 BPM within 5% (L1 Clock, tools/brand/motion.py);
  - every UiAnim::Value/Hover call passes tok::motion_* or a literal <= 200;
Exit 1 on any hit.
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/brand"))
from motion import snap  # noqa: E402

bad = []
for k, v in json.loads((ROOT / "src/app/ui/design/tokens.json").read_text())["base"]["motion_ms"].items():
    if v > 200 and k != "tooltip_delay":
        bad.append(f"tokens.json motion_ms.{k} = {v} > 200")
    if v and abs(v - snap(v)[1]) / snap(v)[1] > 0.05:
        bad.append(f"tokens.json motion_ms.{k} = {v} is off the 120 BPM grid (nearest {snap(v)[0]} = {snap(v)[1]:.0f} ms)")
CALL = re.compile(r"UiAnim::(Value|Hover)\(([^;]*)\);")
for f in sorted((ROOT / "src").rglob("*")):
    if f.suffix not in (".cpp", ".h", ".mm") or "design/UiAnim." in str(f): continue
    for n, line in enumerate(f.read_text(errors="ignore").splitlines(), 1):
        for m in CALL.finditer(line):
            for lit in re.findall(r"(?<![\w.])(\d+(?:\.\d+)?)f?(?![\w.])", m.group(2).split(",", 1)[-1]):
                if float(lit) > 200: bad.append(f"{f.relative_to(ROOT)}:{n} UiAnim duration {lit} ms > 200")
print(f"motion lint: {len(bad)} problem(s)")
for b in bad: print(" ", b)
sys.exit(1 if bad else 0)
