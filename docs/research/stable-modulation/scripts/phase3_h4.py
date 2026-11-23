"""Phase 3, H4: slew certificates for the Moog and diode ladders (trajectory T3, McClellan's future work).

Claim proved: for every sequence g[n] in [1e-5, 1e3] (any speed) and k[n] in [0, kmax] with
|k[n+1] - k[n]| <= dk = kmax / 960 (the full range in 20 ms at 48 kHz), the filter is stable.

Certificate: V(x, k) = x' P(k) x with P(k) = P0 + (k / kmax) P1, I <= P(k) <= 1e4 I on [-dk, kmax + dk], and
    A(g, k)' P(k + D) A(g, k) < P(k)   for D = -dk and D = +dk
at every (g, k). The condition is affine in D, so the two ends cover every slew in between. P depends on k
only: phase 3 found no witness from switching g alone at a fixed k (McClellan Cor. 4.1 says the same for
the continuous filter), so g keeps any-speed modulation.

Same steps as T1 (certify.py): LMI on a grid with s_i = 1 - rho(A_i) scaling, a float64 re-check of
||P(k+D)^1/2 A P(k)^-1/2|| < 1, and a cover of the (log g, k) box: the bound on a cell is the largest corner
norm plus 1.5x the multilinear blend error at the centre and edge midpoints, for each D; failing corners go
back to the LMI. kmax is found by bisection to 2 decimals. Sanity only: 2^16-sample simulations under random
slew-limited k and random g at the certified kmax.

    python phase3_h4.py            writes data/phase3-h4.json
"""
import itertools, json, math, pathlib, sys, time
import numpy as np
import cvxpy as cp

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from stabmod import discretise, moog_Ac, diode_Ac, radius  # noqa: E402

KMAX_P = 1e4
SAFETY = 1.5
G_LO, G_HI = 1e-5, 1e3
ANY_SPEED = {"moog": 2.8848, "diode": 8.8965}  # phase 1, H2 (certified any-speed upper k)
AC = {"moog": moog_Ac, "diode": diode_Ac}

def sqrtm_pair(P):
    w, V = np.linalg.eigh(P)
    if w[0] <= 0:
        return None, None
    return V @ np.diag(np.sqrt(w)) @ V.T, V @ np.diag(1 / np.sqrt(w)) @ V.T

class Slew:
    """P(k) is piecewise linear in k over `nodes` equal segments of [0, kmax]; nodes = 2 is the affine P of
    README section 8. Real k is clipped to [0, kmax], so a step D is limited to k + D in [0, kmax]."""
    def __init__(self, name, kmax, nodes=2):
        self.name, self.kmax, self.dk, self.m = name, kmax, kmax / 960.0, nodes
        self.knots = list(np.linspace(0, kmax, nodes))
        self.cache = {}
    def A(self, ug, k):
        key = (round(ug, 13), round(k, 13))
        if key not in self.cache:
            self.cache[key] = discretise(AC[self.name](k), math.exp(ug))
        return self.cache[key]
    def weights(self, k):
        h = self.kmax / (self.m - 1)
        j = min(max(int(math.floor(k / h)), 0), self.m - 2)
        lam = k / h - j
        return j, lam
    def Ds(self, k):
        """The ends of each affine piece of D -> P(k + D) over the allowed steps: -dk, +dk (where k + D stays
        in range) and every knot within dk of k (as a function of k, so a cover cell can use it)."""
        out = []
        if k >= self.dk - 1e-15: out.append(lambda kk: -self.dk)
        if k <= self.kmax - self.dk + 1e-15: out.append(lambda kk: self.dk)
        for q in self.knots:
            if abs(q - k) <= self.dk + 1e-15:
                out.append(lambda kk, q=q: q - kk)
        return out
    def k_edges(self):
        e = set([0.0, self.kmax])
        for q in self.knots:
            for v in (q - self.dk, q, q + self.dk):
                if 0 < v < self.kmax: e.add(v)
        return sorted(e)

