#!/usr/bin/env python3
"""Writes tests/render3d/skinned_two_bone.glb: the smallest rigged file the
rigging self-test needs, made here so no test asset is downloaded.

A square column 2 units tall (rings at y = 0, 0.5, 1, 1.5, 2) on two joints:
  root at the origin, tip at y = 1 (child of root), end at y = 2 (child of
  tip, no weights: the IK effector at the top of the column).
  Weights: rings 0-1 on root, ring 2 half and half, rings 3-4 on tip.
Clips:
  "bend"  - tip turns 90 degrees about Z over 1 s (linear).
  "twist" - root turns 90 degrees about Y over 2 s (linear).
Morph target "bulge": the top ring moves outward by half its offset.

    python3 tools/make-skinned-gltf.py [out.glb]
"""
import json
import math
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "tests", "render3d", "skinned_two_bone.glb")

RINGS = [0.0, 0.5, 1.0, 1.5, 2.0]
SQUARE = [(0.2, 0.2), (-0.2, 0.2), (-0.2, -0.2), (0.2, -0.2)]

pos, joints, weights, bulge = [], [], [], []
for r, y in enumerate(RINGS):
    for x, z in SQUARE:
        pos.append((x, y, z))
        if y <= 0.5:
            joints.append((0, 0, 0, 0)); weights.append((1, 0, 0, 0))
        elif y >= 1.5:
            joints.append((1, 0, 0, 0)); weights.append((1, 0, 0, 0))
        else:
            joints.append((0, 1, 0, 0)); weights.append((0.5, 0.5, 0, 0))
        bulge.append((x * 0.5, 0, z * 0.5) if y == 2.0 else (0, 0, 0))

idx = []
for r in range(len(RINGS) - 1):
    for s in range(4):
        a, b = r * 4 + s, r * 4 + (s + 1) % 4
        c, d = a + 4, b + 4
        idx += [a, b, d, a, d, c]
idx += [0, 2, 1, 0, 3, 2]          # bottom cap
top = (len(RINGS) - 1) * 4
idx += [top, top + 1, top + 2, top, top + 2, top + 3]

s45 = math.sin(math.pi / 4)
blob = bytearray()
views, accessors = [], []


def add(data, fmt, count, comp, typ, target=None, minmax=None):
    while len(blob) % 4:
        blob.append(0)
    off = len(blob)
    for item in data:
        blob.extend(struct.pack(fmt, *(item if isinstance(item, tuple) else (item,))))
    view = {"buffer": 0, "byteOffset": off, "byteLength": len(blob) - off}
    if target:
        view["target"] = target
    views.append(view)
    acc = {"bufferView": len(views) - 1, "componentType": comp, "count": count, "type": typ}
    if minmax:
        acc["min"], acc["max"] = minmax
    accessors.append(acc)
    return len(accessors) - 1


def bounds(v):
    return [[min(p[k] for p in v) for k in range(3)], [max(p[k] for p in v) for k in range(3)]]


F, U16, U32 = 5126, 5123, 5125
a_pos = add(pos, "<3f", len(pos), F, "VEC3", 34962, bounds(pos))
a_jnt = add(joints, "<4H", len(joints), U16, "VEC4", 34962)
a_wgt = add(weights, "<4f", len(weights), F, "VEC4", 34962)
a_blg = add(bulge, "<3f", len(bulge), F, "VEC3", 34962, bounds(bulge))
a_idx = add(idx, "<I", len(idx), U32, "SCALAR", 34963)
ident = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
tip_ibm = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1)   # column-major: translate (0, -1, 0)
end_ibm = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -2, 0, 1)
a_ibm = add([ident, tip_ibm, end_ibm], "<16f", 3, F, "MAT4")
a_t1 = add([0.0, 1.0], "<f", 2, F, "SCALAR", None, [[0.0], [1.0]])
a_bend = add([(0, 0, 0, 1), (0, 0, s45, s45)], "<4f", 2, F, "VEC4")
a_t2 = add([0.0, 2.0], "<f", 2, F, "SCALAR", None, [[0.0], [2.0]])
a_twist = add([(0, 0, 0, 1), (0, s45, 0, s45)], "<4f", 2, F, "VEC4")

gltf = {
    "asset": {"version": "2.0", "generator": "Infinite make-skinned-gltf.py"},
    "scene": 0,
    "scenes": [{"nodes": [0, 1]}],
    "nodes": [
        {"name": "column", "mesh": 0, "skin": 0},
        {"name": "root", "children": [2]},
        {"name": "tip", "translation": [0, 1, 0], "children": [3]},
        {"name": "end", "translation": [0, 1, 0]},
    ],
    "meshes": [{
        "name": "column",
        "primitives": [{
            "attributes": {"POSITION": a_pos, "JOINTS_0": a_jnt, "WEIGHTS_0": a_wgt},
            "indices": a_idx,
            "targets": [{"POSITION": a_blg}],
        }],
        "weights": [0.0],
        "extras": {"targetNames": ["bulge"]},
    }],
    "skins": [{"joints": [1, 2, 3], "inverseBindMatrices": a_ibm, "skeleton": 1}],
    "animations": [
        {"name": "bend", "samplers": [{"input": a_t1, "output": a_bend, "interpolation": "LINEAR"}],
         "channels": [{"sampler": 0, "target": {"node": 2, "path": "rotation"}}]},
        {"name": "twist", "samplers": [{"input": a_t2, "output": a_twist, "interpolation": "LINEAR"}],
         "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}}]},
    ],
    "buffers": [{"byteLength": len(blob)}],
    "bufferViews": views,
    "accessors": accessors,
}

js = json.dumps(gltf, separators=(",", ":")).encode()
js += b" " * ((4 - len(js) % 4) % 4)
while len(blob) % 4:
    blob.append(0)
total = 12 + 8 + len(js) + 8 + len(blob)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "wb") as f:
    f.write(struct.pack("<III", 0x46546C67, 2, total))
    f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
    f.write(struct.pack("<II", len(blob), 0x004E4942) + bytes(blob))
print(f"wrote {OUT} ({total} bytes, {len(pos)} vertices, {len(idx) // 3} triangles)")
