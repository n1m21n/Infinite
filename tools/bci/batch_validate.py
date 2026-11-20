#!/usr/bin/env python3
"""Block A validation: per-region vs averaged alpha on many people.

Mirrors the per-region DSP planned for BciDspEngine:
  per-channel Welch (2 s Hann, 50 % overlap) -> robust log-log 1/f fit 3-40 Hz
  (peaks dropped iteratively) -> alpha prominence = mean 10^(log P - fit) over 8-12 Hz
  plus alpha wPLI / PLV for the AF7-AF8 and TP9-TP10 pairs.

Metrics (see docs/plans/bci/STRATEGY.md section 7):
  M1  any region: closed/open alpha prominence ratio > 1.5
  M2  best region (picked on the first half of windows) beats the averaged spectrum
      on Cohen's d measured on the second half (held out)
  acc per-window eyes-closed detection accuracy, threshold set on first half

usage:
  batch_validate.py physionet DIR [--csv-out DIR]
  batch_validate.py gipsa DIR
  batch_validate.py heroic DIR
Run with the datasets venv (numpy + scipy): ~/Infinite/datasets/eeg/.venv/bin/python
"""
import argparse
import glob
import json
import os
import sys

import numpy as np

REGIONS = ["AF7", "AF8", "TP9", "TP10"]
WIN_S = 2.0


