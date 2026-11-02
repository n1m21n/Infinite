#!/usr/bin/env python3
"""Dead-settings and mishap sweep for the 3D View: change one setting, render headless, compare with the base frame.

    python3 tools/3dview-settings-sweep.py                  # every setting, report in /tmp/3dview-sweep/report.md
    python3 tools/3dview-settings-sweep.py bloom fov        # only settings whose name contains one of these
    python3 tools/3dview-settings-sweep.py --no-extremes    # skip the min/max renders (about a third of the time)
    python3 tools/3dview-settings-sweep.py --binary PATH --out DIR

Per setting:
  LIVE     the frame changed
  DEAD     the frame did not change (beyond the base's own render noise)
  MISHAP   the change or a min/max value crashed, timed out, went black or went flat (one colour)
  UNTESTED Infinite --describe lists a param that SETTINGS does not cover, so a new param cannot slip through
EXPECTED holds settings that rightly leave a still frame alone (paths, motion over time, editor-only); each has a reason.
Exit 0 = nothing DEAD, MISHAP or UNTESTED outside EXPECTED.

Scene builder and patch format come from tools/render3d-lab.py. What this cannot judge: whether a change looks right
(that is the lab's board), and the save/reopen round trip (headless canonicalize is data-level, not a node load).
"""

import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_file_location("render3d_lab", os.path.join(ROOT, "tools", "render3d-lab.py"))
lab = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lab)

SIZE = "480x270"
OUT = "/tmp/3dview-sweep"
TYPES = {"view": "3D View", "cam": "Camera", "key": "Light", "env": "HDRI", "out": "Output"}

