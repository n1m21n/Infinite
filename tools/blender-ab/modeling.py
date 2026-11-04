"""Modelling A/B: the same mesh and the same operation through Blender and through Infinite, compared as geometry.

  python3 tools/blender-ab/modeling.py --out DIR [cases...] [--set-baseline] [--reuse-blender]

Infinite's MODELAB self-test (src/app/selftest/ModelAbTests.cpp) writes the base meshes, then runs each case's
operations and writes the result as OBJ; Blender runs the same operations on the same base (blender_modeling.py).
Generator cases (primitives, revolve, sweep) are built from parameters on both sides. Cases are written in Blender's
Z-up; this file turns them into Infinite's Y-up. Per case:
  msd    symmetric mean surface distance (20000 area-weighted samples each side) / Blender's bbox diagonal. LOWER is better
  hd     the 99th percentile of the same distances, same normalisation
  verts, tris   welded points and triangles, Infinite vs Blender;  bbox   max extent difference / diagonal
  area   Infinite's surface area / Blender's
and a board (DIR/<case>/board.png): Blender left, Infinite right, Workbench with the edges drawn."""
import argparse, json, math, os, subprocess, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
INFINITE = os.environ.get("INFINITE_BIN", os.path.join(REPO, "build-dev/Infinite.app/Contents/MacOS/Infinite"))
BASELINE = os.path.join(HERE, "baseline_modeling.json")
Z = [0, 0, 1]
TOP = {"normal": Z, "min": 0.9, "where": [[2, ">", 0.3]]}
CAP = {"normal": Z, "min": 0.9, "where": [[2, ">", 0.3]]}   # the sphere's top patch (0.5 radius)
CORNER = {"where": [[0, ">", 0.4], [1, ">", 0.4], [2, ">", 0.4]]}
EDGE = [[-0.5, -0.5, 0.5], [0.5, -0.5, 0.5]]


def edit(prim, ops, quads="dissolve", geo=None):
    return {"kind": "edit", "prim": prim, "ops": ops, "quads": quads, "geo": geo or []}


CUBE, SPHERE = ["cube", 1], ["sphere", 16, 24]
CASES = {}
for nm, prim, sel, q in (("cube", CUBE, TOP, "dissolve"), ("sphere", SPHERE, CAP, "none")):
    CASES.update({
        nm + "_extrude": edit(prim, [{"op": "extrude", "faces": sel, "d": 0.3}], q),
        nm + "_extrude_scale": edit(prim, [{"op": "extrude", "faces": sel, "d": 0.3}, {"op": "scale", "v": [0.5, 0.5, 0.5]}], q),
        nm + "_inset": edit(prim, [{"op": "inset", "faces": sel, "w": 0.1}], q),
        nm + "_delete_face": edit(prim, [{"op": "delete_faces", "faces": sel}], q),
        nm + "_move": edit(prim, [{"op": "move", "faces": sel, "v": [0.2, 0, 0.1]}], q),
        nm + "_rotate": edit(prim, [{"op": "rotate", "faces": sel, "axis": Z, "deg": 30}], q),
        nm + "_scale": edit(prim, [{"op": "scale", "faces": sel, "v": [0.5, 0.5, 0.5]}], q),
    })
CASES["cube_delete_edge"] = edit(CUBE, [{"op": "delete_edge", "edge": EDGE}])
CASES["cube_delete_point"] = edit(CUBE, [{"op": "delete_verts", "verts": CORNER}])
CASES["cube_subdivide1"] = edit(CUBE, [], geo=[{"op": "subdivide", "levels": 1}])
CASES["cube_subdivide2"] = edit(CUBE, [], geo=[{"op": "subdivide", "levels": 2}])
CASES["sphere_subdivide1"] = edit(["sphere", 8, 12], [], quads="join", geo=[{"op": "subdivide", "levels": 1}])
CASES["sphere_smooth"] = edit(["sphere", 12, 16], [], quads="none", geo=[{"op": "smooth", "iters": 5, "strength": 0.5}])
CASES["cube_smooth"] = edit(["cube", 4], [], quads="none", geo=[{"op": "smooth", "iters": 5, "strength": 0.5}])
CASES["cube_twist"] = edit(["cube", 8], [], quads="none", geo=[{"op": "twist", "deg": 90, "axis": 2}])
CASES["cube_bevel"] = edit(CUBE, [], geo=[{"op": "bevel", "w": 0.1, "segs": 3, "angle": 30}])
for shape, kw in (("cube", {"seg": 2}), ("sphere", {"rings": 16, "sectors": 24}), ("cylinder", {"sides": 24, "rings": 1, "top": 1.0}),
                  ("torus", {"rings": 24, "sides": 12, "tube": 0.4})):
    CASES["prim_" + shape] = dict({"kind": "prim", "shape": shape}, **kw)
