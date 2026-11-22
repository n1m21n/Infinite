#!/usr/bin/env python3
"""Brain Light: every pixel from brain data (docs/plans/bci/BRAIN_LIGHT.md, Python mirror).

The law, per engine update (one column of direct_pins.spectro, 21.3 per second):
  per sensor k (AF7 AF8 TP9 TP10, placed where they sit on the head):
    e_b   = max(z_b - 1, 0)              excess power of bin b (1..40 Hz) over the person's own
                                         calibration, z = (log p - median) / MAD; unclipped so the
                                         strongest rhythm owns the colour
    a_k   = tanh(mean(e_b) / 0.6)        strength      -> brightness
    v_k   = sum e_b^2 * exp(i * hue(b))  rhythm         -> hue = angle(v), vividness = |v| / sum e_b^2
    g_k   = tanh(mean(e_b, b >= 25) / 6) fast EMG       -> grain (only well above your own baseline)
    phi_k = Hilbert phase (2-40 Hz)      the wave       -> rings, slowed SLOW times
  per pixel: inverse-distance blend of the 4 sensors.
    hue, vividness from sum w_k a_k vivid_k exp(i hue_k)
    brightness = sum w_k a_k (0.6 + 0.4 cos(RING * d_k - phi_k / SLOW)) + grain noise
  hue(f) is linear: 1 Hz red .. 40 Hz blue-violet (alpha gold, beta green).
Nothing else is drawn: zero input gives a black frame.

usage: brain_light.py sheet  CSV_DIR OUT_DIR [VIDEO_DIR]proof sheets + clips (open/closed, blink, EMG, flat)
       brain_light.py checks CSV_DIR           L1 (no authored art), L4 (alive), L2 proxy
"""
import os
import subprocess
import sys

import numpy as np
from scipy import signal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import direct_pins as dp  # noqa: E402

FS, HOP, BINS = dp.FS, dp.HOP, dp.BINS
COL_RATE = FS / HOP  # 21.33 columns per second
W, H = 320, 200
SLOW, RING, EPS = 20.0, 40.0, 0.012
# Head map seen from above, nose up: forehead sensors top, behind-the-ear sensors bottom.
POS = np.array([[0.14, 0.16], [0.86, 0.16], [0.10, 0.86], [0.90, 0.86]])
HUE_B = np.deg2rad(280.0 * (BINS - 1) / 39.0)
FAST = BINS >= 25

_yy, _xx = np.mgrid[0:H, 0:W]
_u, _v = _xx / (W - 1), _yy / (H - 1) * (H / W)
DIST = np.stack([np.hypot(_u - p[0], _v - p[1] * (H / W)) for p in POS])
WT = 1.0 / (DIST ** 2 + EPS)
WT /= WT.sum(0)


def features(x, nm):
    """x (4, n) -> per column: a (c,4), hue (c,4), vivid (c,4), grain (c,4), phase (c,4)."""
    s = dp.spectro(x)
    z = (s - nm.med) / nm.mad
    e = np.maximum(z - 1.0, 0)
    a = np.tanh(e.mean(2) / 0.6)
    e2 = e * e
    vec = (e2 * np.exp(1j * HUE_B)).sum(2)
    hue = np.angle(vec)
    vivid = np.abs(vec) / (e2.sum(2) + 1e-9)
    grain = np.tanh(e[:, :, FAST].mean(2) / 6.0)
    xb = signal.sosfilt(dp._BP, x, axis=1)
    d = (len(dp._HIL) - 1) // 2
    im = signal.lfilter(dp._HIL, 1, xb, axis=1)
    re = np.concatenate([np.zeros((4, d)), xb[:, :-d]], axis=1)
    ph = np.unwrap(np.arctan2(im, re), axis=1)
    ends = np.arange(dp.N - 1, x.shape[1], HOP)[:len(s)]
    return a, hue, vivid, grain, ph[:, ends].T


def hsv_rgb(h, s, v):
    h = (h % (2 * np.pi)) / (np.pi / 3)
    i = np.floor(h).astype(int) % 6
    f = h - np.floor(h)
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    r = np.choose(i, [v, q, p, p, t, v])
    g = np.choose(i, [t, v, v, q, p, p])
    b = np.choose(i, [p, p, t, v, v, q])
    return np.stack([r, g, b], -1)


