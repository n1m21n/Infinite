#!/usr/bin/env python3
"""Journey tests (internal/plans/ui-system/journey-tests.md): plays each tools/journeys/<area>/*.json through the
debug app's real mouse and keys, checks the result, writes journeys/report.md. Needs a dev build.

  python3 tools/journeys/run.py [--area 2d] [--budget 300] [--strict]

--strict also fails when a 2D tool or registry control has no journey and no skip line (coverage)."""
import argparse, glob, json, os, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BIN = os.path.join(ROOT, "build-dev/Infinite.app/Contents/MacOS/Infinite")


def coverage(area_dir):
    """Controls the journeys click or declare, against the audit registry and the skip file."""
    covered = set()
    for f in glob.glob(os.path.join(area_dir, "*.json")):
        j = json.load(open(f))
        covered.update(j.get("covers", []))
        covered.update(s["click"] for s in j.get("steps", []) if "click" in s)
    skip = set()
    sk = os.path.join(area_dir, "skip.tsv")
    if os.path.exists(sk):
        skip = {l.split("\t")[0] for l in open(sk) if l.strip() and not l.startswith("#")}
    reg = os.path.join(ROOT, "ui-audit-view2d", "registry.json")
    labels = set()
    if os.path.exists(reg):
        for c in json.load(open(reg)):
            if c.get("status") != "skipped" and c.get("label"):
                labels.add(c["label"])
    missing = sorted(l for l in labels if l not in covered and l not in skip)
    return len(labels), len(labels) - len(missing), missing


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--area", default="2d")
    ap.add_argument("--budget", type=int, default=300)
    ap.add_argument("--out", default=os.path.join(ROOT, "journeys"))
    ap.add_argument("--app", default="")
    ap.add_argument("--strict", action="store_true")
    a = ap.parse_args()
    area_dir = os.path.join(ROOT, "tools", "journeys", a.area)
    out = os.path.join(a.out, a.area)
    os.makedirs(out, exist_ok=True)
    tsv = os.path.join(out, "report.tsv")
    if os.path.exists(tsv):
        os.remove(tsv)
    log = os.path.join(out, "run.log")
    env = dict(os.environ, INFINITE_JOURNEYS=area_dir, INFINITE_JOURNEYS_OUT=out, HOME=tempfile.mkdtemp(prefix="journeys-home-"))
    p = subprocess.Popen(["script", "-q", log, a.app or BIN], env=env, stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        p.wait(timeout=a.budget)
    except subprocess.TimeoutExpired:
        p.kill()
        print("killed after %d s; see %s" % (a.budget, log))
    if not os.path.exists(tsv):
        print("no report; see %s" % log)
        return 2
    rows = [l.rstrip("\n").split("\t") for l in open(tsv)][1:]
    titles = {json.load(open(f))["id"]: json.load(open(f)).get("title", "") for f in glob.glob(os.path.join(area_dir, "*.json"))}
    n_pass = sum(r[1] == "pass" for r in rows)
    n_human = sum(r[1] == "human-element" for r in rows)
    bad = [r for r in rows if r[1] not in ("pass", "human-element")]
    total, cov, missing = coverage(area_dir)
    md = ["# Journeys: %s" % a.area, "",
          "| Result | Count |", "|---|---|",
          "| pass | %d |" % n_pass, "| human-element (more steps than the reference) | %d |" % n_human,
          "| fail / error | %d |" % len(bad), "| controls covered | %d / %d |" % (cov, total), "",
          "| Journey | Result | Steps | Ref | Detail |", "|---|---|---|---|---|"]
    for r in rows:
        md.append("| %s %s | %s | %s | %s | %s |" % (r[0], titles.get(r[0], ""), r[1], r[2], r[3], r[4] if len(r) > 4 else ""))
    open(os.path.join(out, "report.md"), "w").write("\n".join(md) + "\n")
    print("\n".join(md[2:8]))
    for r in bad:
        print("  %s %s: %s" % (r[0], r[1], r[4] if len(r) > 4 else ""))
    if a.strict and missing:
        print("uncovered: %d (first: %s)" % (len(missing), ", ".join(missing[:10])))
        return 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
