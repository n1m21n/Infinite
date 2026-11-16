#!/usr/bin/env python3
"""re-survey affinity_probe: measure Affinity (3.3+) through its own MCP scripting interface and A/B it against
Infinite's 2D View on one real photo.

Black-box only: Affinity's public MCP server (Settings > Model Context Protocol > Enable MCP server, served at
localhost:6767) and the SDK documentation it serves. Nothing executable in the bundle is read; `inventory`
(affinity_inventory.py) adds the public help pages, the accessibility menu map and bundle .strings metadata.

What it records (~/re-output/affinity/probe.json):
  layer node types, blend modes, every live filter / adjustment with its defaults and ranges, the raster brush
  settings the SDK exposes, selection / fill / mask enums, and how a placed image layer behaves under pixel
  operations (does it stay an image layer, do its pixels change).

A/B (~/re-output/affinity/ab/): the same operation on the same photo (an image layer, 512x768), in Affinity by
script and in Infinite through the 2D View panel's own actions (dev build, INFINITE_AB2D). Each pair is scored
with FLIP (lower is closer) and kept against tools/re-survey/affinity_ab_baseline.json.

Usage:
  python3 tools/re-survey/affinity_probe.py run [--photo P] [--app build-dev/Infinite.app] [--op NAME] [--set-baseline]
  python3 tools/re-survey/affinity_probe.py facts          # probe.json only
  python3 tools/re-survey/affinity_probe.py ab [...]       # A/B only
  python3 tools/re-survey/affinity_probe.py adjust [--case wbs_]  # adjustment renders for fitting
  python3 tools/re-survey/affinity_probe.py inventory      # tools + settings + adjustments vs the 2D View
  python3 tools/re-survey/affinity_probe.py --self-test
Exit: 0 ok, 1 failed / regressed, 2 usage error or Affinity not reachable.
"""

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import tempfile
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from common import RE_OUTPUT  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(RE_OUTPUT, "affinity")
AB = os.path.join(OUT, "ab")
BASELINE = os.path.join(HERE, "affinity_ab_baseline.json")
DESK = os.path.expanduser("~/Desktop/affinity-ab")   # Affinity's MCP may only touch files on the Desktop
DEFAULT_PHOTO = os.path.expanduser("~/Downloads/hanna-lazar-dLLvu1ipYF0-unsplash.jpg")   # Unsplash licence
W, H = 512, 768
FLIP_SLACK = 0.01   # an op regresses when its FLIP rises more than this over the baseline

# ---------------------------------------------------------------------------------------------------------------
# The operations. Coordinates are document pixels, y down; the Infinite side (View2DAffinityAb in
# src/app/panels/View2DPanel.cpp) uses the same numbers. "proxy" marks an Affinity side that stands in for a tool
# the SDK cannot drive (no brush strokes): a hard round eraser is a delete inside the stroke's capsule, the blur
# brush is a Gaussian blur inside a feathered capsule, repair is Affinity's inpainting fill inside the spot.
# Measured: on a placed image layer Affinity's fill, flood fill, blur and inpaint commands do nothing (delete does
# work), so those ops rasterise first, as the app asks before painting on an image layer.
RASTERISE = "doc.executeCommand(DocumentCommand.createRasteriseObjects(sel(), false, false)); "
OPS = [
    {"name": "rasterize", "proxy": False, "js": "doc.executeCommand(DocumentCommand.createRasteriseObjects(sel(), false, false));"},
    {"name": "delete_rect", "proxy": False, "js": "select(rectPoly(100,200,250,320)); del();"},
    {"name": "delete_ellipse", "proxy": False, "js": "select(ellipsePoly(150,450,350,590)); del();"},
    # Layer via Copy, the new layer alone: what is outside the selection goes.
    {"name": "copy_rect", "proxy": False, "js": "select(rectPoly(100,200,250,320)); doc.executeCommand(DocumentCommand.createRasterInvertSelection()); del();"},
    # Layer via Cut, the source alone: the selected pixels leave it.
    {"name": "cut_rect", "proxy": False, "js": "select(rectPoly(100,200,250,320)); del();"},
    {"name": "wand_delete", "proxy": False,
     "js": "doc.executeCommand(DocumentCommand.createRasterFloodSelect(new G.Point(300,100), 0.2, true, true, OP.New, dom.SamplingSource.CurrentLayer)); del();"},
    {"name": "eraser", "proxy": True, "js": "select(capsulePoly(60,600,450,560,20)); del();"},
    {"name": "blur_brush", "proxy": True,
     "js": RASTERISE + "select(capsulePoly(120,640,420,640,40), 16); const gp = N.GaussianBlurFilterParameters.create(); gp.radius = 6; "
           "doc.executeCommand(DocumentCommand.createGaussianBlurFilter(sel(), gp));"},
    {"name": "heal", "proxy": True,
     "js": RASTERISE + "select(ellipsePoly(215,355,265,405)); doc.executeCommand(DocumentCommand.createRasterFill(sel(), dom.RasterFillMode.Inpainting, red(), 1.0, BlendMode.Normal));"},
    {"name": "fill_rect", "proxy": False,
     "js": RASTERISE + "select(rectPoly(100,200,250,320)); doc.executeCommand(DocumentCommand.createRasterFill(sel(), dom.RasterFillMode.Colour, red(), 1.0, BlendMode.Normal));"},
    {"name": "bucket", "proxy": False,
     "js": RASTERISE + "doc.executeCommand(DocumentCommand.createRasterFloodFill(sel(), new G.Point(300,100), 0.2, true, true, dom.RasterFloodFillSamplingSource.CurrentLayer, BlendMode.Normal, red()));"},
]
# Adjustment layers over the photo (fitted maths in Scene2DNode: White Balance, Split Toning, Recolour, Normals, Tone Compression, Tone Stretch).
def _adj(kind, setter):
    return (f"const ap = N.{kind}AdjustmentParameters.create(); {setter} "
            f"doc.addNode(N.{kind}AdjustmentRasterNodeDefinition.create(ap), null, undefined, false);")
