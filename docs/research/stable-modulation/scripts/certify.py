"""Certifier: prove a parameter-varying linear recurrence x[n+1] = A(theta[n]) x[n] (+ input) can't blow
up for ANY sequence theta[n] inside a box, by finding one matrix P with A(theta)^T P A(theta) < P on
the whole box (a common quadratic Lyapunov function, CQLF).

Steps (README section 8):
 1. LMI on a grid (cvxpy + Clarabel): P >= I, P <= KMAX*I, (P - Ai^T P Ai)/si >= t*I, maximise t.
    si = 1 - rho(Ai) rescales each constraint, so points where the filter barely decays (cutoff -> 0)
    are as well conditioned as the rest.
 2. Verify in float64, independent of the solver: ||P^1/2 A P^-1/2||_2 < 1 at every point used.
 3. Cover the whole box (not just the grid): split it into cells. On a cell, A(theta) lies within
    e_cell of the convex hull of its corner matrices (multilinear interpolation). The P-norm is convex,
    so ||A(theta)||_P <= max_corners ||A(c)||_P + e_cell. e_cell is estimated as 1.5x the largest
    interpolation error seen at the cell centre and edge midpoints (a declared heuristic, D2).
    A cell that fails is split; a corner whose own P-norm is >= 1 goes back to step 1 as a cutting
    plane. If the cover can't close within the cell budget, the result is "grid-certified" only.
"""
import itertools, math
import numpy as np
import cvxpy as cp

KMAX = 1e4
SAFETY = 1.5

def pnorm_factory(P):
    w, V = np.linalg.eigh(P)
    S = V @ np.diag(np.sqrt(w)) @ V.T
    Si = V @ np.diag(1 / np.sqrt(w)) @ V.T
    return lambda M: float(np.linalg.norm(S @ M @ Si, 2))

def solve_lmi(mats):
    n = mats[0].shape[0]
    P = cp.Variable((n, n), symmetric=True)
    t = cp.Variable()
    cons = [P >> np.eye(n), P << KMAX * np.eye(n)]
    for A in mats:
        s = max(1.0 - float(np.max(np.abs(np.linalg.eigvals(A)))), 1e-12)
        M = (P - A.T @ P @ A) / s
        cons.append((M + M.T) / 2 >> t * np.eye(n))
    prob = cp.Problem(cp.Maximize(t), cons)
    try:
        prob.solve(solver=cp.CLARABEL)
    except cp.error.SolverError:
        return None, -np.inf
    if P.value is None:
        return None, -np.inf
    Pv = (P.value + P.value.T) / 2
    return Pv, float(t.value)

class Box:
    """lo/hi per axis; axes listed in `log` are gridded and split in log space (cutoff, damping)."""
    def __init__(self, lo, hi, log=()):
        self.lo, self.hi, self.log = np.array(lo, float), np.array(hi, float), set(log)
    def to_u(self, x):
        return np.array([math.log(v) if j in self.log else v for j, v in enumerate(x)])
    def from_u(self, u):
        return tuple(math.exp(v) if j in self.log else float(v) for j, v in enumerate(u))
    @property
    def ulo(self): return self.to_u(self.lo)
    @property
    def uhi(self): return self.to_u(self.hi)
    def grid(self, per_axis):
        axes = [np.linspace(a, b, per_axis[j]) for j, (a, b) in enumerate(zip(self.ulo, self.uhi))]
        return [self.from_u(u) for u in itertools.product(*axes)]

