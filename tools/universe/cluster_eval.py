#!/usr/bin/env python3
"""Goal item 4: does a new instrument land in the right sound cluster from its audio alone?
Per family with >= 24 audio instruments: reference clusters = sound_clusters.best_k on the whole family. Then 5 random 80/20 splits per
seed (seeds 0 and 1): re-cluster on the 80% only, map each train cluster to the reference cluster most of its members belong to, assign the
held-out 20% to the nearest train centroid, and score the mapped label against the reference label. Reports instrument-weighted accuracy,
mean +- sd over the 10 splits, per seed, and the gap between seeds (bar: >= 85%, seeds within 3 points)."""
import collections, json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sound_clusters as sc
ATLAS = os.path.expanduser("~/Infinite/training-data/atlas/instruments.jsonl")
OUT = os.path.expanduser("~/Infinite/repo/docs/plans/data-universe/feel/cluster_eval.json")

def zmat(rows):
    F = np.array([sc.feat(r["audio"]) for r in rows]); cm = np.nan_to_num(np.nanmean(F, 0)); F = np.where(np.isnan(F), cm, F)
    return (F - F.mean(0)) / (F.std(0) + 1e-9)

def main():
    rows = [json.loads(l) for l in open(ATLAS)]; fam = collections.defaultdict(list)
    for r in rows:
        if r.get("audio") and r["audio"].get("bands"): fam[r["family"]].append(r)
    res = {"per_seed": {}, "families": {}}; hit = {0: [0, 0], 1: [0, 0]}; splits = {0: [], 1: []}
    for f, rs in sorted(fam.items()):
        if len(rs) < 24: continue
        Z = zmat(rs); ref, _, _ = sc.best_k(Z, 12); accs = {0: [], 1: []}
        for seed in (0, 1):
            rng = np.random.default_rng(seed)
            for _ in range(5):
                p = rng.permutation(len(Z)); te, tr = p[: len(Z) // 5], p[len(Z) // 5:]
                lab, C, _ = sc.best_k(Z[tr], 12)
                m = {c: collections.Counter(ref[tr][lab == c]).most_common(1)[0][0] for c in range(len(C)) if (lab == c).any()}
                pred = np.array([m.get(c, -1) for c in ((Z[te][:, None, :] - C[None]) ** 2).sum(2).argmin(1)])
                ok = int((pred == ref[te]).sum()); hit[seed][0] += ok; hit[seed][1] += len(te); accs[seed].append(ok / len(te))
        res["families"][f] = {"n": len(rs), "k": int(ref.max() + 1), "acc_seed0": round(float(np.mean(accs[0])), 3), "acc_seed1": round(float(np.mean(accs[1])), 3)}
        for s in (0, 1): splits[s] += accs[s]
    for s in (0, 1): res["per_seed"][s] = {"acc": round(hit[s][0] / hit[s][1], 4), "sd_over_splits": round(float(np.std(splits[s])), 3), "heldout_n": hit[s][1]}
    res["gap"] = round(abs(res["per_seed"][0]["acc"] - res["per_seed"][1]["acc"]), 4)
    res["bar"] = ">= 0.85 held-out, seeds within 0.03"; json.dump(res, open(OUT, "w"), indent=1)
    print(json.dumps(res["per_seed"]), "gap", res["gap"]); [print(f, v) for f, v in res["families"].items()]

if __name__ == "__main__": main()
