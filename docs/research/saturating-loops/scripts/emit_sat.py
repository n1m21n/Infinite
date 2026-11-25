"""Phase 2: write L and G as Field sample kernels with their saturators (.field) plus the knob settings
to dump (.grid), and give the Python Lur'e reference (A, B, C, shapes) for every kernel, so
phase2_compare.py can check what INFINITE_FIELDSTABDUMP_SAT=1 extracts from the real interpreter.

Saturators are written in the library's Field forms (README section 8):
  tanh   e = exp(2*y), (e - 1)/(e + 1)       soft   y/(1 + abs(y))
  clamp  clamp(y, -1, 1)                     sin    sin(y)             abs    abs(y)
Each argument y goes to a local first, so the matcher sees one register for it.

L: each filter of lit.py / satloops.py written once more from its equation as Field statements (not
generated from the Python matrices), so the check compares two independent ports.
G: corpus/G.json, the Report 2 kernel with row saturators (x_i+ = phi(row_i . x)) and column
saturators (rows that aren't row-saturated read phi(x_j) instead of x_j), as gen_sat.lure builds it.

    python emit_sat.py <out dir>      writes <out>/L/*.field|.grid and <out>/G/*.field|.grid
"""
import json, pathlib, sys
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from satloops import moog_ABC  # noqa: E402
import lit  # noqa: E402
from gen_sat import lure  # noqa: E402
from emit_field import fmt, g_expr, field_text  # noqa: E402  (Report 2's, via satloops' path)
from gen_kernels import frozen_grid  # noqa: E402

def sat(shape, y, w):
    """Body lines computing w = shape(y), y already a local or a state."""
    if shape == "tanh":
        return [(f"e_{w}", f"exp(2*{y})"), (w, f"(e_{w} - 1)/(e_{w} + 1)")]
    if shape == "soft":
        return [(w, f"{y}/(1 + abs({y}))")]
    if shape == "clamp":
        return [(w, f"clamp({y}, -1, 1)")]
    if shape in ("sin", "abs"):
        return [(w, f"{shape}({y})")]
    raise ValueError(shape)

def axis(lo, hi, log, m=5):
    return [float(v) for v in (np.geomspace(lo, hi, m) if log else np.linspace(lo, hi, m))]

def grid_of(axes):
    return [tuple(x) for x in np.array(np.meshgrid(*axes, indexing="ij")).reshape(len(axes), -1).T]

# ---------------------------------------------------------------- L, from the equations

def moog_text(shape):
    body = [("y0", "in - k*s4")] + sat(shape, "y0", "w0")
    for i in range(1, 5):
        body += sat(shape, f"s{i}", f"w{i}")
    nexts = {f"s{i}": f"s{i} + g*(w{i - 1} - w{i})" for i in range(1, 5)}
    return [f"s{i}" for i in range(1, 5)], body, nexts, "s4"

def ladder_fb_text(rows):
    """Linear forward-Euler ladder s+ = s + g*(Ac0 s + e1 w0), w0 = tanh(in - k*s4)."""
    body = [("y0", "in - k*s4")] + sat("tanh", "y0", "w0")
    nexts = {}
    for i, r in enumerate(rows):
        terms = [f"{c}*s{j + 1}" if c != "1" else f"s{j + 1}" for j, c in enumerate(r) if c is not None]
        if i == 0:
            terms.append("w0")
        nexts[f"s{i + 1}"] = f"s{i + 1} + g*({' + '.join(terms)})"
    return [f"s{i}" for i in range(1, 5)], body, nexts, "s4"

