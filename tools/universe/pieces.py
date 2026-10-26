#!/usr/bin/env python3
"""Piece store, stage 1: break the music data into typed pieces and run a QA gate on each.
    pieces.py [--out pieces.jsonl]
Pieces (v1): drum_groove and bass_seq from the style cards (tools/catalog/styles_music.json),
note_seq from the bundled MIDI files (tools/universe/assets/*.mid).
A hard gate failure rejects a piece; a soft one only warns. Nothing here learns: it measures how
much of the current data is clean enough to learn from, and which gate refuses what."""
import argparse, collections, glob, hashlib, json, os, struct

HERE = os.path.dirname(os.path.abspath(__file__))
CARDS = os.path.join(HERE, "..", "catalog", "styles_music.json")
LANES = ["kick", "snare", "chat", "ohat", "clap", "tom", "htom", "bell"]
FOUR_FLOOR = {"house", "techno", "trance", "hard"}


def digest(x):
    return hashlib.sha1(json.dumps(x, sort_keys=True).encode()).hexdigest()[:12]


# ---- extractors -------------------------------------------------------------------------------

def cards():
    return json.load(open(CARDS))["styles"]


def drum_pieces():
    for c in cards():
        if not any(c["drums"].values()):
            continue  # no drum part on this card (piano, drone): nothing to judge
        yield {"type": "drum_groove", "src": "card:" + c["id"], "family": c["family"],
               "lanes": [c["drums"].get(l, "") for l in LANES], "swing": c.get("swing", 0.0),
               "groove": c.get("groove")}


def bass_pieces():
    for c in cards():
        if not c.get("bass"):
            continue  # no bass figure on this card
        yield {"type": "bass_seq", "src": "card:" + c["id"], "family": c["family"], "steps": c.get("bass", "")}


def midi_notes(path):
    """(tick, pitch, ticks_per_beat) for every note-on, any track, format 0 or 1."""
    b = open(path, "rb").read()
    if b[:4] != b"MThd":
        raise ValueError("not a midi file")
    ntrk, div = struct.unpack(">HH", b[10:14])
    out, pos = [], 14
    while pos + 8 <= len(b):
        tag, ln = b[pos:pos + 4], struct.unpack(">I", b[pos + 4:pos + 8])[0]
        pos += 8
        if tag == b"MTrk":
            out += _track(b[pos:pos + ln], div)
        pos += ln
    return sorted(out), div


def _track(d, div):
    i, t, run, notes = 0, 0, 0, []

    def vlq():
        nonlocal i
        v = 0
        while True:
            c = d[i]; i += 1
            v = (v << 7) | (c & 0x7f)
            if not c & 0x80:
                return v
    while i < len(d):
        t += vlq()
        s = d[i]
        if s == 0xff:
            i += 2
            n = vlq()  # vlq() advances i itself, so read it before adding
            i += n
            continue
        if s in (0xf0, 0xf7):
            i += 1
            n = vlq()
            i += n
            continue
        if s & 0x80:
            run = s; i += 1
        k = run & 0xf0
        n = 1 if k in (0xc0, 0xd0) else 2
        a = d[i:i + n]; i += n
        if k == 0x90 and len(a) == 2 and a[1] > 0:
            notes.append((t, a[0]))
    return notes


def midi_pieces():
    for p in sorted(glob.glob(os.path.join(HERE, "assets", "*.mid"))):
        try:
            notes, div = midi_notes(p)
        except Exception as e:
            yield {"type": "note_seq", "src": "midi:" + os.path.basename(p), "error": str(e), "pitches": [], "grid": []}
            continue
        yield {"type": "note_seq", "src": "midi:" + os.path.basename(p), "family": "midi",
               "pitches": [n[1] for n in notes],
               "grid": [round(n[0] / (div / 4.0), 3) for n in notes]}  # position in sixteenths


# ---- gates: (hard_reasons, soft_reasons) ------------------------------------------------------

