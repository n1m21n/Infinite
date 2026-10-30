#!/usr/bin/env python3
"""Which model of four-bar progressions predicts unseen songs best? Whole songs held out (5 seeds, 80/20), scored as bits per
progression on the held-out windows (lower is better), one model per family (major, minor).
    ~/Infinite/ml-venv/bin/python3 -I tools/universe/chord_model.py
Models: the shipped bank (uniform over its progressions, 1e-3 of the mass spread over the rest), a count table (add-k), an order-2
Markov chain, a small GRU. Result: docs/plans/data-universe/feel/chord_model.json."""
import collections, json, math, os, random, re, sys
import numpy as np, torch, torch.nn as nn
torch.set_num_threads(3)
HERE = os.path.dirname(os.path.abspath(__file__))
S = os.path.expanduser("~/Infinite/training-data/store/chord_windows.jsonl")
rows = [json.loads(l) for l in open(S)]
inc = open(os.path.join(HERE, "../../src/app/graph/GenerateSamplesNotes.inc")).read()
BANK = {0: set(), 1: set()}
for m, _, *d in re.findall(r"\{\s*(\d)\s*,\s*(\d+)\s*,\s*\{\s*(\d)\s*,\s*(\d)\s*,\s*(\d)\s*,\s*(\d)\s*\}", inc.split("kNoteProgs")[1].split("};")[0]):
    BANK[int(m)].add(tuple(int(x) for x in d))
def collect(songs, fam):
    c = collections.Counter()
    for r in songs:
        if r["minor"] == fam:
            for x, n in r["w"]:
                c[tuple(x)] += 1  # a song votes once per window, so a looped chorus does not dominate
    return c
def bits(P, test):
    n = sum(test.values()); return -sum(v * math.log2(P(k)) for k, v in test.items()) / n
class Gru(nn.Module):
    def __init__(s, d=48):
        super().__init__(); s.e = nn.Embedding(8, d); s.g = nn.GRU(d, d, 2, batch_first=True, dropout=0.2); s.o = nn.Linear(d, 7)
    def forward(s, x):
        return s.o(s.g(s.e(x))[0])
def gru(train):
    keys = list(train); w = np.array([train[k] for k in keys], float)
    X = torch.tensor([[7] + list(k[:3]) for k in keys]); Y = torch.tensor(keys); W = torch.tensor(w / w.sum(), dtype=torch.float32)
    m = Gru(); o = torch.optim.Adam(m.parameters(), 3e-3, weight_decay=1e-5)
    for ep in range(350):
        m.train(); l = (nn.functional.cross_entropy(m(X).transpose(1, 2), Y, reduction="none").sum(1) * W).sum()
        o.zero_grad(); l.backward(); o.step()
    m.eval()
    allk = [(a, b, c, d) for a in range(7) for b in range(7) for c in range(7) for d in range(7)]
    with torch.no_grad():
        lp = torch.log_softmax(m(torch.tensor([[7] + list(k[:3]) for k in allk])), -1)
        s = lp.gather(2, torch.tensor(allk).unsqueeze(-1)).squeeze(-1).sum(1).numpy()
    # windows with a repeat-only chord are excluded from the data: renormalise over what can occur
    P = {k: math.exp(v) for k, v in zip(allk, s) if len(set(k)) >= 2}; t = sum(P.values())
    return lambda k: max(P[k] / t, 1e-9)
def markov(train):
    tri = collections.Counter(); bi = collections.Counter()
    for k, v in train.items():
        h = (7, 7)
        for x in k:
            tri[h + (x,)] += v; bi[h] += v; h = (h[1], x)
    def p(k):
        h = (7, 7); r = 1.0
        for x in k:
            r *= (tri[h + (x,)] + 0.2) / (bi[h] + 1.4); h = (h[1], x)
        return r
    return p
res = []
for seed in range(5):
    songs = sorted({r["song"] for r in rows}); random.Random(seed).shuffle(songs); te = set(songs[:len(songs) // 5])
    for fam in (0, 1):
        tr = collect([r for r in rows if r["song"] not in te], fam); ts = collect([r for r in rows if r["song"] in te], fam)
        n = sum(tr.values()); b = BANK[fam]
        out = {"bank": bits(lambda k: (0.999 / len(b) if k in b else 0) + 0.001 / 2394, ts),
               "counts": bits(lambda k: (tr[k] + 0.3) / (n + 0.3 * 2394), ts), "markov2": bits(markov(tr), ts), "gru": bits(gru(tr), ts)}
        res.append(out); print(seed, fam, len(tr), n, {k: round(v, 3) for k, v in out.items()}, flush=True)
sm = {k: (float(np.mean([r[k] for r in res])), float(np.std([r[k] for r in res]))) for k in res[0]}
print(sm); print("songs", len({r['song'] for r in rows}), "bank sizes", len(BANK[0]), len(BANK[1]))
json.dump({"bits_per_progression": sm, "songs": len({r["song"] for r in rows}), "bank": [len(BANK[0]), len(BANK[1])]}, open(os.path.join(HERE, "../../docs/plans/data-universe/feel/chord_model.json"), "w"), indent=1)
