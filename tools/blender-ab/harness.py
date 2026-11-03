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
from cases import CASES, REGIONS  # noqa: E402

BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
INFINITE = os.path.join(REPO, "build-dev/Infinite.app/Contents/MacOS/Infinite")
ASSETS = os.path.join(HERE, "assets")
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


def asset_file(o):
    """A model's file: the environment variable it names when that file exists (the Fox), else assets/."""
    env = os.environ.get(o.get("env", ""), "")
    return env if env and os.path.exists(env) else os.path.join(ASSETS, o["asset"])


def material_lines(m, sheen=True):
    trans = m.get("transmission", 0.0)
    trough = m.get("transmission_roughness", m.get("roughness", 0.0) if trans > 0 else 0.0)
    return [
        "  c color %g %g %g" % tuple(srgb(m.get("base_color", (0.8, 0.8, 0.8)))),
        "  f metallic %g" % m.get("metallic", 0.0), "  f roughness %g" % m.get("roughness", 0.5),
        "  f ior %g" % m.get("ior", 1.5), "  f transmission %g" % trans,
        "  f transmissionRoughness %g" % trough,
        "  f clearcoat %g" % m.get("coat", 0.0), "  f clearcoatRoughness %g" % m.get("coat_roughness", 0.03),
        ] + (["  f sheen %g" % m.get("sheen", 0.0), "  f sheenRoughness %g" % m.get("sheen_roughness", 0.5)] if sheen else []) + [
        "  f specular 0.5",
        "  c emissionColor %g %g %g" % tuple(srgb(m.get("emission", (0, 0, 0)))),
        "  f emission %g" % m.get("emission_strength", 0.0)]


def dof_aperture(cam, aspect):
    """Render 3D's dofAperture for a Blender f-stop: the thin-lens formula Scene3DNode uses for a real
    camera (blur circle over the sensor span, times 45), with Blender's 36 mm horizontal sensor."""
    span = 36.0
    mm = span / (2.0 * math.tan(math.radians(cam["fov"]) * 0.5))
    focus_mm = max(cam.get("focus", cam["distance"]) * 1000.0, mm * 1.01)
    return mm * mm / (cam["fstop"] * (focus_mm - mm)) / span * 45.0


def gltf_material(path):
    """What SpawnGltfParts (a glTF dropped on the canvas) builds for part 0: the file's factors and which
    maps exist, as (Material params, {Material slot: Image Source slot})."""
    import struct
    b = open(path, "rb").read()
    j = json.loads(b[20:20 + struct.unpack("<I", b[12:16])[0]])
    mat = j["materials"][0]
    pbr = mat.get("pbrMetallicRoughness", {})
    base = pbr.get("baseColorFactor", [1, 1, 1, 1])
    lines = ["  c color %g %g %g" % tuple(base[:3]), "  f opacity %g" % base[3],
             "  f metallic %g" % pbr.get("metallicFactor", 1.0), "  f roughness %g" % pbr.get("roughnessFactor", 1.0)]
    em = mat.get("emissiveFactor", [0, 0, 0])
    if any(em):
        lines += ["  c emissionColor %g %g %g" % tuple(em), "  f emission 1"]
    maps = {}
    if "baseColorTexture" in pbr: maps["albedo"] = "albedo"
    if "metallicRoughnessTexture" in pbr: maps["roughness"] = "roughness"; maps["metallic"] = "metallic"
    if "normalTexture" in mat: maps["normal"] = "normal"
    if "occlusionTexture" in mat: maps["ao"] = "ao"
    if "emissiveTexture" in mat: maps["emission"] = "emission"
    return lines, maps


