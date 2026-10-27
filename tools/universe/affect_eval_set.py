#!/usr/bin/env python3
"""Listener-labelled MIDI -> a held-out affect set for B10, never trained on.
    affect_eval_set.py [--out ~/Infinite/training-data/eval/affect_midi.jsonl]
  EMOPIA    1,078 pop-piano clips, each in one Russell quadrant (Q1 +v+a, Q2 -v+a, Q3 -v-a, Q4 +v-a)
  VGMIDI    labelled game-music phrases with valence and arousal of -1 or +1
One row per clip: the labels, summary features (mode, tempo, note rate, velocity, register, span, polyphony, minor-chord
share) and the clip's song_arc, so the analysis can test any feature against what listeners said."""
import argparse, csv, json, os, re, statistics, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import expression_pieces as E
import midi_ingest as M
import song_detail as S

DATA = os.path.expanduser("~/Infinite/datasets/midi/data")
QUADRANT = {1: (1, 1), 2: (-1, 1), 3: (-1, -1), 4: (1, -1)}


def clips():
    root = os.path.join(DATA, "emopia_piano_emotions/EMOPIA_1.0")
    for r in csv.DictReader(open(os.path.join(root, "label.csv"))):
        v, a = QUADRANT[int(r["4Q"])]
        yield "emopia", r["ID"], os.path.join(root, "midis", r["ID"] + ".mid"), v, a
    root = os.path.join(DATA, "vgmidi_game_tracks")
    for r in csv.DictReader(open(os.path.join(root, "vgmidi_labelled.csv"))):
        # the csv names phrase files; the download holds the whole labelled pieces in labelled/midi
        piece = re.sub(r"_\d+\.mid$", ".mid", os.path.basename(r["midi"]))
        yield "vgmidi", r["id"], os.path.join(root, "labelled/midi", piece), int(r["valence"]), int(r["arousal"])


def features(path):
    tracks, div = E.parse_midi(path)
    ns = M.notes(tracks)
    pitched = [n for n in ns if n[3] != 9]
    if len(pitched) < 8:
        return None
    tm = S.tempo_map(tracks)
    sec, _ = S.clock(tm, div)
    secs = max(1e-3, sec(max(n[1] for n in ns)) - sec(min(n[0] for n in ns)))
    key = M.key_of(ns, div)
    bars = S.bar_map(tracks, div, max(n[1] for n in ns))
    bass_ch = M.bass_channel(ns)
    arc = S.song_arc(tracks, ns, div, bars, key, bass_ch)
    chords = [c for c in S.chord_per_bar(ns, bars, bass_ch) if c]
    ps = [n[2] for n in pitched]
    return {"mode": key[1] if key else None, "bpm": round(S.bpm_at(tm, 0)), "notes_per_s": round(len(pitched) / secs, 2),
            "vel": round(statistics.fmean(n[4] for n in pitched), 1), "vel_sd": round(statistics.pstdev(n[4] for n in pitched), 1),
            "reg": statistics.median(ps), "span": max(ps) - min(ps),
            "poly": round(sum(n[1] - n[0] for n in pitched) / div / max(1, len(bars)) / 4, 2),
            "minor_chords": round(sum(c[1] in S.MINORISH for c in chords) / len(chords), 2) if chords else None,
            "seconds": round(secs, 1)}, arc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="~/Infinite/training-data/eval/affect_midi.jsonl")
    a = ap.parse_args()
    out = os.path.expanduser(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    n, bad = {}, 0
    with open(out, "w") as fo:
        for ds, cid, path, v, ar in clips():
            try:
                got = features(path)
            except Exception:
                got = None
            if not got:
                bad += 1
                continue
            f, arc = got
            fo.write(json.dumps({"set": ds, "id": cid, "valence": v, "arousal": ar, "features": f, "arc": arc},
                                separators=(",", ":")) + "\n")
            n[ds] = n.get(ds, 0) + 1
    print("wrote", out, n, "unreadable", bad)


if __name__ == "__main__":
    main()
