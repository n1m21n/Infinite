#!/usr/bin/env python3
"""C2 with a derivative-informed loss: descriptor-only forward model trained on values AND on differences between near-neighbour renders.   (ml-venv python)

    c2_delta.py --node Analog [--lam 1.0] [--seeds 3] [--epochs 200]

Same split, frozen C2 moves and scorer as c2_predict.py (rows outside the held-out banks only). Extra loss: for every pair of training rows that
differ in at most 3 knobs, (f(a) - f(b)) must match (V[a] - V[b]), the supervised version of 'what does this knob do'. Stores the prediction
with claims.py predict (model name m1d_<engine>) and scores single / combined / all like c2_predict."""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, p3_characteristics as p3, sound_data as sd, sound_eval as se, claims as cl  # noqa: E402


def pairs(K, maxd=3, chunk=400):
    out = []
    for s in range(0, len(K), chunk):
        nd = (np.abs(K[s:s + chunk, None, :] - K[None, :, :]) > 0.005).sum(2)
        i, j = np.nonzero((nd >= 1) & (nd <= maxd)); out += [(s + a, b) for a, b in zip(i, j) if s + a < b]
    return np.array(out)


def train(X, V, P, seed, epochs, lam):
    import torch, torch.nn as nn
    torch.manual_seed(seed); mu, sd_ = V.mean(0), V.std(0) + 1e-6
    t = lambda a: torch.tensor(np.asarray(a, np.float32))
    xs, vs = t(X), t((V - mu) / sd_)
    rng = np.random.default_rng(seed); isv = rng.random(len(P)) < .15
    net = nn.Sequential(nn.Linear(X.shape[1], 512), nn.ReLU(), nn.Dropout(.1), nn.Linear(512, 512), nn.ReLU(), nn.Linear(512, V.shape[1]))
    opt = torch.optim.AdamW(net.parameters(), 1e-3, weight_decay=1e-4)
    Ptr, Pva = P[~isv], P[isv]
    def loss(idx_rows, pp):
        a = net(xs[idx_rows]); lv = ((a - vs[idx_rows]) ** 2).mean()
        d = net(xs[pp[:, 0]]) - net(xs[pp[:, 1]]); ld = ((d - (vs[pp[:, 0]] - vs[pp[:, 1]])) ** 2).mean()
        return lv + lam * ld
    best, state, wait = 1e9, None, 0; n = len(xs)
    for ep in range(epochs):
        net.train()
        for b in torch.randperm(len(Ptr)).split(256):
            rows = torch.randint(0, n, (256,)); opt.zero_grad(); loss(rows, torch.tensor(Ptr[b.numpy()])).backward(); opt.step()
        net.eval()
        with torch.no_grad(): v = float(loss(torch.arange(n), torch.tensor(Pva)))
        if v < best - 1e-4: best, wait, state = v, 0, {k: x.clone() for k, x in net.state_dict().items()}
        else:
            wait += 1
            if wait >= 15: break
    net.load_state_dict(state); net.eval()
    print("  seed %d: %d epochs, best val loss %.4f" % (seed, ep + 1, best), flush=True)
    return lambda Z: net(t(Z)).detach().numpy() * sd_ + mu


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", required=True); ap.add_argument("--epochs", type=int, default=200)
    ap.add_argument("--seeds", type=int, default=3); ap.add_argument("--lam", type=float, default=1.0); a = ap.parse_args()
    fn = "own_%s.jsonl" % a.node.lower(); fz = cl.load()["C2"]["sets"][fn]["data"]; d = sd.load(o.out_path(a.node)); pos = {p: i for i, p in enumerate(d["preset_id"])}
    held = sd.split(d)["group"]; keep = np.flatnonzero(d["ok"][:, 0] & ~held)
    Vall = np.full((len(d["preset_id"]), len(p3.COLS)), np.nan)
    for i in np.flatnonzero(d["ok"][:, 0]): Vall[i] = p3.vec(d["mel"][i, 0].astype(np.float64) / 2 - 120)
    means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]; names = [p3.COLS[j].split(".")[0] for j in means]
    X = se.encode(d["knobs"]); mv = np.array([[48, 100]], float) / 127; Xi = np.hstack([X, np.repeat(mv, len(X), 0)]).astype(np.float32)
    Vk = Vall[keep]; Vk = np.where(np.isfinite(Vk), Vk, np.nanmean(Vk, 0))
    P = pairs(d["knobs"][keep]); print("%d training rows, %d near-neighbour pairs" % (len(keep), len(P)), flush=True)
    fs = [train(Xi[keep], Vk, P, s, a.epochs, a.lam) for s in range(a.seeds)]
    pred = lambda i: np.mean([f(Xi[i:i + 1])[0][means] for f in fs], 0)
    out = {}
    for m in fz["single"] + fz["combined"]:
        if m["variant"] in pos and m["base"] in pos:
            dv = pred(pos[m["variant"]]) - pred(pos[m["base"]]); out[m["variant"]] = {n: float(x) for n, x in zip(names, dv)}
    tmp = os.path.join(tempfile.gettempdir(), "c2d_%s.json" % a.node); json.dump(out, open(tmp, "w"))
    subprocess.run([sys.executable, os.path.join(HERE, "claims.py"), "predict", "C2", "--model", "m1d_%s_lam%g" % (a.node.lower(), a.lam), "--file", tmp])
    class A: pass
    b = A(); b.claim = "C2"; b.file = tmp; cl.cmd_score(b)


if __name__ == "__main__":
    main()
