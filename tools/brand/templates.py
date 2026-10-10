"""Brand templates: slide deck, release email and press fact sheet, generated from docs/brand/brand.json.

What: writes three HTML templates into docs/brand/templates/ and renders them headless:
    slides.html      1920x1080 deck on brand.css (title, statement, image, two-up, end); arrow keys / click to step;
                     ?print renders every slide as one page each. Rendered to slides.pdf.
    email.html       600 px release email: table layout and inline styles only (mail clients drop <style> and CSS
                     variables), Paper mode, one Ember button. Rendered to email.png for review.
    fact-sheet.html  A4 press fact sheet (what it is, facts, platforms, links, logo use). Rendered to fact-sheet.pdf.
Facts (version, licence, links) are read live: the latest tag from `gh release list` (git tags if gh is missing),
the licence line from LICENSE, links from README.md.
Why: decks, emails and press sheets were hand-made per release and drifted from the brand; these are filled from data.

Usage:
    python3 tools/brand/templates.py [--only slides,email,fact-sheet] [--no-render] [--out docs/brand/templates]
Exit codes: 0 written (and rendered), 1 a render failed, 2 usage error.
"""
import argparse
import html
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
from channels import CHROME, font_faces  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
B = json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json")))
M = B["colour"]["modes"]
LOGO = os.path.join(ROOT, "art", "brand", "logo", "svg")
RENDERS = os.path.join(ROOT, "art", "brand", "3d", "renders")
E = html.escape


def facts():
    tag = ""
    try:
        r = subprocess.run(["gh", "release", "list", "--limit", "1"], cwd=ROOT, capture_output=True, text=True, timeout=20)
        tag = r.stdout.split("\t")[0].strip() if r.returncode == 0 else ""
    except (OSError, subprocess.TimeoutExpired):
        pass
    if not tag:
        r = subprocess.run(["git", "describe", "--tags", "--abbrev=0"], cwd=ROOT, capture_output=True, text=True)
        tag = r.stdout.strip() or "unreleased"
    readme = open(os.path.join(ROOT, "README.md")).read()
    discord = (re.search(r"https://discord\.gg/\w+", readme) or [""])[0]
    lic = open(os.path.join(ROOT, "LICENSE")).readline().strip()
    return {
        "name": "Infinite", "version": tag, "licence": lic, "price": "Free",
        "platforms": "macOS (Apple silicon and Intel), Windows 10/11, Linux (x86-64)",
        "site": "https://n1m21n.github.io/Infinite", "repo": "https://github.com/n1m21n/Infinite", "discord": discord,
    }


def svg(name):
    return open(os.path.join(LOGO, name)).read() if os.path.exists(os.path.join(LOGO, name)) else ""


def img(name):
    p = os.path.join(RENDERS, name)
    return os.path.relpath(p, OUT) if os.path.exists(p) else ""


