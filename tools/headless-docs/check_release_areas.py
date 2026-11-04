#!/usr/bin/env python3
"""Headless smoke test for every 3D / MIDI area added since v1.0.0.

Each case builds a tiny patch, then runs the built binary headless:
  validate (exit 0) -> explain (geometry / settings visible) -> frame (exit 0, picture not empty)
  -> canonicalize (the numeric form the app saves) -> explain again: every setting the case sets
  must still be listed with the same value, so it survives save and load.

    python3 tools/headless-docs/check_release_areas.py [--bin path/to/Infinite] [--only name,name] [--keep dir]

Exit 0 when every case passes. A case with `refused` expects exit code 4 (hardware source) from a render.
Areas the CLI cannot reach are listed in GAPS at the bottom, printed at the end of every run.
"""
import argparse, json, os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_BIN = os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite")
GLB = os.path.join(ROOT, "tests/render3d/skinned_two_bone.glb")

# ---- patch builder ---------------------------------------------------------------------------

def node(i, cat, typ, **kw):
    """Values are plain Python (bool, int, float, str). The tag letter is filled in from `--describe` at run
    time (`?` below), because the engine insists on the tag a control really has."""
    lines = [f"node @{i} {cat} {typ}", f"  id {i}"]
    for k, v in kw.items():
        lines.append(f"  ? {k} {int(v) if isinstance(v, bool) else v}")
    lines.append("end")
    return "\n".join(lines)


_TAGS = {}


def resolve_tags(binary, text):
    """Replace each `?` with the control's real tag (f, i, b, c, s) as `--describe <type> --json` reports it."""
    out, typ = [], None
    for line in text.split("\n"):
        m = re.match(r"node \d+ \S+ (.+)$", line)
        if m:
            typ = m.group(1)
            if typ not in _TAGS:
                r = subprocess.run([binary, "--describe", typ], capture_output=True, text=True)
                j = last_json(r.stdout)
                rows = (j.get("types") or [{}])[0].get("params", [])
                _TAGS[typ] = {p["key"]: p["tag"] for p in rows}
        m = re.match(r"  \? (\w+) (.*)$", line)
        if m:
            tag = _TAGS.get(typ, {}).get(m.group(1))
            if tag is None:
                raise SystemExit(f"{typ} has no control '{m.group(1)}' (describe it and fix the case)")
            line = f"  {tag} {m.group(1)} {m.group(2)}"
        out.append(line)
    return "\n".join(out)


def scene(geo_src, render=None, light=None, camera=None):
    """Camera + light + Render 3D + Output around a geometry node called `geo_src`."""
    n = [node("cam", "3D", "Camera", **(camera or {"distance": 3.0})),
         node("lit", "3D", "Light", **(light or {})),
         node("r3", "3D", "Render 3D", **{"width": 256, "height": 256, **(render or {})}),
         node("out", "Utility", "Output")]
    w = [f"geo r3 0 {geo_src}", "geo r3 4 cam", "geo r3 5 lit", "cable out 0 r3"]
    return n, w


def patch(nodes, wires):
    """Node indices must be integers; `node @name` becomes the next number and the `id` keeps the name."""
    text = "\n".join(nodes)
    for k, name in enumerate(re.findall(r"^node @(\S+)", text, re.M), 1):
        text = text.replace(f"node @{name} ", f"node {k} ", 1)
    return "infinite-patch 1\n" + text + "\n" + "\n".join(wires) + "\n"


CASES = []


def case(name, nodes, wires, t=0.5, expect=(), settings=None, refused=False, nonblack=True, size="256x256", note=""):
    """settings: {nodeId: {key: expectedTextInExplain}} that must survive canonicalize."""
    CASES.append(dict(name=name, text=patch(nodes, wires), t=t, expect=expect, settings=settings or {},
                      refused=refused, nonblack=nonblack, size=size, note=note))


