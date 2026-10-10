"""Run the filter subset of Field Pixel code with numpy, so a library preview can use a real photo.

The headless app cannot wire an image into a Field Pixel's declared input, so filters are evaluated here:
the code is the same text the .field file holds; every statement is `name = expr;` over vec/float values.
Only the functions the library's filters use are implemented. uv.y points up, like the app.
"""
import re
import numpy as np


class V(np.ndarray):
    """(H, W, n) float array with GLSL swizzles."""
    def _c(self, *i):
        return np.asarray(self)[..., list(i)].view(V)
    x = property(lambda s: s._c(0)); y = property(lambda s: s._c(1))
    z = property(lambda s: s._c(2)); w = property(lambda s: s._c(3))
    r = x; g = y; b = z; a = w
    xy = property(lambda s: s._c(0, 1)); rgb = property(lambda s: s._c(0, 1, 2))


def _v(a):
    a = np.asarray(a, dtype=np.float64)
    if a.ndim == 0:
        a = a.reshape(1, 1, 1)
    return a.view(V)


def _cat(args):
    parts = [_v(a) for a in args]
    H = max(p.shape[0] for p in parts); W = max(p.shape[1] for p in parts)
    return np.concatenate([np.broadcast_to(np.asarray(p), (H, W, p.shape[2])) for p in parts], axis=-1).view(V)


def _ctor(n):
    def f(*args):
        v = _cat(args)
        return _cat([v] * n) if v.shape[2] == 1 and n > 1 else v
    return f


def _clamp(x, lo, hi): return np.minimum(np.maximum(x, lo), hi).view(V)
def _mix(a, b, t): return (a * (1.0 - t) + b * t).view(V)
def _step(e, x): return (np.asarray(x) >= np.asarray(e)).astype(np.float64).view(V)
def _smooth(a, b, x):
    t = _clamp((x - a) / (b - a), 0.0, 1.0)
    return (t * t * (3.0 - 2.0 * t)).view(V)
def _dot(a, b): return np.sum(np.asarray(a * b), axis=-1, keepdims=True).view(V)
def _length(a): return np.sqrt(_dot(a, a))
def _norm(a): return (a / _length(a)).view(V)
def _fmod(a, b): return np.mod(a, b).view(V)
def _fract(a): return (np.asarray(a) - np.floor(a)).view(V)


def _sample(photo):
    H, W, _ = photo.shape
    def img(c):
        u = np.clip(np.asarray(c)[..., 0] , 0.0, 1.0) * W - 0.5
        v = (1.0 - np.clip(np.asarray(c)[..., 1], 0.0, 1.0)) * H - 0.5
        x0 = np.clip(np.floor(u).astype(int), 0, W - 1); y0 = np.clip(np.floor(v).astype(int), 0, H - 1)
        x1 = np.clip(x0 + 1, 0, W - 1); y1 = np.clip(y0 + 1, 0, H - 1)
        fx = (u - np.floor(u))[..., None]; fy = (v - np.floor(v))[..., None]
        top = photo[y0, x0] * (1 - fx) + photo[y0, x1] * fx
        bot = photo[y1, x0] * (1 - fx) + photo[y1, x1] * fx
        return (top * (1 - fy) + bot * fy).view(V)
    return img


def run(code, photo, w, h, t=0.0, overrides=None):
    """photo: float (H,W,4) 0-1 array. Returns uint8 (h,w,3)."""
    env = dict(vec2=_ctor(2), vec3=_ctor(3), vec4=_ctor(4), floor=lambda a: np.floor(a).view(V), fract=_fract, fmod=_fmod,
               abs=lambda a: np.abs(a).view(V), min=lambda a, b: np.minimum(a, b).view(V), max=lambda a, b: np.maximum(a, b).view(V),
               clamp=_clamp, mix=_mix, step=_step, smoothstep=_smooth, dot=_dot, length=_length, normalize=_norm,
               sign=lambda a: np.sign(a).view(V), sin=lambda a: np.sin(a).view(V), cos=lambda a: np.cos(a).view(V),
               sqrt=lambda a: np.sqrt(a).view(V), img=_sample(photo))
    xs = (np.arange(w) + 0.5) / w; ys = 1.0 - (np.arange(h) + 0.5) / h
    X, Y = np.meshgrid(xs, ys)
    env.update(uv=np.stack([X, Y], -1).view(V), res=_v(np.array([w, h], float)).reshape(1, 1, 2).view(V),
               aspect=w / h, t=float(t))
    over = overrides or {}
    for name, val in re.findall(r"param\s+float\s+(\w+)\s*=\s*(-?[\d.]+)", code):
        env[name] = float(over.get(name, val))
    body = "\n".join(l for l in code.splitlines() if l.strip() and not l.startswith(("param", "input", "#")))
    for stmt in [s.strip() for s in body.replace("\n", " ").split(";") if s.strip()]:
        name, expr = stmt.split("=", 1)
        env[name.strip()] = eval(expr, {"__builtins__": {}}, env)
    out = np.broadcast_to(np.asarray(env["col"]), (h, w, 3))
    return (np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)
