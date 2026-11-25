"""Phase 1 on corpus G (README sections 6, 8 and 12): per kernel

  B1  frozen check on the small-signal matrix: B1a at the default knobs (box midpoint, what
      AliasAnalysis does), B1b at every point of a grid (7/5/4 per axis for d = 1/2/3)
  B4  linearise and hope: Report 2's covered certifier on the small-signal matrix
  T1  sector vertices: Report 2's covered certifier with the saturator gains delta_i as extra box axes
      (A' + B diag(delta) C is affine in each delta, so its cover is exact along them). Every vertex is
      held to strict decrease, including delta_i = 0: sound, and more conservative than section 8's
      non-strict rule
  T3  Lur'e-Lyapunov (t3cert). Corners are exact only for affine knob terms, knob-free C on every
      saturator and shapes tanh / soft / clamp; elsewhere a T3 pass is recorded as grid-only and not counted
  oracle (D1): satloops.oracle, fixed seed per kernel
G1 verdict = T1 certified or T3 exactly certified (decided 2026-11-23).

Report 2's cover runs with a cell budget of 20 000 (Report 2: 200 000) to keep the run on the M2; a
cover that does not close is "grid-certified", counted as no proof. Writes data/phase1-G.json (resumable).
Usage: phase1_G.py [workers] [first:last]
Env: C1_BUDGET (cell budget), C1_OUT (data file name), C1_IDS (comma list of kernel ids).
"""
import os
os.environ.setdefault("OMP_NUM_THREADS", "1"); os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
import json, sys, time, pathlib, itertools, warnings
import multiprocessing as mp
import numpy as np
warnings.filterwarnings("ignore")
from satloops import SHAPES, oracle, R2cert
from gen_sat import lure, breaks, affine, A_from_spec
import t3cert

HERE = pathlib.Path(__file__).resolve().parent.parent
OUT = HERE / "data" / os.environ.get("C1_OUT", "phase1-G.json")
BUDGET = int(os.environ.get("C1_BUDGET", "20000"))
GRIDN = {1: 9, 2: 7, 3: 5}
FROZEN = {1: 7, 2: 5, 3: 4}

def rho(M):
    return float(np.max(np.abs(np.linalg.eigvals(M))))

def run(spec):
    t0 = time.time()
    d, n = spec["d"], spec["n"]
    L = lure(spec)
    m = L.m
    lo, hi = [0.0] * d, [1.0] * d
    br = breaks(spec)
    r = {"id": spec["id"], "n": n, "d": d, "m": m, "shapes": L.shapes,
         "kinds": [s["kind"] for s in spec["sats"]], "built_frozen_stable": spec["built_frozen_stable"],
         "affine": affine(spec)}
    A = lambda *th: A_from_spec(spec, th)
    r["B1a_radius"] = rho(A(*[0.5] * d))
    grid = list(itertools.product(*[np.linspace(0, 1, FROZEN[d])] * d))
    r["B1b_radius"] = max(rho(A(*th)) for th in grid)
    box = R2cert.Box(lo, hi, breaks=br)
    tb = time.time()
    b4 = R2cert.certify(A, box, grid=[GRIDN[d]] * d, cell_budget=BUDGET)
    r["B4"] = {"status": b4["status"], "reason": b4.get("reason", ""), "s": round(time.time() - tb, 2)}
    # T1: sector vertices as extra axes
    sec = [SHAPES[s]["sector"] for s in L.shapes]
    def M(*z):
        Ap, B, C = L.ABC(*z[:d])
        return Ap + B @ np.diag(z[d:]) @ C
    box1 = R2cert.Box(lo + [a for a, _ in sec], hi + [b for _, b in sec], breaks=br)
    tb = time.time()
    t1 = R2cert.certify(M, box1, grid=[GRIDN[d]] * d + [2] * m, cell_budget=BUDGET)
    r["T1"] = {"status": t1["status"], "reason": t1.get("reason", ""), "s": round(time.time() - tb, 2)}
    # T3
    tb = time.time()
    knobfree = all(s["kind"] == "column" for s in spec["sats"])
    exact = r["affine"] and knobfree and set(L.shapes) <= {"tanh", "soft", "clamp"} and not br
    try:
        t3 = t3cert.certify_t3(L, lo, hi, cells=[2] * d, log_axes=())
        st = t3["status"]
        if st == "certified" and not exact:
            st = "grid-certified"
        r["T3"] = {"status": st, "reason": t3.get("reason", ""), "exact_corners": exact,
                   "x_strict": t3.get("x_strict"), "s": round(time.time() - tb, 2)}
    except Exception as e:  # noqa: BLE001
        r["T3"] = {"status": "no proof", "reason": f"error: {e}", "exact_corners": exact}
    r["G1"] = "certified" if (r["T1"]["status"] == "certified" or r["T3"]["status"] == "certified") else "no proof"
    tb = time.time()
    rng = np.random.default_rng(int(spec["id"][1:].replace("-", "")))
    w = oracle(L, lo, hi, (), rng)
    r["witness"] = None if w is None else {k: w[k] for k in ("kind", "seq", "repeats", "residual", "amplitude") if k in w}
    r["oracle_s"] = round(time.time() - tb, 2)
    r["seconds"] = round(time.time() - t0, 1)
    return r

def main():
    workers = int(sys.argv[1]) if len(sys.argv) > 1 else 4
    specs = json.loads((HERE / "corpus" / "G.json").read_text())
    if len(sys.argv) > 2:
        a, b = (int(v) for v in sys.argv[2].split(":"))
        specs = specs[a:b]
    if os.environ.get("C1_IDS"):
        want = set(os.environ["C1_IDS"].split(","))
        specs = [s for s in specs if s["id"] in want]
    res = json.loads(OUT.read_text()) if OUT.exists() else {}
    todo = [s for s in specs if s["id"] not in res]
    print(f"{len(todo)} kernels to run, {workers} workers", flush=True)
    with mp.get_context("fork").Pool(workers, maxtasksperchild=4) as pool:
        for r in pool.imap_unordered(run, todo):
            res[r["id"]] = r
            OUT.write_text(json.dumps(res, indent=1, default=float))
            w = r["witness"]
            print(f"{r['id']} n{r['n']} d{r['d']} m{r['m']} B1b {r['B1b_radius']:.3f} B4 {r['B4']['status']:14s} "
                  f"T1 {r['T1']['status']:14s} T3 {r['T3']['status']:14s} wit {w['kind'] if w else '-':6s} {r['seconds']}s",
                  flush=True)

if __name__ == "__main__":
    main()
