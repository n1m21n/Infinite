#!/usr/bin/env python3
"""Mark parity-map.tsv rows done, citing a symbol's current line.

    python3 tools/3dview_parity_mark.py src/app/panels/View3DPanel.cpp "void DrawViewTab" "Snap card: use for move" ToolSettings.use_snap_translate ...

The line number is looked up with the first regex match of SYMBOL in FILE, so the citation tracks the code.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP = os.path.join(ROOT, "docs", "plans", "3d-view", "parity-map.tsv")


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    file, symbol, note, keys = sys.argv[1], sys.argv[2], sys.argv[3], set(sys.argv[4:])
    line = next((n for n, l in enumerate(open(os.path.join(ROOT, file)), 1) if re.search(symbol, l)), None)
    if line is None:
        sys.exit(f"{symbol!r} not found in {file}")
    where = f"{os.path.basename(file)}:{line} {note}"
    rows, hit = open(MAP).read().split("\n"), set()
    for i, r in enumerate(rows):
        f = r.split("\t")
        if f[0] in keys:
            rows[i] = f"{f[0]}\tdone\t{where}"
            hit.add(f[0])
    if keys - hit:
        sys.exit(f"not in map: {sorted(keys - hit)}")
    open(MAP, "w").write("\n".join(rows))
    print(f"{len(hit)} rows -> {where}")


main()
