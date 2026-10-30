#!/usr/bin/env python3
"""Python twin of PianoNode's voice (src/nodes/PianoNode.cpp), for tuning its constants without a rebuild (PLAN item 10).
    piano_proto.py [--notes 21,33,45,...] [--vel 0.3,0.8] [--k key=value,...] [--wav out.wav]
One struck note per pitch through the same model (numba): a felt hammer (force K * compression^p, Chaigne & Askenfelt 1994)
against the string at 1/8 of its length, 1-3 unison strings plus a slow second polarisation, each a delay loop with a one-pole
loss (decay per note) and four first-order dispersion allpasses fitted to the stiff-string partials f_k = k f0 sqrt(1 + B k^2)
(Van Duyne & Smith 1994; Rauhala & Valimaki 2006), a soundboard of resonators. Prints per note: pitch by YIN (cents), contact
time, RMS of the first second, T60 of the fundamental, inharmonicity read back from the partials. Keep DEF in step with the C++."""
import argparse, math, os, sys
import numpy as np
import numba
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
SR = 48000
DEF = dict(hard=0.5, tone=0.5, decay=0.5, detune=0.3, stiff=0.5, body=0.5,
           K0=8e9, p0=2.5, mh=0.005, v0=2.5, Z=4.0, mslope=18.0, kslope=18.0, treble=8.0, beta=0.125, gain=0.1, tilt=0.75)
M_AP = 4


def stiffness_B(note, stiff):
    return min(2e-2, max(1.2e-4, 3e-4 * 2 ** (1.2 * (note - 60) / 12))) * 2 * stiff


def t60(note, decay):
    return 25.0 * 2 ** (-(note - 21) / 18.0) * 2 ** (2 * (decay - 0.5) * 2)


def ap_delay(a, w):
    # phase delay (samples) of (a + z^-1) / (1 + a z^-1)
    return (math.atan2(math.sin(w), a + math.cos(w)) - math.atan2(a * math.sin(w), 1 + a * math.cos(w))) / w


def lp_delay(a, w):
    return math.atan2(a * math.sin(w), 1 - a * math.cos(w)) / w


def loss_filter(f0, T, tone):
    """One-pole loss g (1 - a) / (1 - a z^-1) per loop pass: T60 = T at f0 and T * 10^(-1.4 (1 - tone)) at fh
    (Valimaki et al. 1996 style two-point design, closed form: |H|^2 ratio is a quadratic in a)."""
    P = SR / f0
    fh = min(0.45 * SR, max(6000.0, 2.5 * f0))
    Th = T * 10 ** (-1.4 * (1 - tone))
    g0 = 10 ** (-3 * P / (T * SR)); gh = 10 ** (-3 * P / (Th * SR))
    R = (gh / g0) ** 2
    c0 = math.cos(2 * math.pi * f0 / SR); ch = math.cos(2 * math.pi * fh / SR)
    b = (R * ch - c0) / (1 - R)
    a = -b - math.sqrt(max(0.0, b * b - 1)) if b < -1 else 0.0
    a = min(0.95, max(0.0, a))
    h0 = (1 - a) / math.sqrt(1 - 2 * a * c0 + a * a)
    return min(0.9999, g0 / h0), a


def thiran(L, f0):
    """Integer delay M plus a first-order Thiran allpass (eta) whose phase delay at f0 makes up the rest. The allpass is
    lossless, so the treble strings keep their decay (a linear interpolator is a lowpass on every loop pass)."""
    M = int(math.floor(L - 0.5)); w = 2 * math.pi * f0 / SR
    d = L - M
    for _ in range(3):  # its phase delay drifts from d at higher w; correct at the fundamental
        eta = (1 - d) / (1 + d)
        d += (L - M) - ap_delay(eta, w)
    return M, (1 - d) / (1 + d)


