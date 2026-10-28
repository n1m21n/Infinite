#!/usr/bin/env python3
"""Claim C2 (knows what each ingredient does): predict the change of every axis for the frozen single and combined moves, then score.   (ml-venv python)

    c2_predict.py --node Analog [--epochs 300] [--seeds 3]

Trains on the engine's rows OUTSIDE the held-out banks (sd.split group), predicts f(variant) - f(base) on the 11 mean axes for each frozen
move (claims.json C2), stores the prediction with claims.py predict (hash and time), and scores it (direction right >= 90%, size r >= 0.7).
Reports single-knob and combined moves separately."""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, p3_characteristics as p3, m1_forward as m1, sound_data as sd, sound_eval as se, claims as cl  # noqa: E402


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", required=True); ap.add_argument("--epochs", type=int, default=300); ap.add_argument("--seeds", type=int, default=3); a = ap.parse_args()
    fn = "own_%s.jsonl" % a.node.lower(); fz = cl.load()["C2"]["sets"][fn]["data"]; d = sd.load(o.out_path(a.node)); pos = {p: i for i, p in enumerate(d["preset_id"])}
    held = sd.split(d)["group"]; keep = np.flatnonzero(d["ok"][:, 0] & ~held)
    V = np.full((len(d["preset_id"]), len(p3.COLS)), np.nan)
    for i in np.flatnonzero(d["ok"][:, 0]): V[i] = p3.vec(d["mel"][i, 0].astype(np.float64) / 2 - 120)
    X = se.encode(d["knobs"]); means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]; names = [p3.COLS[j].split(".")[0] for j in means]
    perm = np.random.default_rng(0).permutation(keep); nva = max(1, int(.15 * len(perm))); va, fit = perm[:nva], perm[nva:]
    Xa, _, Ma, Ia, Ta = m1.samples(d, X, fit); Xv, _, Mv, Iv, Tv = m1.samples(d, X, va)
    c3a, c3v = Ta == 0, Tv == 0; Xa, Ma, Ia, Xv, Mv, Iv = Xa[c3a], Ma[c3a], Ia[c3a], Xv[c3v], Mv[c3v], Iv[c3v]
    mu = np.nanmean(V[Ia], 0); fill = lambda A: np.where(np.isfinite(A), A, mu); mv = np.array([[48, 100]], float) / 127
    fs = [m1.train(Xa, fill(V[Ia]).astype(np.float32), Ma, Xv, fill(V[Iv]).astype(np.float32), Mv, s, a.epochs, lambda s: None)[0] for s in range(a.seeds)]
    pred = lambda i: np.mean([f(np.hstack([X[i:i + 1], mv]).astype(np.float32))[0][0][means] for f in fs], 0)
    out = {}
    for m in fz["single"] + fz["combined"]:
        if m["variant"] in pos and m["base"] in pos:
            dv = pred(pos[m["variant"]]) - pred(pos[m["base"]]); out[m["variant"]] = {n: float(x) for n, x in zip(names, dv)}
    tmp = os.path.join(tempfile.gettempdir(), "c2_%s.json" % a.node); json.dump(out, open(tmp, "w"))
    subprocess.run([sys.executable, os.path.join(HERE, "claims.py"), "predict", "C2", "--model", "m1_%s" % a.node.lower(), "--file", tmp])
    for kind in ("single", "combined"):
        sub = {m["variant"]: out[m["variant"]] for m in fz[kind] if m["variant"] in out}
        json.dump(sub, open(tmp, "w")); print("-- %s moves (%d with predictions)" % (kind, len(sub)))
        # score only this kind by temporarily restricting the frozen list
        import claims
        full = claims.load()["C2"]["sets"][fn]["data"]; keepset = set(sub)
        orig = claims.load; claims.load = lambda: {"C2": {"sets": {fn: {"data": {"single": [m for m in full["single"] if m["variant"] in keepset], "combined": [m for m in full["combined"] if m["variant"] in keepset]}}}}}
        class A: pass
        b = A(); b.claim = "C2"; b.file = tmp; claims.cmd_score(b); claims.load = orig


if __name__ == "__main__":
    main()
