#!/usr/bin/env python3
"""Embedding monotonicity (ml-venv python): does the embedding move steadily when one knob moves steadily?

    embed_monotone.py --node Analog [--bases 20] [--levels 5] [--knobs 12]

For random base settings, each continuous knob is swept over `levels` evenly spaced unit values (others fixed), rendered, embedded
(CLAP 512 + EnCodec mean/std). Per sweep: Spearman of level index vs embedding distance from the lowest level (CLAP part and
EnCodec part separately), only for sweeps where the embedding moves more than 2x the distance between two renders of the same
setting (zero here: renders are deterministic, so 'moves at all' = range > 1e-6). Reports share of sweeps with rho >= 0.8, mean rho,
and the share of sweeps where the knob did not move the embedding at all. Local only."""
import argparse, os, sys, tempfile
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)


def rank(a):
    r = np.empty(len(a)); r[np.argsort(a, kind="stable")] = np.arange(len(a)); return r


def spearman(x, y):
    rx, ry = rank(x), rank(y)
    if rx.std() == 0 or ry.std() == 0: return np.nan
    return float(np.corrcoef(rx, ry)[0, 1])


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", default="Analog"); ap.add_argument("--bases", type=int, default=20)
    ap.add_argument("--levels", type=int, default=5); ap.add_argument("--knobs", type=int, default=12); ap.add_argument("--seed", type=int, default=0)
    a = ap.parse_args()
    import own_sweeps as o, embed as em, audio_features as af
    table, sw, fixed = o.param_table(a.node); tags = o.tag_of(table)
    cont = [j for j, q in enumerate(sw) if q["kind"] in ("lin", "log", "time")]
    rng = np.random.default_rng(a.seed); M = em.models(); lv = np.linspace(0.05, 0.95, a.levels)
    res = []
    for b in range(a.bases):
        base = rng.uniform(0.2, 0.8, len(sw))
        for j in rng.choice(cont, min(a.knobs, len(cont)), replace=False):
            recs = []
            for t in lv:
                u = base.copy(); u[j] = t; recs.append(o.recipe(a.node, sw, fixed, list(u)))
            with tempfile.TemporaryDirectory(prefix="mono_") as tmp:
                wavs = o.render_batch(o.binary(), a.node, tags, recs, tmp)
                E = []
                for w in wavs:
                    if not w: E = []; break
                    r = af.read_pcm(w, 60); x = r[0].mean(0); rate = r[1]; s0 = int(o.LEAD * rate)
                    E.append(em.embed(M, em.resample(x[s0:s0 + int(o.WIN * rate)], rate, 48000)))
            if len(E) != len(lv): continue
            E = np.array(E); d_c = np.linalg.norm(E[:, :512] - E[0, :512], axis=1); d_e = np.linalg.norm(E[:, 512:] - E[0, 512:], axis=1)
            res.append((sw[j]["key"], d_c.max() > 1e-6, spearman(lv, d_c), spearman(lv, d_e)))
        print("base %d/%d, %d sweeps" % (b + 1, a.bases, len(res)), flush=True)
    moved = [r for r in res if r[1]]
    print("%s: %d sweeps (%d bases x up to %d continuous knobs x %d levels); knob moved the embedding in %d (%.0f%%)" %
          (a.node, len(res), a.bases, a.knobs, a.levels, len(moved), 100 * len(moved) / max(1, len(res))))
    for name, k in (("CLAP", 2), ("EnCodec", 3)):
        r = np.array([m[k] for m in moved], float); r = r[np.isfinite(r)]
        print("  %-8s mean rho %.2f | rho >= 0.8: %.0f%% | rho >= 0.5: %.0f%% | rho < 0: %.0f%%" % (name, r.mean(), 100 * (r >= .8).mean(), 100 * (r >= .5).mean(), 100 * (r < 0).mean()))
    by = {}
    for m in moved:
        if np.isfinite(m[2]): by.setdefault(m[0], []).append(m[2])
    print("  per knob (CLAP mean rho): " + ", ".join("%s %.2f" % (k, np.mean(v)) for k, v in sorted(by.items(), key=lambda kv: np.mean(kv[1]))))


if __name__ == "__main__":
    main()
