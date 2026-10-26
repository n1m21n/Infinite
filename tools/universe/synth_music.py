#!/usr/bin/env python3
"""Synthetic music pieces from theory rules, every one labelled and gated.
    synth_music.py [--per-family 300] [--seed 1] [--store ~/Infinite/training-data/store]
Drum grooves: a skeleton per genre (where the kick, backbeat and hats sit) plus seeded variation (drops, ghost
notes, fills). Bass figures: a rhythm template with chord-tone degrees. Melodies: a scale walk with a contour arc,
stepwise motion, leaps that resolve, and a cadence. Every piece carries synthetic=true so training can weigh it and
the held-out test can leave it out: synthetic data teaches the model our theory, it must never be the judge."""
import argparse, collections, json, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pieces as P
import expression_pieces as E

# lane order: kick snare chat ohat clap tom htom bell; each lane is (always-on steps, maybe steps with prob)
Z = {
    "house":        {"kick": ([0, 4, 8, 12], []), "clap": ([4, 12], []), "ohat": ([2, 6, 10, 14], []), "chat": ([], [(i, .35) for i in range(16) if i % 2]), "snare": ([], [(15, .1)])},
    "techno":       {"kick": ([0, 4, 8, 12], []), "clap": ([4, 12], [(7, .15)]), "ohat": ([2, 6, 10, 14], [(i, .2) for i in (3, 11)]), "chat": ([], [(i, .5) for i in range(16)]), "tom": ([], [(i, .12) for i in (10, 11, 14)])},
    "trance":       {"kick": ([0, 4, 8, 12], []), "clap": ([4, 12], []), "ohat": ([2, 6, 10, 14], []), "chat": ([], [(i, .6) for i in range(1, 16, 2)])},
    "drum_and_bass": {"kick": ([0], [(10, .8), (2, .2)]), "snare": ([4, 12], [(15, .25), (7, .15)]), "chat": ([], [(i, .7) for i in range(0, 16, 2)]), "ohat": ([], [(14, .3)])},
    "breaks":       {"kick": ([0], [(6, .6), (10, .8), (3, .2), (14, .2)]), "snare": ([4, 12], [(7, .2), (15, .3), (9, .15)]), "chat": ([], [(i, .55) for i in range(0, 16, 2)])},
    "hiphop":       {"kick": ([0], [(7, .5), (10, .6), (14, .25)]), "snare": ([4, 12], []), "chat": ([], [(i, .75) for i in range(0, 16, 2)]), "ohat": ([], [(15, .2)])},
    "bass_music":   {"kick": ([0], [(10, .5), (7, .3)]), "snare": ([8], [(4, .1), (12, .1)]), "chat": ([], [(i, .4) for i in range(16)]), "clap": ([], [(8, .3)])},
    "downtempo":    {"kick": ([0], [(7, .5), (10, .4)]), "snare": ([4, 12], []), "chat": ([], [(i, .6) for i in range(0, 16, 2)])},
    "ambient":      {"kick": ([], [(0, .4), (8, .2)]), "chat": ([], [(i, .12) for i in range(16)]), "bell": ([], [(i, .15) for i in range(16)]), "tom": ([], [(i, .08) for i in range(16)])},
    "retro":        {"kick": ([0, 8], [(6, .4), (10, .4)]), "snare": ([4, 12], []), "chat": ([], [(i, .9) for i in range(0, 16, 2)]), "clap": ([], [(12, .3)])},
    "experimental": {"kick": ([], [(i, .18) for i in range(16)]), "snare": ([], [(i, .15) for i in range(16)]), "chat": ([], [(i, .25) for i in range(16)]), "tom": ([], [(i, .15) for i in range(16)]), "bell": ([], [(i, .1) for i in range(16)])},
    "hard":         {"kick": ([0, 4, 8, 12], [(i, .25) for i in (3, 7, 11, 15)]), "clap": ([4, 12], []), "ohat": ([2, 6, 10, 14], []), "snare": ([], [(i, .12) for i in range(16)])},
}
LANES = ["kick", "snare", "chat", "ohat", "clap", "tom", "htom", "bell"]
SCALES = {"minor": [0, 2, 3, 5, 7, 8, 10], "major": [0, 2, 4, 5, 7, 9, 11], "dorian": [0, 2, 3, 5, 7, 9, 10],
          "phrygian": [0, 1, 3, 5, 7, 8, 10], "mixolydian": [0, 2, 4, 5, 7, 9, 10]}
FAM_SCALE = {"house": ["minor", "dorian"], "techno": ["minor", "phrygian"], "trance": ["minor"], "drum_and_bass": ["minor"], "breaks": ["minor", "dorian"],
             "hiphop": ["minor", "dorian"], "bass_music": ["minor", "phrygian"], "downtempo": ["dorian", "major", "minor"], "ambient": ["major", "dorian", "mixolydian"],
             "retro": ["minor", "major"], "experimental": list(SCALES), "hard": ["phrygian", "minor"]}