def fit_dispersion(f0, B, lossa):
    """Allpass coefficient and loop delay so the loop's partials land on k f0 sqrt(1 + B k^2)."""
    K = max(2, min(16, int(0.45 * SR / f0 / math.sqrt(1 + B * 64))))
    def err(a):
        w1 = 2 * math.pi * f0 * math.sqrt(1 + B) / SR
        L = (2 * math.pi / w1) - M_AP * ap_delay(a, w1) - lp_delay(lossa, w1)
        if L < 4.0: return 1e9 + (4.0 - L), L
        e = 0.0
        for k in range(2, K + 1):
            fk = k * f0 * math.sqrt(1 + B * k * k); wk = 2 * math.pi * fk / SR
            if wk > 3.0: break
            tau = L + M_AP * ap_delay(a, wk) + lp_delay(lossa, wk)
            e += (tau * fk / SR - k) ** 2  # phase error in cycles
        return e, L
    lo, hi = -0.95, 0.0
    g = (math.sqrt(5) - 1) / 2
    for _ in range(40):
        m1 = hi - g * (hi - lo); m2 = lo + g * (hi - lo)
        if err(m1)[0] < err(m2)[0]: hi = m2
        else: lo = m1
    a = 0.5 * (lo + hi)
    return a, err(a)[1]


@numba.njit(cache=True)
def render(n, Ms, etas, gains, aps, lossas, mix, K, p, mh, v0, D, body_f, body_q, body, Z):
    ns = len(Ms); N = 1 << 15; th1 = np.zeros(ns); thy = np.zeros(ns)
    buf = np.zeros((ns, N)); lp = np.zeros(ns); aps_s = np.zeros((ns, 4))
    out = np.zeros(n)
    # hammer
    yh = 0.0; vh = v0; ys = 0.0
    Fh = np.zeros(N); contact = 0; contact_end = -1
    # body resonators
    nb = len(body_f); b1 = np.zeros(nb); b2 = np.zeros(nb); ba1 = np.zeros(nb); ba2 = np.zeros(nb); bg = np.zeros(nb)
    for j in range(nb):
        w = 2 * math.pi * body_f[j] / 48000.0; r = math.exp(-w / (2 * body_q[j]))
        ba1[j] = -2 * r * math.cos(w); ba2[j] = r * r; bg[j] = (1 - r * r) * 0.5
    dt = 1.0 / 48000.0
    R = math.exp(-2 * math.pi * 20.0 / 48000.0); dci = 0.0; dco = 0.0
    for i in range(n):
        F = 0.0
        if contact_end < 0:
            c = yh - ys
            F = K * c ** p if c > 0 else 0.0
            vh -= F / mh * dt; yh += vh * dt
            fd = Fh[(i - D) % N] if i >= D else 0.0
            vs = (F - fd) / (2 * Z)
            ys += vs * dt
            if F > 0: contact = i
            if i > 32 and F == 0.0 and vh < 0 and contact > 0: contact_end = contact
        Fh[i % N] = F
        exc = (F - (Fh[(i - D) % N] if i >= D else 0.0)) / (2 * Z)
        s = 0.0
        for k in range(ns):
            xi = buf[k, (i - Ms[k]) % N] if i >= Ms[k] else 0.0
            x = etas[k] * xi + th1[k] - etas[k] * thy[k]
            th1[k] = xi; thy[k] = x
            # loss
            lp[k] = gains[k] * ((1 - lossas[k]) * x + lossas[k] * lp[k])
            y = lp[k]
            for m in range(4):
                a = aps[k]
                yo = a * y + aps_s[k, m]
                aps_s[k, m] = y - a * yo
                y = yo
            buf[k, i % N] = y + exc * mix[k]
            s += x * mix[k]
        # body
        bo = 0.0
        for j in range(nb):
            yb = bg[j] * (s - 0.0) - ba1[j] * b1[j] - ba2[j] * b2[j]
            b2[j] = b1[j]; b1[j] = yb; bo += yb
        o = (1 - body) * s + body * bo * 4.0
        dco = o - dci + R * dco; dci = o
        out[i] = dco
    return out, contact_end


BODY_F = np.array([95., 140., 210., 290., 380., 520., 700., 950., 1300., 1800., 2600., 3500.])
BODY_Q = np.array([8., 10., 12., 12., 14., 14., 16., 16., 18., 18., 20., 20.])


