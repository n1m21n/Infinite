"""Phase 2: write sets L and G as Field sample kernels (.field) plus the knob settings to dump (.grid),
and give the Python reference A(theta) for every kernel, so phase2_compare.py can check that the
matrices INFINITE_FIELDSTABDUMP extracts from the real interpreter match.

L (literature, calibration):
  - the four McClellan (DAFx26) filters in the paper's own state form, trapezoidal:
    (I - g Ac) x' = (I + g Ac) x + 2 g Bc in, solved by unrolled Gaussian elimination (no pivoting;
    the leading minors are positive on the whole box). Not rewritten in TPT form: a state transform
    that depends on the knobs changes a time-varying system. Python reference: stabmod.discretise.
  - 12 textbook filters (one-pole, TPT one-pole, Chamberlin SVF, TPT SVF, RBJ biquad DF1 / DF2 /
    DF2T, coupled-form resonator, first-order allpass, 4-stage phaser with feedback, Stilson ladder,
    DC blocker), each written once as statements that both Python and Field evaluate. Python
    reference: the statements run in float64, columns next(e_j) - next(0).
G: corpus/G.json, knobs p0..p2 in [0, 1], A = scale * (c0 + sum c f(p)).

Every state's next value goes to a local first and the states are assigned last, so a state read
inside the body is always the previous sample's value.

    python emit_field.py <out dir>      writes <out>/L/*.field|.grid and <out>/G/*.field|.grid
"""
import json, math, pathlib, re, sys
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from stabmod import discretise, moog_Ac, diode_Ac, svf_Ac, sallenkey_Ac  # noqa: E402
from gen_kernels import A_from_spec, frozen_grid  # noqa: E402

PI = "3.14159265358979"
SR = 48000.0

def logspace(lo, hi, m):
    return [float(x) for x in np.geomspace(lo, hi, m)]

def linspace(lo, hi, m):
    return [float(x) for x in np.linspace(lo, hi, m)]

def grid_of(axes):
    return [tuple(x) for x in np.array(np.meshgrid(*axes, indexing="ij")).reshape(len(axes), -1).T]

def fmt(x):
    """%.9g, but never exponent notation (Field number literals are plain decimals)."""
    s = "%.9g" % x
    if "e" in s:
        s = ("%.15f" % x).rstrip("0").rstrip(".")
    return s

def field_text(title, params, states, body, nexts, out):
    """params: [(name, default, lo, hi)]; states: [names]; body: [(local, expr)]; nexts: {state: expr}."""
    lines = [f"# {title}"]
    lines += [f"param float {p} = {fmt(d)} [{fmt(lo)}, {fmt(hi)}]" for (p, d, lo, hi) in params]
    lines += [f"state float {s} = 0" for s in states]
    lines += [f"{v} = {e}" for (v, e) in body]
    lines += [f"nx_{s} = {nexts[s]}" for s in states]
    lines += [f"{s} = nx_{s}" for s in states]
    lines.append(f"out = {out}")
    return "\n".join(lines) + "\n"

# ---------------------------------------------------------------- paper filters (implicit trapezoidal)

PAPER = {
    # name: (Ac as strings or None, Bc input row, second param (name, default, lo, hi), numeric Ac, grid axis)
    "moog": ([["-1", None, None, "-k"], ["1", "-1", None, None], [None, "1", "-1", None], [None, None, "1", "-1"]],
             0, ("k", 1.0, 0.0, 4.0), moog_Ac, linspace(0, 4, 9)),
    "diode": ([["-2", "1", None, "-k"], ["1", "-2", "1", None], [None, "1", "-2", "1"], [None, None, "1", "-1"]],
              0, ("k", 2.0, 0.0, 10.0), diode_Ac, linspace(0, 10, 9)),
    "svf": ([["-2*R", "-1"], ["1", None]], 0, ("R", 0.5, 0.01, 2.0), svf_Ac, logspace(0.01, 2, 9)),
    "sallenkey": ([["-1", "-k"], ["1", "k - 1"]], 0, ("k", 1.0, 0.0, 2.5), sallenkey_Ac, linspace(0, 2.5, 9)),
}

