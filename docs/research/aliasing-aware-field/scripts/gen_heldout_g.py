#!/usr/bin/env python3
"""Generates held-out set G for phase 4: random in-driven Field Effect kernels from the sample-rate grammar.

What: writes corpus/heldout/G/g-NNN.field, N kernels (default 240) from seed 20261121 (README section 13).
      Every kernel reads `in` and writes `out`. Half or more carry memory (`state` or `delay`), so memoryless
      kernels, which the analysis predicts exactly by construction, cannot dominate the score.
      Shapes, picked uniformly per stateful kernel:
        pre    one-pole filter, then a random nonlinear expression
        post   random nonlinear expression, then a one-pole filter
        fb     a bounded nonlinearity inside a state feedback loop (|feedback| < 0.9)
        comb   a nonlinear expression plus a delayed copy of itself (feed-forward)
        mixed  one-pole on the input, a nonlinearity in feedback, a one-pole on the output
      The random expressions use every sample-rate op except tick/note/rand: + - * / % ^, abs, clamp, min, max,
      floor, ceil, exp, log, sqrt, sin, cos, tan, if, mix. Partial ops are guarded so every kernel is defined for
      every input (sqrt(abs(.)), log(1 + abs(.)), exp(clamp(., -6, 6)), tan(clamp(., -1.2, 1.2)), x / (c + abs(x))).
      A draw with no nonlinear op (a plain gain, filter or comb) is redrawn from the same stream: the scorer
      leaves linear kernels out (its is_nonlinear), as phase 1 did, so they would only shrink the set.
Why:  nobody picks these kernels, so the held-out score has no author bias, and the seed makes the set
      reproducible. The generator is committed before it is run and before any kernel is scored.
Usage: python3 scripts/gen_heldout_g.py [count]
"""
import os, random, sys

SEED = 20261121
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from score_heldout import is_nonlinear   # the scorer's own definition of "nonlinear", so the two cannot disagree
OUT = os.path.join(HERE, "..", "corpus", "heldout", "G")


def g(v):
    return f"{v:.4g}"


