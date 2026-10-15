#!/usr/bin/env python3
"""Writes the bundled note pattern bank (assets/note-patterns/<pack>/*.mid).

Every pattern here is written for Infinite-Turbo and released under MIT. The
"Classical themes", "Classical figures" and "Folk and traditional" packs are
hand transcriptions of public domain works (composers who died more than 70
years ago, or traditional tunes); the other packs are original patterns in
common styles. Each file is one loop with a key signature, so the Pattern
Player lands the key's tonic on the chord root (the global key when nothing
is played into it).
Run: python scripts/make_note_patterns.py
"""
import os
import re
import struct

PPQ = 480
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "note-patterns")
COUNT = {}

NOTE_RE = re.compile(r"^([A-Ga-g])([#b]?)(-?\d)$")
SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def key_of(name):
    m = NOTE_RE.match(name)
    if not m:
        raise ValueError("bad note " + name)
    k = SEMI[m.group(1).upper()] + (1 if m.group(2) == "#" else -1 if m.group(2) == "b" else 0)
    return k + 12 * (int(m.group(3)) + 1)


def varlen(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, (v & 0x7F) | 0x80)
        v >>= 7
    return bytes(out)


def mel(spec, unit=0.25, start=0.0, vel=96, legato=0.92):
    """Space separated tokens: NOTE[/len][!]  or  N1+N2+N3[/len][!]  or  r[/len].
    len is in units (default 1); ! accents. Returns (notes, end_beat)."""
    t = start
    out = []
    for tok in spec.split():
        if tok == "|":
            continue
        acc = tok.endswith("!")
        if acc:
            tok = tok[:-1]
        length = 1.0
        if "/" in tok:
            tok, l = tok.split("/")
            length = float(l)
        d = length * unit
        if tok != "r":
            v = min(127, vel + 22) if acc else vel
            for n in tok.split("+"):
                out.append((t, d * legato, key_of(n), v))
        t += d
    return out, t


def write(pack, name, notes, ts=(4, 4), key=(0, 0), bpm=110, beats=None):
    """notes: list of (beat, dur, key, vel). key = (sharps, minor)."""
    bpb = 4.0 * ts[0] / ts[1]
    end = max(b + d for b, d, _, _ in notes)
    if beats is None:
        beats = end
    bars = max(1, int(-(-round(beats * 1000) // round(bpb * 1000))))
    total = bars * bpb
    ev = []
    for b, d, k, v in notes:
        if b >= total - 1e-9:
            continue
        on = round(b * PPQ)
        off = max(on + 1, round(min(b + d, total) * PPQ))
        ev.append((on, 1, bytes([0x90, k, v])))
        ev.append((off, 0, bytes([0x80, k, 0])))
    ev.sort(key=lambda e: (e[0], e[1]))
    trk = bytearray()
    title = name.encode("utf-8")
    trk += varlen(0) + bytes([0xFF, 0x03]) + varlen(len(title)) + title
    us = round(60000000 / bpm)
    trk += varlen(0) + bytes([0xFF, 0x51, 3, (us >> 16) & 255, (us >> 8) & 255, us & 255])
    dd = {2: 1, 4: 2, 8: 3, 16: 4}[ts[1]]
    trk += varlen(0) + bytes([0xFF, 0x58, 4, ts[0], dd, 24, 8])
    trk += varlen(0) + bytes([0xFF, 0x59, 2, key[0] & 0xFF, 1 if key[1] else 0])
    last = 0
    for t, _, data in ev:
        trk += varlen(t - last) + data
        last = t
    endt = max(last, round(total * PPQ))
    trk += varlen(endt - last) + bytes([0xFF, 0x2F, 0])
    data = b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ) + b"MTrk" + struct.pack(">I", len(trk)) + bytes(trk)
    folder = os.path.join(ROOT, pack)
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, name + ".mid"), "wb") as f:
        f.write(data)
    COUNT[pack] = COUNT.get(pack, 0) + 1


def m(spec, **kw):
    return mel(spec, **kw)[0]


def cat(*parts):
    out = []
    for p in parts:
        out += p
    return out


def comp(hits, chords, bars, unit=0.25, vel=84, bar_units=16, legato=0.9):
    """hits: list of (pos, len) in units within one bar (or a list per bar,
    cycled); chords: list of chord strings ("C4+E4+G4"), one per bar, cycled."""
    out = []
    for b in range(bars):
        h = hits[b % len(hits)] if hits and isinstance(hits[0], list) else hits
        ch = chords[b % len(chords)]
        for pos, ln in h:
            for n in ch.split("+"):
                out.append((b * bar_units * unit + pos * unit, ln * unit * legato, key_of(n), vel))
    return out