# ---- cases -----------------------------------------------------------------------------------

n, w = scene("em")
case("edit_mesh_extrude_inset",
     [node("cube", "3D", "Cube"),
      node("em", "3D", "Edit Mesh", ops="sel all\\nextrude 0.3\\ninset 0.05")] + n,
     ["geo em 0 cube"] + w,
     expect=[r'Edit Mesh "em".*\n\s+> geometry: (\d+) vertices'],
     settings={"em": {"ops": "sel all"}})

n, w = scene("em")
# The top picked as two coarse triangles with their footprints (`t`): on any Cube detail it is the whole top face.
case("edit_mesh_footprint_top",
     [node("cube", "3D", "Cube"),
      node("em", "3D", "Edit Mesh",
           ops="sel f 0.1667 0.5 0.1667 0 1 0 -0.1667 0.5 -0.1667 0 1 0 t -0.5 0.5 0.5 0.5 0.5 0.5 0.5 0.5 -0.5 "
               "-0.5 0.5 0.5 0.5 0.5 -0.5 -0.5 0.5 -0.5\\nextrude 0.3")] + n,
     ["geo em 0 cube"] + w,
     expect=[r'Edit Mesh "em".*\n\s+> geometry: (\d+) vertices'],
     settings={"em": {"ops": "sel f"}})

n, w = scene("em")
case("edit_mesh_delete_face",
     [node("cube", "3D", "Cube"),
      node("em", "3D", "Edit Mesh", ops="sel f 0 0 0.5 0 0 1\\ndelete")] + n,
     ["geo em 0 cube"] + w, settings={"em": {"ops": "delete"}})

n, w = scene("sc")
case("sculpt",
     [node("sph", "3D", "Cube", shape="Sphere"),
      node("sc", "3D", "Sculpt", amount=0.6, level=1)] + n,
     ["geo sc 0 sph"] + w, settings={"sc": {"amount": "0.6", "level": "1"}})

for mode in ("AO", "Curvature", "Position", "Normal", "Thickness", "Sculpt"):
    case(f"bake_{mode.lower()}",
         [node("sph", "3D", "Cube", shape="Torus"),
          node("bk", "3D", "Bake", mode=mode, size=0, samples=4),
          node("out", "Utility", "Output")],
         ["geo bk 0 sph", "cable out 0 bk"],
         settings={"bk": ({"mode": mode} if mode != "AO" else {}) | {"samples": "4"}}, nonblack=(mode != "Sculpt"),
         note="Sculpt mode bakes the node's own strokes; an unsculpted mesh bakes flat" if mode == "Sculpt" else "")

n, w = scene("ar")
case("armature",
     [node("cyl", "3D", "Cube", shape="Cylinder"),
      node("ar", "3D", "Armature", bones=6, curl=40.0, waveAmount=0.3, envelope=1.2)] + n,
     ["geo ar 0 cyl"] + w, t=0.7, settings={"ar": {"bones": "6", "curl": "40", "waveAmount": "0.3"}})

n, w = scene("uv")
case("uv_project",
     [node("cube", "3D", "Cube"),
      node("uv", "3D", "UV Project", uvMode="Cylinder", uvScale=2.0, uvMargin=0.05)] + n,
     ["geo uv 0 cube"] + w, settings={"uv": {"uvScale": "2"}})

n, w = scene("m3")
case("model3d_skinned_gltf",
     [node("m3", "3D", "Model 3D", path=GLB, clip=0, pose0=1, pose0X=20.0)] + n, w,
     t=0.6, settings={"m3": {"path": "skinned_two_bone.glb"}})

n, w = scene("mat")
case("material_maps",
     [node("cube", "3D", "Cube"),
      node("nz", "Source", "Noise"),
      node("mat", "3D", "Material", normalBump=1, normalStrength=1.5, wrapMode=1, textureAlpha=1, roughness=0.3)] + n,
     ["geo mat 0 cube", "cable mat 1 nz", "cable mat 2 nz", "cable mat 4 nz", "cable mat 5 nz"] + w,
     settings={"mat": {"normalStrength": "1.5", "roughness": "0.3"}})