# (node, key) -> changed value, low, high, context. Context picks the base scene and the settings the change needs to
# show: "sets" are applied to base and change alike, so the only difference between the two frames is the setting.
S = lambda changed, lo=None, hi=None, ctx="dir", sets=(), frame=0: dict(changed=changed, lo=lo, hi=hi, ctx=ctx, sets=sets, frame=frame)
SETTINGS = {
    ("view", "width"): S(960, 16, 8192, ctx="size"),
    ("view", "height"): S(540, 16, 8192, ctx="size"),
    ("view", "samples"): S(0, 0, 3),
    ("view", "shadows"): S(0),
    ("view", "exposure"): S(2.0, 0, 16),
    ("view", "bloom"): S(1.0, 0, 10),
    ("view", "ao"): S(1.0, 0, 4),
    ("view", "supersample"): S(2, 1, 4),
    ("view", "tonemap"): S(0, 0, 5),
    ("view", "bloomThreshold"): S(0.3, 0, 10, sets=("view.bloom=1",)),
    ("view", "bloomRadius"): S(1.0, 0, 4, sets=("view.bloom=1",)),
    ("view", "aoRadius"): S(1.5, 0, 10, sets=("view.ao=1",)),
    ("view", "shadowQuality"): S(0, 0, 3),
    ("view", "shadowBias"): S(0.5, 0, 1),
    ("view", "shadowSoftness"): S(4.0, 0, 20),
    ("view", "shadowStrength"): S(0.2, 0, 1),
    ("view", "dofAperture"): S(2.0, 0, 64),
    ("view", "focusDistance"): S(9.0, 0.01, 1000, sets=("view.dofAperture=2",)),
    ("view", "ambient"): S("1 0.2 0.2"),
    ("view", "envIntensity"): S(3.0, 0, 20, ctx="env"),
    ("view", "floatOutput"): S(1),
    ("view", "renderPass"): S(1, 0, 10),
    ("view", "mistStart"): S(0.0, 0, 100, sets=("view.renderPass=5",)),
    ("view", "mistDepth"): S(5.0, 0.01, 200, sets=("view.renderPass=5",)),
    ("view", "mistFalloff"): S(1, 0, 2, sets=("view.renderPass=5",)),
    ("view", "mistIntensity"): S(0.5, 0, 0.9, sets=("view.renderPass=5",)),   # 1 is mist everywhere: rightly flat
    ("view", "mistHeight"): S(1.0, 0, 100, sets=("view.renderPass=5", "view.mistStart=0", "view.mistDepth=10")),
    ("view", "passThreshold"): S(0.0, 0, 1, ctx="fade", sets=("view.renderPass=1",)),
    ("view", "cavity"): S(1),
    ("view", "cavityType"): S(1, 0, 2, sets=("view.cavity=1",)),
    ("view", "cavityRidge"): S(2.5, 0, 2.5, sets=("view.cavity=1",)),
    ("view", "cavityValley"): S(2.5, 0, 2.5, sets=("view.cavity=1",)),
    ("view", "cavityDistance"): S(1.0, 0.01, 2, sets=("view.cavity=1",)),
    ("view", "motionBlur"): S(1, frame=2, sets=("cam.orbitPerBeat=1", "view.motionShutter=1")),
    ("view", "motionShutter"): S(0.25, 0, 2, frame=2, sets=("cam.orbitPerBeat=1", "view.motionBlur=1", "view.motionShutter=1")),
    ("view", "motionPosition"): S(0, 0, 2, frame=2, sets=("cam.orbitPerBeat=1", "view.motionBlur=1", "view.motionShutter=1", "view.motionMax=200")),
    ("view", "motionMax"): S(4, 1, 256, frame=2, sets=("cam.orbitPerBeat=1", "view.motionBlur=1", "view.motionShutter=1")),
    ("view", "curvatureRidge"): S(0.0, 0, 2, sets=("view.cavity=1", "view.cavityType=1")),
    ("view", "curvatureValley"): S(0.0, 0, 2, sets=("view.cavity=1", "view.cavityType=1")),
    ("view", "projection"): S(2, 0, 4),
    ("view", "domeAngle"): S(200, 180, 220, sets=("view.projection=4",)),
    ("view", "domeTilt"): S(30, -90, 90, sets=("view.projection=4",)),
    ("cam", "projection"): S(1, 0, 1),
    ("cam", "fov"): S(60, 1, 179),
    ("cam", "shiftX"): S(0.3, -2, 2),
    ("cam", "shiftY"): S(0.3, -2, 2),
    ("cam", "sensorWidth"): S(18, 1, 200, ctx="dof"),
    ("cam", "orthoHeight"): S(4, 0.01, 1000, sets=("cam.projection=1",)),
    ("cam", "distance"): S(14, 0.3, 20),
    ("cam", "azimuth"): S(120, -180, 180),
    ("cam", "elevation"): S(50, -85, 85),
    ("cam", "targetX"): S(1.5, -3, 3),
    ("cam", "targetY"): S(1.0, -3, 3),
    ("cam", "targetZ"): S(1.5, -3, 3),
    ("cam", "roll"): S(25, -180, 180),
    ("cam", "near"): S(9, 0.01, 1),
    ("cam", "far"): S(8, 5, 500),
    ("cam", "orbitPerBeat"): S(0.5, -1, 1, frame=30),
    ("key", "type"): S(1, 0, 5),
    ("key", "azimuth"): S(-60, -180, 180),
    ("key", "elevation"): S(15, -85, 85),
    ("key", "distance"): S(1.5, 0.2, 20, ctx="point"),
    ("key", "color"): S("1 0.2 0.2"),
    ("key", "intensity"): S(5, 0, 5),
    ("key", "falloff"): S(2, 0, 4, ctx="point"),
    ("key", "spotAngle"): S(20, 5, 85, ctx="spot"),
    ("key", "spotPenumbra"): S(1.0, 0, 1, ctx="spot"),
    ("key", "orbitPerBeat"): S(0.5, -1, 1, frame=30),
    ("key", "areaWidth"): S(6, 0.05, 10, ctx="area"),
    ("key", "areaHeight"): S(6, 0.05, 10, ctx="area"),
    ("key", "normalize"): S(0, ctx="area", sets=("key.areaWidth=3",)),
    ("key", "softFalloff"): S(0, ctx="point"),
    ("key", "diffuse"): S(0.0, 0, 1),
    ("key", "specular"): S(0.0, 0, 1),
    ("key", "transmission"): S(0.0, 0, 1, ctx="glass"),
    ("key", "castShadow"): S(0),
    ("key", "shadowRadius"): S(3.0, 0, 10),
    ("key", "shadowFilter"): S(5.0, 0, 10),
    ("key", "shadowRes"): S(1, 0, 3),
    ("key", "shadowClip"): S(7.0, 0.01, 10, ctx="spot"),
    ("env", "path"): S("/tmp/none.hdr", ctx="env"),
    ("env", "intensity"): S(3.0, 0, 20, ctx="env"),
    ("env", "rotation"): S(120, -360, 360, ctx="env"),
    ("env", "sunThreshold"): S(10, 0, 100, ctx="env"),
    ("env", "sunAngle"): S(20, 0, 180, ctx="env", sets=("env.sunThreshold=10", "key.castShadow=0")),
    ("env", "sunShadow"): S(0, ctx="env", sets=("env.sunThreshold=10", "key.castShadow=0")),
    ("env", "sunShadowRes"): S(1, 0, 3, ctx="env", sets=("env.sunThreshold=10", "key.castShadow=0")),
    ("env", "sunShadowFilter"): S(6, 0, 10, ctx="env", sets=("env.sunThreshold=10", "key.castShadow=0")),
    ("out", "recordFps"): S(60),
    ("out", "includeAudio"): S(1),
    ("out", "imageFormat"): S(1),
    ("out", "videoFormat"): S(1),
    ("out", "exportImagePath"): S("/tmp/x.png"),
    ("out", "recordVideoPath"): S("/tmp/x.mp4"),
    ("out", "offlineFps"): S(60),
    ("out", "offlineDurationSeconds"): S(3),
    ("out", "offlinePrerollFrames"): S(10),
    # Fields of the scene text (`settings` line and items), changed by rewriting the patch, not --set.
    ("scene", "percent"): S(50, 1, 400, ctx="size"),
    ("scene", "transparent"): S(1),
    ("scene", "shadowItem"): S("rim"),
    ("scene", "fStop"): S(1.4, 0, 64),
    ("scene", "border"): S({"border": 1, "borderMin0": 0.25, "borderMin1": 0.25, "borderMax0": 0.75, "borderMax1": 0.75}),
    ("scene", "crop"): S({"border": 1, "crop": 1, "borderMax0": 0.5}),
    ("scene", "borderMin0"): S({"border": 1, "borderMin0": 0.4}),
    ("scene", "borderMin1"): S({"border": 1, "borderMin1": 0.4}),
    ("scene", "borderMax0"): S({"border": 1, "borderMax0": 0.6}),
    ("scene", "borderMax1"): S({"border": 1, "borderMax1": 0.6}),
    ("scene", "item.delta"): S("1 0 0 0 30 0 1 1 1"),
    ("scene", "item.visible"): S(0),
    ("scene", "item.render"): S(0),
    ("scene", "item.scale"): S(2.0),
    ("scene", "item.lookAt"): S("box"),
    ("view", "viewShading"): S(1, 0, 1),
    ("view", "solidLighting"): S(1, 0, 2, sets=("view.viewShading=1",)),
    ("view", "matcap"): S(1, 0, 3, sets=("view.viewShading=1", "view.solidLighting=1")),
    ("view", "solidColor"): S(3, 0, 4, sets=("view.viewShading=1",)),
    ("view", "solidSingle"): S("1 0.2 0.2", sets=("view.viewShading=1", "view.solidColor=2")),
    ("view", "solidSpecular"): S(0, sets=("view.viewShading=1",)),
    ("view", "studioWorld"): S(1, sets=("view.viewShading=1",)),
    ("view", "studioRotation"): S(90, -180, 180, sets=("view.viewShading=1", "view.studioWorld=1")),
    ("scene", "item.unlit"): S("key"),
    ("scene", "item.pass"): S(40, sets=("view.renderPass=6",)),
    ("scene", "world.color"): S("0.8 0.1 0.1"),
    ("scene", "world.strength"): S(4.0),
}
EXPECTED = {
    ("cam", "orbitPerBeat"): "motion over beats; tested at frame 30",
    ("key", "orbitPerBeat"): "motion over beats; tested at frame 30",
    ("view", "floatOutput"): "16-bit float file; an 8-bit frame looks the same",
    ("out", "recordFps"): "record settings: no effect on a still", ("out", "includeAudio"): "record settings: no effect on a still",
    ("out", "imageFormat"): "file format of the export, not the picture", ("out", "videoFormat"): "file format of the export",
    ("out", "exportImagePath"): "a path", ("out", "recordVideoPath"): "a path",
    ("out", "offlineFps"): "render length/rate, not a still", ("out", "offlineDurationSeconds"): "render length, not a still",
    ("out", "offlinePrerollFrames"): "pre-roll before frame 0, not frame 0 itself",
    ("scene", "transparent"): "alpha only, and --frame writes opaque PNGs (--alpha is --frames-dir only)",
    ("scene", "item.backdrop"): "camera background image: 3D View only, never rendered (VIEW3DTEST checks it)",
}
# Settings-line fields that only drive the editor (view, gizmos, snapping, guides): a headless render can't see them.
EDITOR_ONLY = ["fps", "seconds", "snap", "snapMove", "snapRot", "snapScale", "passAlpha", "safe", "thirds", "center",
               "golden", "safeTitle", "safeAction", "orientation", "pivot", "cursor", "zoomToMouse", "orbitSelection",
               "proportional", "propSize", "propFalloff", "invertZoom", "textInfo", "stats", "originsAll", "gizmoMove",
               "gizmoNav", "snapObjects"]
