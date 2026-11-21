#!/usr/bin/env python3
"""Re-runs the H2 rig once per independently measured cost model, for a confidence interval on the saving.

What: reads every INFINITE_FIELDRATE_COST=... line in a log of repeated runs of tools/field_rate_cost_bench.cpp,
      runs the Infinite binary with INFINITE_FIELDRATEPLAN=1 under each, and writes $OUT/phase2-cost-repeats.csv
      (one row per run: the cost line, aggregate and median saving, and whether the tanh x3 plan beats fixed 8x)
      plus each H2 log as $OUT/phase2-cost-repeats/run-<i>.log. The bench log is copied next to it unchanged.
Why:  the verdict run used one cost line. A single microbenchmark is a sample, so the saving needs a spread.
Usage: OUT=data-rerun python3 scripts/cost_repeats.py <bench-repeats.log>
       (make the log with: for i in $(seq 10); do /tmp/rate_cost_bench; sleep 15; done > bench-repeats.log)
"""
import csv, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def binary():
    # _bin.sh finds the repo from $0, so pass this script's path as bash's $0
    out = subprocess.run(["bash", "-c", f'source "{HERE}/_bin.sh" && echo "$BIN"', os.path.abspath(__file__)],
                         capture_output=True, text=True)
    if out.returncode:
        sys.exit(out.stderr.strip())
    return out.stdout.strip()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    log = sys.argv[1]
    costs = re.findall(r"^INFINITE_FIELDRATE_COST=([\d.,]+)$", open(log, encoding="utf-8").read(), re.M)
    if not costs:
        sys.exit("no INFINITE_FIELDRATE_COST lines in " + log)
    out = os.environ.get("OUT") or os.path.join(HERE, "..", "data-rerun")
    runs = os.path.join(out, "phase2-cost-repeats")
    os.makedirs(runs, exist_ok=True)
    shutil.copyfile(log, os.path.join(out, "phase2-cost-bench.log"))
    b = binary()
    rows = []
    for i, c in enumerate(costs, 1):
        env = dict(os.environ, INFINITE_FIELDRATEPLAN="1", INFINITE_FIELDRATE_COST=c)
        r = subprocess.run([b], env=env, capture_output=True, text=True)
        text = r.stdout + r.stderr
        open(os.path.join(runs, f"run-{i:02d}.log"), "w", encoding="utf-8").write(text)
        m = re.search(r"H2 aggregate saving ([\d.]+)%.*?median per kernel ([\d.]+)%", text)
        ch = re.search(r"tanh x3 vs fixed 8x:.*?staged (does not beat|beats) 8x", text)
        if not m or not ch:
            sys.exit(f"run {i}: no H2 summary line (see {runs}/run-{i:02d}.log)")
        v = [float(x) for x in c.split(",")]
        rows.append({"run": i, "cost_line": c, "per_run": v[0], "per_instr": v[1],
                     "up": "/".join(f"{x:g}" for x in v[2:5]), "down": "/".join(f"{x:g}" for x in v[5:8]),
                     "aggregate_pct": m.group(1), "median_pct": m.group(2),
                     "chain_beats_8x": "yes" if ch.group(1) == "beats" else "no"})
        print(f"run {i}: {c}  aggregate {m.group(1)}%  median {m.group(2)}%  tanh x3 beats 8x: {rows[-1]['chain_beats_8x']}")
    with open(os.path.join(out, "phase2-cost-repeats.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader(); w.writerows(rows)
    print("wrote", os.path.join(out, "phase2-cost-repeats.csv"))


if __name__ == "__main__":
    main()
