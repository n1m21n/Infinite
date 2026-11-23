"""G1 vertex certifier for Lur'e kernels whose matrices are multilinear in the knobs (README section 8).

When A + B diag(d) C is multilinear in theta (true for the Moog: g and g*k), its P-norm is convex along
each axis, so the max over the knob box is at the box corners: an LMI on corners x vertices is exact,
with no cell cover. Strict rows: (P - M'PM)/s >= t I. Non-strict rows (a saturator at slope 0):
P - M'PM >= 0. Float64 re-check: strict ||M||_P < 1; non-strict ||M||_P <= 1 + NS_TOL (reported).
"""
import itertools
import numpy as np
import cvxpy as cp

KMAX = 1e4
NS_TOL = 1e-9

def pn(P):
    w, V = np.linalg.eigh(P)
    S = V @ np.diag(np.sqrt(w)) @ V.T
    Si = V @ np.diag(1 / np.sqrt(w)) @ V.T
    return lambda M: float(np.linalg.norm(S @ M @ Si, 2))

def corners(lo, hi):
    return list(itertools.product(*zip(lo, hi)))

def certify_vertices(sys_, lo, hi, kind="sector", floor=None):
    rows = []
    for th in corners(lo, hi):
        for strict, M in sys_.vertices(th, kind, floor):
            rows.append((strict, M, th))
    n = rows[0][1].shape[0]
    P = cp.Variable((n, n), symmetric=True)
    t = cp.Variable()
    cons = [P >> np.eye(n), P << KMAX * np.eye(n)]
    for strict, M, _ in rows:
        Q = P - M.T @ P @ M
        Q = (Q + Q.T) / 2
        if strict:
            s = max(1.0 - float(np.max(np.abs(np.linalg.eigvals(M)))), 1e-12)
            cons.append(Q / s >> t * np.eye(n))
        else:
            cons.append(Q >> 0)
    prob = cp.Problem(cp.Maximize(t), cons)
    try:
        prob.solve(solver=cp.CLARABEL)
    except cp.error.SolverError:
        return {"status": "no proof", "reason": "solver error"}
    if P.value is None or t.value is None or t.value <= 0:
        return {"status": "no proof", "reason": f"LMI infeasible (t={t.value})"}
    Pv = (P.value + P.value.T) / 2
    f = pn(Pv)
    ws = max(f(M) for s, M, _ in rows if s)
    wn = max([f(M) for s, M, _ in rows if not s] or [0.0])
    if ws >= 1.0 or wn > 1.0 + NS_TOL:
        return {"status": "no proof", "reason": f"float64 check failed (strict {ws:.12f}, non-strict {wn:.12f})"}
    ev = np.linalg.eigvalsh(Pv)
    return {"status": "certified", "t": float(t.value), "kappa": float(ev[-1] / ev[0]),
            "worst_strict": ws, "worst_nonstrict": wn, "rows": len(rows), "P": Pv.tolist()}
