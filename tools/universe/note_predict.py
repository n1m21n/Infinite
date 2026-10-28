#!/usr/bin/env python3
"""How many notes per preset must be rendered? Predict the other notes' measured features from a few rendered notes (ridge, held-out presets).
    note_predict.py preset_diva.jsonl [--known C3] [--folds 5]
Reports R^2 per feature group and per held-out note, against the 'copy the C3 row' baseline."""
import argparse, json, collections, math
import numpy as np
ap = argparse.ArgumentParser(); ap.add_argument("file"); ap.add_argument("--known", default="C3"); ap.add_argument("--folds", type=int, default=5)
a = ap.parse_args()
rows = collections.defaultdict(dict)
for l in open(a.file):
    r = json.loads(l); rows[r["src"]][r["note"]] = r
SC = ["rms", "centroid", "rolloff", "flatness", "crest", "attack_ms", "decay_ms", "low", "mid", "high", "peak"]
def vec(r):
    b = np.log10(np.maximum(r["bands"], 1e-12))
    s = [math.log10(max(r.get(k) or 0, 1e-6)) if k in ("rms", "peak", "attack_ms", "decay_ms", "centroid", "rolloff") else (r.get(k) or 0) for k in SC]
    return np.concatenate([b, s, r.get("env") or [0] * 40])
GROUPS = {"spectrum(32 bands)": slice(0, 32), "scalars": slice(32, 32 + len(SC)), "envelope(40)": slice(32 + len(SC), None)}
known = a.known.split(",")
ids = [k for k, v in rows.items() if len(v) == 6 and all("bands" in x for x in v.values())]
notes = ["C2", "C3", "C4", "C5", "C3p", "C3f"]
X = np.array([np.concatenate([vec(rows[i][n]) for n in known]) for i in ids])
rng = np.random.default_rng(0); perm = rng.permutation(len(ids)); fold = np.array_split(perm, a.folds)
print(len(ids), "presets; known notes:", known)
print("%-6s %-20s %8s %8s" % ("target", "group", "R2 ridge", "R2 copy-C3"))
for n in [x for x in notes if x not in known]:
    Y = np.array([vec(rows[i][n]) for i in ids]); Yc = np.array([vec(rows[i]["C3"]) for i in ids]); P = np.zeros_like(Y)
    for f in fold:
        tr = np.setdiff1d(perm, f); mu, sd = X[tr].mean(0), X[tr].std(0) + 1e-9
        Z = (X - mu) / sd; A = Z[tr].T @ Z[tr] + 30 * np.eye(Z.shape[1]); W = np.linalg.solve(A, Z[tr].T @ (Y[tr] - Y[tr].mean(0)))
        P[f] = Z[f] @ W + Y[tr].mean(0)
    for g, s in GROUPS.items():
        def r2(Q):
            res = ((Y[:, s] - Q[:, s]) ** 2).sum(0); tot = ((Y[:, s] - Y[:, s].mean(0)) ** 2).sum(0)
            return float(np.mean(1 - res / np.maximum(tot, 1e-12)))
        print("%-6s %-20s %8.3f %8.3f" % (n, g, r2(P), r2(Yc)))
