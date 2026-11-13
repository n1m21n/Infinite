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
BOUNCES = int(os.environ.get("JUDGE_BOUNCES", "8"))
ENGINES = {"raster": None, "pt": None, "live": "7", "settle": "15"}


def infinite(scene, engine, name, spp=64, edit=None, case=None):
    """Render `scene` with one engine into OUT/<name>.png; returns seconds. edit(patch text) -> patch text."""
    os.makedirs(OUT, exist_ok=True)
    patch = harness.to_patch(scene, case or CASE, engine=engine, pt_samples=spp, pt_bounces=BOUNCES)
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


def _fill(path, out, grow=24):
    """Emission render -> linear map: decode sRGB and the 0.02 floor, drop the antialiased rim, then grow the object's
    values `grow` px past its silhouette so edge samples never read the empty frame. Returns the object's cover."""
    import numpy as np
    from PIL import Image
    c = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    ok = lin.max(axis=2) > 0.012
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (0, 2), (0, -2), (2, 0), (-2, 0)):
        ok &= np.roll(lin.max(axis=2) > 0.012, (dy, dx), (0, 1))
    cover = float(ok.mean())
    rgb = np.clip((lin - 0.02) / 0.98, 0.0, 1.0)
    for _ in range(grow):
        acc, n = np.zeros_like(rgb), np.zeros(ok.shape, np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            acc += np.roll(rgb * ok[..., None], (dy, dx), (0, 1))
            n += np.roll(ok, (dy, dx), (0, 1))
        new = (~ok) & (n > 0)
        rgb[new] = acc[new] / n[new][:, None]
        ok = ok | new
    Image.fromarray(np.clip(rgb * 255.0 + 0.5, 0, 255).astype(np.uint8)).save(out)
    return cover


def bake(a):
    """Each Field material alone through the final camera (no depth of field) at 3840x2880, unlit, emitting its colour,
    then again emitting roughness / metallic / height (Render 3D's albedo pass does not carry a Field material).
    Cycles reads the maps back by window coordinates (blender_scene.py, material key `proj`). Surfaces the camera
    cannot see take the texel in front of them."""
    import judge_table_fields as F
    os.makedirs(JT.PROJ, exist_ok=True)
    for k, (idx, _) in JT.BAKED.items():
        sc = JT.judge_table("flaws", size=(3840, 2880), proj=False)
        sc["camera"].pop("fstop", None)
        sc["supersample"] = 1
        sc.pop("view", None)
        sc["world"], sc["lights"] = {}, []
        o = sc["objects"][idx]
        cd = os.path.join(OUT, "bake_" + k)
        os.makedirs(cd, exist_ok=True)
        link = os.path.join(cd, "obj_0.glb")
        if os.path.lexists(link):
            os.remove(link)
        os.symlink(os.path.join(CASE, "obj_%d.glb" % idx), link)
        for kind, tail in (("albedo", F.TAIL_A), ("rmh", F.TAIL_RMH)):
            m = {"roughness": 0.5, "code": o["material"]["code"].replace(F.TAIL, tail)}
            ly = o["material"].get("layer")
            if ly:
                m["layer"] = dict(ly, code=ly["code"].replace(F.TAIL, tail))
            dt = infinite(dict(sc, objects=[dict(o, material=m)]), "raster", "bake_%s_%s" % (k, kind), case=cd)
            cover = _fill(os.path.join(OUT, "bake_%s_%s.png" % (k, kind)), os.path.join(JT.PROJ, "%s_%s.png" % (k, kind)))
            print("bake %-6s %-6s %.1f s, covers %.1f %% of the frame" % (k, kind, dt, cover * 100), flush=True)


COLUMNS = [("truth", "Cycles 1024 spp"), ("pt", "GL Path trace 256 spp"), ("settle", "settle 256 spp"), ("live", "live, one frame")]


def final(a):
    import json
    from PIL import Image, ImageDraw, ImageFont
    d = os.path.join(OUT, "final")
    os.makedirs(d, exist_ok=True)
    w, h = a.size
    scene = JT.judge_table("flaws", size=(w, h), samples=1024)
    times, res = {}, {}
    if not (a.keep_truth and os.path.exists(os.path.join(d, "truth.png"))):
        open(os.path.join(d, "scene.json"), "w").write(json.dumps(scene, sort_keys=True, indent=1))
        t0 = time.time()
        _, log = harness.run([harness.BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, "blender_scene.py"), "--", d])
        if "SCENEDONE" not in log:
            sys.exit("blender failed:\n" + log[-3000:])
        times["truth"] = time.time() - t0
        json.dump(times, open(os.path.join(d, "times.json"), "w"))
        print("final truth  %.1f s" % times["truth"], flush=True)
    else:
        times = json.load(open(os.path.join(d, "times.json")))
    for eng, spp in (("pt", 256), ("settle", 256), ("live", 1)):
        times[eng] = infinite(scene, eng, "final_" + eng, spp, case=d)
        os.replace(os.path.join(OUT, "final_%s.png" % eng), os.path.join(d, "inf_%s.png" % eng))
        res[eng] = harness.score(d, "judge_table", scene, "_" + eng)
        print("final %-6s %.1f s  FLIP %.4f  brightness %.3f" % (eng, times[eng], res[eng]["flip"], res[eng]["brightness_ratio"]), flush=True)
    json.dump({"times": times, "scores": res, "size": [w, h]}, open(os.path.join(d, "result.json"), "w"), indent=1)
    cw, ch, bar = 1280, 1280 * h // w, 64
    sheet = Image.new("RGB", (cw * 4, ch + bar), (18, 18, 18))
    dr = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 30)
    except OSError:
        font = ImageFont.load_default()
    for i, (k, label) in enumerate(COLUMNS):
        f = os.path.join(d, "truth.png" if k == "truth" else "inf_%s.png" % k)
        sheet.paste(Image.open(f).convert("RGB").resize((cw, ch), Image.LANCZOS), (cw * i, 0))
        note = "%s   %.0f s" % (label, times[k]) + ("   FLIP %.3f" % res[k]["flip"] if k in res else "")
        dr.text((cw * i + 16, ch + 14), note, fill=(230, 230, 230), font=font)
    sheets = os.path.join(harness.REPO, "docs", "plans", "realtime-engine", "sheets")
    sheet.save(os.path.join(sheets, "judge-table.jpg"), quality=88)
    Image.open(os.path.join(d, "inf_settle.png")).convert("RGB").save(os.path.join(sheets, "judge-table-settle.jpg"), quality=92)
    print(os.path.join(sheets, "judge-table.jpg"))