def frame(a, hue, vivid, grain, phase, seed=0):
    """One column of features -> (H, W, 3) uint8."""
    rng = np.random.default_rng(seed)
    wa = WT * a[:, None, None]
    vec = (wa * vivid[:, None, None] * np.exp(1j * hue[:, None, None])).sum(0)
    sat = np.clip(np.abs(vec) / (wa.sum(0) + 1e-9), 0, 1)
    ring = 0.6 + 0.4 * np.cos(RING * DIST - phase[:, None, None] / SLOW)
    noise = rng.standard_normal((H, W))
    val = (wa * ring).sum(0) + (WT * grain[:, None, None]).sum(0) * 0.5 * noise
    rgb = hsv_rgb(np.angle(vec), sat, np.clip(val, 0, 1))
    return (rgb * 255).astype(np.uint8)


def render(x, nm, cols=None):
    f = features(x, nm)
    idx = range(len(f[0])) if cols is None else cols
    return [frame(*(q[c] for q in f), seed=c) for c in idx]


# ---------------------------------------------------------------- sheets

def label(img, text):
    from PIL import Image, ImageDraw
    im = Image.fromarray(img)
    ImageDraw.Draw(im).text((6, 4), text, fill=(235, 235, 235))
    return np.asarray(im)


def grid(rows, gap=6):
    width = max(sum(t.shape[1] for t in r) + gap * (len(r) - 1) for r in rows)
    out = []
    for r in rows:
        line = []
        for i, t in enumerate(r):
            if i:
                line.append(np.full((t.shape[0], gap, 3), 250, np.uint8))
            line.append(t)
        line = np.hstack(line)
        if line.shape[1] < width:
            line = np.hstack([line, np.full((line.shape[0], width - line.shape[1], 3), 250, np.uint8)])
        out += [line, np.full((gap, width, 3), 250, np.uint8)]
    return np.vstack(out[:-1])


def save(path, img):
    from PIL import Image
    Image.fromarray(img).save(path)
    print("wrote", path)


def clip_mp4(path, frames):
    p = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
                          "-s", f"{W}x{H}", "-r", f"{COL_RATE:.4f}", "-i", "-", "-vf", "scale=960:600:flags=bicubic",
                          "-pix_fmt", "yuv420p", "-c:v", "libx264", "-crf", "18", path], stdin=subprocess.PIPE)
    for f in frames:
        p.stdin.write(f.tobytes())
    p.stdin.close()
    p.wait()
    print("wrote", path)


def best_burst(xs, nm, chans, fast):
    """Column with the biggest low (blink) or fast (EMG) excess on the given sensors."""
    s = dp.spectro(xs)
    e = np.maximum((s - nm.med) / nm.mad - 1.0, 0)
    band = FAST if fast else BINS <= 4
    score = e[:, chans][:, :, band].mean((1, 2))
    return int(np.argmax(score)), float(score.max())


def sheet(d, out, vid=None):
    vid = vid or out
    os.makedirs(out, exist_ok=True)
    os.makedirs(vid, exist_ok=True)
    sess = list(dp.sessions(d))
    picks = [s for s in sess if s[0].startswith(("sub001_in_lab", "sub006_remote_session1", "sub010_in_lab",
                                                     "sub014_remote_session0"))][:4]
    rows, sample_t = [], [1.0, 4.0, 7.0, 9.5]
    for tag, xo, xc in picks:
        cal, co, cc = dp.pair(xo, xc)
        nm = dp.Norm(dp.spectro(cal))
        for state, x in (("open", co), ("closed", cc)):
            cols = [int(t * COL_RATE) for t in sample_t]
            fr = render(x, nm, cols)
            rows.append([label(f, f"{tag[:14]} {state} {t:.0f}s") for f, t in zip(fr, sample_t)])
    save(os.path.join(out, "brain_light_open_closed.png"), grid(rows))

    # Clips: one person, open then closed, plus the biggest blink and EMG burst we can find.
    tag, xo, xc = picks[0]
    cal, co, cc = dp.pair(xo, xc)
    nm = dp.Norm(dp.spectro(cal))
    clip_mp4(os.path.join(vid, "brain_light_open_then_closed.mp4"), render(co, nm) + render(cc, nm))

    bursts = []
    for kind, chans, fast in (("blink (AF7/AF8, slow)", [0, 1], False), ("EMG (TP9/TP10, fast)", [2, 3], True)):
        best = None
        for tg, xo2, xc2 in sess:
            cal2 = xo2[:, :int(dp.CAL * FS)]
            nm2 = dp.Norm(dp.spectro(cal2))
            seg = xo2[:, int(dp.CAL * FS):]
            if seg.shape[1] < dp.N + 4 * FS:
                continue
            c, sc = best_burst(seg, nm2, chans, fast)
            if best is None or sc > best[0]:
                best = (sc, tg, seg, nm2, c)
        sc, tg, seg, nm2, c = best
        lo = max(0, c - 10)
        fr = render(seg, nm2, range(lo, lo + 4 * 5, 5))
        bursts.append([label(f, f"{kind} {tg[:12]} {(lo + 5 * i - c) / COL_RATE:+.1f}s") for i, f in enumerate(fr)])
        a0 = max(0, (c - 64) * HOP)
        clip_mp4(os.path.join(vid, f"brain_light_{'blink' if not fast else 'emg'}.mp4"),
                 render(seg[:, a0:a0 + 8 * FS], nm2))
    flat = render(np.zeros((4, 10 * FS)), nm, [50])[0]
    bursts.append([label(flat, "no signal (all zeros)")])
    save(os.path.join(out, "brain_light_bursts.png"), grid(bursts))


