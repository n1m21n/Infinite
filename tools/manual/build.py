#!/usr/bin/env python3
"""Rebuild Infinite_Node_Reference_Manual.pdf (the "Node Field Guide").

The guide's original HTML source was never kept, so this keeps the 47 published chapter pages
(1-based pages 4..50 of the previous PDF, saved under tools/manual/base/) and regenerates
everything around them: the cover, intro and contents pages, the newer chapters 21-24 from
cards.py, and the closing page. Pages are printed by headless Chrome and joined with pdfunite.

    python3 tools/manual/build.py            # writes Infinite_Node_Reference_Manual.pdf + website/assets copy
"""
import html
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from cards import CHAPTERS  # noqa: E402

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
BASE_PAGES = os.path.join(HERE, "base", "chapters_1_20.pdf")  # old pages 4..50, untouched
OUT = os.path.join(ROOT, "Infinite_Node_Reference_Manual.pdf")
OUT_WEB = os.path.join(ROOT, "website", "assets", "Infinite_Node_Reference_Manual.pdf")

# Counts come from `Infinite --describe` (299 registered) minus the five types the spawn menu hides
# (Delete Selected, Transform Selected, Extrude Selected, Group, Field Graph).
SPAWNABLE = 294
NEW_NODES = sum(len(c["cards"]) for c in CHAPTERS)

OLD_CHAPTERS = [
    ("2D Sources & Visual Generators", "pictures, video, shapes, noise, paint"),
    ("Typography & Text", "flat and extruded 3D lettering"),
    ("2D Image Effects — Blur, Warp & Glow", "the hand-picked favorites"),
    ("2D Image Effects — The Full Filter Shelf", "all 34 filters, one-line reference"),
    ("Color Grading & Tone Mapping", "grade, curve, LUT, gradient map"),
    ("Compositing & Layering", "blending, layers, routing, organization"),
    ("Feedback, Trails & AI Vision", "background removal, feedback, mutation"),
    ("3D Building Blocks & Materials", "primitives, import, shading, deform"),
    ("3D Points, Instancing & Mesh Ops", "scatter, instance, weld, convert"),
    ("3D Simulation, Scene & Render", "ocean, cloth, particles, camera, light"),
    ("Audio Synthesizers", "wavetables, physical modeling, granular"),
    ("Audio Effects & Plugin Hosting", "filters, EQ, AU plugins, full FX shelf"),
    ("Audio Utility & Routing", "gain, mixing, splitting, in/out"),
    ("MIDI & Note Shaping", "filter, transpose, humanize, quantize"),
    ("Generative & Physical Note Nodes", "arpeggios, chords, merging, bouncing-ball melodies"),
    ("Modulators — Shape & Combine", "LFO, random, math, curves"),
    ("Modulators — Control & Performance", "knobs, pads, MIDI CC, palettes"),
    ("Modulators — Audio/Note Bridges & Analysis", "CV converters, envelopes, analysis"),
    ("Output & Sharing", "screen, speakers, Syphon, projection mapping, OSC"),
    ("The Field Language — Programmable Nodes", "kernels, per-element code, custom nodes"),
]
NEW_BLURBS = {
    21: "slideshow and nine more vector shapes",
    22: "keys, mattes, edges, seamless tiles",
    23: "platonic solids, mesh to points, wireframe, explode",
    24: "looper, resonators, meters, prediction",
}
CHAPTER_COUNT = 20 + len(CHAPTERS)
WORDS = {24: "Twenty-Four"}

