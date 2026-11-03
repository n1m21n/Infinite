#!/usr/bin/env python3
"""UI audit, layers 2 and 3 (docs/plans/ui-system/ui-audit.md).

  python3 tools/ui-audit/run.py                 run the debug build, write ui-audit/report.md
  python3 tools/ui-audit/run.py --rebaseline    accept today's findings as tools/ui-audit/baseline.tsv

Needs a debug build (python3 scripts/ibuild.py build --mode dev). Exit 1 when a finding is not in the
baseline, or when a canary check did not fire (the audit is not looking).
"""
import argparse, os, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.path.join(ROOT, "build-dev/Infinite.app/Contents/MacOS/Infinite")
PATCH = os.path.join(ROOT, "tests/render3d/shadows.inf")
BASELINE = os.path.join(ROOT, "tools/ui-audit/baseline.tsv")
CANARY = {"duplicate id", "clipped", "tiny target", "overlap", "hit smaller than drawn", "text overflow", "contrast text"}


def key(row):
    return (row["check"], row["window"], row["label"])


def read_tsv(path):
    if not os.path.exists(path):
        return [], ""
    lines = open(path).read().splitlines()
    head = lines[0].split("\t")
    rows = [dict(zip(head, l.split("\t"))) for l in lines[1:] if not l.startswith("#")]
    tail = next((l for l in lines if l.startswith("#")), "").lstrip("#").strip()
    return rows, tail


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "ui-audit"))
    ap.add_argument("--budget", type=int, default=300, help="seconds before the run is killed")
    ap.add_argument("--nodes", default="", help="comma list of node types to cover, or none (default: every registered type)")
    ap.add_argument("--dark-only", action="store_true")
    ap.add_argument("--rebaseline", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    tsv = os.path.join(a.out, "report.tsv")
    if os.path.exists(tsv):
        os.remove(tsv)
    log = os.path.join(a.out, "run.log")
    env = dict(os.environ, INFINITE_UIAUDIT="1", INFINITE_UIAUDIT_PATCH=PATCH, INFINITE_UIAUDIT_OUT=a.out,
               HOME=tempfile.mkdtemp(prefix="uiaudit-home-"))
    if a.nodes:
        env["INFINITE_UIAUDIT_NODES"] = a.nodes
    if a.dark_only:
        env["INFINITE_UIAUDIT_DARK_ONLY"] = "1"
    t0 = time.time()
    # Under a pty: redirected output is lost if the run is killed.
    p = subprocess.Popen(["script", "-q", log, BIN], env=env, stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        p.wait(timeout=a.budget)
    except subprocess.TimeoutExpired:
        p.kill()
        print("killed after %d s; see %s" % (a.budget, log))
        return 2
    rows, tail = read_tsv(tsv)
    if not os.path.exists(tsv):
        print("no report written; see %s" % log)
        return 2
    canary = [r for r in rows if r["window"] == "UI audit canary"]
    rows = [r for r in rows if r["window"] != "UI audit canary"]
    missed = sorted(CANARY - {r["check"] for r in canary})
    base, _ = read_tsv(BASELINE)
    known = {key(r) for r in base}
    new = [r for r in rows if key(r) not in known]
    if a.rebaseline:
        with open(BASELINE, "w") as f:
            f.write("check\tstate\twindow\tlabel\tdetail\n")
            for r in rows:
                f.write("\t".join(r[c] for c in ("check", "state", "window", "label", "detail")) + "\n")
        new = []
    by = {}
    for r in rows:
        by.setdefault(r["check"], []).append(r)
    with open(os.path.join(a.out, "report.md"), "w") as f:
        f.write("# UI audit\n\n%d findings, %d new, %s, %.0f s\n\n" % (len(rows), len(new), tail, time.time() - t0))
        f.write("Canary: %s\n\n" % ("all checks fired" if not missed else "MISSED " + ", ".join(missed)))
        f.write("| Check | Count |\n|---|---|\n")
        for c in sorted(by):
            f.write("| %s | %d |\n" % (c, len(by[c])))
        f.write("\n| Check | State | Window | Control | Detail | New |\n|---|---|---|---|---|---|\n")
        for c in sorted(by):
            for r in by[c]:
                f.write("| %s | %s | %s | %s | %s | %s |\n" % (c, r["state"], r["window"], r["label"].replace("|", "\\|"),
                                                          r["detail"], "new" if key(r) not in known else ""))
    print("%d findings (%d new), %s; canary %s -> %s" % (len(rows), len(new), tail,
                                                       "ok" if not missed else "MISSED " + ",".join(missed),
                                                       os.path.join(a.out, "report.md")))
    return 1 if new or missed else 0


if __name__ == "__main__":
    sys.exit(main())