def paper_kernel(name):
    Ac, bi, p2, _, _ = PAPER[name]
    n = len(Ac)
    xs = [f"x{i}" for i in range(n)]
    body = []
    # M = I - g Ac, rhs = (I + g Ac) x + 2 g Bc in
    M = [[None] * n for _ in range(n)]
    rhs = []
    for i in range(n):
        for j in range(n):
            a = Ac[i][j]
            if i == j:
                M[i][j] = f"m{i}{j}"
                body.append((M[i][j], f"1 - g*({a})" if a else "1"))
            elif a:
                M[i][j] = f"m{i}{j}"
                body.append((M[i][j], f"-g*({a})"))
        terms = [f"({Ac[i][j]})*{xs[j]}" for j in range(n) if Ac[i][j]]
        e = xs[i] + (f" + g*({' + '.join(terms)})" if terms else "") + (" + 2*g*in" if i == bi else "")
        rhs.append(f"r{i}")
        body.append((rhs[i], e))
    # forward elimination, no pivoting
    stage = 0
    for c in range(n):
        for r in range(c + 1, n):
            if M[r][c] is None:
                continue
            stage += 1
            f = f"f{r}{c}"
            body.append((f, f"{M[r][c]} / {M[c][c]}"))
            for j in range(c + 1, n):
                if M[c][j] is None:
                    continue
                v = f"m{r}{j}_{stage}"
                body.append((v, f"{M[r][j]} - {f}*{M[c][j]}" if M[r][j] else f"-{f}*{M[c][j]}"))
                M[r][j] = v
            v = f"r{r}_{stage}"
            body.append((v, f"{rhs[r]} - {f}*{rhs[c]}"))
            rhs[r] = v
            M[r][c] = None
    # back substitution
    ys = [None] * n
    for i in reversed(range(n)):
        terms = [f"{M[i][j]}*{ys[j]}" for j in range(i + 1, n) if M[i][j]]
        num = rhs[i] + "".join(f" - {t}" for t in terms)
        ys[i] = f"y{i}"
        body.append((ys[i], f"({num}) / {M[i][i]}"))
    params = [("g", 1.0, 1e-5, 1e3), p2]
    txt = field_text(f"L set: {name}, McClellan DAFx26, trapezoidal in the paper's state form", params, xs, body,
                     {xs[i]: ys[i] for i in range(n)}, xs[n - 1])
    return txt, params

def paper_ref(name, theta):
    g, q = theta
    return discretise(PAPER[name][3](q), g)

# ---------------------------------------------------------------- textbook filters (one source, two backends)

FC = ("fc", 1000.0, 20.0, 20000.0)
FC_AX = logspace(20, 20000, 9)
COEF_RBJ = [("w0", f"2*{PI}*fc/sr"), ("cw", "cos(w0)"), ("al", "sin(w0)/(2*Q)"), ("a0", "1 + al"),
            ("b0", "(1 - cw)/2/a0"), ("b1", "(1 - cw)/a0"), ("b2", "b0"), ("a1", "-2*cw/a0"), ("a2", "(1 - al)/a0")]
Q = ("Q", 0.707, 0.5, 20.0)
Q_AX = logspace(0.5, 20, 7)
AP = [("tt", f"tan({PI}*fc/sr)"), ("a", "(tt - 1)/(tt + 1)")]

