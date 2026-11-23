"""Phase 0 on the literature set L (README sections 6 and 8): per filter, the frozen check (B1),
linearise-and-hope (B4: Report 2's vertex certificate with every saturator set to the identity), T1
(sector vertices), T3 (Lur'e-Lyapunov, the main G1 method) and the oracle (D1). For filters with a
resonance-like knob, the largest certified ceiling on that knob. Writes data/phase0-L.json.

Usage: phase0_L.py [name ...]   (default: all of L)
"""
import json, sys, time, pathlib, itertools, warnings
import numpy as np
warnings.filterwarnings("ignore")
from satloops import Lure, oracle
from vcert import certify_vertices
import t3cert, lit

OUT = pathlib.Path(__file__).resolve().parent.parent / "data"
SEED = 20261123

def grid(lo, hi, log_axes, k=25):
    ax = []
    for j, (a, b) in enumerate(zip(lo, hi)):
        ax.append([a] if a == b else list(np.geomspace(a, b, k) if (j in log_axes and a > 0) else np.linspace(a, b, k)))
    return list(itertools.product(*ax))

def bis(pred, lo, hi, tol=1e-3):
    if not pred(lo):
        return None
    if pred(hi):
        return hi
    while hi - lo > tol:
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if pred(mid) else (lo, mid)
    return lo

def t3_ok(S, lo, hi, cells, log_axes):
    return t3cert.certify_t3(S, lo, hi, cells=cells, log_axes=log_axes)["status"] == "certified"

def run(entry):
    name, S, lo, hi, log_axes, cells, rax, affine = entry
    t0 = time.time()
    rng = np.random.default_rng(SEED)
    lin = Lure(name + "-linear", S.ABC, ["id"] * S.m)
    G = grid(lo, hi, log_axes)
    frozen = max(float(max(abs(np.linalg.eigvals(S.linear(th))))) for th in G)
    r = {"name": name, "shapes": S.shapes, "box": [list(lo), list(hi)], "multi_affine": affine,
         "frozen_max_radius": frozen, "frozen_check": "safe" if frozen < 1 else "unsafe"}
    if affine:
        r["B4_linear"] = certify_vertices(lin, lo, hi).get("status")
        r["T1"] = certify_vertices(S, lo, hi).get("status")
        r3 = t3cert.certify_t3(S, lo, hi, cells=cells, log_axes=log_axes)
    else:
        # not multi-affine: corners are not exact; run on the cell cover and mark grid-only
        r["B4_linear"] = "not run (not multi-affine)"
        r["T1"] = "not run (not multi-affine)"
        r3 = t3cert.certify_t3(S, lo, hi, cells=cells, log_axes=log_axes)
        if r3["status"] == "certified":
            r3["status"] = "grid-certified"
    r["T3"] = {k: r3.get(k) for k in ("status", "reason", "t", "worst", "ncells", "x_strict", "P_min_eig", "q", "I")}
    w = oracle(S, lo, hi, log_axes, rng)
    r["witness"] = w if w is None else {k: w[k] for k in w if k != "x"} | {"x": w.get("x")}
    if rax is not None and r3["status"] != "certified":
        pr = lambda v: t3_ok(S, lo, tuple(v if j == rax else h for j, h in enumerate(hi)), cells, log_axes)
        r["T3_ceiling"] = {"axis": rax, "value": bis(pr, lo[rax], hi[rax], tol=(hi[rax] - lo[rax]) / 256)}
    r["seconds"] = round(time.time() - t0, 1)
    return r

if __name__ == "__main__":
    want = set(sys.argv[1:])
    path = OUT / "phase0-L.json"
    res = json.loads(path.read_text()) if path.exists() else {}
    for e in lit.L:
        if want and e[0] not in want:
            continue
        r = run(e)
        res[e[0]] = r
        w = r["witness"]
        print(f"{e[0]:14s} frozen {r['frozen_check']:6s} B4 {r['B4_linear']:9s} T1 {r['T1']:9s} T3 {r['T3']['status']:14s}"
              f" witness {w['kind'] if w else 'none':6s} ceiling {r.get('T3_ceiling')} {r['seconds']}s", flush=True)
        path.write_text(json.dumps(res, indent=1, default=float))
