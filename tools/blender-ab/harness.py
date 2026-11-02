#!/usr/bin/env python3
"""Blender vs Infinite A/B harness: one scene, both apps, one score.

Every case in cases.py is a small scene file. Blender builds it and renders
the truth with Cycles (blender_scene.py); the same file becomes an Infinite
patch (to_patch below) rendered headless. Each case gets a FLIP score, the
brightness ratio and a board row; results.jsonl holds one line per case and
the exit code says whether every case held its baseline.

  python3 tools/blender-ab/harness.py --out DIR [case ...]   # all cases by default
  python3 tools/blender-ab/harness.py --out DIR --set-baseline
  python3 tools/blender-ab/harness.py --out DIR --ao         # also the AO bake A/B (ab.py)
  python3 tools/blender-ab/harness.py --list

Blender's truth is reused while a case's scene is unchanged (scene.hash).
"""
import argparse, hashlib, json, math, os, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from cases import CASES  # noqa: E402

BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
INFINITE = os.path.join(REPO, "build-dev/Infinite.app/Contents/MacOS/Infinite")
BASELINE = os.path.join(HERE, "baseline.json")
# A case passes while its FLIP stays within this much of its baseline.
FLIP_SLACK = 0.01


def srgb(c):
    # Infinite's colour controls hold sRGB-encoded values; the scene holds linear ones.
    return [12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055 for x in (min(max(v, 0.0), 1.0) for v in c)]


def light_lines(l):
    """Scene light -> Infinite Light params. Units, by the shading maths in Geometry3DNodes.cpp:
    sun: irradiance = intensity (W/m^2, as Cycles); point/spot with hard falloff 1: irradiance =
    intensity * 4 / d^2, and Cycles' point of P watts gives P / (4 pi d^2), so intensity = P / (16 pi);
    area (normalized): far irradiance P / (pi d^2) in Cycles, so intensity = P / (4 pi)."""
    t = l["type"]
    kind = {"sun": "Directional", "point": "Point", "spot": "Spot", "area": "Area"}[t]
    p = l["power"]
    inten = {"sun": p, "point": p / (16 * math.pi), "spot": p / (16 * math.pi), "area": p / (4 * math.pi)}[t]
    out = ["  i type %s" % kind, "  f azimuth %g" % l["azimuth"], "  f elevation %g" % l["elevation"],
           "  c color %g %g %g" % tuple(srgb(l.get("color", (1, 1, 1)))), "  f intensity %g" % inten]
    if t != "sun":
        out += ["  f distance %g" % l.get("distance", 10), "  b softFalloff 0", "  f falloff 1"]
    if t in ("point", "spot"):
        out.append("  f shadowRadius %g" % l.get("radius", 0.1))
    if t == "spot":
        out += ["  f spotAngle %g" % l["spot_angle"], "  f spotPenumbra %g" % l.get("blend", 0.2)]
    if t == "area":
        w, h = l.get("size", (1, 1))
        out += ["  f areaWidth %g" % w, "  f areaHeight %g" % h, "  b normalize 1",
                "  f shadowRadius %g" % (max(w, h) / 2)]
    out.append("  i shadowRes 1")
    return out


