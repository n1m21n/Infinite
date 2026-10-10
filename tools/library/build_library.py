#!/usr/bin/env python3
"""Build the Infinite Library: .field device files, preview images and index.json for the website.

    python3 tools/library/build_library.py            # all steps
    python3 tools/library/build_library.py --no-preview   # files + manifest only (fast)

Steps per entry (library_defs.ENTRIES):
  1. compile the Field source with build/field-preset-check (the same Lex/Parse/IR/GLSL path the app runs)
  2. write website/assets/library/pixel/<id>.field (the app's own DeviceFile JSON)
  3. render a preview with `Infinite --frame` (filters get a procedural source picture + a source inset)
  4. write website/assets/library/index.json (what library.html reads) and a zip of everything
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
from library_defs import ENTRIES  # noqa: E402
from sketch_defs import SKETCHES  # noqa: E402

BIN = os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite")
CHECK = os.path.join(ROOT, "build/field-preset-check")
OUT = os.path.join(ROOT, "website/assets/library")
PW, PH = 1200, 800

PARAM_RE = re.compile(r"^\s*param\s+float\s+(\w+)\s*=\s*(-?[\d.]+)\s*\[", re.M)


def clean(code):
    return "\n".join(l.strip() for l in code.strip().splitlines() if l.strip())


def one_line(code):
    return " ".join(l for l in clean(code).splitlines() if not l.startswith("#"))


def src_expr(x):
    """A procedural test picture, as a vec4 expression of the coordinate expression x."""
    X = f"({x})"
    return (
        f"vec4((0.5 + 0.5 * cos(6.283185 * (vec3(0.0, 0.33, 0.67) + {X}.x + 0.5 * {X}.y)))"
        f" * (0.6 + 0.4 * (0.5 + 0.5 * cos(40.0 * length({X} - vec2(0.5, 0.5)))))"
        f" + vec3(0.8) * smoothstep(0.46, 0.495, abs(fract(8.0 * {X}.x) - 0.5))"
        f" + vec3(0.8) * smoothstep(0.46, 0.495, abs(fract(8.0 * {X}.y) - 0.5)), 1.0)"
    )


def substitute_img(code):
    """Replace every img(<expr>) with the procedural picture so a filter can render headless."""
    out, i = "", 0
    while True:
        m = re.compile(r"(?<![\w.])img\(").search(code, i)
        if not m:
            out += code[i:]
            break
        out += code[i:m.start()]
        j, depth = m.end(), 1
        while depth:
            depth += {"(": 1, ")": -1}.get(code[j], 0)
            j += 1
        out += src_expr(code[m.end():j - 1])
        i = j
    return out


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def compile_check(entry):
    with tempfile.NamedTemporaryFile("w", suffix=".field.txt", delete=False) as f:
        f.write(clean(entry["code"]))
    r = run([CHECK, "pixel", f.name])
    os.unlink(f.name)
    return r.returncode == 0, (r.stdout + r.stderr).strip().splitlines()[:6]


def write_field(entry):
    code = clean(entry["code"]) + "\n"
    params = {k: float(v) for k, v in PARAM_RE.findall(code)}
    dev = {
        "format": "field",
        "version": 1,
        "meta": {"name": entry["name"], "author": "Infinite", "description": entry["blurb"], "domain": "pixel"},
        "device": {"code": code, "params": params, "nodeSettings": {"width": 1280.0, "height": 720.0, "animate": 1.0}},
    }
    path = os.path.join(OUT, "pixel", entry["id"] + ".field")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(dev, f, indent=2)
        f.write("\n")
    return path, len(params)


def render(code_line, t, tmp, name):
    inf = os.path.join(tmp, name + ".inf")
    with open(inf, "w") as f:
        f.write(
            "infinite-patch 1\nnode 1 Source FieldPixel\n  id fp\n"
            f"  f width {PW}\n  f height {PH}\n  s code {code_line}\nend\n"
            "node 2 Utility Output\n  id out\nend\ncable out 0 fp\n"
        )
    od = os.path.join(tmp, name)
    r = run([BIN, "--frame", inf, str(t), od + "/"], timeout=180)
    pngs = sorted(glob.glob(od + "/*.png"))
    if not pngs:
        raise RuntimeError(f"{name}: render failed\n{r.stdout[-600:]}\n{r.stderr[-300:]}")
    return pngs[0]


SOURCE = os.path.join(os.path.dirname(__file__), "src", "source.jpg")  # NASA/ESA Hubble, Pillars of Creation (public domain)


def make_preview(entry, tmp):
    import numpy as np
    from PIL import Image, ImageDraw
    import field_np

    code = clean(entry["code"])
    if "input pixel image" in code:
        # Filters run on a real photo: the headless app cannot wire an image into a Field Pixel, so field_np evaluates the same code.
        src = Image.open(SOURCE).convert("RGB")
        ph = np.asarray(src, dtype=float) / 255
        ph = np.concatenate([ph, np.ones(ph.shape[:2] + (1,))], -1)
        img = Image.fromarray(field_np.run(code, ph, 900, 600, entry["t"]))
        s = src.resize((900 // 5, 600 // 5), Image.LANCZOS)
        pad = 14
        d = ImageDraw.Draw(img)
        d.rounded_rectangle((pad - 3, 600 - pad - s.height - 3, pad + s.width + 3, 600 - pad + 3), 8, fill=(255, 255, 255))
        img.paste(s, (pad, 600 - pad - s.height))
    else:
        result = render(one_line(code), entry["t"], tmp, entry["id"])
        img = Image.open(result).convert("RGB").resize((900, 600), Image.LANCZOS)
    dst = os.path.join(OUT, "previews", entry["id"] + ".jpg")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    img.save(dst, quality=86, optimize=True)
    return dst


def build_sketch(e, tmp, preview):
    """A Sketch device is the node's JS: <id>.js, pasted into the Sketch code editor. Preview = headless app render."""
    code = e["code"].strip() + "\n"
    path = os.path.join(OUT, "sketch", e["id"] + ".js")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(f"// {e['name']} - Infinite Library (Sketch). Paste into a Sketch node's code editor.\n" + code)
    if preview:
        from PIL import Image
        inf = os.path.join(tmp, e["id"] + ".inf")
        esc = code.replace("\\", "\\\\").replace("\n", "\\n")
        with open(inf, "w") as f:
            f.write(f"infinite-patch 1\nnode 1 Source Sketch\n  id sk\n  f width {PW}\n  f height {PH}\n  s code {esc}\nend\n"
                    "node 2 Utility Output\n  id out\nend\ncable out 0 sk\n")
        od = os.path.join(tmp, e["id"])
        run([BIN, "--frame", inf, str(e["t"]), od + "/"], timeout=180)
        pngs = sorted(glob.glob(od + "/*.png"))
        if not pngs:
            raise RuntimeError(e["id"] + ": sketch render failed")
        dst = os.path.join(OUT, "previews", e["id"] + ".jpg")
        Image.open(pngs[0]).convert("RGB").resize((900, 600), Image.LANCZOS).save(dst, quality=86, optimize=True)
    return {
        "id": e["id"], "name": e["name"], "kind": e["kind"], "tags": e["tags"], "description": e["blurb"],
        "price": 0, "domain": "sketch", "category": "Sketch", "params": len(re.findall(r'^param\(', code, re.M)),
        "file": f"assets/library/sketch/{e['id']}.js", "preview": f"assets/library/previews/{e['id']}.jpg",
        "bytes": os.path.getsize(path),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-preview", action="store_true")
    ap.add_argument("--only", help="build just this id")
    a = ap.parse_args()

    items, failed = [], False
    tmp = tempfile.mkdtemp(prefix="library_")
    for e in ENTRIES:
        if a.only and e["id"] != a.only:
            continue
        ok, msg = compile_check(e)
        print(f"{'ok  ' if ok else 'FAIL'} compile {e['id']}")
        if not ok:
            failed = True
            print("     " + "\n     ".join(msg))
            continue
        path, nparams = write_field(e)
        if not a.no_preview:
            print("     preview ->", os.path.relpath(make_preview(e, tmp), ROOT))
        items.append({
            "id": e["id"], "name": e["name"], "kind": e["kind"], "tags": e["tags"],
            "description": e["blurb"], "price": 0, "domain": "pixel", "category": "Pixel", "params": nparams,
            "file": f"assets/library/pixel/{e['id']}.field", "preview": f"assets/library/previews/{e['id']}.jpg",
            "bytes": os.path.getsize(path),
        })
    if failed:
        sys.exit(1)
    for e in SKETCHES:
        if a.only and e["id"] != a.only:
            continue
        items.append(build_sketch(e, tmp, not a.no_preview))
        print("ok   sketch", e["id"])
    if a.only:
        return
    with open(os.path.join(OUT, "index.json"), "w") as f:
        json.dump({"version": 1, "currency": "USD", "items": items}, f, indent=2)
        f.write("\n")
    with zipfile.ZipFile(os.path.join(OUT, "infinite-library.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for it in items:
            z.write(os.path.join(ROOT, "website", it["file"]), f"{it['domain']}/{os.path.basename(it['file'])}")
    print(f"wrote {len(items)} devices, index.json and infinite-library.zip in {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
