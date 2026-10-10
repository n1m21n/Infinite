"""Colour science for the brand book: OKLab/OKLCH (Ottosson 2020), WCAG 2.x contrast, APCA Lc (0.0.98G),
colour-vision-deficiency simulation (Machado, Oliveira, Fernandes 2009, severity 1.0) and OKLCH gamut mapping by
chroma reduction (the same approach as the app's Palette node since v0.5.0). No dependencies."""
import math


def _s2l(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _l2s(c):
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def rgb(h):
    h = h.lstrip("#")
    return [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]


def hexs(c):
    return "#" + "".join(f"{round(max(0, min(1, x)) * 255):02X}" for x in c)


def lin_to_oklab(r, g, b):
    l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)


def oklab_to_lin(L, a, b):
    l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    return (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
            -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)


def oklab(h):
    return lin_to_oklab(*[_s2l(x) for x in rgb(h)])


def oklch(h):
    L, a, b = oklab(h)
    return L, math.hypot(a, b), math.degrees(math.atan2(b, a)) % 360


def from_oklch(L, C, H):
    """OKLCH -> sRGB hex, gamut-mapped by reducing chroma at fixed L and H (binary search)."""
    def lin(c):
        return oklab_to_lin(L, c * math.cos(math.radians(H)), c * math.sin(math.radians(H)))
    if all(-1e-6 <= x <= 1 + 1e-6 for x in lin(C)):
        return hexs([_l2s(max(0, x)) for x in lin(C)])
    lo, hi = 0.0, C
    for _ in range(30):
        mid = (lo + hi) / 2
        if all(-1e-6 <= x <= 1 + 1e-6 for x in lin(mid)):
            lo = mid
        else:
            hi = mid
    return hexs([_l2s(max(0, x)) for x in lin(lo)])


def lum(h):
    r, g, b = [_s2l(x) for x in rgb(h)]
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def wcag(a, b):
    x, y = sorted([lum(a), lum(b)], reverse=True)
    return (x + 0.05) / (y + 0.05)


def apca(txt, bg):
    """APCA-W3 0.0.98G Lc. Positive = dark text on light, negative = light on dark. Guide only: WCAG 2.2 is the
    legal floor; APCA was pulled from the WCAG 3 draft in 2023."""
    def Y(h):
        r, g, b = rgb(h)
        y = 0.2126729 * r ** 2.4 + 0.7151522 * g ** 2.4 + 0.0721750 * b ** 2.4
        return y if y > 0.022 else y + (0.022 - y) ** 1.414
    yt, yb = Y(txt), Y(bg)
    if yb > yt:
        s = (yb ** 0.56 - yt ** 0.57) * 1.14
        return 0.0 if s < 0.1 else (s - 0.027) * 100
    s = (yb ** 0.65 - yt ** 0.62) * 1.14
    return 0.0 if s > -0.1 else (s + 0.027) * 100


_CVD = {  # Machado et al. 2009, severity 1.0, applied in linear RGB
    "protan": ((0.152286, 1.052583, -0.204868), (0.114503, 0.786281, 0.099216), (-0.003882, -0.048116, 1.051998)),
    "deutan": ((0.367322, 0.860646, -0.227968), (0.280085, 0.672501, 0.047413), (-0.011820, 0.042940, 0.968881)),
    "tritan": ((1.255528, -0.076749, -0.178779), (-0.078411, 0.930809, 0.147602), (0.004733, 0.691367, 0.303900)),
}


def cvd(h, kind):
    c = [_s2l(x) for x in rgb(h)]
    m = _CVD[kind]
    return hexs([_l2s(max(0, min(1, sum(m[i][j] * c[j] for j in range(3))))) for i in range(3)])


def delta_e(a, b):
    """deltaE OK (Euclidean in OKLab, x100 so 2 is about one just-noticeable step)."""
    return 100 * math.dist(oklab(a), oklab(b))


def ramp(hue, chroma, steps):
    """Tonal ramp at one hue. steps: {name: L}. Chroma follows a sine bell so the ends stay quiet."""
    out = {}
    for name, L in steps.items():
        c = chroma * (0.25 + 0.75 * math.sin(math.pi * min(1, max(0, (L - 0.08) / 0.9))))
        out[name] = from_oklch(L, c, hue)
    return out