VASE = [[0.1, 0, -0.5], [0.4, 0, -0.3], [0.25, 0, 0.0], [0.35, 0, 0.3], [0.2, 0, 0.5]]   # x, y, z (profile in the XZ plane)
CASES["revolve_vase"] = {"kind": "revolve", "profile": VASE, "deg": 360, "steps": 24, "axis": 2}
CASES["revolve_arc"] = {"kind": "revolve", "profile": VASE, "deg": 270, "steps": 18, "axis": 2}
ARC = [[0.5 * math.cos(t * math.pi / 2 / 12), 0.5 * math.sin(t * math.pi / 2 / 12), 0] for t in range(13)]
CIRCLE = [[0.08 * math.cos(2 * math.pi * k / 12), 0.08 * math.sin(2 * math.pi * k / 12)] for k in range(12)]
SQUARE = [[-0.08, -0.08], [0.08, -0.08], [0.08, 0.08], [-0.08, 0.08]]
CASES["sweep_circle"] = {"kind": "sweep", "profile": CIRCLE, "path": ARC, "caps": True}
CASES["sweep_square"] = {"kind": "sweep", "profile": SQUARE, "path": ARC, "caps": True}
CASES["sweep_ring"] = {"kind": "sweep", "profile": CIRCLE, "path": [[0.4 * math.cos(2 * math.pi * k / 24), 0.4 * math.sin(2 * math.pi * k / 24), 0] for k in range(24)],
                       "closed_path": True, "caps": False}


def run(cmd, timeout=900, env=None):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env, stdin=subprocess.DEVNULL)
    return r.stdout + r.stderr


# ---- Blender Z-up -> Infinite Y-up -------------------------------------------------------------
inf = lambda p: [p[0], p[2], -p[1]]
INF_AXIS = {0: 0, 1: 2, 2: 1}


def read_obj(path):
    vs, fs = [], []
    for l in open(path):
        p = l.split()
        if p and p[0] == "v":
            vs.append([float(x) for x in p[1:4]])
        elif p and p[0] == "f":
            fs.append([int(x.split("/")[0]) - 1 for x in p[1:]])
    return np.array(vs).reshape(-1, 3), np.array(fs).reshape(-1, 3)


def tri_pick(vs, fs, sel):
    """Triangles of a base mesh a pick names: (centre, normal) in Blender coords."""
    out = []
    for t in fs:
        a, b, c = vs[t]
        n = np.cross(b - a, c - a); ln = np.linalg.norm(n)
        if ln < 1e-12:
            continue
        n /= ln
        cen = (a + b + c) / 3
        if "normal" in sel and n.dot(sel["normal"]) < sel.get("min", 0.9):
            continue
        if all((cen[ax] > v if op == ">" else cen[ax] < v) for ax, op, v in sel.get("where", [])):
            out.append((cen, n))
    return out


def inf_edit_lines(case, vs, fs):
    lines = []
    for op in case["ops"]:
        if "faces" in op:
            parts = [" ".join("%.5g" % x for x in inf(c) + inf(n)) for c, n in tri_pick(vs, fs, op["faces"])]
            lines.append("sel f " + " ".join(parts))
        elif "verts" in op:
            pts = [v for v in vs if all((v[ax] > x if o == ">" else v[ax] < x) for ax, o, x in op["verts"]["where"])]
            lines.append("sel v " + " ".join("%.5g %.5g %.5g" % tuple(inf(p)) for p in pts))
        elif "edge" in op:
            lines.append("sel e " + " ".join("%.5g" % x for p in op["edge"] for x in inf(p)))
        k = op["op"]
        if k == "extrude": lines.append("extrude %g" % op["d"])
        elif k == "inset": lines.append("inset %g" % op["w"])
        elif k.startswith("delete"): lines.append("delete")
        elif k == "move": lines.append("move %g %g %g" % tuple(inf(op["v"])))
        elif k == "rotate": lines.append("rotate %g %g %g %g" % (*inf(op["axis"]), op["deg"]))
        elif k == "scale": lines.append("scale %g %g %g" % (op["v"][0], op["v"][2], op["v"][1]))
    return ";".join(lines)


