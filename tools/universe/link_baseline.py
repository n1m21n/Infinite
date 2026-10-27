#!/usr/bin/env python3
"""W5 link layer, stage (a): the count baseline (docs/plans/data-universe/next-phase.md section 8).
    link_baseline.py build      per-song role activity by section tenth (arcs/detail.jsonl.gz cc_motion rows) joined to genre label and
                                mood bins -> ~/Infinite/training-data/link/songs.jsonl
    link_baseline.py eval       held-out bits for: B0 role only, B1 + genre, B2 + genre + mood bin + tenth (count tables, smoothed)
Event = "role r is active in section tenth t of a song" (activity = share of that tenth's bars with notes). Bits are log2 loss on the
test songs (80/20 split by song hash); lower is better; the matrix link (stage b) must beat the best count model by >= 0.10 bits.
Statistics only."""
import argparse, collections, gzip, hashlib, json, math, os
import numpy as np

TD = os.path.expanduser("~/Infinite/training-data")
ROLES = ["bass", "drums", "keys", "leads", "pads", "plucks", "percussion", "textures", "foley"]


def tenths(bars):
    n = len(bars)
    if n < 10:
        return None
    return [float(np.mean(np.array(bars[int(i * n / 10):max(int((i + 1) * n / 10), int(i * n / 10) + 1)]) > 0)) for i in range(10)]


def build(a):
    mood = {}
    for ln in open(os.path.join(TD, "mood", "song_mood.jsonl")):
        r = json.loads(ln)
        mood[r["src"]] = r
    songs = {}
    n = 0
    for ln in gzip.open(os.path.join(TD, "arcs", "detail.jsonl.gz"), "rt"):
        if '"cc_motion"' not in ln[:200] and '"type":"cc_motion"' not in ln:
            continue
        r = json.loads(ln)
        if r.get("type") != "cc_motion" or r.get("role") not in ROLES or not r.get("bars"):
            continue
        t = tenths(r["bars"])
        if t is None:
            continue
        song = r["src"].split("#")[0]
        s = songs.setdefault(song, {"src": song, "family": r.get("family"), "act": {}})
        cur = s["act"].get(r["role"])
        s["act"][r["role"]] = [round(max(x, y), 3) for x, y in zip(cur, t)] if cur else [round(x, 3) for x in t]  # any channel of the role
        n += 1
        if n % 200000 == 0:
            print("%d rows, %d songs" % (n, len(songs)), flush=True)
    os.makedirs(os.path.join(TD, "link"), exist_ok=True)
    k = lab = 0
    with open(os.path.join(TD, "link", "songs.jsonl"), "w") as fo:
        for s in songs.values():
            m = mood.get(s["src"], {})
            s["mood"] = {ax: m.get(ax + "_pct") for ax in ("valence", "energy", "danceability")}
            fo.write(json.dumps(s, separators=(",", ":")) + "\n")
            k += 1
            lab += s["family"] not in (None, "unlabeled")
    print("%d songs (%d with a genre label) from %d role rows" % (k, lab, n))


def bin3(v):
    return None if v is None else min(2, int(v * 3))


def evaluate(a):
    songs = [json.loads(l) for l in open(os.path.join(TD, "link", "songs.jsonl"))]
    songs = [s for s in songs if s["family"] not in (None, "unlabeled")]
    tr = [s for s in songs if int(hashlib.md5(s["src"].encode()).hexdigest(), 16) % 5 != 0]
    te = [s for s in songs if int(hashlib.md5(s["src"].encode()).hexdigest(), 16) % 5 == 0]
    print("labelled songs: %d train / %d test; genres %d" % (len(tr), len(te), len({s["family"] for s in songs})))

    def events(S):
        for s in S:
            g = s["family"]
            mb = (bin3(s["mood"]["valence"]), bin3(s["mood"]["energy"]))
            for r, v in s["act"].items():
                for t, x in enumerate(v):
                    yield g, mb, r, t, 1 if x >= 0.5 else 0
    tabs = [collections.defaultdict(lambda: [0, 0]) for _ in range(3)]
    keys = [lambda g, mb, r, t: (r,), lambda g, mb, r, t: (r, g), lambda g, mb, r, t: (r, g, mb, t)]
    for g, mb, r, t, y in events(tr):
        for i in range(3):
            tabs[i][keys[i](g, mb, r, t)][y] += 1
    # back-off: B2 smooths toward B1, B1 toward B0 (additive prior of 4 pseudo-counts at the parent's rate)
    def p(i, g, mb, r, t):
        c = tabs[i].get(keys[i](g, mb, r, t), [0, 0])
        par = 0.5 if i == 0 else p(i - 1, g, mb, r, t)
        return (c[1] + 4 * par) / (c[0] + c[1] + 4)
    tot = [0.0] * 3
    n = 0
    for g, mb, r, t, y in events(te):
        for i in range(3):
            q = min(max(p(i, g, mb, r, t), 1e-4), 1 - 1e-4)
            tot[i] += -math.log2(q if y else 1 - q)
        n += 1
    names = ["B0 role only", "B1 + genre", "B2 + genre + mood + tenth"]
    print("held-out events: %d" % n)
    for i in range(3):
        print("%-28s %.4f bits/event" % (names[i], tot[i] / n))
    print("B2 vs B1: %+.4f bits; B1 vs B0: %+.4f bits" % ((tot[1] - tot[2]) / n, (tot[0] - tot[1]) / n))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("build", "eval"))
    a = ap.parse_args()
    build(a) if a.cmd == "build" else evaluate(a)
