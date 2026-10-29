#!/usr/bin/env python3
"""Chord progressions and 16-step lines from labelled note data, for the keys, plucks, leads and bass parts (owner's
review, 2026-10-29: "keys, plucks, leads: note data, progressions, arps"; 2026-10-30: "data is gold and a slight variation
can give many results"). Every source names its key, so every note becomes a scale degree of that key's mode:
  - Scaler 3 chord sets (scaler3_chord_progressions.json): the bass of each voicing is the chord's root.
  - Ableton Tonal chord-progression clips: notes struck together are one chord, the lowest is its root.
  - Kontakt 8 Phrases MIDI and Ableton Tonal clips (.alc): one bar on a 16th grid is a line.
A progression is four chord roots; a line is sixteen steps of degree and velocity. Both are kept per family (major:
major, ionian, lydian, mixolydian; minor: minor, aeolian, dorian, phrygian, harmonic minor) because a degree means a
different chord in the other family. Exact repeats across sources are merged and counted. (Kontakt's Chords files hold
one chord each, a key's palette with no order, so they give no progressions.)
    note_bank.py [--out FILE] [--stats]
The data is the owner's licensed library; only degree patterns ship, never a file's notes in its own key and rhythm."""
import argparse, collections, glob, gzip, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from groove_bank import notes as midi_notes  # noqa: E402

DEFAULT_OUT = os.path.join(HERE, "..", "..", "src", "app", "graph", "GenerateSamplesNotes.inc")
DATA = os.path.expanduser("~/Infinite/musical_datasets/1_midi_harmonics")
SCALER = os.path.expanduser("~/Infinite/scaler3_data/scaler3_chord_progressions.json")
MODES = {"major": (0, [0, 2, 4, 5, 7, 9, 11]), "ionian": (0, [0, 2, 4, 5, 7, 9, 11]), "lydian": (0, [0, 2, 4, 6, 7, 9, 11]),
         "mixolydian": (0, [0, 2, 4, 5, 7, 9, 10]), "minor": (1, [0, 2, 3, 5, 7, 8, 10]), "aeolian": (1, [0, 2, 3, 5, 7, 8, 10]),
         "dorian": (1, [0, 2, 3, 5, 7, 9, 10]), "phrygian": (1, [0, 1, 3, 5, 7, 8, 10]), "phrigian": (1, [0, 1, 3, 5, 7, 8, 10]),
         "harmonic minor": (1, [0, 2, 3, 5, 7, 8, 11]), "min": (1, [0, 2, 3, 5, 7, 8, 10]), "maj": (0, [0, 2, 4, 5, 7, 9, 11])}
PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
KEY = re.compile(r"(?:^|[_ ])([A-G])[ _]?(sharp|flat|#|b)?[ _]+(harmonic minor|major|minor|ionian|dorian|phrygian|phrigian|lydian|"
                 r"mixolydian|aeolian|maj|min)(?![a-z])", re.I)


def key_of(text):
    """(tonic pc, family, mode intervals) from a name like 'EZ_Airy_FSharp_Minor' or 'Rave Lead C Min 130 bpm'."""
    m = None
    for m in KEY.finditer(text.replace("Sharp", " sharp").replace("Flat", " flat")):
        pass
    if not m:
        return None
    acc = (m.group(2) or "").lower()
    fam, iv = MODES[m.group(3).lower()]
    return (PC[m.group(1).upper()] + (1 if acc in ("sharp", "#") else -1 if acc in ("flat", "b") else 0)) % 12, fam, iv


