#!/usr/bin/env python3
"""P3 of the sound-model handoff: does the forward model understand the characteristics? knobs + note -> characteristic summaries.

    p3_characteristics.py diva_m1.jsonl [--frac 0.5] [--seeds 1] [--epochs 300] [--out DIR] [--extra ROWS.jsonl ...]

Same data, split, knob encoding and network as M1 (m1_forward.train), with the 43 raw descriptors replaced by the characteristics of
each note (characteristics.from_mel on the stored mel: mean, std and slope of loudness, brightness, sharpness, warmth, weight,
fullness, noisiness, movement; attack, decay, density). The mel head is kept, and the characteristics are also measured on the
predicted mel, so each axis gets two scores: the direct head and "through the predicted sound". Bar (HANDOFF.md P3): R2 >= 0.6 per
characteristic mean on the group split at C3. k-NN in knob space (k=5) is the baseline. Results append to <out>/results.txt.
--extra adds rows (D1 new presets, D2 one-knob variants) to fitting only; the test presets stay those of the main file, and an extra
row whose bank is held out on the group split is dropped, so no bank leaks into the honest score."""
import argparse, os, sys, time
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import characteristics as C  # noqa: E402
import m1_forward as m1  # noqa: E402
import sound_data as sd  # noqa: E402
import sound_eval as se  # noqa: E402
import sound_model_probe as smp  # noqa: E402

CURVED = ["loudness", "brightness", "sharpness", "warmth", "weight", "fullness", "noisiness", "movement"]
ONE = ["attack", "decay"]
COLS = ["%s.%s" % (a, s) for a in CURVED for s in ("mean", "std", "slope")] + ["%s.mean" % a for a in ONE + ["density"]]
BAR = 0.6


def vec(mel_db):
    """64 x 48 mel in dB -> the characteristic vector (COLS); NaN where an axis is silent."""
    q = np.clip(np.round((mel_db + 120) * 2), 0, 255).astype(np.uint8)
    c, dt = C.from_mel(q)
    ax = C.summary(c, dt, one=ONE)
    return np.array([ax.get(n.split(".")[0], {}).get(n.split(".")[1], np.nan) for n in COLS])


