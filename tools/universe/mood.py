#!/usr/bin/env python3
"""Mood layer (W4 of docs/plans/data-universe/next-phase.md): measured valence / energy / danceability / brightness /
contrast per song, by formula from the rows we already have, so every number can be audited and none is a declared guess.
    mood.py build [--store ~/Infinite/training-data/store] [--out ~/Infinite/training-data/mood/song_mood.jsonl] [--limit N]
    mood.py check [--mood ...]     correlation of measured axes against the genres' declared pillars (atlas/genres.json)
One row per song (the `src` before '#'), aggregated over its melody / expression / chord / bass / drum rows:

  axis          from (MIDI rows)                                                       scale
  valence       major-chord share (chord_prog) 0.55, major-mode share of bass_seq 0.15,  0..1
                tempo 0.15 (60 bpm 0 -> 160 bpm 1), upward-step share of melodies 0.15
  energy        note rate (melody `rate`, 0..6 /s) 0.35, mean velocity 0.30,            0..1
                drum hit density 0.20, tempo 0.15
  danceability  kick-on-the-beat share (steps 0/4/8/12 of the kick lane) 0.5, drum      0..1
                regularity (1 - hamming to the bar's own half-bar repeat) 0.3, tempo near 90-150 0.2
  brightness    melody register centroid (MIDI 36 -> 0, 96 -> 1)                        0..1
  contrast      velocity spread (p90 - p10 over 127 x 2.5), clipped                      0..1
Axes with no source row stay null. Audio rows join the same axes later (key mode, spectral centroid, rms, onset rate);
the character-sim emotion axes (stress, dominance) are reserved, not built. Statistics only."""
import argparse, bisect, collections, json, math, os, sys

TD = os.path.expanduser("~/Infinite/training-data")
AXES = ("valence", "energy", "danceability", "brightness", "contrast")


def clip(x):
    return max(0.0, min(1.0, x))


def tempo01(bpm):
    return clip((bpm - 60) / 100.0)


def row_obs(r):
    """(axis, value) observations one row contributes"""
    t = r["type"]
    out = []
    if t == "chord_prog":
        ch = r.get("chords") or []
        if ch:
            out.append(("maj", sum(not c.get("minor") for c in ch) / len(ch)))
    elif t == "melody_contour":
        p, s = r.get("pitches") or [], r.get("steps") or []
        if p:
            out.append(("reg", clip((sum(p) / len(p) - 36) / 60.0)))
        if s:
            out.append(("up", sum(x > 0 for x in s) / len(s)))
        if r.get("rate") is not None:
            out.append(("rate", clip(float(r["rate"]) / 6.0)))
    elif t == "note_expression":
        v = [x for x in (r.get("vel") or []) if x]
        if v:
            v.sort()
            out.append(("vel", sum(v) / len(v) / 127.0))
            out.append(("spread", clip((v[int(0.9 * (len(v) - 1))] - v[int(0.1 * (len(v) - 1))]) / 127.0 * 2.5)))
    elif t == "bass_seq":
        if r.get("mode") in ("Major", "Minor"):
            out.append(("bmode", 1.0 if r["mode"] == "Major" else 0.0))
        if r.get("bpm"):
            out.append(("bpm", float(r["bpm"])))
    elif t == "drum_groove":
        lanes = r.get("lanes") or []
        if lanes and len(lanes) >= 1 and lanes[0]:
            k = [i for i, c in enumerate(lanes[0]) if c != "."]
            if k:
                out.append(("kickbeat", sum(i % 4 == 0 for i in k) / len(k)))
        hits = sum(c != "." for ln in lanes for c in (ln or ""))
        out.append(("dens", clip(hits / 40.0)))
        a = "".join(ln[:8] if ln else "........" for ln in lanes)
        b = "".join(ln[8:] if ln else "........" for ln in lanes)
        if len(a) == len(b) and a:
            same = sum((x != ".") == (y != ".") for x, y in zip(a, b))
            out.append(("reg_d", same / len(a)))
        if r.get("bpm"):
            out.append(("bpm", float(r["bpm"])))
    return out


def mean(d, k):
    v = d.get(k)
    return sum(v) / len(v) if v else None


def combine(d):
    """five axes from one song's accumulated observations (lists per key)"""
    m = lambda k: mean(d, k)
    bpm = m("bpm")

    def mix(parts):
        w = sum(x[0] for x in parts if x[1] is not None)
        return round(sum(x[0] * x[1] for x in parts if x[1] is not None) / w, 3) if w else None
    t = tempo01(bpm) if bpm else None
    dance_t = None if bpm is None else clip(1 - abs(bpm - 120) / 60.0)
    return {
        "valence": mix([(0.55, m("maj")), (0.15, m("bmode")), (0.15, t), (0.15, m("up"))]),
        "energy": mix([(0.35, m("rate")), (0.30, m("vel")), (0.20, m("dens")), (0.15, t)]),
        "danceability": mix([(0.5, m("kickbeat")), (0.3, m("reg_d")), (0.2, dance_t)]),
        "brightness": mix([(1.0, m("reg"))]),
        "contrast": mix([(1.0, m("spread"))]),
    }


