#!/usr/bin/env python3
"""Sound pairs for the kit compatibility model, from training-data/compat/sounds.jsonl (sound_features.py).
    pairs.py [--per-group 400] [--neg 1]       (imported by compat_model.py; run alone it prints the pair counts)
Positive: two sounds of different roles that come together: the same kit or pack, the same Ableton drum rack, the same set
or Bitwig project (one track's stems). Negative: the same role pair, the second sound swapped for a same-role sound from
another group, of another genre when both genres are known.
Pair features (role order fixed, A then B, e.g. kick then snare):
  d_<f>    signed difference on a log scale (pitch in semitones, times and Hz in log2, shares as they are) = the ratio and order
  a_<f>    its absolute value
  overlap  sum of min(bands A, bands B) over the 32 bands (spectral masking), plus low-band overlap (bands 0-10)
  interval pitch class distance 0-6 when both are pitched (else 0, with has_pitch 0)
  A_<f>, B_<f>  each sound's own scalars and its 8 coarse bands (4 of the 32 each): the model learns which values go together
Features the generator can predict for its own candidates only: loudness is kept in the sound vector but is not a pair
feature (every pack normalises its level differently, and the engine sets level on the mixer)."""
import collections, json, math, os, random
import numpy as np

TD = os.path.expanduser("~/Infinite/training-data")
SOUNDS = os.path.join(TD, "compat/sounds.jsonl")
ROLES = ["kick", "snare", "hat", "bass", "keys", "pad", "lead"]
# role pairs in the order the generator builds the kit: kick-snare-hat first, then bass, then pad / keys / lead against the drum bus
PAIRS = [("kick", "snare"), ("kick", "hat"), ("snare", "hat"), ("kick", "bass"), ("snare", "bass"), ("hat", "bass"),
         ("kick", "pad"), ("bass", "pad"), ("hat", "pad"), ("kick", "keys"), ("bass", "keys"), ("snare", "keys"),
         ("kick", "lead"), ("bass", "lead"), ("hat", "lead"), ("pad", "keys"), ("keys", "lead"), ("pad", "lead")]
SCAL = ["pitch", "attack", "decay", "centroid", "crest", "noise", "low", "mid", "high"]
OWN = SCAL + ["b%d" % i for i in range(8)]
NAMES = [p + f for f in SCAL for p in ("d_", "a_")] + ["overlap", "low_overlap", "interval", "has_pitch"] + \
    ["A_" + f for f in OWN] + ["B_" + f for f in OWN]


def vec(r):
    """sound -> the scalars on the scale the differences are taken in"""
    return {"pitch": r["pitch"], "attack": math.log2(1.0 + r["attack_ms"]), "decay": math.log2(5.0 + r["decay_ms"]),
            "centroid": math.log2(max(20.0, r["centroid_hz"])), "crest": r["crest_db"] / 6.0, "noise": math.log10(1e-4 + r["noise"]),
            "low": r["low"], "mid": r["mid"], "high": r["high"], "bands": np.asarray(r["bands"], float)}


def pair_features(a, b):
    out = []
    for f in SCAL:
        if f == "pitch":
            d = (a["pitch"] - b["pitch"]) / 12.0 if a["pitch"] is not None and b["pitch"] is not None else 0.0
        else:
            d = a[f] - b[f]
        out += [d, abs(d)]
    ov = float(np.minimum(a["bands"], b["bands"]).sum())
    lo = float(np.minimum(a["bands"][:11], b["bands"][:11]).sum())
    hp = a["pitch"] is not None and b["pitch"] is not None
    iv = 0.0
    if hp:
        k = int(round(a["pitch"] - b["pitch"])) % 12
        iv = min(k, 12 - k) / 6.0
    return out + [ov, lo, iv, 1.0 if hp else 0.0] + own(a) + own(b)


def own(v):
    b = v["bands"]
    return [v["pitch"] / 12.0 if v["pitch"] is not None else 0.0] + [v[f] for f in SCAL[1:]] + [float(b[4 * i:4 * i + 4].sum()) for i in range(8)]


def load(path=SOUNDS):
    rows = []
    for l in open(path):
        r = json.loads(l)
        if r["role"] in ROLES and r["decay_ms"] is not None:
            r["v"] = vec(r); rows.append(r)
    return rows


def build(rows, groups, per_group=400, neg=1, seed=1):
    """pairs for each role pair from the groups given -> {pair: (X, y, group of the positive)}"""
    rng = random.Random(seed)
    by = collections.defaultdict(lambda: collections.defaultdict(list))
    for r in rows:
        if r["group"] in groups:
            by[r["group"]][r["role"]].append(r)
    pool = collections.defaultdict(list)  # role -> sounds across the groups given
    for g, rr in by.items():
        for role, lst in rr.items():
            pool[role] += lst
    out = {}
    for A, B in PAIRS:
        X, y, G = [], [], []
        for g, rr in by.items():
            if not rr.get(A) or not rr.get(B):
                continue
            cand = [(a, b) for a in rr[A] for b in rr[B] if a["src"] != b["src"]]
            rng.shuffle(cand)
            for a, b in cand[:per_group]:
                X.append(pair_features(a["v"], b["v"])); y.append(1); G.append(g)
                for _ in range(neg):
                    for _try in range(20):
                        o = rng.choice(pool[B])
                        if o["group"] == g:
                            continue
                        if a["genre"] and o["genre"] and a["genre"] == o["genre"] and _try < 19:
                            continue
                        break
                    else:
                        continue
                    X.append(pair_features(a["v"], o["v"])); y.append(0); G.append(g)
        if X:
            out[(A, B)] = (np.asarray(X, float), np.asarray(y, int), np.asarray(G))
    return out


if __name__ == "__main__":
    rows = load()
    gs = {r["group"] for r in rows}
    P = build(rows, gs)
    print("%d sounds, %d groups" % (len(rows), len(gs)))
    for k, (X, y, G) in P.items():
        print("%-12s n=%6d pos=%6d groups=%d" % ("-".join(k), len(y), int(y.sum()), len(set(G))))
