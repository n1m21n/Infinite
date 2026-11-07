#!/usr/bin/env python3
"""Step 7: String vs the library's acoustic sounds. Per family (names only): distance of each library sample to the nearest String render
(or Wind with --set wind) on the 11 shared axes (sound_space.AX, z-scored on the pooled rows), against the family's own inner distance (nearest other sample of
the same family). Ratio < 1 means the String node already lands inside the family; > 1 means the family has regions the node misses.
    string_families.py [--node String] [--set strings|wind] [--notes C3,C4,C1,C5,C6]   (default C3: the renders' other pitches
    count only when named; a library sample at any pitch then meets renders at more than one pitch)
    --axes timbre: the 8 axes a single held note can match. Loudness is a gain level (the sweeps render at a fixed volume, the
    library is mastered), movement is missing on every library row (it read as a fixed gap of about 1.2 z on every family), and
    density counts notes per second (the library holds phrases, a sweep holds one note). --axes all keeps the 11."""
import argparse, os, re, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import sound_space as ss  # noqa: E402

FAM = {"guitar": r"guitar|gtr", "harp": r"harp", "pluck": r"pluck", "violin": r"violin|vln", "cello": r"cello|vcl", "piano": r"piano|pno|keys"}
WIND = {"clarinet": r"clarinet|clar\b", "sax": r"sax", "flute": r"flute|piccolo|fife", "brass": r"brass|trumpet|trombone|horn|tuba|cornet|flugel"}


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", default="String"); ap.add_argument("--n", type=int, default=0, help="subsample renders to n (equal-size comparison)")
    ap.add_argument("--set", default="strings", choices=["strings", "wind"]); ap.add_argument("--notes", default="C3")
    ap.add_argument("--axes", default="all", choices=["all", "timbre"])
    a = ap.parse_args()
    S, _, _ = ss.own_matrix(os.path.join(ss.AUDIO, "own_%s.jsonl" % a.node.lower()), notes=tuple(a.notes.split(",")))
    X, N = ss.sample_matrix(None, 200000)
    ok = np.isfinite(S).all(1); S = S[ok]
    if a.axes == "timbre":
        keep = [i for i, k in enumerate(ss.AX) if k not in ("loudness", "movement", "density")]; S, X = S[:, keep], X[:, keep]
    if a.n and len(S) > a.n: S = S[np.random.default_rng(0).choice(len(S), a.n, replace=False)]
    print('%s: %d renders, %s axes' % (a.node, len(S), a.axes))
    z = ss.prep(X, S); Sz, Xz = z(S), z(X)
    print("| family | samples | nearest render (median z) | inner distance (median z) | ratio | share within inner |\n|---|---|---|---|---|---|")
    for fam, pat in (WIND if a.set == "wind" else FAM).items():
        idx = [i for i, n in enumerate(N) if re.search(pat, n.lower())]
        if len(idx) < 20: print("| %s | %d | too few | | | |" % (fam, len(idx))); continue
        F = Xz[idx]; d = ss.nn_dist(F, Sz)
        inner = np.array([np.sort(((F[i] - F) ** 2).sum(1) ** .5)[1] for i in range(len(F))])
        print("| %s | %d | %.2f | %.2f | %.2f | %.0f%% |" % (fam, len(idx), np.median(d), np.median(inner), np.median(d) / np.median(inner), 100 * (d <= inner).mean()))


if __name__ == "__main__":
    main()
