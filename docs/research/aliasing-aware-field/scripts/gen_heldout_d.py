#!/usr/bin/env python3
"""Generates held-out set D for phase 5: random in-driven Field Effect kernels that all use delay(x, N).

What: writes corpus/heldout/D/d-NNN.field, N kernels (default 60) from seed 20261122 (README section 15).
      Every kernel reads `in`, writes `out` and has at least one delay line. Uses set G's expression grammar
      (gen_heldout_g.Gen, same guards) so the two sets differ only in how memory enters. Shapes, picked uniformly:
        combff  a nonlinear expression plus a delayed copy of itself (as G's comb, longer delays)
        predel  a nonlinear expression of the input and a delayed copy of the input
        dfb     a bounded nonlinearity inside a feedback loop closed through delay(y, N) (|feedback| < 0.85)
        adaa    first-order ADAA shape: (F(x) - F(x1)) / (x - x1), F a random expression, with a fallback
                G((x + x1) / 2) when |x - x1| < 0.01, x1 = delay(x, 1)
        multi   two delay lines of different length around one nonlinearity, plus a one-pole
      Delay lengths are drawn from {1, 2, 3, 7, 13, 29, 61, 101, 240, 480}.
      A draw with no nonlinear op is redrawn from the same stream, as for G.
Why:  G's only delay shape is a feed-forward comb, and L's delay kernels were written by the author. D adds
      feedback through delay lines and ADAA structure that nobody picked. Committed before it is run and before any
      kernel is scored.
Usage: python3 scripts/gen_heldout_d.py [count]
"""
import os, random, sys

SEED = 20261122
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_heldout_g import Gen          # noqa: E402  same grammar and guards as G
from score_heldout import is_nonlinear  # noqa: E402
OUT = os.path.join(HERE, "..", "corpus", "heldout", "D")
LENGTHS = [1, 2, 3, 7, 13, 29, 61, 101, 240, 480]


def kernel(gen):
    r = gen.r
    depth = r.choice([1, 2, 2, 3])
    n = r.choice(LENGTHS)
    shape = r.choice(["combff", "predel", "dfb", "adaa", "multi"])
    head = [f"x = in * {gen.const(0.5, 4.0)}"]
    if shape == "combff":
        lines = head + [f"w = {gen.expr(['x'], depth)}",
                        f"out = (w + {gen.const(-0.7, 0.7)} * delay(w, {n})) * 0.5"]
    elif shape == "predel":
        lines = head + [f"xd = delay(x, {n})", f"out = {gen.expr(['x', 'xd'], depth)} * 0.5"]
    elif shape == "dfb":
        lines = ["state float y = 0", "yp = y", f"d = delay(yp, {n})",
                 f"u = in * {gen.const(0.5, 3.0)} + d * {gen.const(-0.85, 0.85)}",
                 f"y = {gen.bounded('u')}", f"out = {gen.expr(['y'], max(0, depth - 1))} * 0.5"]
    elif shape == "adaa":
        f = gen.expr(["x"], depth)
        gmid = gen.expr(["m"], max(0, depth - 1))
        lines = head + [f"fx = {f}", "x1 = delay(x, 1)", "f1 = delay(fx, 1)", "dx = x - x1",
                        "m = (x + x1) * 0.5", f"gm = {gmid}",
                        "out = if(abs(dx) < 0.01, gm, (fx - f1) / if(abs(dx) < 0.01, 1.0, dx)) * 0.5"]
    else:
        n2 = r.choice([v for v in LENGTHS if v != n])
        lines = ["state float z = 0"] + head + [f"a = delay(x, {n})", f"w = {gen.expr(['x', 'a'], depth)}",
                                                f"b = delay(w, {n2})",
                                                f"z = z + {gen.const(0.1, 0.7)} * (w + {gen.const(-0.6, 0.6)} * b - z)",
                                                "out = z * 0.5"]
    return shape, lines


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 60
    gen = Gen(random.Random(SEED))
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".field"):
            os.remove(os.path.join(OUT, f))
    shapes = {}
    for i in range(count):
        shape, lines = kernel(gen)
        while not is_nonlinear("\n".join(lines)):
            shape, lines = kernel(gen)
        shapes[shape] = shapes.get(shape, 0) + 1
        head = f"# D set, seed {SEED}, kernel {i + 1:03d}, shape {shape}\n"
        open(os.path.join(OUT, f"d-{i + 1:03d}.field"), "w", encoding="utf-8").write(head + "\n".join(lines) + "\n")
    print(f"wrote {count} kernels to corpus/heldout/D:", ", ".join(f"{k} {v}" for k, v in sorted(shapes.items())))


if __name__ == "__main__":
    main()
