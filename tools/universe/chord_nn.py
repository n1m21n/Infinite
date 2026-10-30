#!/usr/bin/env python3
"""Note model comparison, step 1 (STATUS-2026-10-29.md section 2.2): does a small LSTM or a small transformer beat the n-gram on
next chord? Same data, tokens, splits (seeds 0-4, 80/20 by progression), baseline and scoring as chord_eval2.py, so the numbers
sit side by side with its chosen n-gram.
    ~/Infinite/ml-venv/bin/python3 -I chord_nn.py [--seeds 0,1,2,3,4] [--models lstm,transformer]
Input per step: the numeral, its chord class and the mode (one token at the start). 10% of the training progressions pick the
epoch (early stop); the test 20% is never seen. Result: docs/plans/data-universe/feel/chord_nn.json."""
import argparse, json, math, os, random, sys
import numpy as np
import torch, torch.nn as nn
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import chord_eval2 as ce  # noqa: E402  (loads the data; its main does not run)

torch.set_num_threads(4)
CK = sorted({c for s in ce.seqs for c in ce.CL[s]})
ci = {c: i + 2 for i, c in enumerate(CK)}  # 0 pad, 1 start
MODES = sorted({ce.MODE[s] for s in ce.seqs}); mi = {m: i for i, m in enumerate(MODES)}
NV = len(ce.V) + 2  # numerals: 0 pad, 1 start, then V (which ends with </s>)


def encode(s):
    """x numerals, x classes, y targets (the next numeral, then </s>)."""
    xn = [1] + [ce.vi[t] + 2 for t in s]; xc = [1] + [ci[c] for c in ce.CL[s]]
    y = [ce.vi[t] for t in s] + [ce.vi["</s>"]]
    return xn, xc, y


def batch(items):
    L = max(len(i[0]) for i in items)
    pad = lambda a: a + [0] * (L - len(a))
    xn = torch.tensor([pad(i[0]) for i in items]); xc = torch.tensor([pad(i[1]) for i in items])
    y = torch.tensor([i[2] + [-100] * (L - len(i[2])) for i in items]); md = torch.tensor([i[3] for i in items])
    return xn, xc, md, y


class Net(nn.Module):
    def __init__(self, kind, d=96):
        super().__init__()
        self.kind = kind
        self.en = nn.Embedding(NV, d, padding_idx=0); self.ec = nn.Embedding(len(CK) + 2, d, padding_idx=0); self.em = nn.Embedding(len(MODES), d)
        if kind == "lstm":
            self.core = nn.LSTM(d, d, num_layers=2, batch_first=True, dropout=0.3)
        else:
            self.pos = nn.Embedding(64, d)
            layer = nn.TransformerEncoderLayer(d, 4, 2 * d, dropout=0.3, batch_first=True)
            self.core = nn.TransformerEncoder(layer, 2)
        self.drop = nn.Dropout(0.3); self.out = nn.Linear(d, len(ce.V))

    def forward(self, xn, xc, md):
        h = self.en(xn) + self.ec(xc) + self.em(md)[:, None, :]
        if self.kind == "lstm":
            h, _ = self.core(self.drop(h))
        else:
            T = h.shape[1]; h = h + self.pos(torch.arange(T))[None]
            mask = torch.triu(torch.full((T, T), float("-inf")), 1)
            h = self.core(self.drop(h), mask=mask, src_key_padding_mask=(xn == 0))
        return self.out(self.drop(h))


def evaluate(net, items):
    net.eval(); t1 = n = 0; cel = 0.0
    with torch.no_grad():
        for k in range(0, len(items), 256):
            xn, xc, md, y = batch(items[k:k + 256]); lp = torch.log_softmax(net(xn, xc, md), -1)
            m = y != -100; yy = y.clamp(min=0)
            py = lp.gather(-1, yy[..., None])[..., 0]
            t1 += int(((lp > py[..., None]).sum(-1) == 0)[m].sum()); cel -= float(py[m].sum()) / math.log(2); n += int(m.sum())
    return t1 / n, cel / n, n


def run(kind, sd, return_net=False):
    rng = random.Random(sd); idx = list(range(len(ce.seqs))); rng.shuffle(idx); cut = int(.8 * len(idx))
    tr_all = [ce.seqs[i] for i in idx[:cut]]; te = [ce.seqs[i] for i in idx[cut:]]
    u = {}
    for s in tr_all:
        for t in s + ("</s>",): u[t] = u.get(t, 0) + 1
    top = max(u, key=u.get)
    base = sum(t == top for s in te for t in s + ("</s>",)) / sum(len(s) + 1 for s in te)
    r2 = random.Random(100 + sd); va_i = set(r2.sample(range(len(tr_all)), len(tr_all) // 10))
    enc = lambda ss: [encode(s) + (mi[ce.MODE[s]],) for s in ss]
    tr = enc([s for i, s in enumerate(tr_all) if i not in va_i]); va = enc([s for i, s in enumerate(tr_all) if i in va_i]); ts = enc(te)
    torch.manual_seed(sd); net = Net(kind); opt = torch.optim.AdamW(net.parameters(), 2e-3, weight_decay=0.05)
    best, best_state, bad = 1e9, None, 0
    for ep in range(60):
        net.train(); random.Random(ep + 1000 * sd).shuffle(tr)
        for k in range(0, len(tr), 64):
            xn, xc, md, y = batch(tr[k:k + 64])
            loss = nn.functional.cross_entropy(net(xn, xc, md).reshape(-1, len(ce.V)), y.reshape(-1), ignore_index=-100)
            opt.zero_grad(); loss.backward(); nn.utils.clip_grad_norm_(net.parameters(), 1.0); opt.step()
        v = evaluate(net, va)[1]
        if v < best - 1e-3:
            best, bad = v, 0; best_state = {k: t.clone() for k, t in net.state_dict().items()}
        else:
            bad += 1
            if bad >= 6:
                break
    net.load_state_dict(best_state)
    t1, cel, n = evaluate(net, ts)
    return (t1, base, cel, n, ep + 1, net) if return_net else (t1, base, cel, n, ep + 1)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--seeds", default="0,1,2,3,4"); ap.add_argument("--models", default="lstm,transformer")
    a = ap.parse_args(); seeds = [int(x) for x in a.seeds.split(",")]
    out = {}
    ref = os.path.join(HERE, "../../docs/plans/data-universe/feel/chord_eval2.json")
    if os.path.exists(ref):
        r = json.load(open(ref)); out["ngram (chord_eval2 chosen)"] = {"top1": round(float(np.mean(r["top1"])), 4), "gain_points": r["gain_points"], "ce_bits": round(float(np.mean(r["ce"])), 3)}
    print(len(ce.seqs), "progressions,", len(ce.V), "numerals; n-gram:", out.get("ngram (chord_eval2 chosen)"), flush=True)
    for kind in a.models.split(","):
        R = np.array([run(kind, sd)[:4] for sd in seeds]); g = R[:, 0] - R[:, 1]
        out[kind] = {"top1": round(float(R[:, 0].mean()), 4), "top1_sd": round(float(R[:, 0].std()), 4), "gain_points": round(100 * float(g.mean()), 2),
                     "gain_sd": round(100 * float(g.std()), 2), "ce_bits": round(float(R[:, 2].mean()), 3), "seeds": seeds}
        print(kind, out[kind], flush=True)
    json.dump(out, open(os.path.join(HERE, "../../docs/plans/data-universe/feel/chord_nn.json"), "w"), indent=1)


if __name__ == "__main__":
    sys.exit(main())
