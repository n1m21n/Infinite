"""Render one message to every distribution channel size, on brand.

What: builds an HTML card per target in docs/brand/brand.json `render.targets` (Open Graph, YouTube thumbnail and
banner, X, LinkedIn, GitHub social + README hero light/dark, Instagram, vertical cover, slide title, Discord) and
screenshots it with headless Chrome. Layouts: hero (text left, image right), banner (inside the platform safe box),
portrait (text top, image under it, inside the vertical safe box). Fonts are the local static Geist/Caveat files in
art/brand/fonts, so renders are offline and repeatable.
Why: every release needs the same message at ~14 sizes; doing it by hand drifted from the brand every time.

Usage:
    python3 tools/brand/channels.py --headline "A DAW for you" [--kicker "INFINITE 0.5"] [--hand "you"]
        [--line "Visuals, music and code on one canvas."] [--image art/brand/3d/renders/node_hero_cutout.png | none]
        [--only og,yt-thumb] [--out art/brand/channels/<slug>] [--html]
    --hand WORD   the one Caveat word in Ember; it must appear in the headline (otherwise an Ember rule is drawn)
    --html        keep the generated .html next to each .png (for tweaking)

Exit codes: 0 rendered, 1 a render failed or came out the wrong size, 2 usage error.
"""
import argparse
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
B = json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json")))
MODES = B["colour"]["modes"]
FONTS = os.path.join(ROOT, "art", "brand", "fonts")
LOGO = os.path.join(ROOT, "art", "brand", "logo", "svg")
CHROME = next((p for p in ("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                           shutil.which("google-chrome") or "", shutil.which("chromium") or "") if p and os.path.exists(p)), None)
E = html.escape


def font_faces():
    faces = [("Geist", 500, "Geist-Medium"), ("Geist", 600, "Geist-SemiBold"), ("Geist", 800, "Geist-ExtraBold"),
             ("Geist Mono", 500, "GeistMono-Medium"), ("Caveat", 700, "Caveat-Bold")]
    return "".join(f'@font-face{{font-family:"{f}";font-weight:{w};src:url("file://{FONTS}/{n}.ttf")}}' for f, w, n in faces)


def mark_svg(mode):
    return open(os.path.join(LOGO, "mark.svg")).read()


def headline_html(text, hand):
    if hand and hand in text:
        a, b = text.split(hand, 1)
        return f'{E(a)}<span class="hand">{E(hand)}<svg class="ul" viewBox="0 0 100 12" preserveAspectRatio="none"><path d="M2 8 Q 30 2 55 6 T 98 5"/></svg></span>{E(b)}'
    return E(text) + '<i class="rule"></i>'


def page(t, a):
    m = MODES[t["mode"]]
    W, H = t["w"], t["h"]
    u = min(W, H) / 100                       # one unit = 1% of the short side
    margin = 0.06 * min(W, H)
    img = f'file://{os.path.abspath(a.image)}' if a.image else ""
    lay = t["layout"]
    if lay == "hero":
        sx, sy, sw, sh = margin, margin, W - 2 * margin, H - 2 * margin
    else:
        sx, sy, sw, sh = t["safe"]
    css = f"""{font_faces()}
*{{box-sizing:border-box;margin:0;padding:0}}
html,body{{width:{W}px;height:{H}px;overflow:hidden;background:{m['ground']};color:{m['ink']};font-family:Geist,sans-serif}}
.img{{position:absolute;background:url("{img}") center/cover no-repeat}}
.safe{{position:absolute;left:{sx}px;top:{sy}px;width:{sw}px;height:{sh}px;display:flex;flex-direction:column}}
.mark{{width:{11 * u:.1f}px;flex:none}} .mark svg{{width:100%;display:block}}
.kicker{{font:500 {2.2 * u:.1f}px/1.2 "Geist Mono";letter-spacing:.12em;text-transform:uppercase;color:{m['ink-2']}}}
h1{{font-weight:800;letter-spacing:-.04em;line-height:.98;color:{m['ink']}}}
.line{{font-weight:500;line-height:1.35;color:{m['ink-2']}}}
.hand{{font-family:Caveat;font-weight:700;color:{m['accent']};letter-spacing:0;font-size:1.18em;position:relative;white-space:nowrap}}
.ul{{position:absolute;left:0;right:0;bottom:-.06em;width:100%;height:.16em;overflow:visible}}
.ul path{{fill:none;stroke:{m['accent']};stroke-width:2.4;stroke-linecap:round;vector-effect:non-scaling-stroke}}
.rule{{display:block;width:{9 * u:.1f}px;height:{0.9 * u:.1f}px;border-radius:99px;background:{m['accent']};margin-top:{2.4 * u:.1f}px}}
"""
    kick = f'<div class="kicker">{E(a.kicker)}</div>' if a.kicker else ""
    line = f'<p class="line">{E(a.line)}</p>' if a.line else ""
    h1 = f'<h1>{headline_html(a.headline, a.hand)}</h1>'
    contain = 'background-size:contain;background-repeat:no-repeat'
    if lay == "hero":
        split = 0.48
        imgel = (f'<div class="img" style="left:{W * 0.40:.0f}px;top:{margin * .5:.0f}px;width:{W * 0.60 - margin * .5:.0f}px;'
                 f'height:{H - margin:.0f}px;{contain};background-position:right center"></div>') if img else ""
        css += f""".safe{{width:{W * split - margin:.0f}px;justify-content:space-between}}
h1{{font-size:{14 * u:.1f}px;white-space:nowrap}} .line{{font-size:{3.4 * u:.1f}px;margin-top:{2.6 * u:.1f}px;max-width:30ch}}
.kicker{{margin-bottom:{2.4 * u:.1f}px}}"""
        body = f'{imgel}<div class="safe"><div class="mark">{mark_svg(t["mode"])}</div><div class="txt">{kick}{h1}{line}</div></div>'
    elif lay == "banner":
        su = sh / 100
        css += f""".safe{{flex-direction:row;align-items:center;gap:{6 * su:.1f}px}}
.mark{{width:{40 * su:.1f}px}} .txt{{flex:none;max-width:{sw * 0.55:.0f}px}} h1{{font-size:{22 * su:.1f}px;white-space:nowrap}}
.line{{font-size:{8 * su:.1f}px;margin-top:{3 * su:.1f}px}} .kicker{{font-size:{6 * su:.1f}px;margin-bottom:{3 * su:.1f}px}} .rule{{display:none}}
.pic{{flex:1;align-self:stretch;min-width:0;background:url("{img}") center/contain no-repeat}}"""
        pic = '<div class="pic"></div>' if img else ""
        body = f'<div class="safe"><div class="mark">{mark_svg(t["mode"])}</div><div class="txt">{kick}{h1}{line}</div>{pic}</div>'
    else:  # portrait
        su = sw / 100
        css += f""".safe{{gap:{4 * su:.1f}px}} .mark{{width:{18 * su:.1f}px}}
h1{{font-size:{15 * su:.1f}px}} .line{{font-size:{4.6 * su:.1f}px;margin-top:{3 * su:.1f}px}}
.kicker{{font-size:{3.2 * su:.1f}px;margin-bottom:{3 * su:.1f}px}}
.pic{{flex:1;min-height:0;background:url("{img}") center/contain no-repeat;margin:0 -{sx:.0f}px 0 0}}"""
        pic = '<div class="pic"></div>' if img else ""
        body = f'<div class="safe"><div class="mark">{mark_svg(t["mode"])}</div><div class="txt">{kick}{h1}{line}</div>{pic}</div>'
    fit = """<script>
// shrink the headline until the text block fits its box (one pass per 2%)
const h=document.querySelector('h1'),box=document.querySelector('.safe'),txt=document.querySelector('.txt');
let fs=parseFloat(getComputedStyle(h).fontSize),n=0;
const over=()=>txt.scrollWidth>txt.clientWidth+1||box.scrollHeight>box.clientHeight+1||h.scrollWidth>h.clientWidth+1;
document.fonts.ready.then(()=>{while(over()&&n++<60){fs*=0.98;h.style.fontSize=fs+'px';}document.body.dataset.done=1;});
</script>"""
    return f'<!doctype html><html><head><meta charset="utf-8"><style>{css}</style></head><body>{body}{fit}</body></html>'


def render(html_path, png, W, H):
    r = subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                        f"--window-size={W},{H}", "--virtual-time-budget=4000", "--allow-file-access-from-files",
                        f"--screenshot={png}", "file://" + html_path], capture_output=True, text=True)
    return r.returncode == 0 and os.path.exists(png) and Image.open(png).size == (W, H)


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--headline")
    ap.add_argument("--kicker", default="")
    ap.add_argument("--hand", default="")
    ap.add_argument("--line", default="")
    ap.add_argument("--image", default=os.path.join(ROOT, "art", "brand", "3d", "renders", "node_hero_cutout.png"))
    ap.add_argument("--only", default="")
    ap.add_argument("--out", default="")
    ap.add_argument("--html", action="store_true")
    ap.add_argument("-h", "--help", action="store_true")
    a, rest = ap.parse_known_args()
    if a.help or rest or not a.headline:
        print(__doc__)
        return 2
    if not CHROME:
        print("needs Google Chrome or Chromium for headless screenshots")
        return 2
    if a.image == "none":
        a.image = ""
    if a.image and not os.path.exists(a.image):
        print(f"no such image: {a.image}")
        return 2
    slug = re.sub(r"[^a-z0-9]+", "-", a.headline.lower()).strip("-")
    out = a.out or os.path.join(ROOT, "art", "brand", "channels", slug)
    os.makedirs(out, exist_ok=True)
    only = set(filter(None, a.only.split(",")))
    targets = [t for t in B["render"]["targets"] if not only or t["id"] in only]
    bad = []
    tmp = tempfile.mkdtemp(prefix="channels-")
    if a.image:  # crop a transparent image to what is visible, so contain-fit fills the space
        im = Image.open(a.image)
        if im.mode == "RGBA":
            bb = im.split()[3].point(lambda v: 255 if v > 8 else 0).getbbox()
            if bb:
                p = int(0.02 * max(im.size))
                bb = (max(0, bb[0] - p), max(0, bb[1] - p), min(im.width, bb[2] + p), min(im.height, bb[3] + p))
                a.image = os.path.join(tmp, "image.png")
                im.crop(bb).save(a.image)
    for t in targets:
        hp = os.path.join(a.html and out or tmp, f"{t['id']}.html")
        open(hp, "w").write(page(t, a))
        png = os.path.join(out, f"{t['id']}.png")
        ok = render(hp, png, t["w"], t["h"])
        print(f"{'ok ' if ok else 'BAD'} {t['id']:15} {t['w']}x{t['h']}  {t['name']}")
        if not ok:
            bad.append(t["id"])
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"{len(targets) - len(bad)} of {len(targets)} rendered into {os.path.relpath(out, ROOT)}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