OPS += [
    {"name": "adj_white_balance", "proxy": False, "js": _adj("WhiteBalance", "ap.whiteBalance = 0.5; ap.tint = 0.25;")},
    {"name": "adj_split_toning", "proxy": False,
     "js": _adj("SplitToning", "ap.highlightsHue = 0.5236; ap.highlightsSaturation = 0.6; ap.shadowsHue = 3.6652; "
                               "ap.shadowsSaturation = 0.5; ap.balance = 0.4;")},
    {"name": "adj_recolour", "proxy": False, "js": _adj("Recolour", "ap.hue = 3.4907; ap.saturation = 0.5; ap.lightness = 0.1;")},
    {"name": "adj_normals", "proxy": False, "js": _adj("Normals", "ap.rotation = 0.7854; ap.scale = 0.75; ap.flipY = true;")},
    {"name": "adj_tone_compression", "proxy": False,
     "js": _adj("ToneCompression", "ap.exposure = 1.5; ap.gamma = 0.8; ap.colour = 0.25;")},
    {"name": "adj_tone_stretch", "proxy": False,
     "js": _adj("ToneStretch", "ap.stretchFactor = 0.25; ap.gamma = 1.0; ap.compression = 0.0;")},
]
# Vector shapes and layer settings: Affinity draws the same items with its shape nodes. Strokes sit on the edge.
def _shape(kind, box, fill, extra=""):
    return (f"const s = shape(S.{kind}, {box}); doc.executeCommand(DocumentCommand.createSetBrushFill(s, rgb({fill}))); "
            f"doc.executeCommand(DocumentCommand.createSetPenFill(s, null)); {extra}")
OPS += [
    {"name": "shape_rect", "proxy": False, "js": _shape("ShapeRectangle", "100,200,250,320", "255,0,0")},
    {"name": "shape_ellipse_stroke", "proxy": False,
     "js": _shape("ShapeEllipse", "150,450,350,590", "255,0,0",
                  "doc.executeCommand(DocumentCommand.createSetPenFill(s, rgb(0,0,255))); "
                  "doc.executeCommand(DocumentCommand.createSetLineStyle(s, LS.LineStyle.createDefaultWithWeight(8)));")},
    {"name": "shape_multiply", "proxy": False,
     "js": _shape("ShapeRectangle", "50,100,460,700", "255,128,64", "doc.executeCommand(DocumentCommand.createSetBlendMode(s, BlendMode.Multiply));")},
    {"name": "shape_opacity", "proxy": False,
     "js": _shape("ShapeRectangle", "100,200,250,320", "255,0,0", "doc.executeCommand(DocumentCommand.createSetOpacity(s, 0.5));")},
    {"name": "layer_opacity", "proxy": False, "js": "doc.executeCommand(DocumentCommand.createSetOpacity(sel(), 0.5));"},
]

# Behaviour checks, Infinite only: brush tools Affinity's SDK cannot stroke. Each names the box (x0, y0, x1, y1) the
# stroke covers and what Affinity's help says the tool does there; outside the box (well clear of the stroke) the
# photo must be untouched. Strokes run (330,520) -> (480,520), 60 px, unless the op says otherwise.
BH_BOX = (345, 505, 465, 535)
BH = {
    "bh_dodge": ("brightens", BH_BOX), "bh_burn": ("darkens", BH_BOX),
    "bh_sponge_sat": ("saturates", BH_BOX), "bh_sponge_desat": ("desaturates", BH_BOX),
    "bh_sponge_vibrance": ("saturates", BH_BOX), "bh_sharpen": ("more detail", BH_BOX),
    "bh_median": ("less detail", BH_BOX), "bh_paint": ("red", BH_BOX), "bh_smudge": ("changes", (360, 360, 450, 490)),
    "bh_clone": ("copies source", BH_BOX), "bh_colour_replace": ("redder", BH_BOX),
    "bh_bg_erase": ("clears", (345, 105, 465, 135)), "bh_undo": ("restores", BH_BOX),
    "bh_redeye": ("not red", BH_BOX), "bh_gradient": ("ramp", None),
}
OPS += [{"name": n, "proxy": False, "behaviour": True, "js": None} for n in BH]

# Reached in neither direction by script: Affinity's SDK has no raster stroke (RasterBrush.createDefault is "not
# yet implemented") and no gradient fill on pixels, so these are compared by the help pages only.
NOT_SCRIPTABLE = ["clone", "smudge", "gradient", "paint brush", "dodge / burn", "sharpen brush"]

