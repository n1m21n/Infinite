#!/usr/bin/env python3
"""Goal item 4, retrained on every part-labelled sample: does a sound's measured audio fit the generator part it is placed in?
    role_critic_samples.py [--folds 5] [--epochs 30] [--out FILE]
Data: audio/features.jsonl (about 150k samples labelled by part from their path). Roles are the Sample generator's parts
(SamplerBodies.cpp kParts); clap goes with snare and cymbal with hat as the generator does, and 'unknown' rows are left out.
Test: grouped K-fold with whole packs held out (no pack is in both train and test). Thresholds per role come from a validation
split of the training packs: the 3rd percentile of P(true role) (target false reject 3%). On the held-out packs: FRR = share of
voices rejected in their true role; catch = share rejected when declared as each wrong role. Bars: FRR <= 3%, catch >= 96%.
Models: softmax regression (baseline) and a two-layer MLP, both plain numpy (no sklearn or torch on this Mac)."""
import argparse, collections, json, os
import numpy as np

DATA = os.path.expanduser("~/Infinite/training-data/audio/features.jsonl")
ROLE = {"synth": "synth", "keys": "keys", "lead": "lead", "pad": "pad", "pluck": "pluck", "bass": "bass", "drums": "drums",
        "kick": "kick", "snare": "snare", "clap": "snare", "hat": "hat", "cymbal": "hat", "tom": "tom", "perc": "perc", "fx": "fx"}
EXTRA = {"guitar": "guitar", "strings": "strings", "brass": "brass", "vocal": "vocal"}   # acoustic families, reported with --extra
GENERIC = {"library", "application support", "logic", "logic pro library.bundle", "samples", "extra packs", "sampler", "sampler files",
           "garageband", "instrument library", "alchemy samples", "apple loops", "other", "ultrabeat samples", "vengaence packs"}

def pack_of(src):
    """The sample pack a file came from: the first folder below the drive that is not a generic library folder."""
    parts = src.split("/Volumes/", 1)[-1].split("/")[1:-1]
    for p in parts:
        if p.lower() not in GENERIC: return p
    return "/".join(parts) or "?"

def features(r):
    b = np.array(r["bands"], float); tot = b.sum() + 1e-12
    shape = np.log10(b / tot + 1e-9)                                   # spectral shape, loudness removed
    sc = [np.log10(tot), r["peak"], r["rms"], r["silence"], r["corr"], np.log1p(r["onsets"]), np.log1p(r["crest"]), r["low"], r["mid"],
          r["high"], np.log1p(r["centroid"]), np.log1p(r["rolloff"]), r["flatness"], np.log1p(r["seconds"]), float(bool(r.get("loop")))]
    return np.r_[shape, np.nan_to_num(np.array(sc, float))]

def softmax(Z):
    Z = Z - Z.max(1, keepdims=True); P = np.exp(Z); return P / P.sum(1, keepdims=True)

class Linear:
    def __init__(self, k, epochs): self.k, self.ep = k, epochs
    def fit(self, X, y, w):
        Y = np.eye(self.k)[y]; Xb = np.c_[X, np.ones(len(X))]; self.W = np.zeros((Xb.shape[1], self.k))
        for _ in range(60 * self.ep):
            P = softmax(Xb @ self.W); self.W -= 0.5 * (Xb.T @ ((P - Y) * w[:, None]) / w.sum() + 1e-4 * self.W)
        return self
    def proba(self, X): return softmax(np.c_[X, np.ones(len(X))] @ self.W)