# ---------------------------------------------------------------------------------------------------------------- slides
def slides(f):
    hero, exploded, kit = img("node_hero_cutout.png"), img("node_exploded.png"), img("kit_components.png")
    return f"""<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Infinite deck</title>
<link rel="stylesheet" href="../brand.css">
<style>{font_faces()}
/* one slide = 1920x1080, scaled to the window; content sits on the 12-column grid with a 96 px margin */
*{{box-sizing:border-box;margin:0}}
html,body{{height:100%;background:#000;overflow:hidden;font-family:Geist,sans-serif}}
.deck{{position:absolute;left:50%;top:50%;width:1920px;height:1080px;transform-origin:center}}
section{{position:absolute;inset:0;padding:96px;display:none;flex-direction:column;background:var(--ground);color:var(--ink)}}
section.on{{display:flex}}
.k{{font:500 28px/1.2 "Geist Mono";letter-spacing:.12em;text-transform:uppercase;color:var(--ink-2)}}
h1{{font-weight:800;font-size:168px;letter-spacing:-.045em;line-height:.95}}
h2{{font-weight:800;font-size:96px;letter-spacing:-.035em;line-height:1}}
p{{font-weight:500;font-size:40px;line-height:1.35;color:var(--ink-2);max-width:28ch}}
.hand{{font-family:Caveat;font-weight:700;color:var(--accent);letter-spacing:0}}
.rule{{width:120px;height:10px;border-radius:99px;background:var(--accent)}}
.mark{{width:140px}} .mark svg{{width:100%;display:block}}
.foot{{margin-top:auto;display:flex;justify-content:space-between;font:500 24px "Geist Mono";color:var(--ink-3)}}
.pic{{flex:1;min-height:0;background:center/contain no-repeat}}
.two{{display:grid;grid-template-columns:1fr 1fr;gap:96px;flex:1;align-items:center}}
@media print{{@page{{size:1920px 1080px;margin:0}} html,body{{background:none;overflow:visible}}
  .deck{{position:static;transform:none!important}} section{{position:relative;display:flex;break-after:page;height:1080px}}}}
body.print .deck{{position:static;transform:none!important}} body.print section{{position:relative;display:flex;height:1080px}}
</style></head><body>
<div class="deck">
<!-- TITLE: mark, kicker, headline with one Caveat word, line -->
<section data-mode="midnight" class="on">
  <div class="mark">{svg("mark.svg")}</div>
  <div style="margin-top:auto"><div class="k">{E(f["name"])} {E(f["version"])}</div>
  <h1 style="margin-top:32px">A DAW for <span class="hand">you</span></h1>
  <p style="margin-top:40px">Visuals, music and code on one canvas. {E(f["price"])}.</p></div>
  <div class="foot"><span>{E(f["site"].replace("https://", ""))}</span><span>01</span></div>
</section>
<!-- STATEMENT: one idea, nothing else -->
<section data-mode="paper">
  <div class="k">The idea</div>
  <h2 style="margin:auto 0;max-width:16ch">Patch anything into anything, and play it live.</h2>
  <div class="rule"></div>
  <div class="foot"><span>Infinite</span><span>02</span></div>
</section>
<!-- IMAGE: one render or screenshot, caption under it -->
<section data-mode="midnight">
  <div class="k">The node</div>
  <div class="pic" style="background-image:url('{hero}');margin:48px 0"></div>
  <p>Every node is a slab: title, body, pins. Knobs turn on the brand springs.</p>
  <div class="foot"><span>Infinite</span><span>03</span></div>
</section>
<!-- TWO-UP: text left, image right -->
<section data-mode="mist">
  <div class="two"><div><div class="k">Inside</div><h2 style="margin-top:32px">Built in layers</h2>
  <p style="margin-top:40px">Ground, surface, controls, light. Nothing floats without a reason.</p></div>
  <div class="pic" style="background-image:url('{exploded}');height:100%"></div></div>
  <div class="foot"><span>Infinite</span><span>04</span></div>
</section>
<!-- END: call to action and links -->
<section data-mode="midnight">
  <div class="mark">{svg("mark.svg")}</div>
  <h2 style="margin-top:auto">Free, open source, on every desktop.</h2>
  <p style="margin-top:40px;max-width:none">{E(f["site"].replace("https://", ""))} &nbsp;·&nbsp; {E(f["repo"].replace("https://", ""))}</p>
  <div class="foot"><span>{E(f["licence"])}</span><span>05</span></div>
</section>
</div>
<script>
const s=[...document.querySelectorAll('section')],d=document.querySelector('.deck');let i=0;
if(location.search.includes('print'))document.body.classList.add('print');
function fit(){{if(document.body.classList.contains('print'))return;const k=Math.min(innerWidth/1920,innerHeight/1080);
  d.style.transform=`translate(-50%,-50%) scale(${{k}})`}}
function go(n){{s[i].classList.remove('on');i=Math.max(0,Math.min(s.length-1,n));s[i].classList.add('on')}}
addEventListener('resize',fit);fit();
addEventListener('keydown',e=>{{if(['ArrowRight',' ','PageDown'].includes(e.key))go(i+1);if(['ArrowLeft','PageUp'].includes(e.key))go(i-1)}});
addEventListener('click',e=>go(e.clientX>innerWidth/2?i+1:i-1));
</script></body></html>"""