def voice(note, vel, d, secs=3.0):
    f0 = 440 * 2 ** ((note - 69) / 12)
    B = stiffness_B(note, d["stiff"])
    nstr = 1 if note < 33 else (2 if note < 50 else 3)
    det = d["detune"] * 6.0
    cents = {1: [0.0], 2: [-det / 2, det / 2], 3: [-det, 0.0, det]}[nstr]
    T = t60(note, d["decay"])
    delays, gains, aps, las, mix, thir = [], [], [], [], [], []
    # unison strings, then the second polarisation: slower, quieter, a hair detuned
    for c, Tk, mk in [(c, T, 1.0 / nstr) for c in cents] + [(0.4, 3 * T, 0.08)]:
        f = f0 * 2 ** (c / 1200)
        g, lossa = loss_filter(f, Tk, d["tone"])
        a, L = fit_dispersion(f / math.sqrt(1 + B), B, lossa)  # partial 1 of the stiff string lands on f itself
        delays.append(L); gains.append(g); thir.append(thiran(L, f)); aps.append(a); las.append(lossa); mix.append(mk)
    up = max(0, note - 72) / d["treble"]  # treble hammers are lighter and harder still: contact well under a period
    K = d["K0"] * 2 ** (4 * (d["hard"] - 0.5)) * 2 ** ((note - 60) / d["kslope"] + up)
    p = d["p0"] + 0.012 * (note - 21)
    v0 = d["v0"] * (0.2 + 1.8 * vel ** 1.5)
    D = max(1, int(round(d["beta"] * SR / f0)))
    x, ce = render(int(secs * SR), np.array([t[0] for t in thir]), np.array([t[1] for t in thir]), np.array(gains), np.array(aps), np.array(las), np.array(mix),
                   K, p, d["mh"] * 2 ** (-(note - 60) / d["mslope"] - up), v0, D, BODY_F, BODY_Q, d["body"], d["Z"])
    # long bass strings put out far more velocity than short treble ones: level the keyboard
    return x * d["gain"] * 10 ** (d["tilt"] * (note - 60) / 20), ce, B


def analyse(x, note, B):
    import wind_tune as wt
    f0 = 440 * 2 ** ((note - 69) / 12)
    seg = x[int(0.15 * SR):int(0.65 * SR)]
    yf, ap = wt.yin(seg[:int(0.25 * SR)], fmin=25.0, fmax=4500.0) if np.isfinite(seg).all() else (0.0, 1.0)
    cents = 1200 * np.log2(yf / f0) if yf > 0 else float("nan")
    rms = 10 * np.log10(np.mean(x[:SR] ** 2) + 1e-20)
    # T60 of the fundamental: 50 ms frames of a narrow DFT bin
    hop = int(0.05 * SR); t = np.arange(hop) / SR
    e = [abs(np.sum(x[i:i + hop] * np.exp(-2j * np.pi * f0 * t))) for i in range(int(0.2 * SR), len(x) - hop, hop)]
    e = 20 * np.log10(np.array(e) + 1e-12)
    slope = np.polyfit(np.arange(len(e)) * 0.05, e, 1)[0] if len(e) > 4 else 0.0
    return cents, ap, rms, (-60 / slope if slope < 0 else float("inf"))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--notes", default="21,28,33,40,45,52,57,64,69,76,81,88,93,100,105")
    ap.add_argument("--vel", default="0.3,0.8"); ap.add_argument("--k", default=""); ap.add_argument("--wav", default="")
    a = ap.parse_args()
    d = dict(DEF); d.update({k: float(v) for k, v in (kv.split("=") for kv in a.k.split(",") if kv)})
    allx = []
    for vel in (float(s) for s in a.vel.split(",")):
        print("vel %.1f" % vel)
        for m in (int(s) for s in a.notes.split(",")):
            x, ce, B = voice(m, vel, d)
            c, apr, rms, T = analyse(x, m, B)
            print("  %3d  cents %7.1f ap %.2f  contact %.2f ms  rms %6.1f dB  peak %.2f  T60 %.1f s (target %.1f)  B %.5f" % (
                m, c, apr, ce / SR * 1000, rms, np.max(abs(x)), T, t60(m, d["decay"]), B), flush=True)
            allx.append(x[:int(1.2 * SR)])
    if a.wav:
        import soundfile as sf
        sf.write(a.wav, np.concatenate(allx) / max(1e-9, np.max(abs(np.concatenate(allx)))) * 0.9, SR)


if __name__ == "__main__":
    main()
