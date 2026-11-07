#!/usr/bin/env python3
"""Train and score the sound judge (internal/plans/generate-samples/judge.md). ml venv: ~/Infinite/media/.mlvenv/bin/python
    judge_train.py train --own Piano Wind String [--epochs 30] [--out v0]   lib.npz + own_<node>.npz -> judge/models/<out>.pt + report
    judge_train.py score --model v0 --npz own_Piano_knock.npz [--fam piano]  realness, family and technical read-out of a render set
One small conv net over the 3 s gain-normalised log-mel (judge_data.py), three outputs:
    tech   the 15 DSP targets (z-scored), one technical head per group in judge_data.HEAD
    fam    family class (10 families + other), trained on library clips only
    real   library one-shot vs our render, per family through the family embedding
Held out: library clips by pack folder, renders by preset (both notes of a preset fall on one side), 20% each, seed 0.
Checks (plan section 7): tech R2 per head, real AUC, and the same AUC with every clip cut to its first 1.5 s
(a judge that only hears release tails or noise floors fails it). Exit 0."""
import argparse, json, os, sys, zlib
import numpy as np
import torch, torch.nn as nn, torch.nn.functional as F
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import judge_data as jd  # noqa: E402

J = jd.OUT; MD = os.path.join(J, "models")
FAMS = sorted(jd.FAMS) + ["other"]
DEV = "mps" if torch.backends.mps.is_available() else "cpu"


def load(path):
    z = np.load(path, allow_pickle=False)
    return {k: z[k] for k in z.files}


def held(key, frac=0.2):
    return (zlib.crc32(key.encode()) % 1000) < frac * 1000


def group(src, fam):
    return os.path.dirname(src) if fam != "own" else src.split("_")[0]


class Net(nn.Module):
    def __init__(self, ntgt, nfam):
        super().__init__()
        ch = [1, 32, 64, 96, 128]; L = []
        for a, b in zip(ch[:-1], ch[1:]):
            L += [nn.Conv2d(a, b, 3, padding=1), nn.BatchNorm2d(b), nn.ReLU(), nn.Conv2d(b, b, 3, padding=1), nn.BatchNorm2d(b), nn.ReLU(), nn.MaxPool2d(2)]
        self.body = nn.Sequential(*L)
        self.emb = nn.Sequential(nn.Linear(256, 128), nn.ReLU())
        self.tech = nn.Linear(128, ntgt); self.fam = nn.Linear(128, nfam); self.real = nn.Linear(128, nfam)

    def forward(self, x):
        h = self.body(x[:, None]); h = torch.cat([h.mean((2, 3)), h.amax((2, 3))], 1); e = self.emb(h)
        return self.tech(e), self.fam(e), self.real(e), e


def mel_f(u8):
    return (u8.astype(np.float32) / 2 - 127.5) / 40.0 + 1.5


def cut(u8, frames=94):
    x = u8.copy(); x[:, :, frames:] = 0
    return x


def predict(net, mel, bs=256):
    net.eval(); outs = [[], [], [], []]
    with torch.no_grad():
        for i in range(0, len(mel), bs):
            o = net(torch.from_numpy(mel_f(mel[i:i + bs])).to(DEV))
            for k in range(4):
                outs[k].append(o[k].float().cpu().numpy())
    return [np.concatenate(o) for o in outs]


def auc(s, y):
    o = np.argsort(s); r = np.empty(len(s)); r[o] = np.arange(1, len(s) + 1); n1 = y.sum(); n0 = len(y) - n1
    return float((r[y == 1].sum() - n1 * (n1 + 1) / 2) / max(1, n1 * n0))


