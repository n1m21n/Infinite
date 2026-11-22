#!/usr/bin/env python3
"""Turns the rig's raw tanh x3 renders into listening clips.

What: reads <dir>/*.f32 (float32 mono, 48 kHz, one steady-state period of 16384 samples each, written by the app with
      INFINITE_FIELDRATE_AUDIO=<dir>) and writes <dir>/<name>.wav: the period repeated to 2 s (the test tone sits on
      bin 1707 of 16384, so the period repeats with no seam), peak-normalised to -18 dBFS, 10 ms linear fades, 16-bit.
      Also writes <dir>/renders.log: the app's "FIELDRATE audio" lines with the directory stripped.
Why:  the research page plays these; the .f32 files stay as the measured data.
Usage: python3 scripts/make_audio.py <dir> <app log containing the FIELDRATE audio lines>
"""
import array, os, re, sys, wave

RATE, SECONDS, PEAK_DB, FADE = 48000, 2.0, -18.0, 0.010


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    d, log = sys.argv[1], sys.argv[2]
    lines = [re.sub(r"\S*/(tanh3-[\w-]+\.f32)", r"\1", ln) for ln in open(log, encoding="utf-8") if ln.startswith("FIELDRATE audio")]
    open(os.path.join(d, "renders.log"), "w", encoding="utf-8").write("".join(lines))
    for f in sorted(os.listdir(d)):
        if not f.endswith(".f32"):
            continue
        a = array.array("f"); a.frombytes(open(os.path.join(d, f), "rb").read())
        n = int(RATE * SECONDS)
        y = [a[i % len(a)] for i in range(n)]
        g = 10 ** (PEAK_DB / 20) / max(abs(v) for v in y)
        nf = int(RATE * FADE)
        for i in range(nf):
            y[i] *= i / nf; y[n - 1 - i] *= i / nf
        pcm = array.array("h", (max(-32767, min(32767, round(v * g * 32767))) for v in y))
        with wave.open(os.path.join(d, f[:-4] + ".wav"), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(pcm.tobytes())
        print("wrote", f[:-4] + ".wav", len(a), "-sample period ->", n, "samples")


if __name__ == "__main__":
    main()
