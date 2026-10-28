#!/usr/bin/env python3
"""P4 dry run of the sound-model handoff: do sources land in the same characteristic space? Reads characteristics.py outputs.

    p4_shared.py diva_v1=preset_diva_v1.jsonl samples=features.jsonl ... [--n 3000] [--axes loudness,brightness,...]

Per axis: median and 10-90% range per source, so a scale offset between sources shows. Coverage table: for a row of source A,
its nearest neighbour in source B (axes z-scored on all sources pooled), compared with A's own nearest-neighbour distance inside A;
"covered" when the cross distance is within A's median inside distance. Row = A, column = B. Only rows of the same form (bands)
should be compared until P4 calibrates the mel / bands offset (CHARACTERISTICS.md)."""
import argparse, json, os
import numpy as np

AXES = ["loudness", "brightness", "sharpness", "warmth", "weight", "fullness", "noisiness"]


def load(path, axes, n, rng):
    rows = []
    for ln in open(os.path.expanduser(path)):
        ax = json.loads(ln)["axes"]
        v = [ax.get(a, {}).get("mean", np.nan) for a in axes]
        if all(np.isfinite(v)):
            rows.append(v)
    A = np.array(rows)
    return A[rng.choice(len(A), min(n, len(A)), False)]


def nn(Q, R, same=False):
    out = np.empty(len(Q))
    for s in range(0, len(Q), 500):
        D = ((Q[s:s + 500, None, :] - R[None]) ** 2).sum(-1)
        if same:
            D[np.arange(D.shape[0]), np.arange(s, s + D.shape[0])] = np.inf
        out[s:s + 500] = np.sqrt(D.min(1))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sources", nargs="+"); ap.add_argument("--n", type=int, default=3000); ap.add_argument("--axes", default=",".join(AXES))
    a = ap.parse_args(); axes = a.axes.split(","); rng = np.random.default_rng(0)
    S = {s.split("=")[0]: load(s.split("=")[1], axes, a.n, rng) for s in a.sources}
    print("rows: " + ", ".join("%s %d" % (k, len(v)) for k, v in S.items()))
    print("\nmedian [10%%, 90%%] per axis\n%-11s" % "axis" + "".join("%26s" % k for k in S))
    for j, ax in enumerate(axes):
        print("%-11s" % ax + "".join("%26s" % ("%.2f [%.2f, %.2f]" % tuple(np.percentile(v[:, j], [50, 10, 90]))) for v in S.values()))
    P = np.vstack(list(S.values())); mu, sd = P.mean(0), P.std(0) + 1e-9
    Z = {k: (v - mu) / sd for k, v in S.items()}
    print("\ncoverage: share of A rows with a B neighbour within A's median inside distance (row A, column B)\n%-11s" % "A \\ B"
          + "".join("%12s" % k for k in S))
    for ka, A in Z.items():
        r = np.median(nn(A, A, same=True))
        print("%-11s" % ka + "".join("%12.2f" % (nn(A, B, same=ka == kb) <= r).mean() for kb, B in Z.items()))


if __name__ == "__main__":
    main()
