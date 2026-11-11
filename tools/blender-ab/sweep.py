#!/usr/bin/env python3
"""Before/after sweep for render changes (docs/plans/realtime-engine/PLAN.md section 5).

  sweep.py [--engine raster|live|settle] [--tag NAME] [--spp N] [--ab ggx,..] [--touch G1,G2] [--skip G] [--budget MIN] run [cases]
  sweep.py [--engine E] [--base pre] [--tag post] diff
  sweep.py [--engine E] [--spp N] same            (tier 0: pixel identity, base vs tag images)
  sweep.py [--engine E] [--spp N] --tag T pair A B   (FLIP between two cases' Infinite pictures of one run)
  sweep.py pick GROUP...                          (which cases a change can touch)
  sweep.py times                                  (seconds per case per engine, from past runs)

Cheap by design (8 GB M2):
  --touch   runs only the cases whose scene has the feature the change touches (GROUPS below, or any regex
            on the scene's JSON). A falloff change needs 6 cases, not 43.
  --ab      turns on shader A/B switches for this run (INFINITE_R3D_AB, src/nodes/Render3DAb.h): variants are
            compared from ONE build; no rebuild per variant.
  --budget  minutes; cases run fastest first (by their last measured time) and the run stops at the budget,
            keeping what finished. Default 20.
  merge     `run` updates only the cases it ran in <tag>_<engine>.json; `diff` compares only the cases of the
            tag's latest run (--all for every stored case), so a partial run never wipes or fakes a result.
  All runs are at background priority (taskpolicy -b). Truth renders are cached by the harness.

Tiers: 0 refactor: `--spp 16 run` pre and post, then `same`. 1 real change: `--touch G run`, `diff`.
       2 block close: full `run`, `diff`.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SLACK = 0.005
# Cases whose FLIP is not a gate: tex_breakup has no Cycles counterpart (its truth is the plain tile), so it is
# judged by tile_repeat.py; diff prints it but never counts it worse or better.
NO_GATE = {"tex_breakup"}

# What a change can touch -> regex on the case's scene JSON. `pick` lists the cases.
GROUPS = {
    "direct": r'"lights": \[\{',                       # any analytic light: direct diffuse + specular
    "falloff": r'"type": "(point|spot|area)"',          # distance falloff
    "sun": r'"type": "sun"',
    "spec": r'"lights": \[\{|"world"',                  # specular lobe (direct or environment)
    "gloss": r'"roughness": 0\.(0|1|2|3)',               # sharp highlights: GGX / Fresnel shape
    "metal": r'"metallic"', "coat": r'"coat"', "sheen": r'"sheen"', "glass": r'"transmission"',
    "sss": r'"subsurface"', "aniso": r'"anisotropy"', "thinfilm": r'"iridescence"', "emission": r'"emission',
    "maps": r'"maps"', "shade": r'"shade"', "hair": r'"(hair|fur)', "hdri": r'"hdri"', "motion": r'"motion"', "dof": r'"(fstop|aperture|dof)',
}


def pick(groups):
    sys.path.insert(0, HERE)
    from cases import CASES
    pats = [re.compile(GROUPS.get(g, g)) for g in groups]
    return [n for n, s in CASES.items() if any(p.search(json.dumps(s, sort_keys=True)) for p in pats)]


def load(path):
    return json.load(open(path)) if os.path.exists(path) else {}


def times_path(out):
    return os.path.join(out, "times.json")


def run(a):
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ)
    env.setdefault("INFINITE_BIN", os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite"))
    if a.ab:
        env["INFINITE_R3D_AB"] = a.ab
    cases = list(a.cases)
    if a.touch:
        cases = [c for c in pick(a.touch) if not a.cases or c in a.cases]
    if not cases:
        sys.path.insert(0, HERE)
        from cases import CASES
        cases = list(CASES)
    if a.skip:
        drop = set(pick(a.skip))
        cases = [c for c in cases if c not in drop]
    times = load(times_path(out))
    known = times.get(a.engine, {})
    cases.sort(key=lambda c: known.get(c, 1e9 if known else 0))   # fastest first, unmeasured last
    est = sum(known.get(c, 0) for c in cases)
    print("%d cases on %s%s, about %.0f s by past runs, budget %g min" % (
        len(cases), a.engine, " ab=" + a.ab if a.ab else "", est, a.budget), flush=True)
    cmd = ["taskpolicy", "-b", sys.executable, "-u", os.path.join(HERE, "harness.py"), *cases, "--engine", a.engine,
           "--out", os.path.join(out, "ab")]
    if a.spp:
        cmd += ["--pt-samples", str(a.spp)]
    t0 = time.time()
    last = t0
    p = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    rows = {}
    stamp = int(t0)
    img = os.path.join(out, "img", Key(a, a.tag))   # this run's Infinite images, for `same` (tier 0)
    os.makedirs(img, exist_ok=True)
    suffix = {"raster": "", "pt": "_pt", "live": "_live", "settle": "_settle"}[a.engine]
    path = os.path.join(out, Key(a, a.tag) + ".json")
    merged = {} if a.fresh else load(path)
    for line in p.stdout:
        if "ABRESULT" in line:
            d = json.loads(line.split("ABRESULT", 1)[1])
            now = time.time()
            rows[d["case"]] = {"flip": round(d.get("flip", 0.0), 4), "brightness": round(d.get("brightness_ratio", 0.0), 3),
                               "run": stamp}
            known[d["case"]] = round(now - last, 1)
            print("%-26s %.3f  %5.1f s" % (d["case"], rows[d["case"]]["flip"], now - last), flush=True)
            last = now
            # Stored case by case, so a stopped or killed run keeps what it finished.
            src = os.path.join(out, "ab", d["case"], "inf%s.png" % suffix)
            if os.path.exists(src):
                shutil.copy(src, os.path.join(img, d["case"] + ".png"))
            merged[d["case"]] = rows[d["case"]]
            json.dump(merged, open(path, "w"), indent=1, sort_keys=True)
            times[a.engine] = known
            json.dump(times, open(times_path(out), "w"), indent=1, sort_keys=True)
            if now - t0 > a.budget * 60 and len(rows) < len(cases):
                p.kill()
                print("budget reached: stopped after %d of %d cases; not run: %s" % (
                    len(rows), len(cases), " ".join(c for c in cases if c not in rows)))
                break
        elif "rror" in line or line.startswith("skip") or "failed" in line:
            print(line.rstrip())
    p.wait()
    print("%d cases in %.0f s -> %s (%d stored)" % (len(rows), time.time() - t0, path, len(merged)))
    return 0 if len(rows) == len(cases) else 2


def Key(a, tag):
    return "%s_%s%s" % (tag, a.engine, "_s%d" % a.spp if a.spp else "")


def latest(rows, everything):
    """The cases of the newest run in a result file (files from before run stamps count as one run)."""
    newest = max((r.get("run", 0) for r in rows.values()), default=0)
    return {c for c, r in rows.items() if everything or r.get("run", 0) == newest}


def same(a):
    import numpy as np
    from PIL import Image
    out = os.path.abspath(a.out)
    pre, post = os.path.join(out, "img", Key(a, a.base)), os.path.join(out, "img", Key(a, a.tag))
    only = latest(load(os.path.join(out, Key(a, a.tag) + ".json")), a.all)
    changed = 0
    names = sorted(n for n in set(os.listdir(pre)) | set(os.listdir(post)) if n[:-4] in only)
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


def pair(a):
    """FLIP between the Infinite pictures of two cases from one result set: a feature built two ways
    (row 1.4: Field material vs knobs) must give the same picture."""
    import numpy as np
    from PIL import Image
    import flip_evaluator as flip
    if len(a.cases) != 2:
        sys.exit("pair needs two cases")
    img = os.path.join(os.path.abspath(a.out), "img", Key(a, a.tag))
    x, y = [np.asarray(Image.open(os.path.join(img, c + ".png")).convert("RGB"), dtype=np.float32) / 255.0 for c in a.cases]
    _, mean, _ = flip.evaluate(x, y, "LDR")
    d = np.abs(x - y).max()
    print("%s vs %s on %s: FLIP %.4f, max diff %d/255" % (a.cases[0], a.cases[1], Key(a, a.tag), mean, round(d * 255)))
    return 0


def diff(a):
    out = os.path.abspath(a.out)
    pre = load(os.path.join(out, Key(a, a.base) + ".json"))
    post = load(os.path.join(out, Key(a, a.tag) + ".json"))
    worse = better = 0
    total = 0.0
    cases = sorted(latest(post, a.all))
    for case in cases:
        o, n = pre.get(case), post[case]
        if o is None:
            print("%-28s       - %7.3f  NEW" % (case, n["flip"]))
            continue
        d = n["flip"] - o["flip"]
        total += d
        if case in NO_GATE:
            print("%-28s %7.3f %7.3f  %+.3f  b %5.3f (not gated)" % (case, o["flip"], n["flip"], d, n["brightness"]))
            total -= d
            continue
        tag = "WORSE" if d > SLACK else ("better" if d < -SLACK else "")
        worse += tag == "WORSE"; better += tag == "better"
        print("%-28s %7.3f %7.3f  %+.3f  b %5.3f %s" % (case, o["flip"], n["flip"], d, n["brightness"], tag))
    print("%s -> %s on %s: %d cases, %d worse, %d better, sum %+.3f" % (a.base, a.tag, a.engine, len(cases), worse, better, total))
    return 1 if worse else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["run", "diff", "same", "pair", "pick", "times"])
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--engine", default="raster", choices=["raster", "pt", "live", "settle"])
    ap.add_argument("--tag", default="post", help="name of this result set (pre, post, or a variant name)")
    ap.add_argument("--base", default="pre", help="diff/same: the result set to compare against")
    ap.add_argument("--out", default=os.path.expanduser("~/.cache/infinite-sweep"))
    ap.add_argument("--spp", type=int, default=0, help="settle/pt samples (tiers 0 and 1); 0 = harness default")
    ap.add_argument("--ab", default="", help="shader A/B switches on for this run, e.g. ggx,fresnel")
    ap.add_argument("--touch", default=[], type=lambda v: [g for g in v.split(",") if g],
                    help="run only cases in these groups, comma-separated (see `pick`)")
    ap.add_argument("--skip", default=[], type=lambda v: [g for g in v.split(",") if g],
                    help="leave out cases in these groups (e.g. hair: ~150 s each on live)")
    ap.add_argument("--budget", type=float, default=20.0, help="minutes; the run stops there and keeps what finished")
    ap.add_argument("--fresh", action="store_true", help="run: replace the stored result set instead of merging")
    ap.add_argument("--all", action="store_true", help="diff/same: every stored case, not only the latest run's")
    a = ap.parse_args()
    if a.mode == "pick":
        for g in a.cases or sorted(GROUPS):
            c = pick([g])
            print("%-9s %2d  %s" % (g, len(c), " ".join(c)))
        return 0
    if a.mode == "times":
        t = load(times_path(os.path.abspath(a.out)))
        for e, rows in t.items():
            print("%s: %d cases, %.0f s total; slowest: %s" % (e, len(rows), sum(rows.values()), ", ".join(
                "%s %.0f" % (c, s) for c, s in sorted(rows.items(), key=lambda x: -x[1])[:5])))
        return 0
    return {"run": run, "diff": diff, "same": same, "pair": pair}[a.mode](a)


if __name__ == "__main__":
    sys.exit(main())
