"""Corpus G for C1 (README section 6): Report 2's linear generator plus 1-3 saturators on loop paths.

Each kernel starts as a Report 2 spec, x+ = A(theta) x, theta in [0, 1]^d (half of them frozen-stable
everywhere, as Report 2 builds them). Saturators are then placed on loop paths in one of two ways:

  row    : state i's whole update saturates,   x_i+ = phi(a_i(theta) . x)   (a stage output, as in a ladder)
  column : state j is saturated where it is read, every other row sees phi(x_j) instead of x_j

Both keep the small-signal matrix (every saturator at slope 1) equal to Report 2's A(theta), so the
frozen check and B4 (linearise and hope) see exactly the kernel Report 2 generated. Written as a Lur'e
model x+ = A'(theta) x + B(theta) w, w = phi(C(theta) x).

Shapes are drawn from the D2 library: tanh .30, soft .20, clamp .20, sin .15, abs .15.
G: seed 20261122, n <= 6, 1-3 saturators. B (D5) is generated only after the scorer lock.
"""
import json, sys, pathlib
import numpy as np

R2 = pathlib.Path(__file__).resolve().parents[2] / "stable-modulation" / "scripts"
sys.path.insert(0, str(R2))
from gen_kernels import generate, A_from_spec  # noqa: E402
from satloops import Lure  # noqa: E402

SHAPE_P = {"tanh": 0.30, "soft": 0.20, "clamp": 0.20, "sin": 0.15, "abs": 0.15}

def place(rng, spec, m_max):
    n = spec["n"]
    A0 = A_from_spec(spec, [0.5] * spec["d"])
    nz_rows = [i for i in range(n) if any(e[0] == i for e in spec["entries"])]
    nz_cols = [j for j in range(n) if any(e[1] == j for e in spec["entries"])]
    m = int(rng.integers(1, m_max + 1))
    sats, used = [], set()
    for _ in range(m * 4):
        if len(sats) == m:
            break
        kind = "row" if rng.random() < 0.5 else "column"
        pool = nz_rows if kind == "row" else nz_cols
        if not pool:
            continue
        idx = int(rng.choice(pool))
        if (kind, idx) in used:
            continue
        used.add((kind, idx))
        shape = str(rng.choice(list(SHAPE_P), p=list(SHAPE_P.values())))
        sats.append({"kind": kind, "index": idx, "shape": shape})
    del A0
    return sats

def lure(spec):
    """Lur'e model of a C1 spec. Rows are cut first; a column saturator only takes entries of rows
    that are not themselves saturated."""
    n = spec["n"]
    sats = spec["sats"]
    R = [s["index"] for s in sats if s["kind"] == "row"]
    def ABC(*th):
        A = A_from_spec(spec, th)
        Ap = A.copy()
        Bc, Cr = [], []
        for s in sats:
            if s["kind"] == "row":
                i = s["index"]
                c = A[i].copy(); Ap[i] = 0.0
                b = np.zeros(n); b[i] = 1.0
                Bc.append(b); Cr.append(c)
        for s in sats:
            if s["kind"] == "column":
                j = s["index"]
                b = Ap[:, j].copy(); b[R] = 0.0
                Ap[[r for r in range(n) if r not in R], j] = 0.0
                c = np.zeros(n); c[j] = 1.0
                Bc.append(b); Cr.append(c)
        return Ap, np.array(Bc).T, np.array(Cr)
    order = [s for s in sats if s["kind"] == "row"] + [s for s in sats if s["kind"] == "column"]
    return Lure(spec["id"], ABC, [s["shape"] for s in order])

def breaks(spec):
    b = {}
    for (_, _, _, terms) in spec["entries"]:
        for (_, kind, p, thr) in terms:
            if kind == "select":
                b.setdefault(p, set()).add(thr)
    return {p: sorted(v) for p, v in b.items()}

def affine(spec):
    return all(kind == "lin" for (_, _, _, terms) in spec["entries"] for (_, kind, _, _) in terms)

def make_set(seed, count, n_max, m_max, p_select):
    base = generate(seed=seed, count=count, n_max=n_max, depth_terms=2, p_select=p_select)
    rng = np.random.default_rng(seed + 1)
    for s in base:
        s["sats"] = place(rng, s, m_max)
        s["id"] = "s" + s["id"][1:]
        # check: small-signal matrix equals Report 2's A
        L = lure(s)
        th = rng.uniform(0, 1, s["d"])
        assert np.allclose(L.linear(th), A_from_spec(s, th), atol=1e-12), s["id"]
    return base

if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "G"
    if which == "G":
        specs = make_set(seed=20261122, count=200, n_max=6, m_max=3, p_select=0.15)
    elif which == "B":
        specs = make_set(seed=20261124, count=200, n_max=8, m_max=4, p_select=0.30)  # D5, after the lock
    else:
        raise SystemExit("sets: G, B")
    p = pathlib.Path(__file__).resolve().parent.parent / "corpus" / f"{which}.json"
    p.parent.mkdir(exist_ok=True)
    p.write_text(json.dumps(specs, indent=0))
    print("wrote", p, len(specs))
