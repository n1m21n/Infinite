"""Colour maths shared by targets, pictures and sound. sRGB / D65, CIE 1931 2-degree observer."""
import numpy as np

WHITE_XYZ = np.array([0.95047, 1.0, 1.08883])  # D65, Y = 1
WHITE_XY = np.array([0.3127, 0.3290])
M_RGB2XYZ = np.array([[0.4124564, 0.3575761, 0.1804375],
                      [0.2126729, 0.7151522, 0.0721750],
                      [0.0193339, 0.1191920, 0.9503041]])
M_XYZ2RGB = np.linalg.inv(M_RGB2XYZ)


def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * c ** (1 / 2.4) - 0.055)


def _f(t):
    d = 6 / 29
    return np.where(t > d ** 3, np.cbrt(t), t / (3 * d * d) + 4 / 29)


def _finv(t):
    d = 6 / 29
    return np.where(t > d, t ** 3, 3 * d * d * (t - 4 / 29))


def linear_rgb_to_lab(rgb):
    xyz = rgb @ M_RGB2XYZ.T
    f = _f(xyz / WHITE_XYZ)
    L = 116 * f[..., 1] - 16
    a = 500 * (f[..., 0] - f[..., 1])
    b = 200 * (f[..., 1] - f[..., 2])
    return np.stack([L, a, b], -1)


def lab_to_xyz(lab):
    L, a, b = lab[..., 0], lab[..., 1], lab[..., 2]
    fy = (L + 16) / 116
    fx = fy + a / 500
    fz = fy - b / 200
    return np.stack([_finv(fx), _finv(fy), _finv(fz)], -1) * WHITE_XYZ


def lab_to_srgb(lab):
    return linear_to_srgb(lab_to_xyz(lab) @ M_XYZ2RGB.T)


# Analytic fit of the CIE 1931 2-degree colour matching functions
# (Wyman, Sloan & Shirley 2013, multi-lobe piecewise Gaussian). Good to ~1% of the tabulated CMFs.
def _g(x, mu, s1, s2):
    s = np.where(x < mu, s1, s2)
    return np.exp(-0.5 * ((x - mu) / s) ** 2)


def cmf_1931(lam_nm):
    x = (1.056 * _g(lam_nm, 599.8, 37.9, 31.0) + 0.362 * _g(lam_nm, 442.0, 16.0, 26.7)
         - 0.065 * _g(lam_nm, 501.1, 20.4, 26.2))
    y = 0.821 * _g(lam_nm, 568.8, 46.9, 40.5) + 0.286 * _g(lam_nm, 530.9, 16.3, 31.1)
    z = 1.217 * _g(lam_nm, 437.0, 11.8, 36.0) + 0.681 * _g(lam_nm, 459.0, 26.0, 13.8)
    return np.stack([x, y, z], -1)


LOCUS_NM = np.arange(400.0, 700.0 + 0.5, 1.0)  # spectral locus used for hue -> wavelength
_c = cmf_1931(LOCUS_NM)
LOCUS_XY = _c[:, :2] / _c.sum(1, keepdims=True)


def _cross(a, b):
    return a[..., 0] * b[..., 1] - a[..., 1] * b[..., 0]


def dominant_wavelength(xy):
    """xy: (2,) chromaticity. Returns dict(kind, nm or weight_red, purity).

    Ray from the D65 white point through xy. If it meets the spectral locus (400-700 nm), kind='spectral'
    with that wavelength. If it meets the straight line joining the 700 nm and 400 nm ends (the line of
    purples), kind='purple' and weight_red in [0,1] is how close the hit is to the red end.
    purity = |xy - white| / |hit - white| (excitation purity), clipped to [0, 1].
    """
    w = WHITE_XY
    d = np.asarray(xy, float) - w
    n = np.hypot(*d)
    if n < 1e-6:
        return {"kind": "grey", "nm": None, "weight_red": None, "purity": 0.0}
    pts = np.vstack([LOCUS_XY, LOCUS_XY[:1]])  # closed polygon: locus + purple line (700 -> 400)
    p0, p1 = pts[:-1], pts[1:]
    e = p1 - p0
    denom = _cross(np.broadcast_to(d, e.shape), e)
    ok = np.abs(denom) > 1e-12
    t = np.where(ok, _cross(p0 - w, e) / np.where(ok, denom, 1), -1)   # distance along ray
    u = np.where(ok, _cross(p0 - w, np.broadcast_to(d, e.shape)) / np.where(ok, denom, 1), -1)
    hit = ok & (t > 0) & (u >= 0) & (u <= 1)
    i = int(np.argmin(np.where(hit, t, np.inf)))
    purity = float(np.clip(1.0 / t[i], 0, 1))
    if i == len(e) - 1:  # purple line, from 700 nm end (u=0) to 400 nm end (u=1)
        return {"kind": "purple", "nm": None, "weight_red": float(1 - u[i]), "purity": purity}
    nm = LOCUS_NM[i] + u[i] * (LOCUS_NM[i + 1] - LOCUS_NM[i])
    return {"kind": "spectral", "nm": float(nm), "weight_red": None, "purity": purity}