def to_patch(scene, case, frame=None, engine="raster", pt_samples=256, pt_bounces=8):
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
        if o["shape"] == "model":
            # Kept with its glTF material; location is Blender's Z-up, Infinite's (x, z, -y).
            x, y, z = o.get("location", (0, 0, 0))
            lines_ = ["  s path %s" % asset_file(o), "  b normalize 0", "  b recenter 0",
                      "  f posX %g" % x, "  f posY %g" % z, "  f posZ %g" % -y, "  f scale %g" % o.get("scale", 1.0)]
            if o.get("clip"):
                lines_ += ["  i clip %d" % o["clip"], "  f clipSpeed 0", "  f clipPhase %g" % (frame or {}).get("phase", 0.0)]
            path = asset_file(o)
            mlines, maps = gltf_material(path)
            model = node("3D", "Model 3D", lines_)
            mat = node("3D", "Material", mlines)
            wires.append("geo %d 0 %d" % (mat, model))
            for slot in maps:
                tex = node("Source", "Image Source", ["  s path gltf://%s#%s" % (path, slot)])
                wires.append("cable %d %s %d" % (mat, slot, tex))
            geos.append(mat)
            continue
        model = node("3D", "Model 3D", ["  s path %s" % os.path.join(case, "obj_%d.glb" % i),
                                        "  b normalize 0", "  b recenter 0"])
        if o.get("instances"):
            # The shape at the origin, stamped on the vertices of the points mesh (one instance per vertex).
            pts = node("3D", "Model 3D", ["  s path %s" % os.path.join(case, "pts_%d.glb" % i),
                                          "  b normalize 0", "  b recenter 0"])
            inst = node("3D", "Instance on Points", ["  i pointMode 0", "  i maxPoints %d" % len(o["instances"]),
                                                      "  f instanceScale 1", "  b inheritMaterial 0"] + material_lines(m, sheen=False))
            wires.append("geo %d points %d" % (inst, pts))
            wires.append("geo %d shape %d" % (inst, model))
            geos.append(inst)
            continue
        mat = node("3D", "Material", material_lines(m))
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
        # Render 3D has three light pins (light_1..3); a fourth light would have to be the HDRI's sun.
        sys.exit("too many lights (max 3)")

    w = scene.get("world", {})
    col = [c * w.get("strength", 1.0) for c in w.get("color", (0, 0, 0))]
    peak = max(col + [1e-6])
    sky = srgb([c / peak for c in col]) if peak > 1 else srgb(col)
    width, height = scene.get("size", (512, 512))
    extra = []
    if cam.get("fstop"):
        extra = ["  f dofAperture %g" % dof_aperture(cam, width / height),
                 "  f focusDistance %g" % cam.get("focus", cam["distance"])]
    hdri = None
    if w.get("hdri"):
        # The HDRI lights, reflects and fills the background; its sun above the threshold becomes a light.
        hdri = node("3D", "HDRI", ["  s path %s" % os.path.join(ASSETS, w["hdri"]),
                                   "  f intensity %g" % w.get("strength", 1.0),
                                   "  f rotation %g" % w.get("rotation", 0.0),
                                   "  f sunThreshold %g" % w.get("sun_threshold", 0.0)])
    render = node("3D", "Render 3D", extra + [
        "  f width %d" % width, "  f height %d" % height,
        "  i tonemap %s" % ("AgX" if scene.get("view") == "AgX" else "None"), "  f exposure 1",
        "  b shadows 1", "  i shadowQuality 1", "  f shadowStrength 1", "  i supersample 2", "  f ao 0",
        # Uniform world: sky, horizon and ground the same colour. Diffuse from the world is
        # env * toLinear(ambient) * 3, so ambient is the sRGB code of 1/3 to make it env alone.
        "  c envSky %g %g %g" % tuple(sky), "  c envHorizon %g %g %g" % tuple(sky),
        "  c envGround %g %g %g" % tuple(sky), "  f envIntensity %g" % max(peak, 1.0),
        "  c ambient %g %g %g" % tuple(srgb([1 / 3] * 3)), "  f rim 0", "  f lightIntensity 0",
        "  c bg %g %g %g" % tuple(sky)] + (
        ["  i engine 1", "  i ptSamples %d" % pt_samples, "  i ptBounces %d" % pt_bounces] if engine == "pt" else []))
    out = node("Utility", "Output", [])
    for j, g in enumerate(geos):
        wires.append("geo %d %d %d" % (render, j, g))
    wires.append("geo %d camera %d" % (render, camera))
    for j, l in enumerate(lights):
        wires.append("geo %d light_%d %d" % (render, j + 1, l))
    if hdri:
        wires.append("cable %d env %d" % (render, hdri))
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


