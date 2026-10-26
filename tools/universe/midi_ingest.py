#!/usr/bin/env python3
"""Inbox MIDI -> gated pieces.
    midi_ingest.py [--inbox ~/Infinite/training-data/inbox] [--store ~/Infinite/training-data/store]
Layout: inbox/<source>/**/*.mid plus an optional SOURCE.txt with lines `family: house` and `license: CC-BY-4.0`.
A source with no licence line is `unknown`: it trains the local model only. A folder or file name containing a
family word (house, techno, trance, dnb, breaks, ambient ...) sets the family when SOURCE.txt does not.
Channel 10 (index 9) becomes drum_groove bars; every other channel becomes melody_contour + note_expression windows
tagged with a role from its median pitch (bass under 55, lead from 72, else mid)."""
import argparse, collections, glob, hashlib, json, os, re, statistics, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pieces as P
import expression_pieces as E
import public_drums as D

FAMILY_WORDS = {"house": "house", "techno": "techno", "trance": "trance", "dnb": "drum_and_bass", "drum_and_bass": "drum_and_bass",
                "jungle": "drum_and_bass", "breaks": "breaks", "breakbeat": "breaks", "ambient": "ambient", "downtempo": "downtempo",
                "dubstep": "bass_music", "bass": "bass_music", "synthwave": "retro", "electro": "retro", "lofi": "downtempo",
                "hiphop": "hiphop", "trap": "hiphop", "disco": "house", "garage": "house", "idm": "experimental"}


def source_meta(folder):
    meta = {"family": None, "license": "unknown"}
    f = os.path.join(folder, "SOURCE.txt")
    if os.path.exists(f):
        for ln in open(f, errors="replace"):
            m = re.match(r"\s*(family|license|licence)\s*:\s*(.+?)\s*$", ln, re.I)
            if m:
                meta["family" if m[1].lower() == "family" else "license"] = m[2]
    return meta


def guess_family(path, meta):
    if meta["family"]:
        return meta["family"]
    toks = set(re.split(r"[^a-z0-9]+", path.lower()))  # whole words only: "bass" must not match "bassoon" or "contrabass"
    for w, fam in FAMILY_WORDS.items():
        if w in toks:
            return fam
    return "unlabeled"


def meta(tracks):
    sig, tempo = (4, 2), 500000
    for ev in tracks:
        for t, k, ch, a, v in ev:
            if k == 0xff and ch == 0 and t == 0 and sig == (4, 2):
                sig = (a, v)
            if k == 0xff and ch == 1 and tempo == 500000:
                tempo = a
    return sig, 60e6 / max(1, tempo)