BASS_RHYTHMS = {"offbeat": [2, 6, 10, 14], "eighths": list(range(0, 16, 2)), "root_fifth": [0, 3, 6, 8, 11, 14], "pump": [0, 2, 4, 6, 8, 10, 12, 14],
                "sync": [0, 3, 6, 10, 12], "sparse": [0, 8], "gallop": [0, 1, 4, 5, 8, 9, 12, 13]}
FAM_BASS = {"house": ["offbeat", "root_fifth", "sync"], "techno": ["eighths", "offbeat", "sync"], "trance": ["offbeat", "pump"], "drum_and_bass": ["sync", "root_fifth"],
            "breaks": ["sync", "root_fifth"], "hiphop": ["sparse", "sync"], "bass_music": ["sync", "sparse"], "downtempo": ["sparse", "root_fifth"],
            "ambient": ["sparse"], "retro": ["eighths", "gallop"], "experimental": list(BASS_RHYTHMS), "hard": ["gallop", "eighths"]}


def groove(fam, rng):
    z = Z[fam]
    lanes = []
    for l in LANES:
        on, maybe = z.get(l, ([], []))
        hit = {s for s in on if rng.random() > 0.04} | {s for s, p in maybe if rng.random() < p}
        lanes.append("".join("x" if s in hit else "." for s in range(16)) if hit else "")
    return lanes


def bass(fam, rng):
    steps = [s for s in BASS_RHYTHMS[rng.choice(FAM_BASS[fam])] if rng.random() > 0.08]
    deg = {s: rng.choices([0, 0, 0, 2, 3, 4, 5, 6], k=1)[0] if s else 0 for s in steps}
    return "".join(str(deg[s]) if s in deg else "." for s in range(16))


def melody(fam, rng):
    scale = SCALES[rng.choice(FAM_SCALE[fam])]
    lo = rng.choice([40, 48, 60, 72])
    pos = rng.choice([0, 2, 4])  # scale-degree index of the start
    arc = rng.choice([1, -1, 0])  # rises, falls or arches
    out = []
    for i in range(16):
        if i == 15:
            pos += -pos % 7 if rng.random() < 0.7 else 0  # cadence: fall to the nearest tonic
        else:
            r = rng.random()
            lean = arc * (1 if i < 8 else -1) * 0.15
            step = (1 if rng.random() < 0.5 + lean else -1) * (1 if r < 0.62 else (2 if r < 0.85 else rng.choice([3, 4, -3])))
            if r > 0.9 and out:
                step = 1 if step < 0 else -1  # a leap resolves the other way next
            pos = max(-3, min(14, pos + step))
        out.append(lo + 12 * (pos // 7) + scale[pos % 7])
    return out


def pieces(per_family, seed):
    rng = random.Random(seed)
    for fam in Z:
        for i in range(per_family):
            yield {"type": "drum_groove", "src": "synth:%s#%d" % (fam, i), "family": fam, "lanes": groove(fam, rng), "swing": 0.0, "groove": None}
            yield {"type": "bass_seq", "src": "synth:%s#%d" % (fam, i), "family": fam, "steps": bass(fam, rng)}
            m = melody(fam, rng)
            yield {"type": "melody_contour", "src": "synth:%s#%d" % (fam, i), "family": fam, "role": "lead" if min(m) >= 60 else "bass",
                   "pitches": m, "steps": [b - a for a, b in zip(m, m[1:])]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-family", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--store", default="~/Infinite/training-data/store")
    a = ap.parse_args()
    gates = {"drum_groove": P.gate_drum, "bass_seq": P.gate_bass, "melody_contour": E.gate_contour}
    rows, seen = [], set()
    for p in pieces(a.per_family, a.seed):
        h, s = gates[p["type"]](p)
        key = P.digest({k: v for k, v in p.items() if k not in ("src", "family")})
        if not h and key in seen:
            h = ["duplicate of an earlier piece"]
        seen.add(key)
        p.update(id=key, hard=h, soft=s, ok=not h, license="owner", synthetic=True)
        rows.append(p)
    store = os.path.expanduser(a.store)
    os.makedirs(store, exist_ok=True)
    out = os.path.join(store, "synthetic.jsonl")
    with open(out, "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    print("%-16s %6s %6s  %s" % ("type", "made", "ok", "hard fails"))
    for t in gates:
        r = [x for x in rows if x["type"] == t]
        why = collections.Counter(x for q in r for x in q["hard"])
        print("%-16s %6d %6d  %s" % (t, len(r), sum(q["ok"] for q in r), "; ".join("%s x%d" % kv for kv in why.most_common(4)) or "-"))
    print("wrote", out)


if __name__ == "__main__":
    main()