# ---------------------------------------------------------------- loaders
def read_edf(path):
    data = open(path, "rb").read()
    hdr = int(data[184:192])
    nrec = int(data[236:244])
    dur = float(data[244:252])
    ns = int(data[252:256])

    def field(off, w):
        return [data[off + i * w: off + (i + 1) * w].decode("ascii").strip() for i in range(ns)]

    off = 256
    labels = field(off, 16); off += 16 * ns
    off += 80 * ns + 8 * ns
    pmin = np.array(field(off, 8), float); off += 8 * ns
    pmax = np.array(field(off, 8), float); off += 8 * ns
    dmin = np.array(field(off, 8), float); off += 8 * ns
    dmax = np.array(field(off, 8), float); off += 8 * ns
    off += 80 * ns
    nsamp = np.array(field(off, 8), int)
    body = np.frombuffer(data[hdr:], dtype="<i2")
    per = nsamp.sum()
    nrec = min(nrec, len(body) // per)
    body = body[: nrec * per].reshape(nrec, per)
    gain = (pmax - pmin) / (dmax - dmin)
    sig, start = {}, 0
    for i, lab in enumerate(labels):
        raw = body[:, start:start + nsamp[i]].reshape(-1).astype(float)
        sig[lab.replace(".", "").upper()] = (raw - dmin[i]) * gain[i] + pmin[i]
        start += nsamp[i]
    return sig, nsamp[0] / dur


def pick(sig, cands):
    for c in cands:
        if c in sig:
            return sig[c], c
    raise KeyError(cands)


# Muse layout proxies. Muse reference is Fpz, so re-reference to Fpz where present.
PHYSIONET_MAP = {"AF7": ["AF7", "FP1"], "AF8": ["AF8", "FP2"],
                 "TP9": ["T9", "TP7", "T7"], "TP10": ["T10", "TP8", "T8"]}


def load_physionet(d):
    out = []
    for f in sorted(glob.glob(os.path.join(d, "S*R01.edf"))):
        g = f.replace("R01.edf", "R02.edf")
        if not os.path.exists(g):
            continue
        runs = []
        for path in (f, g):
            sig, fs = read_edf(path)
            ref = sig.get("FPZ", 0.0)
            chans = [pick(sig, PHYSIONET_MAP[r])[0] - ref for r in REGIONS]
            runs.append(np.array(chans))
        out.append((os.path.basename(f)[:4], runs[0], runs[1], fs))
    return out


def load_gipsa(d):
    from scipy.io import loadmat
    # 16 ch order per dataset doc: FP1 FP2 FC5 FC6 FZ T7 CZ T8 P7 P3 PZ P4 P8 O1 Oz O2
    idx = {"AF7": 1, "AF8": 2, "TP9": 9, "TP10": 13}  # FP1 FP2 P7 P8 (col 0 = time)
    out = []
    for f in sorted(glob.glob(os.path.join(d, "subject_*.mat"))):
        try:
            m = loadmat(f)
        except Exception:  # noqa: BLE001  (partial download)
            continue
        key = [k for k in m if not k.startswith("__")][0]
        S = np.asarray(m[key], float)
        fs = 512.0
        x = np.array([S[:, idx[r]] for r in REGIONS])
        closed_trig, open_trig = S[:, 17], S[:, 18]
        segs = {"closed": [], "open": []}
        for name, trig in (("closed", closed_trig), ("open", open_trig)):
            for t in np.flatnonzero(trig > 0):
                a, b = t, min(t + int(10 * fs), S.shape[0])  # 10 s blocks
                segs[name].append(x[:, a:b])
        if not segs["closed"] or not segs["open"]:
            continue
        out.append((os.path.basename(f)[:-4], np.concatenate(segs["open"], 1),
                    np.concatenate(segs["closed"], 1), fs))
    return out


def load_heroic(d):
    """Real Muse 2 recordings (HEROIC, zenodo 11493897). EEG.csv: timestamps,TP9,AF7,AF8,TP10,marker.
    In the eyes_closed_eyes_open task a marker fires every ~1 s: 3 = closed, 4 = open.
    A sample belongs to the state of the last 3/4 marker if that marker is < 1.1 s old.
    One entry per session (subject/setting/session)."""
    out = []
    for f in sorted(glob.glob(os.path.join(d, "sub*", "*", "*", "EEG.csv"))):
        try:
            A = np.genfromtxt(f, delimiter=",", skip_header=1)
        except Exception:  # noqa: BLE001
            continue
        if A.ndim != 2 or A.shape[1] < 6:
            continue
        ts, mk = A[:, 0], A[:, 5]
        fs = 256.0
        lab = np.zeros(len(A), int)
        last_t, last_m = -1e9, 0
        for i in range(len(A)):
            if mk[i] in (3, 4):
                last_t, last_m = ts[i], int(mk[i])
            elif mk[i] != 0 and mk[i] not in (3, 4):
                last_m = 0 if mk[i] in (85, 73, 84) else last_m
            if last_m and ts[i] - last_t < 1.1:
                lab[i] = last_m
        x = A[:, [2, 3, 1, 4]].T  # -> AF7, AF8, TP9, TP10
        xo, xc = x[:, lab == 4], x[:, lab == 3]
        if xo.shape[1] < 20 * fs or xc.shape[1] < 20 * fs:
            continue
        parts = f.split(os.sep)
        out.append((f"{parts[-4]}/{parts[-3]}/{parts[-2][:15]}", xo, xc, fs))
    return out


# ---------------------------------------------------------------- DSP
def welch_windows(x, fs):
    """x: (4, N). returns freqs, (W, 4, F) PSD per 2 s window (1 s Hann segments, 50 %),
    and (W, 4) bad mask: window peak-to-peak > 4x the channel's median (blink, motion, contact)."""
    seg = int(fs)
    hop = seg // 2
    win = int(WIN_S * fs)
    h = np.hanning(seg)
    freqs = np.fft.rfftfreq(seg, 1 / fs)
    out, ptp = [], []
    for w0 in range(0, x.shape[1] - win + 1, win // 2):
        xs = x[:, w0:w0 + win]
        xs = xs - xs.mean(1, keepdims=True)
        ps = [np.abs(np.fft.rfft(xs[:, s:s + seg] * h, axis=1)) ** 2
              for s in range(0, win - seg + 1, hop)]
        out.append(np.mean(ps, 0))
        ptp.append(np.ptp(xs, 1))
    ptp = np.array(ptp)
    bad = ptp > 4 * np.median(ptp, 0, keepdims=True)
    return freqs, np.array(out), bad


def robust_fit(freqs, p):
    """log10 P = b + m log10 f over 3-40 Hz; drop points above fit + 1 sd, twice."""
    sel = (freqs >= 3) & (freqs <= 40)
    lx, ly = np.log10(freqs[sel]), np.log10(p[sel] + 1e-12)
    keep = np.ones_like(lx, bool)
    for _ in range(3):
        m, b = np.polyfit(lx[keep], ly[keep], 1)
        r = ly - (b + m * lx)
        keep = r < r[keep].std() + 1e-9 if keep.sum() > 6 else keep
    return m, b


def alpha_prominence(freqs, p):
    m, b = robust_fit(freqs, p)
    a = (freqs >= 8) & (freqs <= 12)
    resid = np.log10(p[a] + 1e-12) - (b + m * np.log10(freqs[a]))
    return float(np.mean(10 ** resid))


SCORES = REGIONS + ["AVG", "EARS"]


def alpha_scores(freqs, P, bad):
    """P: (W, 4, F). returns (W, 6): 4 regions, averaged spectrum (today's engine), and EARS
    (TP9+TP10 spectra averaged). Rejected channel-windows are NaN; AVG keeps every window
    as today's engine does (no rejection) so the baseline stays the baseline."""
    W = P.shape[0]
    s = np.full((W, 6), np.nan)
    for w in range(W):
        for c in range(4):
            if not bad[w, c]:
                s[w, c] = alpha_prominence(freqs, P[w, c])
        s[w, 4] = alpha_prominence(freqs, P[w].mean(0))
        good = [c for c in (2, 3) if not bad[w, c]]
        if good:
            s[w, 5] = alpha_prominence(freqs, P[w, good].mean(0))
    return s


def phase_sync(x, fs):
    """alpha (8-12 Hz) wPLI and PLV for pairs (AF7,AF8), (TP9,TP10) over the run."""
    seg = int(fs)
    h = np.hanning(seg)
    freqs = np.fft.rfftfreq(seg, 1 / fs)
    a = (freqs >= 8) & (freqs <= 12)
    res = {}
    for name, (i, j) in {"front": (0, 1), "ear": (2, 3)}.items():
        ims, phs = [], []
        for s in range(0, x.shape[1] - seg + 1, seg // 2):
            X = np.fft.rfft((x[i, s:s + seg] - x[i, s:s + seg].mean()) * h)[a]
            Y = np.fft.rfft((x[j, s:s + seg] - x[j, s:s + seg].mean()) * h)[a]
            c = X * np.conj(Y)
            ims.append(c.imag)
            phs.append(c / (np.abs(c) + 1e-12))
        ims, phs = np.array(ims), np.array(phs)
        wpli = np.abs(ims.mean(0)) / (np.abs(ims).mean(0) + 1e-12)
        plv = np.abs(phs.mean(0))
        res[name] = (float(wpli.mean()), float(plv.mean()))
    return res


def cohen_d(a, b):
    a, b = a[~np.isnan(a)], b[~np.isnan(b)]
    if len(a) < 3 or len(b) < 3:
        return np.nan
    sp = np.sqrt((a.var(ddof=1) + b.var(ddof=1)) / 2) + 1e-12
    return (a.mean() - b.mean()) / sp


# ---------------------------------------------------------------- per subject
def evaluate(name, x_open, x_closed, fs):
    f, Po, bo = welch_windows(x_open, fs)
    _, Pc, bc = welch_windows(x_closed, fs)
    so, sc = alpha_scores(f, Po, bo), alpha_scores(f, Pc, bc)
    ratio = np.nanmean(sc, 0) / np.nanmean(so, 0)
    ho, hc = len(so) // 2, len(sc) // 2
    K = len(SCORES)
    d_cal = [cohen_d(sc[:hc, k], so[:ho, k]) for k in range(K)]
    d_test = [cohen_d(sc[hc:, k], so[ho:, k]) for k in range(K)]
    best = int(np.nanargmax(d_cal[:4]))

    def acc(k):
        thr = (np.nanmean(sc[:hc, k]) + np.nanmean(so[:ho, k])) / 2
        c, o = sc[hc:, k], so[ho:, k]
        c, o = c[~np.isnan(c)], o[~np.isnan(o)]
        return ((c > thr).sum() + (o <= thr).sum()) / max(1, len(c) + len(o))

    ps_o, ps_c = phase_sync(x_open, fs), phase_sync(x_closed, fs)
    r4 = np.nan_to_num(ratio[:4], nan=0)
    return {
        "subject": name,
        "ratio": dict(zip(SCORES, np.round(ratio, 3).tolist())),
        "rejected_pct": round(100 * float(np.concatenate([bo, bc]).mean()), 1),
        "best_region": REGIONS[best],
        "d_test": dict(zip(SCORES, np.round(d_test, 3).tolist())),
        "d_best_test": round(float(d_test[best]), 3),
        "d_avg_test": round(float(d_test[4]), 3),
        "d_ears_test": round(float(d_test[5]), 3),
        "acc_best": round(float(acc(best)), 3),
        "acc_avg": round(float(acc(4)), 3),
        "acc_ears": round(float(acc(5)), 3),
        "M1": bool(max(r4.max(), np.nan_to_num(ratio[5])) > 1.5),
        "M1_avg": bool(ratio[4] > 1.5),
        "M2": bool(d_test[best] > d_test[4]),
        "M2_ears": bool(np.nan_to_num(d_test[5], nan=-9) > d_test[4]),
        "sync": {"open": ps_o, "closed": ps_c},
    }


def resample_256(x, fs):
    if abs(fs - 256) < 1e-6:
        return x
    from scipy.signal import resample_poly
    from fractions import Fraction
    fr = Fraction(256, int(round(fs))).limit_denominator(64)
    return resample_poly(x, fr.numerator, fr.denominator, axis=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dataset", choices=["physionet", "gipsa", "heroic"])
    ap.add_argument("dir")
    ap.add_argument("--csv-out", help="also write 256 Hz 4-col CSVs for the C++ selftest")
    ap.add_argument("--json", help="write per-subject results here")
    ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()
    subjects = {"physionet": load_physionet, "gipsa": load_gipsa, "heroic": load_heroic}[a.dataset](a.dir)
    if a.limit:
        subjects = subjects[: a.limit]
    rows = []
    for name, xo, xc, fs in subjects:
        try:
            r = evaluate(name, xo, xc, fs)
        except Exception as e:  # noqa: BLE001
            print(f"{name}: skipped ({e})", file=sys.stderr)
            continue
        rows.append(r)
        if a.csv_out:
            os.makedirs(a.csv_out, exist_ok=True)
            for tag, x in (("open", xo), ("closed", xc)):
                np.savetxt(os.path.join(a.csv_out, f"{name}_{tag}.csv"),
                           resample_256(x, fs).T, fmt="%.4f", delimiter=",")
        rr = r["ratio"]
        print(f"{name}  " + " ".join(f"{k}x{v:4.2f}" for k, v in rr.items())
              + f"  best={r['best_region']:4s} d best/ears/avg={r['d_best_test']:5.2f}/{r['d_ears_test']:5.2f}/{r['d_avg_test']:5.2f}"
              + f"  acc={r['acc_best']:.2f}/{r['acc_ears']:.2f}/{r['acc_avg']:.2f} rej={r['rejected_pct']}%")
    n = len(rows)
    if not n:
        return
    def pct(k):
        return 100.0 * sum(r[k] for r in rows) / n
    sync = {p: {s: np.median([r["sync"][s][p][0] for r in rows]) for s in ("open", "closed")}
            for p in ("front", "ear")}
    plv = {p: {s: np.median([r["sync"][s][p][1] for r in rows]) for s in ("open", "closed")}
           for p in ("front", "ear")}
    best_counts = {k: sum(r["best_region"] == k for r in rows) for k in REGIONS}
    summary = {
        "dataset": a.dataset, "subjects": n,
        "M1_pct": round(pct("M1"), 1), "M1_avg_only_pct": round(pct("M1_avg"), 1),
        "M2_pct": round(pct("M2"), 1), "M2_ears_pct": round(pct("M2_ears"), 1),
        "median_d_ears": round(float(np.nanmedian([r["d_ears_test"] for r in rows])), 2),
        "median_acc_ears": round(float(np.median([r["acc_ears"] for r in rows])), 3),
        "median_rejected_pct": round(float(np.median([r["rejected_pct"] for r in rows])), 1),
        "median_d_best": round(float(np.nanmedian([r["d_best_test"] for r in rows])), 2),
        "median_d_avg": round(float(np.median([r["d_avg_test"] for r in rows])), 2),
        "median_acc_best": round(float(np.median([r["acc_best"] for r in rows])), 3),
        "median_acc_avg": round(float(np.median([r["acc_avg"] for r in rows])), 3),
        "median_ratio": {k: round(float(np.nanmedian([r["ratio"][k] for r in rows])), 2)
                         for k in SCORES},
        "best_region_counts": best_counts,
        "alpha_wpli_median": sync, "alpha_plv_median": plv,
    }
    print(json.dumps(summary, indent=1, default=float))
    if a.json:
        json.dump({"summary": summary, "subjects": rows}, open(a.json, "w"), indent=1, default=float)


if __name__ == "__main__":
    main()
