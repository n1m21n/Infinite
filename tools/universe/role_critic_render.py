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

OUT = os.path.expanduser("~/Infinite/training-data/audio/gen_parts_v2.jsonl")   # v2: note features; gen_parts.jsonl is the first run
GEN = os.path.join(HERE, "../../website/gen")
NESTED = {("drums", "kicks"), ("drums", "snares"), ("drums", "hats"), ("synths", "bass"), ("synths", "leads"), ("percussion", "toms"), ("percussion", "snares")}

def render(a):
    env = dict(os.environ, STYLE_FROM=str(a.style_from), STYLE_STEP=str(a.style_step))
    p = subprocess.Popen(["node", os.path.join(HERE, "wasm_render_parts.js"), GEN, str(a.first), str(a.seeds), "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15"] + (["shaped"] if a.shaped else []), stdout=subprocess.PIPE, env=env)
    f, n = p.stdout, 0
    with open(a.rows, "a") as o:
        while True:
            h = f.readline()
            if not h: break
            r = json.loads(h); x = np.frombuffer(f.read(r["frames"] * 8), np.float32).reshape(-1, 2).astype(float)
            r["role"] = r["label"].split()[0]; r["seconds"] = round(r["frames"] / 48000, 2); r["dots"] = r.pop("shape")   # frame features also write 'shape'
            r.update(ff.measure(x[:, 0], x[:, 1], 48000)); o.write(json.dumps(r) + "\n"); n += 1
            if n % 200 == 0: print(n, "measured", flush=True)
    p.wait(); print(n, "rows ->", a.rows)