n, w = scene("cube")
case("render3d_raster_passes",
     [node("cube", "3D", "Cube")] + [n[0], n[1], node("r3", "3D", "Render 3D", width=256, height=256, ao=1, aoRadius=0.4,
                                                    cavity=1, renderPass="Normals", engine="Raster"), n[3]], w,
     settings={"r3": {"renderPass": "Normals", "aoRadius": "0.4"}})

n, w = scene("cube")
case("render3d_pathtrace",
     [node("cube", "3D", "Cube")] + [n[0], n[1], node("r3", "3D", "Render 3D", width=128, height=128, engine="Path trace",
                                                    ptSamples=8, ptBounces=3), n[3]], w,
     settings={"r3": {"engine": "Path trace", "ptSamples": "8", "ptBounces": "3"}}, size="128x128")

n, w = scene("cl")
case("cloth_collider_friction",
     [node("pl", "3D", "Cube", shape="Plane", posY=0.8), node("ball", "3D", "Cube", shape="Sphere", scale=0.6),
      node("cl", "3D", "Cloth", colliderFriction=0.8, colliderDistance=0.03, friction=0.7, pinMode="Corners")] + n,
     ["geo cl 0 pl", "geo cl 1 ball"] + w, t=1.0,
     settings={"cl": {"colliderFriction": "0.8", "colliderDistance": "0.03", "friction": "0.7"}})

n, w = scene("rb")
case("rigid_bodies",
     [node("cube", "3D", "Cube", posY=1.0), node("rb", "3D", "Rigid Bodies", collider="Sphere", bounce=0.5, friction=0.8,
                                                  wallsEnabled=True, wallSize=3.0)] + n,
     ["geo rb 0 cube"] + w, t=1.0, settings={"rb": {"collider": "Sphere", "bounce": "0.5", "wallSize": "3"}})

case("light_hdri_camera",
     [node("cube", "3D", "Cube"),
      node("cam", "3D", "Camera", distance=3.0, shiftX=0.1, sensorFit=1, showLimits=True),
      node("lit", "3D", "Light", type="Area", areaWidth=2.0, areaHeight=1.5, temperature=4000.0, useTemperature=True,
           shadowRadius=0.4, useCutoff=True, cutoffDistance=20.0),
      node("hd", "3D", "HDRI", intensity=0.8, rotation=45.0, sunThreshold=0.5),
      node("r3", "3D", "Render 3D", width=256, height=256),
      node("out", "Utility", "Output")],
     ["geo r3 0 cube", "geo r3 4 cam", "geo r3 5 lit", "cable r3 8 hd", "cable out 0 r3"],
     settings={"cam": {"shiftX": "0.1", "sensorFit": "1"}, "lit": {"areaWidth": "2", "temperature": "4000", "cutoffDistance": "20"},
               "hd": {"intensity": "0.8", "rotation": "45"}})

case("view3d_passes",
     [node("cube", "3D", "Cube"), node("v3", "3D", "3D View", width=256, height=256, renderPass=3, ao=1,
                                         bloom=1, tonemap=4),
      node("out", "Utility", "Output")],
     ["cable out 0 v3"], settings={"v3": {"renderPass": "3", "tonemap": "4"}},
     nonblack=False, note="3D View draws its own scene items; a bare node renders the empty stage")

case("midi_notes_device_gate",
     [node("mn", "Notes", "MIDI Notes", device="Test Device", channel=3, transpose=2),
      node("nz", "Source", "Noise"), node("out", "Utility", "Output")],
     ["cable out 0 nz"], refused=True, nonblack=False, settings={"mn": {"device": "Test Device", "channel": "3", "transpose": "2"}},
     note="a MIDI source is refused headless (exit 4); the per-device setting still validates and round-trips")


