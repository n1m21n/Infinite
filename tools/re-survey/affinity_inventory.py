#!/usr/bin/env python3
"""re-survey affinity_inventory: Affinity's tools and their settings against the 2D View's, black-box.

Sources (nothing executable is read):
  - Affinity's public help, one page per tool (affinity.help/photo2, static HTML): the context toolbar settings.
    Pages are cached in ~/re-output/affinity/help/.
  - The macOS accessibility menu map of the running app (ui_map's ax_dump): adjustment and live filter names.
  - Bundle metadata: the Brush Editor's localized strings (its "Associated Tool" list and dynamics labels).
  - Infinite: View2DPanel.cpp, through the hand-kept INF table below. Every label in it must appear as a string
    literal in that file, so the table cannot drift from the code without the check failing.

Usage:
  python3 tools/re-survey/affinity_probe.py inventory      (same as running this file)
  python3 tools/re-survey/affinity_inventory.py --self-test
Writes ~/re-output/affinity/inventory.md and inventory.json; exit 1 when an INF label is no longer in the code.
"""

import html
import json
import os
import re
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from common import RE_OUTPUT  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(RE_OUTPUT, "affinity")
HELP = os.path.join(OUT, "help")
HELP_URL = "https://affinity.help/photo2/en-US.lproj/pages/Tools/tools_%s.html"
PANEL = os.path.join(REPO, "src", "app", "panels", "View2DPanel.cpp")
SCENE = os.path.join(REPO, "src", "nodes", "Scene2DNode.cpp")
BRUSH_STRINGS = "/Applications/Affinity.app/Contents/Frameworks/libcocoaui.framework/Versions/A/Resources/" \
                "en-GB.lproj/BrushEditPanel.strings"

# Affinity tool (help slug, name) -> the 2D View tool that does the same job, or None.
TOOLS = [
    ("move", "Move", "move"), ("pan", "View", "hand"), ("zoom", "Zoom", "zoom"), ("crop", "Crop", "crop"),
    ("clrpicker", "Colour Picker", "pick"), ("stylePicker", "Style Picker", None), ("measure", "Measure", None),
    ("selectionBrush", "Selection Brush", None), ("objectSelection", "Object Selection", None),
    ("floodSelect", "Flood Select", "wand"), ("marquee", "Marquee (+ Freehand)", "marquee"),
    ("floodFill", "Flood Fill", "fill"), ("gradient", "Gradient", "gradient"),
    ("paintBrush", "Paint Brush", "brush"), ("pixel", "Pixel", "brush"), ("clrReplacementBrush", "Colour Replacement", None),
    ("paintMixerBrush", "Paint Mixer", None), ("eraseBrush", "Erase Brush", "eraser"),
    ("backgroundEraseBrush", "Background Erase", None), ("floodErase", "Flood Erase", None),
    ("dodgeBrush", "Dodge", "dodge"), ("burnBrush", "Burn", "burn"), ("spongeBrush", "Sponge", None),
    ("cloneBrush", "Clone", "clone"), ("undoBrush", "Undo Brush", None), ("blurBrush", "Blur", "blur"),
    ("sharpenBrush", "Sharpen", "sharpen"), ("medianBrush", "Median", None), ("smudgeBrush", "Smudge", "brush"),
    ("healingBrush", "Healing", "heal"), ("patch", "Patch", None), ("blemishRemoval", "Blemish Removal", None),
    ("inpaintingBrush", "Inpainting", None), ("redEye", "Red Eye", None),
    ("pen", "Pen", "pen"), ("node", "Node", "node"), ("rectangle", "Shapes (Rectangle)", "shape"),
    ("artText", "Artistic Text", "text"), ("frameText", "Frame Text", "text"),
    ("meshWarp", "Mesh Warp", None), ("perspective", "Perspective", None),
]

