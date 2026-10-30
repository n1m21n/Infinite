#!/usr/bin/env python3
"""Python twin of WindNode's Tick (src/nodes/WindNode.cpp), for tuning its constants without a rebuild (PLAN item 9).
    wind_proto.py --inst flute [--breath 0.6] [--notes 60,72,84] [--k key=value,...]
Runs one held note per pitch through the same loop (numba), measures f0 (wind_tune.yin) and RMS like wind_tune.py.
Keys in --k override the constants in DEF (pressure, loop, reed, jet, lip...). Keep DEF in step with the C++."""
import argparse, math, os, sys
import numpy as np
import numba
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
SR = 48000
INSTR = ["clarinet", "sax", "flute", "brass"]
DEF = {"clarinet": dict(pmin=0.6, prange=0.4, loop=0.5, off=0.7, slope=-0.3, refl=0.95),
       "sax": dict(pmin=0.68, prange=0.22, loop=1.0, off=0.7, slope=0.3, refl=0.95, pos=0.4),
       "flute": dict(pmin=0.88, prange=0.32, loop=1.9965, jet=0.32, jrefl=0.5, erefl=0.5),
       "brass": dict(pmin=0.4, prange=0.45, loop=2.0, lipq=25.0, lipdc=3.0, lipf=0.975, bref=0.85, mouth=0.3)}


@numba.njit(cache=True)
def run(inst, f, p_target, pole, n, kv, seed):
    N = 1 << 16
    bore = np.zeros(N); aux = np.zeros(N); out = np.zeros(n)
    P = SR / f; w = 2 * math.pi * f / SR
    pmin, prange, loop, a1, a2, a3, a4 = kv[0], kv[1], kv[2], kv[3], kv[4], kv[5], kv[6]
    lossd = 0.0 if inst == 3 else math.atan2(pole * math.sin(w), 1 - pole * math.cos(w)) / w
    L = max(4.0, loop * P - lossd)
    if inst == 1:
        d1 = max(2.0, L * a4); d0 = max(2.0, L - d1)
    elif inst == 2:
        d1 = max(2.0, L * a1 / (1 + a1)); d0 = max(2.0, L - d1)
    else:
        d0 = L; d1 = 2.0
    wl = w * kv[7]
    lr = math.exp(-wl / (2 * a1)) if inst == 3 else 0.0
    lipA1 = -2 * lr * math.cos(wl); lipA2 = lr * lr; lipG = a2 * (1 + lipA1 + lipA2)
    lp = 0.0; dcI = 0.0; dcO = 0.0; R = math.exp(-2 * math.pi * 20 / SR); y1 = 0.0; y2 = 0.0
    env = 0.0; step = 1.0 / (0.03 * SR); st = seed
    for i in range(n):
        env = min(1.0, env + step)
        st = (st * 1664525 + 1013904223) % 4294967296
        nz = st / 2147483648.0 - 1.0
        p = env * p_target
        p += p * 0.2 * 0.15 * nz
        pos = i - d0; fl = math.floor(pos); fr = pos - fl; k = int(fl)
        bo = bore[k % N] + fr * (bore[(k + 1) % N] - bore[k % N]) if k >= 0 else 0.0
        if inst == 0:
            lp = (1 - pole) * bo + pole * lp
            diff = -a3 * lp - p
            r = min(1.0, max(-1.0, a1 + a2 * diff))
            o = p + diff * r
            bore[i % N] = o
        elif inst == 1:
            pos = i - d1; fl = math.floor(pos); fr = pos - fl; k = int(fl)
            ao = aux[k % N] + fr * (aux[(k + 1) % N] - aux[k % N]) if k >= 0 else 0.0
            lp = (1 - pole) * bo + pole * lp
            bell = -a3 * lp
            o = bell - ao
            diff = p - o
            r = min(1.0, max(-1.0, a1 + a2 * diff))
            aux[i % N] = bell
            bore[i % N] = p - diff * r - bell
        elif inst == 2:
            lp = (1 - pole) * bo + pole * lp
            back = -lp
            dc = back - dcI + R * dcO; dcI = back; dcO = dc; back = dcO
            pos = i - d1; fl = math.floor(pos); fr = pos - fl; k = int(fl)
            jo = aux[k % N] + fr * (aux[(k + 1) % N] - aux[k % N]) if k >= 0 else 0.0
            aux[i % N] = p - a2 * back
            jet = min(1.0, max(-1.0, jo * (jo * jo - 1)))
            bore[i % N] = jet + a3 * back
            o = 0.3 * bo
        else:
            mouth = a4 * p; bp = a3 * bo
            lip = lipG * (mouth - bp) - lipA1 * y1 - lipA2 * y2
            y2 = y1; y1 = lip
            op = min(1.0, lip * lip)
            x = op * mouth + (1 - op) * bp
            dc = x - dcI + R * dcO; dcI = x; dcO = dc
            bore[i % N] = dcO
            o = bo
        out[i] = o
    return out


def kvec(inst, d):
    if inst == "clarinet": return np.array([d["pmin"], d["prange"], d["loop"], d["off"], d["slope"], d["refl"], 0.0, 0.0])
    if inst == "sax": return np.array([d["pmin"], d["prange"], d["loop"], d["off"], d["slope"], d["refl"], d["pos"], 0.0])
    if inst == "flute": return np.array([d["pmin"], d["prange"], d["loop"], d["jet"], d["jrefl"], d["erefl"], 0.0, 0.0])
    return np.array([d["pmin"], d["prange"], d["loop"], d["lipq"], d["lipdc"], d["bref"], d["mouth"], d["lipf"]])


def measure(inst, notes, breath, d, tone=0.5, vel=0.8):
    import wind_tune as wt
    pole = (0.75 - 0.6 * tone) ** (44100 / SR); kv = kvec(inst, d); rows = []
    for m in notes:
        f = 440 * 2 ** ((m - 69) / 12)
        x = run(INSTR.index(inst), f, (d["pmin"] + d["prange"] * breath) * (0.7 + 0.3 * vel), pole, int(1.3 * SR), kv, 12345)
        seg = x[int(0.4 * SR):int(1.2 * SR)]
        seg = seg - seg.mean()
        rms = 10 * np.log10(np.mean(seg ** 2) + 1e-20) if np.isfinite(seg).all() else float("nan")
        f0, ap = wt.yin(seg[:int(0.25 * SR)]) if np.isfinite(seg).all() else (0.0, 1.0)
        c = 1200 * np.log2(f0 / f) if f0 > 0 else float("nan")
        rows.append((m, round(float(c), 1) if rms > -60 and ap < 0.3 else None, round(float(rms), 1)))
    return rows


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--inst", required=True); ap.add_argument("--breath", default="0.3,0.6,0.9")
    ap.add_argument("--notes", default=""); ap.add_argument("--k", default=""); a = ap.parse_args()
    import wind_tune as wt
    d = dict(DEF[a.inst]); d.update({k: float(v) for k, v in (kv.split("=") for kv in a.k.split(",") if kv)})
    lo, hi = wt.RANGE[a.inst]
    notes = [int(s) for s in a.notes.split(",")] if a.notes else list(range(lo, hi + 1, 5))
    for b in (float(s) for s in a.breath.split(",")):
        print("breath %.1f:" % b, measure(a.inst, notes, b, d), flush=True)


if __name__ == "__main__":
    main()
