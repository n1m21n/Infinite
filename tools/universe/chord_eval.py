#!/usr/bin/env python3
"""Goal item 2: next-chord accuracy and loss on unseen progressions. Distinct numeral sequences only (same numerals in 12 keys = one), 5 random 80/20 splits by progression.
Model: back-off trigram->bigram->unigram with add-k smoothing over numeral tokens. Baseline: always the most frequent chord. Reports top-1, top-3, cross-entropy (bits), mean +- sd."""
import json, math, random, collections, os
import numpy as np
S = os.path.expanduser("~/Infinite/training-data/store/chords.jsonl")
seqs = sorted({tuple(json.loads(l)["numerals"]) for l in open(S)}); print(len(seqs), "distinct progressions")
V = sorted({t for s in seqs for t in s}) + ["</s>"]; vi = {t: i for i, t in enumerate(V)}
def fit(tr):
    u = collections.Counter(); b = collections.defaultdict(collections.Counter); t3 = collections.defaultdict(collections.Counter)
    for s in tr:
        s = ("<s>", "<s>") + s + ("</s>",)
        for i in range(2, len(s)): u[s[i]] += 1; b[s[i-1]][s[i]] += 1; t3[(s[i-2], s[i-1])][s[i]] += 1
    return u, b, t3
def prob(m, h2, h1):
    u, b, t3 = m; U = sum(u.values()); p = np.array([(u[t] + .1) / (U + .1 * len(V)) for t in V])
    for c, w in ((b.get(h1), .5), (t3.get((h2, h1)), .8)):
        if c:
            n = sum(c.values()); q = np.array([c[t] / n for t in V]); p = (1 - w) * p + w * q
    return p / p.sum()
res = []
for sd in range(5):
    rng = random.Random(sd); idx = list(range(len(seqs))); rng.shuffle(idx); cut = int(.8 * len(idx))
    m = fit([seqs[i] for i in idx[:cut]]); top = max(m[0], key=m[0].get); t1 = t3_ = base = n = 0; ce = 0.0
    for i in idx[cut:]:
        s = ("<s>", "<s>") + seqs[i] + ("</s>",)
        for j in range(2, len(s)):
            p = prob(m, s[j-2], s[j-1]); y = vi[s[j]]; rk = (p > p[y]).sum(); t1 += rk == 0; t3_ += rk < 3; base += s[j] == top; ce -= math.log2(p[y]); n += 1
    res.append((t1 / n, t3_ / n, base / n, ce / n))
r = np.array(res); print("top1 %.3f+-%.3f | top3 %.3f+-%.3f | baseline top1 %.3f | cross-entropy %.2f+-%.2f bits (uniform %.2f)" % (r[:,0].mean(), r[:,0].std(), r[:,1].mean(), r[:,1].std(), r[:,2].mean(), r[:,3].mean(), r[:,3].std(), math.log2(len(V))))
json.dump({"top1": r[:,0].tolist(), "top3": r[:,1].tolist(), "base": r[:,2].tolist(), "ce": r[:,3].tolist()}, open("../../docs/plans/data-universe/feel/chord_eval.json", "w"))
