#!/usr/bin/env python3
"""re-survey affinity_probe: measure Affinity (3.3+) through its own MCP scripting interface and A/B it against
Infinite's 2D View on one real photo.

Black-box only: Affinity's public MCP server (Settings > Model Context Protocol > Enable MCP server, served at
localhost:6767) and the SDK documentation it serves. Nothing in the bundle is read.

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
    env = dict(os.environ, INFINITE_AB2D=AB)
    if only:
        env["INFINITE_AB2D_OP"] = only
    log = os.path.join(AB, "infinite.log")
    # Under a pty: redirected test output is lost when a run is killed on timeout.
    with open(log, "w") as f:
        try:
            subprocess.run(["script", "-q", "/dev/null", exe], env=env, stdout=f, stderr=subprocess.STDOUT, timeout=300)
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
        if os.path.exists(a) and os.path.exists(b):
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
        print("%-15s %6s %8s %9s %9s %6s" % (n, r.get("flip", "-"), r.get("baseline_flip", "-"), r.get("changed_aff", "-"),
                                           r.get("changed_inf", "-"), "yes" if r["proxy"] else ""))
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
    check("every op script closes its document", all("doc.close()" in op_script(o) for o in OPS))
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
    check("capsule polygon closes", "capsulePoly" in JS_PRELUDE and "i <= 64" in JS_PRELUDE)
    print("SELFTEST %s" % ("FAIL" if fails else "OK"))
    return 1 if fails else 0


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
    ap.add_argument("cmd", choices=["run", "facts", "ab"])
    ap.add_argument("--photo", default=DEFAULT_PHOTO)
    ap.add_argument("--app", default=os.path.join(REPO, "build-dev", "Infinite.app"))
    ap.add_argument("--op")
    ap.add_argument("--set-baseline", action="store_true")
    ap.add_argument("--infinite-only", action="store_true", help="reuse the Affinity pictures already in the ab folder")
    a = ap.parse_args()
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
