#!/usr/bin/env python3
"""Human-patch benchmark: how well does the model trained on sweep banks predict the owner's own settings?   (ml-venv python for score)

    human_bench.py extract [--dir ~/Downloads/Patches]          -> queue_<engine>_human.json per engine (distinct knob settings)
    INF_OUT_TAG=_human own_sweeps.py run --node X --queue ~/Infinite/training-data/own/queue_x_human.json --shards 2
    human_bench.py score --node X [--epochs 300] [--seeds 3]

Only the swept knobs are taken from a patch (the rest sit at the sweep's fixed values), static values only (no cables, no modulation).
Score: the same ensemble (trained outside the held-out banks) predicts the 11 mean axes of (a) the held-out bank rows and (b) the human
renders; per-axis R2 against the measured axes, averaged, plus the error in spreads of the bank. The gap (a) - (b) is the same-generator gap."""
import argparse, glob, json, os, re, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o  # noqa: E402

ENGINES = ("Analog", "Oscillator", "Wavetable", "FM", "Metallic", "String")


def parse_nodes(path):
    out, cur = [], None
    for ln in open(path, errors="replace"):
        m = re.match(r"node \d+ Synths (.+?)\s*$", ln)
        if m:
            cur = {"engine": m.group(1), "p": {}}; out.append(cur); continue
        if ln.startswith("end"):
            cur = None; continue
        m = re.match(r"\s+([fib]) (\S+) (\S+)\s*$", ln)
        if cur is not None and m:
            cur["p"][m.group(2)] = (m.group(1), float(m.group(3)))
    return [n for n in out if n["engine"] in ENGINES]


def cmd_extract(a):
    seen = {}
    for f in sorted(glob.glob(os.path.join(os.path.expanduser(a.dir), "*.inf"))):
        for n in parse_nodes(f):
            seen.setdefault(n["engine"], []).append((os.path.basename(f), n["p"]))
    for eng, lst in seen.items():
        table, sw, fixed = o.param_table(eng); items, ids, clipped, off = [], set(), 0, 0
        for fn, p in lst:
            u = []
            for q in sw:
                v = p.get(q["key"], (None, q["default"]))[1]
                if v < q["lo"] - 1e-9 or v > q["hi"] + 1e-9: clipped += 1
                u.append(min(1., max(0., o.to_u(q, v))))
            off += sum(1 for k, dv in fixed.items() if k in p and abs(p[k][1] - float(dv)) > 1e-6)
            i = o.rid(eng, o.recipe(eng, sw, fixed, u))
            if i in ids: continue
            ids.add(i); items.append({"u": [round(x, 6) for x in u], "design": "human", "from": fn})
        out = os.path.join(o.CACHE_DIR, "queue_%s_human.json" % eng.lower())
        json.dump({"engine": eng, "round": "human", "items": items}, open(out, "w"))
        print("%-10s %3d nodes, %3d distinct settings; %d knob values outside the sweep range (clipped); %d fixed-knob values differ from the sweep's fixed ones -> %s" %
              (eng, len(lst), len(items), clipped, off, out))


def r2_table(P, M, spread):
    r2 = 1 - ((P - M) ** 2).sum(0) / (((M - M.mean(0)) ** 2).sum(0) + 1e-12)
    return r2, float(np.sqrt(((P - M) ** 2).mean(0) / spread ** 2).mean())


def cmd_score(a):
    import p3_characteristics as p3, m1_forward as m1, sound_data as sd, sound_eval as se, reach  # noqa: E402
    node = a.node; real = sd.load(o.out_path(node)); tag = os.environ.pop("INF_OUT_TAG", "")
    os.environ["INF_OUT_TAG"] = "_human"; hum = sd.load(o.out_path(node)); os.environ["INF_OUT_TAG"] = tag
    means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]
    def axes(d):
        V = np.full((len(d["preset_id"]), len(p3.COLS)), np.nan)
        for i in np.flatnonzero(d["ok"][:, 0]): V[i] = p3.vec(d["mel"][i, 0].astype(np.float64) / 2 - 120)
        return V
    V = axes(real); Vh = axes(hum); held = sd.split(real)["group"]
    keep = np.flatnonzero(real["ok"][:, 0] & ~held); hk = np.flatnonzero(hum["ok"][:, 0] & np.isfinite(Vh[:, means]).all(1))
    hb = np.flatnonzero(real["ok"][:, 0] & held & np.isfinite(V[:, means]).all(1))
    X = se.encode(real["knobs"]); enc = reach.make_encoder(real["knobs"]); XH = enc(hum["knobs"])
    perm = np.random.default_rng(0).permutation(keep); nva = max(1, int(.15 * len(perm))); va, fit = perm[:nva], perm[nva:]
    Xa, _, Ma, Ia, Ta = m1.samples(real, X, fit); Xv, _, Mv, Iv, Tv = m1.samples(real, X, va)
    Xa, Ma, Ia, Xv, Mv, Iv = Xa[Ta == 0], Ma[Ta == 0], Ia[Ta == 0], Xv[Tv == 0], Mv[Tv == 0], Iv[Tv == 0]
    mu = np.nanmean(V[Ia], 0); fill = lambda A: np.where(np.isfinite(A), A, mu); mv = np.array([[48, 100]], float) / 127
    fs = [m1.train(Xa, fill(V[Ia]).astype(np.float32), Ma, Xv, fill(V[Iv]).astype(np.float32), Mv, s, a.epochs, lambda s: None)[0] for s in range(a.seeds)]
    pred = lambda XX: np.mean([f(np.hstack([XX, np.repeat(mv, len(XX), 0)]).astype(np.float32))[0][:, means] for f in fs], 0)
    spread = np.nanstd(V[keep][:, means], 0) + 1e-9
    r2b, eb = r2_table(pred(X[hb]), V[hb][:, means], spread); r2h, eh = r2_table(pred(XH[hk]), Vh[hk][:, means], spread)
    names = [p3.COLS[j].split(".")[0] for j in means]
    print("%s: held-out bank n=%d | human patches n=%d" % (node, len(hb), len(hk)))
    print("  %-12s %8s %8s" % ("axis", "bank R2", "human R2"))
    for n, x, y in zip(names, r2b, r2h): print("  %-12s %8.2f %8.2f" % (n, x, y))
    print("  mean R2: bank %.3f | human %.3f | error in bank spreads: bank %.2f | human %.2f" % (r2b.mean(), r2h.mean(), eb, eh))
    print("  human patches' axes spread vs bank: %s" % ", ".join("%s %.2f" % (n, s) for n, s in zip(names, np.nanstd(Vh[hk][:, means], 0) / spread)))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["extract", "score"]); ap.add_argument("--dir", default="~/Downloads/Patches")
    ap.add_argument("--node"); ap.add_argument("--epochs", type=int, default=300); ap.add_argument("--seeds", type=int, default=3)
    a = ap.parse_args(); {"extract": cmd_extract, "score": cmd_score}[a.cmd](a)


if __name__ == "__main__":
    main()
