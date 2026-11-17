"""Look up node screenshots (docs/node_screenshots/) by the names the guide uses."""
import json
import os
import re

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(os.path.dirname(os.path.dirname(HERE)), "docs", "node_screenshots")
CATS = json.load(open(os.path.join(HERE, "type_categories.json")))
_files = {}
for f in sorted(os.listdir(DIR)):
    if f.endswith(".png"):
        _files[os.path.splitext(f)[0]] = os.path.join(DIR, f)


def _norm(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


_by_norm = {_norm(k): k for k in _files}
# Guide entries that cover several node types, or whose guide name differs from the type name.
ALIASES = {
    "Gaussian / Box Blur": ["gaussianblur", "boxblur"],
    "Bloom & Diffuse Glow": ["bloom", "diffuseglow"],
    "Twirl, Pinch/Punch & Ripple": ["twirl", "pinchpunch", "ripple"],
    "Geometry Primitives": ["Geometry", "Sphere", "Torus"],
    "Feedback & Trails": ["Feedback", "Trails"],
    "Ocean Surface": ["Ocean"],
    "Cloth Simulation": ["Cloth"],
    "Wavetable Synth": ["Wavetable"],
    "Metallic Resonator": ["Metallic"],
    "Shape": ["Circle", "Star", "Gear"],
}
_size = {}


def _dims(tid):
    if tid not in _size:
        with Image.open(_files[tid]) as im:
            _size[tid] = im.size
    return _size[tid]


_MAN = os.path.join(os.path.dirname(os.path.dirname(HERE)), "docs", "shots", "manifest.json")


def _manifest_nodes():
    """2.0 node shots: name -> [(id, framed png, w, h)] for the closed and open states, from docs/shots/manifest.json."""
    out = {}
    if not os.path.exists(_MAN):
        return out
    shots_ = json.load(open(_MAN)).get("shots", {})
    for sid in sorted(shots_):
        e = shots_[sid]
        if e.get("kind") != "node" or not e.get("framed"):
            continue
        path = os.path.join(os.path.dirname(os.path.dirname(HERE)), e["framed"])
        if os.path.exists(path):
            out.setdefault(e["name"], []).append((sid, path, e["width"], e["height"]))
    for v in out.values():
        v.sort(key=lambda t: t[0].endswith("/o"))   # closed first, then open
    return out


_M2 = _manifest_nodes()
CAPTIONS = json.load(open(_MAN)).get("captions", {}) if os.path.exists(_MAN) else {}


def find(name):
    if name in _M2:
        return _M2[name]
    ids = ALIASES.get(name) or [name]
    out = []
    for i in ids:
        tid = i if i in _files else _by_norm.get(_norm(i))
        if tid:
            w, h = _dims(tid)
            out.append((tid, _files[tid], w, h))
    return out


def all_shots():
    return [(t, p) + _dims(t) for t, p in _files.items()]


def category(tid):
    return CATS.get(tid, "Other")
