#!/usr/bin/env python3
"""Kit compatibility: one small gradient-boosted model per role pair scoring P(these two sounds come together).
    ~/Infinite/media/.ytvenv/bin/python3 -I tools/universe/compat_model.py [--folds 4] [--out internal/plans/generate-samples/refs/compat.json]
Held-out check: groups (kits, packs, racks, sets, projects) are split into folds; every fold trains on the other groups and is
scored on pairs built only from its own groups, so the AUC is on packs and tracks the model never saw. A role pair under
AUC 0.70 (or with fewer than 200 held-out positives) is marked weak: the generator falls back to the genre prior for it.
Exported per pair: "run", fit on one half of the groups (the engine picks kits with it), and "check", fit on the other half
(the engine's held-out score: a pick that also raises the check model's score generalises past the data it was picked with).
Histogram gradient-boosted trees (80 trees, depth 3, 8 leaves) per pair, exported as flat arrays the engine walks; the logistic AUC is listed
beside it for reference."""
import argparse, hashlib, json, os, sys
import numpy as np
from sklearn.ensemble import HistGradientBoostingClassifier
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import roc_auc_score

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pairs as pr  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
WEAK_AUC, MIN_POS = 0.70, 200


def fold_of(g, k):
    return int(hashlib.md5(g.encode()).hexdigest(), 16) % k


TREES, DEPTH, RATE = 80, 3, 0.15


def fit_gb(X, y):
    return HistGradientBoostingClassifier(max_iter=TREES, max_depth=DEPTH, learning_rate=RATE, max_leaf_nodes=8,
                                          early_stopping=False, random_state=1).fit(X, y)


def export_gb(m, X):
    """trees as flat arrays: feature (-1 = leaf), threshold (x <= t goes left), left, right, value;
    raw score = offset + rate * sum(leaf values) (leaf values already carry the learning rate, so rate is 1)"""
    trees = []
    for (pred,) in m._predictors:
        nd = pred.nodes
        trees.append({"f": [-1 if int(n["is_leaf"]) else int(n["feature_idx"]) for n in nd],
                      "t": [float(n["num_threshold"]) for n in nd],
                      "l": [int(n["left"]) for n in nd], "r": [int(n["right"]) for n in nd],
                      "v": [float(n["value"]) for n in nd]})
    out = {"offset": float(np.ravel(m._baseline_prediction)[0]), "rate": 1.0, "trees": trees}
    # the walk the engine does must give sklearn's own score
    xs = X[:200]
    got = np.array([out["offset"] + sum(walk(t, x) for t in trees) for x in xs])
    err = float(np.abs(got - m.decision_function(xs)).max())
    assert err < 1e-6, "tree export does not reproduce the model (%g)" % err
    return out


def walk(t, x):
    i = 0
    while t["f"][i] >= 0:
        i = t["l"][i] if x[t["f"][i]] <= t["t"][i] else t["r"][i]
    return t["v"][i]


# Kits the engine draws as one set: a kit is scored by the mean P(together) over its role pairs. Held out the same way:
# each fold's models score whole kits from its own groups (real: one group's kick, snare, hat...) against kits whose
# other roles come from other groups. A set at AUC 0.70 or more exports its pairs even when a pair alone is under it.
SETS = [("kick", "snare", "hat"), ("kick", "snare", "hat", "bass"), ("kick", "bass", "keys", "pad"), ("bass", "keys", "lead")]


def set_pairs(roles):
    out = []
    for i in range(len(roles)):
        for j in range(i + 1, len(roles)):
            out.append((roles[i], roles[j]) if (roles[i], roles[j]) in pr.PAIRS else (roles[j], roles[i]))
    return out


def set_check(rows, groups, folds, roles, fold_models, per_group=10):
    import collections, random
    ys, ss = [], []
    for f in range(folds):
        ps = set_pairs(roles)
        if any((f, p) not in fold_models for p in ps):
            continue
        te = {g for g in groups if fold_of(g, folds) == f}
        by = collections.defaultdict(lambda: collections.defaultdict(list)); pool = collections.defaultdict(list)
        for r in rows:
            if r["group"] in te:
                by[r["group"]][r["role"]].append(r); pool[r["role"]].append(r)
        rng = random.Random(f)
        kits, lab = [], []
        for g, rr in by.items():
            if not all(rr.get(x) for x in roles):
                continue
            for _ in range(per_group):
                kit = {x: rng.choice(rr[x]) for x in roles}
                mix = dict(kit)
                for x in roles[1:]:
                    for _try in range(50):
                        o = rng.choice(pool[x])
                        if o["group"] != g:
                            break
                    mix[x] = o
                kits += [kit, mix]; lab += [1, 0]
        if not kits:
            continue
        sc = np.zeros(len(kits))
        for p in ps:
            X = np.array([pr.pair_features(k[p[0]]["v"], k[p[1]]["v"]) for k in kits])
            sc += fold_models[(f, p)].predict_proba(X)[:, 1]
        ys += lab; ss += list(sc / len(ps))
    if len(set(ys)) < 2:
        return None, 0
    return float(roc_auc_score(ys, ss)), len(ys) // 2