JS_PRELUDE = r"""
const { app } = require('/application.js');
const D = require('/document.js');
const G = require('/geometry.js');
const N = require('/nodes.js');
const C = require('/colours.js');
const { DocumentCommand } = require('/commands.js');
const { Selection } = require('/selections.js');
const { Bitmap, RasterFormat } = require('/rasterobject.js');
const dom = require('affinity:dom');
const { BlendMode } = require('affinity:common');
const OP = dom.RasterSelectionLogicalOperation;
const desk = app.userDesktopPath + '/affinity-ab';
for (const d of D.Document.all) if (d.title.startsWith('photo')) d.close();
const doc = D.Document.load(desk + '/photo.png');
const kids = () => { const a = []; for (const c of doc.currentSpread.children) a.push(c); return a; };
const sel = () => Selection.create(doc, [kids()[kids().length - 1]]);
function polyOf(pts) { const p = G.Polygon.create(); for (const [x, y] of pts) p.addPoint(new G.Point(x, y)); return p; }
function rectPoly(x0, y0, x1, y1) { return polyOf([[x0, y0], [x1, y0], [x1, y1], [x0, y1]]); }
function ellipsePoly(x0, y0, x1, y1) { const cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = (x1 - x0) / 2, ry = (y1 - y0) / 2, pts = [];
  for (let i = 0; i < 256; i++) { const a = i / 256 * 2 * Math.PI; pts.push([cx + rx * Math.cos(a), cy + ry * Math.sin(a)]); } return polyOf(pts); }
function capsulePoly(ax, ay, bx, by, r) { const dx = bx - ax, dy = by - ay, l = Math.hypot(dx, dy), nx = -dy / l, ny = dx / l, t = Math.atan2(dy, dx), pts = [];
  for (let i = 0; i <= 64; i++) { const a = t + Math.PI / 2 + i / 64 * Math.PI; pts.push([ax + r * Math.cos(a), ay + r * Math.sin(a)]); }
  for (let i = 0; i <= 64; i++) { const a = t - Math.PI / 2 + i / 64 * Math.PI; pts.push([bx + r * Math.cos(a), by + r * Math.sin(a)]); }
  return polyOf(pts); }
function select(poly, feather) { doc.executeCommand(DocumentCommand.createSetRasterSelectionFromPolygon(poly, OP.New, true, feather || 0)); }
function del() { doc.executeCommand(DocumentCommand.createDeleteSelection(sel(), false)); }
function red() { return C.Colour.createRGBA8({ r: 255, g: 0, b: 0, alpha: 255 }); }
function rgb(r, g, b) { return C.Colour.createRGBA8({ r, g, b, alpha: 255 }); }
const S = require('/shapes.js');
const LS = require('/linestyle.js');
function shape(kind, x0, y0, x1, y1) {
  const def = N.ShapeNodeDefinition.createDefault(); def.shape = kind.create();
  def.setBoundingRectangle(new G.Rectangle(x0, y0, x1 - x0, y1 - y0));
  doc.addNode(def, null, undefined, false); return Selection.create(doc, [kids()[kids().length - 1]]); }
// The photo as a placed image layer (as a user drags a photo in), not the pixel layer a PNG opens as.
doc.addNode(N.ImageNodeDefinition.create(RasterFormat.RGBA8).setBitmap(Bitmap.loadFromFile(desk + '/photo.png')), null, undefined, false);
doc.deleteSelection(Selection.create(doc, kids().filter(c => c.isRasterNode)));
"""


def op_script(op):
    return (JS_PRELUDE + "try {\n" + op["js"] + "\n"
            "doc.executeCommand(DocumentCommand.createRasterDeselect());\n"
            "const k = kids(); console.log(JSON.stringify({layers: k.length, kind: k.length ? k[k.length - 1].constructor.name : null}));\n"
            "doc.export(desk + '/aff_" + op["name"] + ".png', D.FileExportOptions.createWithPresetName('PNG'), D.FileExportArea.createForWholeDocument());\n"
            "} finally { doc.close(); }\n")


FACTS_JS = r"""
const N = require('/nodes.js');
const common = require('affinity:common');
const dom = require('affinity:dom');
const brushes = require('affinity:brushes');
const out = { blendModes: common.BlendMode.keys, enums: {}, params: {}, nodeTypes: [] };
for (const e of ['RasterSelectionLogicalOperation', 'RasterFillMode', 'RasterFloodFillSamplingSource', 'SamplingSource',
                 'RasterSelectionOutlineAlignment', 'NodeChildType']) if (dom[e]) out.enums[e] = dom[e].keys;
for (const e of ['BrushDynamicControllerType', 'RasterBrushTextureMode']) out.enums[e] = brushes[e].keys;
for (const k of Object.keys(N)) {
  if (/NodeDefinition$/.test(k) && typeof N[k] === 'function') out.nodeTypes.push(k.replace(/Definition$/, ''));
  if (!/Parameters$/.test(k) || typeof N[k]?.create !== 'function') continue;
  let p; try { p = N[k].create(); } catch (e) { out.params[k] = { error: String(e.message) }; continue; }
  const vals = {}; let o = Object.getPrototypeOf(p);
  for (const name of Object.getOwnPropertyNames(o)) {
    const d = Object.getOwnPropertyDescriptor(o, name);
    if (!d || !d.get || /^is[A-Z]|^handle$/.test(name)) continue;
    try { const v = p[name]; vals[name] = (v === null || ['number', 'boolean', 'string'].includes(typeof v)) ? v : (v?.toString?.() ?? typeof v); }
    catch (e) { vals[name] = 'error'; }
  }
  out.params[k] = vals;
}
console.log(JSON.stringify(out));
"""

