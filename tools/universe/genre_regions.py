#!/usr/bin/env python3
"""G5 / claim C8 first pass: can a genre be read from the 22-axis (here: 11 shared axes + tempo) description of a loop or track?   (ml-venv python)

    genre_regions.py [--min-rows 150] [--min-packs 3]

Labels are a reference, not truth (owner 6.3): a row gets a genre when its pack / file name contains the genre's words (atlas/genres.json
ids, longest match wins, ambiguous ids skipped). Features = the 11 shared axes of the row (z-scored) + log tempo when listed.
Split = held-out packs (hash of the pack folder, a fifth). Model = softmax regression (numpy). Reports top-1 / top-3 on held-out packs
against the majority-class baseline, and writes atlas/genre_regions.json (per-genre mean and spread in the axes, tempo range, packs, rows)."""
import argparse, json, math, os, re, sys, zlib
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import sound_space as ss  # noqa: E402

ATLAS = os.path.expanduser("~/Infinite/training-data/atlas")
SKIP = {"hard", "retro", "world", "experimental", "piano", "breaks", "edm", "pop", "rock", "latin", "folk", "afro", "classical", "cinematic"}


def keywords():
    g = json.load(open(os.path.join(ATLAS, "genres.json")))["genres"]; out = {}
    for gid in g:
        if gid in SKIP: continue
        w = gid.replace("_", "[ _-]?")
        out[gid] = re.compile(r"(?<![a-z])" + w + r"(?![a-z])")
    return out


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--min-rows", type=int, default=60); ap.add_argument("--min-packs", type=int, default=2); a = ap.parse_args()
    kw = keywords(); X, Y, P, B = [], [], [], []
    for ln in open(os.path.join(ss.AUDIO, "features.jsonl")):
        r = json.loads(ln); name = (r.get("pack") or r["src"]).lower()
        hits = [(len(g), g) for g, rx in kw.items() if rx.search(name)]
        if not hits: continue
        v = ss.vec_from_row(r)
        if v is None: continue
        g = max(hits)[1]; X.append(v); Y.append(g); P.append((r.get("pack") or r["src"]).split("/")[0]); B.append(r.get("bpm"))
    X = np.array(X); Y = np.array(Y); P = np.array(P); print("%d labelled rows, %d genres, %d packs" % (len(X), len(set(Y)), len(set(P))))
    ok = []
    for g in sorted(set(Y)):
        m = Y == g
        if m.sum() >= a.min_rows and len(set(P[m])) >= a.min_packs: ok.append(g)
    keep = np.isin(Y, ok); X, Y, P, B = X[keep], Y[keep], P[keep], [b for b, k in zip(B, keep) if k]
    bpm = np.array([math.log(b) if b else np.nan for b in B]); bm = np.nanmedian(bpm); has = np.isfinite(bpm)
    F = np.hstack([ss.prep(X)(X), np.where(has, bpm - bm, 0)[:, None] * 3, has[:, None].astype(float)])
    fold = np.array([zlib.crc32(p.encode()) % 5 for p in P])                       # every pack is held out once
    cls = sorted(set(Y)); ci = {g: i for i, g in enumerate(cls)}; y = np.array([ci[g] for g in Y]); A = np.hstack([F, np.ones((len(F), 1))])
    top = np.zeros((len(F), len(cls)), int)
    for k in range(5):
        tr, te = fold != k, fold == k
        if not te.any(): continue
        W = np.zeros((A.shape[1], len(cls))); cnt = np.bincount(y[tr], minlength=len(cls)); wts = (1.0 / np.maximum(cnt, 1))[y[tr]]; wts = wts / wts.mean()
        for it in range(500):
            Z = A[tr] @ W; Z -= Z.max(1, keepdims=True); Pm = np.exp(Z); Pm /= Pm.sum(1, keepdims=True)
            T = np.zeros_like(Pm); T[np.arange(tr.sum()), y[tr]] = 1
            W -= 0.5 * (A[tr].T @ ((Pm - T) * wts[:, None]) / tr.sum() + 1e-3 * W)
        top[te] = np.argsort(-(A[te] @ W), 1)
    hit1 = top[:, 0] == y; hit3 = np.array([y[i] in top[i, :3] for i in range(len(y))])
    pg = lambda h: np.mean([h[y == c].mean() for c in range(len(cls))])               # mean over genres, so house does not dominate
    acc1, acc3 = float(pg(hit1)), float(pg(hit3))
    print("leave-packs-out over %d rows, %d genres, %d packs | per-genre mean top-1 %.3f | top-3 %.3f (bar 0.7) | chance top-1 %.3f top-3 %.3f" %
          (len(y), len(cls), len(set(P)), acc1, acc3, 1.0 / len(cls), min(1, 3.0 / len(cls))))
    print("| genre | rows | packs | top-1 | top-3 |\n|---|---|---|---|---|")
    for c, g in enumerate(cls):
        m = y == c; print("| %s | %d | %d | %.2f | %.2f |" % (g, m.sum(), len(set(P[m])), hit1[m].mean(), hit3[m].mean()))
    reg = {g: {"rows": int((Y == g).sum()), "packs": len(set(P[Y == g])), "mean": np.round(X[Y == g].mean(0), 3).tolist(), "std": np.round(X[Y == g].std(0), 3).tolist(),
               "bpm": [round(float(np.nanpercentile(np.exp(bpm[Y == g]), q)), 1) for q in (10, 50, 90)] if np.isfinite(bpm[Y == g]).sum() > 20 else None} for g in cls}
    json.dump({"axes": ss.AX, "note": "labels from names (reference, not truth)", "top1": acc1, "top3": acc3, "genres": reg}, open(os.path.join(ATLAS, "genre_regions.json"), "w"))


if __name__ == "__main__":
    main()