def certify(Afun, box, grid=None, rounds=20, cell_budget=200000):
    d = len(box.lo)
    grid = grid or [25 if j in box.log else 9 for j in range(d)]
    pts = box.grid(grid)
    cache = {}
    def A(theta):
        key = tuple(round(v, 15) for v in theta)
        if key not in cache:
            cache[key] = Afun(*theta)
        return cache[key]
    info = {"rounds": 0, "lmi_points": len(pts)}
    for rnd in range(rounds):
        info["rounds"] = rnd + 1
        P, t = solve_lmi([A(p) for p in pts])
        if P is None or t <= 0:
            return {"status": "no proof", "reason": f"LMI infeasible (t={t:.3g})", **info}
        pn = pnorm_factory(P)
        bad = [p for p in pts if pn(A(p)) >= 1.0]
        if bad:  # solver tolerance; tighten by retrying is pointless, report
            return {"status": "no proof", "reason": f"solver P fails float64 check at {len(bad)} grid points", **info}
        # cover
        ok, cells, new_pts, worst = cover(A, pn, box, d, cell_budget)
        info.update(cells=cells, worst_bound=worst)
        if ok:
            ev = np.linalg.eigvalsh(P)
            return {"status": "certified", "P": P.tolist(), "t": t, "kappa": float(ev[-1] / ev[0]), **info}
        if new_pts:
            pts = pts + new_pts
            info["lmi_points"] = len(pts)
            continue
        ev = np.linalg.eigvalsh(P)
        return {"status": "grid-certified", "reason": "cover did not close within the cell budget",
                "P": P.tolist(), "t": t, "kappa": float(ev[-1] / ev[0]), **info}
    return {"status": "no proof", "reason": "cutting planes did not converge", **info}

def cover(A, pn, box, d, budget):
    """Return (ok, cells_checked, corner points with P-norm >= 1, worst bound seen)."""
    nrm = {}
    def N(u):
        key = tuple(np.round(u, 13))
        if key not in nrm:
            nrm[key] = pn(A(box.from_u(u)))
        return nrm[key]
    stack = [(box.ulo.copy(), box.uhi.copy())]
    cells, worst, failing = 0, 0.0, []
    corners_idx = list(itertools.product([0, 1], repeat=d))
    while stack:
        lo, hi = stack.pop()
        cells += 1
        if cells > budget:
            return False, cells, [], worst
        corners = [np.where(np.array(c) == 1, hi, lo) for c in corners_idx]
        cn = [N(c) for c in corners]
        if max(cn) >= 1.0:
            failing += [box.from_u(c) for c, v in zip(corners, cn) if v >= 1.0]
            if len(failing) > 20:
                break
            continue
        # interpolation error at centre and at every edge midpoint, measured in the P-norm
        mid = (lo + hi) / 2
        Am = [np.array(A(box.from_u(c))) for c in corners]
        err = pn(A(box.from_u(mid)) - sum(Am) / len(Am))
        for j in range(d):
            for c in corners_idx:
                if c[j] == 0:
                    a = np.where(np.array(c) == 1, hi, lo)
                    b = a.copy(); b[j] = hi[j]
                    m = (a + b) / 2
                    err = max(err, pn(A(box.from_u(m)) - (np.array(A(box.from_u(a))) + np.array(A(box.from_u(b)))) / 2))
        bound = max(cn) + SAFETY * err
        if bound < 1.0:
            worst = max(worst, bound)  # largest bound over the leaf cells that closed
            continue
        # split along the axis with the largest edge error relative to its own width
        j = int(np.argmax(hi - lo)) if d == 1 else split_axis(A, box, lo, hi, pn)
        m = (lo[j] + hi[j]) / 2
        a_hi = hi.copy(); a_hi[j] = m
        b_lo = lo.copy(); b_lo[j] = m
        stack += [(lo, a_hi), (b_lo, hi)]
    worst = max(worst, 1.0) if failing else worst
    return (not failing), cells, failing, worst

def split_axis(A, box, lo, hi, pn):
    errs = []
    for j in range(len(lo)):
        a = lo.copy(); b = lo.copy(); b[j] = hi[j]
        m = (a + b) / 2
        errs.append(pn(A(box.from_u(m)) - (A(box.from_u(a)) + A(box.from_u(b))) / 2))
    return int(np.argmax(errs))
