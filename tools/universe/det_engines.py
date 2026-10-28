#!/usr/bin/env python3
"""Determinism of the own-engine renders: the same recipe rendered twice must give the same audio.

    det_engines.py [--nodes Wavetable FM Metallic String Analog Oscillator] [-n 20]

Per engine: n random recipes from its stored rows, rendered in two separate batches; compares the rendered window (max abs sample
difference, relative to the window's peak). Prints, per engine, how many are bit-identical and the worst relative difference."""
import argparse, os, sys, tempfile
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--nodes", nargs="+", default=["Wavetable", "FM", "Metallic", "String", "Analog", "Oscillator"])
    ap.add_argument("-n", type=int, default=20); a = ap.parse_args()
    import own_sweeps as o, claims as cl, audio_features as af
    for node in a.nodes:
        table, sw, fixed = o.param_table(node); tags = o.tag_of(table); rows = cl.own_rows(o.out_path(node))
        ids = sorted(rows); rng = np.random.default_rng(1); ids = [ids[i] for i in rng.choice(len(ids), min(a.n, len(ids)), replace=False)]
        recs = [rows[p]["recipe"] for p in ids]; W = []
        for _ in range(2):
            with tempfile.TemporaryDirectory(prefix="det_") as tmp:
                out = []
                for w in o.render_batch(o.binary(), node, tags, recs, tmp):
                    if not w: out.append(None); continue
                    r = af.read_pcm(w, 60); out.append((r[0].mean(0), r[1]))
                W.append(out)
        same = worst = 0; n = 0
        for x, y in zip(*W):
            if x is None or y is None: continue
            n += 1; m = min(len(x[0]), len(y[0])); d = np.abs(x[0][:m] - y[0][:m]).max(); pk = max(np.abs(x[0]).max(), 1e-9)
            same += d == 0; worst = max(worst, d / pk)
        print("%-10s %d/%d identical, worst relative difference %.2e" % (node, same, n, worst), flush=True)


if __name__ == "__main__":
    main()