def degree(pitch, key):
    """Scale degree (0..6 plus 7 per octave above the tonic at or below middle C) or None when off the scale."""
    tonic, _, iv = key
    rel = pitch - tonic
    pc = rel % 12
    return iv.index(pc) + 7 * (rel // 12) if pc in iv else None


def prog_of(roots, key):
    """The first four chord changes as degrees 0..6, cycled when a set has fewer; None if a root is off the scale."""
    ds = []
    for r in roots:
        d = degree(r, key)
        if d is None:
            return None
        d %= 7
        if not ds or ds[-1] != d:
            ds.append(d)
    if len(ds) < 2:
        return None
    return tuple((ds * 4)[:4])


def lines_of(ns, beat, key):
    """Each bar on a 16th grid as (degrees, velocities) relative to the bar's lowest octave; bars mostly off the scale or
    with fewer than four notes are skipped. Off-scale notes snap down one semitone (the passing tone's neighbour)."""
    six = beat / 4.0
    bars = collections.defaultdict(dict)
    for t, p, v in ns:
        s = int(round(t / six))
        cell = bars[s // 16]
        if s % 16 not in cell or p > cell[s % 16][0]:   # the top voice is the line
            cell[s % 16] = (p, v)
    for b, cell in sorted(bars.items()):
        if len(cell) < 4:
            continue
        ds, off = {}, 0
        for i, (p, v) in cell.items():
            d = degree(p, key)
            if d is None:
                d, off = degree(p - 1, key), off + 1
            ds[i] = (d, v)
        if off > len(cell) * 0.15:
            continue
        lo = min(d for d, _ in ds.values()) // 7 * 7
        if max(d for d, _ in ds.values()) - lo > 14:
            continue
        yield tuple((ds[i][0] - lo, max(1, min(9, round(ds[i][1] / 127 * 9)))) if i in ds else None for i in range(16))


def alc_notes(path):
    """(tick, pitch, velocity) from an Ableton clip; 96 ticks per beat."""
    x = gzip.open(path).read().decode("utf-8", "ignore")
    out = []
    for m in re.finditer(r'<KeyTrack Id="\d+">(.*?)</KeyTrack>', x, re.S):
        key = re.search(r'<MidiKey Value="(\d+)"', m.group(1))
        for e in re.finditer(r'<MidiNoteEvent Time="([\d.]+)" Duration="[\d.]+" Velocity="([\d.]+)"', m.group(1)):
            out.append((int(round(float(e.group(1)) * 96)), int(key.group(1)), int(float(e.group(2)))))
    return sorted(out), 96


def chord_roots(ns, beat):
    """The lowest note of each group struck within a 32nd of each other."""
    roots, last = [], None
    for t, p, _ in ns:
        if last is None or t - last > beat / 8:
            roots.append(p)
            last = t
        else:
            roots[-1] = min(roots[-1], p)
    return roots


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--stats", action="store_true")
    a = ap.parse_args()
    progs = collections.defaultdict(set)   # (family, degrees) -> sources
    lines = collections.defaultdict(set)   # (family, role, cells) -> sources
    seen = collections.Counter()

    for e in json.load(open(SCALER)):
        key = key_of(e.get("scale_mode") or "")
        if not key or not e.get("voicings"):
            continue
        p = prog_of([min(60 + i for i in v["intervals"]) for v in e["voicings"]], key)
        if p:
            progs[(key[1], p)].add("scaler:" + e["name"]); seen["scaler progs"] += 1

    clips = [(f, midi_notes) for f in glob.glob(os.path.join(DATA, "kontakt8_tools/Phrases/**/*.mid"), recursive=True)]
    clips += [(f, alc_notes) for d in ("Melodic", "Bassline") for f in glob.glob(os.path.join(DATA, "ableton_clips/Tonal", d, "*.alc"))]
    clips += [(f, alc_notes) for f in glob.glob(os.path.join(DATA, "ableton_clips/Tonal/Chord Progression/*.alc"))]
    for f, reader in clips:
        rel = os.path.relpath(f, DATA)
        key = key_of(rel)
        if not key:
            continue
        ns, div = reader(f)
        if not ns:
            continue
        if "Chord Progression" in rel:
            p = prog_of(chord_roots(ns, div), key)
            if p:
                progs[(key[1], p)].add(f); seen["ableton progs"] += 1
            continue
        pitches = sorted(p for _, p, _ in ns)
        role = 1 if "bass" in rel.lower() or pitches[len(pitches) // 2] < 50 else 0
        for cells in lines_of(ns, div, key):
            lines[(key[1], role, cells)].add(f); seen["lines " + ("bass" if role else "melodic")] += 1

    P = sorted(((len(s), k) for k, s in progs.items()), reverse=True)
    L = sorted(((len(s), k) for k, s in lines.items()), key=lambda r: (r[0], str(r[1])), reverse=True)
    if a.stats:
        print("bars/sets read:", dict(seen))
        for fam in (0, 1):
            ps = [(c, k[1]) for c, k in P if k[0] == fam]
            print("%s: %d progressions, top %s" % ("major" if fam == 0 else "minor", len(ps), ps[:6]))
            for role in (0, 1):
                print("   %s lines %d" % ("bass" if role else "melodic", sum(1 for _, k in L if k[0] == fam and k[1] == role)))
    enc = lambda cells: "".join("." if c is None else chr(ord("a") + c[0]) for c in cells)
    vel = lambda cells: "".join("." if c is None else str(c[1]) for c in cells)
    out = ["// Generated by tools/universe/note_bank.py from the owner's note library (Scaler 3 chord sets, Kontakt 8 Phrases",
           "// MIDI, Ableton Tonal clips). Every note is a degree of its source's key, so only degree patterns ship.",
           "// minor: 0 major family, 1 minor family. A progression is four chord roots (0..6). A line is one bar of 16ths:",
           "// '.' a rest, 'a' + n degree n above the bar's lowest octave; v holds velocity 1..9. files: sources that play it.",
           "// Do not edit by hand: rerun the tool.",
           "struct NoteProg { int minor, files; int d[4]; };",
           "struct NoteLine { int minor, bass, files; const char* d; const char* v; };",
           "static const NoteProg kNoteProgs[] = {"]
    out += ["   { %d, %d, { %s } }," % (k[0], c, ", ".join(map(str, k[1]))) for c, k in P] + ["};", "static const NoteLine kNoteLines[] = {"]
    out += ['   { %d, %d, %d, "%s", "%s" },' % (k[0], k[1], c, enc(k[2]), vel(k[2])) for c, k in L] + ["};"]
    open(a.out, "w").write("\n".join(out) + "\n")
    print("%d progressions, %d lines -> %s" % (len(P), len(L), os.path.relpath(a.out)))


if __name__ == "__main__":
    main()
