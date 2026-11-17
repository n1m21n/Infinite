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
PLAN = os.path.join(ROOT, "docs", "release", "shot-plan-2.0.json")


def plan_ids(plan):
    shots = plan["shots"]
    vals = shots.values() if isinstance(shots, dict) else shots
    return [v.get("id", "") for v in vals]


def parse_id(sid):
    """'node/<cat>/<name>/<state>' -> (cat, name)."""
    _, cat, rest = sid.split("/", 2)
    return cat, rest.rsplit("/", 1)[0]


def file_name(cat, name, collide):
    """Picture filename (without .png). Names that differ only by case (Invert in Modulators vs invert in
    Compositing) would share one file on a case-insensitive disk, so those get the category appended."""
    return f"{name} ({cat})" if name.lower() in collide else name


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--swap", action="store_true")
    ap.add_argument("--expect", type=int, default=372)
    ap.add_argument("--manifest", default=os.path.join(ROOT, "docs", "shots", "manifest.json"),
                    help="the manifest the shots were written to (its raw paths are relative to its own worktree)")
    a = ap.parse_args()
    mroot = os.path.abspath(os.path.join(os.path.dirname(a.manifest), "..", ".."))
    man = json.load(open(a.manifest))
    # The plan's spawnable nodes are the authority; manifest extras (selection-only types) are ignored.
    plan = json.load(open(PLAN))
    want = {parse_id(sid) for sid in plan_ids(plan) if sid.startswith("node/") and sid.endswith("/c")}
    cats_of = {}
    for cat, name in want:
        cats_of.setdefault(name.lower(), set()).add(cat)
    collide = {low for low, cs in cats_of.items() if len(cs) > 1}
    print("case collisions (same name, different case, one disk file without a category):")
    for cat, name in sorted(want, key=lambda p: (p[1].lower(), p[0])):
        if name.lower() in collide:
            print(f"  {cat}/{name} -> {file_name(cat, name, collide)}.png")
    rows = {}
    for sid, e in man["shots"].items():
        if sid.startswith("node/") and sid.endswith("/c") and e.get("kind") == "node" and e.get("state") == "c":
            key = parse_id(sid)
            if key in want and e.get("name") == key[1]:
                rows[key] = e
    missing = sorted(want - set(rows))
    if missing:
        print("missing closed shots for plan nodes:", missing)
    if os.path.isdir(a.scratch) and os.listdir(a.scratch):
        sys.exit("scratch folder must be empty: " + a.scratch)
    os.makedirs(a.scratch, exist_ok=True)
    widths, bad = [], []
    names = set()
    for (cat, name), e in sorted(rows.items()):
        label = f"{cat}/{name}"
        src = os.path.join(mroot, e["raw"])
        if not os.path.exists(src):
            bad.append((label, "missing raw")); continue
        w, h = Image.open(src).size
        if w < MIN_W:
            bad.append((label, f"{w}px wide")); continue
        out = file_name(cat, name, collide)
        if out.lower() in names:
            bad.append((label, "filename collides with another node")); continue
        names.add(out.lower())
        shutil.copyfile(src, os.path.join(a.scratch, out + ".png"))
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
