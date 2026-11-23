"""Kernel generator for corpora G (calibration) and B (blind, D4): random linear recurrences
next = A(theta) state + b in, with A's entries random functions of 1-3 knobs.

Each entry a_ij = c0 + sum over a few terms c * f(theta_p), f in {lin, sin, sq, exp, select}.
`select` is a switch: one value below a threshold, another above (a switched system).
Half the kernels are scaled so the frozen spectral radius is <= 0.98 at every grid point (stable at every
fixed knob setting: the interesting half, where the frozen check says "safe"); the other half are scaled
so the worst frozen radius lands in [0.9, 1.15].

Specs are plain JSON so phase 2 can emit Field code from the same file. Knobs are theta in [0, 1];
phase 2 maps them to declared ranges.
"""
import json, sys, pathlib
import numpy as np

FUNCS = ["lin", "sin", "sq", "exp", "select"]

def f_eval(kind, x, thr):
    if kind == "lin": return x
    if kind == "sin": return np.sin(np.pi * x)
    if kind == "sq": return x * x
    if kind == "exp": return np.exp(x) - 1.0
    if kind == "select": return 1.0 if x > thr else 0.0
    raise ValueError(kind)

def A_from_spec(spec, theta):
    n = spec["n"]
    A = np.zeros((n, n))
    for (i, j, c0, terms) in spec["entries"]:
        v = c0
        for (c, kind, p, thr) in terms:
            v += c * f_eval(kind, theta[p], thr)
        A[i, j] = v
    return spec["scale"] * A

def frozen_grid(d, per=7):
    axes = [np.linspace(0, 1, per)] * d
    return [tuple(x) for x in np.array(np.meshgrid(*axes, indexing="ij")).reshape(d, -1).T]

def make(rng, n_max, depth_terms, p_select):
    n = int(rng.integers(1, n_max + 1))
    d = int(rng.integers(1, 4))
    entries = []
    for i in range(n):
        for j in range(n):
            if i != j and rng.random() > 0.55:
                continue
            c0 = float(rng.normal(0, 0.6)) + (0.5 if i == j else 0.0)
            terms = []
            for _ in range(int(rng.integers(0, depth_terms + 1))):
                kind = "select" if rng.random() < p_select else str(rng.choice(FUNCS[:4]))
                terms.append((float(rng.normal(0, 0.7)), kind, int(rng.integers(0, d)), float(rng.uniform(0.2, 0.8))))
            entries.append((i, j, c0, terms))
    return {"n": n, "d": d, "entries": entries, "scale": 1.0}

def generate(seed, count, n_max, depth_terms, p_select):
    rng = np.random.default_rng(seed)
    out = []
    while len(out) < count:
        spec = make(rng, n_max, depth_terms, p_select)
        if not any(t for (_, _, _, t) in spec["entries"]):
            continue  # must depend on a knob
        rho = max(float(np.max(np.abs(np.linalg.eigvals(A_from_spec(spec, th))))) for th in frozen_grid(spec["d"]))
        if rho < 1e-3:
            continue
        stable_half = len(out) % 2 == 0
        target = 0.98 if stable_half else float(rng.uniform(0.9, 1.15))
        spec["scale"] = target / rho
        spec["id"] = f"g{seed % 1000:03d}-{len(out):03d}"
        spec["built_frozen_stable"] = stable_half
        out.append(spec)
    return out

if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "G"
    if which == "G":
        specs = generate(seed=20261121, count=200, n_max=6, depth_terms=2, p_select=0.15)
    else:
        raise SystemExit("B is generated only after the scorer is locked (phase 4, D4)")
    p = pathlib.Path(__file__).resolve().parent.parent / "corpus" / f"{which}.json"
    p.parent.mkdir(exist_ok=True)
    p.write_text(json.dumps(specs, indent=0))
    print("wrote", p, len(specs))
