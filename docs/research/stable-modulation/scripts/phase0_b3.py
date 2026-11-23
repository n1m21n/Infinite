"""Phase 0, baseline B3: does our oracle find the paper's counterexamples, and nothing below them?

For the Moog and diode ladders, with g free in (0, inf) and k in [0, kmax], bisect on kmax for the
smallest value at which the oracle finds a witness. The paper proves stability up to 2.88 / 8.90 and
gives counterexamples at 2.89 / 8.91, so a sound oracle must report a value in (2.88, 2.89] and
(8.90, 8.91]. A value at or below the proven bound means an oracle bug (a witness can't exist there).
Also re-checks the paper's exact counterexamples.

Writes data/phase0-b3.json. Seed fixed.
"""
import json, sys, time, pathlib
import numpy as np
from scipy.optimize import minimize
from stabmod import A_of, radius, find_witness

OUT = pathlib.Path(__file__).resolve().parent.parent / (sys.argv[1] if len(sys.argv) > 1 else "data")
OUT.mkdir(exist_ok=True)
SEED = 20261123
W = np.linspace(0, 1, 19)[1:-1]  # interior only: w = 0 and w = 1 are exactly marginal (A = I, A = -I)

def oracle(name, kmax, seed):
    rng = np.random.default_rng(seed)
    ks = np.linspace(0, kmax, 9)
    cands = [(w, k) for w in W for k in ks]
    seqs = find_witness(lambda c: A_of(name, *c), cands, rng, max_len=8, starts=30)
    best = (0.0, None)
    for _, seq in seqs:
        # continuous refinement: keep the k pattern, tune each factor's w inside (0, 1)
        ks_seq = [cands[i][1] for i in seq]
        def neg(wv, ks_seq=ks_seq):
            wv = np.clip(wv, 1e-6, 1 - 1e-6)
            P = np.eye(4)
            for w, k in zip(wv, ks_seq):
                P = A_of(name, w, k) @ P
            return -radius(P) ** (1.0 / len(ks_seq))
        starts = [np.array([cands[i][0] for i in seq])] + [rng.uniform(0.1, 0.9, len(seq)) for _ in range(4)]
        for st in starts:
            res = minimize(neg, st, method="Nelder-Mead", options={"xatol": 1e-8, "fatol": 1e-13, "maxiter": 4000})
            if -res.fun > best[0]:
                wv = np.clip(res.x, 1e-6, 1 - 1e-6)
                best = (-res.fun, [(float(w), float(k)) for w, k in zip(wv, ks_seq)])
    rate, seq = best
    return rate ** len(seq), seq

def smallest_unstable(name, lo, hi, seed):
    """Bisection to 0.001 on kmax. lo must be witness-free, hi must have a witness."""
    trace = []
    for kmax in (lo, hi):
        r, seq = oracle(name, kmax, seed)
        trace.append({"kmax": kmax, "radius": r, "witness": bool(r > 1 + 1e-6), "seq": seq})
    if trace[0]["witness"] or not trace[1]["witness"]:
        return None, trace
    while hi - lo > 1e-3:
        mid = round((lo + hi) / 2, 4)
        r, seq = oracle(name, mid, seed)
        w = r > 1 + 1e-6
        trace.append({"kmax": mid, "radius": r, "witness": bool(w), "seq": seq})
        lo, hi = (lo, mid) if w else (mid, hi)
    return hi, trace

def paper_counterexample(name, g, k):
    w = g / (1 + g)
    F = A_of(name, w, k) @ A_of(name, w, 0.0)
    return radius(F)

t0 = time.time()
result = {"seed": SEED, "paper": {}, "oracle": {}}
for name, g, kbad, kok, bracket in [("moog", 1.3764, 2.89, 2.88, (2.5, 3.2)), ("diode", 1.8161, 8.91, 8.90, (8.0, 9.5))]:
    result["paper"][name] = {
        "g": g, f"radius_A(k={kbad})A(0)": paper_counterexample(name, g, kbad),
        f"radius_A(k={kok})A(0)": paper_counterexample(name, g, kok),
    }
    k_star, trace = smallest_unstable(name, *bracket, seed=SEED)
    result["oracle"][name] = {"bracket": bracket, "smallest_unstable_kmax": k_star,
                              "paper_proven_stable_up_to": kok, "paper_counterexample_at": kbad, "trace": trace}
    print(name, "paper rho(A1 A0):", result["paper"][name], "| oracle smallest unstable kmax:", k_star, flush=True)
result["seconds"] = time.time() - t0
(OUT / "phase0-b3.json").write_text(json.dumps(result, indent=1))
print("wrote", OUT / "phase0-b3.json", f"{result['seconds']:.0f} s")
