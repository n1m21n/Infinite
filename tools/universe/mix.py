#!/usr/bin/env python3
"""Find the drum "families" in the data itself: a mixture of Bernoulli grooves fitted with EM (no labels needed).
    mix.py [--scope local|shippable] [--k 8 16 32 64] [--store DIR] [--models DIR]
Every bar is one of K latent styles; each style is a table P(hit | lane, step). Held-out score is the same unit as learn.py
(bits saved per cell vs one Bernoulli per lane), so the numbers compare directly. Styles are named after the style-card
family they sit closest to, else left as `cluster_N`. Needs numpy (run without -I)."""
import argparse, collections, json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import learn

L, S = learn.LANES, learn.STEPS


def vec(p):
    return np.array(learn.cells(p), dtype=np.float32).reshape(-1)


def loglik(X, th):
    lt, l1 = np.log(th), np.log1p(-th)
    return X @ lt.T + (1 - X) @ l1.T  # (N, K)


def em(X, k, iters=40, seed=0):
    rng = np.random.default_rng(seed)
    th = np.clip(X[rng.choice(len(X), k, replace=False)] * 0.6 + X.mean(0) * 0.4, 0.02, 0.98)
    pi = np.full(k, 1.0 / k)
    for _ in range(iters):
        ll = loglik(X, th) + np.log(pi)
        m = ll.max(1, keepdims=True)
        r = np.exp(ll - m)
        r /= r.sum(1, keepdims=True)
        nk = r.sum(0) + 1e-3
        pi = nk / nk.sum()
        th = np.clip((r.T @ X + 0.5) / (nk[:, None] + 1.0), 0.005, 0.995)
    return th, pi


def bits_saved(X, th, pi, lane_p):
    ll = loglik(X, th) + np.log(pi)
    m = ll.max(1)
    tot = -(m + np.log(np.exp(ll - m[:, None]).sum(1))) / np.log(2)
    lp = np.repeat(lane_p, S)
    base = -(X @ np.log2(lp) + (1 - X) @ np.log2(1 - lp))
    return float((base - tot).sum() / (len(X) * L * S))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scope", default="local")
    ap.add_argument("--k", type=int, nargs="+", default=[8, 16, 32, 64])
    ap.add_argument("--store", default="~/Infinite/training-data/store")
    ap.add_argument("--models", default="~/Infinite/training-data/models")
    a = ap.parse_args()
    rows = learn.load(a.store, a.scope)
    real = [r for r in rows if not r.get("synthetic")]
    train, test = learn.split(real)
    tr = [p for p in train if p["type"] == "drum_groove"]
    te = [p for p in test if p["type"] == "drum_groove"]
    Xtr, Xte = np.stack([vec(p) for p in tr]), np.stack([vec(p) for p in te])
    lane_p = np.clip(Xtr.reshape(-1, L, S).mean((0, 2)), 1e-4, 1 - 1e-4)
    print("drum bars: train %d  test %d" % (len(tr), len(te)))
    best = None
    for k in a.k:
        th, pi = em(Xtr, k)
        s = bits_saved(Xte, th, pi, lane_p)
        print("K=%-3d held-out bits saved per cell %+.3f" % (k, s))
        if best is None or s > best[0]:
            best = (s, k, th, pi)
    s, k, th, pi = best
    cards = [p for p in learn.load(a.store, "local") if p["type"] == "drum_groove" and p["src"].startswith("card:")]
    name = {}
    if cards:
        resp = np.argmax(loglik(np.stack([vec(p) for p in cards]), th) + np.log(pi), 1)
        votes = collections.defaultdict(collections.Counter)
        for p, c in zip(cards, resp):
            votes[int(c)][p["family"]] += 1
        name = {c: v.most_common(1)[0][0] for c, v in votes.items()}
    use = np.argmax(loglik(Xtr, th) + np.log(pi), 1)
    print("\nbest K=%d (%+.3f). styles, biggest first (name = nearest style-card family):" % (k, s))
    for c in np.argsort(-pi)[:20]:
        t = th[c].reshape(L, S)
        dens = t.sum() / 16
        kicks = t[0].sum()
        kpat = "".join("x" if v > .5 else ("o" if v > .25 else ".") for v in t[0])
        print("  #%-2d %5.1f%%  %-14s hits/bar %4.1f  kicks/bar %3.1f  kick %s" % (
            c, 100 * pi[c], name.get(int(c), "cluster_%d" % c), dens, kicks, kpat))
    d = os.path.join(os.path.expanduser(a.models), a.scope)
    os.makedirs(d, exist_ok=True)
    out_p = os.path.join(d, "drum_mix.json")
    prev_s = None
    if os.path.exists(out_p):
        try:
            prev_s = json.load(open(out_p)).get("score")
        except Exception:
            prev_s = None
    if prev_s is not None and s < prev_s - 1e-4:
        print("REJECTED drum_mix (%+.3f < previous %+.3f): keeping %s" % (s, prev_s, out_p))
    else:
        json.dump({"k": k, "score": s, "pi": pi.tolist(), "theta": th.round(4).tolist(), "names": {str(c): n for c, n in name.items()}},
                  open(out_p, "w"))
        print("KEPT drum_mix (%+.3f): wrote %s" % (s, out_p))


if __name__ == "__main__":
    main()
