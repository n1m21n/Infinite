#!/usr/bin/env python3
"""Snapshot the drive-only references the Generate Samples scorecard needs, so it never reads the WD-Black drive again.
    ~/Infinite/media/.ytvenv/bin/python3 -I tools/universe/snapshot_refs.py [--out internal/plans/generate-samples/refs]
Writes (small JSON, quantiles only, no paths or audio):
  library.json  per library part (training-data/audio/features.jsonl, loops only): p5, q1, median, q3, p95 and n of the
                part_vs_library keys (centroid, low, lowmid, high, sub, crest, flatness, onsets_per_s)
  corpus.json   MIDI corpus (store/midi_full.jsonl + midi_axes.jsonl): bass and melodic register and density per channel, exactly as
                role_spread.corpus counts them (first 150,000 files), and per-file bpm, density, register, rest, swing as note_spread does
  mix.json      finished tracks (mix/audio_mix.jsonl): lufs, crest_db, side_mid, low_corr, tilt_db and band shares, overall and per genre;
                Ableton sets (mix/als_mix.jsonl): per role vol_db, pan and send levels, plus the device chains' most common devices
Exit 0, or 2 when the drive is not mounted."""
import argparse, collections, json, os, random, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
TD = os.path.expanduser("~/Infinite/training-data")
Q = (5, 25, 50, 75, 95)


def qs(v):
    v = np.asarray([x for x in v if x is not None and np.isfinite(x)], float)
    if not len(v): return None
    return dict(zip(("p5", "q1", "median", "q3", "p95"), (round(float(x), 5) for x in np.percentile(v, Q))), n=int(len(v)))


def library():
    import part_vs_library as pvl
    acc = collections.defaultdict(lambda: collections.defaultdict(list))
    lo = 20 * 1000 ** (np.arange(32) / 32); hi = 20 * 1000 ** (np.arange(1, 33) / 32)
    for line in open(os.path.join(TD, "audio/features.jsonl")):
        r = json.loads(line)
        if not r.get("loop") or not r.get("part") or not r.get("bands"): continue
        b = np.array(r["bands"]); t = b.sum() or 1e-30
        v = {"centroid": r["centroid"], "low": r["low"], "high": r["high"], "crest": r["crest"], "flatness": r["flatness"],
             "lowmid": float(b[(lo >= 250) & (hi <= 2100)].sum() / t), "sub": float(b[hi <= 41].sum() / t),
             "onsets_per_s": r["onsets"] / max(0.5, r["seconds"])}
        for k in pvl.KEYS: acc[r["part"]][k].append(v[k])
    return {p: {k: qs(v) for k, v in d.items()} for p, d in acc.items()}


def corpus():
    import role_spread
    reg, den = role_spread.corpus(150000)
    out = {"per_channel": {k: {"register": qs(reg[k]), "density": qs(den[k])} for k in reg},
           "per_channel_rule": "role_spread.corpus(150000): files >= 5 s, non-drum channels, register = (lo+hi)/2, density = notes/s"}
    S = os.path.join(TD, "store/")
    ax = {}
    for l in open(S + "midi_axes.jsonl"):
        r = json.loads(l)
        if "a24" not in r or "a25" not in r or "a27" not in r: continue
        sw = r["a25"].get("swing_ratio") or 1.0
        ax[r["src"]] = {"bpm": r["a24"]["bpm"], "rest": r["a27"]["silence_ratio"], "swing": max(0.0, (sw - 1) / (sw + 1))}
    keys = list(ax); random.Random(0).shuffle(keys); keep = set(keys[:20000]); F = []
    for l in open(S + "midi_full.jsonl"):
        r = json.loads(l)
        if r["src"] not in keep or not r.get("seconds"): continue
        ch = [c for c in r["channels"].values() if c.get("family") != "drums" and c.get("lo") is not None]
        if not ch: continue
        d = dict(ax[r["src"]]); d["density"] = r["notes"] / r["seconds"]; d["register"] = float(np.mean([(c["lo"] + c["hi"]) / 2 for c in ch]))
        F.append(d)
    out["per_file"] = {k: qs([x.get(k) for x in F]) for k in ("bpm", "density", "register", "rest", "swing")}
    out["per_file_rule"] = "note_spread.corpus(20000): 20,000 files shuffled with seed 0"
    return out


def mix():
    A = [json.loads(l) for l in open(os.path.join(TD, "mix/audio_mix.jsonl")) if l.strip()]
    def table(rows):
        t = {k: qs([r.get(k) for r in rows]) for k in ("lufs", "crest_db", "side_mid", "low_corr", "tilt_db", "arc_range_db")}
        t["bands"] = {b: qs([r["bands"].get(b) for r in rows if r.get("bands")]) for b in ("sub", "bass", "lowmid", "mid", "himid", "high")}
        return t
    g = collections.defaultdict(list)
    for r in A: g[r.get("genre") or "?"].append(r)
    tracks = {"all": table(A), "genres": {k: table(v) for k, v in sorted(g.items()) if len(v) >= 20}}
    L = [json.loads(l) for l in open(os.path.join(TD, "mix/als_mix.jsonl")) if l.strip()]
    roles = collections.defaultdict(list)
    for r in L:
        if r.get("on", True) and r.get("kind") != "ReturnTrack": roles[r.get("role") or "?"].append(r)
    sets = {}
    for role, rows in sorted(roles.items()):
        if len(rows) < 10: continue
        dev = collections.Counter(d for r in rows for d in set(r.get("devices") or []))
        sets[role] = {"vol_db": qs([r.get("vol_db") for r in rows]), "pan": qs([r.get("pan") for r in rows]),
                      "send_db_max": qs([max(r["sends_db"]) for r in rows if r.get("sends_db")]),
                      "devices": [(d, round(c / len(rows), 3)) for d, c in dev.most_common(8)], "n": len(rows)}
    return {"tracks": tracks, "ableton_sets": {"roles": sets, "n_tracks": len(L), "n_sets": len({r.get("set") for r in L})}}


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--out", default=os.path.join(HERE, "../../internal/plans/generate-samples/refs"))
    a = ap.parse_args()
    if not os.path.exists(os.path.join(TD, "audio/features.jsonl")):
        print("training-data is not mounted (%s)" % TD, file=sys.stderr); return 2
    os.makedirs(a.out, exist_ok=True)
    for name, fn in (("mix", mix), ("library", library), ("corpus", corpus)):
        d = fn(); p = os.path.join(a.out, name + ".json"); json.dump(d, open(p, "w"), indent=1)
        print("wrote %s (%d bytes)" % (p, os.path.getsize(p)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
