#!/usr/bin/env python3
"""M0 of SOUND-MODEL.md: one evaluation for every knobs -> sound model, any plug-in (row v2).

    sound_eval.py preset_diva_v2.jsonl [--models mean,ridge,knn,mlp] [--target desc,mel] [--note C3] [--boot 1000]

Data, groups and the two held-out sets come from sound_data.py (random: by preset; group: whole banks and their copies held out).
Knobs: NaN -> column mean; a parameter with <= 12 distinct values across the library is a selector and is one-hot encoded
(oscillator model, filter type: categories, not amounts); the rest are standardised.
Targets: desc = 32 log bands + 11 scalars at one note, scored as mean R2 over outputs; mel = the 64 x 48 log-mel at that note,
scored as mean absolute error in dB (lower is better). Every score carries a 95% bootstrap interval over held-out presets
(resampled by group on the group split), so two models differ only when their intervals do. The group split is the one the
milestone bars use."""
import argparse, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sound_data as sd  # noqa: E402
import sound_model_probe as smp  # noqa: E402


def encode(K, max_levels=12):
    cols = []
    for j in range(K.shape[1]):
        c = K[:, j]; f = np.isfinite(c)
        if not f.any():
            continue
        c = np.where(f, c, np.nanmean(c)); lv = np.unique(np.round(c, 4))
        if len(lv) <= 1:
            continue
        if len(lv) <= max_levels and len(lv) < len(c) / 10:  # a selector, and enough presets to tell
            cols += [(np.round(c, 4) == v).astype(np.float64) for v in lv]
        else:
            cols.append(c.astype(np.float64))
    X = np.stack(cols, 1)
    return (X - X.mean(0)) / (X.std(0) + 1e-9)


def boot(per, groups, n, rng):
    """95% interval of the mean of a per-preset score, resampling groups (clusters) with replacement."""
    u = np.unique(groups); idx = [np.flatnonzero(groups == g) for g in u]; out = []
    for _ in range(n):
        pick = np.concatenate([idx[k] for k in rng.integers(0, len(u), len(u))])
        out.append(per[pick].mean())
    return np.percentile(out, [2.5, 97.5])


def fit(name, X, Y, Xq, rng):
    if name == "mean":  # the floor every model must beat: the training average, whatever the knobs
        return np.tile(Y.mean(0), (len(Xq), 1))
    if name == "ridge":
        return smp.ridge(X, Y, Xq, 300)
    if name == "knn":
        return smp.knn(X, Y, Xq, 5)
    if name == "mlp":
        return smp.mlp(X, Y, Xq, rng)
    raise SystemExit("unknown model " + name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows"); ap.add_argument("--models", default="mean,ridge,knn,mlp"); ap.add_argument("--target", default="desc,mel")
    ap.add_argument("--note", default="C3"); ap.add_argument("--boot", type=int, default=1000)
    ap.add_argument("--noise", default="", help="rows of the same presets rendered a second time: prints the ceiling (render vs render)")
    a = ap.parse_args()
    d = sd.load(a.rows); t = list(d["notes"]).index(a.note); ok = d["ok"][:, t]
    if a.noise:
        n = sd.load(a.noise); pos = {p: i for i, p in enumerate(d["preset_id"])}
        pairs = [(pos[p], j) for j, p in enumerate(n["preset_id"]) if p in pos and d["ok"][pos[p], t] and n["ok"][j, t]]
        A, B = np.array([i for i, _ in pairs]), np.array([j for _, j in pairs])
        e = d["desc"][A, t] - n["desc"][B, t]; v = d["desc"][ok, t].var(0)
        mel = np.abs(d["mel"][A, t].astype(float) - n["mel"][B, t]).mean() / 2
        print("noise floor at %s from %d presets rendered twice: desc R2 ceiling %.3f (bands %.3f, scalars %.3f) | mel %.2f dB"
              % (a.note, len(pairs), 1 - ((e ** 2).mean(0) / v).mean(), 1 - ((e[:, :32] ** 2).mean(0) / v[:32]).mean(),
                 1 - ((e[:, 32:] ** 2).mean(0) / v[32:]).mean(), mel))
    X = encode(d["knobs"][ok]); g = d["group"][ok]; masks = {k: m[ok] for k, m in sd.split(d).items()}
    print("%d presets at %s, %d knob features (from %d host parameters), %d groups" % (ok.sum(), a.note, X.shape[1], d["knobs"].shape[1], g.max() + 1))
    print("%-7s %-5s %-6s %5s %5s  %s" % ("split", "tgt", "model", "train", "test", "score [95% interval]"))
    for tg in a.target.split(","):
        if tg == "desc":
            Y = d["desc"][ok, t].astype(np.float64)
        else:
            Y = d["mel"][ok, t].reshape(ok.sum(), -1).astype(np.float64) / 2 - 120
            if not d["mel"][ok, t].any():
                print("  (no mel in these rows)"); continue
        for sn, m in masks.items():
            tr, te = np.flatnonzero(~m), np.flatnonzero(m)
            if len(te) < 30 or len(np.unique(g[te])) < 5:
                print("%-7s %-5s  too few held out (%d presets, %d groups): no score" % (sn, tg, len(te), len(np.unique(g[te])))); continue
            for mn in a.models.split(","):
                P = fit(mn, X[tr], Y[tr], X[te], np.random.default_rng(0))
                if tg == "desc":
                    v = ((Y[te] - Y[te].mean(0)) ** 2).sum(0); e = (Y[te] - P) ** 2
                    # per-preset share of the R2 so the bootstrap resamples presets: R2 = 1 - sum_i e_i / v, averaged over outputs
                    per = 1 - (e / np.maximum(v, 1e-12)).mean(1) * len(te)
                    score, unit = smp.r2(Y[te], P).mean(), "R2"
                else:
                    per = np.abs(Y[te] - P).mean(1); score, unit = per.mean(), "dB"
                lo, hi = boot(per, g[te], a.boot, np.random.default_rng(1))
                print("%-7s %-5s %-6s %5d %5d  %s %.3f [%.3f, %.3f]" % (sn, tg, mn, len(tr), len(te), unit, score, lo, hi))


if __name__ == "__main__":
    main()
