"""The hygiene checks. Each takes a probe record and returns Findings. Values come from docs/brand/brand.json.

Families: platform (images and media fit phone and desktop), padding, spacing, typography, buttons (size, pill radius, equal weight), layout (gutters, overflow, grid,
alignment), tokens (radius, motion, borders), density (less information, more UI), loading (weight, images, LCP/CLS).
"""
from __future__ import annotations
import re
from collections import defaultdict
from common import Finding, brand

B = brand()
SPACE = set(B["shape"]["space"]) | {0, 2, 4, 6, 10, 20, 40, 56, 80}      # the brand scale plus 4 px steps used by the grid
DUR = {0, 83, 125, 167, 188, 333, 500, 667}
for k, v in B["motion"]["durations_ms"].items():
    DUR.add(v)
for k, v in B["motion"]["app_ms"].items():
    DUR.add(v)
EASE_OK = ("cubic-bezier(0.16, 1, 0.3, 1)", "cubic-bezier(0.65, 0, 0.35, 1)", "cubic-bezier(0.7, 0, 0.84, 0)", "linear", "linear(")
RADII = {r["px"] for r in B["shape"]["brand_radius"]} | {20, 12, 8}   # card 24 (20 phone), media 16 (12 phone), chip 10, pill
FAMILIES = [f["name"] for f in B["type"]["families"] if f["name"] != "Pixel 5x7"]
SCALE = [(s["step"], s["min"], s["max"]) for s in B["type"]["web_scale"]]
PROSE_MAX = B["type"]["measure"]["body_ch"][1]
AMERICAN = re.compile(r"\b(color|colors|colored|center|centered|optimiz\w+|behavior|analyz\w+|favorite|gray|license[sd]? (?:the)|customiz\w+|organiz\w+|recogniz\w+|artifact)\b", re.I)


def near(v, allowed, tol=0.6):
    return any(abs(v - a) <= tol for a in allowed)


def num(v):
    m = re.match(r"-?[\d.]+", v or "")
    return float(m.group()) if m else 0.0


def is_box(e):
    return e["bg"] not in ("rgba(0, 0, 0, 0)", "transparent") or e["sh"] != "none"


def is_button(e):
    c = " " + e["cls"] + " "
    return e["t"] == "button" or " btn " in c or " chip " in c or " pill " in c or e["cls"].startswith(("btn", "chip")) or " reel-btn " in c


def is_cardlike(e):
    return is_box(e) and e["w"] > 160 and e["h"] > 80 and num(e["rad"]) >= 10


# ---------------------------------------------------------------- padding
def padding(rec):
    out = []
    for e in rec["probe"]["els"]:
        p = e["pad"]
        if not any(p) or e["t"] in ("html", "body", "td", "th", "textarea", "pre", "code", "input", "svg"):
            continue
        if is_box(e) or is_button(e):
            for side, v in zip("trbl", p):
                if v and not near(v, SPACE):
                    out.append(Finding("padding-off-grid", "warn", e["s"], "padding-%s %gpx is not on the 4px grid" % (side, round(v, 1))))
            if is_button(e) or is_cardlike(e):
                if abs(p[1] - p[3]) > 6:   # up to 6px is an icon sitting on one side
                    out.append(Finding("padding-asymmetric", "error", e["s"], "left %gpx vs right %gpx" % (round(p[3], 1), round(p[1], 1))))
    return out


