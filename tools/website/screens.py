#!/usr/bin/env python3
"""Screenshots of the website for design review: every public page at phone, tablet and desktop, in each mode,
with contact sheets per viewport and a before/after compare.

  screens.py shoot OUT [--page index.html ...] [--viewport phone ...] [--mode default|midnight|paper] [--fold] [--all]
  screens.py sheet OUT                      contact sheets OUT/sheet-<viewport>.png (one tile per page, top of page)
  screens.py compare BEFORE AFTER OUT       side-by-side pairs, ranked by how much changed; OUT/compare.html to browse

Shots are full page (capped at 6 screens) unless --fold. Motion is frozen: reveals shown, animations paused at their
end state, so a shot is the page as it reads, not a frame of a transition. Uses Playwright and Pillow; set CHROME_PATH
to use an installed Chrome. Pages, server and viewports come from tools/hygiene/common.py, the same set the checks use.
"""
from __future__ import annotations
import argparse, html, json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "hygiene"))
import common  # noqa: E402

FREEZE = """
.reveal, .reveal:not(.in) { opacity: 1 !important; transform: none !important; }
*, *::before, *::after { animation-delay: -1ms !important; animation-duration: 1ms !important; animation-iteration-count: 1 !important;
  transition-duration: 0ms !important; transition-delay: 0ms !important; caret-color: transparent !important; }
"""
MAX_SCREENS = 6


def name(rel, vp, mode):
    return "%s--%s--%s.png" % (rel[:-5].replace("/", "__"), vp, mode)


def shoot(a):
    from playwright.sync_api import sync_playwright
    os.makedirs(a.out, exist_ok=True)
    pages = a.page or common.pages(a.all)
    srv, base = common.serve()
    index = []
    with sync_playwright() as p:
        b = p.chromium.launch(executable_path=common.CHROME)
        for vp in a.viewport or list(common.VIEWPORTS):
            w, h = common.VIEWPORTS[vp]
            for mode in a.mode or ["default"]:
                ctx = b.new_context(viewport={"width": w, "height": h}, device_scale_factor=1, reduced_motion="no-preference",
                                    color_scheme="light" if mode == "paper" else "dark")
                if mode != "default":  # default: the page's own mode (Midnight, or Paper where a page asks for it)
                    ctx.add_init_script("try{localStorage.setItem('infinite-mode',%s)}catch(e){}" % json.dumps(mode))
                for rel in pages:
                    pg = ctx.new_page()
                    try:
                        pg.goto(base + rel, wait_until="load", timeout=25000)
                        pg.add_style_tag(content=FREEZE)
                        pg.evaluate("document.fonts && document.fonts.ready")
                        pg.wait_for_timeout(600)
                        full_h = pg.evaluate("document.documentElement.scrollHeight")
                        clip_h = h if a.fold else min(full_h, h * MAX_SCREENS)
                        f = os.path.join(a.out, name(rel, vp, mode))
                        pg.screenshot(path=f, clip={"x": 0, "y": 0, "width": w, "height": clip_h}, full_page=not a.fold)
                        index.append({"page": rel, "vp": vp, "mode": mode, "file": os.path.basename(f), "h": full_h})
                        print("shot", rel, vp, mode, full_h)
                    except Exception as ex:
                        print("FAIL", rel, vp, mode, str(ex)[:100])
                    pg.close()
                ctx.close()
        b.close()
    srv.shutdown()
    json.dump(index, open(os.path.join(a.out, "index.json"), "w"), indent=1)


