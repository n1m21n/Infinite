#!/usr/bin/env python3
"""PLAN-2026-10-30 item 3: does sampling the next-chord transformer (chord_nn.py, item 2's winner) with a temperature give
progressions as varied as real ones, and still real-sounding? Compared with what ships today: uniform draws from the note
bank's progressions (GenerateSamplesNotes.inc, kNoteProgs).
    ~/Infinite/ml-venv/bin/python3 -I tools/universe/chord_sample.py [--seeds 0,1,2,3,4] [--temps 0.6,0.8,1.0,1.2,1.5] [--n 2000]
Everything is in the bank's own terms: four chord roots as scale degrees (0-6), major or minor family (the first chord's quality).
- variety: entropy (bits) of the four-degree progressions, at the real set's size (generated draws subsampled to it, 50 times);
  bar 0.90 x the real progressions' entropy (the B5 rule; B5 itself in benchmark.py scores drum grooves, not chords).
- realism: Jensen-Shannon distance between degree-bigram shares and the held-out 20% (never trained on); the train 80% against
  the same 20% is the floor, the shipped bank is the one to beat.
Result: docs/plans/data-universe/feel/chord_sample.json."""
import argparse, collections, json, math, os, random, re, sys
import numpy as np
import torch
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import chord_nn as cn  # noqa: E402
ce = cn.ce
INC = os.path.join(HERE, "../../src/app/graph/GenerateSamplesNotes.inc")

ROWS = [json.loads(l) for l in open(ce.S)]
DEG = {}
FAM = {}
for r in ROWS:
    for t, c in zip(r["numerals"], r["chords"]): DEG.setdefault(t, c["deg"] - 1)
    FAM.setdefault(tuple(r["numerals"]), 1 if r["chords"][0]["minor"] else 0)
CLS = collections.defaultdict(collections.Counter)  # (mode, numeral) -> class token, for the class input while sampling
for s in ce.seqs:
    for t, c in zip(s, ce.CL[s]): CLS[(ce.MODE[s], t)][c] += 1


def prog(seq):
    """First four chords as degrees, looped like the bank when shorter (the bank is four roots)."""
    d = [DEG[t] for t in seq]
    return tuple((d * 4)[:4]) if d else None


def bank():
    rows = re.findall(r"\{ (\d), \d+, \{ (\d), (\d), (\d), (\d) \} \}", open(INC).read())
    return [(int(m), tuple(int(x) for x in d)) for m, *d in rows]


def entropy(progs):
    c = collections.Counter(progs); n = len(progs)
    return -sum(v / n * math.log2(v / n) for v in c.values())


def ent_at(progs, n, rng, reps=50):
    return float(np.mean([entropy(rng.sample(progs, n)) for _ in range(reps)])) if len(progs) > n else entropy(progs)


def bigrams(progs):
    c = np.full((7, 7), 1e-6)
    for p in progs:
        for a, b in zip(p, p[1:] + p[:1]): c[a, b] += 1
    return (c / c.sum()).ravel()


def js(p, q):
    m = (p + q) / 2
    kl = lambda a, b: float(np.sum(a * np.log2(a / b)))
    return math.sqrt(max(0.0, (kl(p, m) + kl(q, m)) / 2))


def sample(net, mode, temp, rng, maxlen=8):
    net.eval(); xn, xc, out = [1], [1], []
    end = ce.vi["</s>"]
    with torch.no_grad():
        for _ in range(maxlen):
            logits = net(torch.tensor([xn]), torch.tensor([xc]), torch.tensor([cn.mi[mode]]))[0, -1] / temp
            p = torch.softmax(logits, -1).numpy().astype(np.float64)
            k = int(rng.choices(range(len(p)), weights=p)[0])
            if k == end:
                if out: break
                continue
            t = ce.V[k]; c = CLS[(mode, t)]
            out.append(t); xn.append(k + 2); xc.append(cn.ci[c.most_common(1)[0][0]] if c else cn.ci[ce.CL[ce.seqs[0]][0]])
    return tuple(out)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--seeds", default="0,1,2,3,4"); ap.add_argument("--temps", default="0.6,0.8,1.0,1.2,1.5")
    ap.add_argument("--n", type=int, default=2000); a = ap.parse_args()
    seeds = [int(x) for x in a.seeds.split(",")]; temps = [float(x) for x in a.temps.split(",")]
    real = [prog(s) for s in ce.seqs]; n_real = len(real); rng = random.Random(7)
    h_real = entropy(real)
    modes = [ce.MODE[s] for s in ce.seqs]
    B = bank(); bank_draws = [rng.choice(B)[1] for _ in range(a.n)]
    res = {"real": {"progressions": n_real, "entropy_bits": round(h_real, 3)}, "bank": {}, "temps": {}}
    acc = collections.defaultdict(list)
    for sd in seeds:
        t1, base, cel, n, ep, net = cn.run("transformer", sd, return_net=True)
        r0 = random.Random(sd); idx = list(range(len(ce.seqs))); r0.shuffle(idx); cut = int(.8 * len(idx))
        tr = [real[i] for i in idx[:cut]]; te = [real[i] for i in idx[cut:]]; trs = {ce.seqs[i] for i in idx[:cut]}
        bt = bigrams(te)
        acc["floor"].append(js(bigrams(tr), bt)); acc["bank_js"].append(js(bigrams(bank_draws), bt))
        for T in temps:
            sr = random.Random(1000 * sd + int(100 * T)); seqs = [sample(net, sr.choice(modes), T, sr) for _ in range(a.n)]
            ps = [prog(s) for s in seqs if s]
            acc[("js", T)].append(js(bigrams(ps), bt)); acc[("ent", T)].append(ent_at(ps, n_real, sr))
            acc[("novel", T)].append(sum(s not in trs for s in seqs) / len(seqs))
        print("seed", sd, "test top1 %.3f" % t1, flush=True)
    res["floor_js_train_vs_test"] = round(float(np.mean(acc["floor"])), 4)
    res["bank"] = {"rows": len(B), "entropy_bits": round(ent_at(bank_draws, n_real, rng), 3), "js_vs_test": round(float(np.mean(acc["bank_js"])), 4)}
    print("real: %d progressions, entropy %.2f bits; bar >= %.2f" % (n_real, h_real, 0.9 * h_real))
    print("train vs test bigram JS (floor) %.3f" % res["floor_js_train_vs_test"])
    print("%-10s %8s %8s %8s %10s" % ("source", "entropy", "x real", "JS test", "novel"))
    print("%-10s %8.2f %8.2f %8.3f %10s" % ("bank", res["bank"]["entropy_bits"], res["bank"]["entropy_bits"] / h_real, res["bank"]["js_vs_test"], "-"))
    for T in temps:
        e = float(np.mean(acc[("ent", T)])); j = float(np.mean(acc[("js", T)])); nv = float(np.mean(acc[("novel", T)]))
        res["temps"][str(T)] = {"entropy_bits": round(e, 3), "x_real": round(e / h_real, 3), "js_vs_test": round(j, 4), "js_sd": round(float(np.std(acc[("js", T)])), 4), "novel": round(nv, 3)}
        print("%-10s %8.2f %8.2f %8.3f %9.0f%%" % ("T=%.1f" % T, e, e / h_real, j, 100 * nv))
    json.dump(res, open(os.path.join(HERE, "../../docs/plans/data-universe/feel/chord_sample.json"), "w"), indent=1)


if __name__ == "__main__":
    sys.exit(main())
