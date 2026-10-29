#!/usr/bin/env python3
"""Groove MIDI -> drum idioms per genre, for the Drums part (owner's review, 2026-10-29: "after some generation the
pattern repeats"; the card had one groove each). Reads every bar of every 4/4 beat file (public_drums.py reads two)
and keeps velocity: a hit under GHOST is a ghost note ('o'), the rest are 'x'. A bar is an idiom only when at least
--min-files different performances play it exactly, so what ships is a shared figure, never one drummer's take.
    groove_bank.py [--root ~/Infinite/datasets/midi/data/groove_drumset/groove] [--min-files 3] [--out FILE] [--stats]
Groove MIDI: Magenta, CC BY 4.0 (Gillick, Roberts, Engel, Eck, Bamman 2019). Credit kept in the generated file."""
import argparse, collections, glob, os, re, struct

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.join(HERE, "..", "..", "src", "app", "graph", "GenerateSamplesGrooves.inc")
GHOST = 60
# Roland TD-11 map the dataset was recorded with. The ride counts as the hat lane: it plays the same role.
LANES = {"k": (35, 36), "s": (38, 40, 37), "h": (42, 22, 44, 51, 59, 53), "o": (46, 26)}
# The card families' drum feels, from the genres whose drummers play them. Four-on-the-floor families keep their kick
# and take the idiom's snare ghosts and hats; half-time families move the backbeat to beat 3.
GENRES = ["funk", "hiphop", "rock", "pop", "soul", "reggae", "latin", "afrobeat", "neworleans", "dance", "punk", "blues", "gospel", "country"]


def notes(path):
    """(tick, pitch, velocity) for every note-on; ticks per beat."""
    b = open(path, "rb").read()
    ntrk, div = struct.unpack(">HH", b[10:14])
    out, pos = [], 14
    while pos + 8 <= len(b):
        tag, ln = b[pos:pos + 4], struct.unpack(">I", b[pos + 4:pos + 8])[0]
        pos += 8
        if tag == b"MTrk":
            d, i, t, run = b[pos:pos + ln], 0, 0, 0

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
                    i += 2; n = vlq(); i += n; continue
                if s in (0xf0, 0xf7):
                    i += 1; n = vlq(); i += n; continue
                if s & 0x80:
                    run = s; i += 1
                k = run & 0xf0
                n = 1 if k in (0xc0, 0xd0) else 2
                a = d[i:i + n]; i += n
                if k == 0x90 and len(a) == 2 and a[1] > 0:
                    out.append((t, a[0], a[1]))
        pos += ln
    return sorted(out), div


def bars(path):
    ns, div = notes(path)
    six = div / 4.0
    grid = collections.defaultdict(lambda: {l: ["."] * 16 for l in LANES})
    for t, p, v in ns:
        for l, ps in LANES.items():
            if p in ps:
                s = int(round(t / six))
                cell = grid[s // 16][l]
                c = "o" if v < GHOST or p == 37 else "x"
                if cell[s % 16] != "x":
                    cell[s % 16] = c
    for b in sorted(grid):
        g = grid[b]
        if "x" in g["k"] and ("x" in g["s"] or "x" in g["h"]):
            yield tuple("".join(g[l]) for l in LANES)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.expanduser("~/Infinite/datasets/midi/data/groove_drumset/groove"))
    ap.add_argument("--min-files", type=int, default=3)
    ap.add_argument("--per-genre", type=int, default=40)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--stats", action="store_true")
    a = ap.parse_args()
    # Three layers, counted apart: the skeleton (kick and snare hits), the hats (closed or ride, and open), and the
    # snare's ghost notes. A whole bar almost never repeats exactly across drummers; each layer does.
    layer = {n: collections.defaultdict(lambda: collections.defaultdict(set)) for n in ("skel", "hats", "ghost")}
    total = collections.Counter()
    for f in sorted(glob.glob(os.path.join(a.root, "**", "*.mid"), recursive=True)):
        m = re.match(r"\d+_([a-z]+)[-_a-z]*_(\d+)_(beat|fill)_(\d+-\d+)", os.path.basename(f))
        if not m or m.group(3) != "beat" or m.group(4) != "4-4" or m.group(1) not in GENRES:
            continue
        g = m.group(1)
        for k, sn, h, o in bars(f):
            hits = lambda lane: "".join("x" if c == "x" else "." for c in lane)
            if k[0] == "x" and "x" in sn: # a skeleton starts on the one and has a backbeat; others are offset bars
                layer["skel"][g][(hits(k), hits(sn))].add(f)
            if "x" in h or "x" in o:
                layer["hats"][g][(h, o)].add(f)
            ghosts = "".join("o" if c == "o" else "." for c in sn)
            if "o" in ghosts:
                layer["ghost"][g][(ghosts,)].add(f)
            total[g] += 1
    out = {n: [] for n in layer}
    for gi, g in enumerate(GENRES):
        row = []
        for n in layer:
            ids = sorted(((len(fs), key) for key, fs in layer[n][g].items() if len(fs) >= a.min_files), reverse=True)[:a.per_genre]
            out[n] += [(gi, c, key) for c, key in ids]
            row.append("%s %3d" % (n, len(ids)))
        if a.stats:
            print("%-11s bars %5d  %s" % (g, total[g], "  ".join(row)))
    lines = ["// Generated by tools/universe/groove_bank.py: drum idioms per genre from Groove MIDI (Magenta, CC BY 4.0;",
             "// Gillick, Roberts, Engel, Eck, Bamman 2019). Each figure is played exactly by at least %d different" % a.min_files,
             "// performances; a groove is a skeleton, hats and ghosts of one genre put together, never one drummer's bar.",
             "// x a hit, o a ghost (velocity under %d), . a rest. Do not edit by hand: rerun the tool." % GHOST,
             "static const char* const kGrooveGenres[] = { %s };" % ", ".join('"%s"' % g for g in GENRES),
             "struct GrooveSkeleton { int genre, files; const char* k; const char* s; };",
             "struct GrooveHats { int genre, files; const char* h; const char* o; };",
             "struct GrooveGhosts { int genre, files; const char* s; };",
             "static const GrooveSkeleton kGrooveSkeletons[] = {"]
    lines += ['   { %d, %d, "%s", "%s" },' % (gi, c, *key) for gi, c, key in out["skel"]] + ["};", "static const GrooveHats kGrooveHats[] = {"]
    lines += ['   { %d, %d, "%s", "%s" },' % (gi, c, *key) for gi, c, key in out["hats"]] + ["};", "static const GrooveGhosts kGrooveGhosts[] = {"]
    lines += ['   { %d, %d, "%s" },' % (gi, c, *key) for gi, c, key in out["ghost"]] + ["};"]
    open(a.out, "w").write("\n".join(lines) + "\n")
    print("%s -> %s" % (", ".join("%d %s" % (len(v), n) for n, v in out.items()), os.path.relpath(a.out)))


if __name__ == "__main__":
    main()