def solve(sl, pts):
    n = sl.A(0.0, 0.0).shape[0]
    Pn = [cp.Variable((n, n), symmetric=True) for _ in range(sl.m)]
    t = cp.Variable()
    def Pk(k):
        j, lam = sl.weights(k)
        return (1 - lam) * Pn[j] + lam * Pn[j + 1]
    I = np.eye(n)
    cons = []
    for Pj in Pn:
        cons += [Pj >> I, Pj << KMAX_P * I]
    for (ug, k) in pts:
        A = sl.A(ug, k)
        s = max(1.0 - radius(A), 1e-12)
        for Df in sl.Ds(k):
            M = (Pk(k) - A.T @ Pk(k + Df(k)) @ A) / s
            cons.append((M + M.T) / 2 >> t * I)
    prob = cp.Problem(cp.Maximize(t), cons)
    try:
        prob.solve(solver=cp.CLARABEL)
    except cp.error.SolverError:
        return None
    if Pn[0].value is None or t.value is None or t.value <= 0:
        return None
    return [(P.value + P.value.T) / 2 for P in Pn], float(t.value)

def certify_slew(sl, rounds=20, budget=200000):
    ulo, uhi = math.log(G_LO), math.log(G_HI)
    ks = sorted(set(list(np.linspace(0, sl.kmax, 2 * sl.m + 7)) + sl.knots))
    pts = [(ug, k) for ug in np.linspace(ulo, uhi, 25) for k in ks]
    for rnd in range(rounds):
        sol = solve(sl, pts)
        if sol is None:
            return {"status": "no proof", "reason": "LMI infeasible", "rounds": rnd + 1}
        Pn, t = sol
        Sk = {}
        def S(k):
            key = round(k, 12)
            if key not in Sk:
                j, lam = sl.weights(k)
                Sk[key] = sqrtm_pair((1 - lam) * Pn[j] + lam * Pn[j + 1])
            return Sk[key]
        if any(sqrtm_pair(P)[0] is None for P in Pn):
            return {"status": "no proof", "reason": "P(k) not positive", "rounds": rnd + 1}
        def F(ug, k, Df):
            return S(k + Df(k))[0] @ sl.A(ug, k) @ S(k)[1]
        def N(ug, k, Dfs):
            return max(np.linalg.norm(F(ug, k, Df), 2) for Df in Dfs)
        bad = [p for p in pts if N(*p, sl.Ds(p[1])) >= 1]
        if bad:
            return {"status": "no proof", "reason": f"float64 check fails at {len(bad)} LMI points", "rounds": rnd + 1}
        ke = sl.k_edges()
        stack = [((ulo, b0), (uhi, b1)) for b0, b1 in zip(ke[:-1], ke[1:])]
        cells, failing, worst = 0, [], 0.0
        while stack:
            (a0, b0), (a1, b1) = stack.pop()
            cells += 1
            if cells > budget:
                break
            Dfs = sl.Ds((b0 + b1) / 2)  # the cell lies inside one band of k_edges, so one set serves it
            cs = [(a0, b0), (a1, b0), (a0, b1), (a1, b1)]
            cn = [N(*c, Dfs) for c in cs]
            if max(cn) >= 1:
                failing += [c for c, v in zip(cs, cn) if v >= 1]
                if len(failing) > 20:
                    break
                continue
            err, eg, ek = 0.0, 0.0, 0.0
            for Df in Dfs:
                Fc = [F(*c, Df) for c in cs]
                err = max(err, np.linalg.norm(F((a0 + a1) / 2, (b0 + b1) / 2, Df) - sum(Fc) / 4, 2))
                for (p, q, mm, ax) in [(0, 1, ((a0 + a1) / 2, b0), 0), (2, 3, ((a0 + a1) / 2, b1), 0),
                                       (0, 2, (a0, (b0 + b1) / 2), 1), (1, 3, (a1, (b0 + b1) / 2), 1)]:
                    e = np.linalg.norm(F(*mm, Df) - (Fc[p] + Fc[q]) / 2, 2)
                    err = max(err, e)
                    if ax == 0: eg = max(eg, e)
                    else: ek = max(ek, e)
            bound = max(cn) + SAFETY * err
            if bound < 1:
                worst = max(worst, bound)
                continue
            if eg >= ek:
                mm = (a0 + a1) / 2
                stack += [((a0, b0), (mm, b1)), ((mm, b0), (a1, b1))]
            else:
                mm = (b0 + b1) / 2
                stack += [((a0, b0), (a1, mm)), ((a0, mm), (a1, b1))]
        if not failing and cells <= budget:
            kap = max(float(e[-1] / e[0]) for e in (np.linalg.eigvalsh(P) for P in Pn))
            return {"status": "certified", "t": t, "cells": cells, "worst_bound": float(worst), "rounds": rnd + 1,
                    "lmi_points": len(pts), "P_nodes": [P.tolist() for P in Pn], "kappa": kap}
        if failing:
            pts = pts + list(dict.fromkeys(failing))
            continue
        return {"status": "grid-certified", "reason": "cover did not close", "cells": cells, "rounds": rnd + 1}
    return {"status": "no proof", "reason": "cutting planes did not converge", "rounds": rounds}

