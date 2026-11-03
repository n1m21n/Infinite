#!/usr/bin/env python3
"""Fetch the hero case's CC0 assets from Poly Haven and pack each model's gltf + bin + jpgs into one glb.
  python3 tools/blender-ab/assets/fetch.py        (skips what is already here; licences in SOURCES.md)
"""
import json, os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MODELS = ["Ottoman_01", "ClassicNightstand_01"]
HDRI = "kloofendal_43d_clear_puresky"


def curl(url, out=None):
    # curl, not urllib: Poly Haven answers Python's default user agent with 403
    r = subprocess.run(["curl", "-sfL", "-m", "120", url] + (["-o", out] if out else []), capture_output=True)
    if r.returncode:
        sys.exit("download failed: " + url)
    return r.stdout


def pack_glb(gltf_path, out):
    g = json.load(open(gltf_path))
    base = os.path.dirname(gltf_path)
    blob = bytearray()

    def add(data):
        while len(blob) % 4:
            blob.append(0)
        off = len(blob)
        blob.extend(data)
        return off, len(data)

    views = g.setdefault("bufferViews", [])
    # the original buffer keeps its views; its bytes go first
    off0, _ = add(open(os.path.join(base, g["buffers"][0]["uri"]), "rb").read())
    for v in views:
        v["byteOffset"] = v.get("byteOffset", 0) + off0
        v["buffer"] = 0
    for im in g.get("images", []):
        off, n = add(open(os.path.join(base, im.pop("uri")), "rb").read())
        views.append({"buffer": 0, "byteOffset": off, "byteLength": n})
        im["bufferView"] = len(views) - 1
        im["mimeType"] = "image/jpeg"
    g["buffers"] = [{"byteLength": len(blob)}]
    js = json.dumps(g, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    blob.extend(b"\0" * (-len(blob) % 4))
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(out, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(js), b"JSON") + js)
        f.write(struct.pack("<I4s", len(blob), b"BIN\0") + bytes(blob))


for name in MODELS:
    out = os.path.join(HERE, name + ".glb")
    if os.path.exists(out):
        continue
    info = json.loads(curl("https://api.polyhaven.com/files/" + name))["gltf"]["1k"]["gltf"]
    tmp = os.path.join(HERE, "_" + name)
    os.makedirs(os.path.join(tmp, "textures"), exist_ok=True)
    curl(info["url"], os.path.join(tmp, name + "_1k.gltf"))
    for rel, f in info["include"].items():
        curl(f["url"], os.path.join(tmp, rel))
    pack_glb(os.path.join(tmp, name + "_1k.gltf"), out)
    subprocess.run(["rm", "-rf", tmp])
    print("wrote", out)

hdr = os.path.join(HERE, HDRI + "_1k.hdr")
if not os.path.exists(hdr):
    curl(json.loads(curl("https://api.polyhaven.com/files/" + HDRI))["hdri"]["1k"]["hdr"]["url"], hdr)
    print("wrote", hdr)