def gate_drum(p):
    h, s = [], []
    L = p["lanes"]
    if any(len(l) not in (0, 16) for l in L):
        return ["lane length not 0 or 16"], s
    if any(set(l) - set("xo.") for l in L):
        return ["lane has a char outside x o ."], s
    cells = sum(len(l) for l in L)
    hits = sum(c != "." for l in L for c in l)
    if hits == 0:
        return ["no hits at all"], s
    # normalise high-density check against at least 3 lanes (48 cells) so kick + 16th-note hats (20/32) is not rejected
    if hits / max(48, cells) > 0.6: h.append("density over 0.6")
    if hits / cells < 0.05: h.append("density under 0.05")
    # hats (lanes 2, 3) legitimately run on every 16th; any other lane over 0.75 is a stuck or machine-gun pattern
    if any(l and sum(c != "." for c in l) / 16 > (1.0 if i in (2, 3) else 0.75) for i, l in enumerate(L)): h.append("one lane over 0.75")
    if not 0 <= p["swing"] <= 0.4: h.append("swing outside 0..0.4")
    k, sn = L[0], L[1]
    if not k and p.get("family") != "ambient":
        h.append("no kick")
    if k and sn and sum(a != "." and b != "." for a, b in zip(k, sn)) / max(1, sum(c != "." for c in k)) > 0.5:
        s.append("kick and snare collide on over half the kicks")
    if p["family"] in FOUR_FLOOR and k and sum(k[i] != "." for i in (0, 4, 8, 12)) < 3:
        s.append("four-on-floor family without 3 of 4 downbeat kicks")
    if not k and not any(L[2:4]):
        s.append("no kick and no hats")
    return h, s


def gate_bass(p):
    h, s = [], []
    st = p["steps"]
    if not st:
        return [], ["no bass figure"]
    if len(st) != 16: return ["length not 16"], s
    if any(c != "." and not c.isalnum() for c in st): return ["bad char"], s
    deg = [int(c, 36) for c in st if c != "."]
    if not deg: return ["no notes"], s
    dens = len(deg) / 16
    if dens > 0.75: h.append("density over 0.75")
    if max(deg) > 9: h.append("degree over 9")
    if dens < 0.1: s.append("density under 0.1")
    if max(abs(a - b) for a, b in zip(deg, deg[1:])) > 7 if len(deg) > 1 else False: h.append("jump over 7 degrees")
    if st[0] == ".": s.append("rest on the downbeat")
    return h, s


def gate_notes(p):
    h, s = [], []
    if p.get("error"): return ["unreadable: " + p["error"]], s
    P, G = p["pitches"], p["grid"]
    if len(P) < 4: return ["fewer than 4 notes"], s
    if max(P) - min(P) > 48: h.append("range over 48 semitones")
    if len(set(x % 12 for x in P)) > 10: h.append("more than 10 pitch classes")
    on = sum(abs(g - round(g)) < 0.1 for g in G) / len(G)
    if on < 0.5: s.append("under half the notes on the sixteenth grid")
    if len(set(P)) == 1: s.append("a single pitch")
    return h, s


GATES = {"drum_groove": gate_drum, "bass_seq": gate_bass, "note_seq": gate_notes}


# ---- run --------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "pieces.jsonl"))
    a = ap.parse_args()
    seen, rows = set(), []
    for p in [*drum_pieces(), *bass_pieces(), *midi_pieces()]:
        h, s = GATES[p["type"]](p)
        key = digest({k: v for k, v in p.items() if k not in ("src", "family")})
        if not h and key in seen:
            h = ["duplicate of an earlier piece"]
        seen.add(key)
        p.update(id=key, hard=h, soft=s, ok=not h)
        rows.append(p)
    with open(a.out, "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    print("%-12s %5s %5s %5s  %s" % ("type", "total", "ok", "warn", "hard fails"))
    for t in GATES:
        r = [x for x in rows if x["type"] == t]
        why = collections.Counter(x for q in r for x in q["hard"])
        print("%-12s %5d %5d %5d  %s" % (t, len(r), sum(q["ok"] for q in r), sum(q["ok"] and bool(q["soft"]) for q in r),
                                         "; ".join("%s x%d" % kv for kv in why.most_common()) or "-"))
    print("\nwarnings:", dict(collections.Counter(x for q in rows if q["ok"] for x in q["soft"])))
    print("rejected:", [(q["src"], q["hard"]) for q in rows if not q["ok"]])
    print("wrote", a.out)


if __name__ == "__main__":
    main()
