"""Phase 0 on the explicit tanh Moog: frozen limit, linear switching witness, and whether the
sector-only vertex certificate (T1, D3 as signed) can prove anything. Writes data/phase0-moog.json.

Also the minimal counterexample to T1: a two-stage tanh cascade at one fixed cutoff. With stage 2 at
slope 0 and stage 1 at slope 1, V non-increasing forces P12 = P22; with stage 1 at slope 0 it forces
P12 = 0; together P22 = 0, so no P > 0 exists.
"""
import json, sys, time, pathlib, warnings
import numpy as np
warnings.filterwarnings("ignore")
from satloops import moog, Lure, radius, find_witness
from vcert import certify_vertices

OUT = pathlib.Path(__file__).resolve().parent.parent / (sys.argv[1] if len(sys.argv) > 1 else "data")
OUT.mkdir(exist_ok=True)
SEED, GMIN = 20261123, 1e-4
S = moog()
LIN = Lure("moog-linear", S.ABC, ["id"] * 5)

def bis(pred, lo, hi, tol=1e-3):
    while hi - lo > tol:
        m = (lo + hi) / 2
        lo, hi = (m, hi) if pred(m) else (lo, m)
    return round(lo, 4)

def lin_witness_rate(gmax, kmax, rng):
    gs = np.geomspace(GMIN * 10, gmax, 9); ks = np.linspace(0, kmax, 5)
    c = [(g, k) for g in gs for k in ks]
    r, seq = find_witness(lambda t: S.linear(t), c, rng, max_len=6, starts=15)[0]
    return r ** (1 / len(seq))

t0 = time.time()
res = {"seed": SEED, "gmin": GMIN, "rows": []}
for gmax in [0.05, 0.2, 0.4, 0.785]:
    rng = np.random.default_rng(SEED)
    row = {"gmax": gmax,
           "frozen_limit_k_at_gmax": bis(lambda k: radius(S.linear((gmax, k))) < 1, 0, 8),
           "linear_switching_witness_k": bis(lambda k: lin_witness_rate(gmax, k, rng) <= 1 + 1e-6, 0.5, 4.5),
           "linear_cqlf_certified_k": bis(lambda k: certify_vertices(LIN, (GMIN, 0), (gmax, k))["status"] == "certified", 0, 4.5),
           "tanh_sector_k0": certify_vertices(S, (GMIN, 0), (gmax, 0.0)).get("reason", "certified")}
    for Y in [10.0, 100.0]:
        eps = float(np.tanh(Y) / Y)
        row[f"tanh_sector_floor_Y{Y:g}_certified_k"] = bis(
            lambda k: certify_vertices(S, (GMIN, 0), (gmax, k), floor=[eps] * 5)["status"] == "certified", 0, 4)
    res["rows"].append(row); print(row, flush=True)

g = 0.2
two = Lure("two-stage", lambda g_, _: (np.eye(2), np.array([[-g_, 0], [g_, -g_]]), np.eye(2)), ["tanh", "tanh"])
res["two_stage_fixed_g"] = certify_vertices(two, (g, 0), (g, 0)).get("reason", "certified")
print("two-stage:", res["two_stage_fixed_g"])
res["seconds"] = time.time() - t0
(OUT / "phase0-moog.json").write_text(json.dumps(res, indent=1))
print("wrote", OUT / "phase0-moog.json")
