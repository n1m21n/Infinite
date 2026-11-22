#!/usr/bin/env python3
"""re-survey app_bench: how heavy an app is, measured from outside with what macOS ships (ps, footprint, du, open).

Per app and run: time from `open` to the first window the window server lists (layer 0, >= 100x100), then an idle window of
`--idle` seconds sampled every second over the app's whole process tree (helpers such as an audio
engine or plugin host count): CPU % (mean and max, 100 % = one core), resident memory (MB), threads,
process count; memory is the summed `footprint` (what Activity Monitor shows). Bundle size on disk once. The app is quit between runs, so runs 2..N are warm starts.
Nothing is clicked or typed: an app showing a dialog at launch still idles behind it.

Usage:
  python3 tools/re-survey/app_bench.py run Logic Bitwig "Ableton Live 12 Suite" TouchDesigner ~/Desktop/Infinite.app [--runs 3] [--idle 20] [--out t.md]
  python3 tools/re-survey/app_bench.py --self-test
An app that is already running is measured as it is (idle only, no launch time) and left open. Results JSON in
~/re-output/app-bench/<stamp>/. Quiet machine first: other renders or benches skew CPU.
Exit: 0 ok, 1 an app failed / self-test failed, 2 usage error.
"""

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import RE_OUTPUT, SelfTest, ensure_dir, md_table, write_out  # noqa: E402
import ui_map  # noqa: E402


def ps_table():
    """{pid: (ppid, cpu%, rss_kb, comm)} for every process."""
    out = subprocess.run(["ps", "-axo", "pid=,ppid=,%cpu=,rss=,comm="], capture_output=True, text=True).stdout
    t = {}
    for line in out.splitlines():
        f = line.split(None, 4)
        if len(f) == 5:
            t[int(f[0])] = (int(f[1]), float(f[2]), int(f[3]), f[4])
    return t


def tree(root, table):
    kids = {}
    for pid, (ppid, *_r) in table.items():
        kids.setdefault(ppid, []).append(pid)
    out, stack = [], [root]
    while stack:
        p = stack.pop()
        if p in table:
            out.append(p)
            stack.extend(kids.get(p, []))
    return out


def threads(pids):
    if not pids:
        return 0
    r = subprocess.run(["ps", "-M", "-p", ",".join(map(str, pids))], capture_output=True, text=True).stdout
    return max(0, len(r.splitlines()) - 1)


def helpers_by_path(app, table):
    """Processes launched from inside the bundle but not children of the main one (XPC services, engines)."""
    r = subprocess.run(["pgrep", "-f", app + "/Contents/"], capture_output=True, text=True).stdout
    return [int(p) for p in r.split() if int(p) in table]


def sample(app, pid):
    t = ps_table()
    pids = sorted(set(tree(pid, t)) | set(helpers_by_path(app, t)))
    return {"cpu": sum(t[p][1] for p in pids), "rss_mb": sum(t[p][2] for p in pids) / 1024.0, "pids": pids,
            "threads": threads(pids), "procs": len(pids), "names": sorted({os.path.basename(t[p][3]) for p in pids})}


def footprint_mb(pids):
    """Sum of macOS memory footprints (what Activity Monitor shows; ps RSS misses compressed memory)."""
    total = 0.0
    for p in pids:
        r = subprocess.run(["footprint", str(p)], capture_output=True, text=True).stdout
        m = re.search(r"Footprint:\s*([\d.]+)\s*([KMG]B)", r)
        if m:
            total += float(m.group(1)) * {"KB": 1 / 1024.0, "MB": 1.0, "GB": 1024.0}[m.group(2)]
    return total


def bundle_mb(app):
    r = subprocess.run(["du", "-sk", app], capture_output=True, text=True).stdout.split()
    return int(r[0]) / 1024.0 if r else None


