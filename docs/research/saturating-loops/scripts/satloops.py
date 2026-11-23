"""Shared code for the saturating-loops study (C1): Lur'e models, vertex matrices and the oracle.

A kernel with zero input is x+ = A x + B phi(C x), phi applied elementwise (README section 8).
Report 2's certifier and witness search are reused from docs/research/stable-modulation/scripts.
"""
import itertools, pathlib, sys
import numpy as np

R2 = pathlib.Path(__file__).resolve().parents[2] / "stable-modulation" / "scripts"
sys.path.insert(0, str(R2))
from stabmod import radius, find_witness  # noqa: E402,F401
import certify as R2cert  # noqa: E402,F401

# ---- shape library (README section 8): sector [a, b], slope range [smin, smax] -----------------
SHAPES = {
    "tanh":    {"f": np.tanh, "sector": (0.0, 1.0), "slope": (0.0, 1.0), "odd": True, "concave": True},
    "soft":    {"f": lambda y: y / (1 + np.abs(y)), "sector": (0.0, 1.0), "slope": (0.0, 1.0), "odd": True, "concave": True},
    "clamp":   {"f": lambda y: np.clip(y, -1.0, 1.0), "sector": (0.0, 1.0), "slope": (0.0, 1.0), "odd": True, "concave": True},
    "sin":     {"f": np.sin, "sector": (-0.21723362821122166, 1.0), "slope": (-1.0, 1.0)},
    "abs":     {"f": np.abs, "sector": (-1.0, 1.0), "slope": (-1.0, 1.0)},
    "id":      {"f": lambda y: y, "sector": (1.0, 1.0), "slope": (1.0, 1.0)},
}

class Lure:
    """x+ = A(th) x + B(th) w, w_i = phi_i(y_i), y = C(th) x. ABC(th) -> (A, B, C)."""
    def __init__(self, name, ABC, shapes):
        self.name, self.ABC, self.shapes = name, ABC, list(shapes)
    @property
    def m(self): return len(self.shapes)
    def step(self, x, th):
        A, B, C = self.ABC(*th)
        y = C @ x
        w = np.array([SHAPES[s]["f"](v) for s, v in zip(self.shapes, y)])
        return A @ x + B @ w
    def linear(self, th):
        """Small-signal matrix at x = 0 (every saturator has slope 1 at 0, except abs)."""
        A, B, C = self.ABC(*th)
        return A + B @ C
    def vertices(self, th, kind="sector", floor=None):
        """Vertex matrices A + B diag(d) C over the sector (or slope) box. floor: per-saturator lower
        bound replacing a 0 end (the amplitude-bounded fallback). Returns list of (strict, M)."""
        A, B, C = self.ABC(*th)
        ends = []
        for i, s in enumerate(self.shapes):
            lo, hi = SHAPES[s][kind]
            if floor is not None and lo == 0.0:
                lo = floor[i]
            ends.append((lo, hi) if lo != hi else (lo,))
        out = []
        for d in itertools.product(*ends):
            strict = all(v != 0.0 for v in d)
            out.append((strict, A + B @ np.diag(d) @ C))
        return out

# ---- literature filters -----------------------------------------------------------------------
def moog_ABC(g, k):
    """Explicit tanh Moog ladder: y_i+ = y_i + g (tanh(y_{i-1}) - tanh(y_i)), y_0 = in - k y_4.
    Forward Euler with g = 2 pi fc / fs, and the previous-state g/(1+g) form, are this same map
    with different g ranges. w = [tanh(-k y4), tanh(y1), ..., tanh(y4)]."""
    A = np.eye(4)
    B = np.zeros((4, 5))
    for i in range(4):
        B[i, i] += g       # tanh of the stage before (w_0 is the input saturator)
        B[i, i + 1] -= g   # tanh of this stage
    C = np.zeros((5, 4))
    C[0, 3] = -k
    C[1:, :] = np.eye(4)
    return A, B, C

def moog(): return Lure("moog-tanh", moog_ABC, ["tanh"] * 5)

# ---- oracle -----------------------------------------------------------------------------------
def simulate(sys_, seq, x0, periods):
    x = np.array(x0, float)
    for _ in range(periods):
        for th in seq:
            x = sys_.step(x, th)
    return x

def period_map(sys_, seq):
    return lambda x: simulate(sys_, seq, x, 1)

def newton_orbit(F, x0, iters=50, tol=1e-10):
    """Solve F(x) = x by Newton with a finite-difference Jacobian. Returns (x, residual)."""
    x = np.array(x0, float)
    n = len(x)
    for _ in range(iters):
        r = F(x) - x
        res = float(np.linalg.norm(r))
        if res < tol:
            return x, res
        J = np.empty((n, n))
        h = 1e-7 * max(1.0, float(np.linalg.norm(x)))
        for j in range(n):
            e = np.zeros(n); e[j] = h
            J[:, j] = (F(x + e) - F(x - e)) / (2 * h)
        try:
            dx = np.linalg.solve(J - np.eye(n), -r)
        except np.linalg.LinAlgError:
            return x, res
        x = x + dx
    return x, float(np.linalg.norm(F(x) - x))

def stick_witness(sys_, seq, x0, settle=4096, amp=1e-3, tol=1e-10):
    """Run the periodic switching sequence from x0; if the state doesn't decay, refine a periodic orbit.
    Returns dict or None. A grows-witness is ||x|| > 1e6 ||x0||."""
    n0 = float(np.linalg.norm(x0))
    x = np.array(x0, float)
    per = max(1, settle // len(seq))
    for _ in range(per):
        x = simulate(sys_, seq, x, 1)
        if not np.all(np.isfinite(x)) or np.linalg.norm(x) > 1e6 * n0:
            return {"kind": "grows", "seq": [list(t) for t in seq]}
    if np.linalg.norm(x) < amp:
        return None
    xs, res = newton_orbit(period_map(sys_, seq), x, tol=tol)
    if res < tol and np.linalg.norm(xs) > amp:
        orbit = [xs]
        for th in seq[:-1]:
            orbit.append(sys_.step(orbit[-1], th))
        a = max(float(np.linalg.norm(v)) for v in orbit)
        return {"kind": "sticks", "seq": [list(t) for t in seq], "x": xs.tolist(), "residual": res, "amplitude": a}
    return None
