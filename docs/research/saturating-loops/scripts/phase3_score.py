"""Phase 3 scorer (locked at the start of phase 4, then run unchanged on B): H1, H3 and H4 per kernel.

Per kernel:
  1. Phase 1's run (phase1_G.run, unchanged): B1, B4, T1, T3, G1 verdict and the oracle (D1). For G the
     cached data/phase1-G.json results are reused (same code, same seeds).
  2. Extracted matrices (phase 2): the kernel's A, B, C from the Field interpreter must match the Python
     model within 1e-5 (data/phase2-compare*.json). Kernels Field can't compile (register cap) are
     scored on the Python model and flagged; the Float32 bound below uses the set's largest measured
     error for them, as in Report 2.
  3. Float32 bound on every G1 certificate: certify.py is rerun to get kappa(P) and the cover's slack
     (1 - largest cell bound). An entry error eps in A, B, C moves a vertex matrix M = A + B diag(d) C
     by at most eps (1 + m bmax (|B|max + |C|max) + m bmax eps) per entry, so ||dM||_P is at most
     sqrt(kappa) n that bound; times 10 as in Report 2. The certificate holds in Float32 if that is
     below the slack.
  4. G2 (H4), trajectory T4: the difference of two runs under the same input and modulation is
     e+ = (A + B S C) e, S_i in phi_i's slope range (mean-value theorem). Certified by certify.py over
     the slope box with every vertex strict (no floor needed, so clamp is covered). tanh, soft, clamp
     and abs have slope range = sector, so T4 is the T1 problem and T1's verdict is reused; sin
     (sector [-0.217, 1], slope [-1, 1]) gets its own run.
  5. Two-start oracle (H4's failure test), on every G1-certified kernel: two starts, the same random
     input |in| <= 4 (+12 dBFS) and the same random modulation (held 1, 8, 64 or 512 samples), 32768
     samples. A failure is ||x1 - x2|| > 1e-6 max(1, ||x1(0) - x2(0)||) at the end.

    python phase3_score.py <set> [workers]        sets: G, B     writes data/phase3-<set>.json
"""
import os
os.environ.setdefault("OMP_NUM_THREADS", "1"); os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
import json, math, sys, pathlib, warnings
import multiprocessing as mp
import numpy as np
warnings.filterwarnings("ignore")
from satloops import SHAPES, R2cert
from gen_sat import lure, breaks, A_from_spec
import phase1_G

HERE = pathlib.Path(__file__).resolve().parent.parent
GRIDN = phase1_G.GRIDN
BUDGET = phase1_G.BUDGET
PHASE2_FALLBACK = {"G": None, "B": None}  # filled from the phase 2 file of the set

def input_vectors(spec, L):
    """Where `in` enters (emit_sat.g_sat_kernel adds it to row 0): inside the row-0 saturator's
    argument if row 0 is row-saturated (Din), else straight into x0 (Bin)."""
    n, m = spec["n"], L.m
    order = [s for s in spec["sats"] if s["kind"] == "row"] + [s for s in spec["sats"] if s["kind"] == "column"]
    Bin, Din = np.zeros(n), np.zeros(m)
    for i, s in enumerate(order):
        if s["kind"] == "row" and s["index"] == 0:
            Din[i] = 1.0
            return Bin, Din
    Bin[0] = 1.0
    return Bin, Din

def two_start(spec, L, rng, trials=4, N=32768):
    d, n = spec["d"], spec["n"]
    Bin, Din = input_vectors(spec, L)
    f = [SHAPES[s]["f"] for s in L.shapes]
    worst = 0.0
    for t in range(trials):
        hold = [1, 8, 64, 512][t % 4]
        x1 = rng.uniform(-1, 1, n) * (10.0 if t % 2 else 1.0)
        x2 = rng.uniform(-1, 1, n) * (10.0 if t % 2 else 1.0)
        e0 = max(1.0, float(np.linalg.norm(x1 - x2)))
        th = rng.uniform(0, 1, d)
        A, B, C = L.ABC(*th)
        for k in range(N):
            if k % hold == 0:
                th = rng.uniform(0, 1, d)
                A, B, C = L.ABC(*th)
            u = rng.uniform(-4, 4)
            y1, y2 = C @ x1 + Din * u, C @ x2 + Din * u
            w1 = np.array([fi(v) for fi, v in zip(f, y1)])
            w2 = np.array([fi(v) for fi, v in zip(f, y2)])
            x1 = A @ x1 + B @ w1 + Bin * u
            x2 = A @ x2 + B @ w2 + Bin * u
            if not (np.all(np.isfinite(x1)) and np.all(np.isfinite(x2))) or np.max(np.abs(x1)) > 1e9:
                return {"fail": True, "why": "diverged", "trial": t}
        r = float(np.linalg.norm(x1 - x2)) / e0
        worst = max(worst, r)
        if r > 1e-6:
            return {"fail": True, "why": f"no convergence, ratio {r:.3g}", "trial": t, "hold": hold}
    return {"fail": False, "worst_ratio": worst}

def float32_bound(spec, L, r1, eps):
    """r1: certify() result with kappa and worst_bound. Returns (bound, slack, holds)."""
    d, n, m = spec["d"], spec["n"], L.m
    bmax = max(max(abs(a), abs(b)) for a, b in (SHAPES[s]["sector"] for s in L.shapes))
    Bm = Cm = 0.0
    for th in [np.full(d, v) for v in (0.0, 0.5, 1.0)] + [np.random.default_rng(1).uniform(0, 1, d) for _ in range(20)]:
        A, B, C = L.ABC(*th)
        Bm, Cm = max(Bm, np.max(np.abs(B))), max(Cm, np.max(np.abs(C)))
    per = eps * (1 + m * bmax * (Bm + Cm) + m * bmax * eps)
    bound = math.sqrt(r1["kappa"]) * n * per * 10
    slack = 1.0 - (r1.get("worst_bound") or 0.0)
    return bound, slack, bool(bound < slack)

