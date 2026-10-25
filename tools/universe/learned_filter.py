#!/usr/bin/env python3
"""F4: a learned filter over genomes. Logistic regression (numpy) on bag-of-node-names + structure,
trained on atlas rows, used to skip probes that are very likely silent or broken.
    learned_filter.py train rows.jsonl... --model f4.json   (prints 5-fold AUC and a pruning table)
    learned_filter.py score f4.json "<genome>"
"""
import argparse, json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u


def feats(names, code, extra=()):
    f = ["n:" + n for n in set(names)] + ["p:%s>%s" % p for p in zip(names, names[1:])]
    f += ["len:%d" % min(len(names), 12), "join:%d" % ("=>" in code), "mods:%d" % min(code.count("@"), 4),
          "cross:%d" % ("<" in code), "first:" + names[0], "last:" + names[-1]]
    return f + list(extra)


def design(rows, vocab):
    X = np.zeros((len(rows), len(vocab) + 1))
    X[:, -1] = 1
    for i, fs in enumerate(rows):
        for f in fs:
            j = vocab.get(f)
            if j is not None:
                X[i, j] = 1
    return X


def fit(X, y, l2=2.0, iters=400, lr=0.5):
    w = np.zeros(X.shape[1])
    for _ in range(iters):
        p = 1 / (1 + np.exp(-X @ w))
        g = X.T @ (p - y) / len(y) + l2 * np.r_[w[:-1], 0] / len(y)
        w -= lr * g
    return w


def auc(y, s):
    o = np.argsort(s)
    r = np.empty(len(s)); r[o] = np.arange(1, len(s) + 1)
    pos = y == 1
    n1, n0 = pos.sum(), (~pos).sum()
    return (r[pos].sum() - n1 * (n1 + 1) / 2) / max(1, n1 * n0)


def load_rows(paths, P):
    out = []
    for p in paths:
        for l in open(p):
            r = json.loads(l)
            names = r.get("names")
            if not names:
                try:
                    names = u.node_names(r["code"], P)
                except Exception:
                    continue
            if r["class"].startswith("error") or r["class"].startswith("classify"):
                continue
            out.append((feats(names, r["code"]), int(r["class"] in ("ok", "clipped"))))
    return out


def score_code(model, code, P):
    vocab = model["vocab"]
    X = design([feats(u.node_names(code, P), code)], vocab)
    return float(1 / (1 + np.exp(-X @ np.array(model["w"])))[0])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["train", "score"])
    ap.add_argument("args", nargs="+")
    ap.add_argument("--model", default="f4.json")
    a = ap.parse_args()
    facts = u.load_facts(); P = u.pools(facts)
    if a.cmd == "score":
        print(score_code(json.load(open(a.args[0])), a.args[1], P)); return
    data = load_rows(a.args, P)
    cnt = {}
    for fs, _ in data:
        for f in set(fs):
            cnt[f] = cnt.get(f, 0) + 1
    vocab = {f: i for i, f in enumerate(sorted(f for f, c in cnt.items() if c >= 3))}
    X = design([d[0] for d in data], vocab); y = np.array([d[1] for d in data], float)
    idx = np.random.RandomState(0).permutation(len(y)); folds = np.array_split(idx, 5)
    oof = np.zeros(len(y))
    for k in range(5):
        tr = np.concatenate([folds[j] for j in range(5) if j != k])
        oof[folds[k]] = 1 / (1 + np.exp(-X[folds[k]] @ fit(X[tr], y[tr])))
    print("rows %d  audible %.1f%%  features %d  5-fold AUC %.3f" % (len(y), 100 * y.mean(), len(vocab), auc(y, oof)))
    print("prune threshold  skipped%  of-skipped-actually-audible%  kept-audible%")
    for t in (0.3, 0.5, 0.6, 0.7):
        sk = oof < t
        if sk.sum():
            print("  p<%.1f          %5.1f      %5.1f                      %5.1f" % (t, 100 * sk.mean(), 100 * y[sk].mean(), 100 * y[~sk].mean()))
    w = fit(X, y)
    json.dump({"vocab": vocab, "w": w.tolist()}, open(a.model, "w"))
    inv = {i: f for f, i in vocab.items()}
    order = np.argsort(w[:-1])
    print("most harmful:", [(inv[i], round(w[i], 2)) for i in order[:6]])
    print("model ->", a.model)


if __name__ == "__main__":
    main()
