"""Phase 0, baseline B3: the oracle's smallest k with a stuck-oscillation witness on the explicit tanh Moog.

For each cutoff ceiling, bisect kmax for the smallest value where the oracle finds a witness (D1): a knob
sequence inside the box and a Newton-refined periodic orbit (residual < 1e-10, amplitude > 1e-3) of the
real nonlinear filter with zero input. Sequences come from the linear witness search; one knob (g of one
slot) is tuned inside its range so the orbit closes after N repeats (satloops.locked_orbit).
Consistency check: the T3-certified k (data/phase0-t3.json) must be witness-free.
Writes data/phase0-b3.json.
"""
import json, sys, time, pathlib, warnings
import numpy as np
from fractions import Fraction
warnings.filterwarnings("ignore")
from satloops import simulate, moog, find_witness, radius, settle, phase_zero, locked_orbit

OUT = pathlib.Path(__file__).resolve().parent.parent / (sys.argv[1] if len(sys.argv) > 1 else "data")
SEED, GMIN = 20261123, 1e-4
S = moog()

def witness(gmax, kmax, rng):
    gs = np.geomspace(GMIN * 10, gmax, 9); ks = np.linspace(0, kmax, 5)
    cands = [(g, k) for g in gs for k in ks]
    seqs = find_witness(lambda t: S.linear(t), cands, rng, max_len=6, starts=15, top=6)
    seqs = [(r, [cands[i] for i in sq]) for r, sq in seqs] + [(radius(S.linear((gmax, kmax))), [(gmax, kmax)])]
    box = ((GMIN, 0.0), (gmax, kmax))
    for r, seq in sorted(seqs, key=lambda v: -v[0] ** (1 / len(v[1]))):
        if r <= 1 + 1e-9:
            continue
        x = settle(S, seq, rng.normal(size=4) * 0.3, samples=1 << 17)
        if np.linalg.norm(x) < 1e-3:
            continue
        x = phase_zero(S, seq, x)
        # estimate the orbit's repeat count from the dominant linear eigenvalue angle of one sequence
        # orbit lengths: best rational approximations m/N of the rotation ang/(2 pi), N <= 4096, measured on
        # the settled nonlinear orbit (its frequency differs from the linear one)
        ang = rotation(seq, x)
        Ns = sorted({Fraction(ang / (2 * np.pi)).limit_denominator(d).denominator
                     for d in (8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096)}) if ang > 1e-6 else [1, 2]
        for N in Ns:
            for slot in range(len(seq)):
                out = locked_orbit(S, seq, slot, 0, box, x, N)
                if out:
                    s, xs, res = out
                    return {"seq": s, "repeats": N, "x": xs.tolist(), "residual": res,
                            "amplitude": float(np.linalg.norm(xs)), "linear_rate": float(r ** (1 / len(seq)))}
    return None

def rotation(seq, x, reps=4096):
    """Mean rotation per sequence repeat of the settled orbit, from zero crossings of x[0]."""
    v = []
    for _ in range(reps):
        x = simulate(S, seq, x, 1); v.append(x[0])
    v = np.array(v)
    z = np.nonzero(np.diff(np.sign(v)) != 0)[0]
    if len(z) < 3:
        return 0.0
    return float(np.pi * (len(z) - 1) / (z[-1] - z[0]))

def bis(gmax, lo, hi, rng, tol=1e-3):
    trace = []
    wlo = witness(gmax, lo, rng); whi = witness(gmax, hi, rng)
    trace += [{"kmax": lo, "witness": wlo}, {"kmax": hi, "witness": whi}]
    if wlo or not whi:
        return None, trace
    while hi - lo > tol:
        m = round((lo + hi) / 2, 5)
        w = witness(gmax, m, rng)
        trace.append({"kmax": m, "witness": w})
        lo, hi = (lo, m) if w else (m, hi)
    return hi, trace

t0 = time.time()
t3 = {r["gmax"]: r["t3_certified_k"] for r in json.loads((OUT / "phase0-t3.json").read_text())["rows"]}
res = {"seed": SEED, "rows": []}
for gmax in [0.05, 0.2, 0.4, 0.785]:
    rng = np.random.default_rng(SEED)
    kstar, trace = bis(gmax, t3[gmax], 4.0, rng)
    row = {"gmax": gmax, "t3_certified_k": t3[gmax], "oracle_first_witness_k": kstar,
           "witness": next((tr["witness"] for tr in trace if tr["kmax"] == kstar), None), "trace_len": len(trace)}
    res["rows"].append(row)
    print(gmax, "T3", t3[gmax], "first witness", kstar, (row["witness"] or {}).get("repeats"), f"{time.time()-t0:.0f}s", flush=True)
res["seconds"] = time.time() - t0
(OUT / "phase0-b3.json").write_text(json.dumps(res, indent=1, default=float))
print("wrote", OUT / "phase0-b3.json")