# ---------------------------------------------------------------- spacing
def spacing(rec):
    out = []
    els = rec["probe"]["els"]
    kids = defaultdict(list)
    for e in els:
        kids[e["par"]].append(e)
    for e in els:
        for g in e["gap"]:
            if g and e["disp"] in ("flex", "grid", "inline-flex", "inline-grid") and not near(g, SPACE):
                out.append(Finding("gap-off-grid", "warn", e["s"], "gap %gpx is not on the 4px grid" % round(g, 1)))
    for par, ks in kids.items():
        flow = [k for k in ks if k["pos"] not in ("absolute", "fixed") and k["disp"] not in ("inline", "contents")]
        # siblings of the same class laid in one row must have equal gaps and equal heights; in one column equal gaps
        groups = defaultdict(list)
        for k in flow:
            groups[k["cls"] or k["t"]].append(k)
        for cls, g in groups.items():
            if len(g) < 3 or not (g[0]["cls"] or g[0]["t"] in ("li", "article", "figure")):
                continue
            g.sort(key=lambda k: (round(k["y"] / 8), k["x"]))
            row = [k for k in g if abs(k["y"] - g[0]["y"]) < 3]
            if len(row) >= 3:
                gaps = [row[i + 1]["x"] - (row[i]["x"] + row[i]["w"]) for i in range(len(row) - 1)]
                if max(gaps) - min(gaps) > 2:
                    out.append(Finding("row-gaps-unequal", "error", g[0]["p"] + " > " + g[0]["s"], "gaps %s px" % ", ".join("%g" % round(x) for x in gaps)))
                if is_cardlike(row[0]) or is_button(row[0]):
                    hs = [k["h"] for k in row]
                    if max(hs) - min(hs) > 2:
                        out.append(Finding("row-heights-unequal", "error", g[0]["s"], "heights %s px" % ", ".join("%g" % round(x) for x in hs)))
    # overlap of in-flow siblings
    for par, ks in kids.items():
        flow = [k for k in ks if k["pos"] in ("static", "relative") and k["disp"] not in ("inline", "contents") and k["h"] > 8 and k["w"] > 8]
        for i in range(len(flow)):
            for j in range(i + 1, min(len(flow), i + 6)):
                a, b = flow[i], flow[j]
                ox = min(a["x"] + a["w"], b["x"] + b["w"]) - max(a["x"], b["x"])
                oy = min(a["y"] + a["h"], b["y"] + b["h"]) - max(a["y"], b["y"])
                if ox > 6 and oy > 6 and not (a["mar"][2] < 0 or b["mar"][0] < 0):
                    out.append(Finding("siblings-overlap", "error", a["s"] + " / " + b["s"], "overlap %gx%g px" % (round(ox), round(oy))))
    return out


# ---------------------------------------------------------------- typography
def typography(rec):
    out = []
    caveat_by_sec = defaultdict(int)
    for e in rec["probe"]["els"]:
        if not e["nown"]:
            continue
        first = e["ff"].split(",")[0].strip().strip("\"'")
        if first not in FAMILIES:
            out.append(Finding("font-family", "error", e["s"], "first family '%s' is not Geist, Geist Mono or Caveat" % first))
        if first == "Caveat":
            caveat_by_sec[e["sec"]] += 1
            if len(e["own"].split()) > 2:
                out.append(Finding("caveat-too-long", "error", e["s"], "hand font on %d words; brand allows one human word" % len(e["own"].split())))
        fs = e["fs"]
        if fs < 11:
            out.append(Finding("font-too-small", "error", e["s"], "%gpx is below the 11px folio minimum" % fs))
        elif first != "Caveat" and not any(lo - 0.6 <= fs <= hi + 0.6 for _, lo, hi in SCALE):
            out.append(Finding("font-off-scale", "warn", e["s"], "%gpx is not a web scale step" % round(fs, 1)))
        if e["t"] in ("p", "li") and e["nown"] > 140 and e["fs"]:
            ch = e["w"] / (e["fs"] * 0.52)
            if ch > PROSE_MAX + 14:
                out.append(Finding("measure-too-wide", "warn", e["s"], "about %d characters a line; brand body measure is up to %d" % (ch, PROSE_MAX)))
        t = e["own"]
        if "—" in t or "–" in t and " – " in t:
            out.append(Finding("em-dash", "error", e["s"], "dash in copy: " + t[:50]))
        if AMERICAN.search(t):
            out.append(Finding("spelling", "warn", e["s"], "American spelling: " + AMERICAN.search(t).group(0)))
        if e["t"] in ("h1", "h2", "h3") and e["tt"] == "none":
            w = t.split()
            if len(w) >= 3 and sum(1 for x in w[1:] if x[:1].isupper() and x.lower() not in ("infinite", "vst3", "midi", "osc", "ndi", "ai", "mcp", "dsp", "cpu", "3d", "ui")) >= 2:
                out.append(Finding("title-case", "warn", e["s"], "heading not in sentence case: " + t[:50]))
    for sec, n in caveat_by_sec.items():
        if n > 1:
            out.append(Finding("caveat-per-section", "warn", "section." + sec.split(" ")[0], "%d hand-font words in one section; brand allows one" % n))
    return out