def inf_spec(name, c, base=None):
    L = ["case " + name]
    if c["kind"] == "prim":
        s = c["shape"]
        L.append({"cube": "prim cube %d" % c.get("seg", 1), "sphere": "prim sphere %d %d" % (c.get("rings"), c.get("sectors")) if s == "sphere" else "",
                  "cylinder": "prim cylinder %d %d %g" % (c.get("sides", 0), c.get("rings", 1), c.get("top", 1)),
                  "torus": "prim torus %d %d %g" % (c.get("rings", 0), c.get("sides", 0), c.get("tube", 0))}[s])
    elif c["kind"] == "edit":
        p = c["prim"]
        L.append("prim cube %d" % p[1] if p[0] == "cube" else "prim sphere %d %d" % (p[1], p[2]))
        for g in c["geo"]:
            o = g["op"]
            L.append({"subdivide": "subdivide %d 1" % g.get("levels", 0), "smooth": "smooth %d %g" % (g.get("iters", 0), g.get("strength", 0)),
                      "twist": "twist %g %d" % (math.radians(g.get("deg", 0)), INF_AXIS[g.get("axis", 2)]),
                      "bevel": "bevel %g %d %g" % (g.get("w", 0), g.get("segs", 1), g.get("angle", 30))}[o])
        if c["ops"]:
            L.append("edit " + inf_edit_lines(c, *base))
    elif c["kind"] == "revolve":
        pts = " ".join("%g %g %g" % tuple(inf(p)) for p in c["profile"])
        L.append("revolve %d %g %d 0 %s" % (INF_AXIS[c["axis"]], c["deg"], c["steps"], pts))
    elif c["kind"] == "sweep":
        prof = " ".join("%g %g 0" % tuple(p) for p in c["profile"])
        path = " ".join("%g %g %g" % tuple(inf(p)) for p in c["path"])
        L.append("sweep 1 1 0 %d %d %d %s %s" % (c.get("caps", True), c.get("closed_path", False), len(c["profile"]), prof, path))
    L.append("end")
    return "\n".join(L)


# ---- metrics -----------------------------------------------------------------------------------
def weld(vs, fs, tol=1e-5):
    key = np.round(vs / tol).astype(np.int64)
    _, idx, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    inv = inv.ravel()
    f = inv[fs]
    f = f[(f[:, 0] != f[:, 1]) & (f[:, 1] != f[:, 2]) & (f[:, 0] != f[:, 2])]
    used = np.unique(f)
    return vs[idx][used], np.searchsorted(used, f)


def sample(vs, fs, n, seed=1):
    a, b, c = vs[fs[:, 0]], vs[fs[:, 1]], vs[fs[:, 2]]
    ar = np.linalg.norm(np.cross(b - a, c - a), axis=1) / 2
    if ar.sum() <= 0:
        return np.zeros((0, 3)), 0.0
    rng = np.random.default_rng(seed)
    t = rng.choice(len(fs), n, p=ar / ar.sum())
    r1, r2 = np.sqrt(rng.random(n)), rng.random(n)
    p = a[t] * (1 - r1)[:, None] + b[t] * (r1 * (1 - r2))[:, None] + c[t] * (r1 * r2)[:, None]
    return p, float(ar.sum())


def nearest(p, q):
    out = np.empty(len(p))
    for i in range(0, len(p), 1000):
        d = ((p[i:i + 1000, None, :] - q[None, :, :]) ** 2).sum(-1)
        out[i:i + 1000] = np.sqrt(d.min(1))
    return out


