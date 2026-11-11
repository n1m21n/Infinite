#!/usr/bin/env python3
"""Before/after sweep for render changes (docs/plans/realtime-engine/PLAN.md section 5).

  python3 tools/blender-ab/sweep.py run  --engine live --tag pre  [--out DIR] [cases...]
  python3 tools/blender-ab/sweep.py run  --engine live --tag post [--out DIR] [cases...]
  python3 tools/blender-ab/sweep.py diff --engine live [--out DIR]
  python3 tools/blender-ab/sweep.py same --engine settle --spp 16   (tier 0: pixel identity, pre vs post images)

Testing tiers (PLAN section 5), to keep load on the 8 GB Mac low:
  0  refactors meant to move no pixel: `run --spp 16` pre and post, then `same` (any pixel change fails)
  1  real changes: `run --spp 64 <the cases the change can touch>` pre and post, then `diff`
  2  block close: full `run` (default samples) on every case, then `diff` against baseline-derived pre
Every run is started at background priority (taskpolicy -b) so the machine stays responsive.

`run` renders every case (or the named ones) through harness.py and saves case -> FLIP, brightness as
<out>/<tag>_<engine>.json. `diff` compares pre with post: a case that rose by more than SLACK FLIP is WORSE
and the exit code is 1. Truth renders are reused by the harness, so only Infinite renders again.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SLACK = 0.005


def run(a):
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ)
    env.setdefault("INFINITE_BIN", os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite"))
    cmd = ["taskpolicy", "-b", sys.executable, os.path.join(HERE, "harness.py"), *a.cases, "--engine", a.engine,
           "--out", os.path.join(out, "ab")]
    if a.spp:
        cmd += ["--pt-samples", str(a.spp)]
    p = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True)
    rows = {}
    for line in (p.stdout + p.stderr).splitlines():
        if "ABRESULT" in line:
            d = json.loads(line.split("ABRESULT", 1)[1])
            rows[d["case"]] = {"flip": round(d.get("flip", 0.0), 4), "brightness": round(d.get("brightness_ratio", 0.0), 3)}
        elif "rror" in line:
            print(line)
    # Keep this run's Infinite images for `same` (tier 0).
    img = os.path.join(out, "img", Key(a, a.tag))
    shutil.rmtree(img, ignore_errors=True)
    os.makedirs(img)
    suffix = {"raster": "", "pt": "_pt", "live": "_live", "settle": "_settle"}[a.engine]
    for case in rows:
        src = os.path.join(out, "ab", case, "inf%s.png" % suffix)
        if os.path.exists(src):
            shutil.copy(src, os.path.join(img, case + ".png"))
    path = os.path.join(out, Key(a, a.tag) + ".json")
    json.dump(rows, open(path, "w"), indent=1, sort_keys=True)
    print("%d cases -> %s" % (len(rows), path))
    return 0


def Key(a, tag):
    return "%s_%s%s" % (tag, a.engine, "_s%d" % a.spp if a.spp else "")


def same(a):
    import numpy as np
    from PIL import Image
    out = os.path.abspath(a.out)
    pre, post = os.path.join(out, "img", Key(a, "pre")), os.path.join(out, "img", Key(a, "post"))
    changed = 0
    names = sorted(set(os.listdir(pre)) | set(os.listdir(post)))
    for n in names:
        if not (os.path.exists(os.path.join(pre, n)) and os.path.exists(os.path.join(post, n))):
            print("%-28s MISSING" % n[:-4]); changed += 1; continue
        x = np.asarray(Image.open(os.path.join(pre, n)).convert("RGB"), dtype=np.int16)
        y = np.asarray(Image.open(os.path.join(post, n)).convert("RGB"), dtype=np.int16)
        d = int(np.abs(x - y).max()) if x.shape == y.shape else 255
        if d > 0:
            print("%-28s max diff %d/255, %.3f%% px" % (n[:-4], d, 100.0 * float((np.abs(x - y).max(axis=2) > 0).mean())))
            changed += 1
    print("%d of %d changed" % (changed, len(names)))
    return 1 if changed else 0


def diff(a):
    out = os.path.abspath(a.out)
    pre = json.load(open(os.path.join(out, Key(a, "pre") + ".json")))
    post = json.load(open(os.path.join(out, Key(a, "post") + ".json")))
    worse = 0
    for case in sorted(set(pre) | set(post)):
        o, n = pre.get(case), post.get(case)
        if n is None:
            print("%-28s %7.3f       -  MISSING" % (case, o["flip"]))
            worse += 1
            continue
        if o is None:
            print("%-28s       - %7.3f  NEW" % (case, n["flip"]))
            continue
        d = n["flip"] - o["flip"]
        tag = "WORSE" if d > SLACK else ("better" if d < -SLACK else "")
        worse += tag == "WORSE"
        print("%-28s %7.3f %7.3f  %5.3f %s" % (case, o["flip"], n["flip"], n["brightness"], tag))
    print("%d worse" % worse)
    return 1 if worse else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["run", "diff", "same"])
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--engine", default="raster", choices=["raster", "pt", "live", "settle"])
    ap.add_argument("--tag", default="post", choices=["pre", "post"])
    ap.add_argument("--out", default=os.path.expanduser("~/.cache/infinite-sweep"))
    ap.add_argument("--spp", type=int, default=0, help="settle/pt samples (tiers 0 and 1); 0 = harness default")
    a = ap.parse_args()
    return {"run": run, "diff": diff, "same": same}[a.mode](a)


if __name__ == "__main__":
    sys.exit(main())
