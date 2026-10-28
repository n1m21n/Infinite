#!/usr/bin/env python3
"""Step 4: the model picks the next renders (EXECUTION.md Step 4).

    propose.py --node Analog --round r01 [--n 2000] [--uniform 200] [--candidates 20000] [--seeds 3]      (run with ml-venv python)

Trains `--seeds` forward models (knobs + note -> the characteristic summaries of p3_characteristics) on every rendered preset of the
engine, draws candidates with the freedom dial c = 0.5 (half the knobs per candidate Beta(.5,.5) so rare, edge values appear; the rest
uniform; candidates are fresh points, single-knob moves and combinations of rendered points), ranks them by the disagreement of the
seeds (std of the predicted characteristics, each column scaled by its spread in the data), keeps the top n - uniform, adds `uniform`
plain uniform draws as the honest check, and writes ~/Infinite/training-data/own/queue_<engine>_<round>.json. The seeds' predictions for
every chosen point are stored in the same file BEFORE any render (claims.py predict hashes it). Render with
`own_sweeps.py run --node <E> --queue <file> --shards 4`."""
import argparse, hashlib, json, math, os, random, sys, time
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o  # noqa: E402
import p3_characteristics as p3  # noqa: E402
import m1_forward as m1  # noqa: E402
import sound_data as sd  # noqa: E402
import sound_eval as se  # noqa: E402


def candidates(node, sw, fixed, rendered, n, rng):
    """n points as 0-1 vectors: 1/3 fresh, 1/3 one-knob moves of rendered points, 1/3 two-to-three-knob moves; c = 0.5 edge bias."""
    def draw():
        return rng.betavariate(.5, .5) if rng.random() < .5 else rng.random()
    out = []
    for k in range(n):
        kind = k % 3
        if kind == 0 or not rendered:
            out.append(("fresh", [draw() for _ in sw]))
        else:
            u = list(rng.choice(rendered))
            for i in rng.sample(range(len(sw)), 1 if kind == 1 else rng.choice((2, 3))):
                u[i] = draw()
            out.append(("one" if kind == 1 else "pair", u))
    return out


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", required=True); ap.add_argument("--round", required=True)
    ap.add_argument("--n", type=int, default=2000); ap.add_argument("--uniform", type=int, default=200)
    ap.add_argument("--candidates", type=int, default=20000); ap.add_argument("--seeds", type=int, default=3)
    ap.add_argument("--epochs", type=int, default=300)
    a = ap.parse_args(); t0 = time.time()
    table, sw, fixed = o.param_table(a.node); path = o.out_path(a.node)
    d = sd.load(path); keep = np.flatnonzero(d["ok"][:, 0])
    V = np.full((len(d["preset_id"]), len(p3.COLS)), np.nan)
    for i in keep:
        V[i] = p3.vec(d["mel"][i, 0].astype(np.float64) / 2 - 120)
    rng = random.Random("%s/%s" % (a.node, a.round))
    rendered = [list(map(float, k)) for k in d["knobs"][keep]]
    cand = candidates(a.node, sw, fixed, rendered, a.candidates, rng)
    CU = np.array([c[1] for c in cand])
    XX = se.encode(np.vstack([d["knobs"], CU]))          # one encoding, as p3 does for extra rows
    X, XC = XX[:len(d["knobs"])], XX[len(d["knobs"]):]
    mv = np.array([[48, 100]], float) / 127
    rp_ = np.random.default_rng(0); perm = rp_.permutation(keep); nva = max(1, int(.15 * len(perm))); va, fit = perm[:nva], perm[nva:]
    Xa, _, Ma, Ia, Ta = m1.samples(d, X, fit[d["ok"][fit, 0]]); Xv, _, Mv, Iv, Tv = m1.samples(d, X, va[d["ok"][va, 0]])
    c3a, c3v = Ta == 0, Tv == 0
    Xa, Ma, Ia, Xv, Mv, Iv = Xa[c3a], Ma[c3a], Ia[c3a], Xv[c3v], Mv[c3v], Iv[c3v]
    mu = np.nanmean(V[Ia], 0); fill = lambda A: np.where(np.isfinite(A), A, mu)
    XCi = np.hstack([XC, np.repeat(mv, len(XC), 0)]).astype(np.float32)
    preds, fs = [], []
    for s in range(a.seeds):
        f, _ = m1.train(Xa, fill(V[Ia]).astype(np.float32), Ma, Xv, fill(V[Iv]).astype(np.float32), Mv, s, a.epochs, print)
        fs.append(f); preds.append(f(XCi)[0])
    P = np.stack(preds)                                   # seeds x candidates x columns
    spread = np.nanstd(V[keep], 0) + 1e-9
    dis = (P.std(0) / spread).mean(1)
    n_dis = a.n - a.uniform
    kinds_c = np.array([c[0] for c in cand]); top = []          # single knobs, combinations and fresh points alike: a third each
    for k, share in (("one", n_dis // 3), ("pair", n_dis // 3), ("fresh", n_dis - 2 * (n_dis // 3))):
        idx = np.flatnonzero(kinds_c == k); top += list(idx[np.argsort(-dis[idx])[:share]])
    top = np.array(top)
    items = [{"u": [round(x, 4) for x in cand[i][1]], "design": "propose", "kind": cand[i][0], "disagreement": round(float(dis[i]), 4),
              "pred": [round(float(x), 4) for x in P.mean(0)[i]]} for i in top]
    # honest check: plain uniform base points, predictions stored too (a model trained on seeds above)
    U = np.array([[rng.random() for _ in sw] for _ in range(a.uniform)])
    XU = se.encode(np.vstack([d["knobs"], U]))[len(d["knobs"]):]
    PU = np.mean([f(np.hstack([XU, np.repeat(mv, len(XU), 0)]).astype(np.float32))[0] for f in fs], 0)
    for u, pu in zip(U, PU):
        items.append({"u": [round(float(x), 4) for x in u], "design": "uniform", "kind": "fresh", "pred": [round(float(x), 4) for x in pu]})
    rng.shuffle(items)
    q = {"engine": a.node, "round": a.round, "cols": p3.COLS, "seeds": a.seeds, "made": time.strftime("%Y-%m-%d %H:%M:%S"),
         "rendered_before": int(len(keep)), "items": items}
    q["sha"] = hashlib.sha1(json.dumps(items, sort_keys=True).encode()).hexdigest()[:16]
    out = os.path.join(o.CACHE_DIR, "queue_%s_%s.json" % (a.node.lower(), a.round)); os.makedirs(o.CACHE_DIR, exist_ok=True)
    json.dump(q, open(out, "w"))
    kinds = {}
    for it in items:
        kinds[it["design"] + "/" + it["kind"]] = kinds.get(it["design"] + "/" + it["kind"], 0) + 1
    print("wrote %s | %d items %s | disagreement top %.3f median %.3f | %.0f s" % (out, len(items), kinds, dis[top].min(), np.median(dis), time.time() - t0))


if __name__ == "__main__":
    main()