# The 2D View's settings per tool, as the bar and its More.. popup label them (View2DPanel.cpp).
_BRUSH = ["Size %.0f px", "Hardness %.0f%%", "Opacity %.0f%%", "Flow %.0f%%", "Smoothing", "Spacing", "Roundness", "Angle", "Hard edge", "Scatter",
          "Follow stroke", "Stabiliser", "Taper in", "Taper out", "Size jitter", "Opacity jitter", "Hue jitter",
          "Saturation jitter", "Brightness jitter", "Pen pressure", "Mode", "Lock alpha", "Paint the mask", "Symmetry"]
_RETOUCH = ["Size %.0f px", "Hardness %.0f%%", "Strength %.0f%%", "Spacing", "Roundness", "Angle", "Smoothing", "Pen pressure", "Symmetry"]
_SELOP = ["New selection", "Add to selection (Shift)", "Subtract from selection (Alt)", "Intersect with selection (Shift+Alt)"]
INF = {
    "move": ["Auto-select", "Align left edges", "Distribute horizontally", "Flip horizontal"],
    "hand": [], "zoom": ["Fit on screen (Ctrl+0)", "Actual pixels (Ctrl+1)"],
    "crop": ["Free", "Original", "Thirds", "Apply", "Cancel"],
    "pick": ["Point", "3x3 average", "All layers", "Current layer"],
    "wand": _SELOP + ["Tolerance %.0f%%", "Contiguous", "All layers", "Expand", "Contract", "Border", "Smooth"],
    "marquee": _SELOP + ["Rectangle", "Ellipse", "Freehand", "Polygon", "Feather %.0f px", "Fixed size"],
    "fill": ["Tolerance %.0f%%", "Contiguous", "All layers"],
    "gradient": ["Linear", "Radial", "Angle", "Reflected", "Diamond", "Opacity %.0f%%", "Reverse"],
    "brush": _BRUSH + ["Normal", "Behind", "Multiply", "Screen", "Smudge"], "eraser": _BRUSH,
    "dodge": _RETOUCH + ["Shadows", "Midtones", "Highlights"], "burn": _RETOUCH + ["Shadows", "Midtones", "Highlights"],
    "clone": _RETOUCH + ["Aligned", "All layers"], "heal": _RETOUCH + ["Aligned", "All layers"],
    "blur": _RETOUCH, "sharpen": _RETOUCH,
    "pen": ["Fill", "Stroke", "Stroke options", "Dash", "Gap %.0f"],
    "node": ["Fill", "Stroke"],
    "shape": ["Rectangle", "Rounded", "Ellipse", "Polygon", "Star", "Line", "Fill", "Stroke", "Radius %.0f", "Inner %.2f"],
    "text": ["##v2dfont", "Bold", "Italic", "Underline", "All caps", "Leading %.2f", "Tracking %.0f", "%.0f px"],
}

# Affinity setting -> the 2D View label that does it (None: a button / popup, not a setting to match).
SAME = {
    "Width": "Size", "Force pressure": "Pen pressure", "Force pressure to control size": "Pen pressure",
    "Stabilizer": "Stabiliser", "Mirror": "Symmetry", "Lock": "Symmetry", "Protect Alpha": "Lock alpha",
    "Blend Mode": "Mode", "Blend mode": "Mode", "Tonal Range": "Shadows", "Mode": "New selection",
    "Source": "All layers", "Auto-select": "Auto-select", "Align Horizontal": "Align left edges",
    "Align Vertical": "Align left edges", "Type": "Linear", "Feather": "Feather", "Tolerance": "Tolerance",
    "Unconstrained": "Free", "Original Ratio": "Original", "Overlay": "Thirds", "Radius": "3x3 average",
    "Fill": "Fill", "Stroke": "Stroke", "Stroke properties": "Stroke options", "Corner": "Radius",
    "Freehand": "Freehand", "Polygonal": "Polygon", "Width (marquee)": "Fixed size", "Height": "Fixed size",
    "Strength": "Strength", "Aligned": "Aligned", "Opacity": "Opacity", "Flow": "Flow", "Hardness": "Hardness", "Font Family": "##v2dfont", "Bold": "Bold",
    "Contiguous": "Contiguous", "Paragraph Leading": "Leading", "Font Size": "%.0f px", "Mode (sharpen)": None,
}
# Affinity adjustment -> the 2D View adjustment that covers it, where the names differ.
ADJ_SAME = {"Brightness / Contrast": "Brightness", "HSL": "Hue", "LUT": "Colour Lookup", "Lens Filter": "Photo Filter"}
# Context-bar entries that are actions, panels or document-level options rather than tool settings.
NOT_SETTING = {"More", "Apply", "Cancel", "Reset", "Settings", "Group", "Ungroup", "Lock Children", "Edit Image",
               "Replace Image", "Convert", "Hide Mesh", "Tools>Tool Handle Size", "Keep selected", "Action",
               "Transform", "Snap", "View", "Default", "Layers", "Groups", "Drag", "Unload", "Clean Brush", "Load Brush"}