def score(blender_obj, inf_obj):
    bv, bf = weld(*read_obj(blender_obj))
    iv, i_f = weld(*read_obj(inf_obj))
    r = {"verts": [len(iv), len(bv)], "tris": [len(i_f), len(bf)]}
    if not len(bf) or not len(i_f):
        r.update(msd=None, hd=None, bbox=None, area=None)
        return r
    lo, hi = bv.min(0), bv.max(0)
    diag = float(np.linalg.norm(hi - lo)) or 1.0
    pb, ab = sample(bv, bf, 20000)
    pi, ai = sample(iv, i_f, 20000)
    d1, d2 = nearest(pb, pi), nearest(pi, pb)
    both = np.concatenate([d1, d2])
    r.update(msd=round(float((d1.mean() + d2.mean()) / 2 / diag), 5), hd=round(float(np.percentile(both, 99) / diag), 5),
             bbox=round(float(np.abs((iv.max(0) - iv.min(0)) - (hi - lo)).max() / diag), 5), area=round(ai / ab, 3) if ab else None)
    return r


def board(d):
    from PIL import Image, ImageDraw
    a, b = Image.open(os.path.join(d, "a.png")).convert("RGB"), Image.open(os.path.join(d, "b.png")).convert("RGB")
    w, h = a.size
    out = Image.new("RGB", (w * 2, h + 28), (250, 250, 250))
    out.paste(a, (0, 28)); out.paste(b, (w, 28))
    dr = ImageDraw.Draw(out)
    dr.text((10, 8), "Blender", fill=(30, 30, 30)); dr.text((w + 10, 8), "Infinite", fill=(30, 30, 30))
    out.save(os.path.join(d, "board.png"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--out", default="/tmp/blender-ab-modeling")
    ap.add_argument("--set-baseline", action="store_true")
    ap.add_argument("--no-boards", action="store_true")
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    names = a.cases or list(CASES)
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    os.makedirs(out, exist_ok=True)
    env = lambda spec: dict(os.environ, INFINITE_MODELAB=spec, INFINITE_MODELAB_OUT=out, INFINITE_EXITAFTER="10")
    inf_run = lambda spec: run(["script", "-q", "/dev/null", INFINITE], env=env(spec))
    # 1. Infinite writes the base meshes; the picks are made on them. 2. Both sides run the operations.
    spec1 = os.path.join(out, "pass1.txt")
    open(spec1, "w").write("\n".join(inf_spec(n, dict(c, ops=[], geo=[]) if c["kind"] == "edit" else c) + "\n" for n, c in ((n, CASES[n]) for n in names)))
    inf_run(spec1)
    spec2 = os.path.join(out, "pass2.txt")
    texts = []
    for n in names:
        c = CASES[n]
        base_mesh = read_obj(os.path.join(out, n + ".base.obj")) if c["kind"] == "edit" else None
        texts.append(inf_spec(n, c, base_mesh))
    open(spec2, "w").write("\n".join(texts) + "\n")
    if "MODELAB" not in inf_run(spec2):
        sys.exit("Infinite model A/B run failed (is the dev build current?)")
    json.dump({n: CASES[n] for n in names}, open(os.path.join(out, "cases.json"), "w"))
    log = run([BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, "blender_modeling.py"), "--", "run", out])
    if "MODELDONE" not in log:
        print(log[-3000:]); sys.exit("Blender run failed")
    failed = []
    for n in names:
        if "MODELFAIL " + n in log:
            print("Blender failed:", n); failed.append(n); continue
        d = os.path.join(out, n)
        os.makedirs(d, exist_ok=True)
        os.replace(os.path.join(out, n + ".obj"), os.path.join(out, n + ".inf.obj"))
        r = score(os.path.join(out, n + ".blender.obj"), os.path.join(out, n + ".inf.obj"))
        if not a.no_boards:
            run([BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, "blender_modeling.py"), "--", "render", out, n])
            for t in ("a", "b"):
                os.replace(os.path.join(out, "%s.%s.png" % (n, t)), os.path.join(d, t + ".png"))
            board(d)
        r.update(case=n, baseline=base.get(n))
        print(json.dumps(r))
        if a.set_baseline and r["msd"] is not None:
            base[n] = r["msd"]
        elif r["msd"] is not None and base.get(n) is not None and r["msd"] > base[n] * 1.1 + 0.001:
            failed.append(n)
    if a.set_baseline:
        json.dump(base, open(BASELINE, "w"), indent=1, sort_keys=True)
        print("baseline written:", BASELINE)
    elif failed:
        sys.exit("worse than baseline / failed: " + ", ".join(failed))


if __name__ == "__main__":
    main()
