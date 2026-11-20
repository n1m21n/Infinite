#!/usr/bin/env python3
"""M4: replay EEG-EyeBlinks (Agarwal & Sivakumar 2019; OpenBCI Fp1+Fp2) through the real BciDspEngine
blink detector (tools/bci/blink_replay.cpp) and score the Blink pin as a toggle source.

Each rising edge of the Blink pin is one toggle. An edge matches a labelled blink if it lands
-0.25..+0.75 s around the label (labels mark the blink minimum; the detector fires at pulse end).
One edge per label; every other edge is a false toggle (it flips the toggle the wrong way).
Corrupt intervals and the first 2 s (filter settle) are left out.
Bar: hit >= 95 %, false <= 1 per minute.

With --cal, each recording starts with the prompted-blink calibration phase, ending 0.6 s after its
5th labelled blink (EEG-IO: 5th cued blink); scoring starts after it.

usage: blink_m4.py DATA_DIR [--cal] [SIGN ...]     DATA_DIR = .../EEG-EyeBlinks
"""
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SETTLE, PRE, POST = 2.0, 0.25, 0.75


def build(out):
    src = [os.path.join(HERE, "blink_replay.cpp"),
           os.path.join(ROOT, "src/core/bci/BciDspEngine.cpp"),
           os.path.join(ROOT, "src/core/bci/BciTypes.cpp")]
    subprocess.run(["clang++", "-std=c++20", "-O2", "-w", "-I" + os.path.join(ROOT, "src"), "-o", out] + src,
                   check=True)


def load(folder, f):
    path = os.path.join(folder, f)
    if folder.endswith("EEG-IO"):
        x = np.loadtxt(path, delimiter=";", skiprows=1, usecols=(0, 1, 2))
        fs = 250.0  # README rate; the time column steps 1/256 s with gaps
    else:
        x = np.loadtxt(path, delimiter=",", skiprows=5, usecols=(0, 1, 2))
        fs = 250.0
        x = x[:int(200 * fs) + 1]  # labels cover the first 200 s (read_data.py)
    corrupt, blinks, n = [], [], 0
    dur = len(x) / fs
    for line in open(path.replace("_data", "_labels")):
        row = [r.strip() for r in line.split(",")]
        if row[0] == "corrupt":
            n = int(row[1])
        elif row[0] == "blinks":
            pass
        elif n > 0:
            corrupt.append((float(row[0]), dur if float(row[1]) == -1 else float(row[1])))
            n -= 1
        elif row[0]:
            blinks.append((float(row[0]), int(row[1])))
    return fs, x[:, 1:3], corrupt, blinks, dur


def cal_end(ds, blinks):
    cued = [t for t, c in blinks if (c == 1 if ds == "EEG-IO" else c < 2)]
    return cued[4] + 0.6 if len(cued) >= 5 else 0.0


def score(fires, blinks, corrupt, dur, start=SETTLE):
    ok = lambda t: t >= start and not any(a - POST <= t <= b + POST for a, b in corrupt)
    labels = [(t, c) for t, c in blinks if ok(t)]
    fires = [t for t in fires if ok(t)]
    used, hit = set(), {}
    for t, c in labels:
        m = [i for i, f in enumerate(fires) if i not in used and t - PRE <= f <= t + POST]
        if m:
            used.add(m[0])
        hit[(t, c)] = bool(m)
    bad = len(fires) - len(used)
    mins = (dur - start - sum(b - a for a, b in corrupt)) / 60
    return hit, bad, mins


def main():
    data = sys.argv[1]
    cal = "--cal" in sys.argv
    signs = [float(s) for s in sys.argv[2:] if s != "--cal"] or [1.0, -1.0]
    exe = os.path.join(os.environ.get("TMPDIR", "/tmp"), "blink_replay")
    build(exe)
    print(f"{'set':7s} {'mode':5s} {'sign':>4s} {'hit 0/1':>8s} {'hit soft':>8s} {'false/min':>9s}  subjects")
    for ds in ("EEG-IO", "EEG-VV", "EEG-VR"):
        folder = os.path.join(data, ds)
        files = sorted(f for f in os.listdir(folder) if f.endswith("_data.csv"))
        sess = [load(folder, f) for f in files]
        for mode in (0, 1):
            for sign in signs:
                hits, soft, bad, mins, per = [], [], 0, 0.0, []
                for fs, x, corrupt, blinks, dur in sess:
                    txt = "\n".join(f"{a:.3f} {b:.3f}" for a, b in x)
                    ce = cal_end(ds, blinks) if cal else 0.0
                    r = subprocess.run([exe, str(fs), str(mode), str(sign), str(ce)], input=txt, capture_output=True,
                                       text=True)
                    fires = [float(v) for v in r.stdout.split()]
                    h, b, m = score(fires, blinks, corrupt, dur, max(SETTLE, ce))
                    hn = [v for (t, c), v in h.items() if c < 2]
                    hits += hn
                    soft += [v for (t, c), v in h.items() if c == 2]
                    bad += b
                    mins += m
                    per.append(f"{np.mean(hn) if hn else 0:.2f}/{b / max(m, 1e-9):.1f}")
                print(f"{ds:7s} {['intent', 'all'][mode]:5s} {sign:+4.0f} {np.mean(hits):8.3f} "
                      f"{(np.mean(soft) if soft else float('nan')):8.3f} {bad / mins:9.2f}  {len(sess)} "
                      f"[{' '.join(per)}]")


if __name__ == "__main__":
    main()
