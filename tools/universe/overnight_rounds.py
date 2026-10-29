#!/usr/bin/env python3
"""Overnight sound-model rounds (STATUS-2026-10-29.md section 4): for each engine, the model picks 2,000 renders where its seeds
disagree (propose.py), Infinite renders them (own_sweeps.py --queue), and P3 rescores the group split (p3_characteristics.py).

    overnight_rounds.py [--engines Analog,Oscillator,...] [--rounds 3] [--n 2000] [--dry]      (system python; children use ml-venv)

Per engine: a P3 score first (the baseline), then up to --rounds rounds named o<date>a, o<date>b, ...; it stops early when two rounds
in a row gain < 0.02 in the group head median (the Step 4 stop rule). An engine with under 1,500 presets gets a plain 2,000-preset
sweep first so the seeds have something to disagree about. One job at a time; ~/STOP_OWN stops after the current step.
Log: ~/Infinite/training-data/models/overnight/<date>.log; summary (one line per engine and round): same folder, <date>.jsonl."""
import argparse, json, os, re, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
PY = os.path.expanduser("~/Infinite/ml-venv/bin/python3")
OUT = os.path.expanduser("~/Infinite/training-data/models/overnight")
STOP = os.path.expanduser("~/STOP_OWN")
MAIN = ["Analog", "Oscillator", "FM", "Metallic", "String", "Wavetable", "Sampler"]
GROUP = re.compile(r"group: (\d+) / (\d+) characteristic means at the bar; median R2 of means: head (-?[\d.]+)")


def run(cmd, log, dry):
    line = " ".join(cmd)
    log.write("$ %s\n" % line); log.flush()
    if dry:
        return ""
    p = subprocess.run(["nice", "-n", "10"] + cmd, cwd=HERE, capture_output=True, text=True)
    log.write(p.stdout[-6000:] + p.stderr[-3000:] + "\n"); log.flush()
    if p.returncode != 0:
        raise RuntimeError("%s failed (%d)" % (cmd[1], p.returncode))
    return p.stdout


def presets(node):
    path = os.path.expanduser("~/Infinite/training-data/audio/own_%s.jsonl" % node.lower().replace(" ", "_"))
    if not os.path.exists(path):
        return 0
    return len({json.loads(ln)["preset_id"] for ln in open(path)})


def p3(node, log, dry):
    out = run([PY, "-I", "p3_characteristics.py", os.path.expanduser("~/Infinite/training-data/audio/own_%s.jsonl" % node.lower()),
               "--out", os.path.join(OUT, "p3_%s" % node.lower())], log, dry)
    m = GROUP.search(out)
    return (int(m.group(1)), float(m.group(3))) if m else (None, None)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--engines", default=",".join(MAIN)); ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--n", type=int, default=2000); ap.add_argument("--dry", action="store_true"); a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True); day = time.strftime("%Y%m%d")
    log = open(os.path.join(OUT, day + ".log"), "a"); summ = open(os.path.join(OUT, day + ".jsonl"), "a")
    def note(rec):
        rec["t"] = time.strftime("%H:%M:%S"); summ.write(json.dumps(rec) + "\n"); summ.flush(); print(rec, flush=True)
    for node in a.engines.split(","):
        try:
            if os.path.exists(STOP):
                note({"stop": "STOP_OWN"}); break
            if presets(node) < 1500:
                run([PY, "-I", "own_sweeps.py", "run", "--node", node, "--count", "2000", "--shards", "3"], log, a.dry)
            n_bar, head = p3(node, log, a.dry); note({"engine": node, "round": "base", "presets": presets(node), "at_bar": n_bar, "head": head})
            gains = []
            for r in range(a.rounds):
                if os.path.exists(STOP):
                    break
                name = "o%s%s" % (day, "abcdefgh"[r])
                run([PY, "-I", "propose.py", "--node", node, "--round", name, "--n", str(a.n), "--mid"], log, a.dry)
                q = os.path.expanduser("~/Infinite/training-data/own/queue_%s_%s.json" % (node.lower(), name))
                run([PY, "-I", "own_sweeps.py", "run", "--node", node, "--queue", q, "--shards", "3"], log, a.dry)
                run([PY, "-I", "round_score.py", "--node", node, "--round", name], log, a.dry)
                n2, h2 = p3(node, log, a.dry)
                gain = (h2 - head) if (h2 is not None and head is not None) else None
                note({"engine": node, "round": name, "presets": presets(node), "at_bar": n2, "head": h2, "gain": gain})
                head = h2; gains.append(gain if gain is not None else 0.0)
                if len(gains) >= 2 and gains[-1] < 0.02 and gains[-2] < 0.02:
                    note({"engine": node, "stopped": "two rounds < 0.02"}); break
        except Exception as ex:  # one engine failing never stops the night
            note({"engine": node, "error": repr(ex)[:300]})
    note({"done": True})


if __name__ == "__main__":
    sys.exit(main())
