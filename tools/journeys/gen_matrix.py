#!/usr/bin/env python3
"""Writes one journey per cell of internal/plans/2d-view/tool-layer-matrix.md into tools/journeys/2d/m_<tool>_<kind>.json.

Every cell uses the same page so a failure reads the same way everywhere:

    Back   white paint layer, whole page            (bottom)
    T      the layer under test, red, ink at P=(200,150)
    Other  green paint corner 0..40, active          (top)

The tool acts at P. The target rule must find T (topmost layer with ink at P), whatever is active. Tools that act on the
active layer (Brush, Blur, Heal) pick T first, as a user would. Rerun after a matrix change:

    python3 tools/journeys/gen_matrix.py
"""
import json, os

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "2d")
P = (200, 150)
PT = {"text": (208, 205)}   # the text box starts at the page centre: a point on the first H's stem
RED, BLUE, WHITE = "1 0 0", "0 0 1", "1 1 1"

# Layer under test: setup lines, name used in queries, its kind name, layer count after setup.
KINDS = {
    "paint": (["layer paint T", "rect 100 75 300 225 " + RED], "T", "paint", 3),
    "image": (["image T fixtures/red.png"], "T", "node", 3),
    "text": (["layer text HHH", "textstyle 150 " + RED], "HHH", "text", 3),
    "vector": (["layer vector T", "shape 100 75 300 225 " + RED], "T", "vector", 3),
    "group": (["layer paint T", "rect 100 75 300 225 " + RED, "group G T"], "T", "paint", 4),
}
LIVE = {"image", "text", "vector"}


def page(kind):
    lines, _, _, _ = KINDS[kind]
    return ["doc 400 300", "layer paint Back", "rect 0 0 400 300 " + WHITE] + lines + \
           ["layer paint Other", "rect 0 0 40 40 0 1 0", "fg " + BLUE]


def at(x, y):
    return {"click": "canvas", "doc": [x, y]}


def cell(tool, kind):
    """(title, ref, setup extra, steps, expect) for one cell, or None when the matrix has no rule for it."""
    _, t, kname, n = KINDS[kind]
    live = kind in LIVE
    x, y = PT.get(kind, P)
    xy = "%d %d" % (x, y)
    keep = "kind %s == %s" % (t, kname)   # a tool never turns a live layer into pixels
    probe = "ink %s %s == 1" % (t, xy)   # the page is what the cell assumes
    if tool == "marquee":
        return ("Marquee round T while Other is active picks T", "Photoshop Auto-Select (Layer)",
                ["tool Marquee"], [{"drag": [x - 50, y - 50, x + 50, y + 50]}],
                [probe, "active.name == %s" % t, "selection == 1", keep])
    if tool == "wand":
        return ("Wand on T while Other is active selects on T and picks it", "Affinity flood select",
                ["tool Wand"], [at(x, y)], [probe, "active.name == %s" % t, "selection == 1", keep])
    if tool == "copy":
        return ("Marquee on T, Cmd+C, Cmd+V: a pixel layer with only the selected part, T kept",
                "Photoshop copy and paste selections", ["tool Marquee"],
                [{"drag": [x - 50, y - 50, x + 50, y + 50]}, {"key": "cmd+c"}, {"key": "cmd+v"}],
                [probe, "layers == %d" % (n + 1), "active.kind == paint", "activepx " + xy + " ~= " + RED,
                 "activepx 60 60 == none", keep])
    if tool == "cut":
        return ("Marquee on T, Cmd+X: the area goes from the picture, a live T stays live",
                "Photoshop cut; mask on live layers", ["tool Marquee"],
                [{"drag": [x - 50, y - 50, x + 50, y + 50]}, {"key": "cmd+x"}],
                [probe.replace("== 1", "== 0"), "px " + xy + " ~= " + WHITE] + (["px 120 90 ~= " + RED] if kind != "text" else []) + [keep])
    if tool == "brush":
        return ("Brush with T active: paint shows, a live T stays live", "Affinity Assistant: new pixel layer",
                ["active %s" % t, "tool Brush"], [{"drag": [x - 50, y, x + 50, y]}],
                ["px " + xy + " ~= " + BLUE, keep] + (["active.kind == paint", "layers == %d" % (n + 1)] if live else
                                                   ["layers == %d" % n]))
    if tool == "eraser":
        return ("Eraser on T while Other is active: T loses ink there, a live T stays live (mask)",
                "Photoshop: mask instead of rasterize", ["tool Eraser"], [{"drag": [x - 4, y, x + 4, y]}],
                ["ink %s %s == 0" % (t, xy), "px " + xy + " ~= " + WHITE, keep, "layers == %d" % n])
    if tool == "fill":
        if kind == "text":
            want = "text.colour HHH ~= " + BLUE
        elif kind == "vector":
            want = "shape.fill.at T ~= " + BLUE
        else:
            want = "px " + xy + " ~= " + BLUE
        return ("Bucket on T while Other is active: fills T's region, no empty layer", "Affinity flood fill",
                ["tool Fill"], [at(x, y)], [want, keep, "layers == %d" % (n + 1 if kind == "image" else n)])
    if tool == "pick":
        return ("Colour picker on T takes T's colour", "Photoshop eyedropper (sample all layers)",
                ["tool Pick"], [at(x, y)], ["fg ~= " + RED])
    if tool == "blur":
        return ("Blur with T active: a live T gets a rasterised copy above, original kept", "Photoshop rasterize prompt",
                ["active %s" % t, "tool Blur"], [{"drag": [x - 50, y, x + 50, y]}],
                [keep] + (["active.kind == paint", "layers == %d" % (n + 1), "rasterask == 0"] if live else
                          ["layers == %d" % n]))
    if tool == "heal":
        return ("Heal with T active: a live T gets a rasterised copy above, original kept", "Affinity retouch rasterizes",
                ["active %s" % t, "tool Heal"], [{"drag": [x - 50, y, x + 50, y]}],
                [keep] + (["active.kind == paint", "layers == %d" % (n + 1)] if live else ["layers == %d" % n]))
    if tool == "meshwarp":
        box = "warp.box ~= 100 75 300 225" if kind in ("paint", "vector", "group") else "warp.box != none"
        return ("Mesh warp on T while Other is active: the grid sits on T's ink", "Photoshop warp grid on layer bounds",
                ["tool MeshWarp"], [at(x, y)], [box, keep])
    if tool == "move":
        moved = "px %d %d ~= %s" % (x, y + 100, RED)
        return ("Move drag on T moves T (a group moves whole)", "Photoshop Move Auto-Select",
                ["tool Move"], [{"drag": [x, y, x, y + 100]}], [moved if kind != "text" else keep, keep])
    return None


TOOLS = ["marquee", "wand", "copy", "cut", "brush", "eraser", "fill", "pick", "blur", "heal", "meshwarp", "move"]


def main():
    for f in os.listdir(OUT):
        if f.startswith("m_") and f.endswith(".json"):
            os.remove(os.path.join(OUT, f))
    n = 0
    for tool in TOOLS:
        for kind in KINDS:
            c = cell(tool, kind)
            if c is None:
                continue
            title, ref, extra, steps, expect = c
            j = {"id": "m_%s_%s" % (tool, kind), "title": "%s (%s)" % (title, kind), "ref": ref, "ref_steps": len(steps),
                 "setup": page(kind) + extra, "steps": steps, "expect": expect}
            with open(os.path.join(OUT, j["id"] + ".json"), "w") as fh:
                json.dump(j, fh, indent=1)
            n += 1
    print("wrote %d cells" % n)


if __name__ == "__main__":
    main()