SETTING_FIELDS = ["fps", "seconds", "percent", "transparent", "snap", "snapMove", "snapRot", "snapScale", "passAlpha",
                  "safe", "thirds", "center", "golden", "safeTitle", "safeAction", "orientation", "pivot", "cursor0",
                  "cursor1", "cursor2", "zoomToMouse", "orbitSelection", "shadowItem", "fStop", "proportional", "propSize",
                  "propFalloff", "invertZoom", "textInfo", "stats", "originsAll", "gizmoMove", "gizmoNav", "snapObjects",
                  "border", "crop", "borderMin0", "borderMin1", "borderMax0", "borderMax1"]
# What a field reads as when the settings line is padded out to reach a later one.
FIELD_DEFAULT = {"propSize": 1, "borderMax0": 1, "borderMax1": 1}


def build(ctx):
    """The base scene: floor, a rough ball, a metal ball, a glowing ball, a box, three lights, a camera."""
    light_kind = {"point": 1, "spot": 4, "area": 5}.get(ctx, 0)
    s = lab.Scene("settings sweep base", world=(0.05, 0.06, 0.09), f_stop=1.4 if ctx == "dof" else 0.0,
                  view={"width": 480, "height": 270, "samples": 2, "shadows": True, "shadowQuality": 2})
    s.floor()
    lab.sphere(s, "Rough", (-1.2, 0, 0), 1.0, color=(0.8, 0.3, 0.2), roughness=0.8)
    lab.sphere(s, "Metal", (0, 0, 0.4), 1.0, color=(0.9, 0.9, 0.9), metallic=1, roughness=0.15)
    lab.sphere(s, "Glow", (1.2, 0, 0), 0.6, color=(0, 0, 0), emission=6, emissionColor=(1, 0.5, 0.2))
    s.add("Cube", "Box", (0.6, 0.3, -1.2), scale=0.6, color=(0.2, 0.5, 0.9), roughness=0.4)
    if ctx == "fade":
        lab.sphere(s, "Fade", (0.2, 0, 1.6), 1.0, color=(1, 1, 1), opacity=0.3)
    if ctx == "glass":
        lab.sphere(s, "Glass", (0.2, 0, 1.6), 1.0, color=(1, 1, 1), roughness=0.3, transmission=1, ior=1.45)
    s.camera_at(70, 20, 6, (0, 0.4, 0), 35)
    extra = {"point": dict(distance=3), "spot": dict(distance=5, spotAngle=40, spotPenumbra=0.2),
             "area": dict(distance=4, areaWidth=1, areaHeight=1)}.get(ctx, {})
    s.light("Key", 60, 50, 2.4, (1, 0.96, 0.9), kind=light_kind, **extra)
    s.light("Rim", -140, 25, 1.2, (0.8, 0.88, 1))
    s.light("Fill", -50, 15, 0.4)
    if ctx == "env":
        s.world_image = s._node("HDRI", {"path": sun_hdri()})
    return s


