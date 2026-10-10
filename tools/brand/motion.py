"""Infinite motion laws: the maths that makes a move feel like Infinite whatever the visual style.

What: one importable module (films, site, 3D, book) plus a CLI that prints the derived tables and checks brand.json.
The identity lives in the movement, not the look:
    L1 Clock    time is musical: every duration is a note value at the transport tempo (house 120 BPM, Transport.h)
    L2 Mass     one acceleration for everything: a bang-bang move covers D in T = 2*sqrt(D/a), snapped to L1
    L3 Spring   one damping ladder (slab zeta .55 is the signature); omega scales with BPM/120 so the shape stays
                musical at any tempo; the overshoot peak t = pi / (omega*sqrt(1-zeta^2)) lands on a grid step
    L4 Path     arcs come from the mark: the lemniscate branches cross at 90 deg, a move leaves at half of half
                (22.5 deg), so the bow is tan(11.25 deg)/2 = 9.9% of the chord; idle loops trace the lemniscate
    L5 Phase    loops last whole bars; siblings are offset by the golden fraction 1 - 1/phi = 0.382 of the cycle
    L6 Percept  zoom interpolates in log2 (Weber-Fechner), colour in OKLab (colour.py), rotation by shortest arc
Why: the brand had springs and durations but no reason behind them; with laws, any new move can be derived.

Usage:
    python3 tools/brand/motion.py            # print the derived tables
    python3 tools/brand/motion.py --check    # brand.json durations, staggers and app_ms sit on the 120 BPM grid (5% tol)
                                             # and app_ms matches src/app/ui/design/tokens.json
    from motion import note_ms, spring, peak_ms, travel_ms, arc, lemniscate, phase, zoom_lerp   (in other tools)
Exit codes: 0 ok, 1 a token is off the grid, 2 usage error.
"""
import json
import math
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BPM = 120.0                          # Transport.h mBpm default
PHI = (1 + 5 ** 0.5) / 2
BOW = math.tan(math.radians(11.25)) / 2
A_PX = 4 * 8 / 0.125 ** 2            # L2: 8 px (one grid step) in a 1/16 at 120 BPM -> px/s^2

# note values in beats (quarter = 1); T = triplet (x2/3), D = dotted (x3/2)
NOTES = {"1/128T": 1 / 48, "1/64": 1 / 16, "1/32T": 1 / 12, "1/32": 1 / 8, "1/16T": 1 / 6, "1/16": 1 / 4,
         "1/16D": 3 / 8, "1/8T": 1 / 3, "1/8": 1 / 2, "1/8D": 3 / 4, "1/4T": 2 / 3, "1/4": 1, "1/2T": 4 / 3,
         "1/2": 2, "bar": 4, "2 bars": 8, "4 bars": 16}


def note_ms(note, bpm=BPM):
    return NOTES[note] * 60000 / bpm


def snap(ms, bpm=BPM):
    """Nearest note value: (name, ms)."""
    return min(((n, note_ms(n, bpm)) for n in NOTES), key=lambda x: abs(math.log(x[1] / ms)))


def spring(z, w, t, bpm=BPM):
    """Step response at t seconds; omega scaled by tempo (L3)."""
    w = w * bpm / BPM
    if z >= 1:
        return 1 - math.exp(-w * t) * (1 + w * t)
    wd = w * math.sqrt(1 - z * z)
    return 1 - math.exp(-z * w * t) * (math.cos(wd * t) + z / math.sqrt(1 - z * z) * math.sin(wd * t))


def overshoot(z):
    return 0.0 if z >= 1 else math.exp(-math.pi * z / math.sqrt(1 - z * z))


def peak_ms(z, w, bpm=BPM):
    return None if z >= 1 else 1000 * math.pi / (w * bpm / BPM * math.sqrt(1 - z * z))


def settle_ms(z, w, tol=0.02, bpm=BPM):
    t, last = 0.0, 0.0
    while t < 3:
        if abs(spring(z, w, t, bpm) - 1) > tol:
            last = t
        t += 0.0005
    return 1000 * last