def r2cols(Y, P):
    ok = np.isfinite(Y) & np.isfinite(P)
    out = []
    for j in range(Y.shape[1]):
        y, p = Y[ok[:, j], j], P[ok[:, j], j]
        out.append(1 - ((y - p) ** 2).sum() / max(((y - y.mean()) ** 2).sum(), 1e-12) if y.size > 10 else np.nan)
    return np.array(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows"); ap.add_argument("--seeds", type=int, default=1); ap.add_argument("--epochs", type=int, default=300)
    ap.add_argument("--frac", type=float, default=1.0); ap.add_argument("--out", default="~/Infinite/training-data/models/p3_char")
    ap.add_argument("--extra", nargs="*", default=[])
    a = ap.parse_args()
    out = os.path.expanduser(a.out); os.makedirs(out, exist_ok=True); lines = []
    def log(s):
        print(s, flush=True); lines.append(s)
    d = sd.load(a.rows); keep = np.flatnonzero(d["ok"].any(1))
    ex = [sd.load(p) for p in a.extra]
    e = {k: np.concatenate([x[k] for x in ex]) for k in ("knobs", "mel", "ok", "src", "preset_id", "desc")} if ex else None
    if e is not None:
        e["notes"] = ex[0]["notes"]
        assert all(list(x["notes"]) == list(d["notes"]) for x in ex), "extra rows must use the main file's notes"
        e["folder"] = np.array(sd.banks(list(d["src"]) + list(e["src"]))[len(d["src"]):])
    XX = se.encode(np.vstack([d["knobs"]] + ([e["knobs"]] if e is not None else [])))  # one encoding, so extra columns match
    X = XX[:len(d["knobs"])]; XE = XX[len(d["knobs"]):]
    t0 = time.time()
    def measure(dd):
        V = np.full(dd["ok"].shape + (len(COLS),), np.nan)
        for i, t in zip(*np.nonzero(dd["ok"])):
            V[i, t] = vec(dd["mel"][i, t].astype(np.float64) / 2 - 120)
        return V
    V = measure(d); VE = measure(e) if e is not None else None
    log("P3 | frac %.2f | %s | %d presets, %d targets | extra %s (%d presets) | targets measured in %.0f s | %s"
        % (a.frac, a.rows, len(keep), len(COLS), ",".join(os.path.basename(p) for p in a.extra) or "none",
           0 if e is None else len(e["src"]), time.time() - t0, time.strftime("%Y-%m-%d %H:%M")))
    for sn, m in sd.split(d).items():
        te_p, tr_p = keep[m[keep]], keep[~m[keep]]
        gtr = np.unique(d["group"][tr_p]); rng = np.random.default_rng(0); va_g = set(rng.choice(gtr, max(1, int(0.15 * len(gtr))), False))
        va_p = tr_p[np.isin(d["group"][tr_p], list(va_g))] if sn == "group" else tr_p[rng.random(len(tr_p)) < 0.15]
        fit_p = np.setdiff1d(tr_p, va_p)
        if a.frac < 1:
            fit_p = np.sort(np.random.default_rng(1).choice(fit_p, max(1, int(a.frac * len(fit_p))), False))
        Xa, _, Ma, Ia, Ta = m1.samples(d, X, fit_p); Xv, _, Mv, Iv, Tv = m1.samples(d, X, va_p); Xt, _, Mt, It, Tt = m1.samples(d, X, te_p)
        Va, Vv, Vt = V[Ia, Ta], V[Iv, Tv], V[It, Tt]
        if e is not None:
            held = set(d["folder"][te_p]) if sn == "group" else set()
            use = np.flatnonzero(e["ok"].any(1) & ~np.isin(e["folder"], list(held)) & ~np.isin(e["preset_id"], d["preset_id"][te_p]))
            Xe, _, Me, Ie, Te = m1.samples(e, XE, use)
            Xa, Ma, Va = np.vstack([Xa, Xe]), np.vstack([Ma, Me]), np.vstack([Va, VE[Ie, Te]])
            Ia, Ta = np.concatenate([Ia, -1 - Ie]), np.concatenate([Ta, Te])  # negative: extra rows (k-NN uses main rows only)
            log("  extra rows in fitting: %d presets (%d dropped as held-out banks or test presets)" % (len(use), int(e["ok"].any(1).sum()) - len(use)))
        mu = np.nanmean(Va, 0); fill = lambda A: np.where(np.isfinite(A), A, mu)  # silent axes: train on the mean, score only measured ones
        log("\n%s split: fit %d, validation %d, test %d presets" % (sn, len(fit_p), len(va_p), len(te_p)))
        kn = np.zeros_like(Vt)
        for t in np.unique(Tt):
            q, r = Tt == t, (Ta == t) & (Ia >= 0)
            kn[q] = smp.knn(X[Ia[r]], fill(Va)[r], X[It[q]], 5)
        runs, via = [], []
        for s in range(a.seeds):
            f, _ = m1.train(Xa, fill(Va).astype(np.float32), Ma, Xv, fill(Vv).astype(np.float32), Mv, s, a.epochs, log)
            pv, pm = f(Xt); runs.append(pv); via.append(np.array([vec(x) for x in pm]))
        P, PM = np.mean(runs, 0), np.nanmean(via, 0)
        c3 = Tt == 0
        rk, rd, rm = r2cols(Vt[c3], kn[c3]), r2cols(Vt[c3], P[c3]), r2cols(Vt[c3], PM[c3])
        log("  %-18s %8s %8s %8s %s" % ("characteristic", "k-NN", "head", "via mel", "bar %.1f (head)" % BAR))
        for j, n in enumerate(COLS):
            log("  %-18s %8.3f %8.3f %8.3f %s" % (n, rk[j], rd[j], rm[j], ("PASS" if rd[j] >= BAR else "FAIL") if n.endswith(".mean") else ""))
        means = [j for j, n in enumerate(COLS) if n.endswith(".mean")]
        log("  %s: %d / %d characteristic means at the bar; median R2 of means: head %.3f, via mel %.3f, k-NN %.3f"
            % (sn, sum(rd[j] >= BAR for j in means), len(means), np.nanmedian(rd[means]), np.nanmedian(rm[means]), np.nanmedian(rk[means])))
    open(os.path.join(out, "results.txt"), "a").write("\n".join(lines) + "\n\n")


if __name__ == "__main__":
    main()