def arp(order, chords, bars, unit=0.25, vel=88, per_bar=16, legato=0.85):
    """order: indexes into each bar's chord (list of note names)."""
    out = []
    for b in range(bars):
        ch = chords[b % len(chords)].split("+")
        for i in range(per_bar):
            idx = order[i % len(order)]
            if idx is None:
                continue
            out.append((b * per_bar * unit + i * unit, unit * legato, key_of(ch[idx % len(ch)]) + 12 * (idx // len(ch)), vel))
    return out
# ============================================================ Classical themes
P = "Classical themes"
write(P, "Ode to Joy (Beethoven)", m(
    "F#4 F#4 G4 A4 | A4 G4 F#4 E4 | D4 D4 E4 F#4 | F#4/1.5 E4/0.5 E4/2 | "
    "F#4 F#4 G4 A4 | A4 G4 F#4 E4 | D4 D4 E4 F#4 | E4/1.5 D4/0.5 D4/2", unit=1.0), key=(2, 0), bpm=120)
write(P, "Fur Elise (Beethoven)", m(
    "E5 D#5 E5 B4 D5 C5 | A4/2 r C4 E4 A4 | B4/2 r E4 G#4 B4 | C5/2 r E4 E5 D#5 | "
    "E5 D#5 E5 B4 D5 C5 | A4/2 r C4 E4 A4 | B4/2 r E4 C5 B4 | A4/2 r r E5 D#5", unit=0.25),
    ts=(3, 8), key=(0, 1), bpm=72)
write(P, "Eine kleine Nachtmusik (Mozart)", m(
    "G4/2 r D4 G4/2 r D4 | G4 D4 G4 B4 D5/4 | C5/2 r A4 C5/2 r A4 | C5 A4 F#4 A4 D4/4", unit=0.5),
    key=(1, 0), bpm=128)
write(P, "Rondo alla turca (Mozart)", m(
    "B4 A4 G#4 A4 C5/4 | D5 C5 B4 C5 E5/4 | F5 E5 D#5 E5 B5 A5 G#5 A5 | B5 A5 G#5 A5 C6/4", unit=0.25),
    ts=(2, 4), key=(0, 1), bpm=120)
write(P, "Sonata facile opening (Mozart)", cat(
    m("C5/4 E5/2 G5/2 | B4/3 C5/0.5 D5/0.5 C5/4", unit=0.5),
    m("C3 G3 E3 G3 C3 G3 E3 G3 | D3 G3 F3 G3 C3 G3 E3 G3", unit=0.5, vel=70)), key=(0, 0), bpm=120)
write(P, "Minuet in G (Petzold)", m(
    "D5/2 G4 A4 B4 C5 | D5/2 G4/2 G4/2 | E5/2 C5 D5 E5 F#5 | G5/2 G4/2 G4/2 | "
    "C5/2 D5 C5 B4 A4 | B4/2 C5 B4 A4 G4 | F#4/2 G4 A4 B4 G4 | A4/6", unit=0.5),
    ts=(3, 4), key=(1, 0), bpm=112)
write(P, "Invention in C (Bach)", m(
    "r C4 D4 E4 F4 D4 E4 C4 G4/2 C5/2 B4/2 C5/2 | D5 G4 A4 B4 C5 A4 B4 G4 D5/2 G5/2 F5/2 G5/2", unit=0.25),
    key=(0, 0), bpm=84)
write(P, "Toccata in D minor (Bach)", cat(
    m("A5 G5 A5/6 r/2 G5 F5 E5 D5 C#5/4 D5/8", unit=0.25, vel=110),
    m("A4 G4 A4/6 r/2 E4 F4 C#4 D4/12", unit=0.25, start=8.0, vel=100)), key=(-1, 1), bpm=70)
write(P, "Jesu joy triplets (Bach)", m(
    "G4 A4 B4 D5 C5 C5 E5 D5 D5 G5 F#5 G5 D5 B4 G4 A4 B4 C5 D5 E5 D5 C5 B4 A4", unit=1.0 / 3.0),
    key=(1, 0), bpm=96)
write(P, "Canon in D theme (Pachelbel)", m(
    "F#5/2 E5/2 D5/2 C#5/2 B4/2 A4/2 B4/2 C#5/2", unit=1.0), key=(2, 0), bpm=70)
write(P, "Canon in D eighths (Pachelbel)", m(
    "D5 C#5 D5 D4 C#4 A4 E4 F#4 | D4 D5 C#5 B4 C#5 F#5 A5 B5 | G5 F#5 E5 G5 F#5 E5 D5 C#5 | B4 A4 G4 F#4 E4 G4 F#4 E4",
    unit=0.5), key=(2, 0), bpm=70)
write(P, "Hall of the Mountain King (Grieg)", m(
    "B2 C#3 D3 E3 F#3 D3 F#3/2 | F3 C#3 F3/2 E3 C3 E3/2 | B2 C#3 D3 E3 F#3 D3 F#3 B3 | A3 F#3 D3 F#3 A3/4",
    unit=0.5), key=(2, 1), bpm=120)
write(P, "Morning Mood (Grieg)", m(
    "B5 G#5 F#5 E5 F#5 G#5 | B5 G#5 F#5 E5 F#5/0.5 G#5/0.5 F#5/0.5 G#5/0.5 | B5 G#5 B5 C#6 G#5 C#6 | B5 G#5 F#5 E5/3",
    unit=0.5), ts=(6, 8), key=(4, 0), bpm=80)
write(P, "Spring (Vivaldi)", m(
    "G#5/2 G#5/2 G#5/2 F#5 E5 | B5/6 B5 A5 | G#5/2 G#5/2 G#5/2 F#5 E5 | B5/6 B5 A5", unit=0.5),
    key=(4, 0), bpm=104)
write(P, "Fifth Symphony motif (Beethoven)", m(
    "r G4! G4! G4! | Eb4/4! | r F4! F4! F4! | D4/4!", unit=0.5), ts=(2, 4), key=(-3, 1), bpm=108)
write(P, "The Entertainer (Joplin)", m(
    "E5 C6/2 E5 C6/2 E5 C6/5 C6 D6 D#6 | E6 C6 D6 E6/2 B5 D6/2 C6/5 r r D5 D#5", unit=0.25),
    ts=(2, 4), key=(0, 0), bpm=96, beats=8.0)
write(P, "Gymnopedie melody (Satie)", m(
    "F#5 A5 G5 | F#5 C#5 B4 | C#5 D5 A4 | F#4/3", unit=1.0), ts=(3, 4), key=(2, 0), bpm=72)
write(P, "Lullaby (Brahms)", m(
    "G4/3 E4 E4/2 | G4/4 E4 G4 | C5/2 B4/3 A4 | A4/2 G4/2 D4 E4 | "
    "F4/2 D4/2 D4 E4 | F4/4 D4 F4 | B4 A4 G4/2 B4/2 | C5/4 E4 E4", unit=0.5), ts=(3, 4), key=(0, 0), bpm=80)
write(P, "Can-can galop (Offenbach)", m(
    "C5/2 D5 F5 E5 D5 G5/2 G5/2 G5 A5 E5 F5 D5/2 D5/2 D5 F5 E5 D5 C5 C6 B5 A5 G5 F5 E5 D5 C5/2", unit=0.5),
    ts=(2, 4), key=(0, 0), bpm=140)
write(P, "Funeral march (Chopin)", m(
    "Bb3/4 Bb3/3 Bb3/1 Bb3/8 | Db4/3 C4/1 C4/3 Bb3/1 Bb3/3 A3/1 Bb3/4", unit=0.25), key=(-5, 1), bpm=60)
write(P, "Bumblebee run (Rimsky-Korsakov)", m(
    "E6 D#6 D6 C#6 D6 C#6 C6 B5 | C6 B5 A#5 A5 G#5 G5 F#5 F5 | E5 D#5 D5 C#5 D5 C#5 C5 B4 | C5 B4 A#4 A4 G#4 G4 F#4 F4",
    unit=0.25, legato=0.8), ts=(2, 4), key=(0, 1), bpm=150)
write(P, "Dies irae (chant)", m("F4 E4 F4 D4 E4 C4 D4/2", unit=1.0), key=(-1, 1), bpm=70)

# ============================================================ Classical figures
P = "Classical figures"
pre = []
for bar, ch in enumerate(["C4 E4 G4 C5 E5", "C4 D4 A4 D5 F5", "B3 D4 G4 D5 F5", "C4 E4 G4 C5 E5"]):
    k = ch.split()
    for half in range(2):
        t = bar * 4.0 + half * 2.0
        pre.append((t, 1.95, key_of(k[0]), 80))
        pre.append((t + 0.25, 1.7, key_of(k[1]), 76))
        for i, idx in enumerate([2, 3, 4, 2, 3, 4]):
            pre.append((t + 0.5 + i * 0.25, 0.23, key_of(k[idx]), 84))
write(P, "Prelude in C arpeggio (Bach)", pre, key=(0, 0), bpm=72)
write(P, "Cello suite prelude (Bach)", m(
    "G2 D3 B3 A3 B3 D3 B3 D3 G2 D3 B3 A3 B3 D3 B3 D3 | G2 E3 C4 B3 C4 E3 C4 E3 G2 E3 C4 B3 C4 E3 C4 E3 | "
    "G2 F#3 C4 B3 C4 F#3 C4 F#3 G2 F#3 C4 B3 C4 F#3 C4 F#3 | G2 G3 B3 A3 B3 G3 B3 G3 G2 G3 B3 A3 B3 G3 B3 F#3",
    unit=0.25), key=(1, 0), bpm=80)
moon = []
trip = [["G#3 C#4 E4"] * 4, ["G#3 C#4 E4"] * 4, ["A3 C#4 E4"] * 2 + ["A3 D4 F#4"] * 2,
        ["G#3 C4 F#4", "G#3 C#4 E4", "G#3 C#4 D#4", "G3 C4 D#4"]]
bass = [[("C#2+C#3", 4)], [("B1+B2", 4)], [("A1+A2", 2), ("F#1+F#2", 2)], [("G#1+G#2", 4)]]
for bar in range(4):
    for beat, grp in enumerate(trip[bar]):
        for i, n in enumerate(grp.split()):
            moon.append((bar * 4 + beat + i / 3.0, 0.31, key_of(n), 70))
    t = bar * 4.0
    for ch, ln in bass[bar]:
        for n in ch.split("+"):
            moon.append((t, ln * 0.98, key_of(n), 76))
        t += ln
write(P, "Moonlight triplets (Beethoven)", moon, key=(4, 1), bpm=56)
gym = []
for i, (b, ch) in enumerate([("G2", "B3+D4+F#4"), ("D2", "A3+C#4+F#4")] * 2):
    gym.append((i * 3.0, 0.95, key_of(b), 72))
    for n in ch.split("+"):
        gym.append((i * 3.0 + 1.0, 1.9, key_of(n), 62))
write(P, "Gymnopedie left hand (Satie)", gym, ts=(3, 4), key=(2, 0), bpm=72)
write(P, "Canon in D bass (Pachelbel)", m("D3 A2 B2 F#2 G2 D2 G2 A2", unit=1.0, legato=0.97), key=(2, 0), bpm=70)
write(P, "Alberti bass (Mozart style)", m(
    "C3 G3 E3 G3 C3 G3 E3 G3 | D3 G3 F3 G3 D3 G3 F3 G3 | C3 G3 E3 G3 C3 A3 F3 A3 | B2 G3 D3 G3 C3 G3 E3 G3",
    unit=0.5, vel=72), key=(0, 0), bpm=120)
write(P, "Stride left hand (ragtime)", comp([(0, 3.5)], ["C2"], 2) + comp(
    [(4, 3), (12, 3)], ["E3+G3+C4", "F3+A3+C4"], 2) + comp([(8, 3.5)], ["G1", "C2"], 2), key=(0, 0), bpm=100)
write(P, "Waltz oom-pah-pah", comp([(0, 3.5)], ["C2", "G1"], 4, bar_units=12) +
      comp([(4, 3), (8, 3)], ["E3+G3+C4", "D3+F3+B3"], 4, bar_units=12), ts=(3, 4), key=(0, 0), bpm=160)
write(P, "Bumblebee chromatic turn (Rimsky-Korsakov)", m(
    "A5 G#5 G5 F#5 F5 F#5 G5 G#5 | A5 G#5 G5 F#5 F5 E5 F5 E5", unit=0.25, legato=0.8), ts=(2, 4), key=(0, 1), bpm=150)

# ========================================================== Folk and traditional
P = "Folk and traditional"
write(P, "Twinkle twinkle (trad.)", m("C4 C4 G4 G4 A4 A4 G4/2 F4 F4 E4 E4 D4 D4 C4/2", unit=1.0), key=(0, 0), bpm=110)
write(P, "Frere Jacques (trad.)", m(
    "C4 D4 E4 C4 C4 D4 E4 C4 E4 F4 G4/2 E4 F4 G4/2 "
    "G4/0.5 A4/0.5 G4/0.5 F4/0.5 E4 C4 G4/0.5 A4/0.5 G4/0.5 F4/0.5 E4 C4 C4 G3 C4/2 C4 G3 C4/2", unit=1.0),
    key=(0, 0), bpm=110)
write(P, "Korobeiniki (trad.)", m(
    "E5/2 B4 C5 D5/2 C5 B4 | A4/2 A4 C5 E5/2 D5 C5 | B4/3 C5 D5/2 E5/2 | C5/2 A4/2 A4/4 | "
    "r D5/2 F5 A5/2 G5 F5 | E5/3 C5 E5/2 D5 C5 | B4/2 B4 C5 D5/2 E5/2 | C5/2 A4/2 A4/4", unit=0.5),
    key=(0, 1), bpm=140)
write(P, "Greensleeves (trad.)", m(
    "C5/2 D5 E5/1.5 F5/0.5 E5 | D5/2 B4 G4/1.5 A4/0.5 B4 | C5/2 A4 A4/1.5 G#4/0.5 A4 | B4/2 G#4 E4/2 A4 | "
    "C5/2 D5 E5/1.5 F5/0.5 E5 | D5/2 B4 G4/1.5 A4/0.5 B4 | C5/1.5 B4/0.5 A4 G#4/1.5 F#4/0.5 G#4 | A4/5 A4",
    unit=0.5), ts=(6, 8), key=(0, 1), bpm=96)
write(P, "Amazing grace (trad.)", m(
    "G4/4 B4 G4 | B4/4 A4/2 | G4/4 E4/2 | D4/4 D4/2 | G4/4 B4 G4 | B4/4 A4/2 | D5/6 | D5/4 D4/2", unit=0.5),
    ts=(3, 4), key=(1, 0), bpm=80)
write(P, "Auld lang syne (trad.)", m(
    "F4/3 F4 F4/2 A4/2 | G4/3 F4 G4/2 A4/2 | F4/3 F4 A4/2 C5/2 | D5/6 D5/2 | "
    "C5/3 A4 A4/2 F4/2 | G4/3 F4 G4/2 A4/2 | F4/3 D4 D4/2 C4/2 | F4/6 C4/2", unit=0.5), key=(-1, 0), bpm=90)
write(P, "When the saints (trad.)", m(
    "r C4 E4 F4 | G4/4 | r C4 E4 F4 | G4/4 | r C4 E4 F4 | G4/2 E4/2 | C4/2 E4/2 | D4/4", unit=1.0),
    key=(0, 0), bpm=150)
write(P, "Sakura sakura (trad.)", m(
    "A4 A4 B4/2 | A4 A4 B4/2 | A4 B4 C5 B4 | A4 B4/0.5 A4/0.5 F4/2 | E4 C4 E4 F4 | E4 E4/0.5 C4/0.5 B3/2", unit=1.0),
    key=(0, 1), bpm=76)
write(P, "Scarborough fair (trad.)", m(
    "D4/2 D4 | A4/2 A4 | E4/1.5 F4/0.5 E4 | D4/3 | r A4 C5 | D5/2 C5 | A4 B4 G4 | A4/3", unit=1.0),
    ts=(3, 4), key=(-1, 1), bpm=100)
rs = []
for bar, ch in enumerate(["A2 E3 A3 C4", "C3 G3 C4 E4", "D3 A3 D4 F#4", "F2 C3 F3 A3",
                          "A2 E3 A3 C4", "C3 G3 C4 E4", "E2 B2 E3 G#3", "E2 B2 E3 G#3"]):
    k = ch.split()
    for i, idx in enumerate([0, 1, 2, 3, 2, 1]):
        rs.append((bar * 3.0 + i * 0.5, 0.48 if i else 1.4, key_of(k[idx]), 84 if i else 96))
write(P, "Rising sun arpeggio (trad.)", rs, ts=(6, 8), key=(0, 1), bpm=72)
# ================================================================== Basslines
P = "Basslines"
write(P, "Walking bass ii-V-I-VI", m("D2 F2 A2 G#2 | G2 B2 D3 C#3 | C3 B2 A2 G2 | A2 C#3 E2 Eb2", unit=1.0, legato=0.95),
      key=(0, 0), bpm=140)
boog = {"C": "C2 E2 G2 A2 Bb2 A2 G2 E2", "F": "F2 A2 C3 D3 Eb3 D3 C3 A2", "G": "G2 B2 D3 E3 F3 E3 D3 B2"}
notes, t = [], 0.0
for ch in "C C C C F F C C G F C G".split():
    nn, t = mel(boog[ch], unit=0.5, start=t, vel=96)
    notes += nn
write(P, "Boogie-woogie twelve bar", notes, key=(0, 0), bpm=150)
write(P, "Boogie-woogie bar", m(boog["C"], unit=0.5), key=(0, 0), bpm=150)
sh, t = [], 0.0
for ch in ["C", "C", "F", "C"]:
    for i, n in enumerate(boog[ch].split()):
        ln = 2.0 / 3.0 if i % 2 == 0 else 1.0 / 3.0
        sh.append((t, ln * 0.9, key_of(n), 100 if i % 2 == 0 else 80))
        t += ln
write(P, "Blues shuffle", sh, key=(0, 0), bpm=120)
write(P, "Funk octaves", m("C2! r C3 r r C2 C2 r Bb2 r C3 r G2 Bb2 C3 r", unit=0.25, legato=0.6), key=(0, 0), bpm=104)
write(P, "Funk sixteenths", m("C2! r r C2 r C3 Eb2 r C2 r Bb1 r C2 r G2 Bb2", unit=0.25, legato=0.5), key=(-3, 1), bpm=100)
write(P, "Disco octaves", m("C2 C3 C2 C3 C2 C3 C2 C3 | A1 A2 A1 A2 A1 A2 A1 A2", unit=0.5, legato=0.55), key=(0, 0), bpm=120)
write(P, "Soul groove", m("C2/1.5 r/0.5 C2 E2 G2 A2 G2 E2 | F2/1.5 r/0.5 F2 A2 C3 D3 C3 A2", unit=0.5, legato=0.8),
      key=(0, 0), bpm=100)
write(P, "Reggae one drop", m("r/4 C2/3 Eb2 G2/4 Bb1/2 C2/2 | r/4 F2/3 Ab2 C3/4 Eb2/2 C2/2", unit=0.25), key=(-3, 1), bpm=76)
write(P, "Dub bass", m("A1/6 r/2 C2/2 D2/2 E2/4 | A1/6 r/2 G1/2 E1/2 G1/4", unit=0.25), key=(0, 1), bpm=72)
write(P, "Tumbao", m("r/6 G2/6 C2/4 | r/6 C3/6 G2/4", unit=0.25, legato=0.97), key=(0, 0), bpm=180)
write(P, "Baiao", m("C2/3 G2 G2/4 C2/3 G2 E2/4", unit=0.25, legato=0.9), key=(0, 0), bpm=110)
write(P, "Bossa nova", m("C2/6 G1/2 G1/6 C2/2 | D2/6 A1/2 G1/6 G1/2", unit=0.25, legato=0.95), key=(0, 0), bpm=130)
write(P, "Samba", m("C2/3 G1 G1/3 C2 G1/3 G1 G1/2 C2/2", unit=0.25, legato=0.85), key=(0, 0), bpm=100)
write(P, "Cumbia", m("C2/4 r/4 G1/4 G1/2 G1/2 | F2/4 r/4 C2/4 C2/2 C2/2", unit=0.25, legato=0.8), key=(0, 0), bpm=95)
write(P, "Tango marcato", m("C2! C2 C2 B1/0.5 A1/0.5 | G1! G1 G1 A1/0.5 B1/0.5", unit=1.0, legato=0.5), key=(0, 1), bpm=116)
write(P, "Habanera", m("C2/3 C2 E2/2 G2/2 C2/3 C2 E2/2 G2/2", unit=0.25, legato=0.8), key=(0, 0), bpm=90)
write(P, "Dembow", m("C2/3 C2/3 C2/2 C2/3 C2/3 Eb2/2 | Ab1/3 Ab1/3 Ab1/2 Bb1/3 Bb1/3 Bb1/2", unit=0.25, legato=0.7),
      key=(-3, 1), bpm=95)
write(P, "Afrobeat", m("C2/2 r C2 Eb2/2 r/2 G2 r Bb2/2 G2/2 r/2 | C2/2 r C2 Eb2/2 r/2 F2 r G2/2 Eb2/2 r/2", unit=0.25,
      legato=0.7), key=(-3, 1), bpm=108)
write(P, "Deep house", m("r/2 C2 r r/2 C2/2 r/2 Eb2 r r/2 G1/2", unit=0.25, legato=0.85), key=(-3, 1), bpm=122)
write(P, "Techno rolling", m("r C2 C2 C2 r C2 C2 C2 r C2 C2 C2 r C2 C2 Eb2", unit=0.25, legato=0.5), key=(-3, 1), bpm=130)
write(P, "Psytrance gallop", m("r C2 C2 C2 " * 4, unit=0.25, legato=0.45), key=(-3, 1), bpm=142)
write(P, "Drum and bass", m("C2/6 r/2 Eb2/2 r/2 G1/4 | Ab1/8 G1/4 Bb1/4", unit=0.25, legato=0.97), key=(-3, 1), bpm=172)
write(P, "Rock eighths", m("C2 C2 C2 C2 C2 C2 C2 C2 | F2 F2 F2 F2 G2 G2 G2 G2", unit=0.5, legato=0.8), key=(0, 0), bpm=128)
write(P, "Punk", m("C2 C2 C2 C2 F2 F2 G2 G2", unit=0.5, legato=0.75), key=(0, 0), bpm=180)
write(P, "Motorik", m("C2 C2 G2 C2 C2 C2 G2 C3", unit=0.5, legato=0.7), key=(0, 0), bpm=130)
write(P, "Synthwave octaves", m("C2 C3 C2 C3 C2 C3 C2 C3 C2 C3 C2 C3 C2 C3 Bb1 Bb2 | "
      "Ab1 Ab2 Ab1 Ab2 Ab1 Ab2 Ab1 Ab2 Bb1 Bb2 Bb1 Bb2 Bb1 Bb2 Bb1 Bb2", unit=0.25, legato=0.6), key=(-3, 1), bpm=100)
write(P, "Trap 808", m("C2/6 r/2 C2/2 r/4 Eb2/2 | r/4 C2/4 G1/3 r Bb1/4", unit=0.25, legato=0.97), key=(-3, 1), bpm=140)
write(P, "Ska walk", m("C2 E2 G2 A2 | F2 A2 C3 A2 | G2 B2 D3 B2 | C2 G1 A1 B1", unit=1.0, legato=0.8), key=(0, 0), bpm=150)
write(P, "Latin bolero", m("C2/2 G1/1.5 C2/0.5 | F2/2 C2/1.5 F2/0.5 | G2/2 D2/1.5 G2/0.5 | C2/2 G1/2", unit=1.0, legato=0.9),
      key=(0, 0), bpm=84)
write(P, "Choro baixaria", m("r/8 G2 F2 E2 D2 C2 B1 A1 G1 | C2/4 E2/4 G2/4 E2/4", unit=0.25, legato=0.9), key=(0, 0), bpm=96)
# ============================================================= Latin and Brazil
P = "Latin and Brazil"
bossa_hits = [[(0, 2), (6, 2), (12, 2)], [(4, 2), (10, 2)]]
write(P, "Bossa nova comping", comp(bossa_hits, ["E3+G3+B3+D4", "C#3+G3+B3+E4", "C3+F3+A3+D4", "B2+F3+A3+D4"], 4, vel=72) +
      m("C2/8 G1/8 | A1/8 E2/8 | D2/8 A1/8 | G1/8 D2/8", unit=0.25, vel=84), key=(0, 0), bpm=130)
write(P, "Samba partido alto", comp([[(0, 2), (3, 2), (6, 2), (10, 2), (13, 2)], [(2, 2), (4, 2), (7, 2), (10, 2), (12, 2), (15, 1)]],
      ["E3+G3+C4", "F3+A3+D4"], 4, vel=78, legato=0.6), key=(0, 0), bpm=100)
write(P, "Baiao accordion", comp([(0, 3), (3, 1), (4, 3), (8, 3), (11, 1), (12, 3)], ["E3+G3+Bb3+C4", "F3+A3+C4+Eb4"], 2, vel=86,
      legato=0.8), key=(0, 0), bpm=112)
write(P, "Frevo stabs", comp([(0, 1), (2, 1), (3, 1), (6, 1), (8, 1), (10, 1), (11, 1), (14, 1)], ["E4+G4+C5", "D4+F4+B4"], 2,
      vel=100, legato=0.7), key=(0, 0), bpm=150)
write(P, "Son montuno", m("E5 C5 G4 E5 r C5 G4 E5 | F5 C5 A4 F5 r D5 B4 G5", unit=0.5, legato=0.6), key=(0, 0), bpm=180)
write(P, "Guajeo", m("C5 r E5 G5 r E5 C5 G4 | B4 r D5 F5 r D5 B4 G4", unit=0.5, legato=0.6), key=(0, 0), bpm=180)
write(P, "Cumbia offbeats", comp([(2, 1.5), (6, 1.5), (10, 1.5), (14, 1.5)], ["C4+E4+A4", "B3+D4+G4"], 2, vel=80, legato=0.6),
      key=(0, 1), bpm=95)
write(P, "Reggae skank", comp([(4, 1), (12, 1)], ["C4+E4+A4", "D4+F4+A4"], 2, vel=84, legato=0.5), key=(0, 1), bpm=76)
write(P, "Habanera chords", comp([(0, 3), (3, 1), (4, 2), (6, 2)] + [(8, 3), (11, 1), (12, 2), (14, 2)],
      ["C3+E3+A3", "B2+E3+G#3"], 2, vel=74), key=(0, 1), bpm=90)
write(P, "Reggaeton chords", comp([(0, 3), (3, 3), (6, 2), (8, 3), (11, 3), (14, 2)], ["C4+E4+A4", "C4+F4+A4", "C4+E4+G4", "B3+D4+G4"],
      4, vel=80, legato=0.7), key=(0, 1), bpm=95)
write(P, "Tango arrastre", comp([(0, 2), (4, 2), (8, 2), (12, 2)], ["A2+E3+A3+C4", "G#2+E3+B3+D4"], 2, vel=96, legato=0.45) +
      m("r/14 A1/2 | r/14 E1/2", unit=0.25, vel=110), key=(0, 1), bpm=116)
write(P, "Maracatu chords", comp([(0, 2), (3, 2), (6, 2), (10, 3), (14, 2)], ["A3+C4+E4", "G3+B3+D4"], 2, vel=88, legato=0.7),
      key=(0, 1), bpm=90)
write(P, "Afro-Cuban six eight", m("C5 C5 D5 C5 E5 D5 C5 D5 C5 C5 E5 G5", unit=0.5, legato=0.6), ts=(6, 8), key=(0, 0), bpm=100)

# ============================================================== Jazz and blues
P = "Jazz and blues"
write(P, "Charleston comping", comp([(0, 3), (6, 2)], ["F3+A3+C4+E4", "F3+A3+B3+E4", "E3+G3+B3+D4", "E3+G3+B3+D4"], 4, vel=78),
      key=(0, 0), bpm=150)
write(P, "Four on the floor comping", comp([(0, 1.5), (4, 1.5), (8, 1.5), (12, 1.5)], ["F3+A3+C4", "F3+B3+D4", "E3+G3+C4", "E3+G3+C4"],
      4, vel=70), key=(0, 0), bpm=170)
write(P, "Neo-soul chords", comp([(0, 6), (7, 2), (10, 5)], ["F3+A3+C4+E4+G4", "F3+B3+E4+A4", "E3+B3+D4+G4", "Eb3+A3+C#4+G4"],
      4, vel=70, legato=0.95), key=(0, 0), bpm=84)
write(P, "Rhodes ballad", comp([(0, 16)], ["E3+B3+D4+G4", "F#3+A3+C4+E4", "F3+A3+C4+E4", "F3+B3+D4+G4"], 4, vel=64, legato=0.98),
      key=(0, 0), bpm=70)
write(P, "Blues lick", m("C5/0.67 Eb5/0.33 F5/0.67 F#5/0.33 G5/1 Bb5/0.67 G5/0.33 | F5/0.67 Eb5/0.33 C5/2 r/1", unit=1.0,
      legato=0.85), key=(-3, 1), bpm=96)
write(P, "Bebop line", m("C5 B4 Bb4 A4 G4 F4 E4 D4 | C#4 E4 G4 Bb4 A4 F4 D4 C4", unit=0.5, legato=0.75), key=(0, 0), bpm=200)
write(P, "ii-V-I line", m("D4 F4 A4 C5 B4 G4 F4 D4 | E4/4 G4 E4 D4 C4", unit=0.5, legato=0.8), key=(0, 0), bpm=160)
write(P, "Minor blues riff", m("C3/2 Eb3 F3 F#3 G3/2 Bb3 | C4/2 Bb3 G3 F3 Eb3/2 C3", unit=0.5, legato=0.8), key=(-3, 1), bpm=110)
write(P, "Gospel walk-up", comp([(0, 3.5)], ["E3+G3+C4", "F3+A3+C4", "F#3+A3+C4+D4", "G3+B3+D4+F4"], 4,
      vel=86, bar_units=4), key=(0, 0), bpm=90)

# ================================================================ Electronic
P = "Electronic"
write(P, "Trance gate", comp([(i, 0.6) for i in range(16) if i % 4 != 3], ["A3+C4+E4", "F3+A3+C4", "C4+E4+G4", "G3+B3+D4"],
      4, vel=90, legato=0.9), key=(0, 1), bpm=138)
write(P, "House organ stabs", comp([(2, 1), (6, 1), (9, 1), (14, 1)], ["A3+C4+E4+G4", "D4+F4+A4+C5"], 2, vel=94, legato=0.7),
      key=(0, 1), bpm=124)
write(P, "Detroit minor nine", comp([(0, 1.5), (3, 1.5), (6, 2), (10, 1.5), (13, 2)], ["Eb3+G3+Bb3+D4", "C3+Eb3+G3+Bb3+D4"],
      2, vel=88, legato=0.8), key=(-3, 1), bpm=126)
write(P, "Synthwave arp", arp([0, 1, 2, 3, 2, 1, 2, 3], ["C3+G3+C4+E4", "A2+E3+A3+C4", "F2+C3+F3+A3", "G2+D3+G3+B3"], 4),
      key=(0, 0), bpm=100)
write(P, "Berlin school sequence", m(
    "A2 A3 E3 A3 C4 A3 E3 A3 G2 G3 D3 G3 B3 G3 D3 G3 | F2 F3 C3 F3 A3 F3 C3 F3 E2 E3 B2 E3 G#3 E3 B2 E3",
    unit=0.25, legato=0.6), key=(0, 1), bpm=112)
write(P, "Chiptune arp", arp([0, 1, 2, 3], ["C4+E4+G4+C5", "A3+C4+E4+A4", "F3+A3+C4+F4", "G3+B3+D4+G4"], 4, unit=0.125, per_bar=32,
      legato=0.7), key=(0, 0), bpm=140)
write(P, "Minimal motif", m("C4 r r Eb4 r r G4 r r r Bb3 r r C4 r r", unit=0.25, legato=0.5), key=(-3, 1), bpm=124)
write(P, "Dub techno chord", comp([(0, 2), (3, 1), (6, 1), (10, 1), (13, 1)], ["C3+Eb3+G3+Bb3"], 1, vel=80, legato=0.9),
      key=(-3, 1), bpm=118)
write(P, "Ambient pad", comp([(0, 32)], ["A2+E3+B3+C4", "F2+C3+G3+A3"], 2, vel=60, bar_units=32, legato=0.99),
      key=(0, 1), bpm=80)
write(P, "Lo-fi chords", [(b + 0.04, d, k, v) for b, d, k, v in comp([(0, 7), (8, 6)],
      ["F3+A3+C4+E4", "E3+G3+B3+D4", "D3+F3+A3+C4", "C3+E3+G3+B3"], 4, vel=66)], key=(0, 0), bpm=78)
write(P, "Garage organ", comp([(0, 1), (3, 1), (7, 1), (10, 1), (12, 1)], ["F3+A3+C4+E4", "E3+G3+C4+D4"], 2, vel=88, legato=0.7),
      key=(0, 0), bpm=132)
write(P, "Minimalist alternation", m(
    "C4 E4 G4 C4 E4 G4 C4 E4 G4 C4 E4 G4 | A3 C4 E4 A3 C4 E4 A3 C4 E4 A3 C4 E4", unit=1.0 / 3.0, legato=0.8),
    key=(0, 0), bpm=96)
write(P, "Pulse in fifths", m("C4 G4 C4 G4 C4 G4 D5 G4 | C4 G4 C4 A4 C4 G4 E5 G4", unit=0.5, legato=0.5), key=(0, 0), bpm=120)
write(P, "Electro riff", m("C3! r C3 r Eb3 r r C3 r r G3 r F3 r Eb3 r", unit=0.25, legato=0.5), key=(-3, 1), bpm=118)
# ============================================================== Turbo examples
P = "Turbo examples"


def seq(keys, step=0.25, dur=None, vel=96, start=0.0):
    return [(start + i * step, dur if dur else step * 0.9, k, vel) for i, k in enumerate(keys) if k is not None]


C = 48
prog = [[48, 52, 55, 60], [43, 47, 50, 55], [45, 48, 52, 57], [41, 45, 48, 53]]
notes = []
for bar, ch in enumerate(prog):
    notes += seq([ch[i] for i in [0, 1, 2, 3, 2, 1, 2, 3, 0, 1, 2, 3, 2, 3, 1, 2]], start=bar * 4.0, vel=90)
write(P, "Arp phrase I-V-vi-IV", notes, key=(0, 0))
pent = [C, C + 3, C + 5, C + 7, C + 10, C + 12]
hook = [pent[0], None, pent[2], pent[3], None, pent[4], pent[3], pent[2],
        pent[0], None, pent[1], pent[2], pent[3], None, pent[1], None]
write(P, "Pentatonic hook", seq(hook, step=0.5, dur=0.42, vel=100), key=(-3, 1))
write(P, "House bass", seq([None, None, C, None, None, None, C + 12, None, None, None, C, None, None, C + 7, C + 10, C + 12],
                           vel=110), key=(-3, 1))
acid = [(C, 120), (C + 12, 90), (None, 0), (C, 90), (C + 3, 120), (C, 90), (C + 15, 90), (C + 12, 120),
        (C, 90), (None, 0), (C + 7, 120), (C + 10, 90), (C, 90), (C + 12, 120), (C + 3, 90), (C + 5, 90)]
write(P, "Acid line", [(i * 0.25, 0.2, k, v) for i, (k, v) in enumerate(acid) if k is not None], key=(-3, 1))
write(P, "Maj7 two octaves", seq([C, C + 4, C + 7, C + 11, C + 12, C + 16, C + 19, C + 23, C + 24, C + 23, C + 19, C + 16,
                                  C + 12, C + 11, C + 7, C + 4], vel=88), key=(0, 0))
w = []
for i in range(4):
    base = i * 3.0
    w += [(base, 0.9, C, 100), (base + 1, 0.4, C + 16, 70), (base + 1, 0.4, C + 19, 70),
          (base + 2, 0.4, C + 16, 70), (base + 2, 0.4, C + 19, 70)]
write(P, "Waltz figure", w, ts=(3, 4), key=(0, 0))
write(P, "Trance lead", seq([C + 12, C + 12, C + 19, C + 12, C + 24, C + 12, C + 19, C + 22,
                             C + 12, C + 12, C + 19, C + 12, C + 24, C + 22, C + 19, C + 17], dur=0.18, vel=100), key=(-3, 1))
al = [C, None, None, C, None, None, C + 7, None, None, C + 10, None, C + 12, None, None, C + 7, None]
write(P, "Syncopated bass", seq(al * 2, dur=0.3, vel=105), key=(-3, 1))
write(P, "Bossa cell", seq([C + 16, None, C + 14, C + 12, None, C + 11, C + 12, None,
                            C + 9, None, C + 11, None, C + 12, C + 14, None, None], step=0.5, dur=0.45, vel=85), key=(0, 0))
ds = []
for i in range(4):
    t = i * 1.0 + 0.5
    ds += [(t, 0.2, C + 12, 95), (t, 0.2, C + 15, 95), (t, 0.2, C + 19, 95)]
write(P, "Dub stabs", ds, key=(-3, 1))

CREDITS = """Infinite-Turbo note pattern bank
================================

Every file here was written for Infinite-Turbo and is released under the MIT
licence (see LICENSE at the top of the repository).

Classical themes, Classical figures, Folk and traditional
   Hand transcriptions of public domain works: the composers died more than 70
   years ago (Bach, Beethoven, Brahms, Chopin, Grieg, Joplin, Mozart, Offenbach,
   Pachelbel, Petzold, Rimsky-Korsakov, Satie, Vivaldi) or the tunes are
   traditional. Short excerpts, arranged as loops.

Basslines, Latin and Brazil, Jazz and blues, Electronic, Turbo examples
   Original patterns in common styles.

Each file carries a key signature: the Pattern Player puts the key's tonic on
the chord root (the global key when nothing is played into it). Regenerate
with scripts/make_note_patterns.py.
"""
os.makedirs(ROOT, exist_ok=True)
with open(os.path.join(ROOT, "CREDITS.txt"), "w", newline="\n") as f:
    f.write(CREDITS)
print(sum(COUNT.values()), "patterns:", ", ".join("%s %d" % kv for kv in COUNT.items()))
