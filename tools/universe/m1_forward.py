#!/usr/bin/env python3
"""M1 of SOUND-MODEL.md: forward model, knobs + note + velocity -> sound, in PyTorch (Colab; any machine with torch).

    m1_forward.py preset_diva_v2.jsonl [--seeds 3] [--epochs 300] [--out DIR] [--dry]

Data, split, knob encoding and scores are the M0 ones (sound_data, sound_eval.encode / score), so a number printed here is the same
number sound_eval would print for this model. One network for every note: input = encoded knobs + midi/127 + velocity/127; a shared
trunk (2 x 512, ReLU, dropout) feeds two heads, the 43 descriptors (standardised) and the 64 x 48 log-mel (dense, 512 -> 1024 -> 3072:
a transposed-conv decoder cost 100x more per example on CPU and returns only if mel error is what limits M1). Loss = MSE on descriptors + L1 on mel / 10 dB. AdamW, early stopping on 15% of the training groups (whole
groups, so validation leaks no near-duplicates). Each seed is scored on its own (the spread is the repeat-run check, M0 asks <= 0.005)
and the seeds' average is the model. Baselines (training mean, k-NN in knob space) are scored on the same held-out presets, and the
M1 bars are printed as PASS / FAIL. --dry runs everything except the network (no torch needed): data, split, baselines.
Results go to --out (default training-data/models/m1_<plugin>): results.txt, and model_<split>.pt per split."""
import argparse, os, sys, time
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_pass as rp  # noqa: E402
import sound_data as sd  # noqa: E402
import sound_eval as se  # noqa: E402
import sound_model_probe as smp  # noqa: E402

BARS = {"random": 0.50, "group": 0.40}  # descriptor R2; mel bar: L1 at least 20% below k-NN on both splits
MEL_DB = 10.0
MEL = sd.MEL_SHAPE


def samples(d, X, rows):
    """(inputs, desc, mel dB, preset index, note index) for every measured note of the given presets."""
    nv = {lab: (m, v) for m, v, lab in rp.NOTES}
    I, T = np.nonzero(d["ok"][rows])
    I = rows[I]
    mv = np.array([nv[str(d["notes"][t])] for t in T], np.float64) / 127
    return (np.hstack([X[I], mv]).astype(np.float32), d["desc"][I, T].astype(np.float32),
            d["mel"][I, T].astype(np.float32) / 2 - 120, I, T)