# ---------------------------------------------------------------- buttons
def buttons(rec):
    out = []
    els = rec["probe"]["els"]
    btns = [e for e in els if is_button(e) and e["h"] > 20 and not e["inpar"] and e["pos"] != "absolute"]
    for e in btns:
        r = num(e["rad"])
        if r < min(e["h"] / 2 - 1, 999) and r < 999 and not e["cls"].startswith("icon"):
            out.append(Finding("button-not-pill", "error", e["s"], "radius %gpx on a %gpx tall button; brand controls are pills" % (r, round(e["h"]))))
        if rec["vp"] == "phone" and (e["h"] < 40 or e["w"] < 40):
            out.append(Finding("touch-target", "warn", e["s"], "%gx%g px; phones need 44 (40 minimum)" % (round(e["w"]), round(e["h"]))))
        if e["pad"][1] and abs(e["pad"][1] - e["pad"][3]) > 1:
            out.append(Finding("button-padding-asymmetric", "error", e["s"], "left/right padding differ"))
        if e["bw"] > 0 and e["bc"] != "rgba(0, 0, 0, 0)":
            out.append(Finding("button-outline", "warn", e["s"], "border %gpx; brand has no outlines" % e["bw"]))
    # buttons sharing a row share a height and a font size; sibling buttons in a pair have equal width
    byp = defaultdict(list)
    for e in btns:
        byp[e["par"]].append(e)
    for par, g in byp.items():
        if len(g) < 2:
            continue
        if max(k["h"] for k in g) - min(k["h"] for k in g) > 2:
            out.append(Finding("buttons-unequal-height", "error", g[0]["p"] + " > " + g[0]["s"], "heights %s px" % ", ".join("%g" % round(k["h"]) for k in g)))
        if max(k["fs"] for k in g) - min(k["fs"] for k in g) > 0.5:
            out.append(Finding("buttons-unequal-text", "warn", g[0]["p"] + " > " + g[0]["s"], "font sizes differ in one row"))
        if len(g) == 2 and abs(g[0]["w"] - g[1]["w"]) > 3 and g[0]["p"].find("actions") >= 0:
            out.append(Finding("button-pair-unequal-width", "warn", g[0]["p"], "%g vs %g px" % (round(g[0]["w"]), round(g[1]["w"]))))
    return out


# ---------------------------------------------------------------- layout
def layout(rec):
    out = []
    pr = rec["probe"]
    if pr["sw"] > pr["w"] + 1:
        out.append(Finding("horizontal-scroll", "error", "page", "scrollWidth %d > viewport %d" % (pr["sw"], pr["w"])))
    gut = {"phone": 16, "tablet": 24, "desktop": 24}[rec["vp"]]
    for e in pr["els"]:
        if " container" in " " + e["cls"] and e["w"] > 200:
            left, right = e["x"], pr["w"] - (e["x"] + e["w"])
            if abs(left - right) > 2 and pr["sw"] <= pr["w"] + 1:
                out.append(Finding("container-off-centre", "error", e["s"], "left %gpx vs right %gpx" % (round(left), round(right))))
            if min(left, right) + e["pad"][3] < gut - 1 and rec["vp"] == "phone":
                out.append(Finding("gutter-too-small", "error", e["s"], "%gpx side gutter; brand phone margin is 16" % round(min(left, right) + e["pad"][3])))
        if e["pos"] not in ("fixed", "absolute") and e["x"] + e["w"] > pr["w"] + 2 and e["w"] > 40 and not e["cls"].startswith("reel") and "reel" not in e["p"] and pr["sw"] > pr["w"] + 1:
            out.append(Finding("overflows-viewport", "error", e["s"], "right edge at %d of %d" % (e["x"] + e["w"], pr["w"])))
    # card rows: equal widths in one row of the same class
    kids = defaultdict(list)
    for e in pr["els"]:
        if is_cardlike(e) and e["pos"] == "static":
            kids[(e["par"], e["cls"])].append(e)
    for (par, cls), g in kids.items():
        row = [k for k in g if abs(k["y"] - g[0]["y"]) < 3]
        if len(row) >= 2 and max(k["w"] for k in row) - min(k["w"] for k in row) > 2 and "reel" not in g[0]["p"]:
            out.append(Finding("cards-unequal-width", "warn", g[0]["s"], "widths %s px" % ", ".join("%g" % round(k["w"]) for k in row)))
    return out