def fit(X, y):
    mu = X.mean(0); sd = X.std(0); sd[sd < 1e-9] = 1.0
    m = LogisticRegression(C=0.5, max_iter=2000).fit((X - mu) / sd, y)
    return m, mu, sd


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--folds", type=int, default=4)
    ap.add_argument("--out", default=os.path.join(ROOT, "internal/plans/generate-samples/refs/compat.json"))
    a = ap.parse_args()
    rows = pr.load()
    groups = sorted({r["group"] for r in rows})
    scores = {p: ([], []) for p in pr.PAIRS}  # held-out labels, scores (gradient-boosted)
    lr_scores = {p: ([], []) for p in pr.PAIRS}  # the same with a logistic model, for the table
    fold_models = {}  # (fold, pair) -> gradient-boosted model, for the set check
    for f in range(a.folds):
        te = {g for g in groups if fold_of(g, a.folds) == f}
        tr = set(groups) - te
        Ptr, Pte = pr.build(rows, tr, seed=1 + f), pr.build(rows, te, seed=101 + f)
        for p in pr.PAIRS:
            if p not in Ptr or p not in Pte or len(set(Ptr[p][1])) < 2 or len(set(Pte[p][1])) < 2:
                continue
            m, mu, sd = fit(Ptr[p][0], Ptr[p][1])
            lr_scores[p][0].extend(Pte[p][1]); lr_scores[p][1].extend(m.predict_proba((Pte[p][0] - mu) / sd)[:, 1])
            gb = fit_gb(Ptr[p][0], Ptr[p][1])
            fold_models[(f, p)] = gb
            scores[p][0].extend(Pte[p][1]); scores[p][1].extend(gb.predict_proba(Pte[p][0])[:, 1])
    set_auc = {k: set_check(rows, groups, a.folds, k, fold_models) for k in SETS}
    strong_sets = [k for k in SETS if set_auc[k][0] is not None and set_auc[k][0] >= WEAK_AUC and set_auc[k][1] >= MIN_POS]
    in_set = {p for k in strong_sets for p in set_pairs(k)}
    Pall = pr.build(rows, set(groups), seed=7)
    out = {"features": pr.NAMES, "weak_auc": WEAK_AUC, "min_heldout_pos": MIN_POS, "sounds": len(rows), "groups": len(groups), "pairs": {}}
    lines = ["role pair      AUC    LR     n_pos  heldout_pos  groups  status", "-------------  -----  -----  -----  -----------  ------  ------"]
    for p in pr.PAIRS:
        ys, ss = scores[p]
        auc = roc_auc_score(ys, ss) if len(set(ys)) == 2 else None
        hpos = int(np.sum(ys)) if ys else 0
        if p not in Pall:
            lines.append("%-13s  %5s  %5s  %5d  %11d  %6d  %s" % ("-".join(p), "-", "-", 0, 0, 0, "weak (no data)"))
            out["pairs"]["-".join(p)] = {"auc": None, "n": 0, "weak": True}
            continue
        X, y, G = Pall[p]
        half = np.array([fold_of(g, 2) for g in G])
        models = {}
        for name, h in (("run", 0), ("check", 1)):
            if len(set(y[half == h])) < 2:
                continue
            models[name] = export_gb(fit_gb(X[half == h], y[half == h]), X[half == h])
        ly, ls = lr_scores[p]
        lr_auc = roc_auc_score(ly, ls) if len(set(ly)) == 2 else None
        weak = (auc is None or auc < WEAK_AUC or hpos < MIN_POS) and p not in in_set
        out["pairs"]["-".join(p)] = {"auc": None if auc is None else round(float(auc), 3), "n": int(y.sum()), "heldout_pos": hpos,
                                     "groups": int(len(set(G[y == 1]))), "weak": bool(weak or len(models) < 2), "lr_auc": None if lr_auc is None else round(float(lr_auc), 3),
                                     **(models if not weak else {})}
        lines.append("%-13s  %5s  %5s  %5d  %11d  %6d  %s" % ("-".join(p), "-" if auc is None else "%.3f" % auc,
                                                          "-" if lr_auc is None else "%.3f" % lr_auc, int(y.sum()), hpos,
                                                          len(set(G[y == 1])), "weak" if weak else "ok" if auc >= WEAK_AUC else "ok in set"))
    lines += ["", "kit set (held out)       AUC    sets  status", "-----------------------  -----  ----  ------"]
    for k in SETS:
        v, n = set_auc[k]
        lines.append("%-23s  %5s  %4d  %s" % ("-".join(k), "-" if v is None else "%.3f" % v, n, "ok" if k in strong_sets else "weak"))
    out["sets"] = {"-".join(k): {"auc": None if set_auc[k][0] is None else round(set_auc[k][0], 3), "n": set_auc[k][1], "weak": k not in strong_sets} for k in SETS}
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    json.dump(out, open(a.out, "w"), indent=1)
    table = "\n".join(lines)
    print("%d sounds, %d groups\n%s" % (len(rows), len(groups), table))
    open(os.path.join(os.path.dirname(a.out), "compat_table.txt"), "w").write(table + "\n")


if __name__ == "__main__":
    main()