# How a placed image layer behaves under each pixel operation: does it stay an image layer, do its pixels change.
BEHAVIOUR_OPS = {
    "raster delete": "select(rectPoly(100,200,250,320)); del();",
    "flood select": "doc.executeCommand(DocumentCommand.createRasterFloodSelect(new G.Point(300,100), 0.2, true, true, OP.New, dom.SamplingSource.CurrentLayer));",
    "gaussian blur": "const gp = N.GaussianBlurFilterParameters.create(); gp.radius = 8; doc.executeCommand(DocumentCommand.createGaussianBlurFilter(sel(), gp));",
    "raster fill": "select(rectPoly(100,200,250,320)); doc.executeCommand(DocumentCommand.createRasterFill(sel(), dom.RasterFillMode.Colour, red(), 1.0, BlendMode.Normal));",
    "rasterise": "doc.executeCommand(DocumentCommand.createRasteriseObjects(sel(), false, false));",
    "add mask": "doc.executeCommand(DocumentCommand.createAddMask(sel(), false));",
}


def behaviour_script(js):
    return (JS_PRELUDE + "try { const before = kids().map(c => c.constructor.name).join(',');\n" + js + "\n"
            "console.log(JSON.stringify({before, after: kids().map(c => c.constructor.name).join(','),"
            " children: kids().map(c => { let n = 0; for (const _ of (c.children ?? [])) n++; return n; })}));\n"
            "} finally { doc.close(); }\n")


def last_json(text):
    for line in reversed(text.strip().splitlines()):
        line = line.strip()
        if line.startswith("{"):
            return json.loads(line)
    raise ValueError("no JSON line in: " + text[-400:])


# ---------------------------------------------------------------------------------------------------------------
def prepare_photo(src):
    os.makedirs(DESK, exist_ok=True)
    os.makedirs(AB, exist_ok=True)
    from PIL import Image
    im = Image.open(src).convert("RGB")
    im = im.resize((W, H), Image.LANCZOS) if im.size != (W, H) else im
    for d in (DESK, AB):
        im.save(os.path.join(d, "photo.png"))


def facts(aff):
    sdk_dir = os.path.join(OUT, "sdk")
    os.makedirs(sdk_dir, exist_ok=True)
    topics = [n.strip() for n in aff.call("list_sdk_documentation").split(",") if n.strip()]
    missing = []
    for t in topics:
        p = os.path.join(sdk_dir, t)
        try:
            text = aff.call("read_sdk_documentation_topic", {"filename": t})
        except RuntimeError:
            missing.append(t)
            continue
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as f:
            f.write(text)
    if missing:
        print("facts: %d listed topics would not read: %s" % (len(missing), ", ".join(missing)[:300]))
    data = last_json(aff.call("execute_script", {"script": FACTS_JS}))
    ranges = json.load(open(os.path.join(sdk_dir, "param_ranges.min.json")))
    data["paramRanges"] = {k: v for k, v in ranges.items() if k.startswith("DocumentCommandApi") or "Parameters" in k}
    data["structRanges"] = json.load(open(os.path.join(sdk_dir, "struct_ranges.min.json")))
    rb = open(os.path.join(sdk_dir, "ts", "brushes.d.ts")).read()
    data["rasterBrush"] = {
        "settings": sorted(set(re.findall(r"set([A-Z]\w+)\(self: RasterBrushHandle", rb))),
        "createDefault": "not implemented in the SDK (rasterbrush.js throws), so defaults are not readable",
        "strokes": "no raster stroke command in commands.js: paint, erase, clone, heal, smudge are not scriptable",
    }
    beh = {}
    for name, js in BEHAVIOUR_OPS.items():
        try:
            beh[name] = last_json(aff.call("execute_script", {"script": behaviour_script(js)}, timeout=300))
        except Exception as e:  # noqa: BLE001
            beh[name] = {"error": str(e)[:300]}
    data["imageLayerBehaviour"] = beh
    data["sdkTopics"] = len(topics)
    data["notScriptable"] = NOT_SCRIPTABLE
    with open(os.path.join(OUT, "probe.json"), "w") as f:
        json.dump(data, f, indent=1)
    print("facts: %d topics, %d node types, %d parameter sets, %d blend modes -> %s" % (
        len(topics), len(data["nodeTypes"]), len(data["params"]), len(data["blendModes"]), os.path.join(OUT, "probe.json")))
    return data


def affinity_side(aff, ops):
    res = {}
    for op in ops:
        if op.get("behaviour"):
            continue
        out = os.path.join(DESK, "aff_%s.png" % op["name"])
        if os.path.exists(out):
            os.remove(out)
        try:
            res[op["name"]] = last_json(aff.call("execute_script", {"script": op_script(op)}, timeout=300))
            shutil.copy(out, os.path.join(AB, "aff_%s.png" % op["name"]))
        except Exception as e:  # noqa: BLE001
            res[op["name"]] = {"error": str(e)[:400]}
        print("  affinity %-15s %s" % (op["name"], res[op["name"]]))
    return res


