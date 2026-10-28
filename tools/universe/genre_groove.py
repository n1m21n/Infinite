#!/usr/bin/env python3
"""C8 on structure: genre family from a drum groove (8 lanes x 16 steps) + tempo, leave-drummer-out.   (ml-venv python)
Data: store/public_drums.jsonl (Groove MIDI, family from the file name). Reference labels, not truth."""
import collections, json, math, os, sys, zlib
import numpy as np
rows = [json.loads(l) for l in open(os.path.expanduser("~/Infinite/training-data/store/public_drums.jsonl"))]
rows = [r for r in rows if r.get("family") and r.get("lanes") and len(r["lanes"]) == 8 and r.get("bpm")]
cnt = collections.Counter(r["family"] for r in rows); cls = sorted(g for g, n in cnt.items() if n >= 40)
rows = [r for r in rows if r["family"] in cls]; ci = {g: i for i, g in enumerate(cls)}
def feat(r):
    L = np.array([[1.0 if (c not in ".") else 0.0 for c in (l.ljust(16, ".")[:16])] for l in r["lanes"]])   # 8 x 16
    return np.r_[L.ravel(), L.sum(1) / 16, [math.log(r["bpm"]) - 4.7, r.get("swing") or 0.0, 1.0]]
F = np.array([feat(r) for r in rows]); y = np.array([ci[r["family"]] for r in rows]); grp = np.array([r["src"].split("/")[1] for r in rows])
fold = np.array([zlib.crc32(g.encode()) % 5 for g in grp]); top = np.zeros((len(F), len(cls)), int)
for k in range(5):
    tr, te = fold != k, fold == k
    if not te.any(): continue
    W = np.zeros((F.shape[1], len(cls))); c = np.bincount(y[tr], minlength=len(cls)); w = (1.0 / np.maximum(c, 1))[y[tr]]; w /= w.mean()
    for _ in range(800):
        Z = F[tr] @ W; Z -= Z.max(1, keepdims=True); P = np.exp(Z); P /= P.sum(1, keepdims=True); T = np.zeros_like(P); T[np.arange(tr.sum()), y[tr]] = 1
        W -= 0.3 * (F[tr].T @ ((P - T) * w[:, None]) / tr.sum() + 1e-3 * W)
    top[te] = np.argsort(-(F[te] @ W), 1)
h1, h3 = top[:, 0] == y, np.array([y[i] in top[i, :3] for i in range(len(y))])
pg = lambda h: np.mean([h[y == c].mean() for c in range(len(cls))])
print("leave-drummer-out: %d grooves, %d families, %d drummers | per-family mean top-1 %.3f top-3 %.3f (bar 0.7) | chance %.3f / %.3f" %
      (len(y), len(cls), len(set(grp)), pg(h1), pg(h3), 1 / len(cls), min(1, 3 / len(cls))))
print("| family | grooves | top-1 | top-3 |\n|---|---|---|---|")
for c, g in enumerate(cls): print("| %s | %d | %.2f | %.2f |" % (g, (y == c).sum(), h1[y == c].mean(), h3[y == c].mean()))
