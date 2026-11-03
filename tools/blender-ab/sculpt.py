"""Sculpt A/B: the same dabs through Blender's brushes and Infinite's, on the same points.

  python3 tools/blender-ab/sculpt.py --out DIR [cases...] [--set-baseline]

Blender preps the mesh and exports base.glb; Infinite's sculpt self-test runs the dabs on it
(INFINITE_SCULPTTEST_STROKES); Blender runs them in a window (its brushes need a 3D view, so a
Blender window opens for a few seconds per case). A dab with "to" is a drag: Blender spaces its
own dabs along it, Infinite puts one every 0.15 radius as the 3D View does. Each point's move is compared:
  err   rms of (Infinite move - Blender move) / rms of Blender's move, over points either moved
  gain  Infinite's total move / Blender's (1 = as strong)
  peak  largest move on each side
and both results are rendered from the same camera in Workbench (DIR/<case>/board.png)."""
import argparse, json, math, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
INFINITE = os.environ.get("INFINITE_BIN", os.path.join(REPO, "build-dev/Infinite.app/Contents/MacOS/Infinite"))
BASELINE = os.path.join(HERE, "baseline_sculpt.json")
DEER = os.environ.get("DEER_GLB", os.path.expanduser("~/Downloads/Assets/deer.glb"))
DRAW, SMOOTH, GRAB, INFLATE, PINCH, FLATTEN, CREASE = range(7)

SPHERE = {"shape": "sphere"}
TOP = [0, 0, 1.2]
SPHERE_CAM = {"azimuth": 0, "elevation": 35, "distance": 5.5, "target": [0, 0, 0.5], "fov": 35}


# A bump on top, built into the mesh, for the brushes that work on shape already there.
BUMPED = dict(SPHERE, bump={"at": [0, 0, 1], "radius": 0.35, "height": 0.15})


def _one(brush, radius=0.5, strength=0.5, mesh=SPHERE):
    return {"mesh": mesh, "camera": SPHERE_CAM,
            "dabs": [{"brush": brush, "at": TOP, "radius": radius, "strength": strength}]}


def _drag(brush, a, b, radius=0.3, strength=0.5, mesh=SPHERE):
    return {"mesh": mesh, "camera": SPHERE_CAM,
            "dabs": [{"brush": brush, "at": a, "to": b, "radius": radius, "strength": strength}]}


ACROSS = ([-0.6, 0, 1.2], [0.6, 0, 1.2])


def _deer():
    # The file's own size (0.51 m tall; Z-up: x ±0.15 width, y -0.445..-0.067 length, z ±0.256 height). Targets snap.
    # Drags, as a user sculpts: a ridge down the back, a fuller flank, a crease, then smooth over the ridge.
    k = lambda b, a, t, r, s=0.5: {"brush": b, "at": a, "to": t, "radius": r, "strength": s}
    return {"mesh": {"shape": "model", "asset": DEER},
            "dabs": [k(DRAW, [0, -0.27, 0.08], [0, -0.15, 0.08], 0.03),
                     k(INFLATE, [0.12, -0.24, -0.03], [0.12, -0.16, -0.03], 0.04),
                     k(CREASE, [0.12, -0.2, 0.0], [0.12, -0.2, -0.06], 0.02, 0.6),
                     k(SMOOTH, [0, -0.24, 0.08], [0, -0.18, 0.08], 0.04)],
            "camera": {"azimuth": 0, "elevation": 15, "distance": 1.0, "target": [0, -0.25, -0.03], "fov": 35}}