def L_kernels():
    """(name, params [(name, default, lo, hi)], states, body, nexts, out, axes, ref(theta) -> ABC, shapes)."""
    box = {name: (lo, hi, log) for (name, _, lo, hi, log, *_r) in lit.L}
    out = []
    def P(name, names):
        lo, hi, log = box[name]
        ps, axes = [], []
        for i, nm in enumerate(names):
            if nm is None:
                continue
            ps.append((nm, 0.5 * (lo[i] + hi[i]), lo[i], hi[i]))
            axes.append(axis(lo[i], hi[i], i in log))
        return ps, axes
    for shape, name in (("tanh", "moog-tanh"), ("soft", "moog-soft")):
        st, body, nx, o = moog_text(shape)
        ps, ax = P(name, ["g", "k"])
        out.append((name, ps, st, body, nx, o, ax, lambda th: moog_ABC(*th), [shape] * 5))
    st, body, nx, o = ladder_fb_text([["-1"], ["1", "-1"], [None, "1", "-1"], [None, None, "1", "-1"]])
    ps, ax = P("moog-fbsat", ["g", "k"])
    out.append(("moog-fbsat", ps, st, body, nx, o, ax, lambda th: lit.moog_fbsat_ABC(*th), ["tanh"]))
    st, body, nx, o = ladder_fb_text([["-2", "1"], ["1", "-2", "1"], [None, "1", "-2", "1"], [None, None, "1", "-1"]])
    ps, ax = P("diode-fbsat", ["g", "k"])
    out.append(("diode-fbsat", ps, st, body, nx, o, ax, lambda th: lit.diode_fbsat_ABC(*th), ["tanh"]))
    # Korg35: x1+ = x1 + g(in - x1 - w), x2+ = x2 + g(x1 - x2 + w), w = tanh(k x2)
    ps, ax = P("korg35-sat", ["g", "k"])
    out.append(("korg35-sat", ps, ["x1", "x2"], [("y0", "k*x2")] + sat("tanh", "y0", "w0"),
                {"x1": "x1 + g*(in - x1 - w0)", "x2": "x2 + g*(x1 - x2 + w0)"}, "x2", ax,
                lambda th: lit.korg35_ABC(*th), ["tanh"]))
    # one-pole: y+ = y + g (tanh(in) - tanh(y)); tanh(in) has no state in it, so it is not cut
    ps, ax = P("onepole-tanh", ["g", None])
    out.append(("onepole-tanh", ps, ["y"], [("e_in", "exp(2*in)"), ("t_in", "(e_in - 1)/(e_in + 1)")]
                + sat("tanh", "y", "w0"), {"y": "y + g*(t_in - w0)"}, "y", ax,
                lambda th: lit.onepole_ABC(th[0]), ["tanh"]))
    # TDF-II, saturated states: y = phi(z1), z1+ = phi(z2) - a1 y + in, z2+ = -a2 y
    ps, ax = P("tdf2-statesat", ["a1", "a2"])
    out.append(("tdf2-statesat", ps, ["z1", "z2"], sat("tanh", "z1", "w0") + sat("tanh", "z2", "w1"),
                {"z1": "w1 - a1*w0 + in", "z2": "-a2*w0"}, "w0", ax,
                lambda th: lit.tdf2_statesat_ABC(*th), ["tanh", "tanh"]))
    ps, ax = P("tdf2-outsat", ["a1", "a2"])
    out.append(("tdf2-outsat", ps, ["z1", "z2"], sat("tanh", "z1", "w0"),
                {"z1": "z2 - a1*w0 + in", "z2": "-a2*w0"}, "w0", ax,
                lambda th: lit.tdf2_outsat_ABC(*th), ["tanh"]))
    ps, ax = P("rossum-2pole", ["a1", "a2"])
    out.append(("rossum-2pole", ps, ["y", "yp"], [("y0", "in - a1*y - a2*yp")] + sat("tanh", "y0", "w0"),
                {"y": "w0", "yp": "y"}, "y", ax, lambda th: lit.rossum_2pole_ABC(*th), ["tanh"]))
    # Chamberlin SVF, clipped band-pass: bp+ = clamp(bp + f(in - lp - q bp)), lp+ = lp + f bp+
    ps, ax = P("svf-clip", ["f", "q"])
    out.append(("svf-clip", ps, ["lp", "bp"], [("y0", "bp + f*(in - lp - q*bp)")] + sat("clamp", "y0", "w0"),
                {"lp": "lp + f*w0", "bp": "w0"}, "bp", ax, lambda th: lit.svf_clip_ABC(*th), ["clamp"]))
    # phaser: u = clamp(in + fb d); four allpasses y = a u + s, s+ = (1 - a^2) u - a s; d+ = y4
    body = [("y0", "in + fb*d")] + sat("clamp", "y0", "u0")
    nx = {}
    for i in range(1, 5):
        body.append((f"u{i}", f"a*u{i - 1} + s{i}"))
        nx[f"s{i}"] = f"(1 - a*a)*u{i - 1} - a*s{i}"
    nx["d"] = "u4"
    ps, ax = P("phaser-clip", ["a", "fb"])
    out.append(("phaser-clip", ps, ["s1", "s2", "s3", "s4", "d"], body, nx, "u4", ax,
                lambda th: lit.phaser_ABC(*th), ["clamp"]))
    return out

