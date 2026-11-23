"""Phase 1, baseline B1 and B2 on corpus G (no certifier involved).

B2: how many G kernels have an instability witness (D1), by the oracle.
B1: of the kernels with a witness, how many today's frozen check calls "stable":
   B1a = radius < 1 at the default knobs only (what AliasAnalysis does; default = midpoint of the box);
   B1b = radius < 1 at every point of a 7-per-axis grid (a stricter frozen check than today's).
Writes data/phase1-b12.json.
"""
import json, sys, time, pathlib
import numpy as np
from scipy.optimize import minimize
from stabmod import find_witness, radius
from gen_kernels import A_from_spec, frozen_grid

HERE = pathlib.Path(__file__).resolve().parent.parent
specs = json.loads((HERE / "corpus" / "G.json").read_text())
rng = np.random.default_rng(20261123)
TOL = 1e-6

def oracle(spec):
    d = spec["d"]
    per = {1: 9, 2: 5, 3: 4}[d]
    cands = frozen_grid(d, per)
    Af = lambda th: A_from_spec(spec, th)
    seqs = find_witness(Af, cands, rng, max_len=8, starts=20, top=4)
    best_rate, best = 0.0, None
    for r, seq in seqs:
        rate = r ** (1 / len(seq))
        if rate > best_rate:
            best_rate, best = rate, [cands[i] for i in seq]
    if 0.97 < best_rate <= 1 + TOL:  # near miss: tune the knob values continuously
        for r, seq in seqs[:2]:
            L = len(seq)
            def neg(v):
                v = np.clip(v, 0, 1).reshape(L, d)
                P = np.eye(spec["n"])
                for th in v: P = Af(th) @ P
                return -radius(P) ** (1 / L)
            x0 = np.array([cands[i] for i in seq], float).ravel()
            res = minimize(neg, x0, method="Nelder-Mead", options={"maxiter": 3000, "xatol": 1e-7, "fatol": 1e-12})
            if -res.fun > best_rate:
                best_rate, best = -res.fun, [tuple(map(float, t)) for t in np.clip(res.x, 0, 1).reshape(L, d)]
    return best_rate, best

t0 = time.time()
rows = []
for s in specs:
    rate, seq = oracle(s)
    L = len(seq)
    wit = rate ** L > 1 + TOL
    mid = tuple([0.5] * s["d"])
    f_default = radius(A_from_spec(s, mid)) < 1
    f_grid = max(radius(A_from_spec(s, th)) for th in frozen_grid(s["d"])) < 1
    rows.append({"id": s["id"], "n": s["n"], "d": s["d"], "built_frozen_stable": s["built_frozen_stable"],
                 "witness": bool(wit), "growth_per_step": rate, "witness_len": L, "witness_seq": seq,
                 "frozen_default_says_stable": bool(f_default), "frozen_grid_says_stable": bool(f_grid)})
W = [r for r in rows if r["witness"]]
summary = {
    "G": len(rows), "with_witness": len(W), "without_witness": len(rows) - len(W),
    "B1a_false_safe_default_only": sum(r["frozen_default_says_stable"] for r in W),
    "B1b_false_safe_whole_grid": sum(r["frozen_grid_says_stable"] for r in W),
    "witness_needs_switching": sum(r["witness_len"] > 1 for r in W),
    "seconds": time.time() - t0}
print(json.dumps(summary, indent=1))
(HERE / "data" / "phase1-b12.json").write_text(json.dumps({"summary": summary, "rows": rows}, indent=1))