def furnace_metric(scene, lt, li):
    """Per-sphere mean linear luminance inside the sphere mask divided by the background."""
    h, w = lt.shape
    cam = scene["camera"]
    target = np.array(cam.get("target", (0, 0, 0)), np.float32)
    a, e = math.radians(cam["azimuth"]), math.radians(cam["elevation"])
    eye = target + np.array([math.cos(e) * math.cos(a), -math.cos(e) * math.sin(a), math.sin(e)], np.float32) * cam["distance"]
    fwd = target - eye; fwd /= np.linalg.norm(fwd)
    right = np.cross(fwd, np.array([0.0, 0.0, 1.0], np.float32)); right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    tan_half = math.tan(math.radians(cam["fov"]) * 0.5)
    gx, gy = np.meshgrid((np.arange(w, dtype=np.float32) + 0.5) / w * 2.0 - 1.0,
                         1.0 - (np.arange(h, dtype=np.float32) + 0.5) / h * 2.0)
    dirs = fwd + gx[..., None] * tan_half * right + gy[..., None] * tan_half * (h / w) * up
    dirs /= np.linalg.norm(dirs, axis=-1, keepdims=True)
    bg = np.ones((h, w), dtype=bool)
    masks = []
    for o in scene["objects"]:
        c = np.array(o.get("location", (0, 0, 0)), np.float32)
        r = float(o.get("scale", 1.0))
        oc = c - eye
        proj = dirs @ oc
        d2 = float(oc @ oc) - proj * proj
        masks.append((o["material"].get("roughness", 0.0), (proj > 0) & (d2 < (r * 0.9) ** 2)))
        bg &= d2 > (r * 1.15) ** 2
    bg_t, bg_i = max(float(lt[bg].mean()), 1e-6), max(float(li[bg].mean()), 1e-6)
    return {str(rough): {"cycles": round(float(lt[m].mean()) / bg_t, 4),
                         "infinite": round(float(li[m].mean()) / bg_i, 4)} for rough, m in masks}


def score(case_dir, name="", scene=None, sfx=""):
    import flip_evaluator as flip
    truth, inf = load(os.path.join(case_dir, "truth.png")), load(os.path.join(case_dir, "inf%s.png" % sfx))
    if truth.shape != inf.shape:
        sys.exit("size mismatch in %s: %s vs %s" % (case_dir, truth.shape, inf.shape))
    err, mean, _ = flip.evaluate(truth, inf, "LDR", applyMagma=True)
    Image.fromarray((np.clip(err, 0, 1) * 255).astype(np.uint8)).save(os.path.join(case_dir, "flip%s.png" % sfx))
    lt, li = luma(truth), luma(inf)
    out = {"flip": round(float(mean), 4),
           # Infinite's mean linear brightness over Blender's: a units or energy mismatch shows here first.
           "brightness_ratio": round(float(li.mean() / max(lt.mean(), 1e-6)), 3),
           "luma_err": round(float(np.abs(li - lt).mean()), 4)}
    if scene and name.startswith("furnace"):
        out["furnace"] = furnace_metric(scene, lt, li)
    return out


REGION_COLORS = [(0, 0, 0), (110, 110, 120), (230, 140, 40), (70, 200, 255), (255, 50, 70), (210, 70, 210),
                 (255, 210, 0), (70, 255, 110), (255, 255, 255)]


def region_masks(path):
    """Region name -> boolean mask from the Blender object-index PNG (value = index, 0 = sky). Each mask
    is eroded one pixel so an edge pixel, which both apps blend differently, counts for no region."""
    ids = np.asarray(Image.open(path).convert("L"))
    out = {}
    for k, name in enumerate(["sky"] + REGIONS):
        m = ids == k
        e = m.copy()
        e[1:] &= m[:-1]; e[:-1] &= m[1:]; e[:, 1:] &= m[:, :-1]; e[:, :-1] &= m[:, 1:]
        out[name] = e
    return ids, out


def score_hero(d, k, sfx=""):
    """Whole-frame and per-region FLIP and brightness ratio for frame k, same mask on both pictures."""
    import flip_evaluator as flip
    truth, inf = load(os.path.join(d, "truth_%d.png" % k)), load(os.path.join(d, "inf%s_%d.png" % (sfx, k)))
    if truth.shape != inf.shape:
        sys.exit("size mismatch in %s frame %d: %s vs %s" % (d, k, truth.shape, inf.shape))
    err, mean, _ = flip.evaluate(truth, inf, "LDR", applyMagma=True)
    Image.fromarray((np.clip(err, 0, 1) * 255).astype(np.uint8)).save(os.path.join(d, "flip%s_%d.png" % (sfx, k)))
    lt, li = luma(truth), luma(inf)
    ids, masks = region_masks(os.path.join(d, "mask_%d.png" % k))
    Image.fromarray(np.array(REGION_COLORS, np.uint8)[np.minimum(ids, len(REGION_COLORS) - 1)]).save(
        os.path.join(d, "mask_color_%d.png" % k))
    res = {"all": {"flip": round(float(mean), 4), "brightness_ratio": round(float(li.mean() / max(lt.mean(), 1e-6)), 3)}}
    for name, m in masks.items():
        if m.sum() < 30:   # too few pixels to say anything (the sky is behind the wall in this camera)
            continue
        res[name] = {"flip": round(float(err[m].mean()), 4),
                     "brightness_ratio": round(float(li[m].mean() / max(lt[m].mean(), 1e-6)), 3), "px": int(m.sum())}
    return res


