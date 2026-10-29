#!/usr/bin/env python3
"""Claim C4 (understands characteristics in words), first half: does the audio of a sample tell whether its file name carries a sound word?
Labels: word in the file name (lowercase tokens), 195k sample rows (features.jsonl). Features: 32 log bands + centroid, rolloff, flatness, crest, rms, low/mid/high.
Objective: logistic regression per word. Baseline: AUC 0.5. Test: pack-held-out (crc32(pack) % 5 == 0 held out), 3 seeds of negative subsampling; AUC mean +- sd.
Bar: AUC >= 0.75 for every word (words with >= 40 positives on each side). The 'made sounds' half (>= 70% of 50) is not covered here."""
import json, os, re, sys, zlib, math
import numpy as np
F = os.path.expanduser("~/Infinite/training-data/audio/features.jsonl")
WORDS = "warm bright dark soft hard punchy fat thin airy wet dry smooth harsh deep dirty clean lofi distorted crisp mellow sub heavy light metallic glassy gritty round tight huge wide dusty analog digital".split()
X, packs, toks = [], [], []
for l in open(F):
    r = json.loads(l)
    if not r.get("bands") or len(r["bands"]) != 32: continue
    sc = [math.log(max(r.get(k) or 1, 1)) for k in ("centroid", "rolloff")] + [r.get(k) or 0 for k in ("flatness", "crest", "rms", "low", "mid", "high")]
    X.append(np.r_[np.log10(np.maximum(r["bands"], 1e-9)), sc]); packs.append(r["pack"].split("/")[0]); toks.append(set(re.findall(r"[a-z]+", os.path.basename(r["src"]).lower())))
X = np.array(X); X = (X - X.mean(0)) / (X.std(0) + 1e-9); held = np.array([zlib.crc32(p.encode()) % 5 == 0 for p in packs]); print(len(X), "rows;", held.sum(), "held out")
def auc(s, y):
    o = np.argsort(s); r = np.empty(len(s)); r[o] = np.arange(1, len(s) + 1); n1 = y.sum(); n0 = len(y) - n1
    return (r[y == 1].sum() - n1 * (n1 + 1) / 2) / (n1 * n0)
def fit(Xt, yt, it=300):
    w = np.zeros(Xt.shape[1] + 1); Xb = np.c_[Xt, np.ones(len(Xt))]
    for _ in range(it):
        p = 1 / (1 + np.exp(-Xb @ w)); w -= 0.3 * (Xb.T @ (p - yt) / len(yt) + 0.01 * w)
    return w
res = {}
for w in WORDS:
    y = np.array([w in t for t in toks]).astype(float); tr, te = ~held, held
    if y[tr].sum() < 40 or y[te].sum() < 40: continue
    a = []
    for seed in range(3):
        rng = np.random.default_rng(seed); pos = np.where(tr & (y == 1))[0]; neg = rng.choice(np.where(tr & (y == 0))[0], len(pos) * 3, replace=False); ii = np.r_[pos, neg]
        wv = fit(X[ii], y[ii]); s = np.c_[X[te], np.ones(te.sum())] @ wv; a.append(auc(s, y[te]))
    res[w] = (float(np.mean(a)), float(np.std(a)), int(y[tr].sum()), int(y[te].sum())); print("%-10s AUC %.3f +- %.3f  (pos train %d, test %d)" % (w, *res[w]))
m = np.mean([v[0] for v in res.values()]); ok = sum(v[0] >= 0.75 for v in res.values()); print("words %d | mean AUC %.3f | >= 0.75: %d of %d" % (len(res), m, ok, len(res)))
json.dump(res, open("../../docs/plans/data-universe/feel/words_eval.json", "w"), indent=1)