def bench_app(name, runs, idle, log):
    app = ui_map.find_app(os.path.expanduser(name))
    label = os.path.basename(app)[:-4]
    res = {"app": label, "bundle_mb": bundle_mb(app), "runs": []}
    running = ui_map.app_pid(app)
    if running:
        log("  %s already running: idle numbers only, no launch time, left open" % label)
    for i in range(1 if running else runs):
        if running:
            pid, up = running, None
        else:
            pid, up = ui_map.launch_and_wait(app, 180, log)
            up -= 8  # launch_and_wait settles 8 s after the window appears
        ss = []
        for _ in range(idle):
            ss.append(sample(app, pid))
            time.sleep(1)
        mem = footprint_mb(ss[-1]["pids"])
        if not running:
            ui_map.quit_app(app, pid, log, force=True)
            if ui_map.app_pid(app):
                raise RuntimeError("did not quit (a dialog?); stopped after run %d" % (i + 1))
        cpu = [s["cpu"] for s in ss]
        res["runs"].append({"window_s": up, "cpu_mean": statistics.mean(cpu), "cpu_max": max(cpu),
                            "rss_mb": statistics.median(s["rss_mb"] for s in ss), "footprint_mb": mem, "threads": max(s["threads"] for s in ss),
                            "procs": max(s["procs"] for s in ss), "names": ss[-1]["names"]})
        log("  %s run %d: window %s s, idle %.1f %% CPU, %.0f MB" % (label, i + 1, "-" if up is None else "%.1f" % up,
                                                                   res["runs"][-1]["cpu_mean"], mem))
        time.sleep(3)
    return res


def report(res):
    rows = []
    for r in res:
        if "error" in r:
            rows.append((r["app"], "FAILED: " + r["error"]) + ("",) * 6)
            continue
        R = r["runs"]
        first, warm = R[0]["window_s"], [x["window_s"] for x in R[1:]]
        rows.append((r["app"], "-" if first is None else "%.1f" % first, "%.1f" % statistics.median(warm) if warm else "-",
                     "%.1f / %.0f" % (statistics.median(x["cpu_mean"] for x in R), max(x["cpu_max"] for x in R)),
                     "%.0f" % statistics.median(x["footprint_mb"] for x in R), "%d" % max(x["threads"] for x in R),
                     "%d" % max(x["procs"] for x in R), "%.0f" % r["bundle_mb"]))
    L = ["# App bench (outside view: ps, footprint, du, open)", "",
         "Generated by `tools/re-survey/app_bench.py`. Window = `open` to the first window-server window "
         "(first run, then the median of warm runs). Idle CPU over the app's process tree, 100 % = one core.", "",
         md_table(["App", "Window s (1st)", "Window s (warm)", "Idle CPU % mean / max", "Memory MB (footprint)", "Threads",
                   "Processes", "Bundle MB"], rows), ""]
    for r in res:
        if "error" not in r:
            L.append("- %s processes: %s" % (r["app"], ", ".join(r["runs"][-1]["names"])))
    return "\n".join(L) + "\n"


def self_test():
    t = SelfTest("app_bench")
    tab = {1: (0, 0.0, 10, "launchd"), 10: (1, 5.0, 2048, "/A.app/Contents/MacOS/A"), 11: (10, 1.5, 1024, "helper"),
           12: (11, 0.5, 512, "grandchild"), 20: (1, 9.0, 99, "other")}
    t.check(sorted(tree(10, tab)) == [10, 11, 12], "process tree follows children, not siblings")
    me = sample("/nonexistent.app", os.getpid())
    t.check(me["procs"] >= 1 and me["rss_mb"] > 1 and me["threads"] >= 1, "samples this process (%s)" % {k: me[k] for k in ("procs", "rss_mb", "threads")})
    t.check(bundle_mb("/System/Applications/Calculator.app") > 0.1, "bundle size of Calculator")
    return t.done()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--self-test", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    rp = sub.add_parser("run")
    rp.add_argument("apps", nargs="+")
    rp.add_argument("--runs", type=int, default=3)
    rp.add_argument("--idle", type=int, default=20)
    rp.add_argument("--out")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if a.cmd != "run":
        ap.print_usage(sys.stderr)
        return 2
    try:
        for x in a.apps:
            ui_map.find_app(os.path.expanduser(x))
    except ValueError as e:
        print("usage: %s" % e, file=sys.stderr)
        return 2
    log = lambda m: print(m, file=sys.stderr)
    res = []
    for x in a.apps:
        try:
            res.append(bench_app(x, a.runs, a.idle, log))
        except Exception as e:  # one app must not stop the rest
            res.append({"app": x, "error": str(e)})
            log("FAILED %s: %s" % (x, e))
    out_dir = ensure_dir(os.path.join(RE_OUTPUT, "app-bench", time.strftime("%Y%m%d-%H%M%S")))
    with open(os.path.join(out_dir, "results.json"), "w") as f:
        json.dump(res, f, indent=1)
    write_out(report(res), a.out)
    return 0 if all("error" not in r for r in res) else 1


if __name__ == "__main__":
    sys.exit(main())
