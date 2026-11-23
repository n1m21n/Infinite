"""Phase 6: exact checks of the H2 certificates and witnesses (reviewer follow-up, owner 2026-11-23).

Lower bounds. The four paper filters are trapezoidal: A = M^-1 N with M = I - g Ac, N = I + g Ac. M and N
commute, so A = N M^-1 and
    M^T (A^T P A - P) M = N^T P N - M^T P M = 2 g (Ac^T P + P Ac).
Hence ||A(g, k)||_P < 1 for every g > 0 iff S(k) = -(Ac(k)^T P + P Ac(k)) is positive definite (and then Ac
is Hurwitz, so M is invertible for every g > 0). Ac is affine in the knob, so S is affine in it, and positive
definiteness at both ends of the knob interval gives it on the whole interval (the PD cone is convex). The
check is therefore two exact rational LDL^T factorisations per certificate: no grid, no cover, no rounding,
and it covers g in (0, inf), wider than the certified box [1e-5, 1e3]. P is the certifier's own float64 P,
converted to rationals exactly (every float is a dyadic rational) and symmetrised exactly.

Upper bounds. The phase 0 witness for the Moog and the diode is a two-step switching sequence. Its knob
values are floats, hence exact rationals; the product of the two A matrices is computed exactly in Q, and the
roots of its characteristic polynomial are enclosed in Arb balls (python-flint). Spectral radius > 1 with the
ball's lower bound above 1 is a proof that the sequence grows. Writes data/phase6-exact.json.
"""
import json, pathlib, time
from fractions import Fraction as Q
import flint
from stabmod import discretise, FILTERS
from certify import certify, Box

DATA = pathlib.Path(__file__).resolve().parent.parent / "data"
G = (1e-5, 1e3)

def Ac_exact(name, p):
    one = Q(1)
    if name == "moog":
        return [[-one, 0, 0, -p], [one, -one, 0, 0], [0, one, -one, 0], [0, 0, one, -one]]
    if name == "diode":
        return [[-2 * one, one, 0, -p], [one, -2 * one, one, 0], [0, one, -2 * one, one], [0, 0, one, -one]]
    if name == "svf":
        return [[-2 * p, -one], [one, Q(0)]]
    if name == "sallenkey":
        return [[-one, -p], [one, p - 1]]

def ldl_pivots(S):
    """Exact LDL^T pivots of a symmetric rational matrix (None if a zero pivot appears)."""
    n = len(S)
    A = [[Q(x) for x in row] for row in S]
    piv = []
    for j in range(n):
        d = A[j][j]
        if d == 0:
            return None
        piv.append(d)
        for i in range(j + 1, n):
            f = A[i][j] / d
            for c in range(j, n):
                A[i][c] -= f * A[j][c]
    return piv

def pd(S):
    p = ldl_pivots(S)
    return p is not None and all(x > 0 for x in p), p

def S_of(Ac, P):
    n = len(P)
    return [[-(sum(Ac[r][i] * P[r][j] for r in range(n)) + sum(P[i][r] * Ac[r][j] for r in range(n)))
             for j in range(n)] for i in range(n)]

def check_cert(name, Pf, ends):
    n = len(Pf)
    P = [[(Q(Pf[i][j]) + Q(Pf[j][i])) / 2 for j in range(n)] for i in range(n)]
    okP, pivP = pd(P)
    out = {"P_pd": okP, "P_min_pivot": float(min(pivP)) if pivP else None, "ends": {}}
    ok = okP
    for e in ends:
        okS, piv = pd(S_of(Ac_exact(name, e), P))
        out["ends"][str(e)] = {"pd": okS, "min_pivot": float(min(piv)) if piv else None}
        ok = ok and okS
    out["proved"] = ok
    return out

def A_exact(name, w, k):
    w = Q(w); g = w / (1 - w)
    Ac = flint.fmpq_mat([[flint.fmpq(x.numerator, x.denominator) for x in map(Q, row)] for row in Ac_exact(name, Q(k))])
    n = Ac.nrows()
    I = flint.fmpq_mat(n, n, [1 if i == j else 0 for i in range(n) for j in range(n)])
    gq = flint.fmpq(g.numerator, g.denominator)
    return (I - Ac * gq).solve(I + Ac * gq)

def check_witness(name, seq):
    M = None
    for w, k in seq:
        A = A_exact(name, w, k)
        M = A if M is None else A * M
    cp = M.charpoly()
    roots = flint.fmpq_poly(cp.coeffs()).complex_roots()
    lows = []
    for r, _mult in roots:
        a = abs(r)
        lows.append(float(a.lower()) if hasattr(a, "lower") else float(a.mid() - a.rad()))
    best = max(lows)
    return {"seq": seq, "radius_lower_bound": best, "grows": best > 1.0}

if __name__ == "__main__":
    t0 = time.time()
    h2 = json.loads((DATA / "phase1-h2.json").read_text())
    b3 = json.loads((DATA / "phase0-b3.json").read_text())
    res = {}
    for name, key in (("moog", "certified_kmax"), ("diode", "certified_kmax"), ("sallenkey", "certified_kmax")):
        kmax = h2[name][key]
        r = certify(lambda g, p, nm=name: discretise(FILTERS[nm](p), g), Box([G[0], 0], [G[1], kmax], log=[0]))
        assert r["status"] == "certified", (name, r.get("reason"))
        res[name] = {"knob": "k", "interval": [0, kmax], **check_cert(name, r["P"], [Q(0), Q(str(kmax))])}
        print(name, "k in [0,", kmax, "] proved:", res[name]["proved"], flush=True)
    for rmin, box in h2["svf"]["boxes"].items():
        res["svf R>=" + rmin] = {"knob": "R", "interval": [float(rmin), 10.0],
                                 **check_cert("svf", box["P"], [Q(rmin), Q(10)])}
        print("svf R in [", rmin, ", 10] proved:", res["svf R>=" + rmin]["proved"], flush=True)
    for name in ("moog", "diode"):
        tr = [t for t in b3["oracle"][name]["trace"] if t["witness"]]
        w = min(tr, key=lambda t: t["kmax"])
        res[name]["witness"] = {"kmax": w["kmax"], **check_witness(name, w["seq"])}
        print(name, "witness at k =", w["kmax"], "radius >=", res[name]["witness"]["radius_lower_bound"],
              "grows:", res[name]["witness"]["grows"], flush=True)
    res["seconds"] = round(time.time() - t0, 1)
    (DATA / "phase6-exact.json").write_text(json.dumps(res, indent=1, default=str))
    print("wrote", DATA / "phase6-exact.json", res["seconds"], "s")
