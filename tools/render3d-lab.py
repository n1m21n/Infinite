#!/usr/bin/env python3
"""Render-quality lab: one 3D View scene per thing a render can get wrong.

    python3 tools/render3d-lab.py                 # write tests/render3d/*.inf, render, build a board
    python3 tools/render3d-lab.py dof bloom       # only these scenes
    python3 tools/render3d-lab.py --list          # what each scene judges
    python3 tools/render3d-lab.py --out DIR       # where frames and board.png go (default /tmp/render3d-lab)
    python3 tools/render3d-lab.py --binary PATH

The scenes are written from here, so change them here. The board is for looking; the status table
holds what can be measured (floor stripes, how far bloom spreads, roughness order).
Exit 0 = every scene rendered and every measure passed, 1 = one did not.
"""

import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENES_DIR = os.path.join(ROOT, "tests", "render3d")
DEFAULT_BINARY = "build-dev/Infinite.app/Contents/MacOS/Infinite"
FRAME_SIZE = (1280, 720)

CATEGORY = {"3D": "3D", "Output": "Utility", "Noise": "Source"}


class Scene:
    def __init__(self, judge, world=(0.012, 0.016, 0.035), view=None, f_stop=0.0):
        self.judge = judge
        self.world = world
        self.world_image = None
        self.view = dict(view or {})
        self.f_stop = f_stop
        self.nodes = []
        self.items = []
        self.camera = 0
        self.key = 0
        self.focus = 0

    def _node(self, type_name, params):
        index = len(self.nodes) + 1
        self.nodes.append((index, type_name, params))
        return index

    def _item(self, index, name, pos=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
        self.items.append((len(self.items) + 1, index, name, pos, rot, scale))
        return len(self.items)

    def add(self, type_name, name, pos=(0, 0, 0), rot=(0, 0, 0), scale=1.0, **params):
        s = (scale, scale, scale) if isinstance(scale, (int, float)) else scale
        return self._item(self._node(type_name, params), name, pos, rot, s)

    def floor(self, size=30, color=(0.5, 0.5, 0.52), **params):
        params = {"color": color, "roughness": 0.7, "metallic": 0, "specular": 0.25, **params}
        return self.add("Plane", "Floor", rot=(-90, 0, 0), scale=size, **params)

    def camera_at(self, azimuth, elevation, distance, target=(0, 0, 0), fov=30):
        item = self.add("Camera", "Camera", azimuth=azimuth, elevation=elevation, distance=distance,
                        fov=fov, targetX=target[0], targetY=target[1], targetZ=target[2])
        self.camera = item

    def light(self, name, azimuth, elevation, intensity, color=(1, 1, 1), kind=0, **params):
        item = self.add("Light", name, type=kind, azimuth=azimuth, elevation=elevation,
                        intensity=intensity, color=color, **params)
        self.key = self.key or item
        return item

    def three_point(self, key=2.4):
        self.light("Key", 60, 50, key, (1, 0.96, 0.9))
        self.light("Rim", -140, 25, key * 0.5, (0.8, 0.88, 1))
        self.light("Fill", -50, 15, key * 0.15)

    def text(self):
        items = {index: item for item, index, *_ in self.items}
        world_uid = 1000 + self.world_image if self.world_image else 0
        lines = [f"next {len(self.items) + 1}", f"camera {self.camera}",
                 f"world {world_uid} {0 if world_uid else 1} {self.world[0]} {self.world[1]} {self.world[2]} 1 {self.focus}",
                 f"settings 30 2 100 0 0 0.25 15 0.1 0.5 0 0 0 0 0.035 0.05 0 0 0 0 0 1 1 {self.key} {self.f_stop}"]
        for item, index, name, pos, rot, scale in self.items:
            nums = " ".join(str(v) for v in (*pos, *rot, *scale))
            lines += [f"item {item} {1000 + index} {nums} 1", f"label {item} {name}"]
        assert len(items) == len(self.items)
        return "\\n".join(lines)

    def write(self, path):
        out = ["infinite-patch 1", f"# {self.judge}"]
        for index, type_name, params in self.nodes:
            out += [f"node {index} {CATEGORY.get(type_name, '3D')} {type_name}", f"  uid {1000 + index}"]
            out += [f"  {param_line(k, v)}" for k, v in params.items()]
            out.append("end")
        view = {"width": FRAME_SIZE[0], "height": FRAME_SIZE[1], "samples": 4, "shadows": True,
                "shadowQuality": 2, "ambient": (0.05, 0.06, 0.08), **self.view}
        v = len(self.nodes) + 1
        out += [f"node {v} 3D 3D View", "  id view"] + [f"  {param_line(k, x)}" for k, x in view.items()]
        out += [f"  s scene {self.text()}", "end", f"node {v + 1} Utility Output", "  id out", "end",
                "cable out 0 view"]
        with open(path, "w") as f:
            f.write("\n".join(out) + "\n")


INT_PARAMS = {"type", "detail", "sides", "samples", "shadowQuality", "supersample", "tonemap"}


def param_line(key, value):
    if isinstance(value, bool):
        return f"b {key} {int(value)}"
    if isinstance(value, tuple):
        return f"c {key} {value[0]} {value[1]} {value[2]}"
    if key in INT_PARAMS:
        return f"i {key} {value}"
    return f"f {key} {value}"


def sphere(s, name, pos, scale, **params):
    """A sphere resting at height pos[1] above the floor (its radius is half its scale)."""
    s.add("Sphere", name, (pos[0], pos[1] + scale / 2, pos[2]), scale=scale, detail=48, **params)


def scene_materials():
    s = Scene("materials: top dielectric, bottom metal; roughness rises left to right, highlights must widen and dim evenly")
    s.floor(color=(0.1, 0.1, 0.12))
    for row, metal in enumerate((0.0, 1.0)):
        for col in range(6):
            sphere(s, f"r{col} m{row}", (col * 1.2 - 3, 0, row * 1.5 - 0.75), 1.0, metallic=metal,
                   roughness=[0.05, 0.2, 0.35, 0.5, 0.75, 1.0][col], color=(0.85, 0.3, 0.2))
    s.camera_at(0 + 90, 28, 15, (0, 0.3, 0), 26)
    s.three_point(3.0)
    return s


def scene_shadows():
    s = Scene("shadows: contact at the base of everything, no float, no acne on curves, thin pole keeps its shadow",
              view={"shadowSoftness": 1.0})
    s.floor(size=14, color=(0.7, 0.7, 0.72))
    s.add("Cube", "Block", (-2.2, 0.5, 0), (0, 25, 0), 1.0, color=(0.7, 0.7, 0.75))
    sphere(s, "Ball", (0, 0, 0.6), 1.2, color=(0.7, 0.7, 0.75))
    s.add("Cylinder", "Pole", (1.8, 1.0, 0.2), scale=(0.08, 2.0, 0.08), color=(0.7, 0.7, 0.75))
    s.add("Cube", "Hover plate", (-0.8, 0.06, 2.2), scale=(1.4, 0.04, 1.0), color=(0.7, 0.7, 0.75))
    s.add("Torus", "Ring", (2.6, 0.5, 1.8), (90, 0, 0), 1.0, detail=48, sides=24, color=(0.7, 0.7, 0.75))
    s.camera_at(90, 30, 13, (0, 0.5, 0.4), 30)
    s.light("Key", 30, 38, 2.6)
    return s


def scene_ao():
    s = Scene("ao: creases darken softly, open floor stays clean with no stripes or halo around the ball",
              view={"ao": 1.0, "aoRadius": 0.6, "shadows": False, "ambient": (0.5, 0.5, 0.52)})
    s.floor(size=24, color=(0.75, 0.75, 0.75))
    s.add("Plane", "Wall", (0, 3, -2.5), scale=12, color=(0.75, 0.75, 0.75), roughness=0.8)
    s.add("Cube", "Low", (-1.6, 0.5, -1.6), (0, 20, 0), 1.0, color=(0.75, 0.75, 0.75))
    s.add("Cube", "Top", (-1.5, 1.25, -1.6), (0, 50, 0), 0.5, color=(0.75, 0.75, 0.75))
    sphere(s, "Ball", (1.2, 0, 0.2), 1.3, color=(0.75, 0.75, 0.75))
    s.camera_at(90, 24, 11, (0, 0.8, 0), 30)
    s.light("Soft", 90, 70, 0.3)
    return s


def stage(s):
    s.floor(size=60, color=(0.6, 0.6, 0.62))
    s.add("Plane", "Back wall", (0, 3, -3), scale=60, color=(0.6, 0.6, 0.62), roughness=0.8)
    sphere(s, "Ball", (-1.2, 0, 0), 1.0, color=(0.75, 0.75, 0.78))
    s.add("Cube", "Block", (1.2, 0.5, 0), (0, 30, 0), 1.0, color=(0.75, 0.75, 0.78))
    s.camera_at(90, 16, 10, (0, 1.0, 0), 34)
    return s


def scene_light_point():
    s = stage(Scene("point light: pool of light falls away with distance; it casts no shadow, by design"))
    s.light("Point", 90, 45, 3.0, kind=1, distance=3.5)
    return s


def scene_light_spot():
    s = stage(Scene("spot: round cone, soft penumbra edge, shadow lands inside the cone"))
    s.light("Spot", 70, 55, 4.0, kind=4, distance=5, spotAngle=38, spotPenumbra=0.3)
    return s


def scene_light_area():
    s = stage(Scene("area: wide soft highlight, softer shadow than the spot"))
    s.light("Area", 70, 55, 3.0, kind=5, distance=4, areaWidth=2.5, areaHeight=1.5)
    return s


def scene_dof():
    s = Scene("dof: middle ball sharp, blur grows both ways, no sharp halo where near blur crosses the sharp ball",
              f_stop=1.4, view={"shadowQuality": 1})
    s.floor(color=(0.25, 0.25, 0.28))
    ids = []
    for i in range(7):
        s.add("Sphere", f"Ball {i}", (0.7 - i * 0.24, 0.18, 1.8 - i * 0.6), scale=0.36, detail=48,
              color=(0.8, 0.35 + i * 0.05, 0.25))
        ids.append(len(s.items))
    for i in range(5):
        s.add("Sphere", f"Spark {i}", (-1.2 + i * 0.6, 0.5 + (i % 2) * 0.3, -3), scale=0.04, detail=16,
              emission=20, emissionColor=(1, 0.8, 0.5))
    s.camera_at(90, 8, 4.5, (0, 0.25, 0), 24)
    s.focus = ids[3]
    s.three_point(2.5)
    return s


def scene_bloom():
    s = Scene("bloom: glow rises smoothly with emission, stays round, no square edge or clipped ring",
              world=(0, 0, 0), view={"bloom": 0.5, "bloomThreshold": 1.0, "bloomRadius": 0.6, "ambient": (0, 0, 0)})
    for i, power in enumerate((0.5, 1, 2, 4, 8, 16)):
        s.add("Sphere", f"Emit {power}", (i * 1.3 - 3.25, 0, 0), scale=0.7, detail=48, color=(0, 0, 0),
              emission=power, emissionColor=(1, 0.45, 0.2))
    s.camera_at(90, 0, 11, (0, 0, 0), 30)
    s.light("Off", 90, 45, 0.0)
    return s


def scene_glass():
    s = Scene("glass: objects behind bend and stay visible, rough glass blurs, dispersion splits colour at the rim")
    s.floor(color=(0.55, 0.55, 0.58))
    for i, color in enumerate(((0.9, 0.2, 0.15), (0.95, 0.75, 0.15), (0.2, 0.7, 0.3), (0.2, 0.4, 0.9), (0.7, 0.25, 0.8))):
        s.add("Cube", f"Back {i}", (i * 1.5 - 3, 0.6, -2.0), (0, 0, 0), (0.5, 1.2, 0.5), color=color)
    for i, params in enumerate(({}, {"ior": 1.33}, {"transmissionRoughness": 0.35}, {"dispersion": 1.0})):
        sphere(s, f"Glass {i}", (i * 1.6 - 2.4, 0, 0.6), 1.2, transmission=1.0, roughness=0.05, metallic=0,
               color=(1, 1, 1), **params)
    s.camera_at(90, 12, 10, (0, 0.6, 0), 30)
    s.three_point(2.5)
    return s


def scene_aliasing():
    s = Scene("edges: thin poles stay continuous at distance, no crawling stairs on the long edges")
    s.floor(color=(0.75, 0.75, 0.78))
    for i in range(20):
        s.add("Cube", f"Slat {i}", (-4 + i * 0.42, 0.75, -i * 0.5), (0, 8, 0), (0.025, 1.5, 0.025), color=(0.05, 0.05, 0.08))
    s.add("Cube", "Long edge", (0, 0.1, 2), (0, 3, 0), (9, 0.2, 0.4), color=(0.05, 0.05, 0.08))
    s.camera_at(90, 14, 12, (0, 0.6, 0), 30)
    s.three_point(2.0)
    return s


def scene_color():
    s = Scene("colour: neutral greys stay neutral and even-stepped, saturated balls keep their hue in the highlight")
    s.floor(color=(0.5, 0.5, 0.5))
    for i in range(7):
        grey = [0.03, 0.06, 0.12, 0.25, 0.45, 0.7, 0.95][i]
        s.add("Cube", f"Grey {grey}", (i * 1.0 - 3, 0.4, 1.0), scale=0.8, color=(grey, grey, grey), roughness=0.9, specular=0.1)
    for i, color in enumerate(((1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 1, 1), (0, 0, 1), (1, 0, 1))):
        sphere(s, f"Hue {i}", (i * 1.2 - 3, 0, -1.0), 0.9, color=color, roughness=0.35)
    s.camera_at(90, 26, 13, (0, 0.2, 0), 28)
    s.light("Front", 90, 35, 3.0, kind=0)
    return s


def scene_world():
    s = Scene("world colour: a red world turns mirror balls red and tints the shade; no blue default sky left", world=(0.5, 0.06, 0.05))
    s.floor(color=(0.5, 0.5, 0.52))
    for i, rough in enumerate((0.02, 0.15, 0.4)):
        sphere(s, f"Metal {rough}", (i * 1.8 - 1.8, 0, 0), 1.4, metallic=1, roughness=rough, color=(0.95, 0.95, 0.95))
    s.camera_at(90, 18, 10, (0, 0.5, 0), 30)
    s.three_point(1.5)
    return s


def scene_reflect():
    s = Scene("image world: stays behind the scene; balls reflect the default sky (known limit), never black")
    noise = s._node("Noise", {})
    s.world_image = noise
    s.floor(color=(0.2, 0.2, 0.22))
    for i, rough in enumerate((0.02, 0.15, 0.4)):
        sphere(s, f"Metal {rough}", (i * 1.8 - 1.8, 0, 0), 1.4, metallic=1, roughness=rough, color=(0.95, 0.95, 0.95))
    s.camera_at(90, 18, 10, (0, 0.5, 0), 30)
    s.three_point(2.0)
    return s


SCENES = {
    "materials": scene_materials, "shadows": scene_shadows, "ao": scene_ao,
    "light_point": scene_light_point, "light_spot": scene_light_spot, "light_area": scene_light_area,
    "world": scene_world, "dof": scene_dof, "bloom": scene_bloom, "glass": scene_glass, "aliasing": scene_aliasing,
    "color": scene_color, "reflect": scene_reflect,
}


def luma(image):
    import numpy
    return numpy.asarray(image.convert("RGB"), dtype=float).dot([0.2126, 0.7152, 0.0722]) / 255.0


def measure_ao(image):
    """Row-to-row wobble of the open floor: stripes show up as a ripple in the row means."""
    import numpy
    rows = luma(image)[500:700, 40:1240].mean(axis=1)
    ripple = float(numpy.abs(numpy.diff(rows, 2)).mean())
    return {"floor_ripple": round(ripple, 5)}, ripple < 0.0012


def measure_bloom(image):
    """Bloom's reach: how far past the brightest ball the glow still lifts the black ground."""
    row = luma(image)[360]
    return {"peak": round(float(row.max()), 3), "ground": round(float(row[10]), 4)}, float(row[10]) < 0.02


def measure_materials(image):
    """The dielectric row's sparkle must fall as roughness rises. The first two balls can both
    clip at white, so the order is read from the second on."""
    l = luma(image)
    spans = [l[200:400, 265 + 125 * i:265 + 125 * (i + 1)].max() for i in range(6)]
    order = all(a >= b - 0.02 for a, b in zip(spans[1:], spans[2:]))
    return {"sparkle": [round(float(v), 2) for v in spans]}, order


MEASURES = {"ao": measure_ao, "bloom": measure_bloom, "materials": measure_materials}


def board(results, path):
    from PIL import Image, ImageDraw
    cols, w, h, strip = 3, 640, 360, 22
    rows = (len(results) + cols - 1) // cols
    out = Image.new("RGB", (cols * w, rows * (h + strip)), (18, 18, 20))
    draw = ImageDraw.Draw(out)
    for i, (name, frame) in enumerate(results):
        x, y = (i % cols) * w, (i // cols) * (h + strip)
        draw.text((x + 6, y + 5), name, fill=(235, 235, 235))
        if frame:
            out.paste(Image.open(frame).convert("RGB").resize((w - 2, h)), (x, y + strip))
    out.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenes", nargs="*", help="scene names (default: all)")
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--out", default="/tmp/render3d-lab")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    if args.list:
        for name, make in SCENES.items():
            print(f"{name:12} {make().judge}")
        return 0
    unknown = [n for n in args.scenes if n not in SCENES]
    if unknown:
        print(f"unknown scene: {unknown}; see --list")
        return 2
    from PIL import Image
    os.makedirs(SCENES_DIR, exist_ok=True)
    os.makedirs(args.out, exist_ok=True)
    binary = os.path.join(ROOT, args.binary) if not os.path.isabs(args.binary) else args.binary
    results, failed = [], False
    for name in args.scenes or SCENES:
        patch = os.path.join(SCENES_DIR, f"{name}.inf")
        SCENES[name]().write(patch)
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "patch-layout.py"), patch],
                       capture_output=True, text=True)
        out = os.path.join(args.out, name)
        run = subprocess.run([binary, "--frame", patch, "0", out + "/", "--timeout", "90"],
                             capture_output=True, text=True)
        status = json.loads(run.stdout.strip().splitlines()[-1])
        if not status.get("ok"):
            print(f"FAIL {name}: {[e['message'] for e in status['errors']]}")
            results.append((name, None))
            failed = True
            continue
        frame = status["frame_stats"][0]["file"]
        notes = [m["message"] for m in status["warnings"]]
        line = f"ok   {name:12} {status['elapsed_ms']:>6} ms"
        if name in MEASURES:
            values, passed = MEASURES[name](Image.open(frame))
            failed = failed or not passed
            line = f"{'ok  ' if passed else 'BAD '}{line[4:]}  {values}"
        print(line + (f"  {notes}" if notes else ""))
        results.append((name, frame))
    board_path = os.path.join(args.out, "board.png")
    board(results, board_path)
    print(board_path)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