TEXTBOOK = {
    "onepole": ([("a", 0.5, 0.0, 1.0)], [linspace(0, 1, 9)], ["z"], [], {"z": "z + a*(in - z)"}),
    "tpt_onepole": ([FC], [FC_AX], ["s"],
                    [("gg", f"tan({PI}*fc/sr)"), ("G", "gg/(1 + gg)"), ("v", "(in - s)*G"), ("y", "v + s")],
                    {"s": "y + v"}),
    "chamberlin_svf": ([FC, ("q", 0.5, 0.05, 2.0)], [FC_AX, logspace(0.05, 2, 7)], ["lp", "bp"],
                       [("f", f"2*sin({PI}*fc/sr)"), ("lp2", "lp + f*bp"), ("hp", "in - lp2 - q*bp")],
                       {"lp": "lp2", "bp": "f*hp + bp"}),
    "tpt_svf": ([FC, ("R", 0.5, 0.01, 2.0)], [FC_AX, logspace(0.01, 2, 7)], ["c1", "c2"],
                [("gg", f"tan({PI}*fc/sr)"), ("kk", "2*R"), ("a1", "1/(1 + gg*(gg + kk))"), ("a2", "gg*a1"),
                 ("a3", "gg*a2"), ("v3", "in - c2"), ("v1", "a1*c1 + a2*v3"), ("v2", "c2 + a2*c1 + a3*v3")],
                {"c1": "2*v1 - c1", "c2": "2*v2 - c2"}),
    "rbj_df1": ([FC, Q], [FC_AX, Q_AX], ["x1", "x2", "y1", "y2"],
                COEF_RBJ + [("y", "b0*in + b1*x1 + b2*x2 - a1*y1 - a2*y2")],
                {"x1": "in", "x2": "x1", "y1": "y", "y2": "y1"}),
    "rbj_df2": ([FC, Q], [FC_AX, Q_AX], ["w1", "w2"],
                COEF_RBJ + [("w", "in - a1*w1 - a2*w2"), ("y", "b0*w + b1*w1 + b2*w2")],
                {"w1": "w", "w2": "w1"}),
    "rbj_df2t": ([FC, Q], [FC_AX, Q_AX], ["s1", "s2"],
                 COEF_RBJ + [("y", "b0*in + s1")],
                 {"s1": "b1*in - a1*y + s2", "s2": "b2*in - a2*y"}),
    "coupled_resonator": ([FC, ("r", 0.99, 0.0, 1.0)], [FC_AX, linspace(0, 1, 7)], ["u", "v"],
                          [("th", f"2*{PI}*fc/sr"), ("c", "r*cos(th)"), ("s", "r*sin(th)")],
                          {"u": "c*u - s*v + in", "v": "s*u + c*v"}),
    "allpass1": ([FC], [FC_AX], ["x1", "y1"], AP + [("y", "a*in + x1 - a*y1")], {"x1": "in", "y1": "y"}),
    "phaser4": ([FC, ("fb", 0.5, 0.0, 0.95)], [FC_AX, linspace(0, 0.95, 7)], ["s1", "s2", "s3", "s4", "yo"],
                AP + [("u1", "in + fb*yo"), ("y1", "a*u1 + s1"), ("y2", "a*y1 + s2"), ("y3", "a*y2 + s3"),
                      ("y4", "a*y3 + s4")],
                {"s1": "u1 - a*y1", "s2": "y1 - a*y2", "s3": "y2 - a*y3", "s4": "y3 - a*y4", "yo": "y4"}),
    "stilson_ladder": ([("p", 0.3, 0.0, 1.0), ("k", 1.0, 0.0, 4.0)], [linspace(0, 1, 7), linspace(0, 4, 9)],
                       ["l1", "l2", "l3", "l4"],
                       [("u", "in - k*l4"), ("n1", "l1 + p*(u - l1)"), ("n2", "l2 + p*(n1 - l2)"),
                        ("n3", "l3 + p*(n2 - l3)"), ("n4", "l4 + p*(n3 - l4)")],
                       {"l1": "n1", "l2": "n2", "l3": "n3", "l4": "n4"}),
    "dc_blocker": ([("R", 0.995, 0.9, 0.9999)], [linspace(0.9, 0.9999, 9)], ["x1", "y1"],
                   [("y", "in - x1 + R*y1")], {"x1": "in", "y1": "y"}),
}

def textbook_kernel(name):
    params, _, states, body, nexts = TEXTBOOK[name]
    return field_text(f"L set: {name}, textbook form", params, states, body, nexts, states[0]), params

