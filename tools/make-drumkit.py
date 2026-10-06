#!/usr/bin/env python3
"""Generate the Drum Sequencer's bundled kit: assets/drumkits/infinite-basic/.

Eight mono 16-bit 44.1 kHz WAVs, one per lane of the groove library
(src/nodes/DrumPatterns.h, KitFile): kick, snare, closed hat, open hat, clap,
low tom, high tom, bell. Every sound is plain additive / subtractive synthesis
written from scratch here (sine with a pitch drop, filtered noise, inharmonic
partials), seeded so the output is the same every run. MIT, like the rest of
the repository; no third-party audio is involved.

    python3 tools/make-drumkit.py            write the WAVs
    python3 tools/make-drumkit.py --check    regenerate in memory and compare with
                                             the files on disk (+-1 LSB), exit 1 on
                                             any difference
"""

import argparse
import os
import sys
import wave

import numpy as np

SR = 44100
SEED = 540
PEAK = 0.89  # of full scale, after normalising each voice

# Same names and order as DrumPatterns::KitFile().
FILES = [
    "01-kick.wav",
    "02-snare.wav",
    "03-closed-hat.wav",
    "04-open-hat.wav",
    "05-clap.wav",
    "06-low-tom.wav",
    "07-high-tom.wav",
    "08-bell.wav",
]


def t_axis(seconds):
    return np.arange(int(round(seconds * SR)), dtype=np.float64) / SR


def decay(t, tau):
    """Exponential decay, tau = time constant in seconds."""
    return np.exp(-t / tau)


def fade_out(x, seconds=0.004):
    """Short linear fade at the very end so the file never ends on a click."""
    n = min(len(x), int(seconds * SR))
    if n > 0:
        x[-n:] *= np.linspace(1.0, 0.0, n)
    return x


def one_pole_lp(x, cutoff):
    a = 1.0 - np.exp(-2.0 * np.pi * cutoff / SR)
    y = np.empty_like(x)
    s = 0.0
    for i in range(len(x)):
        s += a * (x[i] - s)
        y[i] = s
    return y


def one_pole_hp(x, cutoff):
    return x - one_pole_lp(x, cutoff)


def band(x, lo, hi):
    return one_pole_lp(one_pole_hp(x, lo), hi)


def pitch_sweep(t, f_start, f_end, sweep_tau):
    """Sine whose frequency falls exponentially from f_start to f_end."""
    freq = f_end + (f_start - f_end) * np.exp(-t / sweep_tau)
    phase = 2.0 * np.pi * np.cumsum(freq) / SR
    return np.sin(phase)


def normalise(x):
    peak = float(np.max(np.abs(x)))
    return x * (PEAK / peak) if peak > 0 else x


def kick(rng):
    t = t_axis(0.5)
    body = pitch_sweep(t, 160.0, 46.0, 0.035) * decay(t, 0.17)
    click = one_pole_lp(rng.standard_normal(len(t)), 4000.0) * decay(t, 0.003) * 0.25
    return fade_out(normalise(body + click))


def snare(rng):
    t = t_axis(0.35)
    tone = (pitch_sweep(t, 230.0, 185.0, 0.02) * decay(t, 0.07)) * 0.6
    noise = band(rng.standard_normal(len(t)), 1500.0, 9000.0) * decay(t, 0.075)
    return fade_out(normalise(tone + noise))


def metallic(t):
    """Six inharmonic square oscillators: the classic cymbal / hat recipe."""
    freqs = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]
    out = np.zeros_like(t)
    for f in freqs:
        out += np.sign(np.sin(2.0 * np.pi * f * 2.6 * t))
    return out


def hat(rng, length, tau):
    t = t_axis(length)
    x = metallic(t) * 0.5 + rng.standard_normal(len(t)) * 0.5
    x = band(x, 6000.0, 15000.0)
    return fade_out(normalise(x * decay(t, tau)))


def closed_hat(rng):
    return hat(rng, 0.12, 0.022)


def open_hat(rng):
    return hat(rng, 0.6, 0.14)


def clap(rng):
    t = t_axis(0.3)
    noise = band(rng.standard_normal(len(t)), 700.0, 3500.0)
    env = np.zeros_like(t)
    # three tight bursts, then the room tail
    for start in (0.0, 0.011, 0.023):
        env += np.where(t >= start, decay(np.maximum(t - start, 0.0), 0.004), 0.0)
    env += np.where(t >= 0.034, decay(np.maximum(t - 0.034, 0.0), 0.07), 0.0) * 0.8
    return fade_out(normalise(noise * env))


def tom(rng, f_start, f_end, tau, length):
    t = t_axis(length)
    body = pitch_sweep(t, f_start, f_end, 0.05) * decay(t, tau)
    tick = one_pole_lp(rng.standard_normal(len(t)), 3000.0) * decay(t, 0.004) * 0.15
    return fade_out(normalise(body + tick))


def low_tom(rng):
    return tom(rng, 150.0, 92.0, 0.18, 0.5)


def high_tom(rng):
    return tom(rng, 250.0, 160.0, 0.14, 0.4)


def bell(rng):
    t = t_axis(0.9)
    out = np.zeros_like(t)
    f0 = 700.0
    # inharmonic partials, higher ones dying faster
    for ratio, amp, tau in ((1.0, 1.0, 0.35), (2.76, 0.6, 0.22), (5.40, 0.35, 0.12), (8.93, 0.18, 0.06)):
        out += amp * np.sin(2.0 * np.pi * f0 * ratio * t) * decay(t, tau)
    attack = np.minimum(t / 0.0015, 1.0)
    return fade_out(normalise(out * attack))


VOICES = [kick, snare, closed_hat, open_hat, clap, low_tom, high_tom, bell]


def render_all():
    rng = np.random.default_rng(SEED)
    out = []
    for voice in VOICES:
        pcm = np.clip(np.round(voice(rng) * 32767.0), -32768, 32767).astype(np.int16)
        out.append(pcm)
    return out


def write_wav(path, pcm):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.astype("<i2").tobytes())


def read_wav(path):
    with wave.open(path, "rb") as w:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, SR):
            raise ValueError("%s: not mono 16-bit %d Hz" % (path, SR))
        return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.int32)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_out = os.path.join(here, "..", "assets", "drumkits", "infinite-basic")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=default_out, help="output folder (default: assets/drumkits/infinite-basic)")
    ap.add_argument("--check", action="store_true", help="compare the files on disk with a fresh render (+-1 LSB)")
    args = ap.parse_args()

    rendered = render_all()
    if args.check:
        bad = 0
        for name, pcm in zip(FILES, rendered):
            path = os.path.join(args.out, name)
            if not os.path.isfile(path):
                print("MISSING %s" % name)
                bad += 1
                continue
            disk = read_wav(path)
            if len(disk) != len(pcm):
                print("LENGTH  %s: disk %d frames, generated %d" % (name, len(disk), len(pcm)))
                bad += 1
                continue
            worst = int(np.max(np.abs(disk - pcm.astype(np.int32))))
            if worst > 1:
                print("DIFFERS %s: max difference %d LSB" % (name, worst))
                bad += 1
            else:
                print("ok      %s (max difference %d LSB)" % (name, worst))
        print("make-drumkit --check: %s" % ("FAIL" if bad else "OK"))
        return 1 if bad else 0

    os.makedirs(args.out, exist_ok=True)
    for name, pcm in zip(FILES, rendered):
        write_wav(os.path.join(args.out, name), pcm)
        print("wrote %s (%d frames, peak %d)" % (name, len(pcm), int(np.max(np.abs(pcm)))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
