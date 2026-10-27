#!/usr/bin/env python3
"""Find groove and drum-layout data on a drive: drum MIDI libraries, groove templates and drum-kit maps.
    groove_scan.py [ROOT ...] [--out groove_scan.json] [--budget 900]
Walks every folder once (read only) and lists, per folder:
  midi     MIDI files, with how many of the first 20 hold drums (channel 10, or GM drum notes on one channel) and
           their meters / tempos, so drum-groove libraries (EZdrummer, BFD, Superior, Groove Monkee ...) stand out
  grooves  groove templates: Ableton .agr, Cubase/Groove .grv, Logic/Maschine/Reason groove files
  kits     drum-kit and pad layouts: Ableton Drum Rack .adg, Battery .kt2/.kt3, Maschine .mxkt, Kontakt drum .nki
           only in drum-named folders
Already-ingested MIDI under ~/Infinite/datasets is not on this drive, so everything found is new."""
import argparse, collections, json, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import expression_pieces as E

MIDI = {".mid", ".midi", ".smf", ".kar"}
GROOVE = {".agr", ".grv", ".mxgrp", ".grov", ".groove", ".swing"}
KIT = {".adg", ".kt2", ".kt3", ".mxkt", ".mxsnd", ".nbkt", ".drm", ".dkt"}
DRUMMY = re.compile(r"groove|drum|beat|loop|kit|fill|break|perc|ezd|bfd|superior|addictive|battery|maschine", re.I)
SKIP = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100"}
GM_DRUMS = set(range(35, 82))


def drum_file(path):
    """(holds drums, meter, bpm) of one MIDI file, or None when it cannot be read"""
    try:
        tracks, div = E.parse_midi(path)
    except Exception:
        return None
    ons = [(ch, a) for ev in tracks for t, k, ch, a, v in ev if k == 0x90 and v > 0]
    meter = next((("%d/%d" % (e[3], 2 ** e[4])) for ev in tracks for e in ev if e[1] == 0xff and e[2] == 0), "4/4")
    us = next((e[3] for ev in tracks for e in ev if e[1] == 0xff and e[2] == 1), 500000)
    if not ons:
        return False, meter, round(6e7 / us)
    ch10 = sum(ch == 9 for ch, _ in ons) / len(ons)
    # drum libraries often write channel 1: call it drums when one channel plays only GM drum notes with kick + snare
    by = collections.defaultdict(set)
    for ch, a in ons:
        by[ch].add(a)
    gm = any(ns <= GM_DRUMS and ns & {35, 36} and ns & {38, 40} for ns in by.values())
    return ch10 > 0.5 or gm, meter, round(6e7 / us)


def scan(root, budget):
    t0 = time.time()
    out, n_dirs = [], 0
    for d, subs, files in os.walk(root):
        subs[:] = [s for s in subs if s not in SKIP and not s.startswith(".")]
        n_dirs += 1
        if time.time() - t0 > budget:
            print("budget hit after %d folders" % n_dirs, file=sys.stderr)
            break
        ext = lambda f: os.path.splitext(f)[1].lower()
        mids = [f for f in files if ext(f) in MIDI and not f.startswith("._")]
        grooves = [f for f in files if ext(f) in GROOVE]
        kits = [f for f in files if ext(f) in KIT or (ext(f) == ".nki" and DRUMMY.search(d))]
        if not (mids or grooves or kits):
            continue
        row = {"dir": d, "midi": len(mids), "grooves": len(grooves), "kits": len(kits),
               "kinds": sorted({ext(f) for f in grooves + kits})}
        if mids:
            got = [x for x in (drum_file(os.path.join(d, f)) for f in sorted(mids)[:20]) if x]
            row["drum_share"] = round(sum(x[0] for x in got) / len(got), 2) if got else 0
            row["meters"] = dict(collections.Counter(x[1] for x in got).most_common(3))
            row["bpm"] = sorted({x[2] for x in got})[:6]
        out.append(row)
    return out, n_dirs, time.time() - t0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*", default=["/Volumes/Wd-Black"])
    ap.add_argument("--out", default="groove_scan.json")
    ap.add_argument("--budget", type=float, default=900, help="seconds")
    a = ap.parse_args()
    rows = []
    for r in a.roots:
        got, nd, s = scan(r, a.budget)
        print("%s: %d folders, %.0f s" % (r, nd, s))
        rows += got
    json.dump(rows, open(a.out, "w"), indent=1)
    drums = [x for x in rows if x["midi"] and x.get("drum_share", 0) >= 0.5]
    print("\nMIDI: %d files in %d folders; drum folders %d (%d files)" % (
        sum(x["midi"] for x in rows), sum(bool(x["midi"]) for x in rows), len(drums), sum(x["midi"] for x in drums)))
    # group by the top library folder (two levels under the root) so a 3,000-folder groove pack reads as one line
    lib = collections.defaultdict(lambda: collections.Counter())
    for x in rows:
        rel = os.path.relpath(x["dir"], a.roots[0]).split(os.sep)
        k = os.sep.join(rel[:3])
        c = lib[k]
        c["midi"] += x["midi"]
        c["drum midi"] += x["midi"] if x.get("drum_share", 0) >= 0.5 else 0
        c["grooves"] += x["grooves"]
        c["kits"] += x["kits"]
        c["folders"] += 1
    print("%-70s %7s %9s %7s %5s %7s" % ("library (top 3 folders)", "midi", "drum midi", "groove", "kits", "folders"))
    for k, c in sorted(lib.items(), key=lambda kv: -(kv[1]["drum midi"] * 2 + kv[1]["midi"] + 50 * kv[1]["grooves"] + 20 * kv[1]["kits"]))[:60]:
        print("%-70s %7d %9d %7d %5d %7d" % (k[-70:], c["midi"], c["drum midi"], c["grooves"], c["kits"], c["folders"]))
    print("groove/kit formats:", dict(collections.Counter(k for x in rows for k in x["kinds"])))
    print("wrote", a.out)


if __name__ == "__main__":
    main()
