"""Phase 0: T3 (D3 as amended) on the explicit tanh Moog. Writes data/phase0-t3.json.

For each cutoff ceiling: the largest any-speed k certified by T3 (24 log cells on g), a dense float64
re-check inside every cell with that cell's multipliers, and a soundness probe: random-switching and
oracle-sequence simulations of the real nonlinear filter at the certified k from large starts, which
must all decay. Then the largest ceiling at which T3 still certifies k = 5/3 (Helie's continuous bound).
"""
import json, sys, time, pathlib, warnings
import numpy as np
warnings.filterwarnings("ignore")
from satloops import moog, simulate, stick_witness
import t3cert

OUT = pathlib.Path(__file__).resolve().parent.parent / (sys.argv[1] if len(sys.argv) > 1 else "data")
OUT.mkdir(exist_ok=True)
SEED, GMIN, CELLS = 20261123, 1e-4, 24
S = moog()

def cert(gmax, k):
    return t3cert.certify_t3(S, (GMIN, 0.0), (gmax, k), cells=[CELLS, 1])

def bis(pred, lo, hi, tol=1e-3):
    while hi - lo > tol:
        m = (lo + hi) / 2
        lo, hi = (m, hi) if pred(m) else (lo, m)
    return round(lo, 4)

def dense(r, k):
    worst = -np.inf
    for clo, chi, cm in r["cert"]["cells"]:
        th = [(g, kk) for g in np.geomspace(clo[0], chi[0], 7) for kk in np.linspace(0, k, 9)]
        worst = max(worst, t3cert.check(S, r["cert"], th, cm))
    return worst

def probe(gmax, k, rng, runs=200, n=20000):
    """Largest final/initial norm ratio over random-switching runs from starts up to 100."""
    worst = 0.0
    for _ in range(runs):
        x0 = rng.normal(size=4) * rng.choice([0.1, 3.0, 100.0])
        x = x0.copy()
        for _s in range(n):
            th = (float(np.exp(rng.uniform(np.log(GMIN * 100), np.log(gmax)))), float(rng.uniform(0, k)))
            x = S.step(x, th)
        worst = max(worst, float(np.linalg.norm(x) / np.linalg.norm(x0)))
    return worst

t0 = time.time()
res = {"seed": SEED, "gmin": GMIN, "cells": CELLS, "rows": []}
rng = np.random.default_rng(SEED)
for gmax in [0.05, 0.2, 0.4, 0.785]:
    kc = bis(lambda k: cert(gmax, k)["status"] == "certified", 0, 4)
    r = cert(gmax, kc)
    row = {"gmax": gmax, "t3_certified_k": kc, "t": r.get("t"), "dense_worst": dense(r, kc) if "cert" in r else None,
           "random_switching_worst_ratio": probe(gmax, kc, rng),
           "q": r.get("q"), "P_min_eig": r.get("P_min_eig")}
    # a stick search at the certified k from the edge sequences: must find nothing
    seqs = [[(gmax, kc), (GMIN * 100, 0.0)], [(gmax, kc)] * 3 + [(gmax, 0.0)], [(gmax, kc)]]
    row["stick_witness_at_certified_k"] = [stick_witness(S, sq, rng.normal(size=4) * 3) for sq in seqs]
    res["rows"].append(row); print({k: v for k, v in row.items() if k not in ("q",)}, f"{time.time()-t0:.0f}s", flush=True)
res["largest_ceiling_certifying_5_3"] = bis(lambda g: cert(g, 5 / 3)["status"] == "certified", 0.05, 0.785, tol=5e-3)
print("largest g ceiling with k = 5/3 certified:", res["largest_ceiling_certifying_5_3"])
res["seconds"] = time.time() - t0
(OUT / "phase0-t3.json").write_text(json.dumps(res, indent=1, default=float))
print("wrote", OUT / "phase0-t3.json", f"{res['seconds']:.0f} s")