def extras(args):
    spec, base, p2, eps_set = args
    d, n = spec["d"], spec["n"]
    L = lure(spec)
    m = L.m
    lo, hi = [0.0] * d, [1.0] * d
    br = breaks(spec)
    r = dict(base)
    r["phase2"] = p2
    if r["G1"] != "certified":
        return r
    # Float32 bound: rerun T1 for kappa and slack
    sec = [SHAPES[s]["sector"] for s in L.shapes]
    def M(*z):
        Ap, B, C = L.ABC(*z[:d])
        return Ap + B @ np.diag(z[d:]) @ C
    box1 = R2cert.Box(lo + [a for a, _ in sec], hi + [b for _, b in sec], breaks=br)
    t1 = R2cert.certify(M, box1, grid=[GRIDN[d]] * d + [2] * m, cell_budget=BUDGET)
    eps = p2.get("max_abs") if p2.get("pass") else eps_set
    if t1["status"] == "certified":
        b, s, ok = float32_bound(spec, L, t1, eps)
        r["float32"] = {"eps": eps, "bound": b, "slack": s, "holds": ok, "kappa": t1["kappa"],
                        "from_python_only": not p2.get("pass", False)}
    else:
        r["float32"] = {"holds": None, "why": "T1 rerun did not certify (G1 from T3)"}
    # G2 by T4 over the slope box
    slope = [SHAPES[s]["slope"] for s in L.shapes]
    if slope == sec:
        r["T4"] = {"status": r["T1"]["status"], "reused_T1": True}
    else:
        box4 = R2cert.Box(lo + [a for a, _ in slope], hi + [b for _, b in slope], breaks=br)
        t4 = R2cert.certify(M, box4, grid=[GRIDN[d]] * d + [2] * m, cell_budget=BUDGET)
        r["T4"] = {"status": t4["status"], "reason": t4.get("reason", ""), "reused_T1": False}
    r["G2"] = "certified" if r["T4"]["status"] == "certified" else "no proof"
    rng = np.random.default_rng(int(spec["id"][1:].replace("-", "")) + 7)
    r["two_start"] = two_start(spec, L, rng)
    return r

def main():
    set_ = sys.argv[1]
    workers = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    specs = json.loads((HERE / "corpus" / f"{set_}.json").read_text())
    p2file = HERE / "data" / ("phase2-compare.json" if set_ == "G" else f"phase4-{set_}-compare.json")
    p2all = json.loads(p2file.read_text()) if p2file.exists() else {}
    p2 = {k.split("/")[1]: v for k, v in p2all.items() if k.startswith(set_ + "/")}
    eps_set = max([v["max_abs"] for v in p2.values() if v.get("pass")] or [1e-5])
    base_file = HERE / "data" / f"phase1-{set_}.json"
    base = json.loads(base_file.read_text()) if base_file.exists() else {}
    todo = [s for s in specs if s["id"] not in base]
    if todo:
        print(f"phase 1 run on {len(todo)} kernels", flush=True)
        with mp.get_context("fork").Pool(workers, maxtasksperchild=4) as pool:
            for r in pool.imap_unordered(phase1_G.run, todo):
                base[r["id"]] = r
                base_file.write_text(json.dumps(base, indent=1, default=float))
    out = HERE / "data" / f"phase3-{set_}.json"
    res = {}
    jobs = [(s, base[s["id"]], p2.get(s["id"], {"pass": False, "status": "missing"}), eps_set) for s in specs]
    with mp.get_context("fork").Pool(workers, maxtasksperchild=4) as pool:
        for r in pool.imap_unordered(extras, jobs):
            res[r["id"]] = r
    out.write_text(json.dumps(res, indent=1, default=float))
    summary(set_, res)

def summary(set_, res):
    R = list(res.values())
    wit = [r for r in R if r["witness"]]
    nowit = [r for r in R if not r["witness"]]
    g1 = [r for r in R if r["G1"] == "certified"]
    false_g1 = [r["id"] for r in wit if r["G1"] == "certified"]
    h3 = sum(r["G1"] == "certified" for r in nowit) / max(1, len(nowit))
    g2 = [r for r in g1 if r.get("G2") == "certified"]
    g2_fail = [r["id"] for r in g2 if r["two_start"]["fail"]]
    f32 = [r for r in g1 if r.get("float32", {}).get("holds") is False]
    matched = sum(1 for r in R if r["phase2"].get("pass"))
    print(f"{set_}: {len(R)} kernels, {matched} matched in phase 2, witness {len(wit)}, no witness {len(nowit)}")
    print(f"  H1 false G1 certificates: {len(false_g1)} {false_g1}")
    print(f"  H3 G1 on no-witness: {sum(r['G1'] == 'certified' for r in nowit)}/{len(nowit)} = {100 * h3:.1f}% (bar 70%)")
    print(f"  H4 G2 on G1-certified: {len(g2)}/{len(g1)} = {100 * len(g2) / max(1, len(g1)):.1f}% (bar 50%), "
          f"two-start failures among G2: {len(g2_fail)} {g2_fail}")
    print(f"  two-start failures among G1 without G2: {[r['id'] for r in g1 if r.get('G2') != 'certified' and r['two_start']['fail']]}")
    print(f"  Float32 bound fails: {[r['id'] for r in f32]}")

if __name__ == "__main__":
    main()
