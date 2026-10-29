#!/usr/bin/env python3
"""Goal item 2: next-chord top-1 on unseen progressions, second model. Same data, tokens, splits and baseline as chord_eval.py
(distinct numeral sequences, 80/20 by progression, seeds 0-4, baseline = always the most frequent chord); the bar is top-1 >= baseline + 10 points.
    chord_eval2.py
Model: the n-gram of chord_eval.py, plus (a) a class back-off: each numeral maps to its chord class (root in semitones above the tonic,
read in the progression's mode, and major / minor / other), the class n-gram predicts the next class and P(numeral | class) spreads it;
(b) mode (Major / Minor / Modal folder) as context; (c) loop context: the chord one bar back (4 chords) and the first chord.
Mixture weights are chosen on split seeds 10-19 (selection) and reported on seeds 0-4 (the chord_eval.py splits), so the choice never sees the report splits."""
import collections, itertools, json, math, os, random
import numpy as np
S = os.path.expanduser("~/Infinite/training-data/store/chords.jsonl")
MAJ = [0, 2, 4, 5, 7, 9, 11]; NMIN = [0, 2, 3, 5, 7, 8, 10]

def cls(ch, mode):
    sc = NMIN if mode == "Minor" else MAJ; pc = (sc[ch["deg"] - 1] + {"b": -1, "#": 1}.get(ch["acc"], 0)) % 12
    e = ch["ext"].lower(); q = "o" if ("dim" in e or "sus" in e or e.startswith("5") or "-5" in e) else ("m" if ch["minor"] else "M")
    return "%d%s" % (pc, q)

rows = [json.loads(l) for l in open(S)]; seen = {}
for r in rows:
    k = tuple(r["numerals"])
    if k not in seen: seen[k] = (r["mode"], tuple(cls(c, r["mode"]) for c in r["chords"]))
seqs = sorted(seen); MODE = {s: seen[s][0] for s in seqs}; CL = {s: seen[s][1] for s in seqs}
V = sorted({t for s in seqs for t in s}) + ["</s>"]; vi = {t: i for i, t in enumerate(V)}

def counts(items):
    u = collections.Counter(); b = collections.defaultdict(collections.Counter); t3 = collections.defaultdict(collections.Counter)
    loop = collections.defaultdict(collections.Counter); first = collections.defaultdict(collections.Counter); md = collections.defaultdict(collections.Counter)
    for s, m in items:
        s = ("<s>", "<s>") + s + ("</s>",)
        for i in range(2, len(s)):
            u[s[i]] += 1; b[s[i - 1]][s[i]] += 1; t3[(s[i - 2], s[i - 1])][s[i]] += 1; md[(m, s[i - 1])][s[i]] += 1
            if i - 4 >= 2: loop[s[i - 4]][s[i]] += 1
            if i > 2: first[(s[2], i - 2)][s[i]] += 1
    return u, b, t3, loop, first, md

def dist(c, keys):
    n = sum(c.values()) if c else 0
    return None if not n else np.array([c[k] / n for k in keys])