def drum_bars(tracks, div, per_file):
    """one-bar windows of the drum channel; 4/4 files only; bars with under 85% of hits near the 16th grid are dropped
    (triplets, live feel). Bar 0 is skipped only when the file has more than 3 bars (count-ins, pickups)."""
    sig, bpm = meta(tracks)
    if sig != (4, 2):
        return
    grid = collections.defaultdict(lambda: [["."] * 16 for _ in D.LANE_NOTES])
    off = collections.defaultdict(lambda: [0, 0])
    sixteenth = div / 4.0
    for ev in tracks:
        for t, k, ch, a, v in ev:
            if k == 0x90 and ch == 9 and v > 0:
                for li, ns in enumerate(D.LANE_NOTES):
                    if a in ns:
                        s = round(t / sixteenth)
                        grid[s // 16][li][s % 16] = "x"
                        off[s // 16][0] += abs(t / sixteenth - s) <= 0.25
                        off[s // 16][1] += 1
    got = 0
    for b in sorted(grid):
        if b == 0 and len(grid) > 3:
            continue
        near, tot = off[b]
        if tot and near / tot < 0.85:
            continue
        lanes = ["".join(l) if "x" in l else "" for l in grid[b]]
        if any(lanes):
            yield lanes, bpm
            got += 1
            if got >= per_file:
                return


def role(pitches):
    m = statistics.median(pitches)
    return "bass" if m < 55 else ("lead" if m >= 72 else "mid")


def pick(files, cap):
    """cap files per source, chosen by hash of the path so a rerun picks the same ones"""
    if not cap or len(files) <= cap:
        return files
    return sorted(files, key=lambda f: hashlib.sha1(f.encode()).hexdigest())[:cap]


def ingest(inbox, per_file=8, cap=4000, deadline=None):
    for src in sorted(d for d in glob.glob(os.path.join(inbox, "*")) if os.path.isdir(d)):
        smeta = source_meta(src)
        files = sorted(glob.glob(os.path.realpath(src) + "/**/*.mid*", recursive=True))
        files = [f for f in files if os.path.getsize(f) < 2_000_000]
        files = pick(files, cap)
        sys.stderr.write("%s: %d files\n" % (os.path.basename(src), len(files)))
        for f in files:
            if deadline and time.time() > deadline:
                sys.stderr.write("time budget reached, stopping\n")
                return
            name = os.path.basename(src) + "/" + os.path.relpath(f, os.path.realpath(src))
            fam = guess_family(name, smeta)
            try:
                tracks, div = E.parse_midi(f)
                for i, (lanes, bpm) in enumerate(drum_bars(tracks, div, per_file)):
                    yield {"type": "drum_groove", "src": "inbox:%s#bar%d" % (name, i), "family": fam, "lanes": lanes,
                           "swing": 0.0, "groove": None, "bpm": round(bpm), "license": smeta["license"]}
                ws = list(E.windows(f))
                if len(ws) > 12:  # spread 12 windows over the song instead of keeping every one
                    ws = [ws[i * len(ws) // 12] for i in range(12)]
                for k, w in enumerate(ws):
                    steps = [b - a for a, b in zip(w["pitches"], w["pitches"][1:])]
                    nm = "inbox:%s#%d" % (name, k)
                    yield {"type": "melody_contour", "src": nm, "family": fam, "role": role(w["pitches"]), "pitches": w["pitches"],
                           "steps": steps, "license": smeta["license"]}
                    yield {"type": "note_expression", "src": nm, "family": fam, "vel": w["vel"], "bend": w["bend"],
                           "pressure": w["pressure"], "slide": w["slide"], "license": smeta["license"]}
            except Exception as e:
                sys.stderr.write("skip %s: %r\n" % (name, e))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--inbox", default="~/Infinite/training-data/inbox")
    ap.add_argument("--store", default="~/Infinite/training-data/store")
    ap.add_argument("--cap", type=int, default=4000, help="max files per source (0 = all)")
    ap.add_argument("--budget", type=float, default=20, help="minutes before it stops and keeps what it has")
    a = ap.parse_args()
    inbox, store = os.path.expanduser(a.inbox), os.path.expanduser(a.store)
    os.makedirs(store, exist_ok=True)
    gates = {"drum_groove": P.gate_drum, "melody_contour": E.gate_contour, "note_expression": E.gate_expr}
    rows, seen = [], set()
    for p in ingest(inbox, cap=a.cap, deadline=time.time() + 60 * a.budget):
        h, s = gates[p["type"]](p)
        key = P.digest({k: v for k, v in p.items() if k not in ("src", "family", "license")})
        if not h and key in seen:
            h = ["duplicate of an earlier piece"]
        seen.add(key)
        p.update(id=key, hard=h, soft=s, ok=not h)
        rows.append(p)
    out = os.path.join(store, "inbox.jsonl")
    with open(out, "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    print("%-16s %6s %6s  %s" % ("type", "total", "ok", "by family (ok)"))
    for t in gates:
        r = [x for x in rows if x["type"] == t]
        fam = collections.Counter(x["family"] for x in r if x["ok"])
        print("%-16s %6d %6d  %s" % (t, len(r), sum(x["ok"] for x in r), dict(fam.most_common(8))))
    print("licenses:", dict(collections.Counter(x["license"] for x in rows)))
    print("wrote", out)


if __name__ == "__main__":
    main()