# ---------------------------------------------------------------- tokens
def tokens(rec):
    out = []
    for e in rec["probe"]["els"]:
        r = e["rad"]
        if not r.startswith("0") and "%" not in r:
            v = num(r)
            if v and (is_box(e) or is_button(e)) and not near(v, RADII | {999, 9999, 4, 2, 6, 14, 18}, 0.6):
                out.append(Finding("radius-off-brand", "warn", e["s"], "radius %gpx (brand: 24/20 card, 16/12 media, 10 chip, pill)" % v))
        for name, td, te in (("transition", e["td"], e["te"]), ("animation", e["ad"], e["ae"])):
            ds = [x for x in td.split(",") if x.strip()]
            for d in ds:
                ms = num(d) * (1000 if d.strip().endswith("s") and not d.strip().endswith("ms") else 1)
                if ms and ms < 5000 and not near(ms, DUR, 2):
                    out.append(Finding("duration-off-brand", "warn", e["s"], "%s %gms; brand steps 125/188/333/500/667" % (name, round(ms))))
            for t in re.findall(r"(cubic-bezier\([^)]*\)|ease-in-out|ease-in|ease-out|ease|linear\([^)]*\)|linear|steps\([^)]*\))", te):
                if t in ("ease", "ease-in-out", "ease-in", "ease-out") or (t.startswith("cubic-bezier") and not any(t.replace(" ", "") == x.replace(" ", "") for x in EASE_OK)):
                    if any(num(x) for x in ds):
                        out.append(Finding("easing-off-brand", "warn", e["s"], "%s uses %s; brand easing is ease-out cubic-bezier(0.16, 1, 0.3, 1)" % (name, t)))
    return out


# ---------------------------------------------------------------- density: less information, more UI
def density(rec):
    out = []
    els = rec["probe"]["els"]
    words = 0
    long_p = []
    for e in els:
        if e["t"] in ("p", "li", "dd", "figcaption", "span", "div") and e["nown"] and e["pos"] != "fixed":
            n = len(e["own"].split())
            words += n
            if e["t"] in ("p", "figcaption") and n > 38 and e["fs"] >= 13:
                long_p.append(e)
    for e in long_p:
        out.append(Finding("long-paragraph", "warn", e["s"], "%d words; cut to a line or two (less information, more UI)" % len(e["own"].split())))
    h = max((e["y"] + e["h"] for e in els), default=1)
    if rec["page"] not in ("philosophy.html",) and not rec["page"].startswith("blog/") and words / max(h / 900, 1) > 190:
        out.append(Finding("text-dense-page", "warn", "page", "%d words over %d px (%d per screen)" % (words, h, words / max(h / 900, 1))))
    # sections: intro copy longer than the content it introduces
    for e in els:
        if e["t"] == "p" and (" lead" in " " + e["cls"] or "sec-sub" in e["cls"]) and len(e["own"].split()) > 30:
            out.append(Finding("intro-too-long", "warn", e["s"], "%d words of intro" % len(e["own"].split())))
    return out


