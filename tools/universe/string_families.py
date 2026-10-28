#!/usr/bin/env python3
"""Step 7: String vs the library's acoustic sounds. Per family (names only): distance of each library sample to the nearest String render
on the 11 shared axes (sound_space.AX, z-scored on the pooled rows), against the family's own inner distance (nearest other sample of
the same family). Ratio < 1 means the String node already lands inside the family; > 1 means the family has regions the node misses.
    string_families.py [--node String]"""
import argparse, os, re, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import sound_space as ss  # noqa: E402

FAM = {"guitar": r"guitar|gtr", "harp": r"harp", "pluck": r"pluck", "violin": r"violin|vln", "cello": r"cello|vcl", "piano": r"piano|pno|keys"}


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", default="String"); a = ap.parse_args()
    S, _, _ = ss.own_matrix(os.path.join(ss.AUDIO, "own_%s.jsonl" % a.node.lower()))
    X, N = ss.sample_matrix(None, 200000)
    ok = np.isfinite(S).all(1); S = S[ok]
    z = ss.prep(X, S); Sz, Xz = z(S), z(X)
    print("| family | samples | nearest String render (median z) | inner distance (median z) | ratio | share within inner |\n|---|---|---|---|---|---|")
    for fam, pat in FAM.items():
        idx = [i for i, n in enumerate(N) if re.search(pat, n.lower())]
        if len(idx) < 20: print("| %s | %d | too few | | | |" % (fam, len(idx))); continue
        F = Xz[idx]; d = ss.nn_dist(F, Sz)
        inner = np.array([np.sort(((F[i] - F) ** 2).sum(1) ** .5)[1] for i in range(len(F))])
        print("| %s | %d | %.2f | %.2f | %.2f | %.0f%% |" % (fam, len(idx), np.median(d), np.median(inner), np.median(d) / np.median(inner), 100 * (d <= inner).mean()))


if __name__ == "__main__":
    main()