def infinite_side(app_path, only=None):
    exe = os.path.join(app_path, "Contents", "MacOS", "Infinite")
    if not os.path.exists(exe):
        sys.exit("no Infinite build at %s (python3 scripts/ibuild.py build --mode dev)" % exe)
    # A clean profile: saved brush presets and settings must not reach the ops.
    env = dict(os.environ, INFINITE_AB2D=AB, HOME=tempfile.mkdtemp(prefix="ab2d-home-"))
    if only:
        env["INFINITE_AB2D_OP"] = only
    log = os.path.join(AB, "infinite.log")
    # Under a pty: redirected test output is lost when a run is killed on timeout.
    with open(log, "w") as f:
        try:
            subprocess.run(["script", "-q", "/dev/null", exe], env=env, stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT, timeout=300)
        except subprocess.TimeoutExpired:
            f.write("\nTIMEOUT\n")
    text = open(log, errors="replace").read()
    res = {}
    for m in re.finditer(r"AB2D (\w+) ran=(\d) kind=(-?\d+) layers=(\d+) size=(\d+)x(\d+) (\w+)", text):
        res[m.group(1)] = {"ran": m.group(2) == "1", "kind": int(m.group(3)), "layers": int(m.group(4)),
                           "size": [int(m.group(5)), int(m.group(6))], "wrote": m.group(7) == "ok"}
    for k, v in res.items():
        print("  infinite %-15s %s" % (k, v))
    return res


def load_rgba(path):
    import numpy as np
    from PIL import Image
    return np.asarray(Image.open(path).convert("RGBA"), np.float32) / 255.0


def over_grey(px):
    """Both sides over the same mid grey, so a cleared pixel reads the same whatever colour a transparent pixel holds."""
    a = px[..., 3:4]
    return px[..., :3] * a + 0.5 * (1.0 - a)


def score_pair(a_path, b_path, sheet=None):
    import numpy as np
    import flip_evaluator as flip
    a, b = over_grey(load_rgba(a_path)), over_grey(load_rgba(b_path))
    if a.shape != b.shape:
        return {"error": "size %s vs %s" % (a.shape, b.shape)}
    err, mean, _ = flip.evaluate(a.astype(np.float32), b.astype(np.float32), "LDR", applyMagma=True)
    if sheet:
        from PIL import Image
        row = np.concatenate([a, b, np.clip(err, 0, 1)[..., :3] if err.ndim == 3 else np.repeat(np.clip(err, 0, 1)[..., None], 3, -1)], 1)
        Image.fromarray((row * 255).astype(np.uint8)).save(sheet)
    return {"flip": round(float(mean), 4)}


def changed(path, photo):
    """Share of pixels the operation changed from the untouched photo: 0 means it did nothing."""
    import numpy as np
    d = np.abs(over_grey(load_rgba(path)) - over_grey(load_rgba(photo))).max(-1)
    return round(float((d > 4 / 255).mean()), 4)


def bh_check(name, path, photo):
    """Did the brush do what Affinity's help says, inside its box and nowhere far from it."""
    import numpy as np
    want, box = BH[name]
    img, src = load_rgba(path), load_rgba(photo)
    a, p = over_grey(img), over_grey(src)
    if a.shape != p.shape:
        return {"pass": False, "why": "size %s" % (a.shape,)}
    if want == "ramp":
        r, b_ = a[:, :, 0].mean(0), a[:, :, 2].mean(0)
        ok = r[:16].mean() > 0.8 and b_[:16].mean() < 0.2 and b_[-16:].mean() > 0.8 and r[-16:].mean() < 0.2 \
            and bool(np.all(np.diff(r[8:-8]) <= 0.01))
        return {"pass": bool(ok), "left": [round(float(r[:16].mean()), 3), round(float(b_[:16].mean()), 3)],
                "right": [round(float(r[-16:].mean()), 3), round(float(b_[-16:].mean()), 3)]}
    x0, y0, x1, y1 = box
    A, P = a[y0:y1, x0:x1], p[y0:y1, x0:x1]
    luma = lambda c: float((c[..., 0] * 0.2126 + c[..., 1] * 0.7152 + c[..., 2] * 0.0722).mean())
    chroma = lambda c: float((c.max(-1) - c.min(-1)).mean())
    redness = lambda c: float((c[..., 0] - c[..., 1:].max(-1)).mean())

    def detail(c):
        g = c.mean(-1)
        return float(np.abs(4 * g[1:-1, 1:-1] - g[:-2, 1:-1] - g[2:, 1:-1] - g[1:-1, :-2] - g[1:-1, 2:]).mean())
    m = {}
    if want == "brightens":
        m["d"] = luma(A) - luma(P); ok = m["d"] > 0.02
    elif want == "darkens":
        m["d"] = luma(A) - luma(P); ok = m["d"] < -0.02
    elif want == "saturates":
        m["d"] = chroma(A) - chroma(P); ok = m["d"] > 0.01
    elif want == "desaturates":
        m["d"] = chroma(A) - chroma(P); ok = m["d"] < -0.01
    elif want == "more detail":
        m["d"] = detail(A) / max(detail(P), 1e-6); ok = m["d"] > 1.1
    elif want == "less detail":
        m["d"] = detail(A) / max(detail(P), 1e-6); ok = m["d"] < 0.9
    elif want == "red":
        m["d"] = redness(A); ok = m["d"] > 0.6
    elif want == "redder":
        m["d"] = redness(A) - redness(P); ok = m["d"] > 0.05
    elif want == "not red":
        m["d"] = redness(A); ok = m["d"] < 0.3
    elif want == "changes":
        m["d"] = float((np.abs(A - P).max(-1) > 4 / 255).mean()); ok = m["d"] > 0.2
    elif want == "clears":
        m["d"] = float(img[y0:y1, x0:x1, 3].mean()); ok = m["d"] < 0.5
    elif want == "restores":
        m["d"] = float(np.abs(A - P).mean()); ok = m["d"] < 0.03
    elif want == "copies source":
        S = p[y0 - 320:y1 - 320, x0:x1]
        m["d"] = float(np.abs(A - S).mean()) / max(float(np.abs(P - S).mean()), 1e-6); ok = m["d"] < 0.5
    else:
        return {"pass": False, "why": "unknown check " + want}
    far = np.ones(a.shape[:2], bool)
    far[max(0, y0 - 60):y1 + 60, max(0, x0 - 60):x1 + 60] = False
    m["far"] = float((np.abs(a - p).max(-1)[far] > 4 / 255).mean())
    return {"pass": bool(ok) and m["far"] < 0.002, **{k: round(v, 4) for k, v in m.items()}}