def orbit(a):
    """30 live frames over a 90 degree arc around the final view (5 s at 6 samples a second), each its own one-frame
    render: a contact sheet, the seconds per frame and the largest brightness step between neighbours."""
    import numpy as np
    from PIL import Image
    w, h = a.size
    tiles, lum, secs = [], [], []
    for i in range(30):
        sc = JT.judge_table("flaws", size=(w, h))
        sc["camera"]["azimuth"] += -45.0 + 90.0 * i / 29.0
        sc["camera"].pop("fstop", None)
        secs.append(infinite(sc, "live", "orbit_%02d" % i, 1))
        im = Image.open(os.path.join(OUT, "orbit_%02d.png" % i)).convert("RGB")
        lum.append(float(np.asarray(im, dtype=np.float32).mean() / 255.0))
        tiles.append(im.resize((480, 480 * h // w), Image.LANCZOS))
        print("orbit %02d  %.1f s  mean %.3f" % (i, secs[-1], lum[-1]), flush=True)
    th = tiles[0].height
    sheet = Image.new("RGB", (480 * 6, th * 5))
    for i, t in enumerate(tiles):
        sheet.paste(t, (480 * (i % 6), th * (i // 6)))
    out = os.path.join(harness.REPO, "docs", "plans", "realtime-engine", "sheets", "judge-table-orbit.jpg")
    sheet.save(out, quality=85)
    step = max(abs(lum[i + 1] - lum[i]) for i in range(29))
    print("ORBIT 30 frames, %.1f s mean per frame, largest brightness step %.4f\n%s" % (sum(secs) / 30, step, out))


def probe(a):
    """One settle render of judge_table with a change, for picture questions that need no sheet:
    --lift M raises the glass and the water by M metres (is the glass base light because it touches the table?),
    --distance M moves the camera back along its line and refocuses (does a wide shot step over thin layers?).
    Writes OUT/probe_<tag>.png and prints FLIP against OUT/probe_<ref>.png when --ref is given."""
    import json
    w, h = a.size
    sc = JT.judge_table("flaws", size=(w, h), samples=16)
    key = "lift%g" % a.lift
    if a.lift:
        for i in (5, 6):   # the glass and the water (judge_table's object order)
            sc["objects"][i]["verts"] = [[v[0], v[1], v[2] + a.lift] for v in sc["objects"][i]["verts"]]
    if a.distance:
        sc["camera"]["focus"] += a.distance - sc["camera"]["distance"]
        sc["camera"]["distance"] = a.distance
    # Moved meshes need their own glbs, which Blender writes on a quick 16 spp render of the case.
    case = CASE if not a.lift else os.path.join(OUT, "probe_case_" + key)
    if not os.path.exists(os.path.join(case, "obj_5.glb")):
        os.makedirs(case, exist_ok=True)
        open(os.path.join(case, "scene.json"), "w").write(json.dumps(sc))
        _, log = harness.run([harness.BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, "blender_scene.py"), "--", case])
        if "SCENEDONE" not in log:
            sys.exit("blender failed:\n" + log[-3000:])
    dt = infinite(sc, "settle", "probe_" + a.tag, a.spp, case=case)
    out = os.path.join(OUT, "probe_%s.png" % a.tag)
    msg = "probe %s  %.1f s  %s" % (a.tag, dt, out)
    if a.ref:
        import flip_evaluator as F
        _, mean, _ = F.evaluate(harness.load(os.path.join(OUT, "probe_%s.png" % a.ref)), harness.load(out), "LDR")
        msg += "  FLIP vs %s %.4f" % (a.ref, mean)
    print(msg, flush=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("cmd", choices=["zoom", "bake", "final", "orbit", "probe"])
    p.add_argument("--stage", default="flaws")
    p.add_argument("--only", nargs="*")
    p.add_argument("--size", nargs=2, type=int, default=[1920, 1440])
    p.add_argument("--keep-truth", action="store_true")
    p.add_argument("--tag", default="now")
    p.add_argument("--ref")
    p.add_argument("--spp", type=int, default=256)
    p.add_argument("--lift", type=float, default=0.0)
    p.add_argument("--distance", type=float, default=0.0)
    a = p.parse_args()
    {"zoom": zoom, "bake": bake, "final": final, "orbit": orbit, "probe": probe}[a.cmd](a)


if __name__ == "__main__":
    main()