def to_patch(scene, case):
    """The scene as an Infinite patch: Model 3D + Material per object (the glbs Blender wrote),
    Group 3D when there are more than four, Camera, Lights, Render 3D."""
    lines, n = ["infinite-patch 1"], [0]
    wires = []

    def node(cat, typ, params):
        n[0] += 1
        lines.append("node %d %s %s" % (n[0], cat, typ))
        lines.extend(params)
        lines.append("end")
        return n[0]

    geos = []
    for i, o in enumerate(scene["objects"]):
        m = o.get("material", {})
        model = node("3D", "Model 3D", ["  s path %s" % os.path.join(case, "obj_%d.glb" % i),
                                        "  b normalize 0", "  b recenter 0"])
        mat = node("3D", "Material", [
            "  c color %g %g %g" % tuple(srgb(m.get("base_color", (0.8, 0.8, 0.8)))),
            "  f metallic %g" % m.get("metallic", 0.0), "  f roughness %g" % m.get("roughness", 0.5),
            "  f ior %g" % m.get("ior", 1.5), "  f transmission %g" % m.get("transmission", 0.0),
            "  f clearcoat %g" % m.get("coat", 0.0), "  f clearcoatRoughness %g" % m.get("coat_roughness", 0.03),
            "  f sheen %g" % m.get("sheen", 0.0), "  f specular 0.5",
            "  c emissionColor %g %g %g" % tuple(srgb(m.get("emission", (0, 0, 0)))),
            "  f emission %g" % m.get("emission_strength", 0.0)])
        wires.append("geo %d 0 %d" % (mat, model))
        geos.append(mat)
    if len(geos) > 4:  # Render 3D takes four; group the rest eight at a time
        grouped = []
        for k in range(0, len(geos), 8):
            g = node("3D", "Group 3D", [])
            for j, src in enumerate(geos[k:k + 8]):
                wires.append("geo %d %d %d" % (g, j, src))
            grouped.append(g)
        geos = grouped
    if len(geos) > 4:
        sys.exit("too many objects (max 32)")

    cam = scene["camera"]
    tx, ty, tz = cam.get("target", (0, 0, 0))
    camera = node("3D", "Camera", ["  f fov %g" % cam["fov"], "  f distance %g" % cam["distance"],
                                   "  f azimuth %g" % cam["azimuth"], "  f elevation %g" % cam["elevation"],
                                   "  f targetX %g" % tx, "  f targetY %g" % tz, "  f targetZ %g" % -ty])
    lights = [node("3D", "Light", light_lines(l)) for l in scene.get("lights", [])]
    if len(lights) > 3:
        sys.exit("too many lights (max 3)")

    w = scene.get("world", {})
    col = [c * w.get("strength", 1.0) for c in w.get("color", (0, 0, 0))]
    peak = max(col + [1e-6])
    sky = srgb([c / peak for c in col]) if peak > 1 else srgb(col)
    width, height = scene.get("size", (512, 512))
    render = node("3D", "Render 3D", [
        "  f width %d" % width, "  f height %d" % height,
        "  i tonemap %s" % ("AgX" if scene.get("view") == "AgX" else "None"), "  f exposure 1",
        "  b shadows 1", "  i shadowQuality 1", "  f shadowStrength 1", "  i supersample 2", "  f ao 0",
        # Uniform world: sky, horizon and ground the same colour. Diffuse from the world is
        # env * toLinear(ambient) * 3, so ambient is the sRGB code of 1/3 to make it env alone.
        "  c envSky %g %g %g" % tuple(sky), "  c envHorizon %g %g %g" % tuple(sky),
        "  c envGround %g %g %g" % tuple(sky), "  f envIntensity %g" % max(peak, 1.0),
        "  c ambient %g %g %g" % tuple(srgb([1 / 3] * 3)), "  f rim 0", "  f lightIntensity 0",
        "  c bg %g %g %g" % tuple(sky)])
    out = node("Utility", "Output", [])
    for j, g in enumerate(geos):
        wires.append("geo %d %d %d" % (render, j, g))
    wires.append("geo %d camera %d" % (render, camera))
    for j, l in enumerate(lights):
        wires.append("geo %d light_%d %d" % (render, j + 1, l))
    wires.append("cable %d 0 %d" % (out, render))
    return "\n".join(lines + wires) + "\n"


def run(cmd, timeout=1800):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    return r.stdout, r.stdout + r.stderr


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0


def luma(p):
    lin = np.where(p <= 0.04045, p / 12.92, ((p + 0.055) / 1.055) ** 2.4)
    return lin @ np.array([0.2126, 0.7152, 0.0722], np.float32)


def score(case_dir):
    import flip_evaluator as flip
    truth, inf = load(os.path.join(case_dir, "truth.png")), load(os.path.join(case_dir, "inf.png"))
    if truth.shape != inf.shape:
        sys.exit("size mismatch in %s: %s vs %s" % (case_dir, truth.shape, inf.shape))
    err, mean, _ = flip.evaluate(truth, inf, "LDR", applyMagma=True)
    Image.fromarray((np.clip(err, 0, 1) * 255).astype(np.uint8)).save(os.path.join(case_dir, "flip.png"))
    lt, li = luma(truth), luma(inf)
    return {"flip": round(float(mean), 4),
            # Infinite's mean linear brightness over Blender's: a units or energy mismatch shows here first.
            "brightness_ratio": round(float(li.mean() / max(lt.mean(), 1e-6)), 3),
            "luma_err": round(float(np.abs(li - lt).mean()), 4)}