def travel_ms(d_px, bpm=BPM, snap_to_grid=True):
    """L2: constant acceleration, accelerate half way and brake half way."""
    ms = 1000 * 2 * math.sqrt(max(d_px, 1) / A_PX) * BPM / bpm
    return snap(ms, bpm) if snap_to_grid else ms


def arc(p0, p1, u):
    """L4: point at u in [0, 1] on the house arc from p0 to p1: a circular arc that leaves and arrives at 22.5 deg
    to the chord (bow = BOW * chord), turning left of travel in y-up coordinates. Uniform speed in u."""
    (x0, y0), (x1, y1) = p0, p1
    dx, dy = x1 - x0, y1 - y0
    c = math.hypot(dx, dy)
    if c == 0:
        return x0, y0
    th = math.radians(22.5)
    R = c / (2 * math.sin(th))
    nx, ny = -dy / c, dx / c                                  # left normal
    cx, cy = (x0 + x1) / 2 - nx * R * math.cos(th), (y0 + y1) / 2 - ny * R * math.cos(th)
    a0 = math.atan2(y0 - cy, x0 - cx)
    a = a0 + 2 * th * u * (1 if dx * (y0 - cy) - dy * (x0 - cx) < 0 else -1)
    return cx + R * math.cos(a), cy + R * math.sin(a)


def lemniscate(u, A=1.0):
    """L4: point on the mark (Bernoulli) at cycle fraction u; u = 0 and 0.5 are the crossing."""
    p = 2 * math.pi * u
    d = 1 + math.cos(p) ** 2
    return A * math.sin(p) / d, A * math.sin(p) * math.cos(p) / d


def phase(i):
    """L5: cycle offset of the i-th sibling loop."""
    return (i * (1 - 1 / PHI)) % 1.0


def zoom_lerp(s0, s1, u):
    """L6: scale interpolated in log2, so each doubling takes equal time."""
    return 2 ** ((1 - u) * math.log2(s0) + u * math.log2(s1))


def tables(B):
    print(f"L1 clock at {BPM:g} BPM")
    print("  " + "  ".join(f"{n} {note_ms(n):.0f}" for n in NOTES))
    print("\nL3 springs   overshoot  peak    nearest note     settle 2%")
    for s in B["motion"]["springs"]:
        z, w = s["zeta"], s["omega"]
        pk = peak_ms(z, w)
        nn = f"{snap(pk)[0]:6} ({snap(pk)[1]:.0f})" if pk else "-"
        print(f"  {s['name']:8} {overshoot(z) * 100:6.1f}%  {pk or 0:5.0f} ms  {nn:15}  {settle_ms(z, w):4.0f} ms")
    print(f"\nL2 travel (a = {A_PX:.0f} px/s^2)")
    for d in (8, 32, 128, 512, 1920):
        n, ms = travel_ms(d)
        print(f"  {d:5} px -> {travel_ms(d, snap_to_grid=False):5.0f} ms -> {n} ({ms:.0f})")
    print(f"\nL4 arc bow {BOW * 100:.1f}% of chord, departure 22.5 deg")
    print("L5 sibling phases " + ", ".join(f"{phase(i):.3f}" for i in range(5)))


def check(B):
    bad = 0
    m = B["motion"]
    for group in ("durations_ms", "stagger_ms", "app_ms"):
        for k, v in m[group].items():
            if not v:
                continue
            n, ms = snap(v)
            off = abs(v - ms) / ms
            flag = "ok " if off <= 0.05 else "OFF"
            bad += flag == "OFF"
            print(f"  {flag} {group[:-3]}.{k:13} {v:4} ms  nearest {n:7} {ms:6.1f} ms  ({off * 100:.0f}% off)")
    app = json.load(open(os.path.join(ROOT, "src", "app", "ui", "design", "tokens.json")))["base"]["motion_ms"]
    for k, v in app.items():
        if m["app_ms"].get(k) != v:
            print(f"  drift: brand.json app_ms.{k} = {m['app_ms'].get(k)} but tokens.json says {v}")
            bad += 1
    return 1 if bad else 0


def main():
    args = sys.argv[1:]
    if set(args) - {"--check"}:
        print(__doc__)
        return 2
    B = json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json")))
    if "--check" in args:
        return check(B)
    tables(B)
    return 0


if __name__ == "__main__":
    sys.exit(main())
