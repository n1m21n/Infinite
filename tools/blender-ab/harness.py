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
INFINITE = os.environ.get("INFINITE_BIN", os.path.join(REPO, "build-dev/Infinite.app/Contents/MacOS/Infinite"))
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


# Row 3.2: map names Infinite draws with a Sketch node; make_texture draws the same picture as a PNG for Blender.
SKETCH_MAPS = {"sketch_stripes": "function draw(t) {\n  background(25 / 255);\n  noStroke();\n  fill(230 / 255);\n"
                                 "  for (let k = 1; k < 8; k += 2) rect(0, k * 64, width, 64);\n}"}


def _truth_version():
    # blender_scene.py's TRUTH_VERSION line: a cached truth stays valid across edits that leave old cases alone.
    for line in open(os.path.join(HERE, "blender_scene.py")):
        if line.startswith("TRUTH_VERSION"):
            return "truth-v" + line.split("=", 1)[1].strip()
    return open(os.path.join(HERE, "blender_scene.py")).read()


BLENDER_TRUTH_VERSION = _truth_version()


def recipe_code(name):
    """A recipe's code from src/nodes/FieldMaterialRecipes.h: FLD_RECIPE("name", R"FLD(code)FLD")."""
    import re
    src = open(os.path.join(REPO, "src", "nodes", "FieldMaterialRecipes.h")).read()
    for n, code in re.findall(r'FLD_RECIPE\("([^"]+)", R"FLD\((.*?)\)FLD"\)', src, re.S):
        if n == name:
            return code
    raise KeyError("no recipe %r in FieldMaterialRecipes.h" % name)


