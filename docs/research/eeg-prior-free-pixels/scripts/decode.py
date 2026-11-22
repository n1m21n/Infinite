"""Prior-free ridge decoding of CIELAB grids from THINGS-EEG2, one subject at a time.

No pretrained image or vision model is used anywhere: inputs are EEG samples, targets are pixel grids.

Usage:
  python decode.py --phase 0 --grids GRIDS.npz --cache CACHE --out OUTDIR --sub sub-01   # controls only
  python decode.py --phase 1 --grids GRIDS.npz --cache CACHE --out OUTDIR --sub sub-01   # real decoder

Phase 0 writes OUTDIR/phase0/sub-XX.npz (controls), Phase 1 writes OUTDIR/phase1/sub-XX.npz.
Everything that varies (shuffles, splits, repeat subsets) is seeded from SEED.
"""
import argparse
import json
import os

import numpy as np

NS = (1, 2, 3, 4, 6, 8, 16)
LAMBDAS = np.logspace(-2, 6, 17)
SEED = 20261122
SFREQ_T0 = 20                     # index of 0 ms in the -200..790 ms epoch (100 Hz)
WIN_MAIN = (20, 70)               # 0..490 ms
WIN_PRE = (0, 20)                 # -200..-10 ms
WIN_EARLY = (20, 40)              # 0..190 ms, same length as WIN_PRE
REPEATS_CURVE = (1, 4, 10, 20, 40, 80)
HOMOLOGOUS = [("O1", "O2"), ("PO7", "PO8"), ("PO3", "PO4"), ("P7", "P8"), ("P5", "P6"),
              ("P3", "P4"), ("P1", "P2")]


# ---------------------------------------------------------------- targets
def target_matrix(grids, split):
    """Columns: for each N, raw cells (N*N*3) then residual cells (N>=2). Returns Y and a column table."""
    cols, blocks = [], []
    for N in NS:
        g = grids[f"{split}_N{N}"].astype(np.float64)          # (n, N, N, 3)
        n = g.shape[0]
        for kind in ("raw", "res"):
            if kind == "res" and N == 1:
                continue
            v = g - g.mean((1, 2), keepdims=True) if kind == "res" else g
            blocks.append(v.reshape(n, -1))
            for r in range(N):
                for c in range(N):
                    for ch in range(3):
                        cols.append((N, kind, r, c, ch))
    return np.concatenate(blocks, 1), np.array(cols, dtype=[("N", "i4"), ("kind", "U3"), ("r", "i4"),
                                                             ("c", "i4"), ("ch", "i4")])


def flip_lr_columns(cols):
    """Index map: column j -> column of the same cell mirrored left-right."""
    key = {(int(N), k, int(r), int(c), int(ch)): j for j, (N, k, r, c, ch) in enumerate(cols)}
    return np.array([key[(int(N), k, int(r), int(N - 1 - c), int(ch))] for (N, k, r, c, ch) in cols])


# ---------------------------------------------------------------- ridge
def features(x, win):
    a, b = win
    return x[..., a:b].reshape(*x.shape[:-2], -1)


def svd_fit(X, Y):
    U, s, Vt = np.linalg.svd(X, full_matrices=False)
    return s, Vt.T, U.T @ Y


def ridge_predict(Xnew, s, V, UtY, lam):
    return Xnew @ (V * (s / (s ** 2 + lam))) @ UtY


def col_corr(P, Y):
    P = P - P.mean(0)
    Y = Y - Y.mean(0)
    den = np.sqrt((P ** 2).sum(0) * (Y ** 2).sum(0))
    return np.where(den > 0, (P * Y).sum(0) / np.where(den > 0, den, 1), 0.0)


def choose_lambda(X, Y, cols, concepts, rng, folds=5):
    """5-fold CV grouped by concept; one lambda per (N, kind, channel) maximising mean CV r.
    Returns (lambda index per column, CV predictions at the chosen lambdas)."""
    uc = np.unique(concepts)
    order = rng.permutation(len(uc))
    fold_of = {c: i % folds for i, c in enumerate(uc[order])}
    f = np.array([fold_of[c] for c in concepts])
    fits = []
    for k in range(folds):
        tr, va = f != k, f == k
        mu, sd = X[tr].mean(0), X[tr].std(0) + 1e-8
        ym = Y[tr].mean(0)
        s, V, UtY = svd_fit((X[tr] - mu) / sd, Y[tr] - ym)
        fits.append((va, (X[va] - mu) / sd, s, V, UtY, ym))

    def cv_pred(li, js=slice(None)):
        P = np.zeros((len(Y), Y[:, js].shape[1]), dtype=np.float32)
        for va, Xv, s, V, UtY, ym in fits:
            P[va] = ridge_predict(Xv, s, V, UtY[:, js], LAMBDAS[li]) + ym[js]
        return P

    R = np.stack([col_corr(cv_pred(li), Y) for li in range(len(LAMBDAS))])
    groups = {}
    for j, (N, kind, r, c, ch) in enumerate(cols):
        groups.setdefault((int(N), str(kind), int(ch)), []).append(j)
    lam_idx = np.zeros(Y.shape[1], dtype=int)
    for js in groups.values():
        lam_idx[js] = int(np.argmax(R[:, js].mean(1)))
    cvpred = np.zeros(Y.shape, dtype=np.float32)
    for li in np.unique(lam_idx):
        js = np.where(lam_idx == li)[0]
        cvpred[:, js] = cv_pred(li, js)
    return lam_idx, cvpred


