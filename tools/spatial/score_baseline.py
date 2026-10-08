#!/usr/bin/env python3
"""Score answers_*.json from listening_test.html against key.json.

usage: score_baseline.py [KIT_DIR]   (default ~/infinite-spatial-baseline)
Writes KIT_DIR/scores.json and prints one row per condition. The first full
run is the locked baseline (docs/plans/spatial/README.md, section 6).
"""
import glob, json, os, sys

BENCH = {"az_err": 20.0, "fb_conf": 25.0, "el_err": 25.0}  # v2 benchmarks, untracked


def wrap(d):
    d = abs(d) % 360
    return min(d, 360 - d)


def score(key, cond, answers):
    order = key["orders"][cond]
    az_errs, el_errs, fb, fb_n = [], [], 0, 0
    for a in answers:
        p = key["positions"][order[a["trial"]]]
        err = wrap(a["az"] - p["az"])
        az_errs.append(err)
        el_errs.append(abs(a["el"] - p["el"]))
        if p["az"] not in (90, 270):  # on the interaural axis there is no mirror
            fb_n += 1
            if wrap(a["az"] - (180 - p["az"])) < err:
                fb += 1
    m = lambda v: round(sum(v) / len(v), 1) if v else None
    return {"trials": len(answers), "az_err": m(az_errs),
            "fb_conf": round(100 * fb / fb_n, 1) if fb_n else None,
            "el_err": m(el_errs)}


def main():
    kit = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else "~/infinite-spatial-baseline")
    key = json.load(open(os.path.join(kit, "key.json")))
    res = {}
    for path in sorted(glob.glob(os.path.join(kit, "answers_*.json"))):
        d = json.load(open(path))
        res[d["condition"]] = score(key, d["condition"], d["answers"])
    res["benchmarks"] = BENCH
    json.dump(res, open(os.path.join(kit, "scores.json"), "w"), indent=1)
    print(f"{'condition':<16}{'az err':>8}{'f/b %':>8}{'el err':>8}")
    for c, r in res.items():
        if c != "benchmarks":
            print(f"{c:<16}{r['az_err']:>8}{r['fb_conf']:>8}{r['el_err']:>8}")
    print(f"{'target':<16}{'<=20':>8}{'<=25':>8}{'<=25':>8}")


if __name__ == "__main__":
    main()
