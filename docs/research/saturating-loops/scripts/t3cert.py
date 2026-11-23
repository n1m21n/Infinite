"""T3: Lur'e-Lyapunov certificate for G1 (README sections 8-9; D3 as amended 2026-11-23).

V(x) = x'Px + 2 sum_{i in I} q_i F_i(c_i x),  F_i(y) = integral_0^y phi_i,  P >= 0, q >= 0.
I = saturators whose argument row c_i doesn't depend on the knobs and whose phi has slope in [0, mu]
(F_i convex and mu-smooth). For those, the smooth-convex inequality gives
    F(y+) - F(y) <= phi(y+)(y+ - y) - (phi(y+) - phi(y))^2 / (2 mu),
so with xi = (x, w, v), w = phi(y), v = phi(y+) for i in I, Delta V <= xi' D xi, a quadratic form.
S-procedure, multipliers lambda >= 0 common over the knob box:
    sector of w_i, sector of v_i, slope between (w_i, y_i) and (v_i, y+_i), and cross-pair slope
    bounds between two saturators of the same monotone shape at the same sample:
    (w_i - s w_j)(mu (y_i - s y_j) - (w_i - s w_j)) >= 0, s = +1, and s = -1 for odd shapes;
    and ratio bounds when two saturators of the same odd shape, concave for y > 0 (tanh, soft clip,
    clamp), see proportional arguments y_i = c(theta) y_j: phi(c y) lies between s phi(y) and |c| s phi(y)
    (s = sign c), i.e. (w_i - s w_j)(|c| s w_j - w_i) >= 0, linear in c.
Certificate: D(theta) + sum lambda Q(theta) + t s(theta) E_w'E_w <= 0 at every box corner, t > 0,
s(theta) = ||B(theta)||_2 (the decrease per sample scales with the loop gain, e.g. with g for a ladder),
so Delta V <= -t s |w|^2. That implies x -> 0 only if w -> 0 forces x -> 0: every C(theta) has full
column rank and every shape has phi(y) = 0 only at y = 0 (tanh, soft clip, clamp, abs; not sin). For
any other kernel the strict term also includes t s |x|^2 (`x_strict`). The form has exact null directions (w = v = 0 and x+ = x), so the float64
re-check accepts lambda_max <= TOL * ||F|| (TOL = 1e-9), recorded with every certificate. If the saturator arguments determine x (C has full column rank), V decreasing,
bounded below and radially unbounded gives sum |w_k|^2 < inf, so w -> 0, y -> 0 and x -> 0 under any
knob sequence.

Corners suffice when xi' F xi is convex along each knob axis: true when A, B are affine in the knobs,
C_I is knob-free and every shape has sector and slope lower end 0 (tanh, soft clip, clamp). Checked
again on a dense grid by `check_grid`.
"""
import itertools
import numpy as np
import cvxpy as cp
from satloops import SHAPES

TOL = 1e-9

def B_of(sys_, th):
    return sys_.ABC(*th)[1]

def _sym(a, b):
    a = np.atleast_2d(a); b = np.atleast_2d(b)
    return (a.T @ b + b.T @ a) / 2

def pairs(sys_):
    out = []
    sh = sys_.shapes
    for i in range(len(sh)):
        for j in range(i + 1, len(sh)):
            if sh[i] == sh[j] and SHAPES[sh[i]]["slope"][0] == 0.0:
                out.append((i, j, 1.0))
                if SHAPES[sh[i]].get("odd"):
                    out.append((i, j, -1.0))
    return out

def ratio(Ci, Cj):
    """c with Ci = c Cj, or None."""
    nj = float(Cj @ Cj)
    if nj == 0.0:
        return None
    c = float(Ci @ Cj) / nj
    return c if np.allclose(Ci, c * Cj, atol=1e-12) else None

def ratio_pairs(sys_, thetas):
    """(i, j, sign) for same-shape odd concave pairs whose argument rows are proportional at every
    corner with one sign of c."""
    out = []
    sh = sys_.shapes
    Cs = [sys_.ABC(*th)[2] for th in thetas]
    for i in range(len(sh)):
        for j in range(len(sh)):
            if i == j or sh[i] != sh[j] or not SHAPES[sh[i]].get("concave"):
                continue
            cs = [ratio(C[i], C[j]) for C in Cs]
            if any(c is None for c in cs):
                continue
            sg = {np.sign(c) for c in cs if c != 0}
            if len(sg) == 1:
                out.append((i, j, float(sg.pop())))
    return out

