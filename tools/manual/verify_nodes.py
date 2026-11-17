#!/usr/bin/env python3
"""Verify the node shots against disk and the spawnable list, then write the website-lane manifest.

  python3 tools/manual/verify_nodes.py            # prunes the manifest, stats every file, writes the release manifest
  python3 tools/manual/verify_nodes.py --check    # read-only: exits 1 unless all spawnable nodes have c and o on disk

Rules: only entries whose raw AND framed files exist survive; non-spawnable names are dropped; d (dropdown) entries
survive only when their file exists. Every spawnable node needs a closed and an open raw PNG at least 250 px wide.
"""
import json
import os
import sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import shot_plan  # noqa: E402

MANIFEST = os.path.join(ROOT, "docs", "shots", "manifest.json")
RELEASE = os.path.join(ROOT, "docs", "release", "manual-shots-2.0.json")
MIN_W = 250


def main():
    check = "--check" in sys.argv
    spawn = [f"node/{c}/{n}" for c, n in shot_plan.nodes()]
    spawn_set = set(spawn)
    m = json.load(open(MANIFEST))
    shots = m["shots"]
    dropped = []
    for k in list(shots):
        if not k.startswith("node/"):
            continue
        e = shots[k]
        base = k.rsplit("/", 1)[0]
        ok = base in spawn_set and all(os.path.exists(os.path.join(ROOT, e.get(f, "") or "x")) for f in ("raw", "framed"))
        if not ok:
            dropped.append(k)
            if not check:
                del shots[k]
    if not check:
        tmp = MANIFEST + ".tmp"
        json.dump(m, open(tmp, "w"), indent=1, sort_keys=True)
        os.replace(tmp, MANIFEST)

    rows, missing, minw = [], [], None
    for base in spawn:
        for st in ("c", "o"):
            k = f"{base}/{st}"
            e = shots.get(k)
            if not e or not os.path.exists(os.path.join(ROOT, e["raw"])):
                missing.append(k)
                continue
            p = os.path.join(ROOT, e["raw"])
            w, h = Image.open(p).size
            minw = w if minw is None else min(minw, w)
            if w < MIN_W:
                missing.append(f"{k} (width {w} < {MIN_W})")
            rows.append(dict(id=k, state=st, file=e["raw"], framed=e["framed"], width=w, height=h, source_px=[w, h]))
    print(f"spawnable nodes: {len(spawn)}  non-spawnable entries dropped: {len([d for d in dropped if d.rsplit('/',1)[0] not in spawn_set])}  "
          f"stale entries dropped: {len(dropped)}")
    print(f"raw PNGs verified: {len(rows)} of {2 * len(spawn)}; minimum width {minw} px")
    if missing:
        print(f"missing or undersized: {len(missing)}")
        for x in missing[:40]:
            print("  ", x)
    if not check:
        os.makedirs(os.path.dirname(RELEASE), exist_ok=True)
        json.dump({"app": m.get("app", ""), "version": m.get("version", ""), "min_width_px": MIN_W,
                   "spawnable_nodes": len(spawn), "complete": not missing, "missing": missing, "shots": rows},
                  open(RELEASE, "w"), indent=1)
        print("wrote", os.path.relpath(RELEASE, ROOT))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
