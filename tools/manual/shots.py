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


_CAT_NAMES = set(CATS.values())


def find(name, cat=None):
    """Pictures for a guide name. A name that exists in two categories (Invert, Transform) needs cat; without it
    the lookup returns nothing for them rather than the other category's picture."""
    if cat and f"{name} ({cat})" in _files:
        return [(f"{name} ({cat})", _files[f"{name} ({cat})"]) + _dims(f"{name} ({cat})")]
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
    if tid in CATS:
        return CATS[tid]
    m = re.match(r"^(.+) \(([^()]+)\)$", tid)
    if m and m.group(2) in _CAT_NAMES:
        return m.group(2)  # category-suffixed picture, e.g. "Invert (Modulators)"
    return "Other"
