#!/usr/bin/env python3
"""Build Infinite_Node_Reference_Manual.pdf (the "Node Field Guide") in the Paper brand style.

Chapters 1-20 come from cards_base.json (extracted once from the v0.5 PDF by extract_base.py, so the text now
has a source of truth), chapters 21-28 from cards.py and cards_v06.py. Every node card carries its screenshot
from docs/node_screenshots/, captured with scripts/screenshot_all_nodes.py (BRAND.md section 10.1). The guide
ends with the UI chapter (ui_chapter.py, screenshots in docs/ui_screenshots/) and a gallery of every node.

    python3 scripts/screenshot_all_nodes.py --out docs/node_screenshots     # refresh the node screenshots
    python3 tools/manual/build.py                                           # writes the PDF + website/assets and press_kit copies
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "brand"))
from cards import CHAPTERS as NEW_CHAPTERS  # noqa: E402
import paper_doc as P  # noqa: E402
from paper_doc import esc  # noqa: E402
import shots  # noqa: E402

OUT = os.path.join(ROOT, "Infinite_Node_Reference_Manual.pdf")
OUT_WEB = os.path.join(ROOT, "website", "assets", "Infinite_Node_Reference_Manual.pdf")
OUT_KIT = os.path.join(ROOT, "press_kit", "manuals", "Infinite_Node_Reference_Manual.pdf")
FOOT_R = "INFINITE / NODE FIELD GUIDE"

# Counts come from `Infinite --describe` (338 registered) minus the five types the spawn menu hides
# (Delete Selected, Transform Selected, Extrude Selected, Group, Field Graph).
SPAWNABLE = 333
BASE = json.load(open(os.path.join(HERE, "cards_base.json")))
NEW_NODES = sum(len(c["cards"]) for c in NEW_CHAPTERS)

OLD_CHAPTERS = [
    ("2D Sources & Visual Generators", "pictures, video, shapes, noise, paint"),
    ("Typography & Text", "flat and extruded 3D lettering"),
    ("2D Image Effects: Blur, Warp & Glow", "the hand-picked favorites"),
    ("2D Image Effects: The Full Filter Shelf", "all 34 filters, one-line reference"),
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
    ("Modulators: Shape & Combine", "LFO, random, math, curves"),
    ("Modulators: Control & Performance", "knobs, pads, MIDI CC, palettes"),
    ("Modulators: Audio/Note Bridges & Analysis", "CV converters, envelopes, analysis"),
    ("Output & Sharing", "screen, speakers, Syphon, projection mapping, OSC"),
    ("The Field Language: Programmable Nodes", "kernels, per-element code, custom nodes"),
]
NEW_BLURBS = {
    21: "slideshow and nine more vector shapes",
    22: "keys, mattes, edges, seamless tiles",
    23: "platonic solids, mesh to points, wireframe, explode",
    24: "looper, resonators, meters, prediction",
    25: "Sketch, Field Notes, MIDI file and out, spatial mixing, hand, face and pose tracking",
    26: "Delaunay, Voronoi, decimate, curve ops, dither, NDI",
    27: "FM, String, Partials, Analog, Saturator, convolution reverb, key-snap, MSEG, voice spread",
    28: "optical flow, time machine, fluid, volume, rigid bodies, L-system, revolve, mesh primitives",
}
CHAPTER_COUNT = 20 + len(NEW_CHAPTERS)
COUNT_WORD = {26: "Twenty-six", 27: "Twenty-seven", 28: "Twenty-eight"}.get(CHAPTER_COUNT, str(CHAPTER_COUNT))


def shot_html(name, cls="shot", max_w=150, max_h=300):
    imgs = shots.find(name)
    if not imgs:
        return ""
    parts = []
    for tid, path, w, h in imgs[:3]:
        k = min(1.0, max_w / (w / 2), max_h / (h / 2)) * 0.5
        parts.append(f'<img src="file://{path}" width="{round(w * k)}" height="{round(h * k)}" alt="{esc(tid)}">')
    return f'<div class="{cls}">{"".join(parts)}</div>'


def card_html(name, tag, desc, tryit, chain):
    chips = '<span class="arrow">&rarr;</span>'.join(f'<span class="chip">{esc(c)}</span>' for c in chain)
    return (f'<div class="card"><div class="txt"><div class="top"><h2>{esc(name)}</h2><span class="tag">{esc(tag)}</span></div>'
            f'<p>{esc(desc)}</p><div class="try"><b>Try it</b>{esc(tryit)}'
            f'<div class="chain">{chips}</div></div></div>{shot_html(name)}</div>')


def chapter_html(ch):
    num = ch["num"]
    intro = ch["intro"] if isinstance(ch["intro"], list) else [ch["intro"]]
    body = [f'<div class="kick">Chapter</div><div class="hand" style="font-size:40px;line-height:1">{esc(num)}</div>',
            f'<h1>{esc(ch["title"])}</h1>', f'<p class="lead">{esc(intro[0])}</p>']
    body += [f"<p>{esc(p)}</p>" for p in intro[1:]]
    body += [card_html(*c) for c in ch["cards"]]
    for name, desc in ch.get("rows", []):
        body.append(f'<div class="row">{shot_html(name, "shot", 70, 90)}<div><h3>{esc(name)}</h3><p>{esc(desc)}</p></div></div>')
    body += [f"<p>{esc(p)}</p>" for p in ch.get("notes", [])]
    return P.doc("".join(body), f'Chapter {ch["n"]:02d} / {ch["title"]}', FOOT_R, P.PILL_CSS)


def all_chapters():
    chs = [dict(c) for c in BASE]
    for c in chs:
        c["num"] = c["num"]
    return chs + [dict(c, intro=c["intro"], rows=[], notes=[]) for c in NEW_CHAPTERS]


def front_html():
    cover = P.cover("AUDIOVISUAL NODE WORKSTATION", "The Node", "Field", "Guide",
                    "A friendly tour of Infinite's entire node palette: what each node does, why you would reach for it, "
                    "a quick recipe to start with, and a picture of the node itself. No engineering degree required.",
                    [("Platform", "macOS, Apple Silicon and Intel"), ("Coverage", f"Every node type, {CHAPTER_COUNT} chapters"),
                     ("Pictures", "Real screenshots of every node")], P.LOGO_TILE, art="node_hero")
    cables = "".join(f'<span><i style="display:inline-block;width:22px;height:6px;border-radius:3px;margin-right:9px;vertical-align:middle;background:{c}"></i>{n}</span>'
                     for n, c in P.CABLES)
    legend = f"""<div class="panel" style="position:absolute;left:0;right:0;bottom:0">
      <div class="kick">Five cables, five jobs</div>
      <p style="color:var(--ink-3);margin:6px 0 12px">Every cable is colour-coded so you always know what is flowing through it. Colours follow your theme; these are the defaults.</p>
      <div style="display:grid;grid-template-columns:repeat(5,1fr);font-size:10px;gap:6px">{cables}</div></div>"""
    cover = cover.rstrip()
    assert cover.endswith("</div>")
    cover = cover[:-len("</div>")] + legend + "</div>"
    rows = [(i, t, d) for i, (t, d) in enumerate(OLD_CHAPTERS, 1)] + [(c["n"], c["title"], NEW_BLURBS[c["n"]]) for c in NEW_CHAPTERS]
    rows += [(CHAPTER_COUNT + 1, "Using the App: Panels, Menus & Settings", "dropdowns, panels, shortcuts, settings, with screenshots"),
             (CHAPTER_COUNT + 2, "Gallery: Every Node", "all node types in one picture index")]

    def li(r):
        return f'<li><span class="n">{r[0]:02d}</span><span class="t">{esc(r[1])}</span><span class="d">{esc(r[2])}</span></li>'

    intro = f"""
