#!/usr/bin/env python3
"""Mark parity-map.tsv rows out, with the reason.

    python3 tools/3dview_parity_out.py "reason" KEY...
"""
import os
import sys

MAP = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "plans", "3d-view", "parity-map.tsv")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    why, keys = sys.argv[1], set(sys.argv[2:])
    rows, hit = open(MAP).read().split("\n"), set()
    for i, r in enumerate(rows):
        f = r.split("\t")
        if f[0] in keys:
            rows[i] = f"{f[0]}\tout\t{why}"
            hit.add(f[0])
    if keys - hit:
        sys.exit(f"not in map: {sorted(keys - hit)}")
    open(MAP, "w").write("\n".join(rows))
    print(f"{len(hit)} rows -> out")


main()