def field_texture_code(sh):
    """Row 3.1: a shade_textures texture (checker, brick or gradient on Object coordinates through a Point mapping)
    as hand-written Material-domain Field code: the maths the Shade nodes compose, with their float32 literals.
    Locals avoid Field Pixel's built-in names (col, alpha, xy, uv, res, aspect)."""
    import numpy as np
    f32 = np.float32

    def F(v):   # ShadeNodes.cpp F(): %.8f, trailing zeros trimmed, near zero snapped
        v = float(f32(v))
        if abs(v) < 1e-6 or not np.isfinite(v):
            v = 0.0
        t = "%.8f" % v
        while len(t) > 2 and t[-1] == "0" and t[-2] != ".":
            t = t[:-1]
        return "0.0" if t == "-0.0" else t

    def lin(c):   # the node decodes the sRGB value the harness writes with %g
        out = []
        for v in srgb(c):
            v = f32(float("%g" % v))
            out.append(v / f32(12.92) if v <= f32(0.04045) else f32(np.power((v + f32(0.055)) / f32(1.055), f32(2.4))))
        return "vec3(%s, %s, %s)" % tuple(F(v) for v in out)

    k = f32(3.14159265358979) / f32(180.0)
    deg = [f32(v) for v in sh.get("rotate", (0, 0, 0))]
    cx, sx = np.cos(deg[0] * k), np.sin(deg[0] * k)
    cy, sy = np.cos(deg[1] * k), np.sin(deg[1] * k)
    cz, sz = np.cos(deg[2] * k), np.sin(deg[2] * k)
    r = [[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
         [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
         [-sy, cy * sx, cy * cx]]
    sc = sh.get("scale", 1.0)
    sc = sc if isinstance(sc, list) else [sc, sc, sc]
    t = sh.get("translate", (0, 0, 0))
    rows = ["dot(vec3(%s, %s, %s), o) + %s" % (F(r[i][0] * f32(sc[0])), F(r[i][1] * f32(sc[1])), F(r[i][2] * f32(sc[2])), F(t[i]))
            for i in range(3)]
    code = ["o = vec3(dot(vec3(1.0, 0.0, 0.0), P) + 0.0, dot(vec3(0.0, 0.0, -1.0), P) + 0.0, dot(vec3(0.0, 1.0, 0.0), P) + 0.0)",   # Y up -> Z up, as Mapping rotate X 90 writes it
            "q = vec3(%s)" % ", ".join(rows)]         # Point mapping: T + R (S o)
    tex = sh.get("tex", "checker")
    if tex == "checker":
        code.append("base = mix(%s, %s, checker(q * 1.0))" % (lin(sh.get("color2", (0.2, 0.2, 0.2))), lin(sh.get("color1", (0.8, 0.8, 0.8)))))
    elif tex == "brick":
        w, h = max(sh.get("width", 0.5), 1e-3), max(sh.get("height", 0.25), 1e-3)
        off, mo = sh.get("offset", 0.5), sh.get("mortar", 0.02)
        code += ["p = q * 1.0",
                 "brow = floor(p.y * %s)" % F(f32(1.0) / f32(h)),
                 "bx = p.x + %s * mod(brow + 1.0, 2.0)" % F(f32(w) * f32(off)),   # the row at 0 is a shifted one
                 "bcol = floor(bx * %s)" % F(f32(1.0) / f32(w)),
                 "bfx = bx - bcol * %s" % F(w),
                 "bfy = p.y - brow * %s" % F(h),
                 "bedge = min(min(bfx, %s - bfx), min(bfy, %s - bfy))" % (F(w), F(h)),
                 "base = mix(%s, %s, step(%s, bedge))" % (lin(sh.get("mortarColor", (0, 0, 0))), lin(sh.get("color1", (0.8, 0.8, 0.8))), F(max(mo, 0.0)))]
    else:
        kind = sh.get("kind", "linear")
        code.append("p = q * 1.0")
        if kind == "radial":
            code.append("g = atan2(p.y, p.x) * 0.15915494 + 0.5")
        elif kind == "quadratic_sphere":
            code += ["r = max(1.0 - length(p), 0.0)", "g = r * r"]
        else:
            code.append("g = clamp(p.x, 0.0, 1.0)")
        code.append("base = vec3(g, g, g)")
    return "\n".join(code)


def material_lines(m, sheen=True):
    trans = m.get("transmission", 0.0)
    trough = m.get("transmission_roughness", m.get("roughness", 0.0) if trans > 0 else 0.0)
    return [
        "  c color %g %g %g" % tuple(srgb(m.get("base_color", (0.8, 0.8, 0.8)))),
        "  f metallic %g" % m.get("metallic", 0.0), "  f roughness %g" % m.get("roughness", 0.5),
        "  f ior %g" % m.get("ior", 1.5), "  f transmission %g" % trans,
        "  f transmissionRoughness %g" % trough,
        "  f clearcoat %g" % m.get("coat", 0.0), "  f clearcoatRoughness %g" % m.get("coat_roughness", 0.03),
        ] + (["  f sheen %g" % m.get("sheen", 0.0), "  f sheenRoughness %g" % m.get("sheen_roughness", 0.5),
                         "  f sheenDirection %g" % m.get("sheen_direction", 0.0)] if sheen else []) + [
        "  f specular 0.5",
        "  c emissionColor %g %g %g" % tuple(srgb(m.get("emission", (0, 0, 0)))),
        "  f emission %g" % m.get("emission_strength", 0.0)] + [
        # Coverage-case lobes, written only when set so older cases keep their exact patch.
        line for key, line in (
            ("alpha", "  f opacity %g" % m.get("alpha", 1.0)),
            ("alpha_cutoff", "  f alphaCutoff %g" % m.get("alpha_cutoff", 0.0)),
            ("subsurface", "  f subsurface %g" % m.get("subsurface", 0.0)),
            ("subsurface_color", "  c subsurfaceColor %g %g %g" % tuple(m.get("subsurface_color", (1.0, 0.2, 0.1)))),
            ("subsurface_radius", "  f subsurfaceRadius %g" % m.get("subsurface_radius", 0.5)),
            ("translucency", "  f translucency %g" % m.get("translucency", 0.0)),
            ("anisotropy", "  f anisotropy %g" % m.get("anisotropy", 0.0)),
            ("anisotropy_rotation", "  f anisotropyRotation %g" % m.get("anisotropy_rotation", 0.0)),
            ("iridescence", "  f iridescence %g" % m.get("iridescence", 0.0)),
            ("iridescence_thickness", "  f iridescenceThickness %g" % m.get("iridescence_thickness", 400.0)),
            ("iridescence_ior", "  f iridescenceIor %g" % m.get("iridescence_ior", 1.33)),
            ("bump", "  b normalBump 1"), ("bump", "  f normalStrength %g" % m.get("bump", 1.0))) if key in m]


def make_texture(case_dir, name, size=512):
    """The coverage cases' image maps, drawn the same every run: <case>/<name>.png, read by both apps."""
    path = os.path.join(case_dir, name + ".png")
    if os.path.exists(path):
        return path
    v, u = np.mgrid[0:size, 0:size].astype(np.float32) / size
    if name.startswith("checker"):
        nx, ny = (16, 8) if name == "checker_fine" else (8, 8)
        on = ((np.floor(u * nx) + np.floor(v * ny)) % 2) > 0
        img = np.where(on[..., None], [230, 230, 230], [200, 40, 30]).astype(np.uint8)
    elif name in ("stripes", "sketch_stripes"):   # row 3.2: 8 bands of 64 px down the image, 25 and 230 (a Sketch draws the same)
        g = np.where((np.floor(v * 8) % 2) > 0, 230, 25).astype(np.uint8)
        img = np.stack([g, g, g], -1)
    elif name == "rough_bands":       # grey roughness, 6 bands along v: 0.1 and 0.9
        g = np.where((np.floor(v * 6) % 2) > 0, 230, 25).astype(np.uint8)
        img = np.stack([g, g, g], -1)
    elif name == "bumps":             # OpenGL tangent-space normals of a height field of round bumps
        h = (np.sin(u * 2 * np.pi * 12) * np.sin(v * 2 * np.pi * 6)) * 0.5
        dx, dy = np.gradient(h, 1.0 / size)
        n = np.stack([-dx * 0.02, dy * 0.02, np.ones_like(h)], -1)   # image rows run down, +v up: flip y
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        img = ((n * 0.5 + 0.5) * 255).astype(np.uint8)
    elif name == "blotches":          # a seamless tile of soft colour blotches: integer frequencies, fixed phases
        rng = np.random.default_rng(3)
        f = np.zeros((size, size, 3), np.float32)
        for _ in range(14):
            kx, ky = rng.integers(1, 5, 2); ph = rng.uniform(0, 2 * np.pi, 3)
            f += np.sin(2 * np.pi * (kx * u + ky * v)[..., None] + ph) * rng.uniform(0.3, 1.0)
        f = (f - f.min()) / (f.max() - f.min())
        img = np.array([40, 90, 60]) + f * np.array([200, 150, 120])
        for _ in range(6):                # a few dark spots, so a repeating tile is easy to see
            cx, cy, r = rng.uniform(0, 1), rng.uniform(0, 1), rng.uniform(0.04, 0.09)
            dx = (u - cx + 0.5) % 1 - 0.5; dy = (v - cy + 0.5) % 1 - 0.5
            img = np.where((np.hypot(dx, dy) < r)[..., None], [30, 20, 25], img)
        img = img.clip(0, 255).astype(np.uint8)
    elif name == "wood":              # row 10.2: planks along u (8 a tile), each its own tone, a fine wavy grain
        rng = np.random.default_rng(7)
        k = np.floor(v * 8).astype(int) % 8
        tone = rng.uniform(0.75, 1.15, 8)[k]
        grain = 0.5 + 0.5 * np.sin(2 * np.pi * (v * 96 + 0.6 * np.sin(2 * np.pi * (u * 3 + k * 0.37))))
        seam = np.where(((v * 8) % 1) < 0.03, 0.45, 1.0)
        img = (np.array([150, 95, 55]) * (tone * (0.82 + 0.18 * grain) * seam)[..., None]).clip(0, 255).astype(np.uint8)
    elif name == "label":             # row 10.3: a paper label, a cream ground with two dark bands and a red one
        g = np.full((size, size, 3), [235, 225, 200], np.float32)
        g[(v > 0.30) & (v < 0.36)] = [40, 35, 30]; g[(v > 0.64) & (v < 0.70)] = [40, 35, 30]
        g[(v > 0.44) & (v < 0.56)] = [170, 30, 25]
        img = g.astype(np.uint8)
    elif name == "holes":             # row 4.2: a 6 x 6 grid of round holes in a flat top, grey height (1 top, 0 floor)
        r = np.hypot((u * 6) % 1 - 0.5, (v * 6) % 1 - 0.5)
        g = (np.clip((r - 0.27) / 0.03, 0, 1) * 255).astype(np.uint8)
        img = np.stack([g, g, g], -1)
    elif name == "weave":             # a fine plain weave: crossed threads, each a half-round ridge (OpenGL normals)
        tu, tv = (u * 16) % 1, (v * 16) % 1
        over = ((np.floor(u * 16) + np.floor(v * 16)) % 2) > 0
        h = np.where(over, np.sin(np.pi * tu), np.sin(np.pi * tv)) * 0.5
        dx, dy = np.gradient(h, 1.0 / size)
        n = np.stack([-dx * 0.04, dy * 0.04, np.ones_like(h)], -1)
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        img = ((n * 0.5 + 0.5) * 255).astype(np.uint8)
    elif name.startswith("mask_"):    # row 4.1: soft grey masks; mask_noise is smooth blobs, mask_half splits u
        if name == "mask_half":
            g = np.where(u > 0.5, 255, 0)
        else:
            rng = np.random.default_rng({"mask_noise": 5, "mask_dirt": 7, "mask_dust": 11}.get(name, 5))
            f = np.zeros((size, size), np.float32)
            for _ in range(10):
                kx, ky = rng.integers(1, 6, 2); ph = rng.uniform(0, 2 * np.pi)
                f += np.sin(2 * np.pi * (kx * u + ky * v) + ph) * rng.uniform(0.3, 1.0)
            f = (f - f.min()) / (f.max() - f.min())
            g = np.clip((f - 0.4) * 4.0, 0, 1) * 255
        g = g.astype(np.uint8)
        img = np.stack([g, g, g], -1)
    elif name == "holes":             # opacity: a solid sheet with a grid of round holes
        d = np.hypot((u * 4) % 1 - 0.5, (v * 4) % 1 - 0.5)
        g = np.where(d < 0.3, 0, 255).astype(np.uint8)
        img = np.stack([g, g, g], -1)
    else:
        sys.exit("unknown texture " + name)
    Image.fromarray(img).save(path)
    return path


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
    sg = mat.get("extensions", {}).get("KHR_materials_pbrSpecularGlossiness")
    if sg and not pbr:   # read as GltfImport does: diffuse = base colour, roughness = 1 - glossiness, non-metal
        pbr = {"baseColorFactor": sg.get("diffuseFactor", [1, 1, 1, 1]), "metallicFactor": 0.0,
               "roughnessFactor": 1.0 if "specularGlossinessTexture" in sg else 1.0 - sg.get("glossinessFactor", 1.0)}
        if "diffuseTexture" in sg: pbr["baseColorTexture"] = sg["diffuseTexture"]
    base = pbr.get("baseColorFactor", [1, 1, 1, 1])
    lines = ["  c color %g %g %g" % tuple(srgb(base[:3])), "  f opacity %g" % base[3],   # linear factor -> sRGB control
             "  f metallic %g" % pbr.get("metallicFactor", 1.0), "  f roughness %g" % pbr.get("roughnessFactor", 1.0)]
    em = mat.get("emissiveFactor", [0, 0, 0])
    if any(em):
        lines += ["  c emissionColor %g %g %g" % tuple(srgb(em)), "  f emission 1"]
    maps = {}
    if "baseColorTexture" in pbr: maps["albedo"] = "albedo"
    if "metallicRoughnessTexture" in pbr: maps["roughness"] = "roughness"; maps["metallic"] = "metallic"
    if sg:   # specular colour -> Material specular (F0 = 0.08 x specular), from the map's mean as GltfImport does
        f0 = sum(sg.get("specularFactor", [1, 1, 1])) / 3
        if "specularGlossinessTexture" in sg:
            import io
            from PIL import Image, ImageStat
            im = j["images"][j["textures"][sg["specularGlossinessTexture"]["index"]]["source"]]
            bv = j["bufferViews"][im["bufferView"]]; o = 20 + struct.unpack("<I", b[12:16])[0] + 8 + bv.get("byteOffset", 0)
            px = Image.open(io.BytesIO(b[o:o + bv["byteLength"]])).convert("RGB").getdata()
            lin = lambda c: c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
            f0 *= sum(lin(sum(p) / 765.0) for p in px) / len(px)
            maps["roughness"] = "roughness"
        lines.append("  f specular %g" % min(1.0, f0 / 0.08))
    sh = mat.get("extensions", {}).get("KHR_materials_sheen")
    if sh:   # sheen colour carries the weight in glTF (Blender's importer reads it the same way)
        sc = sh.get("sheenColorFactor", [0, 0, 0])
        lines += ["  f sheen %g" % max(sc), "  c sheenColor %g %g %g" % tuple(c / max(max(sc), 1e-6) for c in sc),
                  "  f sheenRoughness %g" % sh.get("sheenRoughnessFactor", 0.0)]
    if "normalTexture" in mat:
        maps["normal"] = "normal"
        lines.append("  f normalStrength %g" % mat["normalTexture"].get("scale", 1.0))
    if "occlusionTexture" in mat: maps["ao"] = "ao"
    if "emissiveTexture" in mat: maps["emission"] = "emission"
    if maps: lines.append("  i wrapMode 1")   # glTF's default sampler wrap is REPEAT (SpawnGltfParts sets the same)
    return lines, maps


def to_patch(scene, case, frame=None, engine="raster", pt_samples=256, pt_bounces=8):
    """The scene as an Infinite patch: Model 3D + Material per object (the glbs Blender wrote),
    Group 3D when there are more than four, Camera, Lights, Render 3D."""
    lines, n = ["infinite-patch 1"], [0]
    wires, exprs = [], []
    motion = scene.get("motion", {})

    def node(cat, typ, params):
        n[0] += 1
        lines.append("node %d %s %s" % (n[0], cat, typ))
        lines.extend(params)
        lines.append("end")
        return n[0]

    def fur_node(f, src):
        """Geometry Op Fur after src (blender_scene.add_fur rebuilds it), with an arc Curve as its guide when asked."""
        fur = node("3D", "Fur", ["  f furDensity %g" % f["density"], "  i furMaxStrands %d" % f.get("max", 400000),
                                 "  f furLength %g" % f["length"], "  f furJitter %g" % f.get("jitter", 0.3),
                                 "  f furWidth %g" % f["width"], "  i furSegments %d" % f.get("segments", 3),
                                 "  f furGravity %g" % f.get("gravity", 0.0), "  f furClump 0", "  f furCurl 0",
                                 "  f seed %g" % f.get("seed", 0)] + (["  i furNormals 1"] if f.get("hair") else []))
        wires.append("geo %d 0 %d" % (fur, src))
        g = f.get("guide")
        if g:   # the arc preset through linear interpolation: exactly the polyline add_fur builds
            cur = node("3D", "Curve", ["  i kind 3", "  i preset 0", "  i pointCount %d" % g["points"],
                                       "  f spread %g" % g["spread"], "  f height %g" % g["height"]])
            wires.append("geo %d 1 %d" % (fur, cur))
        return fur

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
            if o.get("detail"):   # Material detail normal on top of the model's own normal map
                mlines += ["  f detailScale %g" % o["detail"][0], "  f detailStrength %g" % o["detail"][1]]
            model = node("3D", "Model 3D", lines_)
            if o.get("fur"):
                model = fur_node(o["fur"], model)
            mat = node("3D", "Material", mlines)
            wires.append("geo %d 0 %d" % (mat, model))
            for slot in maps:
                tex = node("Source", "Image Source", ["  s path gltf://%s#%s" % (path, slot)])
                wires.append("cable %d %s %d" % (mat, slot, tex))
            geos.append(mat)
            continue
        if o.get("sketch"):   # row 3.3: the mesh from a Sketch 3D script (Infinite's Y-up world), not the Blender glb
            model = node("3D", "Sketch 3D", ["  s code %s" % o["sketch"].replace("\n", "\\n")])
        else:
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
        if str(i) in motion.get("velocity", {}):
            vx, vy, vz = motion["velocity"][str(i)]   # Blender's Z-up m/s -> Infinite (x, z, -y), through a Transform
            xf = node("3D", "Transform", [])
            wires.append("geo %d 0 %d" % (xf, model))
            exprs += ["expr %d offsetX %g*t" % (xf, vx), "expr %d offsetY %g*t" % (xf, vz), "expr %d offsetZ %g*t" % (xf, -vy)]
            model = xf
        if o.get("fur"):
            model = fur_node(o["fur"], model)
        if any(k in m for k in ("uv_scale", "breakup", "mapping")):
            # Texture coordinates through a Mapping node: UV or generated, a scale, triplanar blend, breakup.
            sc = m.get("uv_scale", 1.0)
            mp = node("3D", "Mapping", ["  i space %d" % (1 if m.get("mapping") == "generated" else 0),
                                        "  f scaleX %g" % sc, "  f scaleY %g" % sc, "  f scaleZ %g" % sc,
                                        "  f triplanarBlend %g" % m.get("triplanar", 0.0), "  f breakup %g" % m.get("breakup", 0.0)])
            wires.append("geo %d 0 %d" % (mp, model))
            model = mp
        dl = ["  f detailScale %g" % m["detail"][0], "  f detailStrength %g" % m["detail"][1]] if "detail" in m else []
        h = (o.get("fur") or {}).get("hair")
        hl = ["  i model 1", "  i hairColour 0", "  f melanin %g" % h["melanin"], "  f melaninRedness %g" % h.get("redness", 1.0),
              "  f radialRoughness %g" % h.get("radial", 0.3), "  f hairShift %g" % h.get("shift", 2.0)] if h else []
        if h:   # the Material's roughness and IOR are the fibre's (the skin shares them)
            m = dict(m, roughness=h["roughness"], ior=h.get("ior", 1.55))
        knobs = dict(m, base_color=[0.5, 0.5, 0.5], metallic=0.0, roughness=0.5) if m.get("field") else m
        mat = node("3D", "Material", material_lines(knobs) + dl + hl + (["  i wrapMode repeat"] if m.get("maps") else []))
        wires.append("geo %d 0 %d" % (mat, model))
        if m.get("wear"):   # row 4.4: curvature or cavity as emission on a black surface (truth: pointiness, AO)
            w = m["wear"]
            code = ("base = vec3(0.0)\\nroughness = 1.0\\nemission = vec3(clamp(0.5 + curvature * %r, 0.0, 1.0))" % float(m.get("gain", 0.1))
                    if w == "curvature" else "base = vec3(0.0)\\nroughness = 1.0\\nemission = vec3(1.0 - cavity)")
            fp = node("Source", "FieldPixel", ["  i domain 1", "  s code %s" % code])
            wires.append("cable %d field %d" % (mat, fp))
        if m.get("split"):   # row 3.3: two tones by the mesh's mask() attribute (attr)
            c0, c1, _ = m["split"]
            code = "base = mix(vec3(%r, %r, %r), vec3(%r, %r, %r), step(0.5, attr))\\nroughness = 0.5" % (*map(float, c0), *map(float, c1))
            fp = node("Source", "FieldPixel", ["  i domain 1", "  s code %s" % code])
            wires.append("cable %d field %d" % (mat, fp))
        if m.get("recipe"):   # rows 3.5 / 4.5: a Material recipe from FieldMaterialRecipes.h, its default params
            fp = node("Source", "FieldPixel", ["  i domain 1", "  s code %s" % recipe_code(m["recipe"]).replace("\n", "\\n")])
            wires.append("cable %d field %d" % (mat, fp))
        if m.get("field"):   # a Field material program writes the case's values over the decoy knobs (linear, as the case)
            code = "base = vec3(%r, %r, %r)\\nmetallic = %r\\nroughness = %r" % (
                *[float(c) for c in m.get("base_color", (0.8, 0.8, 0.8))], float(m.get("metallic", 0.0)), float(m.get("roughness", 0.5)))
            fp = node("Source", "FieldPixel", ["  i domain 1", "  s code %s" % code])
            wires.append("cable %d field %d" % (mat, fp))
        sh = m.get("shade")
        if sh and sh.get("tex") == "image":   # row 2.4: Shade Coord (UV) -> Shade Image <- Image Source
            src = node("Source", "Image Source", ["  s path %s" % make_texture(case, sh.get("image", "blotches"))])
            co = node("3D", "Shade Coord", ["  i output 0"])
            ck = node("3D", "Shade Image", ["  i projection 0", "  i space 0"])
            wires += ["cable %d vector %d" % (ck, co), "cable %d image %d" % (ck, src), "cable %d field %d" % (mat, ck)]
        elif sh and sh.get("code"):   # row 3.1: the same texture written by hand as a Material-domain Field Pixel
            fp = node("Source", "FieldPixel", ["  i domain 1", "  s code %s" % field_texture_code(sh).replace("\n", "\\n")])
            wires.append("cable %d field %d" % (mat, fp))
        elif sh:   # Shade Coord (Object) -> Mapping rotate X +90 (Infinite's Y-up to the case's Z-up) -> Mapping -> Checker
            sc = sh.get("scale", 1.0)
            sc = sc if isinstance(sc, list) else [sc, sc, sc]
            co = node("3D", "Shade Coord", ["  i output 1"])
            up = node("3D", "Shade Mapping", ["  i kind 0", "  f rotateX 90"])
            mp = node("3D", "Shade Mapping", ["  i kind 0"] + ["  f %s%s %g" % (k, a, v) for k, vals in
                      (("translate", sh.get("translate", (0, 0, 0))), ("rotate", sh.get("rotate", (0, 0, 0))), ("scale", sc))
                      for a, v in zip("XYZ", vals)])
            tex = sh.get("tex", "checker")
            if tex == "bump":   # row 2.6: sin(x) sin(y) -> Shade Bump -> Material normal pin
                sx = node("3D", "Shade Separate", ["  i mode 0", "  i channel 0"])
                sy = node("3D", "Shade Separate", ["  i mode 0", "  i channel 1"])
                mx = node("3D", "Shade Math", ["  i op 29"])
                my = node("3D", "Shade Math", ["  i op 29"])
                mm = node("3D", "Shade Math", ["  i op 2"])
                bp = node("3D", "Shade Bump", ["  f strength %g" % sh.get("strength", 1.0), "  f distance %g" % sh.get("distance", 0.1)])
                wires += ["cable %d vector %d" % (up, co), "cable %d vector %d" % (mp, up),
                          "cable %d vector %d" % (sx, mp), "cable %d vector %d" % (sy, mp),
                          "cable %d a %d" % (mx, sx), "cable %d a %d" % (my, sy),
                          "cable %d a %d" % (mm, mx), "cable %d b %d" % (mm, my),
                          "cable %d height %d" % (bp, mm), "cable %d normal %d" % (mat, bp)]
                geos.append(mat)
                continue
            col = lambda k, d: "  c %s %g %g %g" % ((k,) + tuple(srgb(sh.get(k, d))))
            if tex == "brick":      # Cycles' brick defaults; one colour for both bricks (its per-brick pick is random)
                ck = node("3D", "Shade Brick", [col("color1", (0.8, 0.8, 0.8)), col("color2", (0.8, 0.8, 0.8)),
                                                col("mortarColor", (0, 0, 0)), "  f scale 1",
                                                "  f width %g" % sh.get("width", 0.5), "  f height %g" % sh.get("height", 0.25),
                                                "  f offset %g" % sh.get("offset", 0.5), "  f mortar %g" % sh.get("mortar", 0.02),
                                                "  f tint 0"])
            elif tex == "gradient":
                kinds = ["linear", "quadratic", "easing", "diagonal", "spherical", "quadratic_sphere", "radial"]
                ck = node("3D", "Shade Gradient", ["  i kind %d" % kinds.index(sh.get("kind", "linear")), "  f scale 1"])
            else:
                ck = node("3D", "Shade Checker", [col("color1", (0.8, 0.8, 0.8)), col("color2", (0.2, 0.2, 0.2)), "  f scale 1"])
            wires += ["cable %d vector %d" % (up, co), "cable %d vector %d" % (mp, up), "cable %d vector %d" % (ck, mp),
                      "cable %d field %d" % (mat, ck)]
        for slot, tex_name in m.get("maps", {}).items():
            make_texture(case, tex_name)   # Blender reads the PNG either way
            if tex_name in SKETCH_MAPS:    # row 3.2: Infinite draws the map with a JS Sketch instead of loading it
                tex = node("Source", "Sketch", ["  s code %s" % SKETCH_MAPS[tex_name].replace("\n", "\\n"),
                                                "  f width 512", "  f height 512", "  b animate 1"])
            else:
                tex = node("Source", "Image Source", ["  s path %s" % make_texture(case, tex_name)])
            wires.append("cable %d %s %d" % (mat, slot, tex))
        top, ly = mat, m.get("layer")
        while ly:   # row 4.1: a second Material on the layer pin, laid over where its mask image's red is 1
            lm = node("3D", "Material", material_lines(ly))
            wires.append("geo %d 12 %d" % (top, lm))
            if ly.get("mask"):
                mk = node("Source", "Image Source", ["  s path %s" % make_texture(case, ly["mask"])])
                wires.append("cable %d 13 %d" % (top, mk))
            top, ly = lm, ly.get("layer")
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
    if motion.get("camera_azimuth_rate"):
        exprs.append("expr %d azimuth %g+%g*t" % (camera, cam["azimuth"], motion["camera_azimuth_rate"]))
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
    if motion.get("blur"):
        extra += ["  b motionBlur 1", "  f motionShutter %g" % motion["blur"], "  i motionPosition 1"]
    if cam.get("fstop"):
        extra += ["  f dofAperture %g" % dof_aperture(cam, width / height),
                 "  f focusDistance %g" % cam.get("focus", cam["distance"])]
        if cam.get("blades"):
            extra += ["  i dofBlades %d" % cam["blades"], "  f dofRotation %g" % cam.get("blade_rotation", 0.0)]
    hdri = None
    if w.get("sky"):
        k = w["sky"]  # row 5.5: the HDRI node's physical sky, azimuth in Infinite's Y-up frame (Blender +Y = -Z)
        hdri = node("3D", "HDRI", ["  i source 1", "  f skyElevation %g" % k["elevation"], "  f skyAzimuth %g" % k["azimuth"],
                                   "  f turbidity %g" % k.get("turbidity", 3.0), "  f intensity %g" % w.get("strength", 1.0)])
    if w.get("hdri"):
        # The HDRI lights, reflects and fills the background; its sun above the threshold becomes a light.
        hdri = node("3D", "HDRI", ["  s path %s" % os.path.join(ASSETS, w["hdri"]),
                                   "  f intensity %g" % w.get("strength", 1.0),
                                   "  f rotation %g" % w.get("rotation", 0.0),
                                   "  f sunThreshold %g" % w.get("sun_threshold", 0.0)])
    fog = scene.get("fog")
    if fog:
        # Row 5.3: Blender's box (Z up) in Infinite's Y-up world: (x, y, z) -> (x, z, -y).
        (x0, y0, z0), (x1, y1, z1) = fog["box"]
        extra += ["  f fogDensity %g" % fog["density"], "  c fogColor %g %g %g" % tuple(srgb(fog["color"])),
                  "  f fogAnisotropy %g" % fog.get("g", 0.0),
                  "  f fogMinX %g" % x0, "  f fogMinY %g" % z0, "  f fogMinZ %g" % -y1,
                  "  f fogMaxX %g" % x1, "  f fogMaxY %g" % z1, "  f fogMaxZ %g" % -y0]
    render = node("3D", "Render 3D", extra + [
        "  f width %d" % width, "  f height %d" % height,
        "  i tonemap %s" % {"AgX": "AgX", "Khronos PBR Neutral": "Neutral"}.get(scene.get("view"), "None"), "  f exposure 1",
        "  b shadows 1", "  i shadowQuality 1", "  f shadowStrength 1", "  i supersample %d" % scene.get("supersample", 2), "  f ao 0",
        # Uniform world: sky, horizon and ground the same colour. Diffuse from the world is
        # env * toLinear(ambient) * 3, so ambient is the sRGB code of 1/3 to make it env alone.
        "  c envSky %g %g %g" % tuple(sky), "  c envHorizon %g %g %g" % tuple(sky),
        "  c envGround %g %g %g" % tuple(sky), "  f envIntensity %g" % max(peak, 1.0),
        "  c ambient %g %g %g" % tuple(srgb([1 / 3] * 3)), "  f rim 0", "  f lightIntensity 0",
        "  c bg %g %g %g" % tuple(sky)] + (
        ["  i engine 1", "  i ptSamples %d" % pt_samples, "  i ptBounces %d" % pt_bounces] if engine == "pt" else
        ["  i ptSamples %d" % pt_samples, "  i ptBounces %d" % pt_bounces] if engine == "settle" else []))
    out = node("Utility", "Output", [])
    for j, g in enumerate(geos):
        wires.append("geo %d %d %d" % (render, j, g))
    wires.append("geo %d camera %d" % (render, camera))
    for j, l in enumerate(lights):
        wires.append("geo %d light_%d %d" % (render, j + 1, l))
    if hdri:
        wires.append("cable %d env %d" % (render, hdri))
    wires.append("cable %d 0 %d" % (out, render))
    return "\n".join(lines + wires + exprs) + "\n"


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


def noise_ratio(case_dir, sfx=""):
    """High-frequency energy of Infinite's picture over Cycles' (denoised): 1 = as clean, above 1 = grain or fireflies."""
    from PIL import ImageFilter
    def hf(p):
        im = Image.open(p).convert("L")
        a = np.asarray(im, np.float32); b = np.asarray(im.filter(ImageFilter.GaussianBlur(1.5)), np.float32)
        return float(np.abs(a - b).mean())
    return round(hf(os.path.join(case_dir, "inf%s.png" % sfx)) / max(hf(os.path.join(case_dir, "truth.png")), 1e-3), 3)


def run_motion(scene, name, d, sfx, engine, a):
    """A motion case: Infinite renders the shot as a frame sequence; each check time is scored against Cycles'
    frame there, and a still shot also gets flicker = mean change between its last 10 frames (0-255 steps)."""
    import flip_evaluator as flip
    mo = scene["motion"]; fps = mo.get("fps", 30)
    w, hgt = scene.get("size", (512, 512))
    patch = os.path.join(d, "inf%s.inf" % sfx)
    open(patch, "w").write(to_patch(scene, d, None, engine, a.pt_samples, a.pt_bounces))
    fd = os.path.join(d, "seq%s" % sfx)
    if os.path.isdir(fd):
        for f in os.listdir(fd): os.remove(os.path.join(fd, f))
    last = int(round(max(mo["checks"]) * fps))
    stdout, log = run([INFINITE, "--frames-dir", patch, fd, "--duration", "%g" % ((last + 1) / fps), "--fps", str(fps),
                       "--size", "%dx%d" % (w, hgt)], 1200)
    tail = stdout.strip().splitlines()[-1] if stdout.strip() else ""
    status = json.loads(tail) if tail.startswith("{") else {}
    if not status.get("ok"):
        sys.exit("infinite failed on %s:\n%s" % (name, log[-3000:]))
    seq = sorted(f for f in os.listdir(fd) if f.endswith(".png"))
    checks = []
    for k, tc in enumerate(mo["checks"]):
        fi = int(round(tc * fps))
        truth, inf = load(os.path.join(d, "truth_%d.png" % k)), load(os.path.join(fd, seq[min(fi, len(seq) - 1)]))
        err, mean, _ = flip.evaluate(truth, inf, "LDR", applyMagma=True)
        Image.fromarray((np.clip(err, 0, 1) * 255).astype(np.uint8)).save(os.path.join(d, "flip%s_%d.png" % (sfx, k)))
        Image.fromarray((inf * 255).astype(np.uint8)).save(os.path.join(d, "inf%s_%d.png" % (sfx, k)))
        checks.append({"t": tc, "flip": round(float(mean), 4),
                       "brightness_ratio": round(float(luma(inf).mean() / max(luma(truth).mean(), 1e-6)), 3)})
    r = {"flip": round(float(np.mean([c["flip"] for c in checks])), 4),
         "brightness_ratio": round(float(np.mean([c["brightness_ratio"] for c in checks])), 3), "checks": checks,
         "frames": len(seq), "ms_per_frame": round(status.get("elapsed_ms", 0) / max(len(seq), 1), 1)}
    if mo.get("flicker"):
        fr = [np.asarray(Image.open(os.path.join(fd, f)).convert("L"), np.float32) for f in seq[-10:]]
        r["flicker"] = round(float(np.mean([np.abs(fr[i] - fr[i - 1]).mean() for i in range(1, len(fr))])), 3)
    import shutil
    shutil.copy(os.path.join(d, "truth_%d.png" % (len(checks) - 1)), os.path.join(d, "truth.png"))
    shutil.copy(os.path.join(d, "inf%s_%d.png" % (sfx, len(checks) - 1)), os.path.join(d, "inf%s.png" % sfx))
    return r


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
    ap.add_argument("--engine", choices=("raster", "pt", "live", "settle"), default="raster",
                    help="pt: Render 3D's GL path tracer; live: the Metal engine's live picture (RT shadows, "
                         "reflections, GI); settle: the Metal engine's path tracer. Files and baselines carry _<engine>")
    ap.add_argument("--pt-samples", type=int, default=256)
    ap.add_argument("--pt-bounces", type=int, default=8)
    a = ap.parse_args()
    if a.list:
        for k, v in CASES.items():
            print("%-12s %d objects, %d lights, %s" % (k, len(v["objects"]), len(v.get("lights", [])), v.get("view", "Standard")))
        return 0
    names = a.cases or list(CASES)
    sfx = "" if a.engine == "raster" else "_" + a.engine
    if a.engine in ("live", "settle"):
        # Render 3D's liveEngineFeatures: 7 = RT shadows + reflections + GI, 15 = also settle (path trace).
        os.environ["INFINITE_LIVE_ENGINE"] = "7" if a.engine == "live" else "15"
        # Row 9.3: INFINITE_SWEEP_FEATURES overrides the bits, e.g. 16 = the live engine with no rays (the Vulkan raster gate).
        os.environ["INFINITE_LIVE_ENGINE"] = os.environ.get("INFINITE_SWEEP_FEATURES", os.environ["INFINITE_LIVE_ENGINE"])
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    rows = []
    os.environ.setdefault("DEER_GLB", os.path.expanduser("~/Downloads/Assets/deer.glb"))
    os.environ.setdefault("SHOE_GLB", os.path.expanduser("~/Infinite/shoe-dl/MaterialsVariantsShoe.glb"))
    for name in names:
        scene = dict(CASES[name]); d = os.path.join(os.path.abspath(a.out), name)
        if any(o["shape"] == "model" and not os.path.exists(asset_file(o)) for o in scene["objects"]):
            print("skip %s: model file missing" % name)
            continue
        os.makedirs(d, exist_ok=True)
        if any(o.get("region") for o in scene["objects"]):
            scene["regions"] = REGIONS
        text = json.dumps(scene, sort_keys=True, indent=1)
        # The truth is reused while the scene, Blender's script and the asset files are unchanged.
        # Infinite-only keys (supersample) leave Blender's truth alone.
        btext = json.dumps({k: v for k, v in scene.items() if k != "supersample"}, sort_keys=True, indent=1)
        h = hashlib.sha1((btext + BLENDER_TRUTH_VERSION).encode()).hexdigest()
        hp = os.path.join(d, "scene.hash")
        frames = scene.get("frames")
        tags = ["_%d" % k for k in range(len(frames))] if frames else [""]
        if scene.get("motion"):
            tags = ["_%d" % k for k in range(len(scene["motion"]["checks"]))]
        for o in scene["objects"]:
            for tex_name in o.get("material", {}).get("maps", {}).values():
                make_texture(d, tex_name)
            ly = o.get("material", {}).get("layer")
            while ly:
                if ly.get("mask"): make_texture(d, ly["mask"])
                ly = ly.get("layer")
            if o.get("material", {}).get("shade", {}).get("tex") == "image":
                make_texture(d, o["material"]["shade"].get("image", "blotches"))
        if not (os.path.exists(hp) and open(hp).read() == h and all(os.path.exists(os.path.join(d, "truth%s.png" % t)) for t in tags)):
            open(os.path.join(d, "scene.json"), "w").write(text)
            _, log = run([BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, "blender_scene.py"), "--", d])
            if "SCENEDONE" not in log:
                sys.exit("blender failed on %s:\n%s" % (name, log[-3000:]))
            open(hp, "w").write(h)
        w, hgt = scene.get("size", (512, 512))
        results = []
        if scene.get("motion"):
            r = {"case": name, **run_motion(scene, name, d, sfx, a.engine, a)}
            r["baseline"] = base.get(name + sfx)
            r["pass"] = r["baseline"] is None or r["flip"] <= r["baseline"] + FLIP_SLACK
            rows.append(r)
            print("ABRESULT " + json.dumps(r), flush=True)
            continue
        for k, tag in enumerate(tags):
            patch = os.path.join(d, "inf%s%s.inf" % (sfx, tag))
            open(patch, "w").write(to_patch(scene, d, frames[k] if frames else None, a.engine, a.pt_samples, a.pt_bounces))
            stdout, log = run([INFINITE, "--frame", patch, "0", d + "/f/", "--size", "%dx%d" % (w, hgt)], 600)
            last = stdout.strip().splitlines()[-1] if stdout.strip() else ""
            status = json.loads(last) if last.startswith("{") else {}
            if not status.get("ok"):
                sys.exit("infinite failed on %s%s:\n%s" % (name, tag, log[-3000:]))
            if "MTLLibraryErrorDomain" in log or "shader compile" in log.lower():   # a broken shader falls back quietly
                sys.exit("shader error on %s%s:\n%s" % (name, tag, log[-3000:]))
            elapsed = status.get("elapsed_ms")
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
            r = {"case": name, **score(d, name, scene, sfx), "noise": noise_ratio(d, sfx), "elapsed_ms": elapsed}
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