def train(a):
    lib = load(os.path.join(J, "lib.npz")); own = [load(os.path.join(J, "own_%s.npz" % n)) for n in a.own]
    mel = np.concatenate([lib["mel"]] + [o["mel"] for o in own]); tg = np.concatenate([lib["targets"]] + [o["targets"] for o in own])
    fam = np.concatenate([lib["fam"]] + [o["fam"] for o in own]); loop = np.concatenate([lib["loop"]] + [o["loop"] for o in own])
    src = np.concatenate([lib["src"]] + [o["src"] for o in own])
    node = np.concatenate([np.array([""] * len(lib["fam"]))] + [np.array([n] * len(o["fam"])) for n, o in zip(a.own, own)])
    tg = np.nan_to_num(tg); mu, sd = tg.mean(0), tg.std(0) + 1e-6; tz = (tg - mu) / sd
    te = np.array([held(group(s, f)) for s, f in zip(src, fam)]); tr = ~te
    is_own = fam == "own"
    fidx = np.array([FAMS.index(f) if f in FAMS else -1 for f in fam])
    # an own render is "posing as" its node's family set: Piano -> piano, Wind -> each wind family, String -> each string family
    pose = {"Piano": ["piano"], "Wind": list(jd.sf.WIND), "String": [f for f in jd.sf.FAM if f != "piano"]}
    real_ok = (~is_own & ~loop & (fidx >= 0) & (fidx < len(FAMS) - 1)) | is_own
    torch.manual_seed(0); np.random.seed(0)
    net = Net(tz.shape[1], len(FAMS)).to(DEV); opt = torch.optim.AdamW(net.parameters(), 2e-3, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, 2e-3, total_steps=int(a.epochs * (tr.sum() // 64 + 1)))
    idx = np.nonzero(tr)[0]
    for ep in range(a.epochs):
        net.train(); np.random.shuffle(idx); tot = 0.0
        for i in range(0, len(idx), 64):
            b = idx[i:i + 64]; x = mel[b]
            sh = np.random.randint(0, 6); x = np.roll(x, sh, 2) if sh else x  # up to 80 ms of onset jitter
            xt = torch.from_numpy(mel_f(x)).to(DEV)
            if np.random.rand() < 0.5:  # spec masks
                f0 = np.random.randint(0, 112); xt[:, f0:f0 + np.random.randint(4, 16)] = 0.0
            pt, pf, pr, _ = net(xt)
            l = F.smooth_l1_loss(pt, torch.from_numpy(tz[b]).float().to(DEV))
            fl = torch.from_numpy(fidx[b]).to(DEV); m = fl >= 0
            if m.any():
                l = l + F.cross_entropy(pf[m], fl[m])
            # real head: library one-shot of family f = 1 at logit f; own render = 0 at each family it poses as
            rows, cols, ys = [], [], []
            for j, k in enumerate(b):
                if not real_ok[k]:
                    continue
                if is_own[k]:
                    for f in pose[node[k]]:
                        rows.append(j); cols.append(FAMS.index(f)); ys.append(0.0)
                else:
                    rows.append(j); cols.append(fidx[k]); ys.append(1.0)
            if rows:
                lg = pr[torch.tensor(rows, device=DEV), torch.tensor(cols, device=DEV)]
                yt = torch.tensor(ys, device=DEV); w = torch.where(yt > 0, 1.0, float(max(1, (yt > 0).sum().item())) / max(1, (yt == 0).sum().item()))
                l = l + F.binary_cross_entropy_with_logits(lg, yt, weight=w)
            opt.zero_grad(); l.backward(); opt.step(); sched.step(); tot += l.item() * len(b)
        print("epoch %d loss %.3f" % (ep, tot / len(idx)), flush=True)
    os.makedirs(MD, exist_ok=True)
    torch.save({"state": net.state_dict(), "mu": mu, "sd": sd, "fams": FAMS, "tgt": jd.TGT}, os.path.join(MD, a.out + ".pt"))
    rep = report(net, mel[te], tz[te], fidx[te], is_own[te], loop[te], node[te], pose, real_ok[te])
    json.dump(rep, open(os.path.join(MD, a.out + "_report.json"), "w"), indent=1); print(json.dumps(rep, indent=1))


def report(net, mel, tz, fidx, is_own, loop, node, pose, real_ok):
    pt, pf, pr, _ = predict(net, mel); rep = {"n_test": int(len(mel))}
    r2 = 1 - ((pt - tz) ** 2).mean(0) / (tz.var(0) + 1e-9)
    rep["tech_r2"] = {k: round(float(r2[jd.TGT.index(k)]), 3) for k in jd.TGT}
    rep["head_r2"] = {h: round(float(np.mean([r2[jd.TGT.index(k)] for k in ks])), 3) for h, ks in jd.HEAD.items()}
    lm = fidx >= 0
    rep["fam_acc"] = round(float((pf[lm].argmax(1) == fidx[lm]).mean()), 3)
    _, _, prc, _ = predict(net, cut(mel))
    for tag, P in (("full", pr), ("cut1.5s", prc)):
        for n, fs in pose.items():
            for f in fs:
                c = FAMS.index(f); pos = ~is_own & ~loop & (fidx == c); neg = is_own & (node == n)
                if pos.sum() >= 10 and neg.sum() >= 10:
                    s = np.r_[P[pos, c], P[neg, c]]; y = np.r_[np.ones(pos.sum()), np.zeros(neg.sum())]
                    rep.setdefault("real_auc_" + tag, {})["%s/%s" % (n, f)] = round(auc(s, y), 3)
    for n, fs in pose.items():
        neg = is_own & (node == n)
        if neg.sum():
            rep.setdefault("own_fam_prob", {})[n] = {f: round(float(torch.softmax(torch.from_numpy(pf[neg]), 1)[:, FAMS.index(f)].mean()), 3) for f in fs}
    return rep


def score(a):
    ck = torch.load(os.path.join(MD, a.model + ".pt"), weights_only=False)
    net = Net(len(ck["tgt"]), len(ck["fams"])).to(DEV); net.load_state_dict(ck["state"])
    z = load(os.path.join(J, a.npz)); pt, pf, pr, _ = predict(net, z["mel"])
    out = {"n": int(len(pt))}
    for f in a.fam:
        c = ck["fams"].index(f); out[f] = {"real_p": round(float(1 / (1 + np.exp(-pr[:, c])).mean()), 3),
                                          "fam_p": round(float(torch.softmax(torch.from_numpy(pf), 1)[:, c].mean()), 3)}
    tz = (np.nan_to_num(z["targets"]) - ck["mu"]) / ck["sd"]
    out["tech_measured_z"] = {k: round(float(np.median(tz[:, i])), 2) for i, k in enumerate(ck["tgt"])}
    print(json.dumps(out, indent=1))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["train", "score"])
    ap.add_argument("--own", nargs="*", default=[]); ap.add_argument("--epochs", type=int, default=30); ap.add_argument("--out", default="v0")
    ap.add_argument("--model", default="v0"); ap.add_argument("--npz"); ap.add_argument("--fam", nargs="*", default=["piano"])
    a = ap.parse_args()
    return train(a) if a.cmd == "train" else score(a)


if __name__ == "__main__":
    sys.exit(main() or 0)