# ---------------------------------------------------------------- G

def g_sat_kernel(spec):
    """Report 2's compact G kernel (emit_field.g_kernel) with the C1 saturators placed."""
    n, d, sc = spec["n"], spec["d"], spec["scale"]
    rows = {s["index"]: s["shape"] for s in spec["sats"] if s["kind"] == "row"}
    cols = {s["index"]: s["shape"] for s in spec["sats"] if s["kind"] == "column"}
    params = [(f"p{i}", 0.5, 0.0, 1.0) for i in range(d)]
    states = [f"z{i}" for i in range(n)]
    body, fnames = [], {}
    for j, shape in cols.items():
        body += sat(shape, f"z{j}", f"c{j}")
    const = {i: [] for i in range(n)}
    byf = {i: {} for i in range(n)}
    for (i, j, c0, terms) in spec["entries"]:
        src = f"c{j}" if (j in cols and i not in rows) else f"z{j}"
        if c0 != 0.0:
            const[i].append(f"{fmt(sc * c0)}*{src}")
        for (c, kind, p, thr) in terms:
            key = (kind, p, thr if kind == "select" else None)
            if key not in fnames:
                fnames[key] = f"f{len(fnames)}"
                body.append((fnames[key], g_expr(kind, f"p{p}", thr)))
            byf[i].setdefault(fnames[key], []).append(f"{fmt(sc * c)}*{src}")
    nexts = {}
    for i in range(n):
        parts = const[i] + [f"{f}*({' + '.join(t)})" for f, t in byf[i].items()]
        lin = (" + ".join(parts) or "0") + (" + in" if i == 0 else "")
        if i in rows:
            body.append((f"r{i}", lin))
            body += sat(rows[i], f"r{i}", f"q{i}")
            nexts[f"z{i}"] = f"q{i}"
        else:
            nexts[f"z{i}"] = lin
    return field_text(f"C1 G set {spec['id']}, n {n}, knobs {d}, saturators {len(spec['sats'])}",
                      params, states, body, nexts, "z0"), params

# ---------------------------------------------------------------- registry

def kernels():
    """Yield (set, name, field text, params, grid, ref(theta) -> (A, B, C), shapes)."""
    for (name, ps, st, body, nx, o, ax, ref, shapes) in L_kernels():
        yield "L", name, field_text(f"C1 L {name}", ps, st, body, nx, o), ps, grid_of(ax), ref, shapes
    for set_ in ("G", "B"):  # B exists only after phase 4 generates it
        f = HERE.parent / "corpus" / f"{set_}.json"
        if not f.exists():
            continue
        for spec in json.loads(f.read_text()):
            L = lure(spec)
            txt, params = g_sat_kernel(spec)
            yield set_, spec["id"], txt, params, frozen_grid(spec["d"]), (lambda th, L=L: L.ABC(*th)), L.shapes

if __name__ == "__main__":
    out = pathlib.Path(sys.argv[1])
    count = {}
    for set_, name, txt, params, grid, _, _ in kernels():
        d = out / set_
        d.mkdir(parents=True, exist_ok=True)
        (d / f"{name}.field").write_text(txt)
        (d / f"{name}.grid").write_text("".join(" ".join(fmt(v) for v in th) + "\n" for th in grid))
        count[set_] = count.get(set_, 0) + 1
    print("wrote", out, count)