def ab(app_path, only=None, set_baseline=False, aff=None):
    ops = [o for o in OPS if only in (None, o["name"])]
    if aff is not None:
        aff_res = affinity_side(aff, ops)
    else:
        aff_res = {}
    inf_res = infinite_side(app_path, only)
    photo = os.path.join(AB, "photo.png")
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    rows, regressed = {}, []
    for op in ops:
        n = op["name"]
        a, b = os.path.join(AB, "aff_%s.png" % n), os.path.join(AB, "inf_%s.png" % n)
        r = {"proxy": op["proxy"], "affinity": aff_res.get(n), "infinite": inf_res.get(n)}
        if op.get("behaviour"):
            r.update(bh_check(n, b, photo) if os.path.exists(b) else {"pass": False, "why": "no picture"})
            if not r["pass"]:
                regressed.append(n)
        elif os.path.exists(a) and os.path.exists(b):
            r.update(score_pair(a, b, os.path.join(AB, "sheet_%s.png" % n)))
            r["changed_aff"], r["changed_inf"] = changed(a, photo), changed(b, photo)
            if n in base and "flip" in r and r["flip"] > base[n]["flip"] + FLIP_SLACK:
                regressed.append(n)
            if n in base:
                r["baseline_flip"] = base[n]["flip"]
        rows[n] = r
    with open(os.path.join(AB, "scores.json"), "w") as f:
        json.dump(rows, f, indent=1)
    print("\n%-15s %6s %8s %9s %9s %6s" % ("op", "flip", "baseline", "chg aff", "chg inf", "proxy"))
    for n, r in rows.items():
        if "pass" in r:
            continue
        print("%-15s %6s %8s %9s %9s %6s" % (n, r.get("flip", "-"), r.get("baseline_flip", "-"), r.get("changed_aff", "-"),
                                           r.get("changed_inf", "-"), "yes" if r["proxy"] else ""))
    bh = {n: r for n, r in rows.items() if "pass" in r}
    if bh:
        print("\n%-20s %-14s %5s %8s %7s" % ("behaviour", "should", "pass", "measure", "far"))
        for n, r in bh.items():
            print("%-20s %-14s %5s %8s %7s" % (n, BH[n][0], "yes" if r["pass"] else "NO", r.get("d", "-"), r.get("far", "-")))
    if set_baseline:
        keep = {n: {"flip": r["flip"]} for n, r in rows.items() if "flip" in r}
        merged = dict(base, **keep)
        with open(BASELINE, "w") as f:
            json.dump(merged, f, indent=1, sort_keys=True)
        print("baseline written: %s" % BASELINE)
    if regressed:
        print("REGRESSED: " + ", ".join(regressed))
    return 1 if regressed else 0


