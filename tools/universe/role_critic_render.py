#!/usr/bin/env python3
"""Goal item 4 on the generator's own audio: render every part of every style with the live generator, measure it frame by frame,
and train the role critic on exact labels (the part the generator was asked for), with whole styles held out.
    role_critic_render.py render [--seeds 10] [--first 9000]       -> ~/Infinite/training-data/audio/gen_parts.jsonl (features only, no audio kept)
    role_critic_render.py train  [--folds 5] [--epochs 40] [--out FILE]
Test: K-fold by style (no style in both train and test). Thresholds per role: 3rd percentile of P(true role) on a validation split of the
training styles. FRR = held-out parts rejected in their own role; catch = rejected when declared as each other role. Bars FRR <= 3%, catch >= 96%.
'Nested' pairs (Drums holds kick/snare/hats, Synths holds bass and the lead line, Percussion holds toms) are reported apart as a diagnostic;
the bar is read on all pairs."""
import argparse, collections, json, os, subprocess, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import frame_features as ff
from role_critic_samples import MLP, Linear, score

OUT = os.path.expanduser("~/Infinite/training-data/audio/gen_parts.jsonl")
GEN = os.path.join(HERE, "../../website/gen")
NESTED = {("drums", "kicks"), ("drums", "snares"), ("drums", "hats"), ("synths", "bass"), ("synths", "leads"), ("percussion", "toms"), ("percussion", "snares")}

def render(a):
    p = subprocess.Popen(["node", os.path.join(HERE, "wasm_render_parts.js"), GEN, str(a.first), str(a.seeds)], stdout=subprocess.PIPE)
    f, n = p.stdout, 0
    with open(OUT, "a") as o:
        while True:
            h = f.readline()
            if not h: break
            r = json.loads(h); x = np.frombuffer(f.read(r["frames"] * 8), np.float32).reshape(-1, 2).astype(float)
            r["role"] = r["label"].split()[0]; r["seconds"] = round(r["frames"] / 48000, 2)
            r.update(ff.measure(x[:, 0], x[:, 1], 48000)); o.write(json.dumps(r) + "\n"); n += 1
            if n % 200 == 0: print(n, "measured", flush=True)
    p.wait(); print(n, "rows ->", OUT)

def train(a):
    rows = [json.loads(l) for l in open(OUT)]
    X = np.nan_to_num(np.array([ff.vector(r) for r in rows])); names = sorted({r["role"] for r in rows}); y = np.array([names.index(r["role"]) for r in rows])
    styles = sorted({r["id"] for r in rows}); g = np.array([styles.index(r["id"]) for r in rows])
    print(len(y), "renders;", len(styles), "styles;", len(names), "roles:", dict(collections.Counter(r["role"] for r in rows)))
    rng = np.random.default_rng(0); order = rng.permutation(len(styles)); fold_of = (np.argsort(order) % a.folds)[g]
    nested = np.zeros((len(names), len(names)), bool)
    for p, q in NESTED:
        if p in names and q in names: nested[names.index(p), names.index(q)] = nested[names.index(q), names.index(p)] = True
    res = {"Linear": [], "MLP": []}; per_role = {k: collections.defaultdict(list) for k in res}; conf = np.zeros((len(names), len(names)))
    for f in range(a.folds):
        te = fold_of == f; trs = np.unique(g[~te]); val = set(rng.choice(trs, max(1, len(trs) // 6), replace=False))
        va = np.isin(g, list(val)) & ~te; tr = ~te & ~va
        mu, sd = X[tr].mean(0), X[tr].std(0) + 1e-9; Z = (X - mu) / sd
        w = (1.0 / np.maximum(np.bincount(y[tr], minlength=len(names)), 1))[y[tr]]
        for name, M in (("Linear", Linear(len(names), a.epochs)), ("MLP", MLP(len(names), a.epochs, h=(128, 64), seed=f))):
            M.fit(Z[tr], y[tr], w)
            Pv = M.proba(Z[va]); th = np.array([np.percentile(Pv[y[va] == c, c], 3) if (y[va] == c).sum() >= 10 else 0.0 for c in range(len(names))])
            P = M.proba(Z[te]); frr, catch, top1, per = score(P, y[te], th)
            rej = P < th[None, :]; yt = y[te]; mask = ~nested[yt] ; mask[np.arange(len(yt)), yt] = False
            catch_flat = float(rej[mask].mean())
            res[name].append((frr, catch, top1, catch_flat))
            for c, v in per.items(): per_role[name][names[c]].append(v)
            if name == "MLP":
                for t, p_ in zip(yt, P.argmax(1)): conf[t, p_] += 1
            print("fold %d %-6s FRR %.3f catch %.3f (non-nested %.3f) top-1 %.3f (%d test renders, %d styles)" % (f, name, frr, catch, catch_flat, top1, te.sum(), len(np.unique(g[te]))), flush=True)
    out = {"roles": names, "n": int(len(y)), "styles": len(styles), "split": "%d-fold, whole styles held out" % a.folds, "features": "frame_features.py"}
    for name, R in res.items():
        R = np.array(R); out[name] = {"frr": R[:, 0].tolist(), "catch": R[:, 1].tolist(), "top1": R[:, 2].tolist(), "catch_non_nested": R[:, 3].tolist(),
                                       "catch_by_role": {k: round(float(np.mean(v)), 3) for k, v in per_role[name].items()}}
        print("%-6s FRR %.3f+-%.3f | catch %.3f+-%.3f (non-nested %.3f) | top-1 %.3f" % (name, R[:, 0].mean(), R[:, 0].std(), R[:, 1].mean(), R[:, 1].std(), R[:, 3].mean(), R[:, 2].mean()))
    cn = conf / np.maximum(conf.sum(1, keepdims=True), 1); np.fill_diagonal(cn, 0)
    out["top_confusions"] = [[names[i], names[j], round(float(cn[i, j]), 3)] for i, j in zip(*np.unravel_index(np.argsort(-cn, None)[:10], cn.shape))]
    print("confusions", out["top_confusions"])
    best = max(("Linear", "MLP"), key=lambda k: np.mean(out[k]["catch"]))
    out["verdict"] = "MET" if np.mean(out[best]["frr"]) <= 0.03 and np.mean(out[best]["catch"]) >= 0.96 else "MISS"; print("verdict", out["verdict"], "(", best, ")")
    if a.out: json.dump(out, open(a.out, "w"), indent=1)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["render", "train"]); ap.add_argument("--seeds", type=int, default=10)
    ap.add_argument("--first", type=int, default=9000); ap.add_argument("--folds", type=int, default=5); ap.add_argument("--epochs", type=int, default=40); ap.add_argument("--out")
    a = ap.parse_args(); render(a) if a.cmd == "render" else train(a)
