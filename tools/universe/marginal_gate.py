#!/usr/bin/env python3
"""Marginal-value gate: is the next batch of renders worth its time?

    marginal_gate.py --engine "u-he diva" --root /Library/Audio/Presets/u-he/Diva --renders preset_diva.jsonl --prefix state:diva: [--cost 6.3]

Proxy task = what the data is for: from a preset's knob values, predict how it sounds (C3 row: 32 spectrum bands + scalars).
Learning curve: ridge trained on N rendered presets, scored on a fixed held-out 20%. The gain per doubling of N is the marginal value of
the next renders; gain per hour = gain / (N * cost / 3600). Verdict: KEEP rendering while a doubling still gains > --min-gain R2,
otherwise STOP and let the model fill the rest (rows tagged predicted). Also prints R2 on held-out presets: that is how well an unrendered
preset's sound can already be guessed from its knobs."""
import argparse, glob, json, math, os
import numpy as np
SC = ["rms", "centroid", "rolloff", "flatness", "crest", "attack_ms", "decay_ms", "low", "mid", "high", "peak"]
LOGK = ("rms", "peak", "attack_ms", "decay_ms", "centroid", "rolloff")


def target(r):
    b = np.log10(np.maximum(r["bands"], 1e-12))
    s = [math.log10(max(r.get(k) or 0, 1e-6)) if k in LOGK else (r.get(k) or 0) for k in SC]
    return np.concatenate([b, s])


def knobs(modules):
    return {"%s.%s" % (m, k): float(v) for m, ps in (modules or {}).items() for k, v in (ps or {}).items() if isinstance(v, (int, float)) and not isinstance(v, bool)}


def args(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default=""); ap.add_argument("--root", default=""); ap.add_argument("--params", default="", help="gz jsonl of {src, params:{flat knobs}} (omni_params)"); ap.add_argument("--renders", required=True)
    ap.add_argument("--prefix", required=True); ap.add_argument("--note", default="C3"); ap.add_argument("--cost", type=float, default=6.3)
    ap.add_argument("--model", default="rff", choices=("rff", "ridge")); ap.add_argument("--min-gain", type=float, default=0.01); ap.add_argument("--presets", default="~/Infinite/training-data/presets")
    return ap.parse_args(argv)


def load(a):
    """Join recipes to C3 renders: standardised knobs X, targets Y, ids, fixed held-out split (te) and training pool."""
    ren = {}
    for l in open(os.path.expanduser(a.renders)):
        r = json.loads(l)
        if r["note"] == a.note and "bands" in r and r["src"].startswith(a.prefix):
            ren[r["src"][len(a.prefix):]] = target(r)
    rec = {}
    if a.params:
        import gzip
        for l in gzip.open(os.path.expanduser(a.params), "rt"):
            r = json.loads(l)
            k = r["src"][len(a.prefix):]
            if k in ren:
                rec[k] = {c: float(v) for c, v in r["params"].items() if isinstance(v, (int, float)) and not isinstance(v, bool)}
    for f in ([] if a.params else glob.glob(os.path.join(os.path.expanduser(a.presets), "*.jsonl"))):
        for l in open(f):
            if '"engine":"%s"' % a.engine not in l.replace('": "', '":"'):
                continue
            r = json.loads(l)
            if r.get("src", "").startswith(a.root):
                rel = r["src"][len(a.root):].lstrip("/")
                if rel in ren and rel not in rec:
                    rec[rel] = knobs(r.get("modules"))
    ids = sorted(rec)
    cols = sorted({k for d in rec.values() for k in d})
    X = np.array([[rec[i].get(c, np.nan) for c in cols] for i in ids], dtype=np.float64)
    X = np.where(np.isnan(X), np.nanmean(X, 0), X)
    sd = X.std(0); ok = sd > 1e-9; X = (X[:, ok] - X[:, ok].mean(0)) / sd[ok]
    Y = np.array([ren[i] for i in ids])
    rng = np.random.default_rng(0); p = rng.permutation(len(ids)); nt = len(ids) // 5
    return X, Y, ids, p[:nt], p[nt:], rng


def main():
    a = args()
    X, Y, ids, te, pool, rng = load(a); nt = len(te)
    D = 1500
    Wr = rng.normal(0, 1 / math.sqrt(X.shape[1]) * 0.7, (X.shape[1], D)); br = rng.uniform(0, 2 * math.pi, D)
    if a.model == "rff":
        X = np.sqrt(2.0 / D) * np.cos(X @ Wr + br)
    Yv = ((Y[te] - Y[te].mean(0)) ** 2).sum(0)
    def r2(n, lam=None):
        tr = pool[:n]; Xt = X[tr]; Yt = Y[tr]; mu = Yt.mean(0)
        best = -9
        for lam in ([lam] if lam else (0.01, 0.1, 1, 10) if a.model == "rff" else (30, 300, 3000)):
            W = np.linalg.solve(Xt.T @ Xt + lam * np.eye(X.shape[1]), Xt.T @ (Yt - mu))
            best = max(best, float(np.mean(1 - ((Y[te] - (X[te] @ W + mu)) ** 2).sum(0) / np.maximum(Yv, 1e-12))))
        return best
    print("%d presets joined (recipe+render), %d knob columns, %d held out" % (len(ids), X.shape[1], nt))
    print("%7s %8s %10s %12s %s" % ("N", "R2", "gain/dbl", "gain/hour", "verdict"))
    ns, prev, stop, run = [], None, None, 0
    n = 50
    while n <= len(pool):
        ns.append(n); n *= 2
    if ns[-1] != len(pool): ns.append(len(pool))
    for n in ns:
        s = r2(n); g = None if prev is None else s - prev[1]
        hrs = (n - (prev[0] if prev else 0)) * a.cost / 3600
        v = "" if g is None else ("KEEP" if g > a.min_gain else "STOP")
        run = run + 1 if v == "STOP" else 0
        if run >= 2 and stop is None: stop = n
        print("%7d %8.3f %10s %12s %s" % (n, s, "-" if g is None else "%+.3f" % g, "-" if g is None else "%+.4f" % (g / max(hrs, 1e-9)), v))
        prev = (n, s)
    print("held-out R2 with all rendered:", "%.3f" % prev[1], "| stop at about", stop or "not reached yet (keep rendering)")
if __name__ == "__main__":
    main()