# ---------------------------------------------------------------- loading
def loading(rec):
    out = []
    pr = rec["probe"]
    total = sum(rec["sizes"].values())
    if total > 1_800_000:
        out.append(Finding("page-weight", "warn", "page", "%.1f MB transferred (budget 1.8 MB)" % (total / 1e6)))
    for url, n in rec["sizes"].items():
        if n > 400_000 and not url.endswith((".mp4", ".webm", ".pdf")) and "/embeds/" not in url:
            out.append(Finding("heavy-file", "warn", url.split("/", 3)[-1], "%d KB" % (n / 1024)))
    for im in pr["imgs"]:
        name = im["src"].split("/", 3)[-1][:60]
        if not im["vis"]:
            continue
        if not im["wa"]:
            out.append(Finding("img-no-dimensions", "error", name, "no width/height; causes layout shift"))
        if im["nw"] and im["dw"] > im["nw"] * 1.05 and not im["src"].endswith(".svg"):
            out.append(Finding("img-upscaled", "error", name, "shown at %dpx, file is %dpx wide: soft or pixelated" % (im["dw"], im["nw"])))
        if im["nw"] and im["dw"] and im["nw"] > im["dw"] * 2.6 * (2 if rec["vp"] == "phone" else 1) and im["nw"] > 900:
            out.append(Finding("img-oversized", "warn", name, "file %dpx wide for a %dpx slot" % (im["nw"], im["dw"])))
        if im["top"] > 1400 and not im["lazy"]:
            out.append(Finding("img-not-lazy", "warn", name, "below the fold without loading=lazy"))
    ld = rec["load"]
    if ld:
        if ld.get("cls", 0) > 0.1:
            out.append(Finding("layout-shift", "error", "page", "CLS %.2f (budget 0.1)" % ld["cls"]))
        if ld.get("lcp", 0) > 2500:
            out.append(Finding("slow-lcp", "warn", "page", "LCP %d ms locally (budget 2500)" % ld["lcp"]))
    return out


# ---------------------------------------------------------------- platform: do pictures and media fit at this width
def platform(rec):
    out = []
    pr = rec["probe"]
    byi = {e["i"]: e for e in pr["els"]}
    for e in pr["els"]:
        if e["t"] not in ("img", "canvas", "iframe", "video", "svg", "pre", "table", "figure", "textarea"):
            continue
        if e["pos"] in ("fixed",):
            continue
        par = byi.get(e["par"])
        if e["x"] + e["w"] > pr["w"] + 1 or e["x"] < -1:
            clipped = False
            q = par
            while q is not None:
                if q["ov"] in ("auto", "scroll", "hidden", "clip"):
                    clipped = True; break
                q = byi.get(q["par"])
            if not clipped:
                out.append(Finding("media-overflows-screen", "error", e["s"], "spans %d to %d on a %dpx screen" % (e["x"], e["x"] + e["w"], pr["w"])))
        elif par and par["ov"] == "visible" and par["w"] > 120 and e["t"] != "svg" and par["disp"] in ("block", "flex", "grid", "flow-root") and e["x"] + e["w"] > par["x"] + par["w"] + 2 and e["pos"] == "static":
            out.append(Finding("media-wider-than-parent", "error", e["s"], "%gpx in a %gpx parent" % (round(e["w"]), round(par["w"]))))
        if e["t"] in ("img", "iframe", "canvas", "video") and e["w"] > pr["w"] * 0.98 and rec["vp"] == "phone" and e["x"] < 4 and e["w"] < pr["w"] and False:
            pass
    for im in pr["imgs"]:
        if im["vis"] and im["nw"] and im["nh"] and im["dh"] and im["of"] == "fill":
            if abs((im["dw"] / im["dh"]) / (im["nw"] / im["nh"]) - 1) > 0.06:
                out.append(Finding("img-distorted", "error", im["src"].split("/", 3)[-1][:60], "shown %dx%d, file ratio %.2f" % (im["dw"], im["dh"], im["nw"] / im["nh"])))
        if im["vis"] and rec["vp"] == "phone" and im["dw"] < 24 and im["dw"] > 0 and im["nw"] > 200:
            out.append(Finding("img-tiny-on-phone", "warn", im["src"].split("/", 3)[-1][:60], "shown %dpx wide" % im["dw"]))
    return out


FAMILIES_MAP = {"padding": [padding], "spacing": [spacing], "typography": [typography], "buttons": [buttons],
                "layout": [layout], "tokens": [tokens], "density": [density], "loading": [loading], "platform": [platform]}