def cross_terms(sys_, Ew, Y, Ev, Yp, I, C=None, rp=()):
    """Quadratic forms of the cross-pair slope bounds, for w (all saturators) and v (those in I)."""
    Q = []
    for i, j, sg in pairs(sys_):
        mu = SHAPES[sys_.shapes[i]]["slope"][1]
        dw = Ew[i] - sg * Ew[j]; dy = Y[i] - sg * Y[j]
        Q.append(_sym(dw, mu * dy - dw))
        if i in I and j in I:
            a, b = I.index(i), I.index(j)
            dv = Ev[a] - sg * Ev[b]; dyp = Yp[i] - sg * Yp[j]
            Q.append(_sym(dv, mu * dyp - dv))
    for i, j, sg in rp:
        c = abs(ratio(C[i], C[j]))
        Q.append(_sym(Ew[i] - sg * Ew[j], c * sg * Ew[j] - Ew[i]))
    return Q

def build(sys_, theta, I, n, m):
    A, B, C = sys_.ABC(*theta)
    N = n + m + len(I)
    Ex = np.zeros((n, N)); Ex[:, :n] = np.eye(n)
    Ew = np.zeros((m, N)); Ew[:, n:n + m] = np.eye(m)
    Ev = np.zeros((len(I), N)); Ev[:, n + m:] = np.eye(len(I))
    Xp = A @ Ex + B @ Ew
    Y = C @ Ex
    Yp = C @ Xp
    return Ex, Ew, Ev, Xp, Y, Yp

def cells_of(lo, hi, cells, log_axes):
    """Split each axis into `cells[j]` pieces (geometric on log axes). Returns list of (lo, hi)."""
    edges = []
    for j, (a, b) in enumerate(zip(lo, hi)):
        c = cells[j] if cells else 1
        e = np.geomspace(a, b, c + 1) if (j in log_axes and a > 0 and c > 1) else np.linspace(a, b, c + 1)
        edges.append(list(zip(e[:-1], e[1:])))
    return [(tuple(x[0] for x in cell), tuple(x[1] for x in cell)) for cell in itertools.product(*edges)]

ZERO_ONLY_AT_0 = {"tanh", "soft", "clamp", "abs"}

def needs_x_strict(sys_, lo, hi):
    if not set(sys_.shapes) <= ZERO_ONLY_AT_0:
        return True
    for th in itertools.product(*zip(lo, hi)):
        C = sys_.ABC(*th)[2]
        if np.linalg.matrix_rank(C) < C.shape[1]:
            return True
    return False

