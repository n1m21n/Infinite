#!/usr/bin/env python3
"""Goal item 4: sound-side per-part critic. Does a voice's measured audio fit the role (instrument family) it is placed in?
Objective: multinomial logistic regression P(family | audio features) on the 2,158 audio-measured instruments (atlas/instruments.jsonl).
Baseline: always accept (catch 0) / accept by family prior. Test: 5 random 80/20 splits x 2 seeds. The accept threshold is the 3rd percentile of
P(true family) on the TRAIN rows of each fold (target false reject 3%); on the held-out 20%: FRR = share of true-role voices rejected,
catch = share of the same voices rejected when declared as a wrong family (each voice swapped to every other family). Bars: FRR <= 3% (+0.5 slack reported), catch >= 96%.
    role_critic.py"""
import collections, json, os, sys
import numpy as np

class LogisticRegression:
    """Softmax regression, L2, plain gradient descent (no sklearn on this Mac)."""
    def __init__(self, max_iter=400, C=1.0): self.it, self.C = max_iter, C
    def fit(self, X, y):
        k = y.max() + 1; Y = np.eye(k)[y]; self.W = np.zeros((X.shape[1] + 1, k)); Xb = np.c_[X, np.ones(len(X))]
        for _ in range(self.it):
            Z = Xb @ self.W; Z -= Z.max(1, keepdims=True); P = np.exp(Z); P /= P.sum(1, keepdims=True)
            self.W -= 0.3 * (Xb.T @ (P - Y) / len(X) + self.W / (self.C * len(X)))
        return self
    def predict_proba(self, X):
        Z = np.c_[X, np.ones(len(X))] @ self.W; Z -= Z.max(1, keepdims=True); P = np.exp(Z); return P / P.sum(1, keepdims=True)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sound_clusters as sc
ATLAS = os.path.expanduser("~/Infinite/training-data/atlas/instruments.jsonl")
rows = [json.loads(l) for l in open(ATLAS)]; rows = [r for r in rows if r.get("audio") and r["audio"].get("bands")]
F = np.array([sc.feat(r["audio"]) for r in rows]); cm = np.nan_to_num(np.nanmean(F, 0)); F = np.where(np.isnan(F), cm, F)
Z = (F - F.mean(0)) / (F.std(0) + 1e-9)
fams = sorted({r["family"] for r in rows if sum(x["family"] == r["family"] for x in rows) >= 24}); keep = [i for i, r in enumerate(rows) if r["family"] in fams]
Z = Z[keep]; y = np.array([fams.index(rows[i]["family"]) for i in keep]); print(len(y), "voices;", len(fams), "roles:", fams)
res = []
for seed in (0, 1):
    rng = np.random.default_rng(seed)
    for fold in range(5):
        idx = rng.permutation(len(y)); cut = int(.8 * len(y)); tr, te = idx[:cut], idx[cut:]
        m = LogisticRegression(max_iter=2000, C=1.0).fit(Z[tr], y[tr]); P = lambda ii: m.predict_proba(Z[ii])
        th = np.percentile(P(tr)[np.arange(len(tr)), y[tr]], 3)
        pt = P(te); frr = float((pt[np.arange(len(te)), y[te]] < th).mean())
        wrong = [pt[i, f] < th for i in range(len(te)) for f in range(len(fams)) if f != y[te][i]]
        res.append((frr, float(np.mean(wrong)), float((pt.argmax(1) == y[te]).mean())))
r = np.array(res); print("FRR %.3f+-%.3f | catch(swapped role) %.3f+-%.3f | top-1 role accuracy %.3f+-%.3f" % (r[:,0].mean(), r[:,0].std(), r[:,1].mean(), r[:,1].std(), r[:,2].mean(), r[:,2].std()))
json.dump({"roles": fams, "frr": r[:,0].tolist(), "catch": r[:,1].tolist(), "top1": r[:,2].tolist()}, open(os.path.expanduser("~/Infinite/repo/docs/plans/data-universe/feel/role_critic.json"), "w"))