CSS = """
@page { size: 8.5in 11in; margin: 0; }
:root { --bg:#f7f4ee; --card:#fdfcf9; --ink:#1f1d18; --mute:#73716b; --faint:#b5afa3; --accent:#c1552f;
        --chip:#f0ead9; --line:#e6e0d3; --green:#3d7a57; --amber:#c98f2e; --purple:#7b68a8; --blue:#3f6ab0; }
* { box-sizing: border-box; }
html, body { margin: 0; background: var(--bg); color: var(--ink);
  font-family: "Helvetica Neue", Helvetica, Arial, sans-serif; -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { padding: 0; }
table.w { width: 100%; border-collapse: collapse; }
table.w td { padding: 0 0.63in; }
.sp.t { height: 0.6in; } .sp.b { height: 0.8in; }
.mono { font-family: Menlo, "SF Mono", monospace; }
.hand { font-family: "Bradley Hand", "Bradley Hand ITC", cursive; font-weight: bold; }
.chap { font-size: 15px; color: var(--faint); font-style: italic; margin: 8px 0 8px; }
h1 { font-size: 27px; margin: 0 0 14px; letter-spacing: -0.3px; }
.intro { color: var(--mute); font-size: 13px; line-height: 1.6; margin: 0 0 22px; max-width: 6.2in; }
.card { position: relative; background: var(--card); border: 1px solid #ece7dc; border-left: 4px solid var(--accent);
  border-radius: 9px; padding: 16px 20px 14px 20px; margin: 0 0 14px; break-inside: avoid; }
.card .top { display: flex; justify-content: space-between; align-items: baseline; }
.card h2 { font-size: 15.5px; margin: 0 0 8px; }
.tag { font-family: Menlo, monospace; font-size: 8.5px; letter-spacing: 0.9px; color: var(--faint); text-transform: uppercase; }
.card p { font-size: 11.6px; line-height: 1.55; margin: 0 0 11px; }
.try { border-top: 1px dashed #e4dfd2; padding-top: 10px; font-size: 11.2px; color: #3a3830; }
.try b { font-family: "Bradley Hand", cursive; color: var(--green); font-size: 14px; font-style: italic; margin-right: 3px; }
.chain { margin-top: 9px; display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }
.chip { font-family: Menlo, monospace; font-size: 9.5px; background: var(--chip); border: 1px solid #e5dcc3; border-radius: 5px; padding: 3px 7px; }
.arrow { color: var(--faint); font-size: 11px; }
.foot { position: fixed; left: 0; right: 0; bottom: 0.35in; margin: 0 0.63in; display: flex; justify-content: space-between;
  border-top: 1px solid #e2ddd2; padding-top: 7px; font-family: Menlo, monospace; font-size: 8px; letter-spacing: 1.2px; color: var(--faint); text-transform: uppercase; }
.kick { font-size: 9.5px; letter-spacing: 2.6px; color: var(--accent); font-weight: bold; text-transform: uppercase; }
.page { break-after: page; }
.panel { background: #fdfcf9; border: 1px solid #e9e3d6; border-radius: 14px; padding: 26px 28px; }
.panel p { font-size: 12px; line-height: 1.7; margin: 0 0 12px; }
.panel p:last-child { margin: 0; }
.toc { list-style: none; padding: 0; margin: 14px 0 0; }
.toc li { break-inside: avoid; display: flex; align-items: baseline; border-bottom: 1px dotted #d9d3c4; padding: 9px 0; font-size: 12.2px; }
.toc .n { width: 30px; font-family: "Bradley Hand", cursive; color: var(--accent); font-style: italic; font-size: 13px; }
.toc .t { font-weight: bold; flex: 1; }
.toc .d { color: var(--faint); font-size: 10.5px; }
"""


def esc(s):
    return html.escape(s, quote=False)


def doc(body, footer_left):
    return f"""<!doctype html><html><head><meta charset="utf-8"><style>{CSS}</style></head><body>
<table class="w"><thead><tr><td><div class="sp t"></div></td></tr></thead><tfoot><tr><td><div class="sp b"></div></td></tr></tfoot><tbody><tr><td>
{body}
</td></tr></tbody></table>
{'<div class="foot"><span>' + esc(footer_left) + '</span><span>INFINITE · NODE FIELD GUIDE</span></div>' if footer_left else ''}</body></html>"""


def chapter_html(ch):
    cards = []
    for name, tag, desc, tryit, chain in ch["cards"]:
        chips = '<span class="arrow">→</span>'.join(f'<span class="chip">{esc(c)}</span>' for c in chain)
        chips = chips.replace('</span><span class="arrow">', '</span><span class="arrow">')
        cards.append(
            f'<div class="card"><div class="top"><h2>{esc(name)}</h2><span class="tag">{esc(tag)}</span></div>'
            f'<p>{esc(desc)}</p><div class="try"><b>Try it</b> — {esc(tryit)}'
            f'<div class="chain">{chips}</div></div></div>'
        )
    body = (f'<div class="chap">Chapter {ch["num"]}</div><h1>{esc(ch["title"])}</h1>'
            f'<p class="intro">{esc(ch["intro"])}</p>' + "".join(cards))
    return doc(body, f'Chapter {ch["n"]} — {ch["title"]}')


