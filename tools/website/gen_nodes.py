#!/usr/bin/env python3
"""Writes one page for every node in the marquee: website/nodes/<slug>.html, the index website/nodes/index.html, an example patch
website/assets/nodes/patches/<slug>.inf, a picture, and website/assets/nodes/index.json (what the marquee dialog reads).

Sources, all in the repository:
  engine   tools/website/data/node_types.json   the engine's own `Infinite --describe` (category, inputs, outputs, controls with ranges).
                                                Refresh with: python3 tools/website/gen_nodes.py --refresh
  words    tools/manual/cards_base.json, cards.py, cards_v06.py   the Node Field Guide's text: description, try-it, chain.
           tools/website/data/pdf_cards.json    cards only the PDF has (audio effects, 3D basics), read from assets/The_Node_Field_Guide.pdf.
  picture  docs/node_screenshots/*.png          zoomed captures of the node in the running app, shrunk to webp.
  list     website/nodes.js                      NODE_LIBRARY, the names in the marquee.
Each patch is built from the node's own inputs and outputs (a source for every input, a sink for the output) and must pass the engine's
strict --validate; a node whose patch fails gets no download rather than a broken one.
Usage: python3 tools/website/gen_nodes.py [--refresh]   then sync_chrome.py, gen_seo.py and gen_search.py (gen_seo calls this).
"""
import html, json, os, re, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W = os.path.join(ROOT, "website")
DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
BIN = os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite")
BASE = "https://infinitedevices.org/"
sys.path.insert(0, os.path.join(ROOT, "tools", "manual"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
e = html.escape

CAT_LABEL = {"AudioEffects": "Audio effects", "3D": "3D", "Source": "Source", "Compositing": "Compositing", "Effects": "Effects", "Modulators": "Modulators",
             "Prediction": "Prediction", "Macros": "Macros", "Utility": "Utility", "Notes": "Notes", "Synths": "Synths"}
CAT_COLOR = {"Source": "#4ADE80", "3D": "#38BDF8", "Compositing": "#818CF8", "Effects": "#FACC15", "Modulators": "#A3E635", "Prediction": "#22D388",
             "Macros": "#FC963C", "Utility": "#A3A9BA", "Notes": "#4CD964", "Synths": "#6992F6", "AudioEffects": "#53C7E3"}
CAT_BLURB = {
    "Source": "a source node: it makes a picture from nothing, so it sits at the start of a visual chain",
    "3D": "a 3D node: it builds, changes or lights geometry that Render 3D turns into a picture",
    "Compositing": "a compositing node: it layers, masks and routes pictures",
    "Effects": "an image effect: it changes the picture that flows through it",
    "Modulators": "a modulator: it makes a moving value that can drive any control on the canvas",
    "Prediction": "a prediction node: it guesses where a value or a phrase goes next and drifts around that guess",
    "Macros": "a macro control: a knob, slider or pad you play by hand and wire to many things",
    "Utility": "a utility node: it routes signals in and out of Infinite or sets up the canvas",
    "Notes": "a note node: it makes, filters or changes notes before a synth plays them",
    "Synths": "a synth: it turns notes into sound",
    "AudioEffects": "an audio effect: it changes the sound that flows through it",
}
# marquee names the engine stores in lower case, and the words a person would say
PRETTY = {"gaussianblur": "Gaussian Blur", "boxblur": "Box Blur", "motionblur": "Motion Blur", "radialblur": "Radial Blur", "unsharpmask": "Unsharp Mask",
          "twirl": "Twirl", "pinchpunch": "Pinch Punch", "ripple": "Ripple", "pixelate": "Pixelate", "addnoise": "Add Noise", "vignette": "Vignette",
          "transform": "Transform", "invert": "Invert", "posterize": "Posterize", "threshold": "Threshold", "exposure": "Exposure", "opacity": "Opacity",
          "premultiply": "Premultiply", "bloom": "Bloom", "diffuseglow": "Diffuse Glow", "glitch": "Glitch", "lensdistortion": "Lens Distortion",
          "displace": "Displace", "liquify": "Liquify", "symmetry": "Symmetry", "kaleidoscope": "Kaleidoscope", "crop": "Crop", "emboss": "Emboss",
          "convolve": "Convolve", "lookup": "Lookup", "halftone": "Halftone", "lut": "LUT", "gradientmap": "Gradient Map", "outerglow": "Outer Glow",
          "coloroverlay": "Color Overlay", "dropshadow": "Drop Shadow", "Mirror tile": "Mirror Tile", "Show alpha": "Show Alpha", "Set alpha": "Set Alpha",
          "Alpha invert": "Alpha Invert", "Alpha from luma": "Alpha From Luma", "Alpha levels": "Alpha Levels", "Chroma key": "Chroma Key",
          "Luma key": "Luma Key", "Normal map": "Normal Map", "Edge sobel": "Edge Sobel", "Edge outline": "Edge Outline",
          "Color adjustments": "Color Adjustments", "FieldPixel": "Field Pixel"}
# a marquee name whose Field Guide card is filed under another name
CARD_ALIAS = {"geometry": "geometryprimitives", "bloom": "bloomdiffuseglow", "diffuseglow": "bloomdiffuseglow", "gaussianblur": "gaussianboxblur",
              "boxblur": "gaussianboxblur", "feedback": "feedbacktrails", "trails": "feedbacktrails", "metallic": "metallicresonator",
              "twirl": "twirlpinchpunchripple", "pinchpunch": "twirlpinchpunchripple", "ripple": "twirlpinchpunchripple"}
NORM = lambda s: re.sub(r"[^a-z0-9]", "", s.lower())


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


# ---------------------------------------------------------------- sources
def engine(refresh=False):
    p = os.path.join(DATA, "node_types.json")
    if refresh or not os.path.exists(p):
        r = subprocess.run([BIN, "--describe"], capture_output=True, text=True, timeout=120)
        j = json.loads(r.stdout.strip().split("\n")[-1])
        keep = []
        for t in j["types"]:
            keep.append({"type": t["type"], "category": t["category"], "inputs": t["inputs"], "outputs": t["outputs"],
                         "rows": [{k: x.get(k) for k in ("key", "kind", "default", "label", "min", "max", "step", "options", "ui")} for x in t["rows"]],
                         "modulatable": [{k: m.get(k) for k in ("key", "label", "min", "max")} for m in t["modulatable"]]})
        os.makedirs(DATA, exist_ok=True)
        json.dump(keep, open(p, "w"), indent=0, ensure_ascii=False)
    return {NORM(t["type"]): t for t in json.load(open(p))}


# nodes the engine has but the marquee list (website/nodes.js, a dated census) does not name yet; they get pages like every other node
EXTRA = ["Sketch", "Sketch 3D", "Field Notes", "FM", "String", "Partials", "Convolution Reverb", "Saturator"]


def library():
    s = open(os.path.join(W, "nodes.js"), encoding="utf-8").read()
    return json.loads(re.search(r"NODE_LIBRARY = (\[.*?\]);", s, re.S).group(1))


def guide_cards():
    """{normalised name: (chapter, description, try-it, chain)} from the manual's card sources, then the PDF-only cards."""
    import cards, cards_v06
    base = json.load(open(os.path.join(ROOT, "tools/manual/cards_base.json")))
    rows = [(ch["title"], c) for ch in base for c in ch["cards"]] + [(ch["title"], list(c)) for ch in cards.CHAPTERS + cards_v06.CHAPTERS_V06 for c in ch["cards"]]
    out = {}
    for chapter, c in rows:
        name, tag, desc, tryit, chain = c[0], c[1], c[2], c[3], c[4]
        for part in [name] + re.split(r" / |, | & | \+ |/", name):
            out.setdefault(NORM(part), (chapter, desc, tryit, chain))
    p = os.path.join(DATA, "pdf_cards.json")
    if os.path.exists(p):
        for k, v in json.load(open(p)).items():
            out.setdefault(k, (v["chapter"], v["desc"], v.get("tryit"), v.get("chain")))
    return out


def refresh_pdf_cards():
    """Cards only the PDF has: a node name on its own line with its description under it (the audio effects and 3D basics chapters)."""
    txt = subprocess.run(["pdftotext", "-layout", os.path.join(W, "assets/The_Node_Field_Guide.pdf"), "-"], capture_output=True, text=True, check=True).stdout.split("\n")
    want = {NORM(n) for n, _ in library()}
    have = set(guide_cards())
    found, chapter = {}, ""
    for i, l in enumerate(txt):
        if l.startswith("CHAPTER") and "/" in l:
            chapter = re.split(r"\s{3,}", l)[0].split("/", 1)[-1].strip().title()
        s = l.strip()
        if not s or len(s) > 40 or s.endswith(".") or "→" in s or s.startswith(("TRY", "CHAPTER", "INFINITE")):
            continue
        k = NORM(s)
        if k not in want or k in have or k in found:
            continue
        desc, j = [], i + 1
        while j < len(txt) and txt[j].strip() and not txt[j].strip().startswith(("TRY", "CHAPTER")) and j - i < 8:
            desc.append(txt[j].strip()); j += 1
        d = " ".join(desc)
        if len(d) >= 30 and d.endswith((".", ")")):
            found[k] = {"chapter": chapter, "desc": d}
    json.dump(found, open(os.path.join(DATA, "pdf_cards.json"), "w"), indent=1, ensure_ascii=False)
    print("pdf cards:", len(found))


# ---------------------------------------------------------------- the patch
SRC = {"image": ("Source", "Ramp"), "geometry": ("3D", "Cube"), "audio": ("Synths", "Oscillator"), "note": ("Notes", "Random Note Generator"),
       "modulator": ("Modulators", "LFO"), "camera": ("3D", "Camera"), "light": ("3D", "Light"), "environment": ("3D", "HDRI"),
       "palette": ("Modulators", "Palette")}
WIRE = {"image": "cable", "geometry": "geo", "camera": "geo", "light": "geo", "environment": "geo", "audio": "aud", "note": "note"}


class Patch:
    def __init__(s, title):
        s.title, s.nodes, s.wires, s.n = title, [], [], 0

    def add(s, cat, typ, x, y, ident=None, **kv):
        s.n += 1
        s.nodes.append((s.n, cat, typ, x, y, ident or "n%d" % s.n, kv))
        return s.n

    def text(s):
        out = ["infinite-patch 1", "rev 2", "# " + s.title]
        for n, cat, typ, x, y, ident, kv in s.nodes:
            out += ["node %d %s %s" % (n, cat, typ), "  id %s" % ident, "  pos %d %d" % (x, y)]
            for k, v in kv.items():
                out.append("  %s %s %s" % (v[0], k, v[1]))
            out.append("end")
        return "\n".join(out + s.wires) + "\n"


def build_patch(t, label):
    """Wires `t` between made-up sources and a sink that show what it does. Returns the .inf text, or None if the node cannot be wired."""
    p = Patch("%s: a starting patch for the %s node" % (label, label))
    X, Y = 520, 300
    me = p.add(t["category"], t["type"], X, Y, "node")
    srcs = []
    seen = {}
    for i in t["inputs"][:4]:
        k = i["kind"]
        if k not in SRC:
            continue
        cat, typ = SRC[k]
        if k in ("camera", "light", "environment") and t["type"] != "Render 3D":
            continue
        y = Y + len(srcs) * 290 - (80 if len(t["inputs"]) > 1 else 0)
        if k == "modulator":
            # a modulator feeding a modulator: drive the node's own first control
            sid = p.add(cat, typ, 0, y, "lfo%d" % len(srcs))
            m = t["modulatable"][0]["key"] if t["modulatable"] else None
            if m: p.wires.append("mod node %s lfo%d 0 0 1 0.5" % (m, len(srcs)))
        elif k == "palette":
            continue
        else:
            sid = p.add(cat, typ, 0, y, "in%d" % len(srcs))
            p.wires.append("%s node %d in%d" % (WIRE[k], i["slot"], len(srcs)))
        srcs.append(sid)
    outk = t["outputs"][0]["kind"] if t["outputs"] else None
    sx = X + 560
    if outk == "image":
        p.add("Utility", "Output", sx, Y, "out"); p.wires.append("cable out 0 node")
    elif outk == "geometry":
        p.add("3D", "Render 3D", sx, Y, "view"); p.wires.append("geo view 0 node")
        p.add("Utility", "Output", sx + 560, Y, "out"); p.wires.append("cable out 0 view")
    elif outk == "audio":
        p.add("Utility", "Audio Out", sx, Y, "speakers"); p.wires.append("aud speakers 0 node")
    elif outk == "note":
        p.add("Synths", "Analog", sx, Y, "voice"); p.wires.append("note voice 0 node")
        p.add("Utility", "Audio Out", sx + 560, Y, "speakers"); p.wires.append("aud speakers 0 voice")
    elif outk == "modulator":
        key = "sizeX"
        p.add("Source", "Shape", sx, Y, "shape"); p.wires.append("mod shape %s node 0 0 0.6 0.5" % key)
        p.add("Utility", "Output", sx + 560, Y, "out"); p.wires.append("cable out 0 shape")
    elif outk == "camera":
        p.add("3D", "Cube", sx, Y - 200, "box"); p.add("3D", "Render 3D", sx, Y, "view")
        p.wires += ["geo view 0 box", "geo view 4 node"]
        p.add("Utility", "Output", sx + 560, Y, "out"); p.wires.append("cable out 0 view")
    elif outk in ("light", "environment"):
        p.add("3D", "Cube", sx, Y - 200, "box"); p.add("3D", "Render 3D", sx, Y, "view")
        p.wires += ["geo view 0 box", ("geo view 5 node" if outk == "light" else "cable view 8 node")]
        p.add("Utility", "Output", sx + 560, Y, "out"); p.wires.append("cable out 0 view")
    elif outk == "palette":
        p.add("Source", "Shape", sx, Y, "shape"); p.wires.append("pal shape 0 node 0")
        p.add("Utility", "Output", sx + 560, Y, "out"); p.wires.append("cable out 0 shape")
    elif outk == "predictor":
        p.add("Prediction", "Predictive Modulator", sx, Y, "guess"); p.wires.append("mod guess constantIn node 0 0 1 0.5")
    return p.text()


def validate(text):
    with tempfile.NamedTemporaryFile("w", suffix=".inf", delete=False) as f:
        f.write(text)
    try:
        r = subprocess.run([BIN, "--validate", f.name], capture_output=True, text=True, timeout=120, stdin=subprocess.DEVNULL)
        j = json.loads(r.stdout.strip().split("\n")[-1])
        return r.returncode == 0 and j.get("ok"), "; ".join("%s %s" % (x.get("code"), x.get("message")) for x in j.get("errors", []))[:200]
    except Exception as x:
        return False, str(x)[:120]
    finally:
        os.unlink(f.name)


# ---------------------------------------------------------------- the pages
def kind_words(items):
    names = {"image": "picture", "geometry": "3D shape", "audio": "audio", "note": "notes", "modulator": "control signal", "camera": "camera",
             "light": "light", "environment": "environment", "palette": "colour palette", "predictor": "prediction"}
    out = []
    for i in items:
        w = names.get(i["kind"], i["kind"])
        lab = (i.get("label") or i.get("name") or "").replace("_", " ").strip()
        out.append((w, lab))
    return out


def io_phrase(items, verb):
    if not items:
        return "It has no %s: it starts a chain." % ("input" if verb == "in" else "output") if verb == "in" else "It has no output: it ends a chain."
    ks = []
    for w, _ in kind_words(items):
        if w not in ks: ks.append(w)
    return ", ".join(ks[:-1]) + (" and " if len(ks) > 1 else "") + ks[-1]


def controls(t):
    rows = []
    for r in t["rows"]:
        if not r.get("ui") or not r.get("label"):
            continue
        lab = r["label"]
        if r.get("options"):
            rng = ", ".join(str(o) for o in r["options"][:8]) + (" …" if len(r["options"]) > 8 else "")
        elif r.get("kind") == "bool":
            rng = "on or off"
        elif r.get("min") is not None and r.get("max") is not None and r["kind"] in ("float", "int"):
            f = lambda v: ("%g" % round(v, 3))
            rng = "%s to %s" % (f(r["min"]), f(r["max"]))
        else:
            rng = {"color": "a colour", "text": "text"}.get(r.get("kind"), "")
        d = r.get("default")
        if r.get("kind") == "bool": d = "on" if str(d) in ("1", "true") else "off"
        rows.append((lab, rng, "" if d in (None, "") else str(d)))
    return rows[:24]


def first_sentence(d):
    m = re.match(r"(.+?[.!?])(\s|$)", d)
    return m.group(1) if m else d


def title_for(name):
    for t in ("%s node: controls, inputs and a free patch" % name, "%s node: controls and a free patch" % name, "%s node for Infinite" % name):
        if 15 <= len(t) <= 70:
            return t
    return name + " node"


def meta_for(name, cat, desc, ncontrols):
    tail = " A free %s node in Infinite, with its controls and an example patch." % CAT_LABEL.get(cat, cat).lower()
    s = first_sentence(desc)
    for c in (s + tail, s + " A free Infinite node with its controls and an example patch.", s + " A free Infinite node with a patch.", s):
        if 70 <= len(c) <= 160:
            return c
    c = (s + " " + tail)[:157].rsplit(" ", 1)[0].rstrip(",;:") + "..."
    return c if len(c) >= 70 else ("%s is %s in Infinite. Read what it does and download a free example patch." % (name, CAT_BLURB.get(cat, "a node")))[:160]


def fallback_desc(name, t, ctr):
    cat = t["category"]
    parts = ["%s is %s." % (name, CAT_BLURB.get(cat, "a node").replace("a source node", "a source node", 1))]
    ins, outs = t["inputs"], t["outputs"]
    if ins and outs:
        parts.append("It takes %s in and gives %s out." % (io_phrase(ins, "in"), io_phrase(outs, "out")))
    elif outs:
        parts.append("It has no input and gives %s out." % io_phrase(outs, "out"))
    elif ins:
        parts.append("It takes %s in and ends the chain." % io_phrase(ins, "in"))
    if ctr:
        parts.append("Its controls include %s." % ", ".join(c[0].lower() for c in ctr[:4]))
    return " ".join(parts)


def pdf_pages():
    """{normalised node name: page} for The_Node_Field_Guide.pdf, found by the description text."""
    pages = {}
    try:
        txt = subprocess.run(["pdftotext", "-layout", os.path.join(W, "assets/The_Node_Field_Guide.pdf"), "-"], capture_output=True, text=True, check=True).stdout.split("\f")
    except Exception:
        return pages
    return [re.sub(r"\s+", " ", t) for t in txt]


def build():
    refresh = "--refresh" in sys.argv
    if refresh or not os.path.exists(os.path.join(DATA, "pdf_cards.json")):
        refresh_pdf_cards()
    types, cards = engine(refresh), guide_cards()
    pages_txt = pdf_pages()
    lib = library() + [(n, types[NORM(n)]["category"]) for n in EXTRA if NORM(n) in types]
    items = []
    for name, cat in lib:
        t = types[NORM(name)]
        disp = PRETTY.get(name, name)
        card = cards.get(NORM(name)) or cards.get(CARD_ALIAS.get(NORM(name), "-")) or cards.get(NORM(disp))
        ctr = controls(t)
        desc = card[1].strip() if card else fallback_desc(disp, t, ctr)
        items.append({"name": disp, "type": t["type"], "slug": slug(disp), "cat": cat, "t": t, "ctr": ctr, "desc": desc, "card": card, "guide": bool(card)})
    # one slug per node
    seen = {}
    for it in items:
        if it["slug"] in seen:
            it["slug"] += "-" + slug(it["cat"])
        seen[it["slug"]] = it
    os.makedirs(os.path.join(W, "nodes"), exist_ok=True)
    os.makedirs(os.path.join(W, "assets/nodes/patches"), exist_ok=True)
    byslug = {it["slug"]: it for it in items}
    bynorm = {NORM(it["name"]): it for it in items}
    bynorm.update({NORM(it["type"]): it for it in items})
    shots_ok = shots(items)
    bad = []
    for it in items:
        txt = build_patch(it["t"], it["name"])
        ok, why = validate(txt) if txt else (False, "no patch")
        it["patch"] = txt if ok else None
        if ok:
            open(os.path.join(W, "assets/nodes/patches", it["slug"] + ".inf"), "w", encoding="utf-8").write(txt)
        else:
            bad.append((it["name"], why))
            p = os.path.join(W, "assets/nodes/patches", it["slug"] + ".inf")
            if os.path.exists(p): os.remove(p)
        it["page"] = None
        if pages_txt and it["card"]:
            key = re.sub(r"\s+", " ", it["card"][1])[:50]
            for n, t in enumerate(pages_txt, 1):
                if key in t:
                    it["page"] = n; break
    for f in os.listdir(os.path.join(W, "nodes")):
        os.remove(os.path.join(W, "nodes", f))
    for it in items:
        open(os.path.join(W, "nodes", it["slug"] + ".html"), "w", encoding="utf-8").write(page(it, items, bynorm))
    open(os.path.join(W, "nodes", "index.html"), "w", encoding="utf-8").write(index_page(items))
    json.dump({"nodes": {it["name"]: {"s": it["slug"], "d": first_sentence(it["desc"]) if len(it["desc"]) > 220 else it["desc"], "p": bool(it["patch"])} for it in items}},
              open(os.path.join(W, "assets/nodes/index.json"), "w"), ensure_ascii=False, separators=(",", ":"))
    print("%d node pages; %d with a Field Guide card; %d patches validated; %d without a patch" % (len(items), sum(i["guide"] for i in items), sum(bool(i["patch"]) for i in items), len(bad)))
    for n, w in bad: print("  no patch:", n, "-", w)
    return items


def shots(items):
    """Picture per node: website/assets/nodes/hd/<file slug>.webp, made from docs/node_screenshots when missing."""
    from PIL import Image
    src = os.path.join(ROOT, "docs", "node_screenshots")
    files = {NORM(f[:-4]): f for f in os.listdir(src) if f.endswith(".png")}
    out = os.path.join(W, "assets/nodes/hd")
    for it in items:
        f = files.get(NORM(it["type"])) or files.get(NORM(it["name"]))
        if not f:
            it["shot"] = None; continue
        fs = re.sub(r"[^a-z0-9]+", "-", f[:-4].lower()).strip("-")
        dst = os.path.join(out, fs + ".webp")
        if not os.path.exists(dst):
            im = Image.open(os.path.join(src, f)).convert("RGBA")
            if im.size[1] > 720: im = im.resize((round(im.size[0] * 720 / im.size[1]), 720), Image.LANCZOS)
            im.save(dst, "WEBP", quality=82, method=6)
        im = Image.open(dst)
        it["shot"] = (fs + ".webp", im.size[0], im.size[1])
        rg = os.path.join(W, "assets/nodes/ring", fs + ".webp")       # small copy for the ring, so a phone never decodes the big one
        if not os.path.exists(rg):
            os.makedirs(os.path.dirname(rg), exist_ok=True)
            th = im.convert("RGBA")
            th = th.resize((max(1, round(th.size[0] * 560 / th.size[1])), 560), Image.LANCZOS)
            th.save(rg, "WEBP", quality=80, method=6)


def chain_html(it, bynorm, prefix=""):
    ch = it["card"][3] if it["card"] else None
    if not ch:
        return ""
    out = []
    for step in ch:
        base = re.sub(r"\s*\(.*\)$", "", step).strip()
        tgt = bynorm.get(NORM(base))
        lab = e(step)
        out.append('<a class="np-step" href="%s.html">%s</a>' % (tgt["slug"], lab) if tgt else '<span class="np-step">%s</span>' % lab)
    return '<p class="np-chain" aria-label="A chain that uses this node">%s</p>' % '<span class="np-arrow" aria-hidden="true">&rarr;</span>'.join(out)


def head(title, desc, url, image, ld, rel="nodes/x.html", bodyclass="blog-page blog-post np-page"):
    return f"""<!DOCTYPE html>
<html lang="en" data-mode="paper">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#FBFAF6">
  <script>window.INFINITE_DEFAULT_MODE = 'paper';</script>
  <script src="../theme.js?v=2"></script>
  <title>{e(title)}</title>
  <meta name="description" content="{e(desc)}">
  <link rel="canonical" href="{url}">
  <meta property="og:title" content="{e(title)}">
  <meta property="og:description" content="{e(desc)}">
  <meta property="og:image" content="{image}">
  <meta property="og:url" content="{url}">
  <meta property="og:type" content="article">
  <meta property="og:site_name" content="Infinite">
  <meta name="twitter:card" content="summary_large_image">
  <meta name="twitter:title" content="{e(title)}">
  <meta name="twitter:description" content="{e(desc)}">
  <meta name="twitter:image" content="{image}">
  <link rel="alternate" type="text/plain" href="../llms.txt" title="Plain-text summary for language models">
  <link rel="icon" href="../assets/favicon.svg" type="image/svg+xml">
  <link rel="icon" href="../assets/favicon-32.png" sizes="32x32">
  <link rel="apple-touch-icon" href="../assets/apple-touch-icon.png">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Caveat:wght@600;700&family=Geist:wght@400;500;600;700;800&family=Geist+Mono:wght@400;500;600&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="../brand.css?v=36336012">
  <link rel="stylesheet" href="../style.css?v=d046c735">
  <link rel="stylesheet" href="../glass.css?v=c25dae44">
  <link rel="stylesheet" href="../blog.css?v=823c9183">
  <link rel="stylesheet" href="../nodepage.css?v=d007f6ff">
  <script type="application/ld+json">{json.dumps(ld, ensure_ascii=False)}</script>
</head>
<body class="{bodyclass}">
"""


def page(it, items, bynorm):
    import sync_chrome as chrome
    rel = "nodes/%s.html" % it["slug"]
    url = BASE + rel
    name, cat, t = it["name"], it["cat"], it["t"]
    label = CAT_LABEL.get(cat, cat)
    title = title_for(name)
    desc_meta = meta_for(name, cat, it["desc"], len(it["ctr"]))
    shot = it["shot"]
    img_abs = BASE + "assets/nodes/hd/" + shot[0] if shot else BASE + "assets/og-image.jpg"
    ld = [{"@context": "https://schema.org", "@type": "TechArticle", "headline": title, "description": desc_meta, "url": url, "image": img_abs,
           "about": {"@type": "SoftwareApplication", "name": "Infinite", "url": BASE, "applicationCategory": "MultimediaApplication", "operatingSystem": "macOS, Windows, Linux"},
           "author": {"@type": "Person", "name": "Naman Soni", "url": "https://github.com/n1m21n"}, "inLanguage": "en", "isAccessibleForFree": True,
           "articleSection": label, "mainEntityOfPage": url},
          {"@context": "https://schema.org", "@type": "BreadcrumbList", "itemListElement": [
              {"@type": "ListItem", "position": 1, "name": "Infinite", "item": BASE}, {"@type": "ListItem", "position": 2, "name": "Nodes", "item": BASE + "nodes/"},
              {"@type": "ListItem", "position": 3, "name": name, "item": url}]}]
    h = head(title, desc_meta, url, img_abs, ld[0], rel)
    # the second JSON-LD block: breadcrumbs
    h = h.replace("</head>", '  <script type="application/ld+json">%s</script>\n</head>' % json.dumps(ld[1], ensure_ascii=False), 1)
    hdr, ftr = chrome.header_html(rel), chrome.footer_html(rel)
    shotf = ""
    if shot:
        shotf = ('<figure class="np-shot" style="--cat:%s"><img src="../assets/nodes/hd/%s" alt="The %s node in Infinite, showing its %s." width="%d" height="%d" decoding="async"></figure>'
                 % (CAT_COLOR.get(cat, "#818CF8"), shot[0], e(name), "inputs, controls and outputs" if t["inputs"] else "controls and output", shot[1], shot[2]))
    actions = []
    if it["patch"]:
        actions.append('<a class="btn btn-primary" href="../assets/nodes/patches/%s.inf" download>Download the patch</a>' % it["slug"])
    pdf = "../assets/The_Node_Field_Guide.pdf" + ("#page=%d" % it["page"] if it["page"] else "")
    actions.append('<a class="btn btn-ghost" href="%s" target="_blank" rel="noopener">Read it in the Field Guide</a>' % pdf)
    ins = "".join('<li><span class="np-kind" data-kind="%s">%s</span> %s</li>' % (e(w), e(w), e(lab)) for w, lab in kind_words(t["inputs"])) or "<li>None. It starts a chain.</li>"
    outs = "".join('<li><span class="np-kind" data-kind="%s">%s</span> %s</li>' % (e(w), e(w), e(lab)) for w, lab in kind_words(t["outputs"])) or "<li>None. It ends a chain.</li>"
    rows = "".join("<tr><th scope=\"row\">%s</th><td>%s</td><td>%s</td></tr>" % (e(a), e(b), e(c)) for a, b, c in it["ctr"])
    ctr = ('<h2 id="controls">Controls</h2><p>Every control with a range can be driven by a modulator, a note, an audio level or a sketch.</p>'
           '<div class="np-table"><table><thead><tr><th scope="col">Control</th><th scope="col">Range or options</th><th scope="col">Default</th></tr></thead><tbody>%s</tbody></table></div>' % rows) if rows else ""
    tryit = ""
    if it["card"] and it["card"][2]:
        tr = it["card"][2].strip()
        tryit = '<h2 id="try-it">Try it</h2><p>%s%s</p>%s' % (e(tr[0].upper() + tr[1:]), "" if tr.endswith(".") else ".", chain_html(it, bynorm))
    patch = ""
    if it["patch"]:
        patch = ('<h2 id="patch">Example patch</h2><p>This patch wires %s to what it needs and shows the result. Download it, then use File, Open in Infinite, or read it here: it is plain text.</p>'
                 '<pre><code>%s</code></pre>' % (e(name), e(it["patch"].strip())))
    same = [x for x in items if x["cat"] == cat and x is not it]
    k = items.index(it)
    near = sorted(same, key=lambda x: abs(items.index(x) - k))[:8]
    rel_html = "".join('<a class="rel-card" href="%s.html"><span class="mono">%s</span><b>%s</b></a>' % (x["slug"], e(label), e(x["name"])) for x in near)
    guide = "" if it["guide"] else ""
    body = f"""  {hdr}

  <main>
    <article class="post">
      <div class="container post-wrap">
        <nav class="crumbs mono" aria-label="Breadcrumb"><a href="../index.html">Infinite</a> / <a href="./">Nodes</a> / {e(label)}</nav>
        <header class="post-head">
          <p class="post-kicker mono np-cat" style="--cat:{CAT_COLOR.get(cat, '#818CF8')}">{e(label)} node</p>
          <h1>{e(name)}</h1>
          <p class="post-lede">{e(it["desc"])}</p>
          <p class="cta-row">{"".join(actions)}</p>
        </header>
        {shotf}
        <div class="prose">
          <h2 id="what">What it does</h2>
          <p>{e(name)} is {e(CAT_BLURB.get(cat, "a node"))}. {e(io_sentence(t))}</p>
          {tryit}
          <h2 id="io">Inputs and outputs</h2>
          <div class="np-io"><div><h3>Inputs</h3><ul class="np-ports">{ins}</ul></div><div><h3>Outputs</h3><ul class="np-ports">{outs}</ul></div></div>
          {ctr}
          {patch}
        </div>
        <aside class="post-cta" aria-label="Try Infinite">
          <p class="hand">your turn</p>
          <h2>Patch it yourself</h2>
          <p>Infinite is free and open source for macOS, Windows and Linux. Drop {e(name)} on the canvas, wire it to anything and play.</p>
          <p class="cta-row"><a class="btn btn-primary" href="../index.html#download">Download Infinite</a><a class="btn btn-ghost" href="./">All {len(items)} nodes</a></p>
        </aside>
        <section class="post-related" aria-label="More nodes"><h2>More {e(label.lower())} nodes</h2><div class="rel-grid">{rel_html}</div></section>
      </div>
    </article>
  </main>

  {ftr}

  <script src="../site.js?v={chrome.JS_VERSION}" defer></script>
</body>
</html>
"""
    return h + body


def io_sentence(t):
    ins, outs = t["inputs"], t["outputs"]
    if ins and outs:
        return "It takes %s in and gives %s out." % (io_phrase(ins, "in"), io_phrase(outs, "out"))
    if outs:
        return "It has no input and gives %s out, so it starts a chain." % io_phrase(outs, "out")
    return "It takes %s in and ends the chain." % io_phrase(ins, "in")


def _jsv():
    import hashlib
    return hashlib.md5(open(os.path.join(W, "nodebrowser.js"), "rb").read()).hexdigest()[:8]


def index_page(items):
    import sync_chrome as chrome
    rel = "nodes/index.html"
    url = BASE + "nodes/"
    title = "Every Infinite node: %d nodes with controls and patches" % len(items)
    desc = "All %d nodes in Infinite, the free audiovisual workstation: sources, 3D, effects, synths, notes and modulators, each with its controls and a patch." % len(items)
    order = ["Source", "Compositing", "Effects", "3D", "Synths", "AudioEffects", "Notes", "Modulators", "Prediction", "Macros", "Utility"]
    ld = {"@context": "https://schema.org", "@type": "CollectionPage", "name": "Infinite nodes", "url": url, "description": desc, "isPartOf": {"@type": "WebSite", "url": BASE},
          "mainEntity": {"@type": "ItemList", "numberOfItems": len(items), "itemListElement": [
              {"@type": "ListItem", "position": n + 1, "url": BASE + "nodes/" + i["slug"] + ".html", "name": i["name"]} for n, i in enumerate(items)]}}
    h = head(title, desc, url, BASE + "assets/og-image.jpg", ld, rel, "blog-page np-page np-index")
    h = h.replace("</head>", '  <link rel="stylesheet" href="../nodedeck.css">\n</head>', 1)
    secs = []
    for c in order:
        xs = sorted([i for i in items if i["cat"] == c], key=lambda i: i["name"].lower())
        if not xs: continue
        pills = "".join('<a class="np-pill" href="%s.html">%s</a>' % (i["slug"], e(i["name"])) for i in xs)
        secs.append('<section class="np-sec" id="%s" style="--cat:%s"><h2>%s <small class="mono">%d</small></h2><p>%s.</p><div class="np-pills">%s</div></section>'
                    % (slug(c), CAT_COLOR[c], e(CAT_LABEL[c]), len(xs), e(CAT_BLURB[c][0].upper() + CAT_BLURB[c][1:]), pills))
    fams = [c for c in order if any(i["cat"] == c for i in items)]
    SEL_IC = '<svg class="ns-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3.5 5h13M6 10h8M8.5 15h3"/></svg>'
    CHEV = '<svg class="ns-chev" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M6 8l4 4 4-4"/></svg>'
    opts = '<button type="button" class="ns-opt" role="option" data-fam="" aria-selected="true"><span>All</span><small class="mono">%d</small></button>' % len(items) + "".join(
        '<button type="button" class="ns-opt" role="option" data-fam="%s" aria-selected="false"><span>%s</span><small class="mono">%d</small></button>'
        % (slug(c), e(CAT_LABEL[c]), len([i for i in items if i["cat"] == c])) for c in fams)
    chips = ('<div class="ns-sel"><button type="button" class="ns-select" aria-haspopup="listbox" aria-expanded="false" aria-label="Filter by family">%s<span class="ns-select-val">All</span>%s</button>'
             '<div class="ns-menu" role="listbox" aria-label="Family" hidden>%s</div></div>' % (SEL_IC, CHEV, opts))
    cards = []
    for n, i in enumerate(sorted(items, key=lambda i: (order.index(i["cat"]), i["name"].lower()))):
        sh = i["shot"]
        img = ""
        if sh:
            w = max(1, round(sh[1] * 560 / sh[2]))
            img = '<img data-src="../assets/nodes/ring/%s" alt="The %s node in Infinite." width="%d" height="560" decoding="async" draggable="false">' % (sh[0], e(i["name"]), w)
        cards.append('<figure class="nd-card" data-fam="%s" data-title="%s" data-desc="%s" data-href="%s.html" style="--cat:%s"><span class="nd-cat">%s</span>%s</figure>'
                     % (slug(i["cat"]), e(i["name"]), e(i["desc"]), i["slug"], CAT_COLOR[i["cat"]], e(CAT_LABEL[i["cat"]]), img))
    cloud = "".join(cards)
    deck = ('<div class="nd" data-nodedeck><div class="nd-grid" tabindex="0" aria-roledescription="carousel" aria-label="Nodes. Swipe or use the arrow keys.">%s</div>'
            '<div class="nd-hud"><button type="button" class="nd-btn" data-nd="-1" aria-label="Previous node"><svg viewBox="0 0 20 20" aria-hidden="true"><path d="M12.5 4.5 7 10l5.5 5.5"/></svg></button>'
            '<div class="nd-info" aria-live="polite"><a class="nd-t" href="#"></a><span class="nd-d"></span></div>'
            '<button type="button" class="nd-btn" data-nd="1" aria-label="Next node"><svg viewBox="0 0 20 20" aria-hidden="true"><path d="M7.5 4.5 13 10l-5.5 5.5"/></svg></button></div></div>' % cloud)
    body = f"""  {chrome.header_html(rel)}

  <main>
    <div class="container post-wrap np-wrap">
      <nav class="crumbs mono" aria-label="Breadcrumb"><a href="../index.html">Infinite</a> / Nodes</nav>
      <header class="post-head">
        <h1>Every node in Infinite</h1>
      </header>
      <div class="ns" id="ns">
        <div class="ns-bar">{chips}</div>
        {deck}
      </div>
      <noscript><div class="ns-list">{"".join(secs)}</div></noscript>
    </div>
  </main>

  {chrome.footer_html(rel)}

  <script src="../site.js?v={chrome.JS_VERSION}" defer></script>
  <script src="../deck.js?v=5" defer></script>
  <script src="../nodebrowser.js?v={_jsv()}" defer></script>
</body>
</html>
"""
    return h + body


def sitemap_paths(items=None):
    return ["nodes/"] + ["nodes/%s.html" % f[:-5] for f in sorted(os.listdir(os.path.join(W, "nodes"))) if f.endswith(".html") and f != "index.html"]


def llms_lines():
    d = json.load(open(os.path.join(W, "assets/nodes/index.json")))["nodes"]
    return ["- [%s](%snodes/%s.html): %s" % (n, BASE, v["s"], v["d"]) for n, v in sorted(d.items(), key=lambda kv: kv[0].lower())]


if __name__ == "__main__":
    build()