# ----------------------------------------------------------------------------------------------------------------- email
def email(f):
    p = M["paper"]
    btn = B["colour"]["anchors"]["ember"]["500"]
    hero = "https://n1m21n.github.io/Infinite/assets/og-image.jpg"
    ff = "Geist,-apple-system,'Segoe UI',Helvetica,Arial,sans-serif"
    return f"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<meta name="color-scheme" content="light"><title>Infinite {E(f["version"])}</title>
<!-- Email: tables + inline styles only. Replace the {{{{ }}}} fields; keep one button. Hero image must be a hosted https URL. -->
</head><body style="margin:0;padding:0;background:{p['deep']};">
<table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="background:{p['deep']};"><tr><td align="center" style="padding:32px 16px;">
<table role="presentation" width="600" cellpadding="0" cellspacing="0" style="width:600px;max-width:100%;background:{p['surface-1']};border-radius:16px;">
<tr><td style="padding:32px 40px 0 40px;font:500 12px/1.2 'Geist Mono',Menlo,monospace;letter-spacing:.12em;text-transform:uppercase;color:{p['ink-3']};">
  Infinite {E(f["version"])}</td></tr>
<tr><td style="padding:12px 40px 0 40px;font:800 40px/1.05 {ff};letter-spacing:-.03em;color:{p['ink']};">{{{{headline}}}}</td></tr>
<tr><td style="padding:16px 40px 0 40px;font:500 17px/1.5 {ff};color:{p['ink-2']};">{{{{one or two sentences: what changed for the user}}}}</td></tr>
<tr><td style="padding:28px 40px 0 40px;"><img src="{hero}" width="520" alt="Infinite {E(f['version'])}" style="display:block;width:100%;height:auto;border:0;border-radius:12px;"></td></tr>
<tr><td style="padding:28px 40px 0 40px;font:500 16px/1.6 {ff};color:{p['ink']};">
  <b style="font-weight:600;">What is new</b><br>&bull; {{{{change one}}}}<br>&bull; {{{{change two}}}}<br>&bull; {{{{change three}}}}</td></tr>
<tr><td style="padding:32px 40px 0 40px;"><table role="presentation" cellpadding="0" cellspacing="0"><tr>
  <td style="background:{btn};border-radius:999px;"><a href="{f['site']}" style="display:inline-block;padding:14px 28px;font:600 16px/1 {ff};color:#FFFFFF;text-decoration:none;">Download {E(f["version"])}</a></td>
</tr></table></td></tr>
<tr><td style="padding:40px;font:500 13px/1.6 {ff};color:{p['ink-3']};">
  {E(f["price"])} and open source ({E(f["licence"])}). <a href="{f['repo']}" style="color:{p['accent-ink']};">GitHub</a> &middot;
  <a href="{f['discord']}" style="color:{p['accent-ink']};">Discord</a> &middot; <a href="{{{{unsubscribe}}}}" style="color:{p['ink-3']};">Unsubscribe</a></td></tr>
</table></td></tr></table></body></html>"""


# ------------------------------------------------------------------------------------------------------------ fact sheet
def fact_sheet(f):
    rows = [("Name", f["name"]), ("Latest release", f["version"]), ("Price", f["price"]), ("Licence", f["licence"]),
            ("Platforms", f["platforms"]), ("Website", f["site"]), ("Source", f["repo"]), ("Community", f["discord"])]
    tr = "".join(f"<tr><th>{E(k)}</th><td>{E(v)}</td></tr>" for k, v in rows if v)
    hero = img("node_hero_cutout.png")
    return f"""<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Infinite fact sheet</title>