def front_html():
    cover = f"""
<div class="page" style="padding-top:0.3in; height:9.5in; position:relative">
  <div style="display:flex; gap:7px; margin-bottom:12px"><i style="width:9px;height:9px;border-radius:50%;background:#e0524a"></i><i style="width:9px;height:9px;border-radius:50%;background:#3aa655"></i><i style="width:9px;height:9px;border-radius:50%;background:#3f6ab0"></i></div>
  <div class="mono" style="font-size:9px;letter-spacing:2.5px;color:var(--mute)">INFINITE &nbsp;&middot;&nbsp; AUDIOVISUAL NODE WORKSTATION</div>
  <div style="font-size:50px;font-weight:bold;letter-spacing:-1.5px;margin-top:36px;line-height:1">The Node</div>
  <div class="hand" style="font-size:54px;color:var(--accent);font-style:italic;line-height:1.1;margin-bottom:22px">Field Guide</div>
  <p style="font-size:15px;line-height:1.55;color:#4a483f;max-width:5.1in">A friendly tour of Infinite's entire node palette — what each one actually does, why you'd reach for it, and a quick recipe to get you started. No engineering degree required.</p>
  <div style="display:flex;flex-wrap:wrap;gap:10px;margin:22px 0 30px;font-size:10.5px">
    <span class="pill">Platform <b>macOS · Apple Silicon &amp; Intel</b></span>
    <span class="pill">This guide <b>Every node type, {CHAPTER_COUNT} chapters</b></span>
    <span class="pill">Style <b>Plain-English, no jargon</b></span>
  </div>
  <div class="panel" style="padding:22px 26px">
    <div class="hand" style="font-size:20px;font-style:italic">Five wires, five jobs</div>
    <div style="font-size:10.5px;color:var(--mute);margin:6px 0 16px">Every cable in Infinite is color-coded so you always know what's flowing through it.</div>
    <div style="display:grid;grid-template-columns:repeat(3,1fr);row-gap:12px;font-size:10.5px">
      <span><i class="dot" style="background:var(--accent)"></i>Image — pictures &amp; video</span>
      <span><i class="dot" style="background:var(--green)"></i>Geometry — 3D shapes</span>
      <span><i class="dot" style="background:var(--amber)"></i>Audio — sound</span>
      <span><i class="dot" style="background:var(--purple)"></i>Notes — musical events</span>
      <span><i class="dot" style="background:var(--blue)"></i>Modulator — a wiggling knob</span>
    </div>
  </div>
  <div style="position:absolute;left:0;right:0;bottom:0;font-size:9.5px;color:var(--mute);display:flex;justify-content:space-between;align-items:flex-end">
    <span style="max-width:5.6in">Infinite ships with {SPAWNABLE} spawnable node types, including the image-filter and audio-effect shelves. This guide covers all of them.</span>
    <span class="hand" style="font-size:16px;color:var(--green);font-style:italic">have fun ✎</span></div>
</div>"""
    rows = []
    for i, (t, d) in enumerate(OLD_CHAPTERS, 1):
        rows.append((i, t, d))
    for c in CHAPTERS:
        rows.append((c["n"], c["title"], NEW_BLURBS[c["n"]]))

    def li(r):
        return f'<li><span class="n">{r[0]:02d}</span><span class="t">{esc(r[1])}</span><span class="d">{esc(r[2])}</span></li>'

    intro = f"""
<div style="padding-top:0.15in">
  <div class="kick">Before you start</div>
  <h1 style="font-size:30px;margin:10px 0 18px">What Even Is Infinite?</h1>
  <div class="panel">
    <p>Infinite is a node-based studio for making sound and image at the same time. Instead of separate apps for music, visuals, and 3D, you drop blocks called <b>nodes</b> onto one canvas and connect them with cables — a synth feeding a filter, a shape feeding a kaleidoscope, a drumbeat feeding a bouncing particle field.</p>
    <p>Because everything lives on the same clock, sound and image can react to each other in real time: a kick drum can punch a shape, a camera's motion can bend a synth's pitch, a color can become a chord. There is no "render and wait" — you hear and see the result the instant you plug a cable in.</p>
    <p>This guide skips the engineering manual entirely and walks through every node in Infinite's palette — {SPAWNABLE} spawnable node types, including the 34-filter image effects shelf and the 20-effect audio shelf. For every one you'll find what it does in plain English, why you'd actually use it, and (where it helps) a short recipe showing it wired up with a couple of friends. Two big shared-table shelves — image filters and audio effects — get a compact one-line-per-entry reference instead of a full card each, since dozens of them are close cousins of each other. Chapter Twenty covers the Field language — five nodes where you write the behavior yourself instead of picking a preset — and the newest four chapters ({NEW_NODES} nodes) cover the shapes, alpha tools, 3D solids, looper and Prediction family added since.</p>
  </div>
  <div class="kick" style="margin-top:26px">Contents</div>
  <h1 style="font-size:28px;margin:8px 0 0">{CHAPTER_COUNT_WORD} Chapters</h1>
  <ul class="toc">{"".join(li(r) for r in rows)}</ul>
</div>"""
    css_extra = """.pill{border:1px solid #d9d3c4;border-radius:20px;padding:8px 14px;color:var(--mute);background:#fbf9f4}.pill b{color:var(--ink);margin-left:4px}
.dot{display:inline-block;width:20px;height:6px;border-radius:3px;margin-right:10px;vertical-align:middle}"""
    return cover, intro, css_extra


