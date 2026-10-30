#!/usr/bin/env python3
"""Pitch, level and speaking check of the Wind node (PLAN-2026-10-30 item 9), from its own renders.
    wind_tune.py [--breaths 0.3,0.6,0.9] [--set key=value,...]
Per instrument and breath: a held note every 5 semitones across the instrument's range, rendered headless
(Keyboard -> Wind -> Audio Out). Per note, from the held part (0.4-1.2 s after the onset): f0 by YIN, error in cents
from the note, RMS dB, and whether it spoke at all (RMS above -60 dB and a clear period). Prints a table per instrument
and the median cents error, which is what the node's loop constants are trimmed by. Writes
~/Infinite/training-data/eval/wind_tune.json."""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
import soundfile as sf
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as osw  # noqa: E402

SR = 48000
INSTR = ["clarinet", "sax", "flute", "brass"]
RANGE = {"clarinet": (50, 86), "sax": (49, 81), "flute": (60, 96), "brass": (40, 82)}
WIN, HOLD, LEAD = 2.0, 1.4, 0.3


def yin(x, fmin=50.0, fmax=2500.0, thr=0.15):
    lo, hi = int(SR / fmax), int(SR / fmin)
    n = len(x) - hi
    if n < hi:
        return 0.0, 1.0
    d = np.array([np.sum((x[:n] - x[t:t + n]) ** 2) for t in range(hi + 1)])
    cm = d[1:] * np.arange(1, hi + 1) / (np.cumsum(d[1:]) + 1e-20)
    cm = np.concatenate([[1.0], cm])
    cand = np.flatnonzero(cm[lo:] < thr)
    t = lo + (cand[0] if len(cand) else int(np.argmin(cm[lo:])))
    while t + 1 <= hi and cm[t + 1] < cm[t]:
        t += 1
    a, b, c = cm[t - 1], cm[t], cm[min(t + 1, hi)]
    den = a - 2 * b + c
    tt = t + (0.5 * (a - c) / den if den != 0 else 0.0)
    return SR / tt, float(b)


def render(binp, inst, over, notes, tmp):
    rec = dict({"instrument": INSTR.index(inst)}, **over)
    L = ["infinite-patch 1", "node 1 Notes Keyboard", "  id keys", "end", "node 2 Synths Wind", "  id syn"]
    L += ["  %s %s %s" % ("i" if k == "instrument" else "f", k, int(v) if k == "instrument" else repr(float(v))) for k, v in rec.items()]
    L += ["end", "node 3 Utility Audio Out", "  id out", "end", "aud out audio syn", "note syn 0 keys"]
    inf, js, wav = (os.path.join(tmp, "w." + e) for e in ("inf", "json", "wav"))
    open(inf, "w").write("\n".join(L) + "\n")
    types = {"p%d" % i: {"node": "syn", "pitch": m, "velocity_from": "v", "length_from": "len"} for i, m in enumerate(notes)}
    evs = [{"t": LEAD + i * WIN, "type": "p%d" % i, "v": 0.8, "len": HOLD} for i in range(len(notes))]
    ep, mp_ = os.path.join(tmp, "e.json"), os.path.join(tmp, "m.json")
    json.dump(evs, open(ep, "w")); json.dump(types, open(mp_, "w"))
    subprocess.run([binp, "--audio-summary", inf, js, "--duration", "%g" % (LEAD + WIN * len(notes)), "--sample-rate", str(SR),
                    "--wav", wav, "--lenient", "--notes", ep, "--note-map", mp_], capture_output=True, text=True, timeout=300)
    x, _ = sf.read(wav, dtype="float64", always_2d=True)
    return x.mean(1)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--breaths", default="0.3,0.6,0.9"); ap.add_argument("--set", default="")
    ap.add_argument("--instruments", default=",".join(INSTR)); a = ap.parse_args()
    over = {k: float(v) for k, v in (kv.split("=") for kv in a.set.split(",") if kv)}
    binp = osw.binary(); res = {}
    for inst in a.instruments.split(","):
        notes = list(range(RANGE[inst][0], RANGE[inst][1] + 1, 5))
        for b in (float(s) for s in a.breaths.split(",")):
            with tempfile.TemporaryDirectory() as tmp:
                x = render(binp, inst, dict(over, breath=b), notes, tmp)
            rows = []
            for i, m in enumerate(notes):
                s = int((LEAD + i * WIN + 0.4) * SR); seg = x[s:s + int(0.8 * SR)]
                rms = 10 * np.log10(np.mean(seg ** 2) + 1e-20)
                f0, ap_ = yin(seg[:int(0.25 * SR)])
                cents = 1200 * np.log2(f0 / (440 * 2 ** ((m - 69) / 12))) if f0 > 0 else float("nan")
                spoke = bool(rms > -60 and ap_ < 0.3)
                rows.append({"note": m, "f0": round(f0, 2), "cents": round(float(cents), 1), "rms_db": round(float(rms), 1),
                             "aperiodic": round(ap_, 3), "spoke": spoke})
            ok = [r["cents"] for r in rows if r["spoke"]]
            med = float(np.median(ok)) if ok else float("nan")
            res["%s@%.1f" % (inst, b)] = {"rows": rows, "median_cents": med, "spoke": len(ok), "notes": len(rows),
                                          "rms_db": float(np.median([r["rms_db"] for r in rows if r["spoke"]])) if ok else -120.0,
                                          "peak": float(np.max(np.abs(x)))}
            print("%-9s breath %.1f: spoke %d/%d, median %+.0f c, cents %s, rms %s, peak %.2f" % (
                inst, b, len(ok), len(rows), med, [r["cents"] if r["spoke"] else None for r in rows],
                [r["rms_db"] for r in rows], float(np.max(np.abs(x)))), flush=True)
    out = os.path.expanduser("~/Infinite/training-data/eval/wind_tune.json")
    json.dump(res, open(out, "w"), indent=1); print("->", out)


if __name__ == "__main__":
    sys.exit(main())