class Model:
    def __init__(self, tr, w):
        self.w = w; self.N = counts([(s, MODE[s]) for s in tr]); self.C = counts([(CL[s], MODE[s]) for s in tr])
        self.ck = sorted({c for s in tr for c in CL[s]}) + ["</s>"]; self.emit = collections.defaultdict(collections.Counter)
        for s in tr:
            for t, c in zip(s, CL[s]): self.emit[c][t] += 1
        self.emit["</s>"]["</s>"] = 1
    def mix(self, M, keys, h, mode, pos, first):
        u, b, t3, loop, fst, md = M; U = sum(u.values()); p = np.array([(u[t] + .1) / (U + .1 * len(keys)) for t in keys])
        for c, w in ((b.get(h[-1]), self.w["bi"]), (md.get((mode, h[-1])), self.w["mode"]), (t3.get((h[-2], h[-1])), self.w["tri"]),
                     (loop.get(h[-4]) if pos >= 4 else None, self.w["loop"]), (fst.get((first, pos)) if pos >= 1 else None, self.w["first"])):
            q = dist(c, keys)
            if q is not None: p = (1 - w) * p + w * q
        return p / p.sum()
    def proba(self, s, cs, j, mode):
        h = ("<s>", "<s>", "<s>", "<s>") + s[:j]; hc = ("<s>", "<s>", "<s>", "<s>") + cs[:j]
        pn = self.mix(self.N, V, h, mode, j, s[0] if j else None)
        pc = self.mix(self.C, self.ck, hc, mode, j, cs[0] if j else None); pv = np.zeros(len(V))
        for c, q in zip(self.ck, pc):
            e = self.emit.get(c)
            if e:
                n = sum(e.values())
                for t, k in e.items(): pv[vi[t]] += q * k / n
        p = (1 - self.w["cls"]) * pn + self.w["cls"] * (pv + 1e-6) / (pv + 1e-6).sum(); return p / p.sum()

def run(seeds, w):
    res = []
    for sd in seeds:
        rng = random.Random(sd); idx = list(range(len(seqs))); rng.shuffle(idx); cut = int(.8 * len(idx)); tr = [seqs[i] for i in idx[:cut]]
        m = Model(tr, w); top = max(m.N[0], key=m.N[0].get); t1 = base = n = 0; ce = 0.0
        for i in idx[cut:]:
            s = seqs[i]; full = s + ("</s>",)
            for j in range(len(full)):
                p = m.proba(s, CL[s], j, MODE[s]); y = vi[full[j]]; t1 += (p > p[y]).sum() == 0; base += full[j] == top; ce -= math.log2(p[y]); n += 1
        res.append((t1 / n, base / n, ce / n, n))
    return np.array(res)

if __name__ == "__main__":
    print(len(seqs), "distinct progressions,", len(V), "numerals,", len({c for s in seqs for c in CL[s]}), "classes")
    ref = {"bi": .5, "tri": .8, "mode": 0, "loop": 0, "first": 0, "cls": 0}   # chord_eval.py's model
    grid = [dict(bi=.5, tri=t, mode=md, loop=l, first=f, cls=c) for t, md, l, f, c in itertools.product((.5, .8), (0, .5), (0, .3, .6), (0, .3, .6), (0, .3, .6))]
    sel = sorted(((run(range(10, 20), w)[:, 0].mean(), i) for i, w in enumerate(grid)), reverse=True)
    best = grid[sel[0][1]]; print("selection seeds 10-19: best", best, "top-1 %.3f" % sel[0][0])
    R0, R1 = run(range(5), ref), run(range(5), best)
    for name, R in (("chord_eval.py n-gram", R0), ("chosen model", R1)):
        g = R[:, 0] - R[:, 1]
        print("%-22s top-1 %.3f+-%.3f | baseline %.3f | gain %+.1f +- %.1f points | CE %.2f bits" % (name, R[:, 0].mean(), R[:, 0].std(), R[:, 1].mean(), 100 * g.mean(), 100 * g.std(), R[:, 2].mean()))
    g = R1[:, 0] - R1[:, 1]
    out = {"chosen": best, "selection_top1": sel[0][0], "report_seeds": [0, 1, 2, 3, 4], "top1": R1[:, 0].tolist(), "base": R1[:, 1].tolist(), "ce": R1[:, 2].tolist(),
           "gain_points": round(100 * g.mean(), 2), "gain_sd": round(100 * g.std(), 2), "ngram_gain_points": round(100 * (R0[:, 0] - R0[:, 1]).mean(), 2),
           "test_tokens_per_split": R1[:, 3].tolist(), "verdict": "MET" if g.mean() >= 0.10 else "MISS"}
    print("verdict (gain >= 10 points):", out["verdict"])
    json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../docs/plans/data-universe/feel/chord_eval2.json"), "w"), indent=1)