def closing_html():
    body = f"""<div style="padding-top:1.2in">
<h1 style="font-size:30px">That's the field guide.</h1>
<p class="intro" style="color:var(--ink);max-width:6in">{SPAWNABLE} spawnable node types, including a 34-filter image effects shelf and a 20-effect audio shelf — every node Infinite ships with, across {CHAPTER_COUNT_WORD.lower()} chapters and five cable colors.</p>
<p class="intro" style="color:var(--ink);max-width:6in">The best way to learn Infinite isn't to read about it — it's to grab two or three nodes and see what happens when you wire them together. For exact parameter ranges and engine internals, see the full technical reference at n1m21n.github.io/Infinite.</p></div>"""
    return doc(body, "Closing")


CHAPTER_COUNT_WORD = {24: "Twenty-four"}.get(CHAPTER_COUNT, str(CHAPTER_COUNT))


def render(html_text, out_pdf, workdir):
    src = os.path.join(workdir, os.path.basename(out_pdf) + ".html")
    with open(src, "w", encoding="utf-8") as f:
        f.write(html_text)
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", "--virtual-time-budget=2000",
                    f"--print-to-pdf={out_pdf}", "file://" + src], check=True, capture_output=True, timeout=120)


def main():
    if not os.path.exists(BASE_PAGES):
        sys.exit(f"missing {BASE_PAGES}")
    tmp = tempfile.mkdtemp()
    cover, intro, css_extra = front_html()
    global CSS
    CSS += css_extra
    parts = []
    # cover and intro/contents share one document so the contents flow onto a second page
    front = doc(cover + intro, "")
    p = os.path.join(tmp, "00_front.pdf")
    render(front, p, tmp)
    parts.append(p)
    parts.append(BASE_PAGES)
    for ch in CHAPTERS:
        p = os.path.join(tmp, f"ch{ch['n']}.pdf")
        render(chapter_html(ch), p, tmp)
        parts.append(p)
    p = os.path.join(tmp, "zz_closing.pdf")
    render(closing_html(), p, tmp)
    parts.append(p)
    joined = os.path.join(tmp, "joined.pdf")
    subprocess.run(["pdfunite"] + parts + [joined], check=True)
    shutil.copy(joined, OUT)
    shutil.copy(joined, OUT_WEB)
    print("wrote", OUT, "and", OUT_WEB)


if __name__ == "__main__":
    main()
