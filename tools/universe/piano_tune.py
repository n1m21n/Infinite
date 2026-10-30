#!/usr/bin/env python3
"""Pitch, level and sustain check of the Piano node (PLAN-2026-10-30 item 10), from its own renders.
    piano_tune.py [--vels 0.3,0.8] [--set key=value,...] [--notes 21,28,...]
Per velocity: one struck note every 7 keys from A0 to C8, rendered headless (Keyboard -> Piano -> Audio Out). Per note:
f0 by YIN 0.1-0.35 s after the onset and its error in cents, RMS of the first second, peak, and whether the note rang at
all. Prints a table and the median cents error; writes ~/Infinite/training-data/eval/piano_tune.json. The Python twin of
the voice is piano_proto.py."""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
import soundfile as sf
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as osw  # noqa: E402
from wind_tune import yin  # noqa: E402

SR = 48000
WIN, HOLD, LEAD = 2.0, 1.5, 0.3
NOTES = list(range(21, 109, 7))


def render(binp, over, notes, vel, tmp):
    L = ["infinite-patch 1", "node 1 Notes Keyboard", "  id keys", "end", "node 2 Synths Piano", "  id syn"]
    L += ["  f %s %r" % (k, float(v)) for k, v in over.items()]
    L += ["end", "node 3 Utility Audio Out", "  id out", "end", "aud out audio syn", "note syn 0 keys"]
    inf, js, wav = (os.path.join(tmp, "p." + e) for e in ("inf", "json", "wav"))
    open(inf, "w").write("\n".join(L) + "\n")
    types = {"p%d" % i: {"node": "syn", "pitch": m, "velocity_from": "v", "length_from": "len"} for i, m in enumerate(notes)}
    evs = [{"t": LEAD + i * WIN, "type": "p%d" % i, "v": vel, "len": HOLD} for i in range(len(notes))]
    ep, mp_ = os.path.join(tmp, "e.json"), os.path.join(tmp, "m.json")
    json.dump(evs, open(ep, "w")); json.dump(types, open(mp_, "w"))
    subprocess.run([binp, "--audio-summary", inf, js, "--duration", "%g" % (LEAD + WIN * len(notes)), "--sample-rate", str(SR),
                    "--wav", wav, "--lenient", "--notes", ep, "--note-map", mp_], capture_output=True, text=True, timeout=300)
    x, _ = sf.read(wav, dtype="float64", always_2d=True)
    return x.mean(1)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--vels", default="0.3,0.8"); ap.add_argument("--set", default="")
    ap.add_argument("--notes", default=""); a = ap.parse_args()
    over = {k: float(v) for k, v in (kv.split("=") for kv in a.set.split(",") if kv)}
    notes = [int(s) for s in a.notes.split(",")] if a.notes else NOTES
    binp = osw.binary(); res = {}
    for vel in (float(s) for s in a.vels.split(",")):
        with tempfile.TemporaryDirectory() as tmp:
            x = render(binp, over, notes, vel, tmp)
        rows = []
        for i, m in enumerate(notes):
            s = int((LEAD + i * WIN) * SR)
            rms = 10 * np.log10(np.mean(x[s:s + SR] ** 2) + 1e-20)
            f0, ap_ = yin(x[s + int(0.1 * SR):s + int(0.35 * SR)], fmin=25.0, fmax=4500.0)
            cents = 1200 * np.log2(f0 / (440 * 2 ** ((m - 69) / 12))) if f0 > 0 else float("nan")
            rang = bool(rms > -60 and ap_ < 0.3)
            rows.append({"note": m, "cents": round(float(cents), 1), "rms_db": round(float(rms), 1), "aperiodic": round(ap_, 3),
                         "peak": round(float(np.max(np.abs(x[s:s + SR]))), 3), "rang": rang})
        ok = [r["cents"] for r in rows if r["rang"]]
        med = float(np.median(ok)) if ok else float("nan")
        res["vel%.1f" % vel] = {"rows": rows, "median_cents": med, "rang": len(ok), "notes": len(rows), "peak": float(np.max(np.abs(x)))}
        print("vel %.1f: rang %d/%d, median %+.1f c, worst %+.1f c, peak %.2f" % (
            vel, len(ok), len(rows), med, max(ok, key=abs) if ok else float("nan"), float(np.max(np.abs(x)))))
        for r in rows:
            print("  %3d  %+7.1f c  rms %6.1f dB  peak %.3f  ap %.2f%s" % (
                r["note"], r["cents"], r["rms_db"], r["peak"], r["aperiodic"], "" if r["rang"] else "  SILENT"), flush=True)
    out = os.path.expanduser("~/Infinite/training-data/eval/piano_tune.json")
    json.dump(res, open(out, "w"), indent=1); print("->", out)


if __name__ == "__main__":
    sys.exit(main())