<link rel="stylesheet" href="../brand.css"><style>{font_faces()}
@page{{size:A4;margin:0}} *{{box-sizing:border-box;margin:0}}
body{{width:210mm;min-height:297mm;padding:18mm;font-family:Geist,sans-serif;background:var(--ground);color:var(--ink);display:flex;flex-direction:column}}
.top{{display:flex;justify-content:space-between;align-items:center}} .top svg{{height:12mm;width:auto}}
.k{{font:500 8.5pt "Geist Mono";letter-spacing:.12em;text-transform:uppercase;color:var(--ink-3)}}
h1{{font-weight:800;font-size:44pt;letter-spacing:-.04em;line-height:.95;margin-top:14mm}}
.hand{{font-family:Caveat;color:var(--accent);letter-spacing:0}}
.lede{{font-weight:500;font-size:13pt;line-height:1.45;color:var(--ink-2);margin-top:6mm;max-width:120mm}}
.pic{{height:70mm;margin:8mm 0;background:url('{hero}') center/contain no-repeat}}
table{{border-collapse:collapse;width:100%;font-size:10pt}} th,td{{text-align:left;padding:2.6mm 0;border-top:.3mm solid var(--line);vertical-align:top}}
th{{width:42mm;font-weight:600;color:var(--ink-2)}}
.use{{margin-top:auto;display:grid;grid-template-columns:repeat(3,1fr);gap:6mm;font-size:8.5pt;color:var(--ink-2);line-height:1.4}}
.use b{{display:block;color:var(--ink);font-weight:600;margin-bottom:1mm}}
</style></head><body data-mode="paper">
<div class="top">{svg("lockup-on-light.svg")}<span class="k">Press fact sheet · {E(f["version"])}</span></div>
<h1>A DAW for <span class="hand">you</span></h1>
<p class="lede">Infinite is a live instrument and audio-visual workstation: visuals, music and code on one canvas, with modulation that reaches every control.</p>
<div class="pic"></div>
<table>{tr}</table>
<div class="use" style="margin-top:10mm">
<div><b>Logo</b>Use the files in the press kit as they are. Keep clear space of one stroke width x4 around the mark.</div>
<div><b>Colour</b>Midnight #151930 ground, Moon #EEF1FA ink, one Ember accent. No gradients on the mark.</div>
<div><b>Name</b>Always “Infinite”, one word, capital I. Not “Infinite DAW” or “INFINITE”.</div>
</div></body></html>"""


def chrome(args):
    r = subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files",
                        "--virtual-time-budget=4000", "--no-pdf-header-footer"] + args, capture_output=True, text=True)
    return r.returncode == 0


OUT = os.path.join(ROOT, "docs", "brand", "templates")


def main():
    global OUT
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--only", default="")
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--out", default=OUT)
    ap.add_argument("-h", "--help", action="store_true")
    a, rest = ap.parse_known_args()
    if a.help or rest:
        print(__doc__)
        return 2
    OUT = os.path.abspath(a.out)
    os.makedirs(OUT, exist_ok=True)
    f = facts()
    only = set(filter(None, a.only.split(","))) or {"slides", "email", "fact-sheet"}
    makers = {"slides": slides, "email": email, "fact-sheet": fact_sheet}
    bad = []
    for name in ("slides", "email", "fact-sheet"):
        if name not in only:
            continue
        hp = os.path.join(OUT, f"{name}.html")
        open(hp, "w").write(makers[name](f))
        out = ""
        if not a.no_render and CHROME:
            if name == "email":
                out = os.path.join(OUT, "email.png")
                ok = chrome(["--window-size=680,1120", "--force-device-scale-factor=2", f"--screenshot={out}", "file://" + hp])
            else:
                out = os.path.join(OUT, f"{name}.pdf")
                url = "file://" + hp + ("?print" if name == "slides" else "")
                ok = chrome([f"--print-to-pdf={out}", url])
            ok = ok and os.path.exists(out) and os.path.getsize(out) > 1000
            if not ok:
                bad.append(name)
        print(f"{'ok ' if name not in bad else 'BAD'} {os.path.relpath(hp, ROOT)}" + (f"  -> {os.path.basename(out)}" if out else ""))
    print(f"facts: {f['name']} {f['version']}, {f['licence']}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
