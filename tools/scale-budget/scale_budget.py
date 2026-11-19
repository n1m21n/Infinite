#!/usr/bin/env python3
"""Row 5.5 (REALISM R12, R18): memory and frame budgets for vegetation and cities built by rules.

Writes three patches and renders each headless at 1080p on the Metal engine, live and settle:
  single  one box: the engine's fixed cost at 1080p
  forest  the Sketch 3D "Recursive tree" stamped by Instance on Points on an n x n jittered ground grid
  city    the Sketch 3D "City block" preset, scaled up
and prints one row per (scene, engine): instances, GPU MB (Render 3D's readout), and
  live:   build_ms (scene upload, warm-ups excluded) and frame_ms (median warm frame, the interactive rate)
  settle: frame_ms (wall time of one exported frame: build plus --samples samples)

  python3 tools/scale-budget/scale_budget.py --out /tmp/sb [--trees 40] [--city-scale 8] [--engines live settle]
"""
import argparse, json, math, os, random, re, struct, subprocess, sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INFINITE = os.environ.get("INFINITE_BIN", os.path.join(REPO, "build/Infinite.app/Contents/MacOS/Infinite"))


def preset(name):
    src = open(os.path.join(REPO, "src/core/sketch/Sketch3DPresets.h")).read()
    m = re.search(r'\{"%s", R"\((.*?)\)"\}' % re.escape(name), src, re.S)
    if not m:
        sys.exit("no Sketch 3D preset %r" % name)
    return m.group(1)


def write_grid_glb(path, n, size, seed=7):
    """An n x n ground grid (jittered vertices, two triangles per cell): the ground and the scatter points at once."""
    rnd = random.Random(seed)
    pos = []
    step = size / (n - 1)
    for j in range(n):
        for i in range(n):
            jx = (rnd.random() - 0.5) * step * 0.8 if 0 < i < n - 1 else 0.0
            jz = (rnd.random() - 0.5) * step * 0.8 if 0 < j < n - 1 else 0.0
            pos += [-size / 2 + i * step + jx, 0.0, -size / 2 + j * step + jz]
    nrm = [0.0, 1.0, 0.0] * (n * n)
    idx = []
    for j in range(n - 1):
        for i in range(n - 1):
            a = j * n + i
            idx += [a, a + n, a + 1, a + 1, a + n, a + n + 1]
    pb = struct.pack("<%df" % len(pos), *pos)
    nb = struct.pack("<%df" % len(nrm), *nrm)
    ib = struct.pack("<%dI" % len(idx), *idx)
    binb = pb + nb + ib
    lo = [min(pos[k::3]) for k in range(3)]
    hi = [max(pos[k::3]) for k in range(3)]
    gj = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
          "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2}]}],
          "buffers": [{"byteLength": len(binb)}],
          "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(pb)},
                          {"buffer": 0, "byteOffset": len(pb), "byteLength": len(nb)},
                          {"buffer": 0, "byteOffset": len(pb) + len(nb), "byteLength": len(ib)}],
          "accessors": [{"bufferView": 0, "componentType": 5126, "count": n * n, "type": "VEC3", "min": lo, "max": hi},
                        {"bufferView": 1, "componentType": 5126, "count": n * n, "type": "VEC3"},
                        {"bufferView": 2, "componentType": 5125, "count": len(idx), "type": "SCALAR"}]}
    js = json.dumps(gj).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    binb += b"\0" * ((4 - len(binb) % 4) % 4)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binb))
    out += struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(binb), 0x004E4942) + binb
    open(path, "wb").write(out)


def patch(nodes, wires):
    lines = ["infinite-patch 1"]
    for i, (cat, typ, body) in enumerate(nodes, 1):
        lines.append("node %d %s %s" % (i, cat, typ))
        lines += body
        lines.append("end")
    return "\n".join(lines + wires) + "\n"


def code(s):
    return "  s code %s" % s.replace("\n", "\\n")


def render_node(samples):
    return ("3D", "Render 3D", ["  f width 1920", "  f height 1080", "  i tonemap None", "  b shadows 1",
                                "  i supersample 1", "  c envSky 0.55 0.65 0.8", "  c envHorizon 0.75 0.75 0.75",
                                "  c envGround 0.3 0.28 0.25", "  f envIntensity 1", "  i ptSamples %d" % samples,
                                "  i ptBounces 4"])


def forest(d, trees, samples):
    size = trees * 1.6
    write_grid_glb(os.path.join(d, "ground.glb"), trees, size)
    nodes = [("3D", "Sketch 3D", [code(preset("Recursive tree"))]),
             ("3D", "Model 3D", ["  s path %s" % os.path.join(d, "ground.glb"), "  b normalize 0", "  b recenter 0"]),
             ("3D", "Instance on Points", ["  i pointMode 0", "  i maxPoints %d" % (trees * trees), "  f instanceScale 1",
                                           "  b inheritMaterial 1"]),
             ("3D", "Material", ["  c color 0.35 0.3 0.22", "  f roughness 0.9"]),
             ("3D", "Camera", ["  f fov 50", "  f distance %g" % (size * 0.75), "  f azimuth 30", "  f elevation 22",
                               "  f targetX 0", "  f targetY 0", "  f targetZ 0"]),
             ("3D", "Light", ["  i type Directional", "  f azimuth 210", "  f elevation 40", "  f intensity 3"]),
             render_node(samples), ("Utility", "Output", [])]
    wires = ["geo 3 points 2", "geo 3 shape 1", "geo 4 0 2", "geo 7 0 3", "geo 7 1 4", "geo 7 camera 5",
             "geo 7 light_1 6", "cable 8 0 7"]
    return patch(nodes, wires)