CASES = {
    "sculpt_draw": _one(DRAW),
    "sculpt_inflate": _one(INFLATE),
    "sculpt_flatten": _one(FLATTEN, mesh=BUMPED),
    "sculpt_crease": _one(CREASE, 0.4),
    "sculpt_pinch": _one(PINCH, mesh=BUMPED),
    "sculpt_smooth": _one(SMOOTH, mesh=BUMPED),
    # One drag across the top, each side spacing its own dabs.
    "drag_draw": _drag(DRAW, *ACROSS),
    "drag_inflate": _drag(INFLATE, *ACROSS),
    "drag_smooth": _drag(SMOOTH, *ACROSS, mesh=BUMPED),
    "drag_crease": _drag(CREASE, *ACROSS, radius=0.2),
    "drag_pinch": _drag(PINCH, *ACROSS, mesh=BUMPED),
    "drag_flatten": _drag(FLATTEN, *ACROSS, mesh=BUMPED),
    # Owner's deer (not in git; DEER_GLB overrides): a ridge down the back, fuller flank, a crease, a smoothed spot.
    "sculpt_deer": _deer(),
}


def inf_dabs(k):
    """Where Infinite puts dabs: one at `at`, or along a drag's path every 0.15 radius (View3DPanel's spacing)."""
    path = k.get("path")
    if not path:
        return [k["at"]]
    out, left, step = [path[0]], 0.0, k["radius"] * 0.15
    for a, b in zip(path, path[1:]):
        left += math.dist(a, b)
        if left >= step:
            out.append(b); left -= step
    return out


def run(cmd, timeout=600, env=None):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env, stdin=subprocess.DEVNULL)
    return r.stdout + r.stderr


def points(path, tag):
    lines = open(path).read().split("\n")
    i = next(k for k, l in enumerate(lines) if l.startswith(tag + " "))
    n = int(lines[i].split()[1])
    return [tuple(float(x) for x in l.split()) for l in lines[i + 1:i + 1 + n]]


def nearest_map(src, dst):
    """For each dst point, the index of the nearest src point (a hash grid; both lists are the same surface)."""
    lo = [min(p[k] for p in src) for k in range(3)]
    hi = [max(p[k] for p in src) for k in range(3)]
    cell = max(hi[k] - lo[k] for k in range(3)) / 200.0 or 1.0
    grid = {}
    for i, p in enumerate(src):
        grid.setdefault(tuple(int((p[k] - lo[k]) / cell) for k in range(3)), []).append(i)
    out = []
    for p in dst:
        c = tuple(int((p[k] - lo[k]) / cell) for k in range(3))
        best, bd = -1, 1e30
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for i in grid.get((c[0] + dx, c[1] + dy, c[2] + dz), ()):
                        dd = sum((src[i][k] - p[k]) ** 2 for k in range(3))
                        if dd < bd:
                            best, bd = i, dd
        out.append(best)
    return out


def score(d):
    bb, ba = points(os.path.join(d, "truth.txt"), "before"), points(os.path.join(d, "truth.txt"), "after")
    # Infinite is Y-up: (x, y, z) -> Blender (x, -z, y).
    zup = lambda p: (p[0], -p[2], p[1])
    ib = [zup(p) for p in points(os.path.join(d, "inf.txt"), "before")]
    ia = [zup(p) for p in points(os.path.join(d, "inf.txt"), "after")]
    m = nearest_map(ib, bb)   # Blender vertex -> Infinite point
    gap = max(math.dist(bb[v], ib[m[v]]) for v in range(len(bb)))
    sq_err = sq_b = sum_b = sum_i = 0.0
    peak_b = peak_i = 0.0
    moved = 0
    mapped = []
    for v in range(len(bb)):
        db = [ba[v][k] - bb[v][k] for k in range(3)]
        di = [ia[m[v]][k] - ib[m[v]][k] for k in range(3)]
        mapped.append(tuple(bb[v][k] + di[k] for k in range(3)))
        lb, li = math.hypot(*db), math.hypot(*di)
        peak_b, peak_i = max(peak_b, lb), max(peak_i, li)
        if lb > 1e-7 or li > 1e-7:
            moved += 1
            sq_err += sum((di[k] - db[k]) ** 2 for k in range(3))
            sq_b += lb * lb
            sum_b += lb; sum_i += li
    with open(os.path.join(d, "inf_mapped.txt"), "w") as f:
        f.write("after %d\n" % len(mapped))
        f.writelines("%g %g %g\n" % p for p in mapped)
    return {"err": round(math.sqrt(sq_err / sq_b), 4) if sq_b else None, "gain": round(sum_i / sum_b, 3) if sum_b else None,
            "peak_blender": round(peak_b, 5), "peak_infinite": round(peak_i, 5), "moved": moved,
            "points": len(bb), "match_gap": round(gap, 6)}


