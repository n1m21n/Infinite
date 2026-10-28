#!/usr/bin/env python3
"""Step 5: judge good and new, on the axes only (EXECUTION.md Step 5, dials.md). numpy only; runs on rows that exist.

    sound_space.py critic   [--engines oscillator,analog]   train map-vs-random, score the frozen C3 sets (AUC)
    sound_space.py novelty  --node Analog                    N and R of every render against the map and all earlier renders
    sound_space.py clusters [--k 40]                         regions of the map on the axes, named from preset / sample names
    sound_space.py report                                    all three, printed as markdown

Feature vector = mean of each axis shared by every source (the 11 spectral / envelope axes: loudness, brightness, sharpness, warmth,
weight, fullness, noisiness, movement, attack, decay, density), standardised on the map. Sources: map = Diva C3 rows (diva_m1),
samples (features.jsonl, bands only); random = base-design renders of our engines. Critic = logistic regression (L2) on these, AUC on the
frozen sets of claims.py C3. Novelty N = distance to the nearest map row, divided by the median nearest-neighbour distance inside
the map (0 = on the map, 1 = as far as a typical map row is from its neighbour; capped at 2). Reach R = share of axes where the
render lies outside the map's 5-95 percentile band."""
import argparse, json, os, re, sys, collections
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import characteristics as ch, claims as cl, sound_data as sd  # noqa: E402

AX = ["loudness", "brightness", "sharpness", "warmth", "weight", "fullness", "noisiness", "movement", "attack", "decay", "density"]
AUDIO = cl.AUDIO


def vec_from_row(r):
    c = ch.from_row(r)
    if not c:
        return None
    return np.array([c["axes"].get(a, {}).get("mean", np.nan) for a in AX])


def own_matrix(path, ids=None):
    """C3 window axis means of an own_*.jsonl, with preset ids and design."""
    X, I, D = [], [], []
    for ln in open(path):
        if '"note":"C3"' not in ln:
            continue
        r = json.loads(ln)
        if r["note"] != "C3" or (ids is not None and r["preset_id"] not in ids):
            continue
        v = vec_from_row(r)
        if v is not None:
            X.append(v); I.append(r["preset_id"]); D.append(r.get("tags", {}).get("design", ""))
    return np.array(X), I, D


def diva_matrix(ids=None):
    p = os.path.join(AUDIO, "diva_m1.jsonl"); X, I, N = [], [], []
    if os.path.exists(p):
        for ln in open(p):
            if '"note":"C3"' not in ln:
                continue
            r = json.loads(ln)
            if r.get("note") == "C3" and (ids is None or r["preset_id"] in ids):
                v = vec_from_row(r)
                if v is not None:
                    X.append(v); I.append(r["preset_id"]); N.append(r.get("label", ""))
    return np.array(X), I, N


def sample_matrix(srcs=None, limit=20000):
    X, N = [], []
    for ln in open(os.path.join(AUDIO, "features.jsonl")):
        r = json.loads(ln)
        if srcs is not None and r["src"] not in srcs:
            continue
        v = vec_from_row(r)
        if v is not None:
            X.append(v); N.append(os.path.basename(r["src"]))
        if len(X) >= limit:
            break
    return np.array(X), N


def auc(s, y):
    o = np.argsort(s); r = np.empty(len(s)); r[o] = np.arange(1, len(s) + 1)
    n1 = y.sum(); n0 = len(y) - n1
    return float((r[y == 1].sum() - n1 * (n1 + 1) / 2) / (n1 * n0))


def logit(X, y, l2=1.0, it=300):
    w = np.zeros(X.shape[1] + 1); A = np.hstack([X, np.ones((len(X), 1))])
    for _ in range(it):
        p = 1 / (1 + np.exp(-A @ w)); g = A.T @ (p - y) / len(y) + l2 * np.r_[w[:-1], 0] / len(y)
        w -= 1.0 * g
    return lambda Z: np.hstack([Z, np.ones((len(Z), 1))]) @ w


def prep(*mats):
    allm = np.vstack([m for m in mats if len(m)]); mu, sdv = np.nanmean(allm, 0), np.nanstd(allm, 0) + 1e-9
    return lambda m: np.nan_to_num((m - mu) / sdv)


