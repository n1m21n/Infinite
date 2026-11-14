#!/usr/bin/env python3
"""Golden shots of the 2D View: one picture of the panel per audit state (tab x layer kind, every tool, both docks,
three picked), both themes, held against approved pictures.

  python3 tools/ui/golden_view2d.py check            run the audit with shots, diff each against its golden
  python3 tools/ui/golden_view2d.py check --reuse    diff the last capture again without a new run
  python3 tools/ui/golden_view2d.py approve NAME...  take these captures as the new goldens (NAME = file stem)
  python3 tools/ui/golden_view2d.py approve --all    take every capture (after reading the diff report)

The shots come from the same off-screen debug run as tools/ui-audit/run.py --view2d (INFINITE_UIAUDIT_V2D_SHOTS), so
the panel is in exactly the state the audit checks. A pixel counts as changed when any channel moves by more than 8
(anti-alias wobble stays under that); a shot fails when more than 0.05% of its pixels change or its size changes.
Every failure writes NAME_diff.png (changed pixels in red over a dimmed capture) to the report folder. Approving is
the only way a changed picture becomes right: nothing is approved by a run.

Goldens live in tools/ui/golden-view2d/, untracked (192 shots at 2x are ~20 MB; repo churn on every UI change is not
worth it). A machine with no golden folder runs the audit alone; the first `approve --all` there seeds it.

Shots are 2x (the off-screen window's backing scale). 1x is not captured: the off-screen window takes the main
display's scale and there is no per-run override, so a 1x golden set would need a 1x display.
"""
import argparse, os, shutil, subprocess, sys

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GOLD = os.path.join(ROOT, "tools", "ui", "golden-view2d")
OUT = os.path.join(ROOT, "ui-audit-view2d")
SHOTS = os.path.join(OUT, "shots")
CH_TOL = 8
PX_FRAC = 0.0005


def capture(dark_only):
    if os.path.isdir(SHOTS):
        shutil.rmtree(SHOTS)
    cmd = [sys.executable, os.path.join(ROOT, "tools", "ui-audit", "run.py"), "--view2d", "--budget", "1500", "--shots", SHOTS]
    if dark_only:
        cmd.append("--dark-only")
    r = subprocess.run(cmd)
    return r.returncode


def run_audit_only(dark_only):
    cmd = [sys.executable, os.path.join(ROOT, "tools", "ui-audit", "run.py"), "--view2d", "--budget", "1500"]
    if dark_only:
        cmd.append("--dark-only")
    return subprocess.run(cmd).returncode


def diff_one(name):
    cur = os.path.join(SHOTS, name + ".png")
    gold = os.path.join(GOLD, name + ".png")
    if not os.path.exists(gold):
        return "unapproved", "no golden yet"
    a = np.asarray(Image.open(cur).convert("RGB")).astype(int)
    b = np.asarray(Image.open(gold).convert("RGB")).astype(int)
    if a.shape != b.shape:
        return "fail", "size %dx%d, golden %dx%d" % (a.shape[1], a.shape[0], b.shape[1], b.shape[0])
    changed = (np.abs(a - b) > CH_TOL).any(axis=2)
    n = int(changed.sum())
    if n <= PX_FRAC * changed.size:
        return "pass", "%d px moved" % n
    ys, xs = np.nonzero(changed)
    vis = (a * 0.35).astype(np.uint8)
    vis[changed] = (255, 40, 40)
    Image.fromarray(vis).save(os.path.join(OUT, name + "_diff.png"))
    return "fail", "%d px changed (%.2f%%) in x %d..%d, y %d..%d" % (n, 100.0 * n / changed.size, xs.min(), xs.max(), ys.min(), ys.max())


def check():
    names = sorted(f[:-4] for f in os.listdir(SHOTS) if f.endswith(".png")) if os.path.isdir(SHOTS) else []
    gold = sorted(f[:-4] for f in os.listdir(GOLD) if f.endswith(".png")) if os.path.isdir(GOLD) else []
    rows = [(n,) + diff_one(n) for n in names]
    rows += [(n, "fail", "golden has no capture (state gone?)") for n in gold if n not in names]
    counts = {}
    for _, st, _ in rows:
        counts[st] = counts.get(st, 0) + 1
    lines = ["# 2D View golden shots", "", " ".join("%s %d" % kv for kv in sorted(counts.items())), "",
             "| Shot | Status | Detail |", "|---|---|---|"]
    lines += ["| %s | %s | %s |" % r for r in rows if r[1] != "pass"]
    open(os.path.join(OUT, "golden.md"), "w").write("\n".join(lines) + "\n")
    print("golden: " + " ".join("%s %d" % kv for kv in sorted(counts.items())) + " -> " + os.path.join(OUT, "golden.md"))
    if not names:
        print("golden: no captures in %s" % SHOTS)
        return 2
    return 1 if counts.get("fail", 0) or counts.get("unapproved", 0) else 0


def approve(names, every):
    os.makedirs(GOLD, exist_ok=True)
    pick = sorted(f[:-4] for f in os.listdir(SHOTS) if f.endswith(".png")) if every else names
    for n in pick:
        src = os.path.join(SHOTS, n + ".png")
        if not os.path.exists(src):
            print("no capture", n)
            continue
        # Goldens are kept small: palette PNG, lossless for the panel's flat UI colours.
        Image.open(src).convert("RGB").save(os.path.join(GOLD, n + ".png"), optimize=True)
        print("approved", n)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["check", "approve"])
    ap.add_argument("names", nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--reuse", action="store_true", help="check: diff the last capture, no new run")
    ap.add_argument("--dark-only", action="store_true")
    a = ap.parse_args()
    if a.cmd == "approve":
        return approve(a.names, a.all)
    rc = 0
    if not a.reuse and not os.path.isdir(GOLD):
        # Goldens are per machine (untracked, ~20 MB): without them this is the audit alone.
        print("golden: no approved set on this machine (%s); audit only. Seed it: check, read the shots, approve --all" % GOLD)
        return run_audit_only(a.dark_only)
    if not a.reuse:
        rc = capture(a.dark_only)   # 1: the audit has findings or untested controls; 2: no report
        if rc == 2:
            return 2
    return max(rc, check())


if __name__ == "__main__":
    sys.exit(main())