def hero_board(out, d, frames, sfx=""):
    """One board per frame: Blender | Infinite | FLIP | region mask, the per-region table underneath.
    A path trace board puts the raster picture, when there is one, before the path traced one."""
    paths = []
    for k, res in enumerate(frames):
        names = ["truth_%d.png", "inf_%d.png", "inf%s_%%d.png" % sfx, "flip%s_%%d.png" % sfx, "mask_color_%d.png"] if sfx \
            else ["truth_%d.png", "inf_%d.png", "flip_%d.png", "mask_color_%d.png"]
        tiles = [Image.open(os.path.join(d, f % k)).convert("RGB").resize((384, 256))
                 for f in names if os.path.exists(os.path.join(d, f % k))]
        rows = ["%-10s %7s %8s" % ("region", "FLIP", "bright x")]
        rows += ["%-10s %7.3f %8.2f" % (n, v["flip"], v["brightness_ratio"]) for n, v in res.items()]
        sheet = Image.new("RGB", (384 * len(tiles), 22 + 256 + 14 * len(rows) + 8), (20, 20, 24))
        dr = ImageDraw.Draw(sheet)
        dr.text((6, 5), "hero frame %d%s   (Blender | %sFLIP | regions: %s)" % (k, sfx, "raster | path trace | " if sfx else "Infinite | ", ", ".join(
            "%s=%d" % (n, i + 1) for i, n in enumerate(REGIONS))), fill=(230, 230, 230))
        for j, t in enumerate(tiles):
            sheet.paste(t, (384 * j, 22))
        for j, r in enumerate(rows):
            dr.text((8, 22 + 256 + 6 + 14 * j), r, fill=(230, 230, 230))
        p = os.path.join(out, "hero_board%s_%d.png" % (sfx, k))
        sheet.save(p)
        paths.append(p)
    return paths


def hero_baseline(frames):
    names = [n for n in frames[0]]
    return {n: [f[n]["flip"] for f in frames if n in f] for n in names}


def hero_pass(frames, base):
    """The gate: every region (and the whole frame) holds its own baseline on every frame."""
    if not base:
        return True, []
    worse = []
    for name, vals in hero_baseline(frames).items():
        for k, v in enumerate(vals):
            ref = base.get(name, [None] * len(vals))[k] if k < len(base.get(name, [])) else None
            if ref is not None and v > ref + FLIP_SLACK:
                worse.append("%s f%d %.3f>%.3f" % (name, k, v, ref))
    return not worse, worse


