#!/usr/bin/env python3
"""Goal item 2: key accuracy on unseen songs. The labelled keys come from the file names of the chord-progression set (`<key> - <numerals> - <mood>.mid`); the detector is midi_axes (fixed profiles, never fitted to these files).
    key_eval.py [--n 1500]
Exact = tonic and mode right. Lenient = also counts the relative major/minor."""
import argparse, glob, json, os, random, collections
import midi_axes as MA
ROOT = os.path.expanduser("~/Infinite/datasets/midi/data/chord_progressions")
PC = {"C":0,"C#":1,"Db":1,"D":2,"D#":3,"Eb":3,"E":4,"F":5,"F#":6,"Gb":6,"G":7,"G#":8,"Ab":8,"A":9,"A#":10,"Bb":10,"B":11}
ap = argparse.ArgumentParser(); ap.add_argument("--n", type=int, default=1500); a = ap.parse_args()
fs = [f for f in glob.glob(ROOT + "/Major/**/*.mid", recursive=True) + glob.glob(ROOT + "/Minor/**/*.mid", recursive=True)]
random.Random(0).shuffle(fs); ex = le = n = 0; by = collections.Counter(); tot = collections.Counter()
for f in fs[:a.n * 2]:
    if n >= a.n: break
    key = os.path.basename(f).split(" - ")[0].strip(); mode = "major" if "/Major/" in f else "minor"
    if key not in PC: continue
    r = MA.row(f)
    if not r or "a23" not in r: continue
    t, m = r["a23"]["tonic"], r["a23"]["mode"]; want = PC[key]; n += 1; tot[mode] += 1
    rel = (want + 9) % 12 if mode == "major" else (want + 3) % 12
    ok = (t == want and m == mode); ex += ok; by[mode] += ok
    le += ok or (t == rel and m != mode)
print(n, "files | exact %.3f | lenient (relative key ok) %.3f" % (ex / n, le / n), {k: round(by[k] / tot[k], 3) for k in tot})
json.dump({"n": n, "exact": ex / n, "lenient": le / n, "by_mode": {k: by[k] / tot[k] for k in tot}}, open("../../docs/plans/data-universe/feel/key_eval.json", "w"))