def sheet(a):
    from PIL import Image, ImageDraw
    idx = json.load(open(os.path.join(a.out, "index.json")))
    for vp in sorted({r["vp"] for r in idx}):
        rows = [r for r in idx if r["vp"] == vp]
        tw = 360 if vp != "phone" else 220
        tiles = []
        for r in rows:
            im = Image.open(os.path.join(a.out, r["file"])).convert("RGB")
            th = int(im.width * (1.6 if vp == "phone" else 0.9))
            im = im.crop((0, 0, im.width, min(th, im.height))).resize((tw, int(tw * min(th, im.height) / im.width)))
            tiles.append((r, im))
        cols = 6 if vp == "phone" else 4
        th = max(t[1].height for t in tiles) + 24
        W = cols * (tw + 12) + 12
        H = ((len(tiles) + cols - 1) // cols) * (th + 12) + 12
        S = Image.new("RGB", (W, H), (21, 25, 48))
        d = ImageDraw.Draw(S)
        for i, (r, im) in enumerate(tiles):
            x = 12 + (i % cols) * (tw + 12); y = 12 + (i // cols) * (th + 12)
            S.paste(im, (x, y + 20)); d.text((x, y + 4), "%s %s" % (r["page"], r["mode"]), fill=(238, 241, 250))
        f = os.path.join(a.out, "sheet-%s.png" % vp); S.save(f); print("sheet", f, len(tiles))


def compare(a):
    from PIL import Image, ImageChops
    os.makedirs(a.out, exist_ok=True)
    before = {r["file"]: r for r in json.load(open(os.path.join(a.before, "index.json")))}
    after = {r["file"]: r for r in json.load(open(os.path.join(a.after, "index.json")))}
    rows = []
    for f in sorted(set(before) & set(after)):
        A = Image.open(os.path.join(a.before, f)).convert("RGB"); B = Image.open(os.path.join(a.after, f)).convert("RGB")
        h = min(A.height, B.height); w = min(A.width, B.width)
        diff = ImageChops.difference(A.crop((0, 0, w, h)), B.crop((0, 0, w, h))).convert("L").point(lambda v: 255 if v > 24 else 0)
        changed = sum(diff.histogram()[255:]) / float(w * h)
        pair = Image.new("RGB", (A.width + B.width + 16, max(A.height, B.height)), (21, 25, 48))
        pair.paste(A, (0, 0)); pair.paste(B, (A.width + 16, 0))
        scale = 900.0 / pair.height if pair.height > 900 else 1
        pair = pair.resize((int(pair.width * scale), int(pair.height * scale)))
        out = "cmp-" + f.replace(".png", ".jpg"); pair.save(os.path.join(a.out, out), quality=82)
        rows.append((changed, f, out, before[f]["h"], after[f]["h"]))
    rows.sort(reverse=True)
    with open(os.path.join(a.out, "compare.html"), "w") as o:
        o.write("<!doctype html><meta charset=utf-8><title>Compare</title><style>body{background:#151930;color:#EEF1FA;font:14px Geist,system-ui;margin:24px}"
                "img{max-width:100%;border-radius:12px}h2{font-size:14px;margin:32px 0 8px}</style><h1>Before / after</h1>")
        for c, f, out, hb, ha in rows:
            o.write("<h2>%s: %.1f%% changed, height %d to %d</h2><img loading=lazy src='%s'>" % (html.escape(f), c * 100, hb, ha, out))
    for c, f, _, hb, ha in rows:
        print("%5.1f%%  %s  h %d->%d" % (c * 100, f, hb, ha))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("shoot"); s.add_argument("out"); s.add_argument("--page", action="append"); s.add_argument("--all", action="store_true")
    s.add_argument("--viewport", action="append", choices=list(common.VIEWPORTS)); s.add_argument("--mode", action="append", choices=["default", "midnight", "paper"])
    s.add_argument("--fold", action="store_true", help="first screen only")
    s = sp.add_parser("sheet"); s.add_argument("out")
    s = sp.add_parser("compare"); s.add_argument("before"); s.add_argument("after"); s.add_argument("out")
    a = ap.parse_args()
    {"shoot": shoot, "sheet": sheet, "compare": compare}[a.cmd](a)


if __name__ == "__main__":
    main()