def train(Xtr, Dtr, Mtr, Xva, Dva, Mva, seed, epochs, log):
    import torch, torch.nn as nn
    torch.manual_seed(seed); np.random.seed(seed)
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    dmu, dsd = Dtr.mean(0), Dtr.std(0) + 1e-6; mmu = Mtr.mean(0)

    class Net(nn.Module):
        def __init__(s, n_in):
            super().__init__()
            s.trunk = nn.Sequential(nn.Linear(n_in, 512), nn.ReLU(), nn.Dropout(0.2), nn.Linear(512, 512), nn.ReLU(), nn.Dropout(0.2))
            s.desc = nn.Linear(512, Dtr.shape[1])
            s.dec = nn.Sequential(nn.Linear(512, 1024), nn.ReLU(), nn.Linear(1024, MEL[0] * MEL[1]))

        def forward(s, x):
            h = s.trunk(x)
            return s.desc(h), s.dec(h).view(-1, *MEL)

    t = lambda a: torch.tensor(a, device=dev)
    xs, ds, ms = t(Xtr), t((Dtr - dmu) / dsd), t((Mtr - mmu) / MEL_DB)
    xv, dv, mv = t(Xva), t((Dva - dmu) / dsd), t((Mva - mmu) / MEL_DB)
    net = Net(Xtr.shape[1]).to(dev); opt = torch.optim.AdamW(net.parameters(), 1e-3, weight_decay=1e-4)
    loss = lambda x, d, m: (lambda p: ((p[0] - d) ** 2).mean() + (p[1] - m).abs().mean())(net(x))
    best, state, wait = np.inf, None, 0
    for ep in range(epochs):
        net.train()
        for b in torch.randperm(len(xs), device=dev).split(128):
            opt.zero_grad(); l = loss(xs[b], ds[b], ms[b]); l.backward(); opt.step()
        net.eval()
        with torch.no_grad():
            v = float(loss(xv, dv, mv))
        if v < best - 1e-4:
            best, wait = v, 0; state = {k: x.detach().clone() for k, x in net.state_dict().items()}
        else:
            wait += 1
            if wait >= 20:
                break
    net.load_state_dict(state); net.eval()
    log("  seed %d: %d epochs, best validation loss %.4f" % (seed, ep + 1, best))

    def predict(X):
        with torch.no_grad():
            p = net(t(X))
        return p[0].cpu().numpy() * dsd + dmu, p[1].cpu().numpy() * MEL_DB + mmu
    return predict, {"state": state, "dmu": dmu, "dsd": dsd, "mmu": mmu, "n_in": Xtr.shape[1]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows"); ap.add_argument("--seeds", type=int, default=3); ap.add_argument("--epochs", type=int, default=300)
    ap.add_argument("--boot", type=int, default=1000); ap.add_argument("--out", default=""); ap.add_argument("--dry", action="store_true")
    a = ap.parse_args()
    d = sd.load(a.rows); keep = np.flatnonzero(d["ok"].any(1))
    plugin = os.path.basename(a.rows).split(".")[0]
    out = os.path.expanduser(a.out or "~/Infinite/training-data/models/m1_" + plugin); os.makedirs(out, exist_ok=True)
    lines = []
    def log(s):
        print(s, flush=True); lines.append(s)
    X = se.encode(d["knobs"])  # same encoding as sound_eval
    log("M1 forward | %s | %d presets, %d knob features, notes %s | %s" % (a.rows, len(keep), X.shape[1], list(map(str, d["notes"])),
                                                                         time.strftime("%Y-%m-%d %H:%M")))
    verdict = []
    for sn, m in sd.split(d).items():
        te_p, tr_p = keep[m[keep]], keep[~m[keep]]
        if len(te_p) < 30 or len(np.unique(d["group"][te_p])) < 5:
            log("%s: too few held out (%d presets): no score" % (sn, len(te_p))); continue
        # validation = 15% of the training groups, chosen by the same name hash as the split, so it is stable too
        gtr = np.unique(d["group"][tr_p]); rng = np.random.default_rng(0); va_g = set(rng.choice(gtr, max(1, int(0.15 * len(gtr))), False))
        va_p = tr_p[np.isin(d["group"][tr_p], list(va_g))] if sn == "group" else tr_p[rng.random(len(tr_p)) < 0.15]
        fit_p = np.setdiff1d(tr_p, va_p)
        Xa, Da, Ma, Ia, Ta = samples(d, X, fit_p); Xv, Dv, Mv, _, _ = samples(d, X, va_p); Xt, Dt, Mt, It, Tt = samples(d, X, te_p)
        Mt2, Ma2 = Mt.reshape(len(Mt), -1), Ma.reshape(len(Ma), -1)
        log("\n%s split: fit %d, validation %d, test %d presets (%d test notes)" % (sn, len(fit_p), len(va_p), len(te_p), len(Xt)))
        preds = {"mean": (np.tile(Da.mean(0), (len(Xt), 1)), np.tile(Ma2.mean(0), (len(Xt), 1)))}
        # k-NN per note: neighbours among training presets at the same note
        kd, km = np.zeros_like(Dt), np.zeros_like(Mt2)
        for t in np.unique(Tt):
            q, r = Tt == t, Ta == t
            kd[q] = smp.knn(X[Ia[r]], Da[r], X[It[q]], 5); km[q] = smp.knn(X[Ia[r]], Ma2[r], X[It[q]], 5)
        preds["knn"] = (kd, km)
        if not a.dry:
            runs = []
            for s in range(a.seeds):
                f, blob = train(Xa, Da, Ma, Xv, Dv, Mv, s, a.epochs, log)
                pd_, pm = f(Xt); runs.append((pd_, pm.reshape(len(Xt), -1)))
                r2s = se.score("desc", Dt[Tt == 0].astype(np.float64), pd_[Tt == 0], d["group"][It[Tt == 0]], 0)[0] if (Tt == 0).any() else np.nan
                log("  seed %d: R2 at %s %.3f" % (s, d["notes"][0], r2s))
                if s == 0:
                    import torch
                    torch.save(blob, os.path.join(out, "model_%s.pt" % sn))
            preds["mlp"] = (np.mean([r[0] for r in runs], 0), np.mean([r[1] for r in runs], 0))
        res = {}
        for t in np.unique(Tt):
            q = Tt == t; g = d["group"][It[q]]
            for mn, (pd_, pm) in preds.items():
                r = se.score("desc", Dt[q].astype(np.float64), pd_[q].astype(np.float64), g, a.boot)
                e = se.score("mel", Mt2[q].astype(np.float64), pm[q].astype(np.float64), g, a.boot)
                res[(mn, t)] = (r[0], e[0])
                log("  %-4s %-5s R2 %.3f [%.3f, %.3f] | mel %.2f dB [%.2f, %.2f]%s"
                    % (d["notes"][t], mn, r[0], r[2], r[3], e[0], e[2], e[3], e[4]))
        if "mlp" in preds:
            r2 = res[("mlp", 0)][0]; mel, mel_k = res[("mlp", 0)][1], res[("knn", 0)][1]
            ok1, ok2 = r2 >= BARS[sn], mel <= 0.8 * mel_k
            verdict.append(ok1 and ok2)
            log("  bar %s at %s: R2 %.3f vs %.2f %s | mel %.2f dB vs 0.8 x k-NN %.2f %s"
                % (sn, d["notes"][0], r2, BARS[sn], "PASS" if ok1 else "FAIL", mel, 0.8 * mel_k, "PASS" if ok2 else "FAIL"))
    if verdict:
        log("\nM1 %s" % ("PASSED" if all(verdict) and len(verdict) == 2 else "not passed"))
    open(os.path.join(out, "results.txt"), "a").write("\n".join(lines) + "\n\n")


if __name__ == "__main__":
    main()
