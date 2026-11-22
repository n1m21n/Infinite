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


def synth_cued_double_blinks(x, fs, blinks, corrupt, ce, ibis=(0.35, 0.45, 0.55)):
    """Pair each subject's own post-cal cued blinks at realistic voluntary double-blink IBIs."""
    ok = lambda t: t >= max(SETTLE, ce + 1.0) and not any(a - 0.8 <= t <= b + 0.8 for a, b in corrupt)
    cued = [t for t, c in blinks if c == 1 and ok(t)]
    if len(cued) < 2:
        return None, []
    # 1st-order 0.35 Hz DC blocker to locate each blink's true peak index
    comm = 0.5 * (x[:, 0] + x[:, 1])
    a = float(np.exp(-2.0 * np.pi * 0.35 / fs))
    dc = np.zeros_like(comm)
    for i in range(1, len(comm)):
        dc[i] = a * (dc[i - 1] + comm[i] - comm[i - 1])
    pre, post = int(0.18 * fs), int(0.22 * fs)
    snips, peaks_i = [], []
    for t in cued:
        i0, i1 = max(pre, int((t - 0.35) * fs)), min(len(x) - post - 1, int((t + 0.50) * fs))
        if i1 <= i0:
            continue
        pk = i0 + int(np.argmax(np.abs(dc[i0:i1])))
        seg = x[pk - pre : pk + post].copy()
        edge0, edge1 = seg[:8].mean(0), seg[-8:].mean(0)
        seg -= edge0 + np.linspace(0.0, 1.0, len(seg))[:, None] * (edge1 - edge0)
        snips.append(seg)
        peaks_i.append(pk)
    if len(snips) < 2:
        return None, []
    y = x.copy()
    targets, used_until = [], -10.0
    for idx, pk1 in enumerate(peaks_i):
        t1 = pk1 / fs
        if t1 < used_until:
            continue
        t2 = t1 + ibis[idx % len(ibis)]
        if any(abs(bt - t2) < 0.7 and abs(bt - t1) > 0.15 for bt, _ in blinks):
            continue
        pk2 = int(round(t2 * fs))
        if pk2 - pre >= 0 and pk2 + post < len(y):
            y[pk2 - pre : pk2 + post] += snips[(idx + 1) % len(snips)]
            targets.append((t1, t2))
            used_until = t2 + 1.2
    return y, targets


def main():
    data = sys.argv[1]
    cal = "--cal" in sys.argv
    signs = [float(s) for s in sys.argv[2:] if s != "--cal"] or [1.0, -1.0]
    exe = os.path.join(os.environ.get("TMPDIR", "/tmp"), "blink_replay")
    build(exe)
    print(f"{'set':7s} {'mode':6s} {'sign':>4s} {'hit 0/1':>8s} {'hit soft':>8s} {'false/min':>9s}  subjects")
    for ds in ("EEG-IO", "EEG-VV", "EEG-VR"):
        folder = os.path.join(data, ds)
        files = sorted(f for f in os.listdir(folder) if f.endswith("_data.csv"))
        sess = [load(folder, f) for f in files]
        for mode in (0, 1, 2):
            for sign in signs:
                hits, soft, bad, mins, per = [], [], 0, 0.0, []
                for fs, x, corrupt, blinks, dur in sess:
                    txt = "\n".join(f"{a:.3f} {b:.3f}" for a, b in x)
                    ce = cal_end(ds, blinks) if cal else 0.0
                    r = subprocess.run([exe, str(fs), str(mode), str(sign), str(ce)], input=txt, capture_output=True,
                                       text=True)
                    fires = [float(v) for v in r.stdout.split()]
                    start = max(SETTLE, ce)
                    if mode == 2:
                        # DoubleBlink mode on raw single-blink/natural recordings: every trigger is a false toggle.
                        ok = lambda t: t >= start and not any(a - POST <= t <= b + POST for a, b in corrupt)
                        b = len([t for t in fires if ok(t)])
                        m = (dur - start - sum(max(0.0, min(b2, dur) - max(a2, start)) for a2, b2 in corrupt)) / 60.0
                        bad += b
                        mins += m
                        per.append(f"{b / max(m, 1e-9):.1f}")
                    else:
                        h, b, m = score(fires, blinks, corrupt, dur, start)
                        hn = [v for (t, c), v in h.items() if c < 2]
                        hits += hn
                        soft += [v for (t, c), v in h.items() if c == 2]
                        bad += b
                        mins += m
                        per.append(f"{np.mean(hn) if hn else 0:.2f}/{b / max(m, 1e-9):.1f}")
                if mode == 2:
                    # Also score cued double-blink recall on EEG-IO
                    dbl_rec = float("nan")
                    if ds == "EEG-IO":
                        dh, dt = 0, 0
                        for fs, x, corrupt, blinks, dur in sess:
                            ce = cal_end(ds, blinks) if cal else 0.0
                            y, targets = synth_cued_double_blinks(x, fs, blinks, corrupt, ce)
                            if y is None or not targets:
                                continue
                            txt_y = "\n".join(f"{a:.3f} {b:.3f}" for a, b in y)
                            ry = subprocess.run([exe, str(fs), "2", str(sign), str(ce)], input=txt_y,
                                                capture_output=True, text=True)
                            fy = [float(v) for v in ry.stdout.split() if float(v) >= max(SETTLE, ce)]
                            for t1, t2 in targets:
                                dt += 1
                                if any(t2 - 0.15 <= f <= t2 + 0.65 for f in fy):
                                    dh += 1
                        dbl_rec = dh / max(1, dt)
                    print(f"{ds:7s} {'double':6s} {sign:+4.0f} {dbl_rec:8.3f} {float('nan'):8.3f} {bad / mins:9.2f}  "
                          f"{len(sess)} [{' '.join(per)}]")
                else:
                    print(f"{ds:7s} {['intent', 'all'][mode]:6s} {sign:+4.0f} {np.mean(hits):8.3f} "
                          f"{(np.mean(soft) if soft else float('nan')):8.3f} {bad / mins:9.2f}  {len(sess)} "
                          f"[{' '.join(per)}]")


if __name__ == "__main__":
    main()
