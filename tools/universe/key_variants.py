#!/usr/bin/env python3
"""Goal item 2, key accuracy: compare key detectors on the labelled chord-progression files, choosing on a tuning split and reading the
result on key_eval.py's 1,500 test files (the same shuffle, the first 1,500 usable files; tuning uses the next 1,500, never the test files).
    key_variants.py
Variants: profile (Krumhansl-Kessler, Temperley, Albrecht-Shanahan) x weighting (duration*velocity; + bass notes x B; + first/last chord x F)."""
import glob, json, os, random, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import midi_axes as MA
from key_eval_files import files

PROF = {
    "kk": (MA.MAJ, MA.MIN),
    "temperley": (np.array([5, 2, 3.5, 2, 4.5, 4, 2, 4.5, 2, 3.5, 1.5, 4]), np.array([5, 2, 3.5, 4.5, 2, 4, 2, 4.5, 3.5, 2, 1.5, 4])),
    "albrecht": (np.array([.238, .006, .111, .006, .137, .094, .016, .214, .009, .080, .008, .081]), np.array([.220, .006, .104, .123, .019, .103, .012, .214, .062, .022, .061, .052])),
}

def notes(path):
    fmt, div, ev, meta = MA.mf.parse(open(path, "rb").read()); nt, end = MA.notes_of(ev)
    nt = [n for n in nt if n[4] != 9]
    if len(nt) < 8: return None
    t0 = np.array([n[0] for n in nt], float); t1 = np.array([n[1] for n in nt], float); p = np.array([n[2] for n in nt], int); v = np.array([n[3] for n in nt], float)
    return t0, np.maximum(t1 - t0, 1), p, v

def detect(n, prof, bass_w, edge_w):
    t0, d, p, v = n; w = d * v
    h = np.bincount(p % 12, weights=w, minlength=12).astype(float)
    if bass_w:   # the lowest note sounding at each onset
        for t in np.unique(t0):
            m = t0 == t; i = np.flatnonzero(m)[np.argmin(p[m])]; h[p[i] % 12] += bass_w * w[i]
    if edge_w:   # first and last chord
        for t in (t0.min(), t0.max()):
            m = t0 == t; h += edge_w * np.bincount(p[m] % 12, weights=w[m], minlength=12)
    MAJ, MIN = PROF[prof]; best = (-2, 0, "")
    for k in range(12):
        r = np.roll(h, -k)
        for mode, pr in (("major", MAJ), ("minor", MIN)):
            c = float(np.corrcoef(r, pr)[0, 1])
            if c > best[0]: best = (c, k, mode)
    return best[1], best[2]

def score(data, prof, b, e):
    ex = le = 0
    for n, want, mode in data:
        t, m = detect(n, prof, b, e); rel = (want + 9) % 12 if mode == "major" else (want + 3) % 12
        ok = t == want and m == mode; ex += ok; le += ok or (t == rel and m != mode)
    return ex / len(data), le / len(data)

if __name__ == "__main__":
    test, tune = files(1500)
    load = lambda fs: [(n, w, m) for f, w, m in fs for n in [notes(f)] if n is not None]
    test, tune = load(test), load(tune); print(len(test), "test,", len(tune), "tune")
    grid = [(p, b, e) for p in PROF for b in (0, 0.5, 1, 2) for e in (0, 1, 3)]
    res = sorted(((score(tune, *g), g) for g in grid), key=lambda r: -r[0][1])
    for (ex, le), g in res[:6]: print("tune %-30s exact %.3f lenient %.3f" % (g, ex, le))
    base = score(test, "kk", 0, 0); best = res[0][1]; got = score(test, *best)
    print("test baseline kk/0/0: exact %.3f lenient %.3f | chosen %s: exact %.3f lenient %.3f (bar lenient >= 0.70)" % (*base, best, *got))
    json.dump({"tune_top": [[list(g), ex, le] for (ex, le), g in res[:6]], "test_baseline": base, "chosen": list(best), "test_chosen": got, "n_test": len(test), "n_tune": len(tune)},
              open(os.path.join(HERE, "../../docs/plans/data-universe/feel/key_variants.json"), "w"), indent=1)
