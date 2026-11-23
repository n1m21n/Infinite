"""Shared code for the stable-modulation study: filters from McClellan (DAFx26) and the instability oracle.

Filters are built straight from the paper's continuous-time matrices and its trapezoidal
discretisation, eq. (6): A = (I - g Ac)^-1 (I + g Ac). The cutoff parameter g in (0, inf) is
mapped to w = g / (1 + g) in [0, 1] so the parameter box is closed (w = 1 is g -> inf, A -> -I).
"""
import numpy as np

def g_of_w(w):
    w = np.minimum(w, 1.0 - 1e-12)
    return w / (1.0 - w)

def discretise(Ac, g):
    n = Ac.shape[-1]
    I = np.eye(n)
    return np.linalg.solve(I - g * Ac, I + g * Ac)

def moog_Ac(k):
    return np.array([[-1, 0, 0, -k], [1, -1, 0, 0], [0, 1, -1, 0], [0, 0, 1, -1]], float)

def diode_Ac(k):
    return np.array([[-2, 1, 0, -k], [1, -2, 1, 0], [0, 1, -2, 1], [0, 0, 1, -1]], float)

def svf_Ac(R):
    """Eq. (13): low-pass SVF, R = damping (lower = more resonance)."""
    return np.array([[-2 * R, -1], [1, 0]], float)

def sallenkey_Ac(k):
    """Eq. (15): two one-poles with feedback gain k; LTI stable for k < 2."""
    return np.array([[-1, -k], [1, k - 1]], float)

FILTERS = {"moog": moog_Ac, "diode": diode_Ac, "svf": svf_Ac, "sallenkey": sallenkey_Ac}

def A_of(name, w, k):
    return discretise(FILTERS[name](k), g_of_w(w))

def radius(M):
    return float(np.max(np.abs(np.linalg.eigvals(M))))

def periodic_radius(mats):
    """Spectral radius of the product, normalised per step (rho(prod)^(1/len))."""
    P = np.eye(mats[0].shape[0])
    for M in mats:
        P = M @ P
    return radius(P) ** (1.0 / len(mats)), radius(P)

def find_witness(Afun, cands, rng, max_len=8, starts=60, top=6):
    """Search periodic switching sequences over the candidate parameter points.

    Afun(c) -> state matrix for candidate c. Returns up to `top` distinct sequences with the highest
    per-step growth rho(product)^(1/len), each as (rho(product), [candidate indices]). A witness is a
    product whose spectral radius exceeds 1 + tol: repeating that sequence grows the state without bound.
    Candidates should exclude points where A is exactly marginal (radius 1, e.g. g = 0 or g -> inf):
    they can't be witnesses and would crowd out the real near-misses.
    """
    mats = np.array([Afun(c) for c in cands])
    m, n = len(cands), mats.shape[1]
    found = {}
    def keep(r, seq):
        key = tuple(seq)
        found[key] = max(found.get(key, 0.0), r)
    r1 = np.max(np.abs(np.linalg.eigvals(mats)), axis=1)
    for i in np.argsort(-r1)[:top]:
        keep(float(r1[i]), [int(i)])
    prod2 = np.einsum("aij,bjk->abik", mats, mats).reshape(m * m, n, n)
    r2 = np.max(np.abs(np.linalg.eigvals(prod2)), axis=1)
    for j in np.argsort(-r2)[:top]:
        keep(float(r2[j]), [int(j % m), int(j // m)])
    for L in range(3, max_len + 1):
        for _ in range(starts):
            seq = [int(x) for x in rng.integers(0, m, L)]
            cur, improved = 0.0, True
            while improved:
                improved = False
                for pos in range(L):
                    pre = np.eye(n)
                    for q in seq[:pos]:
                        pre = mats[q] @ pre
                    post = np.eye(n)
                    for q in seq[pos + 1:]:
                        post = mats[q] @ post
                    r = np.max(np.abs(np.linalg.eigvals(post @ mats @ pre)), axis=1)
                    s = int(np.argmax(r))
                    if r[s] > cur + 1e-12:
                        cur = float(r[s]); improved = improved or seq[pos] != s; seq[pos] = s
            keep(cur, seq)
    ranked = sorted(found.items(), key=lambda kv: -(kv[1] ** (1.0 / len(kv[0]))))
    return [(r, list(seq)) for seq, r in ranked[:top]]