class Gen:
    def __init__(self, rng):
        self.r = rng

    def const(self, lo, hi):
        return g(self.r.uniform(lo, hi))

    def expr(self, leaves, depth):
        """A random expression over `leaves`, bounded for bounded input."""
        r = self.r
        if depth <= 0 or r.random() < 0.25:
            leaf = r.choice(leaves)
            return f"({leaf} * {self.const(0.5, 3.0)})" if r.random() < 0.5 else leaf
        a = self.expr(leaves, depth - 1)
        kind = r.choice(["add", "mul", "poly", "abs", "clamp", "minmax", "floor", "ceil", "exp", "log", "sqrt",
                         "sin", "cos", "tan", "if", "mix", "rat", "mod", "sat"])
        if kind == "add":
            return f"({a} + {self.expr(leaves, depth - 1)} * {self.const(0.2, 1.0)})"
        if kind == "mul":
            return f"({a} * {self.expr(leaves, depth - 1)})"
        if kind == "poly":
            return f"({a} ^ {r.choice(['2.0', '3.0'])})"
        if kind == "abs":
            return f"abs({a})"
        if kind == "clamp":
            c = self.const(0.2, 1.5)
            return f"clamp({a}, -{c}, {c})"
        if kind == "minmax":
            return f"{r.choice(['min', 'max'])}({a}, {self.const(-0.5, 0.5)})"
        if kind == "floor":
            s = self.const(2.0, 16.0)
            return f"(floor({a} * {s}) / {s})"
        if kind == "ceil":
            s = self.const(2.0, 16.0)
            return f"(ceil({a} * {s}) / {s})"
        if kind == "exp":
            return f"(exp(clamp({a}, -6.0, 6.0)) - 1.0)"
        if kind == "log":
            return f"log(1.0 + abs({a}))"
        if kind == "sqrt":
            return f"sqrt(abs({a}))"
        if kind in ("sin", "cos"):
            return f"{kind}({a} * {self.const(0.5, 4.0)})"
        if kind == "tan":
            return f"tan(clamp({a}, -1.2, 1.2))"
        if kind == "if":
            return f"if({a} > {self.const(-0.3, 0.3)}, {self.expr(leaves, depth - 1)}, {self.expr(leaves, depth - 1)})"
        if kind == "mix":
            return f"mix({a}, {self.expr(leaves, depth - 1)}, {self.const(0.1, 0.9)})"
        if kind == "rat":
            return f"({a} / ({self.const(0.5, 2.0)} + abs({a})))"
        if kind == "mod":
            return f"(({a} + 8.0) % 1.0 - 0.5)"
        # sat: a smooth bounded map written with exp, as the corpus has no tanh at sample rate
        return f"(1.0 - 2.0 / (exp(clamp(2.0 * {a}, -12.0, 12.0)) + 1.0))"

    def bounded(self, x):
        """A bounded map of x, used inside feedback so the loop cannot run away."""
        c = self.const(0.3, 1.2)
        return self.r.choice([
            f"clamp({x}, -{c}, {c})",
            f"({x} / (1.0 + abs({x})))",
            f"sin({x})",
            f"(1.0 - 2.0 / (exp(clamp(2.0 * {x}, -12.0, 12.0)) + 1.0))",
            f"clamp({x} - {x} * {x} * {x} / 3.0, -1.0, 1.0)",
        ])

    def kernel(self, stateful):
        r = self.r
        depth = r.choice([1, 2, 2, 3])
        lines = [f"x = in * {self.const(0.5, 4.0)}"]
        if not stateful:
            lines.append(f"out = {self.expr(['x'], depth)} * 0.5")
            return "memoryless", lines
        shape = r.choice(["pre", "post", "fb", "comb", "mixed"])
        if shape == "pre":
            lines = ["state float z = 0", f"z = z + {self.const(0.05, 0.6)} * (in - z)",
                     f"x = z * {self.const(0.5, 4.0)}", f"out = {self.expr(['x'], depth)} * 0.5"]
        elif shape == "post":
            lines += ["state float z = 0", f"z = z + {self.const(0.05, 0.6)} * ({self.expr(['x'], depth)} - z)",
                      "out = z * 0.5"]
        elif shape == "fb":
            fb = self.const(-0.85, 0.85)
            lines = ["state float y = 0", f"u = in * {self.const(0.5, 3.0)} + y * {fb}",
                     f"y = {self.bounded('u')}", f"out = {self.expr(['y'], max(0, depth - 1))} * 0.5"]
        elif shape == "comb":
            n = r.choice([3, 7, 13, 29, 61])
            lines += [f"w = {self.expr(['x'], depth)}", f"out = (w + {self.const(-0.7, 0.7)} * delay(w, {n})) * 0.5"]
        else:
            fb = self.const(-0.8, 0.8)
            lines = ["state float a = 0", "state float y = 0", "state float z = 0",
                     f"a = a + {self.const(0.1, 0.7)} * (in - a)",
                     f"u = a * {self.const(0.5, 3.0)} + y * {fb}", f"y = {self.bounded('u')}",
                     f"z = z + {self.const(0.1, 0.7)} * (y - z)", "out = z * 0.5"]
        return shape, lines


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 240
    rng = random.Random(SEED)
    gen = Gen(rng)
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".field"):
            os.remove(os.path.join(OUT, f))
    shapes = {}
    for i in range(count):
        stateful = i % 5 != 0 if count >= 5 else True   # 80% carry memory, interleaved
        shape, lines = gen.kernel(stateful)
        while not is_nonlinear("\n".join(lines)):
            shape, lines = gen.kernel(stateful)
        shapes[shape] = shapes.get(shape, 0) + 1
        head = f"# G set, seed {SEED}, kernel {i + 1:03d}, shape {shape}\n"
        open(os.path.join(OUT, f"g-{i + 1:03d}.field"), "w", encoding="utf-8").write(head + "\n".join(lines) + "\n")
    print(f"wrote {count} kernels to corpus/heldout/G:", ", ".join(f"{k} {v}" for k, v in sorted(shapes.items())))


if __name__ == "__main__":
    main()
