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

def locked_orbit(sys_, seq, slot, axis, box, x0, N, iters=60, tol=1e-10):
    """Periodic stuck orbit with one knob value tuned so the orbit closes after N repeats of `seq`.

    Unknowns: the state x and knob `axis` of `seq[slot]`. Equations: F^N(x) - x = 0 plus the phase
    condition x[0] = 0. The tuned knob must stay inside [box[0][axis], box[1][axis]]. Returns
    (seq_tuned, x, residual) or None.
    """
    n = len(x0)
    def F(z):
        s = [list(t) for t in seq]
        s[slot][axis] = z[n]
        return simulate(sys_, [tuple(t) for t in s], z[:n], N) - z[:n]
    def G(z):
        return np.concatenate([F(z), [z[0]]])
    z = np.concatenate([x0, [seq[slot][axis]]])
    for _ in range(iters):
        r = G(z)
        if np.linalg.norm(r) < tol:
            break
        J = np.empty((n + 1, n + 1))
        for j in range(n + 1):
            h = 1e-7 * max(1.0, abs(z[j]))
            e = np.zeros(n + 1); e[j] = h
            J[:, j] = (G(z + e) - G(z - e)) / (2 * h)
        try:
            dz = np.linalg.lstsq(J, -r, rcond=None)[0]
        except np.linalg.LinAlgError:
            return None
        step = 1.0
        while step > 1e-4 and np.linalg.norm(G(z + step * dz)) > np.linalg.norm(r):
            step /= 2
        z = z + step * dz
    res = float(np.linalg.norm(F(z)))
    lo, hi = box[0][axis], box[1][axis]
    if res < tol and lo <= z[n] <= hi and np.linalg.norm(z[:n]) > 1e-3:
        s = [list(t) for t in seq]; s[slot][axis] = float(z[n])
        return s, z[:n], res
    return None

