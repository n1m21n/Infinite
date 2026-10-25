#!/usr/bin/env python3
"""Render styled patches and check each sounds like its style: tempo, pulse, loudness, not glitchy.
    style_check.py DIR [--bin BIN] [--secs 8]      (DIR holds <style>_<seed>.inf from styled.py)
Prints one row per patch: lufs, silence, onsets/s, onset-interval regularity (CV, lower = steadier), estimated bpm, verdict."""
import argparse, glob, json, os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import styled


def wav_metrics(path):
    import wave
    w = wave.open(path); sr = w.getframerate(); ch = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768
    x = x.reshape(-1, ch).mean(1)
    hop = sr // 100
    env = np.array([np.sqrt(np.mean(x[i:i + hop] ** 2)) for i in range(0, len(x) - hop, hop)])
    flux = np.maximum(np.diff(env), 0)
    thr = flux.mean() + 1.5 * flux.std() if len(flux) else 1
    peaks = [i for i in range(1, len(flux) - 1) if flux[i] > thr and flux[i] >= flux[i - 1] and flux[i] > flux[i + 1]]
    iv = np.diff(peaks) / 100.0 if len(peaks) > 3 else np.array([])
    cv = float(iv.std() / iv.mean()) if len(iv) else 9.0
    # tempo from autocorrelation of the onset envelope, 60..200 bpm
    best = 0
    if len(flux) > 400:
        ac = np.correlate(flux - flux.mean(), flux - flux.mean(), "full")[len(flux) - 1:]
        lags = range(30, 100)
        best = 60.0 / (max(lags, key=lambda l: ac[l]) / 100.0)
    return dict(rms=float(np.sqrt(np.mean(x ** 2))), silent=float(np.mean(env < 0.003)), onsets_s=len(peaks) / (len(x) / sr), cv=cv, tempo=best)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir"); ap.add_argument("--bin", default=styled.u.DEFAULT_BIN); ap.add_argument("--secs", type=int, default=8)
    a = ap.parse_args()
    S = styled.styles()
    for inf in sorted(glob.glob(os.path.join(a.dir, "*.inf"))):
        sid = os.path.basename(inf).rsplit("_", 1)[0]
        wav = inf[:-4] + ".wav"
        subprocess.run([a.bin, "--audio-summary", inf, "/dev/null", "--duration", str(a.secs), "--wav", wav], capture_output=True, text=True)
        if not os.path.exists(wav):
            print(sid, "NO RENDER"); continue
        m = wav_metrics(wav); s = S[sid]
        lo, hi = s["bpm"]
        fold = [m["tempo"] * k for k in (0.5, 1, 2, 3, 4) if m["tempo"] and lo * 0.94 <= m["tempo"] * k <= hi * 1.06]
        m["tempo"] = fold[0] if fold else m["tempo"]
        steady = bool(fold) or m["cv"] < 0.9 or s["glitch"] >= 0.4 or s["family"] == "ambient"
        ok = m["rms"] > 0.01 and m["silent"] < 0.5 and steady
        print("%-22s rms %.3f silent %.2f onsets/s %4.1f cv %.2f tempo %5.1f (style %d-%d)  %s" % (sid, m["rms"], m["silent"], m["onsets_s"], m["cv"], m["tempo"], *s["bpm"], "ok" if ok else "FAIL"))


if __name__ == "__main__":
    main()