def cmd_critic(a):
    fz = cl.load().get("C3")
    if not fz:
        raise SystemExit("C3 is not frozen: run claims.py freeze C3")
    eng = [e for e in a.engines.split(",") if e]
    test_map, test_rand, tr_rand = [], [], []
    for f in cl.own_files(eng):
        name = os.path.basename(f); X, I, D = own_matrix(f)
        held = set(fz["sets"].get("random", {}).get("data", {}).get(name, []))
        for x, i, dsn in zip(X, I, D):
            if i in held: test_rand.append(x)
            elif dsn in ("base", "uniform", "propose"): tr_rand.append(x)
    ids = set(fz["sets"]["map"]["data"].get("diva_m1.jsonl", []))
    dm, _, _ = diva_matrix(); tm, _, _ = diva_matrix(ids)
    held_src = set(fz["sets"]["map"]["data"].get("features.jsonl", []))
    sm, _ = sample_matrix(held_src) if held_src else (np.zeros((0, len(AX))), [])
    trs, _ = sample_matrix(None, 4000)
    test_map = np.vstack([m for m in (tm, sm) if len(m)])
    train_map = np.vstack([m for m in (dm, trs) if len(m)])
    # train rows must not be test rows: remove diva held-out from train
    if len(tm): train_map = np.vstack([m for m in (diva_matrix_excluding(ids), trs) if len(m)])
    z = prep(train_map, np.array(tr_rand))
    f = logit(z(np.vstack([train_map, tr_rand])), np.r_[np.ones(len(train_map)), np.zeros(len(tr_rand))])
    s = f(z(np.vstack([test_map, np.array(test_rand)]))); y = np.r_[np.ones(len(test_map)), np.zeros(len(test_rand))]
    # baseline: loudness alone
    print("C3 critic | train map %d, random %d | test map %d (diva %d, samples %d), random %d | AUC %.3f (bar 0.8) | baseline loudness-only %.3f"
          % (len(train_map), len(tr_rand), len(test_map), len(tm), len(sm), len(test_rand), auc(s, y),
             auc(np.nan_to_num(np.vstack([test_map, np.array(test_rand)])[:, 0]), y)))


def diva_matrix_excluding(ids):
    X, I, _ = diva_matrix()
    return X[[i not in ids for i in I]] if len(X) else X


def nn_dist(Q, M, chunk=500):
    out = []
    for i in range(0, len(Q), chunk):
        d = ((Q[i:i + chunk, None, :] - M[None, :, :]) ** 2).sum(2) ** .5
        out.append(d.min(1))
    return np.concatenate(out)


def cmd_novelty(a):
    dm, _, _ = diva_matrix(); sm, _ = sample_matrix(None, 5000); M = np.vstack([m for m in (dm, sm) if len(m)])
    X, I, D = own_matrix(cl.AUDIO and os.path.join(AUDIO, "own_%s.jsonl" % a.node.lower()))
    z = prep(M); Mz, Xz = z(M), z(X)
    sub = Mz[np.random.default_rng(0).choice(len(Mz), min(1500, len(Mz)), False)]
    inner = np.median([np.sort(((sub[i] - sub) ** 2).sum(1) ** .5)[1] for i in range(0, len(sub), 5)])
    N = np.minimum(nn_dist(Xz, Mz) / inner, 2.0) / 2.0
    lo, hi = np.nanpercentile(M, 5, 0), np.nanpercentile(M, 95, 0)
    R = ((X < lo) | (X > hi)).mean(1)
    for des in sorted(set(D)):
        m = np.array([d == des for d in D])
        print("%s %-8s n=%4d | N mean %.3f | R mean %.3f" % (a.node, des, m.sum(), N[m].mean(), R[m].mean()))


def cmd_clusters(a):
    dm, _, dn = diva_matrix(); sm, sn = sample_matrix(None, 8000); M = np.vstack([dm, sm]); names = list(dn) + sn
    z = prep(M); Z = z(M); rng = np.random.default_rng(0); C = Z[rng.choice(len(Z), a.k, False)]
    for _ in range(25):
        lab = np.argmin(((Z[:, None] - C[None]) ** 2).sum(2), 1)
        C = np.array([Z[lab == k].mean(0) if (lab == k).any() else C[k] for k in range(a.k)])
    df = collections.Counter(w for n in names for w in set(re.findall(r"[a-z]{3,}", n.lower())))
    print("| cluster | n | words (by lift) | centre (z) |\n|---|---|---|---|")
    for k in np.argsort(-np.bincount(lab, minlength=a.k)):
        idx = np.flatnonzero(lab == k)
        if len(idx) < 5: continue
        cnt = collections.Counter(w for i in idx for w in set(re.findall(r"[a-z]{3,}", names[i].lower())))
        top = sorted(cnt, key=lambda w: -(cnt[w] / len(idx)) / (df[w] / len(names)) * (cnt[w] >= 3))[:5]
        print("| %d | %d | %s | %s |" % (k, len(idx), ", ".join(top), " ".join("%s%+.1f" % (AX[j][:4], C[k, j]) for j in np.argsort(-abs(C[k]))[:3])))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["critic", "novelty", "clusters", "report"]); ap.add_argument("--engines", default="")
    ap.add_argument("--node", default="Analog"); ap.add_argument("--k", type=int, default=40)
    a = ap.parse_args()
    if a.cmd == "report":
        cmd_critic(a); cmd_novelty(a); cmd_clusters(a)
    else:
        {"critic": cmd_critic, "novelty": cmd_novelty, "clusters": cmd_clusters}[a.cmd](a)


if __name__ == "__main__":
    main()