def settle(sys_, seq, x0, samples=8192):
    x = np.array(x0, float)
    for _ in range(max(1, samples // len(seq))):
        x = simulate(sys_, seq, x, 1)
    return x

def phase_zero(sys_, seq, x, max_steps=4096):
    """Step along the orbit (whole sequences) until x[0] changes sign, then return the state nearest 0."""
    prev = x
    for _ in range(max_steps):
        nx = simulate(sys_, seq, prev, 1)
        if np.sign(nx[0]) != np.sign(prev[0]):
            return nx if abs(nx[0]) < abs(prev[0]) else prev
        prev = nx
    return x

def run_batch(sys_, seq, X, repeats):
    """Run many starts at once (columns of X). Returns X and a per-column grew flag (non-finite or > 1e6 x start)."""
    mats = [sys_.ABC(*th) for th in seq]
    fs = [SHAPES[s]["f"] for s in sys_.shapes]
    n0 = np.linalg.norm(X, axis=0)
    grew = np.zeros(X.shape[1], bool)
    for r in range(repeats):
        for A, B, C in mats:
            Y = C @ X
            W = np.vstack([f(Y[i]) for i, f in enumerate(fs)])
            X = A @ X + B @ W
        if r % 64 == 63:
            nx = np.linalg.norm(X, axis=0)
            bad = ~np.isfinite(nx) | (nx > 1e6 * n0)
            grew |= bad
            X[:, bad] = 0.0
            if np.all(nx[~bad] < 1e-9):
                break
    return X, grew

def genuine(sys_, seq, x, N, cap=1e6):
    """Rig-bug guard for 'sticks' witnesses: amplitude at most cap, and at every step of the orbit the
    float64 update fl(x+) - x matches the true increment (A - I) x + B w to 1e-6 relative. Otherwise the
    'orbit' can be float64 absorption: a huge state whose small increments are lost to rounding."""
    x = np.array(x, float)
    if not np.all(np.isfinite(x)) or np.linalg.norm(x) > cap:
        return False
    for _ in range(N):
        for th in seq:
            A, B, C = sys_.ABC(*th)
            w = np.array([SHAPES[s]["f"](v) for s, v in zip(sys_.shapes, C @ x)])
            d = (A - np.eye(len(x))) @ x + B @ w
            xn = A @ x + B @ w
            if np.linalg.norm((xn - x) - d) > 1e-6 * np.linalg.norm(d) + 1e-14:
                return False
            x = xn
    return True

def _try_stick(sys_, seq, x, lo, hi):
    w = _try_stick_raw(sys_, seq, x, lo, hi)
    if w and not genuine(sys_, [tuple(t) for t in w["seq"]], w["x"], w["repeats"]):
        return None
    return w

def _try_stick_raw(sys_, seq, x, lo, hi):
    from fractions import Fraction
    v = []; xr = x.copy(); j0 = int(np.argmax(np.abs(x)))
    for _ in range(4096):
        xr = simulate(sys_, seq, xr, 1); v.append(xr[j0])
    z = np.nonzero(np.diff(np.sign(v)) != 0)[0]
    if len(z) < 3:
        for N in (1, 2):
            xs, res = newton_orbit(lambda y: simulate(sys_, seq, y, N), xr)
            if res < 1e-10 and np.linalg.norm(xs) > 1e-3:
                return {"kind": "sticks", "seq": [list(t) for t in seq], "repeats": N, "x": xs.tolist(),
                        "residual": float(res), "amplitude": float(np.linalg.norm(xs))}
        return None
    ang = float(np.pi * (len(z) - 1) / (z[-1] - z[0]))
    Ns = sorted({Fraction(ang / (2 * np.pi)).limit_denominator(d).denominator
                 for d in (8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096)})
    xp = xr
    for _ in range(4096):
        nx = simulate(sys_, seq, xp, 1)
        if np.sign(nx[0]) != np.sign(xp[0]):
            xp = nx if abs(nx[0]) < abs(xp[0]) else xp
            break
        xp = nx
    free = [(sl, ax) for sl in range(len(seq)) for ax in range(len(lo)) if lo[ax] != hi[ax]]
    for N in Ns:
        for sl, ax in free:
            out = locked_orbit(sys_, seq, sl, ax, (lo, hi), xp, N)
            if out:
                s, xs, res = out
                return {"kind": "sticks", "seq": s, "repeats": N, "x": xs.tolist(), "residual": float(res),
                        "amplitude": float(np.linalg.norm(xs))}
    return None

def oracle(sys_, lo, hi, log_axes, rng, grid=7, starts=12, random_starts=200, samples=1 << 16):
    """General witness search (D1). Sequences: Report 2's linear witness search on the small-signal matrix
    over a grid of the box, plus frozen corners. Starts per sequence: 0.3 and 30, plus 200 random starts
    (log-uniform amplitude 1e-2..1e2) spread over the sequences, run in one batch for 2^16 samples. A start
    past 1e6 x its size is a 'grows' witness; a start still above 1e-3 gets a Newton-refined periodic orbit
    (a locked orbit with one knob tuned inside its range when it rotates)."""
    axes = []
    for j, (a, b) in enumerate(zip(lo, hi)):
        if a == b:
            axes.append([a])
        elif j in log_axes and a > 0:
            axes.append(list(np.geomspace(a, b, grid)))
        else:
            axes.append(list(np.linspace(a, b, grid)))
    cands = [tuple(c) for c in itertools.product(*axes)]
    seqs = find_witness(lambda t: sys_.linear(t), cands, rng, max_len=6, starts=starts, top=6)
    seqs = [[cands[i] for i in sq] for _, sq in seqs]
    seqs += [[tuple(c)] for c in itertools.product(*[(a, b) if a != b else (a,) for a, b in zip(lo, hi)])]
    n = sys_.ABC(*seqs[0][0])[0].shape[0]
    per = int(np.ceil(random_starts / len(seqs)))
    for seq in seqs:
        amps = np.concatenate([[0.3, 30.0], 10 ** rng.uniform(-2, 2, per)])
        D = rng.normal(size=(n, len(amps))); D /= np.linalg.norm(D, axis=0)
        X0 = D * amps
        X, grew = run_batch(sys_, seq, X0.copy(), max(1, samples // len(seq)))
        if grew.any():
            j = int(np.argmax(grew))
            return {"kind": "grows", "seq": [list(t) for t in seq], "start": X0[:, j].tolist()}
        nx = np.linalg.norm(X, axis=0)
        for j in np.argsort(-nx)[:3]:
            if nx[j] < 1e-3:
                break
            w = _try_stick(sys_, seq, X[:, j], lo, hi)
            if w:
                return w
    return None