def certify_t3(sys_, lo, hi, I=None, cells=None, log_axes=(0,)):
    th0 = tuple(lo)
    A, B, C = sys_.ABC(*th0)
    n, m = A.shape[0], len(sys_.shapes)
    if I is None:
        Cs = [sys_.ABC(*th)[2] for th in itertools.product(*zip(lo, hi))]
        I = [i for i in range(m) if SHAPES[sys_.shapes[i]]["slope"][0] == 0.0
             and all(np.allclose(Ci[i], Cs[0][i]) for Ci in Cs)]
    P = cp.Variable((n, n), symmetric=True)
    q = cp.Variable(len(I), nonneg=True)
    t = cp.Variable()
    rp = ratio_pairs(sys_, list(itertools.product(*zip(lo, hi))))
    ncross = len(cross_terms(sys_, *[build(sys_, th0, I, n, m)[k] for k in (1, 4, 2, 5)], I, C, rp))
    xs = needs_x_strict(sys_, lo, hi)
    cons = [P >> 0, cp.trace(P) + cp.sum(q) == 1]
    cell_list = cells_of(lo, hi, cells, log_axes)
    mults = []
    rows = []
    for clo, chi in cell_list:
        mu_ = {"w": cp.Variable(m, nonneg=True), "v": cp.Variable(len(I), nonneg=True),
               "s": cp.Variable(len(I), nonneg=True), "c": cp.Variable(max(ncross, 1), nonneg=True)}
        mults.append(mu_)
        for th in itertools.product(*zip(clo, chi)):
            rows.append((th, mu_))
    for th, mu_ in rows:
        lam_w, lam_v, lam_s, lam_c = mu_["w"], mu_["v"], mu_["s"], mu_["c"]
        Ex, Ew, Ev, Xp, Y, Yp = build(sys_, th, I, n, m)
        F = Xp.T @ P @ Xp - Ex.T @ P @ Ex
        for j, i in enumerate(I):
            mu = SHAPES[sys_.shapes[i]]["slope"][1]
            dy = Yp[i] - Y[i]; dw = Ev[j] - Ew[i]
            F = F + q[j] * (2 * _sym(Ev[j], dy) - np.outer(dw, dw) / mu)
        for i in range(m):
            a, b = SHAPES[sys_.shapes[i]]["sector"]
            F = F + lam_w[i] * _sym(Ew[i] - a * Y[i], b * Y[i] - Ew[i])
        for j, i in enumerate(I):
            a, b = SHAPES[sys_.shapes[i]]["sector"]
            s0, s1 = SHAPES[sys_.shapes[i]]["slope"]
            F = F + lam_v[j] * _sym(Ev[j] - a * Yp[i], b * Yp[i] - Ev[j])
            dy = Yp[i] - Y[i]; dw = Ev[j] - Ew[i]
            F = F + lam_s[j] * _sym(dw - s0 * dy, s1 * dy - dw)
        Cth = sys_.ABC(*th)[2]
        for c, Qc in enumerate(cross_terms(sys_, Ew, Y, Ev, Yp, I, Cth, rp)):
            F = F + lam_c[c] * Qc
        F = F + t * np.linalg.norm(B_of(sys_, th), 2) * (Ew.T @ Ew + (Ex.T @ Ex if xs else 0))
        F = (F + F.T) / 2
        cons.append(F << 0)
    prob = cp.Problem(cp.Maximize(t), cons)
    try:
        prob.solve(solver=cp.CLARABEL)
    except cp.error.SolverError:
        return {"status": "no proof", "reason": "solver error", "I": I}
    if t.value is None or t.value <= 1e-9:
        return {"status": "no proof", "reason": f"LMI infeasible (t={t.value})", "I": I}
    cert = {"P": P.value, "q": q.value, "t": float(t.value), "I": I, "rp": rp, "x_strict": xs,
            "cells": [(clo, chi, {k: v.value for k, v in mu_.items()}) for (clo, chi), mu_ in zip(cell_list, mults)]}
    worst = max(check(sys_, cert, list(itertools.product(*zip(clo, chi))), cm) for clo, chi, cm in cert["cells"])
    ok = worst <= TOL
    ev = np.linalg.eigvalsh((cert["P"] + cert["P"].T) / 2)
    return {"status": "certified" if ok else "no proof",
            "reason": "" if ok else f"float64 re-check failed ({worst:.3g})",
            "t": cert["t"], "worst": worst, "ncells": len(cell_list), "x_strict": xs, "P_min_eig": float(ev[0]), "q": cert["q"].tolist(), "I": I,
            "cert": cert}

def check(sys_, cert, thetas, cm):
    """Largest eigenvalue of D + sum lambda Q + (t/2) E_w'E_w over the given knob points (float64).
    Negative means Delta V <= -(t/2)|w|^2 holds there."""
    P, q, I = cert["P"], cert["q"], cert["I"]
    worst = -np.inf
    for th in thetas:
        A, B, C = sys_.ABC(*th)
        n, m = A.shape[0], len(sys_.shapes)
        Ex, Ew, Ev, Xp, Y, Yp = build(sys_, th, I, n, m)
        F = Xp.T @ P @ Xp - Ex.T @ P @ Ex
        for j, i in enumerate(I):
            mu = SHAPES[sys_.shapes[i]]["slope"][1]
            dy = Yp[i] - Y[i]; dw = Ev[j] - Ew[i]
            F += q[j] * (2 * _sym(Ev[j], dy) - np.outer(dw, dw) / mu)
        for i in range(m):
            a, b = SHAPES[sys_.shapes[i]]["sector"]
            F += cm["w"][i] * _sym(Ew[i] - a * Y[i], b * Y[i] - Ew[i])
        for j, i in enumerate(I):
            a, b = SHAPES[sys_.shapes[i]]["sector"]
            s0, s1 = SHAPES[sys_.shapes[i]]["slope"]
            F += cm["v"][j] * _sym(Ev[j] - a * Yp[i], b * Yp[i] - Ev[j])
            dy = Yp[i] - Y[i]; dw = Ev[j] - Ew[i]
            F += cm["s"][j] * _sym(dw - s0 * dy, s1 * dy - dw)
        for c, Qc in enumerate(cross_terms(sys_, Ew, Y, Ev, Yp, I, C, cert["rp"])):
            F += cm["c"][c] * Qc
        F += cert["t"] / 2 * np.linalg.norm(B, 2) * (Ew.T @ Ew + (Ex.T @ Ex if cert.get("x_strict") else 0))
        ev = np.linalg.eigvalsh((F + F.T) / 2)
        worst = max(worst, float(ev[-1]) / max(1.0, float(np.abs(ev).max())))
    return worst