def board(d):
    from PIL import Image, ImageDraw
    a, b = Image.open(os.path.join(d, "truth.png")).convert("RGB"), Image.open(os.path.join(d, "inf.png")).convert("RGB")
    w, h = a.size
    out = Image.new("RGB", (w * 2, h + 28), (250, 250, 250))
    out.paste(a, (0, 28)); out.paste(b, (w, 28))
    dr = ImageDraw.Draw(out)
    dr.text((10, 8), "Blender", fill=(30, 30, 30)); dr.text((w + 10, 8), "Infinite", fill=(30, 30, 30))
    out.save(os.path.join(d, "board.png"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--out", default="/tmp/blender-ab-sculpt")
    ap.add_argument("--set-baseline", action="store_true")
    a = ap.parse_args()
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    failed = []
    script = os.path.join(HERE, "blender_sculpt.py")
    for name in a.cases or list(CASES):
        case = CASES[name]
        if case["mesh"]["shape"] == "model" and not os.path.exists(case["mesh"]["asset"]):
            print("skip %s: model file missing" % name)
            continue
        d = os.path.join(os.path.abspath(a.out), name)
        os.makedirs(d, exist_ok=True)
        json.dump(case, open(os.path.join(d, "case.json"), "w"), indent=1)
        log = run([BLENDER, "-b", "--factory-startup", "--python", script, "--", "prep", d])
        if "SCULPTPREP" not in log:
            print(log[-3000:]); sys.exit("Blender prep failed: " + name)
        dabs = json.load(open(os.path.join(d, "dabs.json")))
        with open(os.path.join(d, "dabs.txt"), "w") as f:
            for k in dabs:
                for x, y, z in inf_dabs(k):
                    f.write("%d %g %g %g %g %g %d\n" % (k["brush"], x, z, -y, k["radius"], k["strength"], k.get("invert", 0)))
        log = run([BLENDER, os.path.join(d, "base.blend"), "--python", script, "--", "sculpt", d])
        if "SCULPTDONE" not in log:
            print(log[-3000:]); sys.exit("Blender sculpt failed: " + name)
        env = dict(os.environ, INFINITE_SCULPTTEST="1", INFINITE_EXITAFTER="10",
                   INFINITE_SCULPTTEST_MODEL=os.path.join(d, "base.glb"),
                   INFINITE_SCULPTTEST_STROKES=os.path.join(d, "dabs.txt"), INFINITE_SCULPTTEST_OUT=os.path.join(d, "inf.txt"))
        inf = os.path.join(d, "inf.txt")
        for attempt in range(3):   # right after Blender's window closes the app can exit early: retry, judged by the file
            time.sleep(2 * attempt)
            if os.path.exists(inf):
                os.remove(inf)
            log = run(["script", "-q", "/dev/null", INFINITE], env=env)
            if os.path.exists(inf) and "after " in open(inf).read():
                break
        else:
            print(log[-3000:]); sys.exit("Infinite sculpt failed: " + name)
        r = score(d)
        for tag, pts in (("truth", "truth.txt"), ("inf", "inf_mapped.txt")):
            run([BLENDER, "-b", os.path.join(d, "base.blend"), "--python", script, "--", "render", d,
                 os.path.join(d, pts), os.path.join(d, tag + ".png")])
        board(d)
        r.update(case=name, baseline=base.get(name))
        print(json.dumps(r))
        if a.set_baseline and r["err"] is not None:
            base[name] = r["err"]
        elif r["err"] is not None and base.get(name) is not None and r["err"] > base[name] * 1.1:
            failed.append(name)
    if a.set_baseline:
        json.dump(base, open(BASELINE, "w"), indent=1, sort_keys=True)
        print("baseline written:", BASELINE)
    if failed:
        sys.exit("worse than baseline: " + ", ".join(failed))


if __name__ == "__main__":
    main()
