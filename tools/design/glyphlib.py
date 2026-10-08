"""Shared SVG-subset -> outline geometry for the Infinite Glyphs pipeline.

Needs: pip install skia-pathops fonttools  (build-time only; the TTF is committed).
Subset: path, circle, rect(rx), line, polyline. Elements with fill="currentColor" are filled (and stroked unless
stroke="none"); everything else is stroked. No transforms, no text.
"""
import pathlib, xml.etree.ElementTree as ET
import pathops
from fontTools.svgLib.path import parse_path

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "art/icons/src"
NS = "{http://www.w3.org/2000/svg}"
GRID, PAD, STROKE = 20.0, 2.0, 1.5

def _path_d(el):
    t = el.tag.replace(NS, "")
    g = lambda k, d=0.0: float(el.get(k, d))
    if t == "path": return el.get("d")
    if t == "circle":
        cx, cy, r = g("cx"), g("cy"), g("r")
        return f"M{cx-r} {cy}A{r} {r} 0 1 0 {cx+r} {cy}A{r} {r} 0 1 0 {cx-r} {cy}Z"
    if t == "rect":
        x, y, w, h, r = g("x"), g("y"), g("width"), g("height"), g("rx")
        return (f"M{x+r} {y}H{x+w-r}A{r} {r} 0 0 1 {x+w} {y+r}V{y+h-r}A{r} {r} 0 0 1 {x+w-r} {y+h}"
                f"H{x+r}A{r} {r} 0 0 1 {x} {y+h-r}V{y+r}A{r} {r} 0 0 1 {x+r} {y}Z")
    if t == "line": return f"M{g('x1')} {g('y1')}L{g('x2')} {g('y2')}"
    if t == "polyline":
        p = [float(v) for v in el.get("points").replace(",", " ").split()]
        return "M" + "L".join(f"{p[i]} {p[i+1]}" for i in range(0, len(p), 2))
    raise ValueError(f"unsupported element <{t}>")

def _mk(d):
    p = pathops.Path(); parse_path(d, p.getPen()); return p

def outline(svg_file):
    """Returns (pathops.Path in SVG coordinates, problems[]) with strokes outlined and everything unioned."""
    root = ET.parse(svg_file).getroot(); bad = []
    if root.get("viewBox") != "0 0 20 20": bad.append("viewBox must be 0 0 20 20")
    caps = {"round": pathops.LineCap.ROUND_CAP}; joins = {"round": pathops.LineJoin.ROUND_JOIN}
    if root.get("stroke-width") != str(STROKE).rstrip("0").rstrip(".") and root.get("stroke-width") != "1.5": bad.append("stroke-width must be 1.5")
    if root.get("stroke-linecap") != "round" or root.get("stroke-linejoin") != "round": bad.append("caps and joins must be round")
    parts = []
    for el in root.iter():
        t = el.tag.replace(NS, "")
        if t in ("svg", "title", "desc"): continue
        if el.get("transform"): bad.append(f"<{t}> has a transform")
        try: d = _path_d(el)
        except ValueError as e: bad.append(str(e)); continue
        filled = el.get("fill") == "currentColor"
        if filled:
            f = _mk(d); f.convertConicsToQuads(); parts.append(f)
        if el.get("stroke") != "none":
            s = _mk(d)
            s.stroke(STROKE, pathops.LineCap.ROUND_CAP, pathops.LineJoin.ROUND_JOIN, 4)
            s.convertConicsToQuads()
            parts.append(s)
    if not parts: bad.append("empty icon"); return pathops.Path(), bad
    res = pathops.op(parts[0], parts[0], pathops.PathOp.UNION)
    for p in parts[1:]: res = pathops.op(res, p, pathops.PathOp.UNION)
    x0, y0, x1, y1 = res.bounds
    eps = 0.02
    if x0 < PAD - eps or y0 < PAD - eps or x1 > GRID - PAD + eps or y1 > GRID - PAD + eps:
        bad.append(f"outside live area 2..18: bounds {x0:.2f},{y0:.2f} {x1:.2f},{y1:.2f}")
    return res, bad

def icons():
    return sorted(SRC.glob("*-20.svg"))
