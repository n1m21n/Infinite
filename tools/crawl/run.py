#!/usr/bin/env python3
"""Crawler (internal/plans/ui-system/next-session.md, Block 1).

  python3 tools/crawl/run.py                         every surface: inventory + every-setting pass, dark then light
  python3 tools/crawl/run.py --surface Mixer,v3d     only surfaces whose name holds one of these
  python3 tools/crawl/run.py --walk 400 --seed 7     a random walk of 400 actions instead
  python3 tools/crawl/run.py --nightly               3 seeds x 400 steps
  python3 tools/crawl/run.py --replay 7 --upto 132   replay seed 7 up to step 132 and take a shot there
  python3 tools/crawl/run.py --changed               pre-push: only surfaces whose source changed against origin/main

Writes crawl/report.md, crawl/<surface>.tsv (inventory per surface), crawl/shots/. Needs a debug build
(python3 scripts/ibuild.py build --mode dev --target Infinite). Exit 1 on any failure.
"""
import argparse, os, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.path.join(ROOT, "build-dev/Infinite.app/Contents/MacOS/Infinite")
PATCH = os.path.join(ROOT, "assets/examples/patch_1.inf")
SKIP = os.path.join(ROOT, "tools/crawl/skip.tsv")
BASELINE = os.path.join(ROOT, "tools/crawl/baseline.tsv")
# Known slow first-poke frames per surface (open question in internal/plans/ui-system/next-session.md); a run fails
# only when a surface is 1.5x slower than stored, or over 100 ms with nothing stored.
SLOW_BASE = os.path.join(ROOT, "tools/crawl/slow_baseline.tsv")

# Source file substring -> surface name substring, for --changed. The 2D View maps to its first state only: its ~100
# states take hours, and the pre-push 2D stage (golden_view2d audit + journeys) already walks them.
SOURCES = {
    "MixerPanel": "panel-Mixer", "Drum": "panel-Drums", "SettingsPanel": "panel-Settings", "SettingsWindow": "panel-Settings", "Help": "panel-Help",
    "Shortcuts": "panel-Shortcuts", "Templates": "panel-Templates", "History": "panel-History",
    "PianoRoll": "panel-Piano Roll", "CurveEditor": "panel-Curve Editor", "SampleStudio": "panel-Sample Studio",
    "ShowSet": "panel-Show Set", "NodePanel": "panel-Library", "Library": "panel-Library", "ViewportPanel": "panel-Viewport",
    "Arrange": "panel-Arrange", "ModMatrix": "panel-Mod Matrix", "PerfPanel": "panel-Performance",
    "View3D": "v3d", "View2D": "v2d-00", "nodes/": "node-", "PanelKit": "panel-", "components/": "panel-",
}


def read_tsv(path):
    if not os.path.exists(path):
        return []
    lines = open(path, errors="replace").read().splitlines()
    if not lines:
        return []
    head = lines[0].split("\t")
    return [dict(zip(head, l.split("\t"))) for l in lines[1:] if l and not l.startswith("#")]


def changed_surfaces():
    try:
        out = subprocess.run(["git", "diff", "--name-only", "origin/main...HEAD"], cwd=ROOT, capture_output=True,
                             text=True, check=True).stdout.split()
    except subprocess.CalledProcessError:
        return []
    hit = set()
    for f in out:
        if not f.startswith("src/"):
            continue
        for k, v in SOURCES.items():
            if k in f:
                hit.add(v)
    return sorted(hit)


