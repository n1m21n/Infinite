#!/usr/bin/env python3
"""Measure every take that wasm_render_parts.js streams: one JSON line per take on stdout.
    node tools/universe/wasm_render_parts.js <gen dir> 9000 2 0,1,3 | python3 -I tools/universe/take_measure.py > takes.jsonl
Per take (48 kHz stereo, 2 bars):
  timbre   32 log bands 30 Hz-20 kHz, peak band = 0 (shape only)   rhythm  spectral flux summed per 16th step (32 steps)
  chroma   pitch-class energy 50 Hz-5 kHz                            centroid, low (<150 Hz), hf (>8 kHz) energy shares
  rms_db, peak_db, crest, clip (samples at full scale), corr (L/R), low_corr (L/R under 150 Hz), side_mid (rms side / rms mid),
  floor_db (10th-percentile frame rms against the loudest frame), flat (spectral flatness)
  mix      lufs (BS.1770, ungated), band shares and tilt exactly as mix_audio.py measures finished tracks
  lib      part_vs_library.measure: the numbers the sample library carries (centroid, low, lowmid, high, sub, crest, flatness, onsets/s)
The header fields from the renderer (style, id, part, seed, bpm, label, attempt, patch) pass through. Exit 0."""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import part_vs_library as pvl
import mix_audio as mixa   # BS.1770 K-weighting and the band split the 1,680 finished tracks were measured with

R = 48000
EDGES = np.geomspace(30, 20000, 33)
N, H = 2048, 1024
_fq = np.fft.rfftfreq(N, 1 / R)
_band = [(_fq >= EDGES[i]) & (_fq < EDGES[i + 1]) for i in range(32)]
_pcbin = np.full(len(_fq), -1)
for k in range(1, len(_fq)):
    if 50 < _fq[k] < 5000: _pcbin[k] = int(round(12 * np.log2(_fq[k] / 440))) % 12


def feats(x, bpm):
    """x: (n, 2) float64."""
    m = x.mean(1)
    fr = np.lib.stride_tricks.sliding_window_view(m, N)[::H] * np.hanning(N)
    S = np.abs(np.fft.rfft(fr, axis=1)) ** 2
    bands = np.array([S[:, b].sum(1) for b in _band]).T + 1e-12
    timbre = np.log10(bands.mean(0)); timbre -= timbre.max()
    flux = np.maximum(np.diff(np.log10(bands), axis=0), 0).sum(1)
    t = (np.arange(len(flux)) * H + N / 2) / R; step = 60 / bpm / 4
    idx = (t / step).astype(int); sel = idx < 32
    rh = np.bincount(idx[sel], weights=flux[sel], minlength=32)[:32]
    col = S.sum(0); ok = _pcbin >= 0
    pc = np.bincount(_pcbin[ok], weights=col[ok], minlength=12)
    tot = S.sum() + 1e-30
    frms = np.sqrt((fr ** 2).mean(1)) + 1e-9
    pk = np.abs(x).max(); rms = np.sqrt((x ** 2).mean())
    L, Rr = x[:, 0], x[:, 1]
    F = np.fft.rfftfreq(len(L), 1 / R); lowm = F < 150
    lo_l = np.fft.irfft(np.fft.rfft(L) * lowm, len(L)); lo_r = np.fft.irfft(np.fft.rfft(Rr) * lowm, len(L))
    low_corr = float(np.corrcoef(lo_l, lo_r)[0, 1]) if lo_l.std() > 1e-7 and lo_r.std() > 1e-7 else 1.0
    side_mid = float(np.sqrt(((L - Rr) ** 2).mean() / (((L + Rr) ** 2).mean() + 1e-12)))
    corr = float(np.corrcoef(L, Rr)[0, 1]) if L.std() > 0 and Rr.std() > 0 else 1.0
    e = {nm: float(S[:, (_fq >= lo) & (_fq < hi)].sum()) for nm, lo, hi in mixa.BANDS}; et = sum(e.values()) + 1e-30
    pw = mixa.kw(L, R) ** 2 + mixa.kw(Rr, R) ** 2
    mix = dict(lufs=float(-0.691 + 10 * np.log10(pw.mean() + 1e-12)), bands={k: v / et for k, v in e.items()},
               tilt_db=float(10 * np.log10((e["himid"] + e["high"] + 1e-30) / (e["sub"] + e["bass"] + 1e-12))))
    return dict(mix=mix, timbre=timbre.tolist(), rhythm=rh.tolist(), chroma=(pc / (pc.sum() + 1e-30)).tolist(),
                centroid=float((S * _fq).sum() / tot), low=float(S[:, _fq < 150].sum() / tot), hf=float(S[:, _fq > 8000].sum() / tot),
                rms_db=float(20 * np.log10(rms + 1e-9)), peak_db=float(20 * np.log10(pk + 1e-9)), crest=float(20 * np.log10(pk / (rms + 1e-9))),
                clip=int((np.abs(x) >= 0.999).sum()), corr=corr, low_corr=low_corr, side_mid=side_mid,
                floor_db=float(20 * np.log10(np.percentile(frms, 10) / frms.max())),
                flat=float(np.exp(np.log(S[:, 1:] + 1e-12).mean(1)).mean() / (S[:, 1:].mean() + 1e-30)))


def main():
    f = sys.stdin.buffer
    while True:
        h = f.readline()
        if not h: break
        meta = json.loads(h); n = meta["frames"]
        x = np.frombuffer(f.read(n * 8), dtype=np.float32).reshape(-1, 2).astype(np.float64)
        if np.abs(x).max() < 1e-6:
            meta.update(silent=True, rms_db=-200.0, clip=0)
        else:
            meta.update(feats(x, meta["bpm"]))
            lib, _ = pvl.measure(x.T); meta["lib"] = {k: v for k, v in lib.items() if k in pvl.KEYS}
        print(json.dumps(meta), flush=True)


if __name__ == "__main__":
    main()
