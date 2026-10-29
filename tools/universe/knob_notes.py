#!/usr/bin/env python3
"""IMPROVEMENT-PLAN 2.3: does a knob's direction learned at C3 hold at other notes and velocities? Held-out banks, as knob_direction.py.
    knob_notes.py [--engines Analog,...] [--folds 5]
own_sweeps stores mel and bands only for C3, C4 and C3f; every window (C3, C4, C3f, C1, C5, C6, C3pp, C3mf) has rms, partials and tail.
So this test uses three axes every window has: brightness (log2 of the partials' amplitude-weighted mean frequency over the note's f0),
loudness (rms in dB) and tail (release-tail steps above -60 dB). Reliable (knob, axis) cells are chosen from the training banks' one-knob
pairs at C3 (knob_direction.reliable's rule: >= 8 moves, >= 80% one way) and scored on the test banks' pairs in each window.
Bar (GOAL item 3): >= 90% per window. Writes feel/knob_notes.json."""
import argparse, json, math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, sound_data as sd
OUT = os.path.join(HERE, "../../docs/plans/data-universe/feel/knob_notes.json")
WIN = [lab for _, _, lab in o.STIM]; F0 = {lab: 440 * 2 ** ((m - 69) / 12) for m, _, lab in o.STIM}
AXES = ["brightness", "loudness", "tail"]
MAIN = ["Analog", "Oscillator", "FM", "Metallic", "String", "Wavetable", "Sampler"]

def axes(r):
    pa = json.loads(r["partials"]) if isinstance(r.get("partials"), str) else r.get("partials") or []
    rms = float(r.get("rms") or 0)
    if not pa or rms <= 1e-5: return None
    f = np.array([p[0] for p in pa], float); a = 10 ** (np.array([p[1] for p in pa], float) / 20)
    tail = json.loads(r["tail"]) if isinstance(r.get("tail"), str) else r.get("tail") or []
    return np.array([math.log2(max((f * a).sum() / max(a.sum(), 1e-9), 1) / F0[r["note"]]), 20 * math.log10(rms), sum(t > -60 for t in tail)])

def windows(node, ids):
    """{window: {row index: axis vector}} from the raw rows, matched to sound_data rows by preset_id."""
    at = {p: i for i, p in enumerate(ids)}; out = {w: {} for w in WIN}
    for line in open(os.path.expanduser(o.out_path(node))):
        r = json.loads(line); i = at.get(r.get("preset_id"))
        if i is None or r.get("note") not in out: continue
        v = axes(r)
        if v is not None: out[r["note"]][i] = v
    return out

def pairs(d, W):
    """{(bank, x, y): (knob, dk, axis moves in spreads)} for one window; x, y are row indices."""
    s = np.std(np.array(list(W.values())), 0) + 1e-9; by = {}; P = {}
    for i in W: by.setdefault(d["group"][i], []).append(i)
    for g, ix in by.items():
        K = d["knobs"][ix]
        for x in range(len(ix)):
            diff = np.abs(K - K[x]) > 0.005
            for y in np.flatnonzero(diff.sum(1) == 1):
                if y > x:
                    j = int(np.flatnonzero(diff[y])[0])
                    P[(g, ix[x], ix[y])] = (j, float(K[y, j] - K[x, j]), (W[ix[y]] - W[ix[x]]) / s)
    return P

def run(node, folds, tau=0.8, eff=0.3):
    table, sw, fixed = o.param_table(node); d = sd.load(o.out_path(node)); nk = len(sw); Wd = windows(node, list(d["preset_id"]))
    P = [pairs(d, Wd[w]) for w in WIN]; base = P[0]
    if len(base) < 50: return {"pairs": len(base), "error": "too few one-knob pairs"}
    banks = sorted({k[0] for k in base}); rng = np.random.RandomState(0); rng.shuffle(banks); fold_of = {b: i % folds for i, b in enumerate(banks)}
    A = len(next(iter(base.values()))[2]); hit = np.zeros(len(WIN)); tot = np.zeros(len(WIN))
    for f in range(folds):
        up = np.zeros((nk, A)); n = np.zeros((nk, A))
        for (g, x, y), (j, dk, mv) in base.items():
            if fold_of[g] == f: continue
            m = np.abs(mv) >= eff; up[j] += np.where(m, np.sign(mv) * np.sign(dk) > 0, 0); n[j] += m
        cons = np.where(n >= 8, np.maximum(up, n - up) / np.maximum(n, 1), 0); maj = np.where(up * 2 >= n, 1, -1)
        for w in range(len(WIN)):
            for (g, x, y), (j, dk, mv) in P[w].items():
                if fold_of.get(g, -1) != f: continue
                for a in np.flatnonzero((cons[j] >= tau) & (np.abs(mv) >= eff)):
                    tot[w] += 1; hit[w] += int(np.sign(dk) * maj[j, a] == np.sign(mv[a]))
    return {"pairs_C3": len(base), "banks": len(banks),
            "by_window": {WIN[w]: {"acc": round(float(hit[w] / tot[w]), 3) if tot[w] else None, "scored_moves": int(tot[w]), "pairs": len(P[w])} for w in range(len(WIN))}}

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--engines", default=",".join(MAIN)); ap.add_argument("--folds", type=int, default=5); a = ap.parse_args()
    res = {}
    for e in a.engines.split(","):
        try: res[e] = run(e, a.folds)
        except Exception as ex: res[e] = {"error": repr(ex)[:160]}
        r = res[e].get("by_window", {}); print(e, " ".join("%s %s (%d)" % (k, v["acc"], v["scored_moves"]) for k, v in r.items()) or res[e], flush=True)
    res["bar"] = "held-out banks, >= 90% per window"; json.dump(res, open(OUT, "w"), indent=1)
