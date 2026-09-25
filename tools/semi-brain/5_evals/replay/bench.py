#!/usr/bin/env python3
"""
bench.py
Speed side of the gate. Appends to history.jsonl next to the replay scorecards.

  python3 bench.py sync --label X [--cmd "..."]   wall time of one sync, run the way the hook
                                                  runs it (taskpolicy -b, nice 19)
  python3 bench.py sync --label X --observed S    record a sync timed elsewhere (seconds)
  python3 bench.py latency --label X              cold CLI (new process, real index) and warm
                                                  analyze_problem latency on the live index
"""

import argparse
import json
import shlex
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from gitdata import SEMI_BRAIN_DIR, REPO_PATH  # noqa: E402

HISTORY_FILE = HERE / "history.jsonl"
DEFAULT_SYNC = "python3 tools/semi-brain/4_engine/sync_brain.py --sync"


def head():
    return subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=REPO_PATH,
                          capture_output=True, text=True).stdout.strip()


def append(card):
    card = {"when": datetime.now(timezone.utc).isoformat(timespec="seconds"), "head": head(), **card}
    with open(HISTORY_FILE, "a") as f:
        f.write(json.dumps(card) + "\n")
    print(json.dumps(card, indent=1))


def queries(n):
    cases = json.loads((HERE / "cases.json").read_text())
    return [c["query_subject"] for c in cases[-n:]]


def bench_sync(args):
    if args.observed is not None:
        return append({"kind": "sync", "label": args.label, "wall_s": args.observed, "how": "observed"})
    cmd = ["taskpolicy", "-b", "nice", "-n", "19"] + shlex.split(args.cmd)
    t0 = time.time()
    proc = subprocess.run(cmd, cwd=REPO_PATH, capture_output=True, text=True)
    wall = time.time() - t0
    append({"kind": "sync", "label": args.label, "wall_s": round(wall, 2), "how": args.cmd,
            "rc": proc.returncode, "tail": proc.stdout.strip().splitlines()[-3:]})


def bench_latency(args):
    qs = queries(args.n)
    cold = []
    for q in qs:
        t0 = time.time()
        subprocess.run([sys.executable, str(SEMI_BRAIN_DIR / "4_engine" / "semi_brain_cli.py"), q],
                       cwd=REPO_PATH, capture_output=True)
        cold.append(time.time() - t0)
    sys.path.insert(0, str(SEMI_BRAIN_DIR / "4_engine"))
    import warnings
    warnings.filterwarnings("ignore")
    from brain_core import SemiBrainCognitiveEngine
    t0 = time.time()
    engine = SemiBrainCognitiveEngine()
    engine.analyze_problem("warm up")
    startup = time.time() - t0
    warm = []
    for q in qs:
        t0 = time.perf_counter()
        engine.analyze_problem(q)
        warm.append((time.perf_counter() - t0) * 1000)
    append({"kind": "latency", "label": args.label, "queries": len(qs),
            "cold_cli_s": {"median": round(statistics.median(cold), 2), "max": round(max(cold), 2)},
            "engine_startup_s": round(startup, 2),
            "warm_ms": {"median": round(statistics.median(warm), 1), "max": round(max(warm), 1)}})


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="what", required=True)
    s = sub.add_parser("sync")
    s.add_argument("--label", required=True)
    s.add_argument("--cmd", default=DEFAULT_SYNC)
    s.add_argument("--observed", type=float)
    l = sub.add_parser("latency")
    l.add_argument("--label", required=True)
    l.add_argument("--n", type=int, default=5)
    args = ap.parse_args()
    bench_sync(args) if args.what == "sync" else bench_latency(args)


if __name__ == "__main__":
    main()