# ---------------------------------------------------------------------------------------------------------------
def self_test():
    import numpy as np
    from PIL import Image
    import tempfile
    fails = []

    def check(what, ok):
        print("  [%s] %s" % ("pass" if ok else "FAIL", what))
        if not ok:
            fails.append(what)

    names = [o["name"] for o in OPS]
    check("op names are unique", len(names) == len(set(names)))
    src = open(os.path.join(REPO, "src", "app", "panels", "View2DPanel.cpp")).read()
    check("every op has an Infinite side", all('{ "%s",' % n in src for n in names))
    check("every op script closes its document", all("doc.close()" in op_script(o) for o in OPS if not o.get("behaviour")))
    check("every behaviour op has a check", all(o["name"] in BH for o in OPS if o.get("behaviour")))
    check("JSON is read from the last line", last_json("noise\n{\"a\": 1}\n") == {"a": 1})
    with tempfile.TemporaryDirectory() as d:
        rng = np.random.default_rng(1)
        img = (rng.random((64, 48, 4)) * 255).astype(np.uint8)
        img[..., 3] = 255
        p, q, r = os.path.join(d, "p.png"), os.path.join(d, "q.png"), os.path.join(d, "r.png")
        Image.fromarray(img).save(p)
        Image.fromarray(img).save(q)
        img2 = img.copy()
        img2[10:30, 10:30, 3] = 0
        Image.fromarray(img2).save(r)
        same, diff = score_pair(p, q), score_pair(p, r)
        check("identical pictures score 0", same["flip"] == 0.0)
        check("a cleared square scores above 0", diff["flip"] > 0.0)
        check("changed() sees the cleared square", abs(changed(r, p) - 400 / (64 * 48)) < 0.02)
        check("changed() of the photo itself is 0", changed(p, p) == 0.0)
        big = (rng.random((768, 512, 4)) * 200).astype(np.uint8)
        big[..., 3] = 255
        ph, lit = os.path.join(d, "photo.png"), os.path.join(d, "lit.png")
        Image.fromarray(big).save(ph)
        x0, y0, x1, y1 = BH_BOX
        brighter = big.copy()
        brighter[y0:y1, x0:x1, :3] += 40
        Image.fromarray(brighter).save(lit)
        check("bh_check passes a brightened box", bh_check("bh_dodge", lit, ph)["pass"])
        check("bh_check fails an untouched photo", not bh_check("bh_dodge", ph, ph)["pass"])
        brighter[10:40, 10:40, :3] += 40
        Image.fromarray(brighter).save(lit)
        check("bh_check fails a change far from the stroke", not bh_check("bh_dodge", lit, ph)["pass"])
    check("capsule polygon closes", "capsulePoly" in JS_PRELUDE and "i <= 64" in JS_PRELUDE)
    print("SELFTEST %s" % ("FAIL" if fails else "OK"))
    return 1 if fails else 0


# Adjustment renders: a grey ramp (rows 0-63) over seeded random colours, through one Affinity adjustment layer.
# Used to fit the maths (White Balance = Bradford in linear sRGB, etc.); pictures go to ~/re-output/affinity/adjust.
ADJ = os.path.join(OUT, "adjust")
ADJ_CASES = {
    "wb_p50": ("WhiteBalance", "p.whiteBalance = 0.5;"), "wb_m50": ("WhiteBalance", "p.whiteBalance = -0.5;"),
    "wb_p100": ("WhiteBalance", "p.whiteBalance = 1.0;"),
    "tint_p50": ("WhiteBalance", "p.tint = 0.5;"), "tint_m50": ("WhiteBalance", "p.tint = -0.5;"),
    "st_h0": ("SplitToning", "p.highlightsHue = 0; p.highlightsSaturation = 1;"),
    "st_s120": ("SplitToning", "p.shadowsHue = 2.0944; p.shadowsSaturation = 1;"),
    "st_both": ("SplitToning", "p.highlightsHue = 1.0472; p.highlightsSaturation = 0.5; "
                               "p.shadowsHue = 4.18879; p.shadowsSaturation = 0.5;"),
    "st_bal": ("SplitToning", "p.highlightsHue = 0; p.highlightsSaturation = 1; p.balance = 0.25;"),
    "rc_0": ("Recolour", "p.hue = 0; p.saturation = 1; p.lightness = 0;"),
    "rc_120h": ("Recolour", "p.hue = 2.0944; p.saturation = 0.5; p.lightness = 0.25;"),
    "combo_a": ("WhiteBalance", "p.whiteBalance = 0.5; p.tint = 0.5;"),
    "combo_b": ("WhiteBalance", "p.whiteBalance = -0.75; p.tint = 0.25;"),
    "combo_c": ("WhiteBalance", "p.whiteBalance = 1; p.tint = -1;"),
    **{f"wbs_{i}": ("WhiteBalance", f"p.whiteBalance = {(i - 8) / 8};") for i in range(17)},
    **{f"tns_{i}": ("WhiteBalance", f"p.tint = {(i - 8) / 8};") for i in range(17)},
    **{f"sth_{i}": ("SplitToning", f"p.highlightsHue = {i * math.pi / 6}; p.highlightsSaturation = 1;") for i in range(12)},
    **{f"sts_{i}": ("SplitToning", f"p.highlightsHue = 0; p.highlightsSaturation = {i / 4};") for i in range(5)},
    **{f"stb_{i}": ("SplitToning", f"p.highlightsHue = 0; p.highlightsSaturation = 1; p.shadowsHue = 3.14159; "
                                   f"p.shadowsSaturation = 1; p.balance = {i / 4};") for i in range(5)},
    **{f"sthb_{j}": ("SplitToning", f"p.highlightsHue = 0; p.highlightsSaturation = 0.25; p.balance = {j / 8};") for j in range(9)},
    **{f"stsb_{j}": ("SplitToning", f"p.shadowsHue = 0; p.shadowsSaturation = 0.25; p.balance = {j / 8};") for j in range(9)},
    **{f"sthc_{j}": ("SplitToning", f"p.highlightsHue = 4.18879; p.highlightsSaturation = 0.5; p.balance = {j / 8};") for j in range(9)},
    **{f"stsc_{j}": ("SplitToning", f"p.shadowsHue = 1.0472; p.shadowsSaturation = 0.5; p.balance = {j / 8};") for j in range(9)},
    **{f"rch_{i}": ("Recolour", f"p.hue = {i * math.pi / 6}; p.saturation = 1; p.lightness = 0;") for i in range(12)},
    **{f"rcs_{i}": ("Recolour", f"p.hue = 0; p.saturation = {i / 4}; p.lightness = 0;") for i in range(5)},
    **{f"rcl_{i}": ("Recolour", f"p.hue = 0; p.saturation = 1; p.lightness = {(i - 4) / 4};") for i in range(9)},
    "nm_0": ("Normals", ""),
    **{f"nmr_{i}": ("Normals", f"p.rotation = {(i - 4) * math.pi / 4};") for i in range(9)},
    **{f"nms_{i}": ("Normals", f"p.scale = {i / 4};") for i in range(9)},
    "nm_fx": ("Normals", "p.flipX = true;"), "nm_fy": ("Normals", "p.flipY = true;"),
    "tc_0": ("ToneCompression", ""),
    **{f"tce_{i}": ("ToneCompression", f"p.exposure = {i / 4};") for i in range(9)},
    **{f"tcg_{i}": ("ToneCompression", f"p.gamma = {i / 4};") for i in range(9)},
    **{f"tcc_{i}": ("ToneCompression", f"p.colour = {i / 4};") for i in range(5)},
    "ts_0": ("ToneStretch", ""),
    **{f"tsf_{i}": ("ToneStretch", f"p.stretchFactor = {i / 8};") for i in range(9)},
    **{f"tsg_{i}": ("ToneStretch", f"p.gamma = {i / 4};") for i in range(9)},
    **{f"tsc_{i}": ("ToneStretch", f"p.compression = {i / 4};") for i in range(5)},
}
ADJ_JS = r"""
const { app } = require('/application.js'); const D = require('/document.js'); const N = require('/nodes.js');
const desk = app.userDesktopPath + '/affinity-ab';
for (const d of D.Document.all) if (d.title.startsWith('src')) d.close();
const doc = D.Document.load(desk + '/src.png');
try {
const p = N.KINDAdjustmentParameters.create(); SET
doc.addNode(N.KINDAdjustmentRasterNodeDefinition.create(p), null, undefined, false);
doc.export(desk + '/adj_NAME.png', D.FileExportOptions.createWithPresetName('PNG'), D.FileExportArea.createForWholeDocument());
console.log(JSON.stringify({ok: true}));
} finally { doc.close(); }
"""


