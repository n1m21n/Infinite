#!/usr/bin/env python3
"""Before/after sweep for render changes (docs/plans/realtime-engine/PLAN.md section 5).

  python3 tools/blender-ab/sweep.py run  --engine live --tag pre  [--out DIR] [cases...]
  python3 tools/blender-ab/sweep.py run  --engine live --tag post [--out DIR] [cases...]
  python3 tools/blender-ab/sweep.py diff --engine live [--out DIR]

`run` renders every case (or the named ones) through harness.py and saves case -> FLIP, brightness as
<out>/<tag>_<engine>.json. `diff` compares pre with post: a case that rose by more than SLACK FLIP is WORSE
and the exit code is 1. Truth renders are reused by the harness, so only Infinite renders again.
"""
import argparse
import json
import os
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
    cmd = [sys.executable, os.path.join(HERE, "harness.py"), *a.cases, "--engine", a.engine, "--out", os.path.join(out, "ab")]
    p = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True)
    rows = {}
    for line in (p.stdout + p.stderr).splitlines():
        if "ABRESULT" in line:
            d = json.loads(line.split("ABRESULT", 1)[1])
            rows[d["case"]] = {"flip": round(d.get("flip", 0.0), 4), "brightness": round(d.get("brightness_ratio", 0.0), 3)}
        elif "rror" in line:
            print(line)
    path = os.path.join(out, "%s_%s.json" % (a.tag, a.engine))
    json.dump(rows, open(path, "w"), indent=1, sort_keys=True)
    print("%d cases -> %s" % (len(rows), path))
    return 0


def diff(a):
    out = os.path.abspath(a.out)
    pre = json.load(open(os.path.join(out, "pre_%s.json" % a.engine)))
    post = json.load(open(os.path.join(out, "post_%s.json" % a.engine)))
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
    ap.add_argument("mode", choices=["run", "diff"])
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--engine", default="raster", choices=["raster", "pt", "live", "settle"])
    ap.add_argument("--tag", default="post", choices=["pre", "post"])
    ap.add_argument("--out", default=os.path.expanduser("~/.cache/infinite-sweep"))
    a = ap.parse_args()
    return run(a) if a.mode == "run" else diff(a)


if __name__ == "__main__":
    sys.exit(main())