def board(out, rows, sfx=""):
    # Path trace rows also show the raster picture when the case has one: Blender | raster | path trace | FLIP.
    tiles = []
    for r in rows:
        d = os.path.join(out, r["case"])
        tag = "_0" if "frames" in r else ""
        files = ["truth", "inf", "inf" + sfx, "flip" + sfx] if sfx else ["truth", "inf", "flip"]
        files = [f + tag + ".png" for f in files if os.path.exists(os.path.join(d, f + tag + ".png"))]
        ims = [Image.open(os.path.join(d, f)).convert("RGB").resize((256, 256)) for f in files]
        t = Image.new("RGB", (256 * len(ims), 256 + 22), (20, 20, 24))
        for k, im in enumerate(ims):
            t.paste(im, (256 * k, 22))
        raster = "   raster FLIP %.3f" % r["raster_flip"] if r.get("raster_flip") is not None else ""
        ImageDraw.Draw(t).text((6, 5), "%s   FLIP %.3f   bright x%.2f   %s%s" % (
            r["case"], r["flip"], r["brightness_ratio"], "ok" if r["pass"] else "WORSE", raster), fill=(230, 230, 230))
        tiles.append(t)
    sheet = Image.new("RGB", (max(t.width for t in tiles), sum(t.height for t in tiles)), (0, 0, 0))
    y = 0
    for t in tiles:
        sheet.paste(t, (0, y)); y += t.height
    sheet.save(os.path.join(out, "board%s.png" % sfx))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="*")
    ap.add_argument("--out", default="/tmp/blender-ab")
    ap.add_argument("--set-baseline", action="store_true")
    ap.add_argument("--ao", action="store_true", help="also run the AO bake A/B (ab.py --gen)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--engine", choices=("raster", "pt"), default="raster",
                    help="pt renders with Render 3D's path tracer; its files, board and baseline carry _pt")
    ap.add_argument("--pt-samples", type=int, default=256)
    ap.add_argument("--pt-bounces", type=int, default=8)
    a = ap.parse_args()
    if a.list:
        for k, v in CASES.items():
            print("%-12s %d objects, %d lights, %s" % (k, len(v["objects"]), len(v.get("lights", [])), v.get("view", "Standard")))
        return 0
    names = a.cases or list(CASES)
    sfx = "_pt" if a.engine == "pt" else ""
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    rows = []
    for name in names:
        scene = dict(CASES[name]); d = os.path.join(os.path.abspath(a.out), name)
        os.makedirs(d, exist_ok=True)
        if any(o.get("region") for o in scene["objects"]):
            scene["regions"] = REGIONS
        text = json.dumps(scene, sort_keys=True, indent=1)
        # The truth is reused while the scene, Blender's script and the asset files are unchanged.
        h = hashlib.sha1((text + open(os.path.join(HERE, "blender_scene.py")).read()).encode()).hexdigest()
        hp = os.path.join(d, "scene.hash")
        frames = scene.get("frames")
        tags = ["_%d" % k for k in range(len(frames))] if frames else [""]
        if not (os.path.exists(hp) and open(hp).read() == h and all(os.path.exists(os.path.join(d, "truth%s.png" % t)) for t in tags)):
            open(os.path.join(d, "scene.json"), "w").write(text)
            _, log = run([BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, "blender_scene.py"), "--", d])
            if "SCENEDONE" not in log:
                sys.exit("blender failed on %s:\n%s" % (name, log[-3000:]))
            open(hp, "w").write(h)
        w, hgt = scene.get("size", (512, 512))
        results = []
        for k, tag in enumerate(tags):
            patch = os.path.join(d, "inf%s%s.inf" % (sfx, tag))
            open(patch, "w").write(to_patch(scene, d, frames[k] if frames else None, a.engine, a.pt_samples, a.pt_bounces))
            stdout, log = run([INFINITE, "--frame", patch, "0", d + "/f/", "--size", "%dx%d" % (w, hgt)], 600)
            last = stdout.strip().splitlines()[-1] if stdout.strip() else ""
            status = json.loads(last) if last.startswith("{") else {}
            if not status.get("ok"):
                sys.exit("infinite failed on %s%s:\n%s" % (name, tag, log[-3000:]))
            os.replace(os.path.join(d, "f", "frame_00000.png"), os.path.join(d, "inf%s%s.png" % (sfx, tag)))
            if frames:
                results.append(score_hero(d, k, sfx))
        if frames:
            ok, worse = hero_pass(results, base.get(name + sfx))
            r = {"case": name, "flip": round(float(np.mean([f["all"]["flip"] for f in results])), 4),
                 "brightness_ratio": round(float(np.mean([f["all"]["brightness_ratio"] for f in results])), 3),
                 "frames": results, "baseline": base.get(name + sfx), "pass": ok, "worse": worse}
            r["boards"] = hero_board(os.path.abspath(a.out), d, results, sfx)
            r["hero_baseline"] = hero_baseline(results)
        else:
            r = {"case": name, **score(d, name, scene, sfx)}
            if sfx and os.path.exists(os.path.join(d, "inf.png")):
                r["raster_flip"] = score(d)["flip"]
            r["baseline"] = base.get(name + sfx)
            r["pass"] = r["baseline"] is None or r["flip"] <= r["baseline"] + FLIP_SLACK
        rows.append(r)
        print("ABRESULT " + json.dumps(r), flush=True)
    with open(os.path.join(a.out, "results%s.jsonl" % sfx), "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    board(a.out, rows, sfx)
    if a.set_baseline:
        base.update({r["case"] + sfx: r["hero_baseline"] if "frames" in r else r["flip"] for r in rows})
        json.dump(base, open(BASELINE, "w"), indent=1, sort_keys=True)
    if a.ao:
        _, log = run([sys.executable, os.path.join(HERE, "ab.py"), "--gen", "--out", os.path.join(a.out, "ao")], 3600)
        print(log[-2000:])
    return 0 if all(r["pass"] for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
