#!/usr/bin/env python3
"""Fetch the bed-scene fabric scans (CC0, Poly Haven, 2k jpg) into assets/bed/ (git-ignored; re-run to restore).
  python3 tools/blender-ab/assets/fetch_bed.py
"""
import json, os, subprocess, sys

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bed")
TEXTURES = {"cotton_jersey": "sheet", "curly_teddy_natural": "plush"}
MAPS = {"Diffuse": "albedo", "nor_gl": "normal", "Rough": "roughness", "Displacement": "height"}
RES = "2k"


def curl(url, out=None):
    # curl, not urllib: Poly Haven answers Python's default user agent with 403
    r = subprocess.run(["curl", "-sfL", "-m", "180", url] + (["-o", out] if out else []), capture_output=True)
    if r.returncode:
        sys.exit("download failed: " + url)
    return r.stdout


os.makedirs(HERE, exist_ok=True)
for tid, role in TEXTURES.items():
    files = json.loads(curl("https://api.polyhaven.com/files/" + tid))
    for key, name in MAPS.items():
        out = os.path.join(HERE, "%s_%s.jpg" % (role, name))
        if not os.path.exists(out):
            curl(files[key][RES]["jpg"]["url"], out)
        print(out, os.path.getsize(out))
