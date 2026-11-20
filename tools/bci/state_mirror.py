#!/usr/bin/env python3
"""Python mirror of BciDspEngine's calibrated eyes-closed state, for the C++ <-> Python check.

Same steps as the engine: 1 s Hann periodogram every 12 samples, 16 features
(log10 band / 2-40 Hz total for 4-8, 8-10, 10-12, 12-30 Hz on AF7 AF8 TP9 TP10),
EMA tau 3 s, calibration = first 30 s of OPEN then first 30 s of CLOSED (first 4 s of each
not used), shrinkage LDA (0.3). Test = rest of OPEN then rest of CLOSED, each scored after a
6 s settle; accuracy = share of updates on the right side of 0.5.
Not mirrored: DC high-pass, notches and artifact blanking (the engine has them).

usage: state_mirror.py OPEN.csv CLOSED.csv  (4-col 256 Hz CSVs from batch_validate.py --csv-out)
       state_mirror.py --heroic-csv DIR OUT_DIR   write per-session open/closed CSVs from HEROIC
"""
import sys

import numpy as np

FS, HOP, N = 256, 12, 256
TAU, CAL, SETTLE, TEST_SETTLE = 3.0, 30.0, 4.0, 6.0
LO, HI = (4, 8, 10, 12), (8, 10, 12, 30)


class Engine:
    def __init__(self):
        self.ring = np.zeros((4, N))
        self.head = 0
        self.since = 0
        self.ema = None
        self.w = None
        self.hann = 0.5 * (1 - np.cos(2 * np.pi * np.arange(N) / (N - 1)))
        self.f = np.arange(N // 2 + 1) * FS / N

    def feats(self):
        x = np.roll(self.ring, -self.head, axis=1) * self.hann
        p = np.abs(np.fft.rfft(x, axis=1)) ** 2
        tot = p[:, (self.f >= 2) & (self.f <= 40)].sum(1)
        return np.concatenate([np.log10(np.maximum(p[:, (self.f >= lo) & (self.f < hi)].sum(1), 1e-12) /
                                        np.maximum(tot, 1e-12)) for lo, hi in zip(LO, HI)])

    def run(self, x, on_update):
        """x: (4, n). calls on_update(t_sec_in_segment, ema) at each PSD update."""
        a = 1 - np.exp(-(HOP / FS) / TAU)
        for i in range(x.shape[1]):
            self.ring[:, self.head] = x[:, i]
            self.head = (self.head + 1) % N
            self.since += 1
            if self.since >= HOP:
                self.since = 0
                f = self.feats()
                self.ema = f if self.ema is None else self.ema + a * (f - self.ema)
                on_update((i + 1) / FS, self.ema.copy())

    def score(self, v):
        z = (v @ self.w - self.bias) / self.scale
        return 1 / (1 + np.exp(-np.clip(z, -20, 20)))


def fit(open_v, closed_v, shrink=0.3):
    X1, X0 = np.array(closed_v), np.array(open_v)
    mu1, mu0 = X1.mean(0), X0.mean(0)
    S = (np.vstack([X1 - mu1, X0 - mu0]).T @ np.vstack([X1 - mu1, X0 - mu0])) / (len(X1) + len(X0) - 1)
    S = (1 - shrink) * S + shrink * np.trace(S) / len(S) * np.eye(len(S))
    w = np.linalg.solve(S, mu1 - mu0)
    return w, w @ (mu1 + mu0) / 2, w @ (mu1 - mu0) / 4


def evaluate(xo, xc):
    cal = int(CAL * FS)
    e = Engine()
    ov, cv = [], []
    e.run(xo[:, :cal], lambda t, v: ov.append(v) if t >= SETTLE else None)
    e.run(xc[:, :cal], lambda t, v: cv.append(v) if t >= SETTLE else None)
    e.w, e.bias, e.scale = fit(ov, cv)
    hits = []
    e.run(xo[:, cal:], lambda t, v: hits.append(e.score(v) < 0.5) if t >= TEST_SETTLE else None)
    e.run(xc[:, cal:], lambda t, v: hits.append(e.score(v) > 0.5) if t >= TEST_SETTLE else None)
    return float(np.mean(hits)), len(hits)


def main():
    if sys.argv[1] == "--heroic-csv":
        sys.path.insert(0, __file__.rsplit("/", 1)[0])
        import os
        import batch_validate as bv
        os.makedirs(sys.argv[3], exist_ok=True)
        for name, xo, xc, fs in bv.load_heroic(sys.argv[2]):
            tag = name.replace("/", "_")
            for k, x in (("open", xo), ("closed", xc)):
                np.savetxt(os.path.join(sys.argv[3], f"{tag}_{k}.csv"), x.T, fmt="%.3f", delimiter=",")
        return
    xo = np.loadtxt(sys.argv[1], delimiter=",").T[:4]
    xc = np.loadtxt(sys.argv[2], delimiter=",").T[:4]
    acc, n = evaluate(xo, xc)
    print(f"STATE_MIRROR acc={acc:.3f} updates={n}")


if __name__ == "__main__":
    main()
