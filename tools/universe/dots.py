#!/usr/bin/env python3
"""The dots engine (reference): four dots (brightness, sharpness, warmth, fullness) each in [-1, 1] -> Analog knob settings.
Built from knob_direction pairs: for each dot axis the trusted knob (consistency >= 0.8, >= 8 samples, not noise/drive/fm/sub) with the least
noisiness coupling; a dot of d moves that knob by d / slope in knob units u (0-1), clipped to [0.02, 0.98]. Validated by dot_test.py (fresh voices:
brightness 100%, sharpness 100%, warmth 91%, fullness 91%, noisiness held 94%). Movement is NOT a dot (11% on fresh voices).
    dots.py table            write dots_table.json (axis -> knob, slope, consistency)
    dots.py apply KEYS.json D1 D2 D3 D4    print the moved knob values for a voice (dict key -> value in knob units)"""
import json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import knob_direction as kd, own_sweeps as ow, p3_characteristics as p3
NODE = "Analog"; BAN = ("noise", "drive", "fm", "sub"); DOTS = ["brightness", "sharpness", "warmth", "fullness"]
TABLE = os.path.join(HERE, "dots_table.json")


def build(lam=0.3):
    """Mixing matrix M (knobs x dots): moving dot vector d changes knob units by M @ d. Ridge least squares on the measured slope matrix, so one dot
    moves its axis by ~1 spread while the other dots' axes and noisiness are asked to stay put. Only knobs with some reliable cell, never BAN knobs."""
    table, sw, fixed = ow.param_table(NODE); keys = [p["key"] for p in sw]; AX = [n[:-5] for n in p3.COLS if n.endswith(".mean")]
    P, nk = kd.pairs(NODE); A = len(AX); num = np.zeros((nk, A)); den = np.zeros(nk); up = np.zeros((nk, A)); n = np.zeros((nk, A))
    for g, j, dk, mv in P:
        num[j] += mv * dk; den[j] += dk * dk; m = np.abs(mv) >= 0.3; n[j] += m; up[j] += m * (np.sign(mv) * np.sign(dk) > 0)
    slope = num / np.maximum(den, 1e-9)[:, None]; cons = np.where(n >= 8, np.maximum(up, n - up) / np.maximum(n, 1), 0)
    rows = [j for j in range(nk) if keys[j] not in BAN and (cons[j] >= 0.8).any()]
    S = np.where(cons[rows] >= 0.8, slope[rows], 0.0)                      # knobs x axes: only reliable cells count
    idx = [AX.index(d) for d in DOTS]; nz = AX.index("noisiness"); ld = AX.index("loudness")
    T = np.zeros((A, len(DOTS)))
    for k, a in enumerate(idx): T[a, k] = 1.0                              # each dot: its own axis +1, every other axis 0 (incl. noisiness)
    W = np.ones(A); W[ld] = 0.1; W[nz] = 3.0                               # noisiness held hard; loudness is renormalised at export
    Wd = np.diag(W); G = S @ Wd @ S.T + lam * np.eye(len(rows)); M = np.linalg.solve(G, S @ Wd @ T)
    ach = S.T @ M                                                          # axes x dots: what each dot really does
    out = {"knobs": [keys[j] for j in rows], "M": M.tolist(), "achieved": {AX[a]: [round(float(x), 2) for x in ach[a]] for a in idx + [nz, ld]}}
    json.dump(out, open(TABLE, "w"), indent=1); return out


def apply(u, dots, table=None):
    """u: dict knob key -> u in 0-1 (the voice); dots: dict dot -> value in [-1,1]; returns the moved copy."""
    t = table or json.load(open(TABLE)); d = np.array([dots.get(x, 0.0) for x in DOTS]); dv = np.array(t["M"]) @ d; out = dict(u)
    for key, x in zip(t["knobs"], dv):
        if key in out: out[key] = float(np.clip(out[key] + x, 0.02, 0.98))
    return out


if __name__ == "__main__":
    if sys.argv[1:2] == ["table"]: print(json.dumps(build(), indent=1))
    else: print(__doc__)
