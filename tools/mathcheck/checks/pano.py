"""Render 3D 360 layouts (src/nodes/Render3DPano.cpp, kPanoBody dirFor()).

Camera frame: x right, y up, -z forward. dirs() is a transcription of the
GLSL; if the shader changes, change it here too. The anchors do not depend
on the transcription being clever - they are the spec.
"""
import numpy as np
from mathcheck import check, grid, unit

PI = np.pi
FACES = {  # idx: (forward, right, up) - top row 0..2, bottom row 3..5
    0: ((1, 0, 0), (0, 0, 1), (0, 1, 0)),
    1: ((-1, 0, 0), (0, 0, -1), (0, 1, 0)),
    2: ((0, 1, 0), (1, 0, 0), (0, 0, 1)),
    3: ((0, -1, 0), (1, 0, 0), (0, 0, -1)),
    4: ((0, 0, -1), (1, 0, 0), (0, 1, 0)),  # front
    5: ((0, 0, 1), (-1, 0, 0), (0, 1, 0)),  # back
}


def equirect(uv):
    lon, lat = (uv[..., 0] - .5) * 2 * PI, (uv[..., 1] - .5) * PI
    return np.stack([np.sin(lon) * np.cos(lat), np.sin(lat), -np.cos(lon) * np.cos(lat)], -1)


def cubemap(uv):
    gx, gy = uv[..., 0] * 3, uv[..., 1] * 2
    cx, cy = np.minimum(np.floor(gx), 2), np.minimum(np.floor(gy), 1)
    s, t = (gx - cx) * 2 - 1, (gy - cy) * 2 - 1
    idx = (cx + np.where(cy > .5, 0, 3)).astype(int)
    d = np.zeros(uv.shape[:-1] + (3,))
    for i, (f, r, u) in FACES.items():
        m = idx == i
        d[m] = np.array(f) + s[m, None] * np.array(r) + t[m, None] * np.array(u)
    return unit(d)


def dome(uv, angle=180., tilt=0.):
    p = uv * 2 - 1
    rr = np.linalg.norm(p, axis=-1)
    th, t = rr * np.radians(angle) * .5, np.radians(tilt)
    c, y = np.array([0, np.cos(t), -np.sin(t)]), np.array([0, np.sin(t), np.cos(t)])
    q = np.where(rr[..., None] > 1e-6, p / np.maximum(rr[..., None], 1e-9), 0)
    d = c * np.cos(th)[..., None] + (np.array([1, 0, 0]) * q[..., :1] + y * q[..., 1:]) * np.sin(th)[..., None]
    return d, rr <= 1


def at(fn, u, v, **kw):
    r = fn(np.array([[[u, v]]]), **kw)
    return (r[0] if isinstance(r, tuple) else r)[0, 0]


@check
def pano_equirect(ctx):
    d = equirect(grid())
    ctx.expect_close("directions are unit length", np.linalg.norm(d, axis=-1), 1.0)
    ctx.expect_dir("centre looks forward (-Z)", at(equirect, .5, .5), (0, 0, -1))
    ctx.expect_dir("top row is up (+Y)", at(equirect, .5, 1 - 1e-6), (0, 1, 0))
    ctx.expect_dir("right quarter looks right (+X)", at(equirect, .75, .5), (1, 0, 0))
    ctx.expect_dir("left quarter looks left (-X)", at(equirect, .25, .5), (-1, 0, 0))
    ctx.expect_dir("left edge looks back (+Z)", at(equirect, 1e-6, .5), (0, 0, 1))


@check
def pano_cubemap(ctx):
    g = grid(300)
    ctx.expect_close("directions are unit length", np.linalg.norm(cubemap(g), axis=-1), 1.0)
    for (u, v), want, name in [((1 / 6, .75), (1, 0, 0), "top-left = right (+X)"),
                               ((.5, .75), (-1, 0, 0), "top-mid = left (-X)"),
                               ((5 / 6, .75), (0, 1, 0), "top-right = up (+Y)"),
                               ((1 / 6, .25), (0, -1, 0), "bottom-left = down (-Y)"),
                               ((.5, .25), (0, 0, -1), "bottom-mid = front (-Z)"),
                               ((5 / 6, .25), (0, 0, 1), "bottom-right = back (+Z)")]:
        ctx.expect_dir(name, at(cubemap, u, v), want)


@check
def pano_dome(ctx):
    for angle, tilt in [(180, 0), (180, 90), (220, 0), (220, 45), (200, 30)]:
        d, inside = dome(grid(), angle, tilt)
        ctx.expect_close(f"{angle}/{tilt}: unit length inside the circle",
                         np.linalg.norm(d[inside], axis=-1), 1.0)
    ctx.expect_dir("tilt 0: centre is up", at(dome, .5, .5, tilt=0.), (0, 1, 0))
    ctx.expect_dir("tilt 90: centre is forward", at(dome, .5, .5, tilt=90.), (0, 0, -1))
    ctx.expect_dir("tilt 0, 180: bottom of circle is forward (horizon)",
                   at(dome, .5, 1e-3, angle=180., tilt=0.), (0, 0, -1), tol_deg=1.0)
    ctx.expect_dir("tilt 0, 180: right of circle is +X horizon", at(dome, 1 - 1e-3, .5, angle=180.), (1, 0, 0), tol_deg=1.0)
    d, inside = dome(grid(), 180., 0.)
    ctx.expect_true("180 dome never looks below the horizon", d[inside][:, 1] > -1e-6)