# ---------------------------------------------------------------- checks

def alpha_gold(frames):
    """Proxy observer: share of light that is gold (hue 45-90 deg, alpha 8-14 Hz)."""
    tot, gold = 0.0, 0.0
    for f in frames:
        r, g, b = [f[..., i].astype(float) for i in range(3)]
        mx, mn = np.maximum(np.maximum(r, g), b), np.minimum(np.minimum(r, g), b)
        hue = np.degrees(np.arctan2(np.sqrt(3) * (g - b), 2 * r - g - b)) % 360
        lum = mx
        tot += lum.sum()
        gold += lum[(hue >= 45) & (hue <= 90) & (mx - mn > 20)].sum()
    return gold / (tot + 1e-9)


def checks(d):
    sess = list(dp.sessions(d))
    tag, xo, xc = sess[1]
    cal, co, cc = dp.pair(xo, xc)
    nm = dp.Norm(dp.spectro(cal))

    z = render(np.zeros((4, 4 * FS)), nm, [20])[0]
    a1, a2 = render(cc, nm, [40])[0], render(cc, nm, [40])[0]
    sw = cc.copy()
    sw[[0, 1]] = sw[[1, 0]]
    sw[[2, 3]] = sw[[3, 2]]
    # swap left/right sensors and their calibration: the picture must mirror
    nm_sw = dp.Norm(dp.spectro(cal[[1, 0, 3, 2]]))
    m = render(sw, nm_sw, [40])[0]
    mirror_err = np.abs(m.astype(int) - a1[:, ::-1].astype(int))
    ok_zero = int(z.max()) == 0
    ok_det = np.array_equal(a1, a2)
    ok_swap = np.percentile(mirror_err, 99) <= 40  # grain noise is not mirrored, everything else is
    print(f"L1 zero input -> black: {ok_zero} (max {int(z.max())})")
    print(f"L1 same data -> same frame: {ok_det}")
    print(f"L1 swap L/R sensors -> mirrored picture: {ok_swap} (99th pct pixel diff {np.percentile(mirror_err, 99):.0f})")

    alive = []
    frozen = 0
    for tg, xo2, xc2 in sess[:20]:
        cal2, co2, _ = dp.pair(xo2, xc2)
        fr = render(co2, dp.Norm(dp.spectro(cal2)))
        ch = [np.mean(np.abs(fr[i].astype(int) - fr[i - 1].astype(int)).max(2) > 2) for i in range(1, len(fr))]
        alive.append(np.mean(ch))
        run = 0
        for c in ch:
            run = run + 1 if c < 0.01 else 0
            frozen = max(frozen, run)
    print(f"L4 eyes open, still: mean changed pixels per frame {100 * np.mean(alive):.1f}% (min session "
          f"{100 * np.min(alive):.1f}%), longest near-frozen run {frozen / COL_RATE:.2f} s")

    res = []
    for tg, xo2, xc2 in sess:
        for off in (0.0, 10.0):
            if xo2.shape[1] - (off + dp.CLIP) * FS < dp.CAL * FS:
                continue
            cal2, co2, cc2 = dp.pair(xo2, xc2, off)
            nm2 = dp.Norm(dp.spectro(cal2))
            step = range(0, 213, 8)
            res.append(alpha_gold(render(cc2, nm2, step)) > alpha_gold(render(co2, nm2, step)))
    print(f"L2 proxy (more gold light when eyes closed): {np.mean(res):.3f} ({sum(res)}/{len(res)} pairs)")


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "sheet":
        sheet(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else None)
    elif len(sys.argv) >= 3 and sys.argv[1] == "checks":
        checks(sys.argv[2])
    else:
        print(__doc__)
