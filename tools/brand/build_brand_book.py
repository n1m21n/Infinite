#!/usr/bin/env python3
"""Build docs/brand/brand-book.html from docs/brand/brand.json (swatches, scales) and art/icons/src (glyphs).
The prose rules live in docs/brand/BRAND.md; this page is the visual companion, so it cannot drift from the numbers."""
import base64, glob, html, json, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
B = json.load(open(os.path.join(ROOT, "docs/brand/brand.json")))
C = B["colour"]


def lum(h):
    c = [int(h[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    c = [x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def cr(a, b):
    x, y = sorted([lum(a), lum(b)], reverse=True)
    return (x + 0.05) / (y + 0.05)


def fg(h):
    return "#151930" if cr(h, "#151930") > cr(h, "#FFFFFF") else "#FFFFFF"


def sw(c, big=False):
    h = c["hex"]
    ring = "box-shadow: inset 0 0 0 1px rgba(128,128,128,.25);" if h.upper() in ("#FBFAF6", "#FFFFFF", "#F3F0E7") else ""
    return (f'<div class="sw{" big" if big else ""}" style="background:{h};color:{fg(h)};{ring}"><b>{html.escape(c["name"])}</b>'
            f'<code>{h}</code>' + (f'<span>{html.escape(c["role"])}</span>' if c.get("role") else "") + "</div>")


def icons():
    out = []
    for p in sorted(glob.glob(os.path.join(ROOT, "art/icons/src/*-20.svg"))):
        n = os.path.basename(p)[:-7]
        if n.endswith("-fill"):
            continue
        out.append(f'<figure>{open(p).read().replace("<svg ", "<svg width=28 height=28 ")}<figcaption>{n}</figcaption></figure>')
    return "".join(out)


logo = "data:image/png;base64," + base64.b64encode(open(os.path.join(ROOT, "website/assets/icon-96.png"), "rb").read()).decode()
g = C["gradient"]["stops"]
grad = f"linear-gradient(90deg,{','.join(g)})"
pairs = [("#F5866B", "#151930", "Coral on Midnight"), ("#EEF1FA", "#151930", "Moon on Midnight"), ("#A3AACB", "#151930", "Moon 2 on Midnight"),
         ("#8B6CFF", "#151930", "Violet on Midnight"), ("#1F1D1A", "#FBFAF6", "Ink on Paper"), ("#5A534C", "#FBFAF6", "Ink 2 on Paper"),
         ("#6E6760", "#FBFAF6", "Ink 3 on Paper"), ("#C2593F", "#FBFAF6", "Terracotta on Paper (large only)"), ("#A8472F", "#FBFAF6", "Terracotta ink on Paper")]
contrast = "".join(f'<div class="cp" style="background:{b};color:{f}"><span>Aa</span><small>{n}</small><code>{cr(f, b):.1f}:1</code></div>' for f, b, n in pairs)
radii = "".join(f'<div class="rd"><div style="border-radius:{min(r["px"], 40)}px"></div><b>{r["name"]}</b><code>{"pill" if r["px"] == 999 else str(r["px"]) + " px"}</code><span>{html.escape(r["role"])}</span></div>' for r in B["shape"]["brand"])
types = "".join(f'<tr><td>{s["step"]}</td><td style="font-family:Geist;font-weight:{s["weight"]};letter-spacing:{s["tracking"]};font-size:{"clamp(1.4rem,3vw,2.2rem)" if "Display" in s["step"] or "Heading" in s["step"] else s["size"]}">Connection is the point</td><td><code>{s["size"]} / {s["weight"]}</code></td></tr>' for s in B["type"]["web_scale"])
fams = "".join(f'<div class="fam"><b>{f["name"]}</b><p>{html.escape(f["role"])}</p></div>' for f in B["type"]["families"])
motion = "".join(f'<div class="mo"><b>{m["name"]}</b><code>{html.escape(m["value"])}</code><span>{html.escape(m["role"])}</span></div>' for m in B["motion"]["brand"])
roles = "".join(f'<div class="rl"><i style="background:{r["dark"]}"></i><i style="background:{r["light"]}"></i><b>{r["name"]}</b></div>' for r in C["role"])
cats = "".join(f'<div class="ct" style="--c:{c["hex"]}"><i></i><b>{c["name"]}</b><code>{c["hex"]}</code></div>' for c in C["category"])
card = f'''<article class="ncard"><p class="hand">filters a picture</p><div class="nc"><header><b>Liquid Glass</b><span>Pixel</span></header>
<div class="media" style="background:linear-gradient(135deg,#8B6CFF,#F5866B 55%,#A9CDF1)"></div><p>A frosted, refracting glass pane over whatever is behind it.</p>
<footer><span class="pill">Free</span><span class="act"><i class="pill sm">⧉</i><i class="pill sm lit">⤓</i></span></footer></div></article>'''

page = f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Infinite Brand Book</title>
<link href="https://fonts.googleapis.com/css2?family=Caveat:wght@700&family=Geist:wght@400;500;600;700;800&family=Geist+Mono:wght@400;500&display=swap" rel="stylesheet">
<style>
:root{{--paper:#FBFAF6;--card:#fff;--inner:#F3F0E7;--ink:#1F1D1A;--ink2:#5A534C;--ink3:#6E6760;--tc:#C2593F;--tci:#A8472F;--m:#151930;--m1:#20263F;--m2:#2A3052;--moon:#EEF1FA;--moon2:#A3AACB;--coral:#F5866B;--r:24px;--rm:16px;--gut:clamp(16px,4vw,48px)}}
*{{box-sizing:border-box}}body{{margin:0;background:var(--paper);color:var(--ink);font:400 1rem/1.55 Geist,system-ui,sans-serif;-webkit-font-smoothing:antialiased}}
code{{font:500 .78rem 'Geist Mono',monospace}}main{{max-width:1180px;margin:0 auto;padding:0 var(--gut)}}
.hero{{background:var(--m);color:var(--moon);padding:clamp(48px,9vw,110px) 0 clamp(40px,7vw,80px);position:relative;overflow:hidden}}
.hero:before{{content:"";position:absolute;inset:0;background:radial-gradient(circle at 80% 20%,rgba(139,108,255,.35),transparent 45%),radial-gradient(circle at 10% 90%,rgba(245,134,107,.22),transparent 40%)}}
.hero main{{position:relative}}.hero img{{width:64px;height:64px;border-radius:16px}}
h1{{font-size:clamp(2.6rem,8vw,6rem);font-weight:800;letter-spacing:-.04em;line-height:1;margin:24px 0 8px}}
.hand{{font:700 1.6rem Caveat,cursive;color:var(--coral);margin:0;transform:rotate(-2deg);display:inline-block}}
.lead{{max-width:620px;color:var(--moon2);font-size:1.1rem}}
section{{padding:clamp(40px,6vw,72px) 0;border-top:1px solid rgba(45,35,25,.06)}}section:first-of-type{{border:0}}
h2{{font-size:clamp(1.6rem,3.4vw,2.4rem);font-weight:700;letter-spacing:-.03em;margin:0 0 8px}}
.k{{font:500 .72rem 'Geist Mono',monospace;letter-spacing:.06em;color:var(--tci);text-transform:uppercase}}
p.s{{color:var(--ink2);max-width:680px;margin:0 0 24px}}
.grid{{display:grid;grid-auto-rows:1fr;gap:16px;grid-template-columns:repeat(auto-fill,minmax(210px,1fr))}}
.sw{{border-radius:var(--r);padding:20px;min-height:150px;display:flex;flex-direction:column;gap:4px;justify-content:flex-end}}.sw.big{{min-height:200px}}
.sw b{{font-weight:700}}.sw span{{font-size:.8rem;opacity:.8;line-height:1.4}}
.grad{{height:90px;border-radius:var(--r);background:{grad};margin:16px 0}}
.cp{{border-radius:var(--rm);padding:16px;display:flex;flex-direction:column;gap:2px}}.cp span{{font-size:2rem;font-weight:800}}.cp small{{font-size:.78rem}}
.ct{{background:var(--card);border-radius:var(--rm);padding:12px 14px;display:flex;align-items:center;gap:10px}}.ct i{{width:14px;height:14px;border-radius:50%;background:var(--c)}}.ct code{{margin-left:auto;color:var(--ink3)}}
.rl{{display:flex;align-items:center;gap:6px;background:var(--card);border-radius:var(--rm);padding:10px 14px}}.rl i{{width:18px;height:18px;border-radius:50%}}.rl b{{margin-left:8px;font-weight:600;font-size:.9rem}}
table{{width:100%;border-collapse:collapse}}td{{padding:12px 8px;border-bottom:1px solid rgba(45,35,25,.07);vertical-align:baseline}}td:first-child{{color:var(--ink3);font:500 .78rem 'Geist Mono',monospace;width:110px}}
.fams{{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));margin-bottom:24px}}.fam{{background:var(--card);border-radius:var(--r);padding:24px}}.fam b{{font-size:1.6rem}}.fam:nth-child(2) b{{font-family:'Geist Mono'}}.fam:nth-child(3) b{{font:700 2.1rem Caveat;color:var(--tc)}}.fam:nth-child(4) b{{font:italic 1.9rem 'Instrument Serif',serif}}.fam p{{margin:6px 0 0;color:var(--ink2);font-size:.9rem}}
.rds{{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(220px,1fr))}}.rd{{background:var(--card);border-radius:var(--r);padding:20px;display:flex;flex-direction:column;gap:6px}}.rd div{{height:90px;background:var(--m1);margin-bottom:8px}}.rd span{{color:var(--ink2);font-size:.82rem}}
.dark{{background:var(--m);color:var(--moon);border-radius:var(--r);padding:clamp(20px,4vw,48px);display:grid;gap:32px;grid-template-columns:minmax(0,320px) 1fr;align-items:center}}
@media(max-width:760px){{.dark{{grid-template-columns:1fr}}}}
.ncard .hand{{font-size:1.5rem;margin:0 0 6px 6px}}.nc{{background:linear-gradient(180deg,rgba(38,44,70,.95),rgba(32,38,63,.95));border-radius:var(--r);padding:0 0 4px;box-shadow:inset 0 1px 0 rgba(255,255,255,.16),0 1px 2px rgba(0,0,0,.2),0 12px 28px rgba(4,5,14,.45)}}
.nc header{{display:flex;justify-content:space-between;padding:14px 16px 10px;font-weight:600;font-size:.9rem}}.nc header span{{font:500 .7rem 'Geist Mono';color:var(--moon2)}}
.media{{height:150px;margin:0 8px;border-radius:var(--rm)}}.nc p{{margin:10px 16px;font-size:.85rem;color:var(--moon2)}}
.nc footer{{display:flex;justify-content:space-between;align-items:center;padding:8px 16px 12px}}.pill{{display:inline-flex;align-items:center;justify-content:center;height:32px;padding:0 14px;border-radius:999px;font-size:.8rem;font-weight:600;background:rgba(255,255,255,.08);box-shadow:inset 0 1px 0 rgba(255,255,255,.14)}}.pill.sm{{width:32px;padding:0;font-style:normal}}.pill.lit{{background:#EEF1FA;color:#151930}}.act{{display:flex;gap:8px}}
.rules li{{margin:6px 0}}.rules{{color:var(--ink2);padding-left:20px}}
.icons{{display:grid;grid-template-columns:repeat(auto-fill,minmax(88px,1fr));gap:8px;background:var(--m);color:var(--moon);border-radius:var(--r);padding:20px}}.icons figure{{margin:0;display:flex;flex-direction:column;align-items:center;gap:6px;padding:10px 0}}.icons figcaption{{font:500 .6rem 'Geist Mono';color:var(--moon2);text-align:center}}
.mo{{background:var(--card);border-radius:var(--rm);padding:14px 18px;display:grid;gap:2px}}.mo span{{color:var(--ink2);font-size:.85rem}}
.do{{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(260px,1fr))}}.do div{{background:var(--card);border-radius:var(--r);padding:22px}}.do b{{display:block;margin-bottom:8px}}.do .no b{{color:var(--tci)}}
footer.f{{padding:48px 0;color:var(--ink3);font-size:.85rem}}
</style></head><body>
<header class="hero"><main><img src="{logo}" alt="Infinite"><p class="hand">one language</p><h1>Brand book</h1>
<p class="lead">How Infinite looks, moves and sounds, on one page. Values come from <code>docs/brand/brand.json</code>; the rules are in <code>docs/brand/BRAND.md</code>. If anything else disagrees, this wins. v1, {B["updated"]}.</p></main></header>
<main>
<section><span class="k">01 · Principles</span><h2>Calm, premium, a little playful</h2><p class="s">Two grounds, one accent, rounded everything, no outlines. Loudness comes from the type, never from colour noise.</p>
<div class="do"><div><b>Two grounds</b>Midnight and Paper. Never pure black or white.</div><div><b>One accent</b>Coral on dark, terracotta on paper.</div><div><b>One card</b>Radius 24, media 16, no outline.</div></div></section>

<section><span class="k">02 · Colour</span><h2>Palette</h2><p class="s">60 percent ground, 30 percent surfaces and ink, 10 percent accent.</p>
<div class="grid">{"".join(sw(c, True) for c in C["ground"])}</div><h3>Ink</h3><div class="grid">{"".join(sw(c) for c in C["ink"])}</div>
<h3>Accent</h3><div class="grid">{"".join(sw(c) for c in C["accent"])}</div><div class="grad"></div><p class="s"><b>{C["gradient"]["name"]}:</b> {C["gradient"]["role"]}</p>
<h3>Contrast, measured</h3><div class="grid">{contrast}</div>
<h3>Category colours (node UI only)</h3><div class="grid">{cats}</div>
<h3>Role colours (dark, light)</h3><div class="grid">{roles}</div></section>

<section><span class="k">03 · Type</span><h2>Four families, no fifth</h2><div class="fams">{fams}</div><table>{types}</table></section>

<section><span class="k">04 · Shape and depth</span><h2>Rounded, concentric, borderless</h2><div class="rds">{radii}</div>
<p class="s" style="margin-top:20px">Media radius is the card radius minus the card padding (24 − 8 = 16). Depth comes from surface value, a lit top edge and soft stacked shadows.</p></section>

<section><span class="k">05 · The card</span><h2>One card everywhere</h2><div class="dark">{card}<ul class="rules"><li style="color:var(--moon2)">Surface Midnight 1, frosted, radius 24</li><li style="color:var(--moon2)">No outline, no line under the title, no rule above the footer</li><li style="color:var(--moon2)">Category colour only on the title bar and pins</li><li style="color:var(--moon2)">Controls are pills of one height</li><li style="color:var(--moon2)">Hand caption above, in Caveat</li></ul></div></section>

<section><span class="k">06 · Iconography</span><h2>20 px grid, 1.5 px stroke, round</h2><p class="s">Stroke 1.5 at 20, 1.25 at 16, 1.75 at 24. Corners 2 and 1. A signal icon ends in a 3 px dot, the same dot as a pin.</p><div class="icons">{icons()}</div></section>

<section><span class="k">07 · Motion</span><h2>Springs and ease-out, nothing linear</h2><div class="grid">{motion}</div></section>

<section><span class="k">08 · Voice</span><h2>Plain and specific</h2><div class="do"><div><b>Do</b>A frosted glass pane that bends what is behind it.<br>Lowercase slab headlines. One handwritten word.<br><b style="margin-top:12px">The line: A DAW for you.</b></div><div class="no"><b>Don't</b>“The world's first…”, exclamation marks, em dashes, “Free for now”, a second accent colour, a card with an outline.</div></div></section>
<footer class="f">Generated by tools/brand/build_brand_book.py from docs/brand/brand.json. Edit the JSON or BRAND.md, not this file.</footer>
</main></body></html>'''
out = os.path.join(ROOT, "docs/brand/brand-book.html")
open(out, "w").write(page)
print("wrote", os.path.relpath(out, ROOT), len(page) // 1024, "KB")