def run_once(a, out, env_extra, budget):
    os.makedirs(out, exist_ok=True)
    for f in ("report.tsv", "coverage.tsv", "stats.tsv", "walk.tsv", "progress.txt"):
        if os.path.exists(os.path.join(out, f)):
            os.remove(os.path.join(out, f))
    log = os.path.join(out, "run.log")
    env = dict(os.environ, INFINITE_CRAWL=out, INFINITE_CRAWL_SKIP=SKIP, INFINITE_CRAWL_PATCH=PATCH,
               HOME=tempfile.mkdtemp(prefix="crawl-home-"), **env_extra)
    t0 = time.time()
    # Under a pty: redirected output is lost if the run is killed.
    p = subprocess.Popen(["script", "-q", log, a.app or BIN], env=env, stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    killed = False
    try:
        p.wait(timeout=budget)
    except subprocess.TimeoutExpired:
        p.kill()
        killed = True
    prog = open(os.path.join(out, "progress.txt"), errors="replace").read().splitlines() if os.path.exists(
        os.path.join(out, "progress.txt")) else []
    last = prog[-1] if prog else "(no progress written)"
    crashed = not killed and (not prog or prog[-1] != "done")
    errors = [l.strip() for l in open(log, errors="replace") if "ERROR" in l or "Assertion" in l] if os.path.exists(log) else []
    return dict(secs=time.time() - t0, killed=killed, crashed=crashed, last=last, errors=errors[:20])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "crawl"))
    ap.add_argument("--surface", default="")
    ap.add_argument("--walk", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--nightly", action="store_true")
    ap.add_argument("--replay", type=int, default=-1)
    ap.add_argument("--upto", type=int, default=0)
    ap.add_argument("--changed", action="store_true")
    ap.add_argument("--dark-only", action="store_true")
    ap.add_argument("--budget", type=int, default=1200, help="seconds before a run is killed")
    ap.add_argument("--rebaseline", action="store_true")
    ap.add_argument("--app", default="")
    ap.add_argument("--report-only", action="store_true", help="re-read --out from an earlier pass (e.g. to --rebaseline) without running")
    a = ap.parse_args()
    if not os.path.exists(a.app or BIN):
        print("no debug build at %s" % (a.app or BIN))
        return 2
    # The crawler lives behind IMGUI_ENABLE_TEST_ENGINE (src/app/selftest/UiAudit.cpp); a release
    # binary has a no-op stub, never writes progress.txt, and would sit until the budget kills it.
    with open(a.app or BIN, "rb") as fh:
        if b"INFINITE_CRAWL_SKIP" not in fh.read():
            print("crawl needs the dev build (IMGUI_ENABLE_TEST_ENGINE); %s has no crawler code" % (a.app or BIN))
            return 2

    runs = []
    if a.replay >= 0:
        runs.append(("replay-%d" % a.replay, {"INFINITE_CRAWL_WALK": str(a.upto or 400), "INFINITE_CRAWL_SEED": str(a.replay),
                                              "INFINITE_CRAWL_UPTO": str(a.upto)}))
    elif a.nightly or a.walk:
        seeds = [a.seed, a.seed + 1, a.seed + 2] if a.nightly else [a.seed]
        for s in seeds:
            runs.append(("walk-%d" % s, {"INFINITE_CRAWL_WALK": str(a.walk or 400), "INFINITE_CRAWL_SEED": str(s)}))
    else:
        surf = a.surface
        if a.changed:
            surf = ",".join(changed_surfaces())
            if not surf:
                print("crawl: no surface source changed")
                return 0
        extra = {"INFINITE_CRAWL_SURFACE": surf} if surf else {}
        if a.dark_only:
            extra["INFINITE_CRAWL_DARK_ONLY"] = "1"
        runs.append(("pass", extra))

    fail = False
    md = ["# Crawl\n"]
    for name, extra in runs:
        out = a.out if len(runs) == 1 else os.path.join(a.out, name)
        if a.walk or a.nightly or a.replay >= 0:
            extra.setdefault("INFINITE_CRAWL_SURFACE", a.surface or "panel,v3d,v2d-00")
        r = {"secs": 0, "killed": False, "crashed": False, "last": "(report only)", "errors": []} if a.report_only \
            else run_once(a, out, extra, a.budget)
        md.append("## %s\n\n%.0f s%s%s; last step: `%s`\n" % (name, r["secs"], ", KILLED (budget)" if r["killed"] else "",
                                                             ", CRASHED" if r["crashed"] else "", r["last"]))
        if r["killed"] or r["crashed"]:
            fail = True
        if r["errors"]:
            md.append("Log errors:\n\n" + "\n".join("    " + e for e in r["errors"]) + "\n")
        if name.startswith("walk") or name.startswith("replay"):
            walk = read_tsv(os.path.join(out, "walk.tsv"))
            if walk:
                fail = fail or not name.startswith("replay")
                md.append("| Seed | Step | Action | Invariant | Detail |\n|---|---|---|---|---|")
                for w in walk:
                    md.append("| %s | %s | %s | %s | %s |" % (w["seed"], w["step"], w["action"].replace("|", "\\|"),
                                                             w["invariant"], w["detail"].replace("|", "\\|")))
                md.append("\nReplay: `python3 tools/crawl/run.py --replay %s --upto %s`\n" % (walk[0]["seed"], walk[0]["step"]))
            else:
                md.append("No invariant broke.\n")
            continue
        # Every-setting pass.
        cov = read_tsv(os.path.join(out, "coverage.tsv"))
        rep = read_tsv(os.path.join(out, "report.tsv"))
        stats = read_tsv(os.path.join(out, "stats.tsv"))
        by_surf = {}
        for c in cov:
            by_surf.setdefault(c["surface"], []).append(c)
        for s, rows in by_surf.items():
            with open(os.path.join(out, s.replace("/", "_") + ".tsv"), "w") as f:
                f.write("status\tkind\twindow\tlabel\ttip\tstate\treason\n")
                for c in rows:
                    f.write("\t".join(c.get(k, "") for k in ("status", "kind", "window", "label", "tip", "state", "reason")) + "\n")
        base = {(b["check"], b["window"], b["label"]) for b in read_tsv(BASELINE)}
        rep = [x for x in rep if x["window"] != "UI audit canary"]
        new = [x for x in rep if (x["check"], x["window"], x["label"]) not in base]
        if a.rebaseline:
            # Rows of surfaces this run did not crawl are kept, so a --surface run rebaselines only its own.
            ran = set(by_surf) | {"home"}
            keep = [b for b in read_tsv(BASELINE) if not any(s in b["state"].split() for s in ran)]
            with open(BASELINE, "w") as f:
                f.write("check\tstate\twindow\tlabel\tdetail\n")
                for x in keep + rep:
                    f.write("\t".join(x[k] for k in ("check", "state", "window", "label", "detail")) + "\n")
            new = []
        slow_base = {b["surface"]: float(b["slow_ms"]) for b in read_tsv(SLOW_BASE)}
        if a.rebaseline:
            for x in stats:
                if x["surface"] != "walk":
                    slow_base[x["surface"]] = float(x.get("slow_ms") or 0)
            with open(SLOW_BASE, "w") as f:
                f.write("surface\tslow_ms\n")
                for k in sorted(slow_base):
                    f.write("%s\t%.0f\n" % (k, slow_base[k]))
        if new:
            fail = True
        md.append("%d findings, %d new\n" % (len(rep), len(new)))
        md.append("| Surface | Controls | Poked | Skipped | Untested | Failing | Slow ms | GL errors |\n|---|---|---|---|---|---|---|---|")
        st = {x["surface"]: x for x in stats}
        for s in sorted(by_surf):
            rows = by_surf[s]
            n = lambda k: sum(1 for c in rows if c["status"] == k)
            x = st.get(s, {})
            md.append("| %s | %d | %d | %d | %d | %d | %s | %s |" % (s, len(rows), n("pass") + n("fail"), n("skipped"), n("untested"),
                                                                    n("fail"), x.get("slow_ms", ""), x.get("gl_errors", "")))
            if x and (float(x.get("slow_ms") or 0) > max(100.0, 1.5 * slow_base.get(s, 0.0)) or int(x.get("gl_errors") or 0) > 0):
                fail = True
        by = {}
        for x in rep:
            by.setdefault(x["check"], []).append(x)
        md.append("\n| Check | Count | New |\n|---|---|---|")
        for c in sorted(by):
            md.append("| %s | %d | %d |" % (c, len(by[c]), sum(1 for x in by[c] if x in new)))
        md.append("\n| Check | State | Window | Control | Detail |\n|---|---|---|---|---|")
        for x in new:
            md.append("| %s | %s | %s | %s | %s |" % (x["check"], x["state"], x["window"], x["label"].replace("|", "\\|"),
                                                     x["detail"].replace("|", "\\|")))
        shots = os.path.join(out, "shots")
        if os.path.isdir(shots):
            md.append("\nShots: %d in %s\n" % (len(os.listdir(shots)), shots))
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "report.md"), "w") as f:
        f.write("\n".join(md) + "\n")
    print("crawl %s -> %s" % ("FAIL" if fail else "ok", os.path.join(a.out, "report.md")))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
