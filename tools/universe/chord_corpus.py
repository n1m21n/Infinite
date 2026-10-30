#!/usr/bin/env python3
"""Four-bar chord progressions read out of real MIDI, in the note bank's terms: four chord roots as scale degrees (0-6) of the
file's key, major or minor family. One row per song: every distinct four-bar window, so splits can hold out whole songs.
    ~/Infinite/ml-venv/bin/python3 -I tools/universe/chord_corpus.py [--budget 1200] [--lakh 3000]
Key = Krumhansl profile over the whole file. Chord per bar = the diatonic triad whose notes hold most duration, bass notes
counting double. A bar under 3 pitch classes, a 4/4-less file or a drum-only bar is skipped. Output: training-data/store/chord_windows.jsonl."""
import argparse, collections, json, os, random, time
import numpy as np, mido
ROOT = os.path.expanduser("~/Infinite/datasets/midi/data")
OUT = os.path.expanduser("~/Infinite/training-data/store/chord_windows.jsonl")
MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
SC = {0: [0, 2, 4, 5, 7, 9, 11], 1: [0, 2, 3, 5, 7, 8, 10]}


def notes(path):
    m = mido.MidiFile(path, clip=True)
    if m.type == 2 or not m.ticks_per_beat:
        return None
    out = []; tpb = m.ticks_per_beat
    for tr in m.tracks:
        t = 0; on = {}; prog = 0
        for ev in tr:
            t += ev.time
            if ev.type == "note_on" and ev.velocity > 0 and ev.channel != 9:
                on[(ev.channel, ev.note)] = t
            elif ev.type in ("note_off", "note_on") and ev.channel != 9:
                s = on.pop((ev.channel, ev.note), None)
                if s is not None and t > s:
                    out.append((s / tpb, t / tpb, ev.note))
    return out


def key_of(n):
    h = np.zeros(12)
    for a, b, p in n:
        h[p % 12] += b - a
    if h.sum() == 0:
        return None
    best = max(((np.corrcoef(np.roll(pr, k), h)[0, 1], k, f) for f, pr in ((0, MAJ), (1, MIN)) for k in range(12)))
    return best[1], best[2]


def bars(n, key, fam):
    end = int(max(b for _, b, _ in n) // 4) + 1
    if end > 600:
        return []
    H = np.zeros((end, 12)); low = [999] * end
    for a, b, p in n:
        i = int(a // 4)
        for bar in range(i, min(end, int((b - 1e-6) // 4) + 1)):
            d = min(b, (bar + 1) * 4) - max(a, bar * 4)
            H[bar, p % 12] += d
            low[bar] = min(low[bar], p)
    for bar in range(end):
        if low[bar] < 999:
            H[bar, low[bar] % 12] += H[bar].sum() * 0.25
    sc = [(key + s) % 12 for s in SC[fam]]
    tri = [[sc[d], sc[(d + 2) % 7], sc[(d + 4) % 7]] for d in range(7)]
    out = []
    for bar in range(end):
        if (H[bar] > 0).sum() < 3:
            out.append(-1); continue
        s = [H[bar][t[0]] * 1.2 + H[bar][t[1]] + H[bar][t[2]] for t in tri]
        out.append(int(np.argmax(s)))
    return out


def windows(ds):
    w = collections.Counter()
    for i in range(0, len(ds) - 3, 2):
        x = ds[i:i + 4]
        if -1 not in x and len(set(x)) >= 2:
            w[tuple(x)] += 1
    return w


def main():
    a = argparse.ArgumentParser(); a.add_argument("--budget", type=float, default=1200); a.add_argument("--lakh", type=int, default=3000)
    a = a.parse_args()
    files = [(os.path.join(d, f), "pop909") for d, _, fs in os.walk(ROOT + "/pop909_songs/POP909") if "versions" not in d for f in fs if f.endswith(".mid")]
    lakh = [os.path.join(d, f) for d, _, fs in os.walk(ROOT + "/lakh_midi_clean") for f in fs if f.lower().endswith(".mid")]
    random.Random(7).shuffle(lakh)
    files += [(f, "lakh") for f in lakh[:a.lakh]]
    t0 = time.time(); kept = 0; n = 0
    with open(OUT, "w") as f:
        for path, src in files:
            if time.time() - t0 > a.budget:
                break
            n += 1
            try:
                ns = notes(path)
                if not ns or len(ns) < 64:
                    continue
                k = key_of(ns)
                if k is None:
                    continue
                w = windows(bars(ns, k[0], k[1]))
            except Exception:
                continue
            if w:
                song = os.path.basename(os.path.dirname(path)) if src == "lakh" else os.path.basename(path)
                f.write(json.dumps({"src": src, "song": song, "minor": k[1], "w": [[list(x), c] for x, c in w.items()]}) + "\n")
                kept += 1
            if n % 200 == 0:
                print(n, kept, "%.0fs" % (time.time() - t0), flush=True)
    print("done", n, kept, "%.0fs" % (time.time() - t0))


main()