# ---- runner ----------------------------------------------------------------------------------

def run(binary, args, timeout=180):
    p = subprocess.run([binary] + args, capture_output=True, text=True, timeout=timeout)
    return p.returncode, p.stdout, p.stderr


def last_json(out):
    for line in reversed(out.strip().splitlines()):
        if line.startswith("{"):
            try:
                return json.loads(line)
            except ValueError:
                pass
    return {}


def picture_ok(path):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(path).convert("RGB"), dtype=float)
    return a.mean() > 1.0 and a.std() > 1.0, f"mean {a.mean():.1f} std {a.std():.1f}"


def settings_ok(explain_text, wanted):
    """Every wanted key/value must appear under its node block in the explain text."""
    bad = []
    for nid, kv in wanted.items():
        m = re.search(r'^  \w[^\n]*"%s" \(\d+\)\n((?:    [^\n]*\n)*)' % re.escape(nid), explain_text, re.M)
        block = m.group(1) if m else ""
        for k, v in kv.items():
            if not re.search(r'^    %s\b[^\n]*%s' % (re.escape(k), re.escape(v)), block, re.M):
                bad.append(f"{nid}.{k}={v}")
    return bad


def surface(binary, work):
    """Whole-release CLI surface: describe (all types, JSON, per type), video render, frame sequence, audio summary."""
    rows = []

    def check(name, ok, detail=""):
        rows.append((name, ok, detail))

    rc, out, _ = run(binary, ["--version"])
    check("--version", rc == 0, out.strip().splitlines()[-1] if out.strip() else "")
    allj = os.path.join(work, "describe_all.json")
    rc, _, _ = run(binary, ["--describe", "--json", allj], timeout=300)
    n = 0
    if rc == 0 and os.path.exists(allj):
        j = json.load(open(allj))
        n = len(j.get("types", []))
    check("--describe --json (every type)", rc == 0 and n > 300, f"{n} types")
    for t in ("Edit Mesh", "Sculpt", "Bake", "Armature", "UV Project", "Model 3D", "Cloth", "Rigid Bodies"):
        rc, out, _ = run(binary, ["--describe", t])
        j = last_json(out)
        check(f"--describe \"{t}\"", rc == 0 and bool(j.get("types")), f"{len((j.get('types') or [{}])[0].get('params', []))} controls")
    src = os.path.join(work, "video.inf")
    open(src, "w").write(resolve_tags(binary, patch(*(lambda n, w: ([node("cube", "3D", "Cube", spin=30.0)] + n, w))(*scene("cube")))))
    mp4 = os.path.join(work, "video.mp4")
    rc, out, _ = run(binary, ["--render", src, mp4, "--duration", "1", "--fps", "12", "--no-audio"], timeout=300)
    check("--render video (h264)", rc == 0 and os.path.exists(mp4) and os.path.getsize(mp4) > 2000, f"{os.path.getsize(mp4) if os.path.exists(mp4) else 0} bytes")
    seq = os.path.join(work, "seq")
    rc, out, _ = run(binary, ["--frames-dir", src, seq, "--duration", "0.5", "--fps", "8", "--size", "128x128"], timeout=300)
    pngs = [f for f in os.listdir(seq)] if os.path.isdir(seq) else []
    check("--frames-dir", rc == 0 and len(pngs) >= 4, f"{len(pngs)} frames")
    csheet = os.path.join(work, "sheet.png")
    rc, out, _ = run(binary, ["--frame", src, "0,0.5", os.path.join(work, "stills") + "/", "--contact-sheet", csheet, "--size", "128x128"])
    check("--frame several + --contact-sheet", rc == 0 and os.path.exists(csheet))
    aud = os.path.join(work, "audio.inf")
    open(aud, "w").write(open(os.path.join(ROOT, "tests/headless/format/audio.inf")).read())
    rc, out, _ = run(binary, ["--audio-summary", aud, os.path.join(work, "a.json"), "--duration", "1"])
    check("--audio-summary", rc == 0)
    print("CLI surface")
    bad = 0
    for name, ok, d in rows:
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {name:40} {d}")
    print()
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=DEFAULT_BIN)
    ap.add_argument("--only", default="")
    ap.add_argument("--keep", default="")
    a = ap.parse_args()
    only = set(filter(None, a.only.split(",")))
    work = a.keep or tempfile.mkdtemp(prefix="inf-areas-")
    os.makedirs(work, exist_ok=True)
    fails = 0 if (only and "surface" not in only) else surface(a.bin, work)
    print(f"{'case':28} {'validate':8} {'frame':8} {'picture':22} {'roundtrip':9}")
    for c in CASES:
        if only and c["name"] not in only:
            continue
        src = os.path.join(work, c["name"] + ".inf")
        open(src, "w").write(resolve_tags(a.bin, c["text"]))
        res = {}
        rc, out, _ = run(a.bin, ["--validate", src])
        res["validate"] = rc == 0
        rc, out, _ = run(a.bin, ["--explain", src])
        explain = out
        pic = "-"
        if c["refused"]:
            rc, out, _ = run(a.bin, ["--render", src, os.path.join(work, c["name"] + ".mp4"), "--duration", "1"])
            res["frame"] = rc == 4
            pic = "refused (exit 4)" if rc == 4 else f"exit {rc}"
        else:
            png = os.path.join(work, c["name"] + ".png")
            rc, out, err = run(a.bin, ["--frame", src, str(c["t"]), png, "--size", c["size"]])
            res["frame"] = rc == 0 and os.path.exists(png)
            if res["frame"]:
                ok, pic = picture_ok(png)
                if c["nonblack"] and not ok:
                    res["frame"] = False
            else:
                pic = f"exit {rc}"
        can = os.path.join(work, c["name"] + ".canon.inf")
        run(a.bin, ["--canonicalize", src, can, "--keep-ids"])
        bad = []
        if os.path.exists(can):
            _, cexp, _ = run(a.bin, ["--explain", can])
            bad = settings_ok(cexp, c["settings"])
            rc2, _, _ = run(a.bin, ["--validate", can])
            res["roundtrip"] = not bad and rc2 == 0
        else:
            res["roundtrip"] = False
        for pat in c["expect"]:
            if not re.search(pat, explain):
                res["frame"] = False
                pic += " (explain mismatch)"
        row_ok = all(res.values())
        fails += not row_ok
        mark = lambda v: "ok" if v else "FAIL"
        print(f"{c['name']:28} {mark(res['validate']):8} {mark(res['frame']):8} {pic:22} {mark(res['roundtrip']):9}"
              + (f"  missing after reload: {bad}" if bad else "") + (f"  [{c['note']}]" if c["note"] and row_ok else ""))
    print("\nGAPS (GUI only, not reachable from the CLI):")
    for g in GAPS:
        print("  -", g)
    print(f"\n{'ALL PASS' if not fails else str(fails) + ' FAILED'}   (work dir: {work})")
    return 1 if fails else 0


GAPS = [
    "Edit Mode selection by mouse/box/lasso (the ops string replays what the 3D View recorded; authoring it by hand needs the selector text)",
    "Sculpt strokes: the node's `data` blob is written by the 3D View brush; the CLI can only replay saved strokes",
    "3D View scene items, gizmos, overlays and viewport navigation (interactive; the 3D View node renders its stage, not the editor)",
    "MIDI Notes device gate: devices need hardware, so a render is refused (exit 4); only validate and save/load are testable",
    "CLI setup / Terminal panel settings: GUI preferences of the running app (INFINITE_CLISETUPTEST covers the launcher check)",
]

if __name__ == "__main__":
    sys.exit(main())