<div style="padding-top:0.05in">
  <div class="kick">Before you start</div>
  <h1>What even is Infinite?</h1>
  <div class="panel">
    <p>Infinite is a node-based studio for making sound and image at the same time. Instead of separate apps for music, visuals, and 3D, you drop blocks called <b>nodes</b> onto one canvas and connect them with cables: a synth feeding a filter, a shape feeding a kaleidoscope, a drumbeat feeding a bouncing particle field.</p>
    <p>Because everything lives on the same clock, sound and image can react to each other in real time: a kick drum can punch a shape, a camera's motion can bend a synth's pitch, a color can become a chord. There is no "render and wait". You hear and see the result the instant you plug a cable in.</p>
    <p>This guide walks through every node in Infinite's palette, {SPAWNABLE} spawnable node types, including the 34-filter image effects shelf and the 20-effect audio shelf. For each you get what it does in plain English, why you would use it, a short recipe, and a screenshot of the node as it looks in the app. The two big shared-table shelves, image filters and audio effects, get a compact one-line entry each since dozens of them are close cousins. Chapter Twenty covers the Field language, where you write the behavior yourself, and the newest eight chapters ({NEW_NODES} nodes) cover shapes, alpha tools, 3D solids, the looper and Prediction family, code-drawn Sketch nodes, Field Notes, MIDI file and out, the Tracking nodes, mesh and curve operators, and the v0.6 synths, effects, modulators, compositing and 3D tools. The last two chapters cover the app around the nodes and a gallery picture of every node.</p>
    <p>The screenshots were captured by the app itself with <code>scripts/screenshot_all_nodes.py</code>, so they always match the build. Run it again after a release to refresh them.</p>
  </div>
  <div class="kick" style="margin-top:22px">Contents</div>
  <h1 style="font-size:26px">{COUNT_WORD} chapters and two appendices</h1>
  <ul class="toc">{"".join(li(r) for r in rows)}</ul>