def board(out, rows):
    tiles = []
    for r in rows:
        d = os.path.join(out, r["case"])
        ims = [Image.open(os.path.join(d, f)).convert("RGB").resize((256, 256)) for f in ("truth.png", "inf.png", "flip.png")]
        t = Image.new("RGB", (256 * 3, 256 + 22), (20, 20, 24))
        for k, im in enumerate(ims):
            t.paste(im, (256 * k, 22))
        ImageDraw.Draw(t).text((6, 5), "%s   FLIP %.3f   bright x%.2f   %s   (Blender | Infinite | FLIP)" % (
            r["case"], r["flip"], r["brightness_ratio"], "ok" if r["pass"] else "WORSE"), fill=(230, 230, 230))
        tiles.append(t)
    sheet = Image.new("RGB", (256 * 3, sum(t.height for t in tiles)), (0, 0, 0))
    y = 0
    for t in tiles:
        sheet.paste(t, (0, y)); y += t.height
    sheet.save(os.path.join(out, "board.png"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--out", default="/tmp/blender-ab")
    ap.add_argument("--set-baseline", action="store_true")
    ap.add_argument("--ao", action="store_true", help="also run the AO bake A/B (ab.py --gen)")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.list:
        for k, v in CASES.items():
            print("%-12s %d objects, %d lights, %s" % (k, len(v["objects"]), len(v.get("lights", [])), v.get("view", "Standard")))
        return 0
    names = a.cases or list(CASES)
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    rows = []
    for name in names:
        scene = dict(CASES[name]); d = os.path.join(os.path.abspath(a.out), name)
        os.makedirs(d, exist_ok=True)
        text = json.dumps(scene, sort_keys=True, indent=1)
        h = hashlib.sha1(text.encode()).hexdigest()
        hp = os.path.join(d, "scene.hash")
        if not (os.path.exists(hp) and open(hp).read() == h and os.path.exists(os.path.join(d, "truth.png"))):
            open(os.path.join(d, "scene.json"), "w").write(text)
            _, log = run([BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, "blender_scene.py"), "--", d])
            if "SCENEDONE" not in log:
                sys.exit("blender failed on %s:\n%s" % (name, log[-3000:]))
            open(hp, "w").write(h)
        patch = os.path.join(d, "inf.inf")
        open(patch, "w").write(to_patch(scene, d))
        w, hgt = scene.get("size", (512, 512))
        stdout, log = run([INFINITE, "--frame", patch, "0", d + "/f/", "--size", "%dx%d" % (w, hgt)], 600)
        last = stdout.strip().splitlines()[-1] if stdout.strip() else ""
        status = json.loads(last) if last.startswith("{") else {}
        if not status.get("ok"):
            sys.exit("infinite failed on %s:\n%s" % (name, log[-3000:]))
        os.replace(os.path.join(d, "f", "frame_00000.png"), os.path.join(d, "inf.png"))
        r = {"case": name, **score(d)}
        r["baseline"] = base.get(name)
        r["pass"] = r["baseline"] is None or r["flip"] <= r["baseline"] + FLIP_SLACK
        rows.append(r)
        print("ABRESULT " + json.dumps(r), flush=True)
    with open(os.path.join(a.out, "results.jsonl"), "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    board(a.out, rows)
    if a.set_baseline:
        base.update({r["case"]: r["flip"] for r in rows})
        json.dump(base, open(BASELINE, "w"), indent=1, sort_keys=True)
    if a.ao:
        _, log = run([sys.executable, os.path.join(HERE, "ab.py"), "--gen", "--out", os.path.join(a.out, "ao")], 3600)
        print(log[-2000:])
    return 0 if all(r["pass"] for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