class MLP:
    """Two hidden ReLU layers, Adam, class-weighted cross-entropy, dropout-free with L2."""
    def __init__(self, k, epochs, h=(256, 128), seed=0): self.k, self.ep, self.h, self.rng = k, epochs, h, np.random.default_rng(seed)
    def fit(self, X, y, w):
        dims = [X.shape[1], *self.h, self.k]
        self.p = [[self.rng.normal(0, np.sqrt(2 / a), (a, b)), np.zeros(b)] for a, b in zip(dims, dims[1:])]
        m = [[np.zeros_like(W), np.zeros_like(c)] for W, c in self.p]; v = [[np.zeros_like(W), np.zeros_like(c)] for W, c in self.p]
        Y = np.eye(self.k)[y]; t = 0
        for ep in range(self.ep):
            lr = 2e-3 * (0.5 * (1 + np.cos(np.pi * ep / self.ep)))
            for bi in np.array_split(self.rng.permutation(len(X)), max(1, len(X) // 512)):
                acts = [X[bi]]
                for W, c in self.p[:-1]: acts.append(np.maximum(acts[-1] @ W + c, 0))
                P = softmax(acts[-1] @ self.p[-1][0] + self.p[-1][1]); g = (P - Y[bi]) * w[bi, None] / w[bi].sum(); t += 1
                for L in range(len(self.p) - 1, -1, -1):
                    gW = acts[L].T @ g + 1e-4 * self.p[L][0]; gc = g.sum(0)
                    if L: g = (g @ self.p[L][0].T) * (acts[L] > 0)
                    for j, gr in ((0, gW), (1, gc)):
                        m[L][j] = 0.9 * m[L][j] + 0.1 * gr; v[L][j] = 0.999 * v[L][j] + 0.001 * gr * gr
                        self.p[L][j] -= lr * (m[L][j] / (1 - 0.9 ** t)) / (np.sqrt(v[L][j] / (1 - 0.999 ** t)) + 1e-8)
        return self
    def proba(self, X):
        a = X
        for W, c in self.p[:-1]: a = np.maximum(a @ W + c, 0)
        return softmax(a @ self.p[-1][0] + self.p[-1][1])

def score(P, y, th):
    n, k = P.shape; frr = float((P[np.arange(n), y] < th[y]).mean())
    rej = P < th[None, :]; wrong = np.ones((n, k), bool); wrong[np.arange(n), y] = False
    per = {int(c): float(rej[y == c][:, [j for j in range(k) if j != c]].mean()) for c in range(k) if (y == c).any()}
    return frr, float(rej[wrong].mean()), float((P.argmax(1) == y).mean()), per

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--folds", type=int, default=5); ap.add_argument("--epochs", type=int, default=30)
    ap.add_argument("--extra", action="store_true"); ap.add_argument("--out"); a = ap.parse_args()
    roles = dict(ROLE, **(EXTRA if a.extra else {}))
    X, lab, packs = [], [], []
    for l in open(DATA):
        r = json.loads(l)
        if r.get("part") not in roles or r.get("bands") is None: continue
        X.append(features(r)); lab.append(roles[r["part"]]); packs.append(pack_of(r["src"]))
    names = sorted(set(lab)); y = np.array([names.index(s) for s in lab]); X = np.array(X)
    upk = sorted(set(packs)); g = np.array([upk.index(p) for p in packs])
    print(len(y), "samples;", len(upk), "packs;", len(names), "roles:", dict(collections.Counter(lab)))
    rng = np.random.default_rng(0); fold_of = rng.integers(0, a.folds, len(upk))[g]
    res = {"Linear": [], "MLP": []}; per_role = {k: collections.defaultdict(list) for k in res}
    for f in range(a.folds):
        te = fold_of == f; tr_all = ~te
        val_pack = rng.random(len(upk)) < 0.15; va = tr_all & val_pack[g]; tr = tr_all & ~val_pack[g]
        mu, sd = X[tr].mean(0), X[tr].std(0) + 1e-9; Z = (X - mu) / sd
        cnt = np.bincount(y[tr], minlength=len(names)); w = (1.0 / np.maximum(cnt, 1))[y]
        for name, M in (("Linear", Linear(len(names), a.epochs)), ("MLP", MLP(len(names), a.epochs, seed=f))):
            M.fit(Z[tr], y[tr], w[tr])
            Pv = M.proba(Z[va]); th = np.array([np.percentile(Pv[y[va] == c, c], 3) if (y[va] == c).sum() >= 20 else 0.0 for c in range(len(names))])
            frr, catch, top1, per = score(M.proba(Z[te]), y[te], th); res[name].append((frr, catch, top1))
            for c, v in per.items(): per_role[name][names[c]].append(v)
            print("fold %d %-6s FRR %.3f catch %.3f top-1 %.3f (test %d samples, %d packs)" % (f, name, frr, catch, top1, te.sum(), len(set(g[te]))), flush=True)
    out = {"roles": names, "n": int(len(y)), "packs": len(upk), "split": "%d-fold, whole packs held out" % a.folds}
    for name, R in res.items():
        R = np.array(R); out[name] = {"frr": R[:, 0].tolist(), "catch": R[:, 1].tolist(), "top1": R[:, 2].tolist(),
                                       "catch_by_role": {k: round(float(np.mean(v)), 3) for k, v in per_role[name].items()}}
        print("%-6s FRR %.3f+-%.3f | catch %.3f+-%.3f | top-1 %.3f+-%.3f" % (name, R[:, 0].mean(), R[:, 0].std(), R[:, 1].mean(), R[:, 1].std(), R[:, 2].mean(), R[:, 2].std()))
    best = out["MLP"]; out["verdict"] = "MET" if np.mean(best["frr"]) <= 0.03 and np.mean(best["catch"]) >= 0.96 else "MISS"
    print("verdict", out["verdict"]); print(json.dumps(out["MLP"]["catch_by_role"]))
    if a.out: json.dump(out, open(a.out, "w"), indent=1)

if __name__ == "__main__":
    main()
