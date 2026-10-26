#!/usr/bin/env python3
"""Chord progressions from the file names of datasets/midi/data/chord_progressions: `<key> - <numerals> - <mood words>.mid`.
    chord_pieces.py [--root DIR] [--store DIR]
A piece is the progression as scale degrees (transposition-free), its mode folder (Major / Minor / Modal), its style folder
(pop, soul, hiphop ...) and the mood words. Same numerals in different keys are one piece (deduped), so 11,700 files are
about a thousand distinct progressions. Gate: every numeral must parse, 2-8 chords."""
import argparse, collections, glob, json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pieces as P

ROOT = os.path.expanduser("~/Infinite/datasets/midi/data/chord_progressions")
NUM = re.compile(r"^([b#]?)(VII|VI|IV|V|III|II|I|vii|vi|iv|v|iii|ii|i)(.*)$")
DEG = {"i": 1, "ii": 2, "iii": 3, "iv": 4, "v": 5, "vi": 6, "vii": 7}


def parse_num(tok):
    m = NUM.match(tok)
    if not m:
        return None
    acc, roman, ext = m.groups()
    return {"deg": DEG[roman.lower()], "acc": acc, "minor": roman.islower(), "ext": ext}


def pieces(root):
    for f in sorted(glob.glob(root + "/**/*.mid", recursive=True)):
        rel = os.path.relpath(f, root).split(os.sep)
        parts = os.path.basename(f)[:-4].split(" - ")
        if len(parts) != 3:
            continue
        key, nums, mood = parts
        yield {"type": "chord_prog", "src": "chords:" + "/".join(rel), "family": "harmony", "mode": rel[0],
               "style": rel[1] if len(rel) > 2 else "", "key": key, "numerals": nums.split(), "mood": mood.split()}


def gate_chord(p):
    h, s = [], []
    ch = [parse_num(t) for t in p["numerals"]]
    if any(c is None for c in ch): return ["unparseable numeral"], s
    if not 2 <= len(ch) <= 8: h.append("length outside 2..8")
    if len(set(p["numerals"])) == 1: s.append("one chord repeated")
    p["chords"] = ch
    return h, s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--store", default="~/Infinite/training-data/store")
    a = ap.parse_args()
    rows, seen = [], set()
    for p in pieces(a.root):
        h, s = gate_chord(p)
        key = P.digest({"m": p["mode"], "s": p["style"], "n": p["numerals"]})
        if not h and key in seen:
            h = ["duplicate of an earlier piece"]
        seen.add(key)
        p.update(id=key, hard=h, soft=s, ok=not h, license="cleared-by-owner")
        rows.append(p)
    store = os.path.expanduser(a.store)
    os.makedirs(store, exist_ok=True)
    out = os.path.join(store, "chords.jsonl")
    with open(out, "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    ok = [r for r in rows if r["ok"]]
    why = collections.Counter(x for r in rows for x in r["hard"])
    print("chord_prog: %d files, %d distinct progressions pass; rejected: %s" % (len(rows), len(ok), dict(why.most_common(4))))
    print("by mode:", dict(collections.Counter(r["mode"] for r in ok)))
    print("moods:", dict(collections.Counter(w for r in ok for w in r["mood"]).most_common(8)))
    print("wrote", out)


if __name__ == "__main__":
    main()