def sun_hdri():
    """A dim blue sky with one small, very bright sun, written as a flat Radiance .hdr."""
    path = os.path.join(OUT, "sun.hdr")
    if not os.path.exists(path):
        import numpy as np
        w, h = 256, 128
        img = np.zeros((h, w, 3), np.float32) + (0.15, 0.2, 0.35)
        img[:h // 2] += (0.1, 0.12, 0.2)
        y, x = np.mgrid[0:h, 0:w]
        img[(x - 40) ** 2 + (y - 30) ** 2 < 9] = (400, 380, 340)
        mx = img.max(axis=2)
        e = np.ceil(np.log2(np.maximum(mx, 1e-32))).astype(np.int32)
        mant = img / np.ldexp(1.0, e)[..., None] * 256
        rgbe = np.dstack([np.clip(mant, 0, 255), np.where(mx > 1e-32, e + 128, 0)]).astype(np.uint8)
        with open(path, "wb") as f:
            f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n" + f"-Y {h} +X {w}\n".encode() + rgbe.tobytes())
    return path


def patch_text(s, path):
    s.write(path)
    lines, out = open(path).read().split("\n"), []
    ids = {}
    for name, idx in (("cam", s.camera), ("key", s.key)):
        ids[1000 + s.items[idx - 1][1]] = name
    if s.world_image:
        ids[1000 + s.world_image] = "env"
    for line in lines:
        out.append(line)
        m = re.match(r"\s+uid (\d+)$", line)
        if m and int(m.group(1)) in ids:
            out.append(f"  id {ids[int(m.group(1))]}")
    open(path, "w").write("\n".join(out))


def scene_edit(path, s, key, value):
    """Rewrite one field of the 3D View's scene text in an existing patch."""
    text = open(path).read()
    m = re.search(r"(  s scene )(.*)", text)
    lines = m.group(2).split("\\n")
    item = 2   # the metal ball
    for n, line in enumerate(lines):
        f = line.split(" ")
        if key in SETTING_FIELDS and f[0] == "settings":
            edits = value if isinstance(value, dict) else {key: value}
            if key == "shadowItem":
                edits = {key: next(t[0] for t in s.items if t[2] == "Rim")}
            while len(f) <= max(SETTING_FIELDS.index(k) for k in edits) + 1:
                f.append(str(FIELD_DEFAULT.get(SETTING_FIELDS[len(f) - 1], 0)))
            for k, v in edits.items():
                f[SETTING_FIELDS.index(k) + 1] = str(v)
        elif key.startswith("item.") and f[0] == "item" and f[1] == str(item):
            if key == "item.visible":
                f[12] = str(value)
            elif key == "item.scale":
                f[9:12] = [str(float(x) * value) for x in f[9:12]]
            elif key == "item.unlit":
                f.append(str(1000 + s.items[s.key - 1][1]))
        elif key.startswith("world.") and f[0] == "world":
            if key == "world.color":
                f[2:6] = ["1", *value.split()]
            else:
                f[6] = str(value)
        lines[n] = " ".join(f)
    if key == "item.pass":
        lines.append(f"pass {item} {value}")
    if key == "item.render":
        lines.append(f"itemx {item} 0")
    if key == "item.delta":
        lines.append(f"delta {item} {value}")
    if key == "item.lookAt":   # the camera turns to keep the box in view
        lines.append(f"look {s.camera} {next(t[0] for t in s.items if t[2] == 'Box')}")
    text = text[:m.start(2)] + "\\n".join(lines) + text[m.end(2):]
    open(path, "w").write(text)


class Runner:
    def __init__(self, binary, out):
        self.binary, self.out = binary, out

    def render(self, patch, name, sets=(), frame=0, size=True):
        d = os.path.join(self.out, "frames", name)
        os.makedirs(d, exist_ok=True)
        cmd = [self.binary, "--frame", patch, str(frame), d + "/", "--timeout", "60"]
        if size:
            cmd += ["--size", SIZE]
        for v in sets:
            cmd += ["--set", v]
        try:
            run = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired:
            return None, "timed out"
        try:
            st = json.loads(run.stdout.strip().splitlines()[-1])
        except (ValueError, IndexError):
            return None, f"exit {run.returncode}, no status"
        if not st.get("ok"):
            return None, "; ".join(e["message"] for e in st.get("errors", []))[:160]
        return st["frame_stats"][0]["file"], None


def load(path):
    import numpy as np
    from PIL import Image
    return np.asarray(Image.open(path).convert("RGBA"), dtype=np.float32)


def sick(img):
    """Black, flat or NaN-looking frames. Returns a reason or None."""
    rgb = img[..., :3]
    if img[..., 3].max() > 0 and rgb.mean() < 0.5:
        return "black frame"
    if rgb.std() < 0.5 and img[..., 3].std() < 0.5:
        return "flat frame (one colour)"
    return None


def diff(a, b):
    """Mean difference, and the share of pixels that moved by more than 8 levels (catches small, local changes)."""
    if a.shape != b.shape:
        return float("inf"), 1.0
    import numpy as np
    d = np.abs(a - b)
    return float(d.mean()), float((d.max(axis=2) > 8).mean())


def fmt(v):
    return v if isinstance(v, str) else repr(v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("only", nargs="*")
    ap.add_argument("--binary", default=lab.DEFAULT_BINARY)
    ap.add_argument("--out", default="/tmp/3dview-sweep")
    ap.add_argument("--no-extremes", action="store_true")
    ap.add_argument("--jobs", type=int, default=2)
    args = ap.parse_args()
    global OUT
    OUT = args.out
    binary = args.binary if os.path.isabs(args.binary) else os.path.join(ROOT, args.binary)
    os.makedirs(args.out, exist_ok=True)
    runner = Runner(binary, args.out)
    t0 = time.time()

    # Coverage: every param the app lists must be in SETTINGS.
    untested = []
    for node, type_name in TYPES.items():
        dj = os.path.join(args.out, f"describe-{node}.json")
        subprocess.run([binary, "--describe", type_name, "--json", dj], capture_output=True)
        for p in json.load(open(dj))["types"][0]["params"]:
            if p["key"] != "scene" and (node, p["key"]) not in SETTINGS:
                untested.append((node, p["key"]))

    todo = [(k, v) for k, v in SETTINGS.items() if not args.only or any(o.lower() in k[1].lower() for o in args.only)]
    bases, scenes = {}, {}
    for ctx in sorted({v["ctx"] for _, v in todo}):
        s = build(ctx)
        path = os.path.join(args.out, f"base-{ctx}.inf")
        patch_text(s, path)
        scenes[ctx] = (s, path)

    def base_frame(ctx, sets, frame):
        k = (ctx, sets, frame)
        if k not in bases:
            f, err = runner.render(scenes[ctx][1], f"base-{ctx}-{abs(hash(k))}", sets, frame, size=ctx != "size")
            bases[k] = (f, err)
        return bases[k]

    # Noise floor: the same base twice.
    f1, e1 = base_frame("dir", (), 0)
    if e1:
        sys.exit(f"base render failed: {e1}")
    f2, _ = runner.render(scenes["dir"][1], "base-dir-again")
    noise, noise_px = diff(load(f1), load(f2))
    floor = max(0.02, noise * 3)
    px_floor = max(0.001, noise_px * 3)

    def one(item):
        (node, key), sp = item
        ctx, sets, frame = sp["ctx"], tuple(sp["sets"]), sp["frame"]
        bf, berr = base_frame(ctx, sets, frame)
        if berr:
            return (node, key, "MISHAP", f"base for this context failed: {berr}", None)
        s, base_path = scenes[ctx]
        size = ctx != "size"
        tag = f"{node}-{key}".replace(".", "_")

        def variant(value, suffix):
            if node == "scene":
                p = os.path.join(args.out, f"{tag}-{suffix}.inf")
                open(p, "w").write(open(base_path).read())
                scene_edit(p, s, key, value)
                return runner.render(p, f"{tag}-{suffix}", sets, frame, size)
            return runner.render(base_path, f"{tag}-{suffix}", sets + (f"{node}.{key}={value}",), frame, size)

        f, err = variant(sp["changed"], "changed")
        if err:
            return (node, key, "MISHAP", f"changed to {fmt(sp['changed'])}: {err}", None)
        img = load(f)
        why = sick(img)
        if why and not sick(load(bf)):
            return (node, key, "MISHAP", f"changed to {fmt(sp['changed'])}: {why}", f)
        d, px = diff(load(bf), img)
        verdict = "LIVE" if d > floor or px > px_floor else "DEAD"
        note = (f"changed to {fmt(sp['changed'])}: mean diff {d:.3f}, {px * 100:.2f}% px" if d != float("inf")
                else f"changed to {fmt(sp['changed'])}: size changed")
        if not args.no_extremes:
            for label in ("lo", "hi"):
                if sp[label] is None:
                    continue
                fe, erre = variant(sp[label], label)
                if erre:
                    return (node, key, "MISHAP", f"{label} {fmt(sp[label])}: {erre}", fe)
                w = sick(load(fe))
                if w and not sick(load(bf)) and not (key == "intensity" and label == "lo") and not (key == "exposure" and label == "lo"):
                    return (node, key, "MISHAP", f"{label} {fmt(sp[label])}: {w}", fe)
        return (node, key, verdict, note, f)

    with ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(one, todo))

    bad = []
    lines = ["# 3D View settings sweep", "",
             f"`python3 tools/3dview-settings-sweep.py`, {time.strftime('%Y-%m-%d %H:%M')}, {len(results)} settings, "
             f"{time.time() - t0:.0f} s. Noise floor {floor:.3f} (base vs base {noise:.3f}).", "",
             "| Node | Setting | Result | Detail | Expected because |", "|---|---|---|---|---|"]
    order = {"MISHAP": 0, "DEAD": 1, "LIVE": 2}
    for node, key, verdict, note, _ in sorted(results, key=lambda r: (order[r[2]], r[0], r[1])):
        why = EXPECTED.get((node, key), "")
        if verdict != "LIVE" and not why:
            bad.append((node, key, verdict))
        lines.append(f"| {node} | {key} | {verdict} | {note} | {why} |")
    for node, key in untested:
        bad.append((node, key, "UNTESTED"))
        lines.append(f"| {node} | {key} | UNTESTED | in --describe, not in SETTINGS | |")
    lines += ["", "Editor-only settings (no effect on a render, not testable headless): " + ", ".join(EDITOR_ONLY) + "."]
    report = os.path.join(args.out, "report.md")
    open(report, "w").write("\n".join(lines) + "\n")
    counts = {v: sum(1 for r in results if r[2] == v) for v in order}
    print(f"{counts['LIVE']} live, {counts['DEAD']} dead, {counts['MISHAP']} mishap, {len(untested)} untested; "
          f"{len(bad)} need a look -> {report}")
    for node, key, verdict in bad:
        print(f"  {verdict:8} {node}.{key}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