def adj_source():
    import numpy as np
    from PIL import Image
    n = 256
    img = np.zeros((n, n, 3), np.uint8)
    img[:64] = np.arange(n, dtype=np.uint8)[None, :, None]
    img[64:] = np.random.default_rng(3).integers(0, 256, (n - 64, n, 3))
    return Image.fromarray(img)


def adjust(aff, prefixes):
    desk = os.path.expanduser("~/Desktop/affinity-ab")
    os.makedirs(desk, exist_ok=True)
    os.makedirs(ADJ, exist_ok=True)
    src = adj_source()
    src.save(os.path.join(desk, "src.png"))
    src.save(os.path.join(ADJ, "src.png"))
    names = [k for k in ADJ_CASES if not prefixes or any(k.startswith(p) for p in prefixes)]
    for name in names:
        kind, setter = ADJ_CASES[name]
        js = ADJ_JS.replace("KIND", kind).replace("SET", setter).replace("NAME", name)
        r = aff.call("execute_script", {"script": js}, timeout=120)
        got = os.path.join(desk, f"adj_{name}.png")
        if os.path.exists(got):
            os.replace(got, os.path.join(ADJ, f"adj_{name}.png"))
        else:
            print(name, "no picture:", str(r).strip().splitlines()[-1][:200])
    os.remove(os.path.join(desk, "src.png"))
    print(f"{len(names)} adjustment renders -> {ADJ}")
    return 0


def connect():
    from affinity_mcp import Affinity, AffinityUnavailable
    try:
        return Affinity().__enter__()
    except AffinityUnavailable as e:
        print(e)
        sys.exit(2)


def main():
    if "--self-test" in sys.argv:
        return self_test()
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["run", "facts", "ab", "adjust", "inventory"])
    ap.add_argument("--photo", default=DEFAULT_PHOTO)
    ap.add_argument("--app", default=os.path.join(REPO, "build-dev", "Infinite.app"))
    ap.add_argument("--op")
    ap.add_argument("--case", action="append", help="adjust: case-name prefix (repeatable); all when omitted")
    ap.add_argument("--set-baseline", action="store_true")
    ap.add_argument("--infinite-only", action="store_true", help="reuse the Affinity pictures already in the ab folder")
    a = ap.parse_args()
    if a.cmd == "inventory":
        import affinity_inventory
        return affinity_inventory.main()
    if a.cmd == "adjust":
        aff = connect()
        try:
            return adjust(aff, a.case)
        finally:
            aff.__exit__(None, None, None)
    os.makedirs(AB, exist_ok=True)
    prepare_photo(a.photo)
    aff = None if a.infinite_only else connect()
    try:
        if a.cmd in ("run", "facts") and aff is not None:
            facts(aff)
        if a.cmd in ("run", "ab"):
            return ab(a.app, a.op, a.set_baseline, aff)
    finally:
        if aff is not None:
            aff.__exit__(None, None, None)
    return 0


if __name__ == "__main__":
    sys.exit(main())