def fetch(slug):
    os.makedirs(HELP, exist_ok=True)
    p = os.path.join(HELP, "tools_%s.html" % slug)
    if not os.path.exists(p):
        req = urllib.request.Request(HELP_URL % slug, headers={"User-Agent": "re-survey"})
        with urllib.request.urlopen(req, timeout=20) as r, open(p, "wb") as f:
            f.write(r.read())
    return open(p, encoding="utf-8", errors="replace").read()


def top_settings(page):
    """First span of each top-level <li> in the list after 'context toolbar' (nested lists hold option values)."""
    i = page.find("context toolbar")
    if i < 0:
        return []
    j = page.find("<ul", i)
    if j < 0:
        return []
    out, depth = [], 0
    for m in re.finditer(r"<(/?)(ul|li)\b[^>]*>", page[j:]):
        close, tag = m.group(1) == "/", m.group(2)
        if tag == "ul":
            depth += -1 if close else 1
            if depth == 0:
                break
        elif not close and depth == 1:
            s = re.match(r'\s*<span class="ui">([^<]+)</span>', page[j + m.end():])
            if s:
                out.append(html.unescape(s.group(1)).strip().rstrip(":"))
    return list(dict.fromkeys(out))


def bare(label):
    """A code literal as the user reads it: no ImGui id, no printf format ("Flow %.0f%%" -> "Flow")."""
    return label.split("##")[0].split("%")[0].strip() or label


def code_labels():
    src = open(PANEL, encoding="utf-8").read()
    lits = set(re.findall(r'"((?:[^"\\]|\\.)*)"', src))
    lits |= {l.split("##")[0] for l in lits}
    return lits


def brush_tools():
    """The Brush Editor's 'Associated Tool' choices: Affinity's pixel brush tools, from bundle strings."""
    if not os.path.exists(BRUSH_STRINGS):
        return [], []
    raw = open(BRUSH_STRINGS, "rb").read()
    txt = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8", "replace")
    vals = sorted(set(re.findall(r'=\s*"([^"]+)"', txt)))
    tools = [v for v in vals if v.endswith("brush tool") or v == "Pixel tool"]
    dyn = [v.rstrip(":") for v in vals if v.endswith("Jitter:") or v in ("Accumulation", "Wet edges:", "Scatter X:",
                                                                          "Base Texture:", "Brush Nozzles:")]
    return tools, dyn


def menu_adjustments():
    """Adjustment-layer names from the running app's menu bar (accessibility tree, read-only)."""
    try:
        import ui_map
        pid = subprocess.run(["pgrep", "-f", "Affinity.app/Contents/MacOS/Affinity"], capture_output=True, text=True).stdout.split()
        if not pid:
            return []
        tree = ui_map.dump(int(pid[0]))
    except Exception:
        return []
    found = []

    def walk(n, path):
        t = n.get("title")
        p = path + ([t] if t else [])
        if n.get("role") == "AXMenuItem" and t and len(p) == 3 and p[:2] == ["Pixel", "New Adjustment Layer"]:
            found.append(t.rstrip("…").strip())
        for k in n.get("kids", []):
            walk(k, p)
    walk(tree.get("menubar") or {}, [])
    return found