def frozen_limit(name):
    """Largest k (2 decimals) where A(g, k) has radius < 1 at every g of a fine log grid: the LTI limit."""
    gs = np.geomspace(G_LO, G_HI, 400)
    lo, hi = 0.0, 40.0
    while hi - lo > 0.005:
        m = (lo + hi) / 2
        if max(radius(discretise(AC[name](m), g)) for g in gs) < 1:
            lo = m
        else:
            hi = m
    return lo

def simulate(name, kmax, rng, steps=1 << 16, runs=20):
    """Sanity only: random slew-limited k (bang-bang at full slew), random g each sample."""
    dk = kmax / 960
    worst = 0.0
    for _ in range(runs):
        x = rng.normal(size=4)
        x /= np.linalg.norm(x)
        k = rng.uniform(0, kmax)
        peak = 1.0
        for i in range(steps):
            k = float(np.clip(k + dk * rng.choice([-1, 1]), 0, kmax))
            x = discretise(AC[name](k), float(np.exp(rng.uniform(math.log(G_LO), math.log(G_HI))))) @ x
            peak = max(peak, float(np.linalg.norm(x)))
        worst = max(worst, peak)
    return worst

G20 = math.tan(math.pi * 20 / 48000)  # 20 Hz cutoff at 48 kHz

def run(name, g_lo):
    global G_LO
    G_LO = g_lo
    return run_one(name)

def run_one(name):
    out = {}
    if True:
        t0 = time.time()
        lti = frozen_limit(name)
        base = ANY_SPEED[name]
        lo, hi, best = base, lti, None
        log = []
        r = certify_slew(Slew(name, lo))
        log.append({"kmax": lo, **{k: v for k, v in r.items() if k not in ("P0", "P1")}})
        if r["status"] == "certified":
            best = (lo, r)
            while hi - lo > 0.01:
                m = round((lo + hi) / 2, 3)
                r = certify_slew(Slew(name, m))
                log.append({"kmax": m, **{k: v for k, v in r.items() if k not in ("P0", "P1")}})
                print(name, m, r["status"], r.get("reason", ""), flush=True)
                if r["status"] == "certified":
                    lo, best = m, (m, r)
                else:
                    hi = m
        kcert = best[0] if best else None
        sim = simulate(name, kcert, np.random.default_rng(20261123), steps=1 << 14, runs=8) if kcert else None
        out[name] = {"any_speed_kmax": base, "lti_limit": lti, "slew_kmax": kcert,
                     "wider_pct": (100 * (kcert / base - 1)) if kcert else None,
                     "bar_pct": 25, "pass": bool(kcert and kcert >= 1.25 * base),
                     "certificate": best[1] if best else None, "bisection": log, "sim_peak_gain": sim,
                     "seconds": time.time() - t0}
        print(name, json.dumps({k: v for k, v in out[name].items() if k not in ("certificate", "bisection")}), flush=True)
    return out[name]

if __name__ == "__main__":
    # registered: g in [1e-5, 1e3]. Exploratory (after the registered run failed, see README): cutoff >= 20 Hz.
    res = {"registered_g_lo_1e-5": {nm: run(nm, 1e-5) for nm in ("moog", "diode")},
           "exploratory_g_lo_20Hz": {nm: run(nm, G20) for nm in ("moog", "diode")}}
    (HERE.parent / "data" / "phase3-h4.json").write_text(json.dumps(res, indent=1))
