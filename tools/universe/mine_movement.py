"""Mine the owner's movement logs: which params they move by hand, per node type, and where they rest.
Writes profile_movement.json: {type: {param name: {hand, mod, hist16}}} (hist16 = hand-set position, 16 bins)."""
import collections, glob, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "prediction"))
import eval as ev

LOG = os.path.expanduser("~/Library/Application Support/Infinite/movement-log")


def main():
    files = sorted(glob.glob(LOG + "/*.mlog*"))
    agg = collections.defaultdict(lambda: collections.defaultdict(lambda: {"hand": 0, "mod": 0, "hist16": [0] * 16}))
    for p in files:
        keys = {}
        for r in ev.read_records(p):
            if r["type"] == ev.REC_KEY:
                keys[r["id"]] = (r["tname"], r["name"], r["is_enum"] or r["is_bool"])
            elif r["type"] == ev.REC_VAL and r["id"] in keys:
                t, n, disc = keys[r["id"]]
                a = agg[t][n]
                if r["src"] == 0:
                    a["hand"] += 1; a["hist16"][min(15, r["q"] >> 12)] += 1
                elif r["src"] in (2, 3):
                    a["mod"] += 1
    out = {t: dict(v) for t, v in agg.items()}
    json.dump(out, open(os.path.join(HERE, "profile_movement.json"), "w"))
    tot = collections.Counter()
    for t, ps in out.items():
        for n, a in ps.items(): tot[(t, n)] += a["hand"]
    print(len(files), "files;", len(out), "node types;", sum(tot.values()), "hand moves")
    for (t, n), c in tot.most_common(40): print("%7d  %s . %s" % (c, t, n))
    print("\nprediction/field/sketch types:", [t for t in out if any(k in t for k in ("Field", "Sketch", "Predict", "Drift"))])


if __name__ == "__main__":
    main()