def textbook_step(name, theta, x, inp):
    params, _, states, body, nexts = TEXTBOOK[name]
    env = {"sin": math.sin, "cos": math.cos, "tan": math.tan, "sqrt": math.sqrt, "exp": math.exp, "sr": SR,
           "inp": inp}
    env.update({p[0]: float(v) for p, v in zip(params, theta)})
    env.update({s: float(v) for s, v in zip(states, x)})
    py = lambda e: re.sub(r"\bin\b", "inp", e)  # noqa: E731
    for v, e in body:
        env[v] = eval(py(e), {"__builtins__": {}}, env)
    return np.array([eval(py(nexts[s]), {"__builtins__": {}}, env) for s in states])

def textbook_ref(name, theta):
    k = len(TEXTBOOK[name][2])
    f0 = textbook_step(name, theta, np.zeros(k), 0.0)
    return np.array([textbook_step(name, theta, np.eye(k)[j], 0.0) - f0 for j in range(k)]).T

# ---------------------------------------------------------------- G

def g_expr(kind, p, thr):
    if kind == "lin": return p
    if kind == "sin": return f"sin({PI}*{p})"
    if kind == "sq": return f"{p}*{p}"
    if kind == "exp": return f"(exp({p}) - 1)"
    if kind == "select": return f"if({p} > {fmt(thr)}, 1, 0)"
    raise ValueError(kind)

def g_kernel(spec):
    """Compact on purpose: the sample backend never reuses a register (cap 128 per kernel), so the
    scale is folded into the coefficients and each knob function is computed once and shared, and
    next_i = sum_j c0_ij z_j + sum_f f * (sum_j c_ijf z_j)."""
    n, d, sc = spec["n"], spec["d"], spec["scale"]
    params = [(f"p{i}", 0.5, 0.0, 1.0) for i in range(d)]
    states = [f"z{i}" for i in range(n)]
    body, fnames = [], {}
    const = {i: [] for i in range(n)}
    byf = {i: {} for i in range(n)}
    for (i, j, c0, terms) in spec["entries"]:
        if c0 != 0.0:
            const[i].append(f"{fmt(sc * c0)}*z{j}")
        for (c, kind, p, thr) in terms:
            key = (kind, p, thr if kind == "select" else None)
            if key not in fnames:
                fnames[key] = f"f{len(fnames)}"
                body.append((fnames[key], g_expr(kind, f"p{p}", thr)))
            byf[i].setdefault(fnames[key], []).append(f"{fmt(sc * c)}*z{j}")
    nexts = {}
    for i in range(n):
        parts = const[i] + [f"{f}*({' + '.join(t)})" for f, t in byf[i].items()]
        nexts[f"z{i}"] = (" + ".join(parts) or "0") + (" + in" if i == 0 else "")
    return field_text(f"G set {spec['id']}, n {n}, knobs {d}", params, states, body, nexts, "z0"), params

# ---------------------------------------------------------------- registry

def kernels():
    """Yield (set, name, field text, params, grid, ref(theta) -> A)."""
    for name, (_, _, _, _, ax) in PAPER.items():
        txt, params = paper_kernel(name)
        yield "L", f"paper_{name}", txt, params, grid_of([logspace(1e-4, 1e3, 9), ax]), \
            (lambda th, nm=name: paper_ref(nm, th))
    for name, (_, axes, _, _, _) in TEXTBOOK.items():
        txt, params = textbook_kernel(name)
        yield "L", name, txt, params, grid_of(axes), (lambda th, nm=name: textbook_ref(nm, th))
    for spec in json.loads((HERE.parent / "corpus" / "G.json").read_text()):
        txt, params = g_kernel(spec)
        yield "G", spec["id"], txt, params, frozen_grid(spec["d"]), (lambda th, s=spec: A_from_spec(s, th))

if __name__ == "__main__":
    out = pathlib.Path(sys.argv[1])
    count = {}
    for set_, name, txt, params, grid, _ in kernels():
        d = out / set_
        d.mkdir(parents=True, exist_ok=True)
        (d / f"{name}.field").write_text(txt)
        (d / f"{name}.grid").write_text("".join(" ".join(fmt(v) for v in th) + "\n" for th in grid))
        count[set_] = count.get(set_, 0) + 1
    print("wrote", out, count)