def single(d, samples):
    """The fixed cost: one box at 1080p, so a scene's own cost is what it adds over this."""
    nodes = [("3D", "Sketch 3D", [code("function draw(t) { box(1, 1, 1); }")]),
             ("3D", "Camera", ["  f fov 50", "  f distance 4", "  f azimuth 35", "  f elevation 28"]),
             ("3D", "Light", ["  i type Directional", "  f azimuth 210", "  f elevation 40", "  f intensity 3"]),
             render_node(samples), ("Utility", "Output", [])]
    return patch(nodes, ["geo 4 0 1", "geo 4 camera 2", "geo 4 light_1 3", "cable 5 0 4"])


def city(d, scale, samples):
    nodes = [("3D", "Sketch 3D", [code(preset("City block"))]),
             ("3D", "Transform", ["  f scaleX %g" % scale, "  f scaleY %g" % scale, "  f scaleZ %g" % scale]),
             ("3D", "Camera", ["  f fov 50", "  f distance %g" % (scale * 3.2), "  f azimuth 35", "  f elevation 28",
                               "  f targetX 0", "  f targetY 0", "  f targetZ 0"]),
             ("3D", "Light", ["  i type Directional", "  f azimuth 210", "  f elevation 40", "  f intensity 3"]),
             render_node(samples), ("Utility", "Output", [])]
    wires = ["geo 2 0 1", "geo 5 0 2", "geo 5 camera 3", "geo 5 light_1 4", "cable 6 0 5"]
    return patch(nodes, wires)


def run(patch_path, d, engine):
    env = dict(os.environ, INFINITE_ENGINE_PROF="1", INFINITE_LIVE_ENGINE="7" if engine == "live" else "15")
    fd = os.path.join(d, "f_" + engine) + "/"
    os.makedirs(fd, exist_ok=True)
    r = subprocess.run([INFINITE, "--frame", patch_path, "0", fd, "--size", "1920x1080"], env=env,
                       capture_output=True, text=True, timeout=1800)
    log = r.stdout + r.stderr
    prof = [l for l in log.splitlines() if l.startswith("ENGINE_PROF ")]
    if not prof:
        return {"error": (log.strip().splitlines() or ["no output"])[-1][:200]}
    def get(line, k):
        m = re.search(r"\b%s=([\d.]+)" % k, line)
        return float(m.group(1)) if m else -1.0
    # Render 3D's own lines carry inst >= 1 (other passes print inst=0); the first is the build, the rest warm frames.
    mine = [l for l in prof if get(l, "inst") >= 1 and "render=" in l]
    if not mine:
        return {"error": "no Render 3D frame"}
    out = {"inst": int(get(mine[0], "inst")), "gpu_mb": max(get(l, "gpuMB") for l in mine)}
    if engine == "settle":
        # settle's per-pass lines time only the CPU encode (the GPU runs on), so report the first frame's wall time:
        # scene build plus all its samples, as an export pays it
        out["frame_ms"] = round(get(mine[0], "build") + get(mine[0], "render"), 1)
    else:
        # the first line's build holds the 48 export warm-ups (warmGpu); the warm frame is the median of those
        frames = []
        for l in log.splitlines():  # the warm-ups printed just before Render 3D's first line (frame=1 starts a run)
            m = re.match(r"ENGINE_PROF live frame=(\d+) ms=([\d.]+)", l)
            if m:
                frames = [] if m.group(1) == "1" else frames + [float(m.group(2))]
            elif l == mine[0]:
                break
        frames.sort()
        out["build_ms"] = round(get(mine[0], "build") - get(mine[0], "warmGpu"), 1)
        out["frame_ms"] = round(frames[len(frames) // 2], 1) if frames else get(mine[-1], "render")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="/tmp/scale-budget")
    ap.add_argument("--trees", type=int, default=40, help="forest is trees x trees")
    ap.add_argument("--city-scale", type=float, default=8.0)
    ap.add_argument("--samples", type=int, default=64, help="settle samples")
    ap.add_argument("--engines", nargs="+", default=["live", "settle"])
    ap.add_argument("--scenes", nargs="+", default=["single", "forest", "city"])
    a = ap.parse_args()
    for sc in a.scenes:
        d = os.path.join(a.out, sc)
        os.makedirs(d, exist_ok=True)
        p = os.path.join(d, sc + ".inf")
        open(p, "w").write({"single": lambda: single(d, a.samples), "forest": lambda: forest(d, a.trees, a.samples),
                            "city": lambda: city(d, a.city_scale, a.samples)}[sc]())
        for e in a.engines:
            print("BUDGET", json.dumps({"scene": sc, "engine": e, **run(p, d, e)}), flush=True)


if __name__ == "__main__":
    main()
