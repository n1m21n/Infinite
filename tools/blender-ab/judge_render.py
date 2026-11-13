#!/usr/bin/env python3
"""Judge-table renders that the sweep does not do (docs/plans/realtime-engine, judge_table.py).

  judge_render.py zoom  [--stage flaws]          640x480 close-ups at the final's pixel density, one per object
  judge_render.py bake                           camera-projected 4k maps of every Field material, for Cycles
  judge_render.py final                          the four columns at 1920x1440 and the sheet
  judge_render.py orbit                          30 live frames over a 5 s orbit, a contact sheet and its numbers

Infinite renders reuse the meshes of a swept judge_table case (~/.cache/infinite-sweep/ab/judge_table/obj_*.glb):
sweep the case once first. INFINITE_BIN picks the binary, as in the sweep.
"""
import argparse
import copy
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness
import judge_table as JT

CASE = os.path.expanduser("~/.cache/infinite-sweep/ab/judge_table")
OUT = os.path.expanduser("~/.cache/infinite-sweep/judge")
ENGINES = {"raster": None, "pt": None, "live": "7", "settle": "15"}


def infinite(scene, engine, name, spp=64, edit=None, frame_t=None):
    """Render `scene` with one engine into OUT/<name>.png; returns seconds. edit(patch text) -> patch text."""
    os.makedirs(OUT, exist_ok=True)
    patch = harness.to_patch(scene, CASE, engine=engine, pt_samples=spp)
    if edit:
        patch = edit(patch)
    pf = os.path.join(OUT, name + ".inf")
    open(pf, "w").write(patch)
    fd = os.path.join(OUT, name + "_f")
    os.makedirs(fd, exist_ok=True)
    for f in os.listdir(fd):
        os.remove(os.path.join(fd, f))
    env = dict(os.environ)
    env.pop("INFINITE_LIVE_ENGINE", None)
    if ENGINES[engine]:
        env["INFINITE_LIVE_ENGINE"] = ENGINES[engine]
    w, h = scene["size"]
    t0 = time.time()
    r = subprocess.run([harness.INFINITE, "--frame", pf, "0", fd + "/", "--size", "%dx%d" % (w, h)],
                       capture_output=True, text=True, timeout=1200, env=env)
    dt = time.time() - t0
    pngs = sorted(f for f in os.listdir(fd) if f.endswith(".png"))
    if not pngs:
        sys.exit("infinite failed (%s):\n%s" % (name, (r.stdout + r.stderr)[-1500:]))
    os.replace(os.path.join(fd, pngs[-1]), os.path.join(OUT, name + ".png"))
    return dt


ZOOMS = {"mug": ([-0.075, 0.02, 0.07], 0.87), "glass": ([0.105, 0.07, 0.075], 0.93), "spoon": ([0.06, -0.14, 0.005], 0.80),
         "table": ([0.0, -0.20, 0.0], 0.74), "cloth": ([-0.20, -0.10, 0.005], 0.78)}


def zoom(a):
    from PIL import Image
    tiles = []
    for k, (target, focus) in ZOOMS.items():
        if a.only and k not in a.only:
            continue
        sc = JT.judge_table(a.stage)
        base = sc["camera"]
        # Same eye, turned to the object, the angle narrowed 3x: 640 px here covers what 1920 px does in the final.
        import math
        e, az, d = math.radians(base["elevation"]), math.radians(base["azimuth"]), base["distance"]
        eye = [base["target"][0] + d * math.cos(e) * math.cos(az), base["target"][1] - d * math.cos(e) * math.sin(az),
               base["target"][2] + d * math.sin(e)]
        v = [eye[i] - target[i] for i in range(3)]
        dist = math.sqrt(sum(c * c for c in v))
        sc["camera"] = dict(base, target=target, distance=dist, focus=dist, fov=2 * math.degrees(math.atan(math.tan(math.radians(base["fov"] / 2)) / 3)),
                            elevation=math.degrees(math.asin(v[2] / dist)), azimuth=math.degrees(math.atan2(-v[1], v[0])))
        sc["camera"].pop("fstop", None)
        dt = infinite(sc, "settle", "zoom_" + k, 64)
        print("zoom %-6s %.1f s" % (k, dt))
        tiles.append(Image.open(os.path.join(OUT, "zoom_%s.png" % k)).convert("RGB"))
    sheet = Image.new("RGB", (640 * min(len(tiles), 3), 480 * ((len(tiles) + 2) // 3)))
    for i, t in enumerate(tiles):
        sheet.paste(t, (640 * (i % 3), 480 * (i // 3)))
    sheet.save(os.path.join(OUT, "zoom_%s.jpg" % a.stage), quality=90)
    print(os.path.join(OUT, "zoom_%s.jpg" % a.stage))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("cmd", choices=["zoom", "bake", "final", "orbit"])
    p.add_argument("--stage", default="flaws")
    p.add_argument("--only", nargs="*")
    a = p.parse_args()
    {"zoom": zoom, "bake": lambda a: bake(a), "final": lambda a: final(a), "orbit": lambda a: orbit(a)}[a.cmd](a)


if __name__ == "__main__":
    main()
