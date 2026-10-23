"""Writes examples/hq/hq_media/studio.hdr: a small procedural studio HDRI for the
Render 3D HQ reference scenes (0.71, S0). No outside licence: everything here is
generated. Equirectangular 1024 x 512, Radiance RGBE, uncompressed scanlines.

Sky gradient over a dark floor, a warm key softbox, a cool fill and a small rim
light, bright enough (HDR) to give clear highlights and readable reflections.

Usage: python scripts/make_hq_studio_hdr.py
"""
import math
import os

W, H = 1024, 512
OUT = os.path.join(os.path.dirname(__file__), "..", "examples", "hq", "hq_media", "studio.hdr")

# (azimuth deg, elevation deg, half width deg, half height deg, rgb radiance)
BOXES = [
    (-40.0, 30.0, 14.0, 9.0, (30.0, 26.0, 20.0)),   # key: warm, large
    (125.0, 18.0, 18.0, 12.0, (5.0, 6.5, 9.0)),     # fill: cool, soft
    (175.0, 55.0, 6.0, 4.0, (22.0, 22.0, 24.0)),    # rim: small, high, behind
]


def sky(el):
    """Sky / horizon / floor radiance for an elevation in radians."""
    t = el / (math.pi / 2)
    if t >= 0.0:
        horizon, zenith = (0.55, 0.58, 0.62), (0.22, 0.30, 0.45)
        k = t ** 0.6
        return tuple(h + (z - h) * k for h, z in zip(horizon, zenith))
    floor = (0.05, 0.045, 0.04)
    k = min(1.0, -t * 6.0)
    horizon = (0.30, 0.30, 0.31)
    return tuple(h + (f - h) * k for h, f in zip(horizon, floor))


def box(az, el, b):
    baz, bel, hw, hh, rgb = b
    daz = (math.degrees(az) - baz + 180.0) % 360.0 - 180.0
    del_ = math.degrees(el) - bel
    # Soft edge: full inside, fades over 25% of the size.
    fx = max(0.0, min(1.0, (hw * 1.25 - abs(daz)) / (hw * 0.25)))
    fy = max(0.0, min(1.0, (hh * 1.25 - abs(del_)) / (hh * 0.25)))
    w = fx * fy
    return tuple(c * w for c in rgb)


def rgbe(r, g, b):
    m = max(r, g, b)
    if m < 1e-32:
        return bytes((0, 0, 0, 0))
    e = math.frexp(m)[1]
    s = 256.0 / (2.0 ** e)
    return bytes((int(r * s), int(g * s), int(b * s), e + 128))


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    rows = []
    for y in range(H):
        el = math.pi / 2 - (y + 0.5) / H * math.pi
        row = bytearray()
        for x in range(W):
            az = (x + 0.5) / W * 2.0 * math.pi - math.pi
            c = list(sky(el))
            for b in BOXES:
                add = box(az, el, b)
                c = [ci + ai for ci, ai in zip(c, add)]
            row += rgbe(*c)
        rows.append(bytes(row))
    with open(OUT, "wb") as f:
        f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n")
        f.write(("-Y %d +X %d\n" % (H, W)).encode())
        for r in rows:
            f.write(r)
    print("wrote", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