def build(a):
    store = os.path.expanduser(a.store)
    acc = collections.defaultdict(lambda: collections.defaultdict(list))
    cnt = collections.defaultdict(collections.Counter)
    fam = {}
    n = 0
    for fn in sorted(os.listdir(store)):
        if not fn.endswith(".jsonl") or fn in ("formats_meta.jsonl", "patch_pieces.jsonl", "synthetic.jsonl"):
            continue
        for ln in open(os.path.join(store, fn)):
            if '"ok": true' not in ln and '"ok":true' not in ln:
                continue
            r = json.loads(ln)
            if r["type"] not in ("chord_prog", "melody_contour", "note_expression", "bass_seq", "drum_groove"):
                continue
            if r["type"] == "drum_groove" and r["src"].startswith("card:"):
                continue  # catalog cards are declared, not measured
            song = r["src"].split("#")[0]
            for k, v in row_obs(r):
                acc[song][k].append(v)
            cnt[song][r["type"]] += 1
            f = r.get("family")
            if f and f != "unlabeled":
                fam[song] = f
            n += 1
            if a.limit and n >= a.limit:
                break
            if n % 500000 == 0:
                print("%d rows, %d songs" % (n, len(acc)), flush=True)
    out = os.path.expanduser(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    rows = []
    for song, d in acc.items():
        row = {"src": song, "family": fam.get(song), "rows": dict(cnt[song]), **combine(d)}
        if "bpm" in d:
            row["bpm"] = round(mean(d, "bpm"), 1)
        rows.append(row)
    # the raw formulas bunch up (energy sits at 0.4-0.5 for every genre): `<axis>_pct` is the song's percentile among all
    # songs, a monotone stretch to 0..1 that keeps the ranking and the audit trail of the raw value
    for ax in AXES:
        v = sorted(r[ax] for r in rows if r[ax] is not None)
        for r in rows:
            if r[ax] is not None:
                r[ax + "_pct"] = round(bisect.bisect_left(v, r[ax]) / max(1, len(v) - 1), 3)
    k = 0
    with open(out, "w") as fo:
        for row in rows:
            fo.write(json.dumps(row, separators=(",", ":")) + "\n")
            k += 1
    print("wrote %d songs from %d rows to %s" % (k, n, out))


def pearson(x, y):
    n = len(x)
    if n < 3:
        return None
    mx, my = sum(x) / n, sum(y) / n
    sx = math.sqrt(sum((a - mx) ** 2 for a in x))
    sy = math.sqrt(sum((b - my) ** 2 for b in y))
    return sum((a - mx) * (b - my) for a, b in zip(x, y)) / (sx * sy) if sx > 0 and sy > 0 else None


def check(a):
    """measured vs declared: songs are grouped by their label (family or the genre word in the path), the mean measured
    axis per group is correlated with the group's declared pillar, across groups"""
    g = json.load(open(os.path.join(TD, "atlas", "genres.json")))["genres"]
    decl, kids = {}, collections.defaultdict(list)
    for gid, v in g.items():
        p = v.get("declared", {}).get("pillars")
        if p:
            decl[gid] = p
            if v.get("parent"):
                kids[v["parent"]].append(p)
    for par, ps in kids.items():  # a parent with no card of its own: the mean of its children
        decl.setdefault(par, {ax: sum(p[ax] for p in ps) / len(ps) for ax in AXES})
    groups = collections.defaultdict(list)
    for ln in open(os.path.expanduser(a.mood)):
        r = json.loads(ln)
        parts = r["src"].replace("inbox:", "").lower().replace("_", " ").split("/")
        lab = r.get("family") if r.get("family") in decl else None
        if lab is None and r["src"].startswith("inbox:adl_piano_genres") and len(parts) > 3:
            lab = {"ambient": "ambient", "electronic": "house"}.get(parts[3].replace(" ", "_"))
        if lab is None and r["src"].startswith("inbox:nes_chiptune"):
            lab = "retro"
        if lab in decl:
            groups[lab].append(r)
    print("groups with a declared pillar:", {k: len(v) for k, v in groups.items()})
    res = {}
    for ax in AXES:
        x, y = [], []
        for lab, rs in groups.items():
            vals = [r[ax + a.suffix] for r in rs if r.get(ax + a.suffix) is not None]
            if len(vals) >= 5:
                x.append(sum(vals) / len(vals))
                y.append(decl[lab][ax])
        res[ax] = (pearson(x, y), len(x))
        print("%-13s r=%s over %d groups" % (ax, "n/a" if res[ax][0] is None else "%.2f" % res[ax][0], len(x)))
    return res


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("build", "check"))
    ap.add_argument("--store", default=os.path.join(TD, "store"))
    ap.add_argument("--out", default=os.path.join(TD, "mood", "song_mood.jsonl"))
    ap.add_argument("--mood", default=os.path.join(TD, "mood", "song_mood.jsonl"))
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--suffix", default="", help="check: '_pct' scores the percentile axes")
    a = ap.parse_args()
    build(a) if a.cmd == "build" else check(a)