def train(a):
    rows = [json.loads(l) for l in open(OUT)]
    X = np.nan_to_num(np.array([ff.vector(r) for r in rows])); names = sorted({r["role"] for r in rows}); y = np.array([names.index(r["role"]) for r in rows])
    styles = sorted({r["id"] for r in rows}); g = np.array([styles.index(r["id"]) for r in rows])
    print(len(y), "renders;", len(styles), "styles;", len(names), "roles:", dict(collections.Counter(r["role"] for r in rows)))
    rng = np.random.default_rng(0); order = rng.permutation(len(styles)); fold_of = (np.argsort(order) % a.folds)[g]
    nested = np.zeros((len(names), len(names)), bool)
    for p, q in NESTED:
        if p in names and q in names: nested[names.index(p), names.index(q)] = nested[names.index(q), names.index(p)] = True
    shaped = np.array([bool(r.get("dots")) for r in rows]); allowed = shaped | ~shaped if a.train_on == "all" else ~shaped
    print("shaped renders:", int(shaped.sum()), "| training on:", a.train_on)
    subsets = {"rest": ~shaped, "shaped": shaped}
    res = {m: {k: [] for k in subsets} for m in ("Linear", "MLP")}; per_role = {m: collections.defaultdict(list) for m in res}; conf = np.zeros((len(names), len(names)))
    for f in range(a.folds):
        te = fold_of == f; trs = np.unique(g[~te & allowed])
        if a.calib == "val":   # one validation slice of training styles sets the cut-offs; the model trains on the rest
            val = set(rng.choice(trs, max(1, len(trs) // 6), replace=False)); va = np.isin(g, list(val)) & ~te & allowed; tr = ~te & ~va & allowed
        else:                  # out-of-fold: every training style is scored once by a model that never saw it; the model trains on all of them
            tr = ~te & allowed; inner = {s: i % a.inner for i, s in enumerate(rng.permutation(trs))}
        mu, sd = X[tr].mean(0), X[tr].std(0) + 1e-9; Z = (X - mu) / sd
        wt = lambda m: (1.0 / np.maximum(np.bincount(y[m], minlength=len(names)), 1))[y[m]]
        make = {"Linear": lambda s: Linear(len(names), a.epochs), "MLP": lambda s: MLP(len(names), a.epochs, h=(128, 64), seed=s)}
        for name in ("Linear", "MLP"):
            M = make[name](f).fit(Z[tr], y[tr], wt(tr))
            if a.calib == "val":
                Pc, yc = M.proba(Z[va]), y[va]
            else:
                Pc = np.zeros((len(y), len(names)))
                for k in range(a.inner):
                    hold = tr & np.array([inner.get(s, -1) == k for s in g]); fit = tr & ~hold
                    Pc[hold] = make[name](100 + 10 * f + k).fit(Z[fit], y[fit], wt(fit)).proba(Z[hold])
                cm = tr & (shaped if a.calib_on == "shaped" else True); Pc, yc = Pc[cm], y[cm]
            th = np.array([np.percentile(Pc[yc == c, c], a.pct) if (yc == c).sum() >= 10 else 0.0 for c in range(len(names))])
            for sub, sm in subsets.items():
                t = te & sm
                if not t.any(): continue
                P = M.proba(Z[t]); yt = y[t]; frr, catch, top1, per = score(P, yt, th)
                rej = P < th[None, :]; mask = ~nested[yt]; mask[np.arange(len(yt)), yt] = False
                res[name][sub].append((frr, catch, top1, float(rej[mask].mean())))
                if sub == "shaped" or not shaped.any():
                    for c, v in per.items(): per_role[name][names[c]].append(v)
                    if name == "MLP":
                        for t_, p_ in zip(yt, P.argmax(1)): conf[t_, p_] += 1
                print("fold %d %-6s %-6s FRR %.3f catch %.3f top-1 %.3f (%d test renders)" % (f, name, sub, frr, catch, top1, t.sum()), flush=True)
    out = {"roles": names, "n": int(len(y)), "shaped": int(shaped.sum()), "train_on": a.train_on, "styles": len(styles),
           "split": "%d-fold, whole styles held out" % a.folds, "features": "frame_features.py (+ notes)", "calib": a.calib, "calib_on": a.calib_on, "pct": a.pct}
    for name in res:
        out[name] = {"catch_by_role": {k: round(float(np.mean(v)), 3) for k, v in per_role[name].items()}}
        for sub, R in res[name].items():
            if not R: continue
            R = np.array(R); out[name][sub] = {"frr": R[:, 0].tolist(), "catch": R[:, 1].tolist(), "top1": R[:, 2].tolist(), "catch_non_nested": R[:, 3].tolist()}
            print("%-6s %-6s FRR %.3f+-%.3f | catch %.3f+-%.3f (non-nested %.3f) | top-1 %.3f" % (name, sub, R[:, 0].mean(), R[:, 0].std(), R[:, 1].mean(), R[:, 1].std(), R[:, 3].mean(), R[:, 2].mean()))
    cn = conf / np.maximum(conf.sum(1, keepdims=True), 1); np.fill_diagonal(cn, 0)
    out["top_confusions"] = [[names[i], names[j], round(float(cn[i, j]), 3)] for i, j in zip(*np.unravel_index(np.argsort(-cn, None)[:10], cn.shape))]
    print("confusions", out["top_confusions"])
    ok = lambda m: all(np.mean(out[m][s]["frr"]) <= 0.03 and np.mean(out[m][s]["catch"]) >= 0.96 for s in subsets if s in out[m])
    out["verdict"] = {m: "MET" if ok(m) else "MISS" for m in res}; print("verdict (every subset inside both bars)", out["verdict"])
    if a.out: json.dump(out, open(a.out, "w"), indent=1)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["render", "train"]); ap.add_argument("--seeds", type=int, default=10)
    ap.add_argument("--first", type=int, default=9000); ap.add_argument("--folds", type=int, default=5); ap.add_argument("--epochs", type=int, default=40); ap.add_argument("--out"); ap.add_argument("--shaped", action="store_true", help="render: random dots and variation per render")
    ap.add_argument("--style-from", type=int, default=0); ap.add_argument("--style-step", type=int, default=1); ap.add_argument("--rows", default=OUT, help="render: file to append rows to")
    ap.add_argument("--train-on", choices=["rest", "all"], default="all", help="train: rows used for training (the test always reports rest and shaped apart)")
    ap.add_argument("--calib", choices=["val", "oof"], default="oof"); ap.add_argument("--inner", type=int, default=4); ap.add_argument("--pct", type=float, default=3)
    ap.add_argument("--calib-on", choices=["all", "shaped"], default="all", help="train, oof: training rows whose out-of-fold scores set the cut-offs")
    a = ap.parse_args(); render(a) if a.cmd == "render" else train(a)
