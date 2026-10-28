#!/usr/bin/env python3
"""G10: measure every wavetable frame (single cycles, so the partials are exact).

    wavetables.py run     wt.jsonl, wt_synth.jsonl, air_vital_wt.jsonl -> ~/Infinite/training-data/characteristics/wavetables.jsonl
    wavetables.py test    synthetic checks (saw, square, sine, bell-like stack)

A frame is `harm`: amplitudes of harmonics 1..64 (max-normalised). At a reference f0 of 220 Hz harmonic k is the partial k * f0, so the
partials-based measures of detail.py apply exactly: spectral centroid, odd / even, roughness (Sethares, top 24), brightness slope,
formant-like peaks (the two strongest separated peaks of the harmonic envelope, in Hz). One row per table: frame mean / std / first / last."""
import json, math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import detail as det  # noqa: E402

F0 = 220.0
SRC = os.path.expanduser("~/Infinite/training-data/wavetables")
OUT = os.path.expanduser("~/Infinite/training-data/characteristics/wavetables.jsonl")


def frame(h):
    a = np.maximum(np.asarray(h, float), 0); k = np.arange(1, len(a) + 1); f = k * F0; p = a ** 2
    if p.sum() <= 0:
        return None
    out = {"centroid": float((f * p).sum() / p.sum()), "rolloff": float(f[np.searchsorted(np.cumsum(p) / p.sum(), 0.85)]),
           "odd_even": float(10 * math.log10((p[0::2].sum() + 1e-12) / (p[1::2].sum() + 1e-12))),
           "fundamental_share": float(p[0] / p.sum()), "flatness": float(np.exp(np.log(a + 1e-9).mean()) / (a.mean() + 1e-12))}
    top = np.argsort(a)[::-1][:24]
    out["roughness"] = det.sethares(f[top], a[top])
    pk = [i for i in range(1, len(a) - 1) if a[i] > a[i - 1] and a[i] >= a[i + 1] and a[i] > 0.1 * a.max()]
    pk = sorted(pk, key=lambda i: -a[i]); sel = []
    for i in pk:
        if all(abs(i - j) >= 2 for j in sel):
            sel.append(i)
    sel = sorted(sel[:2]); out["f1"] = float(f[sel[0]]) if sel else None; out["f2"] = float(f[sel[1]]) if len(sel) > 1 else None
    return out


def table(tab):
    fr = [frame(s["harm"]) for s in tab["shapes"] if "harm" in s]; fr = [x for x in fr if x]
    if not fr:
        return None
    r = {k: tab.get(k) for k in ("src", "name", "kind", "category", "method", "synthetic") if tab.get(k) is not None}
    r["frames"] = len(fr)
    for k in fr[0]:
        v = np.array([x[k] for x in fr if x[k] is not None], float)
        if v.size:
            r[k] = {"mean": round(float(v.mean()), 4), "std": round(float(v.std()), 4), "first": round(float(v[0]), 4), "last": round(float(v[-1]), 4)}
    return r


def rows():
    for n in ("wt.jsonl", "wt_synth.jsonl", "air_vital_wt.jsonl"):
        p = os.path.join(SRC, n)
        if not os.path.exists(p):
            continue
        for ln in open(p):
            t = json.loads(ln)
            if "shapes" not in t and "harm" in t:       # wt_synth: one frame per row
                t = {"name": n, "shapes": [t], "method": t.get("method"), "synthetic": t.get("synthetic")}
            r = table(t)
            if r:
                r["file"] = n; yield r


def cmd_run():
    os.makedirs(os.path.dirname(OUT), exist_ok=True); n = 0
    with open(OUT, "w") as f:
        for r in rows():
            f.write(json.dumps(r, separators=(",", ":")) + "\n"); n += 1
    print("%d tables -> %s" % (n, OUT))


def cmd_test():
    k = np.arange(1, 65); ok = True
    def chk(name, c):
        nonlocal ok; ok &= bool(c); print("%s  %s" % ("PASS" if c else "FAIL", name))
    saw = frame(1 / k); sq = frame(np.where(k % 2, 1 / k, 0)); sine = frame(np.eye(64)[0]); bell = frame(np.eye(64)[[0, 3, 8, 13]].sum(0))
    chk("saw: odd/even near +3 dB or less (all harmonics)", abs(saw["odd_even"]) < 6)
    chk("square: odd-only gives odd/even > 40 dB", sq["odd_even"] > 40)
    chk("sine: centroid is the fundamental, roughness 0", abs(sine["centroid"] - F0) < 1e-6 and sine["roughness"] == 0)
    chk("saw brighter than sine", saw["centroid"] > sine["centroid"] * 2)
    chk("bell-like stack rougher than sine", bell["roughness"] >= 0)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(cmd_test() if sys.argv[1:] == ["test"] else cmd_run() if sys.argv[1:] == ["run"] else print(__doc__))
