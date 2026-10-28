#!/usr/bin/env python3
"""Step 4 loop (system python): rounds of propose -> render -> score -> P3 per engine until two rounds in a row gain < 0.02 on the group-split
head median (or --max-rounds). One heavy job at a time; stop file ~/STOP_OWN. Log lines are the record for HANDOFF.md.
    step4_loop.py --engines Analog,Oscillator --first 2 --max-rounds 6"""
import argparse, os, re, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); PY = os.path.expanduser("~/Infinite/ml-venv/bin/python"); AUD = os.path.expanduser("~/Infinite/training-data/audio")
Q = os.path.expanduser("~/Infinite/training-data/own"); OUT = os.path.expanduser("~/Infinite/training-data/own/loop")


def sh(cmd, log):
    with open(log, "w") as f:
        subprocess.run(cmd, cwd=HERE, stdout=f, stderr=subprocess.STDOUT)
    return open(log).read()


def group_head(node):
    t = sh([PY, "p3_characteristics.py", os.path.join(AUD, "own_%s.jsonl" % node.lower())], os.path.join(OUT, "p3_%s.txt" % node))
    m = re.search(r"group: (\d+) / 11.*?head ([0-9.]+)", t)
    r = re.search(r"random: (\d+) / 11.*?head ([0-9.]+)", t)
    return (int(m.group(1)), float(m.group(2)), int(r.group(1)), float(r.group(2))) if m and r else None


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--engines", required=True); ap.add_argument("--first", type=int, default=2)
    ap.add_argument("--max-rounds", type=int, default=6); ap.add_argument("--baseline", default=""); a = ap.parse_args(); os.makedirs(OUT, exist_ok=True)
    base = {kv.split("=")[0]: float(kv.split("=")[1]) for kv in a.baseline.split(",") if kv}
    for node in a.engines.split(","):
        prev = base.get(node); small = 0
        if prev is None:
            g = group_head(node); prev = g[1] if g else 0.0
        for n in range(a.first, a.first + a.max_rounds):
            if os.path.exists(os.path.expanduser("~/STOP_OWN")): return
            rnd = "r%02d" % n; q = os.path.join(Q, "queue_%s_%s.json" % (node.lower(), rnd))
            sh([PY, "propose.py", "--node", node, "--round", rnd] + (["--mid"] if n >= 2 else []), os.path.join(OUT, "prop_%s_%s.txt" % (node, rnd)))
            sh([sys.executable, "own_sweeps.py", "run", "--node", node, "--queue", q, "--shards", "4"], os.path.join(OUT, "run_%s_%s.txt" % (node, rnd)))
            score = sh([PY, "round_score.py", "--node", node, "--round", rnd], os.path.join(OUT, "score_%s_%s.txt" % (node, rnd)))
            sh([PY, "round_score.py", "--node", node, "--round", rnd, "--by-knobs"], os.path.join(OUT, "knobs_%s_%s.txt" % (node, rnd)))
            g = group_head(node)
            if not g: print("%s %s: P3 failed" % (node, rnd), flush=True); break
            gain = g[1] - prev; small = small + 1 if gain < 0.02 else 0
            print("[%s] %s %s | group %d/11 head %.3f (gain %+.3f) | random %d/11 head %.3f | small-gain rounds %d" %
                  (time.strftime("%H:%M:%S"), node, rnd, g[0], g[1], gain, g[2], g[3], small), flush=True)
            prev = g[1]
            if small >= 2: break
    print("[%s] LOOP DONE" % time.strftime("%H:%M:%S"), flush=True)


if __name__ == "__main__":
    main()
