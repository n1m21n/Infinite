"""Tiny harness for checking that a piece of math in the repo is right.

A check is a function registered with @check. It gets a Ctx and records
results with ctx.expect(...) / ctx.compare(...); nothing needs a GPU or a
build. Two kinds of evidence, strongest first:

  anchors     ctx.expect_dir(...), ctx.expect_close(...) - values you can state
              from the spec alone ("the centre of an equirect looks down -Z").
  comparison  ctx.compare(...) - one implementation against another over a grid
              (repo transcription vs a reference, branch A vs branch B).

Run:  python3 tools/mathcheck/run.py [name ...]    exit 1 on any failure.
"""
import numpy as np

CHECKS = {}


def check(fn):
    CHECKS[fn.__name__] = fn
    return fn


def grid(n=201):
    """n x n pixel-centre uv grid, shape (n, n, 2), uv in (0, 1)."""
    t = (np.arange(n) + 0.5) / n
    return np.stack(np.meshgrid(t, t), -1)


def unit(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def angle_deg(a, b):
    return np.degrees(np.arccos(np.clip((unit(a) * unit(b)).sum(-1), -1, 1)))


class Ctx:
    def __init__(self, name):
        self.name, self.results = name, []

    def _add(self, ok, what, detail):
        self.results.append((ok, what, detail))

    def expect_close(self, what, got, want, tol=1e-6):
        err = float(np.max(np.abs(np.asarray(got, float) - np.asarray(want, float))))
        self._add(err <= tol, what, f"max err {err:.3g} (tol {tol:g})")

    def expect_dir(self, what, got, want, tol_deg=0.5):
        err = float(np.max(angle_deg(np.asarray(got, float), np.asarray(want, float))))
        self._add(err <= tol_deg, what, f"{err:.3f} deg off (tol {tol_deg:g})")

    def expect_true(self, what, cond, detail=""):
        self._add(bool(np.all(cond)), what, detail)

    def compare(self, what, a, b, tol_deg=0.01, mask=None):
        """Two direction maps (..., 3) must agree; mask = where both are valid."""
        ang = angle_deg(a, b)
        if mask is not None:
            ang = ang[mask]
        self._add(float(ang.max()) <= tol_deg, what,
                  f"max {ang.max():.4f} deg, {(ang > tol_deg).mean() * 100:.1f}% of points over {tol_deg:g}")

    @property
    def ok(self):
        return all(r[0] for r in self.results)


def run(names=None):
    from importlib import import_module
    import pkgutil
    import checks
    for m in pkgutil.iter_modules(checks.__path__):
        import_module("checks." + m.name)
    bad = 0
    for name, fn in CHECKS.items():
        if names and name not in names:
            continue
        ctx = Ctx(name)
        fn(ctx)
        print(f"[{'PASS' if ctx.ok else 'FAIL'}] {name}")
        for ok, what, detail in ctx.results:
            if not ok or names:
                print(f"    {'ok  ' if ok else 'FAIL'} {what}: {detail}")
        bad += not ctx.ok
    print(f"{len(CHECKS) if not names else len(names)} checks, {bad} failed")
    return 1 if bad else 0
