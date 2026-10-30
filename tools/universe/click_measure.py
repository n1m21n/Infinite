#!/usr/bin/env python3
"""Click transients (PLAN-2026-10-30 item 16): the SubLab transient samples as the reference for Generate Samples' click layers.
Per file: length (peak to -40 dB), centroid, share above 2 kHz and under 200 Hz, all over the first 40 ms from the peak.
    click_measure.py sublab                 the reference ranges (kick attacks vs bass/plain attacks)
    click_measure.py files a.wav b.wav ...  the same numbers for any renders"""
import glob, json, os, sys
import numpy as np, soundfile as sf

SUBLAB = os.path.expanduser("~/Infinite/musical_datasets/6_sub_bass_and_kicks/sublab/transient_samples")
WIN = 0.040


def measure(path):
    x, fs = sf.read(path, always_2d=True); x = x.mean(1)
    if not np.abs(x).max():
        return None
    p = int(np.argmax(np.abs(x))); env = np.abs(x[p:]); thr = np.abs(x).max() * 0.01
    # length: last sample above -40 dB of peak, through a 1 ms max-hold so zero crossings do not end it
    h = max(1, int(fs * 0.001)); held = np.array([env[i:i + h].max() for i in range(0, len(env), h)])
    above = np.flatnonzero(held > thr); length = (above[-1] + 1) * h / fs if len(above) else 0.0
    seg = x[max(0, p - int(fs * 0.002)):p + int(fs * WIN)]
    fade = np.ones(len(seg)); n = max(1, len(seg) // 4); fade[-n:] = np.hanning(2 * n)[n:]  # onset kept, tail faded
    seg = seg * fade; sp = np.abs(np.fft.rfft(seg, 1 << 14)) ** 2; f = np.fft.rfftfreq(1 << 14, 1 / fs)
    t = sp.sum() or 1e-30
    return {"ms": round(length * 1000, 1), "centroid": round(float((f * sp).sum() / t)), "hf": round(float(sp[f >= 2000].sum() / t), 3),
            "low": round(float(sp[f < 200].sum() / t), 3)}


def summary(rows, label):
    A = np.array([[r["ms"], r["centroid"], r["hf"], r["low"]] for r in rows])
    q = np.percentile(A, [10, 50, 90], 0)
    print("%-12s n=%3d  ms %5.0f %5.0f %5.0f | centroid %5.0f %5.0f %5.0f | >2k %.2f %.2f %.2f | <200 %.2f %.2f %.2f"
          % (label, len(rows), *q[:, 0], *q[:, 1], *q[:, 2], *q[:, 3]))


if __name__ == "__main__":
    if sys.argv[1] == "sublab":
        rows = []
        for p in sorted(glob.glob(os.path.join(SUBLAB, "*", "*.wav"))):
            m = measure(p)
            if m:
                m["kick"] = "kick" in os.path.basename(p).lower(); rows.append(m)
        print("10th / median / 90th percentile")
        summary([r for r in rows if r["kick"]], "kick attack"); summary([r for r in rows if not r["kick"]], "attack")
        summary(rows, "all")
    else:
        rows = [m for m in map(measure, sys.argv[2:]) if m]
        summary(rows, "renders")