</div>"""
    return P.doc(cover + intro, "", "", P.PILL_CSS)


def gallery_html():
    cats = {}
    for tid, path, w, h in shots.all_shots():
        cats.setdefault(shots.category(tid), []).append((tid, path, w, h))
    body = ['<div class="kick">Appendix</div><div class="hand" style="font-size:40px;line-height:1">Everything</div>',
            "<h1>Gallery: every node</h1>",
            '<p class="lead">One picture of every node type, grouped by palette category. Names are the exact type names the Infinite RPC and patch files use.</p>']
    for cat in sorted(cats):
        body.append(f"<h2>{esc(cat)} <span class='tag'>{len(cats[cat])} nodes</span></h2><div class='grid'>")
        for tid, path, w, h in sorted(cats[cat], key=lambda x: x[0].lower()):
            k = min(1.0, 150 / (h / 2), 140 / (w / 2)) * 0.5
            body.append(f'<div class="cell"><img src="file://{path}" width="{round(w * k)}" height="{round(h * k)}"><span class="nm">{esc(tid)}</span></div>')
        body.append("</div>")
    return P.doc("".join(body), "Gallery / every node", FOOT_R, P.PILL_CSS)


def closing_html():
    body = f"""<div style="padding-top:1.2in">
<div class="hand" style="font-size:44px">That's the guide.</div>
<h1>{SPAWNABLE} node types, five cables, one canvas.</h1>
<p class="lead">The best way to learn Infinite is to grab two or three nodes and see what happens when you wire them together.</p>
<p>For exact parameter ranges and engine internals, see the technical reference at n1m21n.github.io/Infinite. Need to script or automate Infinite? The Headless User Guide covers the command line and the control port, and the Field Language Manual covers writing your own nodes.</p></div>"""
    return P.doc(body, "Closing", FOOT_R, P.PILL_CSS)


def main():
    tmp = tempfile.mkdtemp()
    parts = []

    def add(name, html_text):
        p = os.path.join(tmp, name + ".pdf")
        P.render(html_text, p, tmp)
        parts.append(p)

    add("00_front", front_html())
    for ch in all_chapters():
        add(f"ch{ch['n']:02d}", chapter_html(ch))
    try:
        import ui_chapter
        add("ui", P.doc(ui_chapter.html_chapter(P), "Chapter 29 / Using the App", FOOT_R, P.PILL_CSS))
    except ImportError:
        print("note: no ui_chapter.py yet, skipping the UI chapter")
    add("zz_gallery", gallery_html())
    add("zzz_closing", closing_html())
    joined = os.path.join(tmp, "joined.pdf")
    subprocess.run(["pdfunite"] + parts + [joined], check=True)
    shutil.copy(joined, OUT)
    shutil.copy(joined, OUT_WEB)
    shutil.copy(joined, OUT_KIT)
    print("wrote", OUT, "and copies in website/assets and press_kit/manuals")


if __name__ == "__main__":
    main()
