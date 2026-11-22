#!/usr/bin/env python3
"""Finds the resampler cost at which per-segment rate plans would clear H2's 25% bar (phase 4, descriptive).

What: re-runs the H2 rig (INFINITE_FIELDRATEPLAN=1) with every up/down-sampler cost in the verdict cost line scaled
      by s, keeping the per-run and per-instruction costs, first at s = 0 (free crossings: the most staging can
      ever save on this corpus) and 1 (the verdict), then bisects s for an aggregate saving of 25%.
      Writes $OUT/phase4-break-even.csv (s, cost line, aggregate, median, tanh x3 beats 8x, 2x pair cost in
      interpreter instructions) and each run's log under $OUT/phase4-break-even/.
Why:  the H2 kill holds for one cost model; the break-even turns it into a condition that transfers to other
      machines, resamplers or a compiled backend ("pays once a 2x up+down pair costs under N instructions").
Usage: OUT=data-rerun python3 scripts/break_even.py [verdict cost line]
"""
import csv, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BAR = 25.0


def binary():
    out = subprocess.run(["bash", "-c", f'source "{HERE}/_bin.sh" && echo "$BIN"', os.path.abspath(__file__)],
                         capture_output=True, text=True)
    if out.returncode:
        sys.exit(out.stderr.strip())
    return out.stdout.strip()


def main():
    base = [float(x) for x in (sys.argv[1] if len(sys.argv) > 1 else "0,3.5,11,23,47,10,23,49").split(",")]
    out = os.environ.get("OUT") or os.path.join(HERE, "..", "data-rerun")
    logs = os.path.join(out, "phase4-break-even")
    os.makedirs(logs, exist_ok=True)
    b = binary()
    rows = []

    def run(s):
        c = base[:2] + [round(v * s, 4) for v in base[2:]]
        line = ",".join(f"{v:g}" for v in c)
        env = dict(os.environ, INFINITE_FIELDRATEPLAN="1", INFINITE_FIELDRATE_COST=line)
        r = subprocess.run([b], env=env, capture_output=True, text=True)
        text = r.stdout + r.stderr
        open(os.path.join(logs, f"s-{s:.4f}.log"), "w", encoding="utf-8").write(text)
        m = re.search(r"H2 aggregate saving ([\d.]+)%.*?median per kernel ([\d.]+)%", text)
        ch = re.search(r"tanh x3 vs fixed 8x:.*?staged (does not beat|beats) 8x", text)
        if not m or not ch:
            sys.exit(f"s={s}: no H2 summary line (see {logs})")
        pair = (c[2] + c[5]) / c[1]
        row = {"s": f"{s:.4f}", "cost_line": line, "aggregate_pct": m.group(1), "median_pct": m.group(2),
               "chain_beats_8x": "yes" if ch.group(1) == "beats" else "no", "pair2x_in_instructions": f"{pair:.2f}"}
        rows.append(row)
        print(f"s={s:.4f} ({line})  aggregate {m.group(1)}%  median {m.group(2)}%  tanh x3 beats 8x: "
              f"{row['chain_beats_8x']}  2x pair = {pair:.2f} instr", flush=True)
        return float(m.group(1))

    top, verdict = run(0.0), run(1.0)
    if top < BAR:
        print(f"even free crossings save only {top}%: no resampler cost clears the bar on this corpus")
    elif verdict < BAR:
        lo, hi = 0.0, 1.0   # saving(lo) >= BAR > saving(hi)
        for _ in range(6):
            mid = (lo + hi) / 2
            if run(mid) >= BAR:
                lo = mid
            else:
                hi = mid
        print(f"break-even s between {lo:.4f} and {hi:.4f}")
    rows.sort(key=lambda r: float(r["s"]))
    with open(os.path.join(out, "phase4-break-even.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader(); w.writerows(rows)
    print("wrote", os.path.join(out, "phase4-break-even.csv"))


if __name__ == "__main__":
    main()