def inf_adjustments():
    src = open(SCENE, encoding="utf-8").read()
    m = re.search(r"kNames\[kAdjCount\]\s*=\s*\{([^}]*)\}", src)
    return re.findall(r'"([^"]+)"', m.group(1)) if m else []


def build():
    lits = code_labels()
    stale = sorted({l for v in INF.values() for l in v if l not in lits})
    rows, missing_all = [], {}
    for slug, name, inf in TOOLS:
        aff = [s for s in top_settings(fetch(slug)) if s not in NOT_SETTING]
        have = {bare(l) for l in INF.get(inf, [])} if inf else set()
        if slug == "marquee":
            aff = [("Width (marquee)" if s == "Width" else s) for s in aff]
        matched, missing = [], []
        for s in aff:
            t = SAME.get(s, s)
            (matched if inf and t in have else missing).append(s)
        if inf:
            missing_all[name] = missing
        rows.append({"tool": name, "infinite": inf, "affinity": aff, "matched": matched, "missing": missing})
    btools, dyn = brush_tools()
    adj_a, adj_i = menu_adjustments(), inf_adjustments()
    norm = lambda s: s.lower().replace("colour", "color").replace(" / ", "/").replace(" ", "")
    adj_missing = [a for a in adj_a if norm(ADJ_SAME.get(a, a)) not in {norm(x) for x in adj_i}]
    return {"rows": rows, "stale": stale, "brushTools": btools, "brushDynamics": dyn,
            "adjAffinity": adj_a, "adjInfinite": adj_i, "adjMissing": adj_missing}


def report(d):
    n_tool = len(d["rows"])
    n_have = sum(1 for r in d["rows"] if r["infinite"])
    n_set = sum(len(r["affinity"]) for r in d["rows"] if r["infinite"])
    n_match = sum(len(r["matched"]) for r in d["rows"] if r["infinite"])
    L = ["Tools: %d of %d Affinity photo tools have a 2D View tool; their settings: %d of %d matched." % (n_have, n_tool, n_match, n_set), "",
         "| Affinity tool | 2D View tool | Settings matched | Settings missing |", "|---|---|---|---|"]
    for r in d["rows"]:
        L.append("| %s | %s | %s | %s |" % (r["tool"], r["infinite"] or "**none**",
                                             "%d/%d" % (len(r["matched"]), len(r["affinity"])) if r["infinite"] else "-",
                                             ", ".join(r["missing"]) if r["infinite"] else "(whole tool)"))
    L += ["", "Brush tools (Brush Editor, bundle strings): " + ", ".join(d["brushTools"]),
          "Brush dynamics: " + ", ".join(d["brushDynamics"]),
          "Adjustments (menu): %d in Affinity, %d in the 2D View; missing: %s" % (
              len(d["adjAffinity"]), len(d["adjInfinite"]), ", ".join(d["adjMissing"]) or "none")]
    if d["stale"]:
        L += ["", "STALE INF labels (not in View2DPanel.cpp): " + ", ".join(d["stale"])]
    return "\n".join(L)


def self_test():
    page = ('<p>from the context toolbar:</p><ul><li><span class="ui">Width</span>&mdash;x<ul><li><span class="ui">Inner'
            '</span></li></ul></li><li><span class="ui">Mode:</span> y</li></ul><ul><li><span class="ui">Later</span></li></ul>')
    ok = top_settings(page) == ["Width", "Mode"]
    ok &= "Contiguous" in code_labels()
    print("self-test", "ok" if ok else "FAILED")
    return 0 if ok else 1


def main():
    if "--self-test" in sys.argv:
        return self_test()
    d = build()
    os.makedirs(OUT, exist_ok=True)
    md = report(d)
    with open(os.path.join(OUT, "inventory.md"), "w") as f:
        f.write(md + "\n")
    with open(os.path.join(OUT, "inventory.json"), "w") as f:
        json.dump(d, f, indent=1)
    print(md)
    return 1 if d["stale"] else 0


if __name__ == "__main__":
    sys.exit(main())
