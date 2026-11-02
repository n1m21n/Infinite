#!/usr/bin/env python3
"""Blender vs Infinite A/B for the AO bake, on any glTF files.

Same mesh, reach, rays, texture size and camera on both sides, and a plain
display (Render 3D tonemap None = Blender Standard; both default to AgX).
Per case: truth.png (Blender AO per pixel, no UVs), bake.png (Blender bake,
missing when Blender crashes packing UVs), inf.png
(Infinite: Model 3D -> UV Project -> Bake -> Material emission -> Render 3D),
sheet.png and one ABRESULT JSON line. See README.md.

  python3 tools/blender-ab/ab.py --out DIR [--gen] [a.glb b.glb ...]
"""
import argparse, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
INFINITE = os.path.join(REPO, "build-dev/Infinite.app/Contents/MacOS/Infinite")


def blender(script, *a):
    r = subprocess.run([BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, script), "--", *a],
                       capture_output=True, text=True, timeout=900)
    # Blender 5.1 sometimes aborts on quit (134) after writing everything; the
    # caller checks the files it needs.
    if "Traceback" in r.stdout + r.stderr:
        sys.exit("blender failed:\n" + (r.stdout + r.stderr)[-3000:])
    return r.stdout


PATCH = """infinite-patch 1
node 1 3D Model 3D
  id model
  s path {glb}
end
node 2 3D UV Project
  id uv
end
node 3 3D Bake
  id bake
  i size {size}
  i samples {samples}
  f distance {distance}
end
node 4 3D Material
  id mat
  c color 0 0 0
  f metallic 0
  f specular 0
  c emissionColor 1 1 1
  f emission 1
end
node 5 3D Camera
  id cam
  f fov 39.6
  f distance 2.2
  f azimuth 50.2
  f elevation 17.75
end
node 6 3D Render 3D
  id r
  i tonemap None
end
node 7 Utility Output
  id out
end
geo uv 0 model
geo bake 0 uv
geo mat 0 uv
cable mat emission bake
geo r 0 mat
geo r camera cam
cable out 0 r
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("glbs", nargs="*")
    ap.add_argument("--out", required=True)
    ap.add_argument("--gen", action="store_true", help="also make the small test objects")
    ap.add_argument("--res", type=int, default=512)
    ap.add_argument("--samples", type=int, default=16)
    ap.add_argument("--distance", type=float, default=0.25)
    a = ap.parse_args()
    glbs = list(a.glbs)
    if a.gen:
        line = [l for l in blender("blender_side.py", "gen", os.path.join(a.out, "objects")).splitlines()
                if l.startswith("GEN ")][0]
        glbs += line.split()[1:]
    size = {256: 0, 512: 1, 1024: 2, 2048: 3}[a.res]
    rows = []
    for g in glbs:
        case = os.path.join(a.out, os.path.splitext(os.path.basename(g))[0])
        for mode in ("ao", "bake"):
            blender("blender_side.py", mode, os.path.abspath(g), case, str(a.res), str(a.samples), str(a.distance))
        patch = os.path.join(case, "inf.inf")
        with open(patch, "w") as f:
            f.write(PATCH.format(glb=os.path.join(case, "joined.glb"), size=size, samples=a.samples,
                                 distance=a.distance))
        r = subprocess.run([INFINITE, "--frame", patch, "0", case + "/f/", "--size", "512x512"],
                           capture_output=True, text=True, timeout=600)
        status = json.loads(r.stdout.strip().splitlines()[-1])
        if not status.get("ok"):
            sys.exit("infinite failed on %s: %s" % (g, status.get("errors")))
        os.replace(os.path.join(case, "f", "frame_00000.png"), os.path.join(case, "inf.png"))
        line = [l for l in blender("metrics.py", case).splitlines() if l.startswith("ABRESULT ")][0]
        rows.append(json.loads(line[9:]))
        print(line[9:], flush=True)
    with open(os.path.join(a.out, "results.jsonl"), "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")


if __name__ == "__main__":
    main()
