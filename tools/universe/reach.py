#!/usr/bin/env python3
"""Step 6 reach test (EXECUTION.md): can an Infinite engine be steered to a map sound?   (ml-venv python)

    reach.py plan  --node Analog --round reach1 [--targets 300] [--candidates 40000]
    reach.py score --node Analog --round reach1

Targets = held-out Diva C3-map rows that have a C3 mel (the sample library has bands only, a different form, so it is not used here).
Space = p3.COLS (the characteristic summaries of the C3 window), each column scaled by its spread over the engine's own renders.
plan:  train 3 forward models on the engine's renders; for each target take the nearest existing render as the start, move 1-3 knobs
       at a time on the models (candidates = moves of the 30 nearest renders + fresh edge-biased points), pick the point whose
       mean prediction is closest to the target; write queue_<engine>_<round>.json (design "reach", target index and predicted
       distance stored before the render). Render with own_sweeps.py run --queue.
score: measured distance of each rendered pick to its target vs the nearest existing render (before the round); reports share
       closer, median ratio, and the same on the passed axes only (R2 >= 0.6 columns from p3, when listed in --axes)."""
import argparse, hashlib, json, os, random, sys, time
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, p3_characteristics as p3, m1_forward as m1, sound_data as sd, sound_eval as se, claims as cl  # noqa: E402


def mel_vec(d, i):
    return p3.vec(d["mel"][i, 0].astype(np.float64) / 2 - 120)


def targets(n, seed=7):
    fz = cl.load()["C3"]["sets"]["map"]["data"]["diva_m1.jsonl"]
    d = sd.load(os.path.join(cl.AUDIO, "diva_m1.jsonl")); pos = {p: i for i, p in enumerate(d["preset_id"])}
    idx = [pos[p] for p in fz if p in pos and d["ok"][pos[p], 0]]
    random.Random(seed).shuffle(idx); out = []
    for i in idx:
        v = mel_vec(d, i)
        if np.all(np.isfinite(v)):
            out.append((d["preset_id"][i], v))
        if len(out) >= n:
            break
    return out


def own_state(node):
    d = sd.load(o.out_path(node)); keep = np.flatnonzero(d["ok"][:, 0])
    V = np.full((len(d["preset_id"]), len(p3.COLS)), np.nan)
    for i in keep:
        V[i] = mel_vec(d, i)
    return d, keep, V


