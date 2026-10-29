#!/usr/bin/env python3
"""IMPROVEMENT-PLAN 7.1: can a take's style be told from its audio? Trained and tested on the live generator's own renders (exact style labels).
    style_critic.py [--folds 5] [--out docs/plans/data-universe/feel/style_critic.json]
Data: role_critic_render.py rows (gen_parts_v2.jsonl): each row is one part of one take; a take = (style, seed offset k, dots or rest).
Per part: MLP on frame_features.vector -> P(style). Per take: sum of the parts' log P(style). Folds split by k, so no take is in both train and test
(other takes of the same style are, which is the point: does a new take of a style sound like that style).
--tempo adds the take's tempo (log2 bpm). Reported for rest takes and dot-shaped takes apart, top-1 and top-3. This is a step toward C8 (real music, top-3 >= 0.7), not C8 itself:
real loops by genre need frame features on real audio (WD Black drive)."""
import argparse, collections, json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import frame_features as ff
from role_critic_samples import MLP
from role_critic_render import OUT as ROWS
OUT = os.path.join(HERE, "../../docs/plans/data-universe/feel/style_critic.json")

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--folds", type=int, default=5); ap.add_argument("--epochs", type=int, default=40); ap.add_argument("--out", default=OUT)
    ap.add_argument("--tempo", action="store_true", help="add the take's tempo (log2 bpm; measurable from real audio too)"); a = ap.parse_args()
    rows = [json.loads(l) for l in open(ROWS)]
    X = np.nan_to_num(np.array([np.r_[ff.vector(r), [np.log2(r["bpm"])] * (3 if a.tempo else 0)] for r in rows])); styles = sorted({r["id"] for r in rows}); y = np.array([styles.index(r["id"]) for r in rows])
    k = np.array([(r["seed"] - r["style"] * 1009 - r["part"] * 101) % 10000 for r in rows]); dots = np.array([bool(r.get("dots")) for r in rows])
    take = np.array(["%s|%d|%d|%d" % (r["id"], k[i], dots[i], (r["seed"] - r["style"] * 1009 - r["part"] * 101) // 10000) for i, r in enumerate(rows)])
    fold = k % a.folds; print(len(rows), "part renders,", len(set(take)), "takes,", len(styles), "styles")
    res = collections.defaultdict(list); conf = np.zeros((len(styles), len(styles)))
    for f in range(a.folds):
        tr, te = fold != f, fold == f
        mu, sd = X[tr].mean(0), X[tr].std(0) + 1e-9; Z = (X - mu) / sd
        M = MLP(len(styles), a.epochs, h=(128, 64), seed=f).fit(Z[tr], y[tr], np.ones(tr.sum())); L = np.log(M.proba(Z[te]) + 1e-9)
        for name, m in (("rest", ~dots[te]), ("dots", dots[te])):
            if not m.any(): continue
            Lp, yp = L[m], y[te][m]; rk = (Lp > Lp[np.arange(len(yp)), yp][:, None]).sum(1)
            res[name + "_part_top1"].append(float((rk == 0).mean())); res[name + "_part_top3"].append(float((rk < 3).mean()))
            T = take[te][m]; ut = sorted(set(T)); S = np.array([Lp[T == t].sum(0) for t in ut]); yt = np.array([yp[T == t][0] for t in ut])
            rk = (S > S[np.arange(len(yt)), yt][:, None]).sum(1)
            res[name + "_take_top1"].append(float((rk == 0).mean())); res[name + "_take_top3"].append(float((rk < 3).mean()))
            if name == "rest":
                for t_, p_ in zip(yt, S.argmax(1)): conf[t_, p_] += 1
        print("fold", f, {k_: round(v[-1], 3) for k_, v in res.items()}, flush=True)
    out = {"tempo_feature": a.tempo, "styles": styles, "n_parts": len(rows), "n_takes": len(set(take)), "split": "%d folds by seed offset (whole takes held out)" % a.folds,
           **{k_: {"mean": round(float(np.mean(v)), 3), "sd": round(float(np.std(v)), 3)} for k_, v in res.items()}}
    cn = conf / np.maximum(conf.sum(1, keepdims=True), 1); np.fill_diagonal(cn, 0)
    out["top_confusions"] = [[styles[i], styles[j], round(float(cn[i, j]), 3)] for i, j in zip(*np.unravel_index(np.argsort(-cn, None)[:10], cn.shape)) if cn[i, j] > 0]
    for k_ in sorted(res): print("%-16s %.3f +- %.3f" % (k_, np.mean(res[k_]), np.std(res[k_])))
    print("confusions", out["top_confusions"]); json.dump(out, open(a.out, "w"), indent=1)

if __name__ == "__main__":
    main()