class Ridge:
    def __init__(self, Xtr, Ytr, lam_idx):
        self.mu, self.sd = Xtr.mean(0), Xtr.std(0) + 1e-8
        self.ym = Ytr.mean(0)
        s, V, UtY = svd_fit((Xtr - self.mu) / self.sd, Ytr - self.ym)
        self.W = np.zeros((Xtr.shape[1], Ytr.shape[1]))
        for li in np.unique(lam_idx):
            js = lam_idx == li
            self.W[:, js] = (V * (s / (s ** 2 + LAMBDAS[li]))) @ UtY[:, js]

    def __call__(self, X):
        return (((X - self.mu) / self.sd) @ self.W + self.ym).astype(np.float32)


def fit_predict(Xtr, Ytr, Xtests, lam_idx):
    m = Ridge(Xtr, Ytr, lam_idx)
    return [m(Xt) for Xt in Xtests], m


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", type=int, required=True)
    ap.add_argument("--grids", required=True)
    ap.add_argument("--cache", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--sub", required=True)
    a = ap.parse_args()
    rng = np.random.default_rng(SEED + int(a.sub[-2:]))
    grids = np.load(a.grids)
    Ytr, cols = target_matrix(grids, "train")
    Yte, _ = target_matrix(grids, "test")
    concepts = grids["train_concepts"]
    info = json.load(open(f"{a.cache}/{a.sub}_info.json"))
    ch = info["ch_names"]
    eeg_tr = np.load(f"{a.cache}/{a.sub}_train.npy").astype(np.float64)        # (16540, 17, 100)
    eeg_te_all = np.load(f"{a.cache}/{a.sub}_test.npy").astype(np.float64)     # (200, 80, 17, 100)
    eeg_te = eeg_te_all.mean(1)
    res = {"cols_N": cols["N"], "cols_kind": cols["kind"], "cols_r": cols["r"], "cols_c": cols["c"],
           "cols_ch": cols["ch"], "Yte": Yte.astype(np.float32)}
    os.makedirs(f"{a.out}/phase{a.phase}", exist_ok=True)

    if a.phase == 0:
        # (a) shuffled training labels, 5 seeds; lambda re-chosen by CV on the shuffled pairing
        Xtr = features(eeg_tr, WIN_MAIN)
        Xte = features(eeg_te, WIN_MAIN)
        sh = []
        for sseed in range(5):
            p = np.random.default_rng(SEED + 1000 * sseed + int(a.sub[-2:])).permutation(len(Ytr))
            lam, _ = choose_lambda(Xtr, Ytr[p], cols, concepts, rng)
            (P,), _ = fit_predict(Xtr, Ytr[p], [Xte], lam)
            sh.append(P)
        res["pred_shuffled"] = np.array(sh, dtype=np.float32)
        # (b) pre-stimulus window and the matched-length early window
        for name, win in (("pre", WIN_PRE), ("early", WIN_EARLY)):
            Xtr_w, Xte_w = features(eeg_tr, win), features(eeg_te, win)
            lam, _ = choose_lambda(Xtr_w, Ytr, cols, concepts, rng)
            (P,), _ = fit_predict(Xtr_w, Ytr, [Xte_w], lam)
            res[f"pred_{name}"] = P.astype(np.float32)
        # (c) mean training grid for every test image
        res["pred_mean"] = np.repeat(Ytr.mean(0, keepdims=True), len(Yte), 0).astype(np.float32)
        np.savez_compressed(f"{a.out}/phase0/{a.sub}.npz", **res)
        print(a.sub, "phase 0 saved")
        return

    # ---------------- phase 1: the real decoder
    Xtr = features(eeg_tr, WIN_MAIN)
    lam_idx, cvpred = choose_lambda(Xtr, Ytr, cols, concepts, rng)
    res["lam"] = LAMBDAS[lam_idx]
    # training-CV spread of predictions, used only to variance-match pictures and sounds
    res["cv_pred_sd"] = cvpred.std(0).astype(np.float32)
    res["train_target_sd"] = Ytr.std(0).astype(np.float32)
    res["train_target_mean"] = Ytr.mean(0).astype(np.float32)
    res["cv_r"] = col_corr(cvpred, Ytr).astype(np.float32)

    def swapped(x, pairs):
        y = x.copy()
        for l, r in pairs:
            if l in ch and r in ch:
                i, j = ch.index(l), ch.index(r)
                y[..., i, :], y[..., j, :] = x[..., j, :], x[..., i, :]
        return y

    tests = {"main": eeg_te,
             "swapO": swapped(eeg_te, HOMOLOGOUS[:1]),
             "swapAll": swapped(eeg_te, HOMOLOGOUS)}
    # repeats curve: mean of k random repeats per image (fixed seed); 80 = all
    for k in REPEATS_CURVE[:-1]:
        idx = np.array([rng.choice(80, k, replace=False) for _ in range(len(eeg_te_all))])
        tests[f"rep{k}"] = np.take_along_axis(eeg_te_all, idx[:, :, None, None], 1).mean(1)
    model = Ridge(Xtr, Ytr, lam_idx)
    for n, x in tests.items():
        res[f"pred_{n}"] = model(features(x, WIN_MAIN))
    rel = np.zeros(Yte.shape[1])
    for _ in range(100):
        p = rng.permutation(80)
        pa = model(features(eeg_te_all[:, p[:40]].mean(1), WIN_MAIN))
        pb = model(features(eeg_te_all[:, p[40:]].mean(1), WIN_MAIN))
        rel += col_corr(pa, pb) / 100
    sb = 2 * rel / (1 + rel)
    res["split_half_r"] = rel.astype(np.float32)
    res["noise_ceiling"] = np.sqrt(np.clip(sb, 0, None)).astype(np.float32)

    # Haufe activation patterns for N<=3 raw cells: A = Cov(X) w / var(yhat)
    Xz = (Xtr - model.mu) / model.sd
    sel = np.where((cols["N"] <= 3) & (cols["kind"] == "raw"))[0]
    W = model.W[:, sel]
    yhat = Xz @ W
    A = (Xz.T @ yhat) / len(Xz) / (yhat.var(0) + 1e-12)
    res["haufe_cols"] = sel
    res["haufe"] = A.reshape(len(ch), WIN_MAIN[1] - WIN_MAIN[0], len(sel)).astype(np.float32)

    # searchlights for N<=4 raw cells: each channel alone (all times), each 50 ms window (all channels)
    sl = np.where((cols["N"] <= 4) & (cols["kind"] == "raw"))[0]
    Ysl_tr, Ysl_te = Ytr[:, sl], Yte[:, sl]
    sub_cols = cols[sl]
    r_chan = np.zeros((len(ch), len(sl)))
    for i in range(len(ch)):
        Xa = eeg_tr[:, i:i + 1, WIN_MAIN[0]:WIN_MAIN[1]].reshape(len(eeg_tr), -1)
        Xb = eeg_te[:, i:i + 1, WIN_MAIN[0]:WIN_MAIN[1]].reshape(len(eeg_te), -1)
        lam, _ = choose_lambda(Xa, Ysl_tr, sub_cols, concepts, rng)
        (P,), _ = fit_predict(Xa, Ysl_tr, [Xb], lam)
        r_chan[i] = col_corr(P, Ysl_te)
    wins = [(SFREQ_T0 + 5 * k, SFREQ_T0 + 5 * k + 5) for k in range(10)]
    r_time = np.zeros((len(wins), len(sl)))
    for k, w in enumerate(wins):
        Xa, Xb = features(eeg_tr, w), features(eeg_te, w)
        lam, _ = choose_lambda(Xa, Ysl_tr, sub_cols, concepts, rng)
        (P,), _ = fit_predict(Xa, Ysl_tr, [Xb], lam)
        r_time[k] = col_corr(P, Ysl_te)
    res["sl_cols"] = sl
    res["r_chan"] = r_chan.astype(np.float32)
    res["r_time"] = r_time.astype(np.float32)
    res["ch_names"] = np.array(ch)
    np.savez_compressed(f"{a.out}/phase1/{a.sub}.npz", **res)
    print(a.sub, "phase 1 saved")


if __name__ == "__main__":
    main()