def cmd_plan(a):
    table, sw, fixed = o.param_table(a.node); d, keep, V = own_state(a.node)
    spread = np.nanstd(V[keep], 0) + 1e-9; T = targets(a.targets)
    rng = random.Random("%s/%s" % (a.node, a.round)); rendered = [list(map(float, k)) for k in d["knobs"][keep]]
    draw = lambda: rng.betavariate(.5, .5) if rng.random() < .5 else rng.random()
    # models (same recipe as propose.py)
    CU = []
    fill_ = np.nanmedian(V[keep], 0); Vk = np.where(np.isfinite(V[keep]), V[keep], fill_)   # a silent axis is NaN: fill it, or every distance is NaN (reach1 bug)
    Tm = np.array([t[1] for t in T])
    near = [np.argsort((((Vk - t) / spread) ** 2).sum(1))[:30] for t in Tm]
    for ti, nb in enumerate(near):
        for k in range(a.candidates // len(T)):
            if k % 4 == 3: CU.append([draw() for _ in sw])
            else:
                u = list(rendered[rng.choice(nb)])
                for j in rng.sample(range(len(sw)), rng.choice((1, 2, 3))): u[j] = draw()
                CU.append(u)
    CU = np.array(CU); per = a.candidates // len(T)
    XX = se.encode(np.vstack([d["knobs"], CU])); X, XC = XX[:len(d["knobs"])], XX[len(d["knobs"]):]
    mv = np.array([[48, 100]], float) / 127
    perm = np.random.default_rng(0).permutation(keep); nva = max(1, int(.15 * len(perm))); va, fit = perm[:nva], perm[nva:]
    Xa, _, Ma, Ia, Ta = m1.samples(d, X, fit[d["ok"][fit, 0]]); Xv, _, Mv, Iv, Tv = m1.samples(d, X, va[d["ok"][va, 0]])
    c3a, c3v = Ta == 0, Tv == 0; Xa, Ma, Ia, Xv, Mv, Iv = Xa[c3a], Ma[c3a], Ia[c3a], Xv[c3v], Mv[c3v], Iv[c3v]
    mu = np.nanmean(V[Ia], 0); fill = lambda A: np.where(np.isfinite(A), A, mu)
    XCi = np.hstack([XC, np.repeat(mv, len(XC), 0)]).astype(np.float32)
    P = np.mean([m1.train(Xa, fill(V[Ia]).astype(np.float32), Ma, Xv, fill(V[Iv]).astype(np.float32), Mv, s, a.epochs, print)[0](XCi)[0]
                 for s in range(3)], 0)
    # refine: per target, hill-climb on the ensemble (keep the best 8, mutate 1-2 knobs, 120 children, a.refine generations); model-only, no renders
    fs_ = None
    if a.refine:
        fs_ = [m1.train(Xa, fill(V[Ia]).astype(np.float32), Ma, Xv, fill(V[Iv]).astype(np.float32), Mv, s, a.epochs, lambda s: None)[0] for s in range(3)]
    best = {}
    items = []
    for ti, (pid, tv) in enumerate(T):
        sl = slice(ti * per, (ti + 1) * per); dist = (((P[sl] - tv) / spread) ** 2).sum(1) ** .5; b = int(np.argmin(dist))
        cu_b, p_b, d_b = CU[sl][b], P[sl][b], float(dist[b])      # with --beta the climb compares pessimistic scores; the stored pred_dist is the plain mean distance
        if a.refine and a.beta: d_b = 1e9      # first refined child always replaces the unrefined pick when scores are pessimistic
        if a.refine:
            order = np.argsort(dist)[:8]; pop = [list(CU[sl][i]) for i in order]
            for g in range(a.refine):
                kids = []
                for u in pop:
                    for _ in range(15):
                        c = list(u)
                        for j in rng.sample(range(len(sw)), rng.choice((1, 2))): c[j] = min(1., max(0., c[j] + rng.gauss(0, .15))) if rng.random() < .7 else draw()
                        kids.append(c)
                K = np.array(kids); XK = se.encode(np.vstack([d["knobs"], K]))[len(d["knobs"]):]
                PS = np.stack([f(np.hstack([XK, np.repeat(mv, len(XK), 0)]).astype(np.float32))[0] for f in fs_]); PK = PS.mean(0)
                dk = (((PK - tv) / spread) ** 2).sum(1) ** .5 + a.beta * (PS.std(0) / spread).sum(1) ** 1   # lower-confidence bound: pessimistic by the seeds' spread
                o_ = np.argsort(dk)[:8]; pop = [kids[i] for i in o_]
                if dk[o_[0]] < d_b: cu_b, p_b, d_b = K[o_[0]], PK[o_[0]], float(dk[o_[0]])
        dist = np.array([float((((p_b - tv) / spread) ** 2).sum() ** .5)]); b = 0; CUsl = np.array([cu_b]); Psl = np.array([p_b])
        d0 = float((((Vk - tv) / spread) ** 2).sum(1).min() ** .5)
        items.append({"u": [round(float(x), 4) for x in CUsl[b]], "design": "reach", "kind": "reach", "target": pid,
                      "pred_dist": round(float(dist[b]), 4), "start_dist": round(d0, 4), "pred": [round(float(x), 4) for x in Psl[b]]})
    q = {"engine": a.node, "round": a.round, "cols": p3.COLS, "made": time.strftime("%Y-%m-%d %H:%M:%S"), "items": items,
         "targets": {pid: [round(float(x), 4) for x in tv] for pid, tv in T}, "spread": spread.tolist(), "fill": fill_.tolist()}
    q["sha"] = hashlib.sha1(json.dumps(items, sort_keys=True).encode()).hexdigest()[:16]
    out = os.path.join(o.CACHE_DIR, "queue_%s_%s.json" % (a.node.lower(), a.round)); json.dump(q, open(out, "w"))
    print("wrote %s | %d targets | median start dist %.2f, median predicted dist %.2f" %
          (out, len(items), np.median([i["start_dist"] for i in items]), np.median([i["pred_dist"] for i in items])))


def cmd_score(a):
    q = json.load(open(os.path.join(o.CACHE_DIR, "queue_%s_%s.json" % (a.node.lower(), a.round))))
    d, keep, V = own_state(a.node); spread = np.array(q["spread"]); table, sw, fixed = o.param_table(a.node)
    rid = {p: i for i, p in enumerate(d["preset_id"])}; res = []
    for it in q["items"]:
        pid = o.rid(a.node, o.recipe(a.node, sw, fixed, it["u"]))
        i = rid.get(pid)
        if i is None or not d["ok"][i, 0]: continue
        tv = np.array(q["targets"][it["target"]]); vi = np.where(np.isfinite(V[i]), V[i], np.array(q["fill"]))
        got = float((((vi - tv) / spread) ** 2).sum() ** .5)
        res.append((it["start_dist"], it["pred_dist"], got))
    if not res: print("no rendered picks found (check o.rid signature)"); return
    R = np.array(res)
    print("%s %s: %d targets | nearest existing render %.2f | predicted %.2f | measured %.2f (median) | closer than nearest: %.1f%%" %
          (a.node, a.round, len(R), np.median(R[:, 0]), np.median(R[:, 1]), np.median(R[:, 2]), 100 * (R[:, 2] < R[:, 0]).mean()))
    # out-of-gamut view: does the target's distance to the engine's own renders (start dist) say how far the pick lands? tertiles of start dist
    cut = np.quantile(R[:, 0], [1 / 3, 2 / 3])
    for name, m in (("near targets", R[:, 0] <= cut[0]), ("middle", (R[:, 0] > cut[0]) & (R[:, 0] <= cut[1])), ("far targets", R[:, 0] > cut[1])):
        if m.sum(): print("    %-13s start %.2f (n=%d): measured %.2f, closer than nearest %.1f%%" % (name, np.median(R[m, 0]), m.sum(), np.median(R[m, 2]), 100 * (R[m, 2] < R[m, 0]).mean()))
    print("    correlation of start dist with measured dist: %.2f" % np.corrcoef(R[:, 0], R[:, 2])[0, 1])


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["plan", "score"]); ap.add_argument("--node", required=True)
    ap.add_argument("--round", default="reach1"); ap.add_argument("--targets", type=int, default=300)
    ap.add_argument("--candidates", type=int, default=45000); ap.add_argument("--epochs", type=int, default=300); ap.add_argument("--beta", type=float, default=0., help="weight of the seeds' spread added to the distance while refining"); ap.add_argument("--refine", type=int, default=0, help="generations of model-only hill-climb per target")
    a = ap.parse_args(); {"plan": cmd_plan, "score": cmd_score}[a.cmd](a)


if __name__ == "__main__":
    main()
