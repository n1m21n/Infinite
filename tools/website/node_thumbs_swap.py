#!/usr/bin/env python3
"""Build docs/node_screenshots/<Node Name>.png from the zoomed closed shots in docs/shots/manifest.json, in a scratch
folder first, then swap it in only if every picture passes the rule.

Rule (docs/release, shot-plan): zoomed node shots are at least 250 px wide in the source image. Any node below that
fails the whole run; nothing is swapped. The old folder is kept as docs/node_screenshots.bak-<stamp> until you delete it.

Usage:
  python3 tools/website/node_thumbs_swap.py --scratch /path/to/empty/dir [--swap] [--expect 372]
Without --swap it only builds the scratch folder and prints the sizes.
"""
import argparse, json, os, shutil, statistics, sys, time
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MIN_W = 250


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--swap", action="store_true")
    ap.add_argument("--expect", type=int, default=372)
    a = ap.parse_args()
    man = json.load(open(os.path.join(ROOT, "docs", "shots", "manifest.json")))
    rows = {}
    for sid, e in man["shots"].items():
        if e.get("kind") == "node" and e.get("state") == "c":
            rows[e["name"]] = e
    if os.path.isdir(a.scratch) and os.listdir(a.scratch):
        sys.exit("scratch folder must be empty: " + a.scratch)
    os.makedirs(a.scratch, exist_ok=True)
    widths, bad = [], []
    for name, e in sorted(rows.items()):
        src = os.path.join(ROOT, e["raw"])
        if not os.path.exists(src):
            bad.append((name, "missing raw")); continue
        w, h = Image.open(src).size
        if w < MIN_W:
            bad.append((name, f"{w}px wide")); continue
        shutil.copyfile(src, os.path.join(a.scratch, name + ".png"))
        widths.append(w)
    print(f"nodes {len(rows)}; built {len(widths)}; min width {min(widths) if widths else 0}px; "
          f"median {statistics.median(widths) if widths else 0}px")
    for n, why in bad:
        print("  REJECT", n, why)
    if len(widths) != a.expect or bad:
        print(f"not swapping: expected {a.expect} good pictures, have {len(widths)}, rejects {len(bad)}")
        sys.exit(1)
    if not a.swap:
        print("scratch ready; rerun with --swap to swap in")
        return
    dst = os.path.join(ROOT, "docs", "node_screenshots")
    if os.path.isdir(dst):
        os.rename(dst, dst + ".bak-" + time.strftime("%Y%m%d-%H%M%S"))
    shutil.move(a.scratch, dst)
    print("swapped into", dst)


if __name__ == "__main__":
    main()
