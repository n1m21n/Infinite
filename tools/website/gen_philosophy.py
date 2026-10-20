#!/usr/bin/env python3
"""Builds website/philosophy.html: why Infinite exists, why it is a node graph, flow, machines and people, design principles.

What: one page from the text below. A live shader hero, a mind-map diagram that turns into a patch, a loop figure from the
      brand mark, a spring demo using the brand's measured springs, an FAQ (also emitted as FAQPage JSON-LD).
Why:  the product's reasons live in one place that people, search engines and agents can read; the brand book supplies
      the idea (two ends, one loop) and the motion laws, so the page uses them rather than inventing its own.
Usage: python3 tools/website/gen_philosophy.py     then python3 tools/website/sync_chrome.py
       python3 tools/website/gen_philosophy.py --check
Exit codes: 0 written (or in sync), 1 differs under --check.
Styles: website/philosophy.css (and audience.css for the shared hero). Behaviour: website/philosophy.js, website/audience.js.
"""
import html, math, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sync_chrome as chrome
import gen_audiences as ga

W = chrome.W
BASE = ga.BASE
e = html.escape

TITLE = "Philosophy of Infinite: node-based creative software built for flow"
DESC = "Why Infinite is a node graph, how it protects flow state in art and music making, how machines should work with people, and the design principles behind it."

# (id, x, y, mind label, patch label, mind sentence, patch sentence)
NODES = [
    ("memory", 300, 58, "Memory", "Sampler", "What you keep and replay.", "Holds a sound you recorded and plays it back."),
    ("rhythm", 100, 138, "Rhythm", "Drum Sequencer", "The pulse the rest leans on.", "Steps through a pattern of hits."),
    ("colour", 500, 138, "Colour", "Color Ramp", "How a feeling looks.", "Maps a value to a run of colours."),
    ("voice", 58, 270, "Voice", "Analog", "Who is speaking.", "A synth voice with oscillators and a filter."),
    ("motion", 300, 208, "Motion", "LFO", "What changes, and how fast.", "A slow wave that moves any knob."),
    ("texture", 546, 270, "Texture", "Noise", "The grain of a thing.", "A source of controlled randomness."),
    ("space", 190, 350, "Space", "Reverb", "Where it happens.", "Puts a sound in a room."),
    ("light", 410, 350, "Light", "Light", "What can be seen.", "A light in a 3D scene."),
]
EDGES = [("motion", "memory"), ("motion", "rhythm"), ("motion", "colour"), ("motion", "voice"), ("motion", "texture"), ("motion", "space"), ("motion", "light"),
         ("memory", "rhythm"), ("memory", "colour"), ("rhythm", "voice"), ("colour", "texture"), ("voice", "space"), ("space", "light"), ("light", "texture")]

AUD = [("artists", "paint", "Artists", "Images that move, on one canvas."), ("musicians", "play", "Musicians", "Synths, effects and plugins in one graph."),
       ("vjs", "perform", "VJs", "Visuals that follow the music, safe on stage."), ("creative-coders", "code", "Creative coders", "Field, sketches and shaders."),
       ("developers", "headless", "Developers and agents", "Render from a text file, with no window.")]

FAQ = [
    ("Why is Infinite a node-based program?",
     "A patch is a map of how things connect, and ideas connect in more than a line. Each node does one job and each cable says what feeds what, so you can see the whole shape of a piece at once, and anything can patch into anything: a sound can move a picture and a picture can move a sound."),
    ("What does flow state mean for music and art software?",
     "Flow is the state where doing and thinking are the same act. Software breaks it with dialogs, loading, deep menus and delay. Infinite keeps controls live while a patch plays, keeps pictures, sound, 3D and code in one graph, and lets any knob be moved by a controller, an LFO, an envelope or the audio itself."),
    ("Who is Infinite for?",
     "Artists, musicians, VJs, creative coders and technologists, and developers who want to render from a text file or give an AI agent the same tools. Each has its own page on this site."),
    ("Is Infinite free?",
     "Yes. Infinite is free to download for macOS, Windows and Linux. The source is on GitHub under the MIT licence, and the binary is GPLv3 by default because it bundles VST3 hosting."),
    ("Can AI agents use Infinite?",
     "Yes. Patches are plain text, and Infinite can render them headless from the command line, from a Python SDK, or through nine MCP tools. The person decides what is kept."),
]


def edge_path(a, b):
    ax, ay, bx, by = a[1], a[2], b[1], b[2]
    dx, dy = bx - ax, by - ay
    mx, my = (ax + bx) / 2, (ay + by) / 2
    bow = 0.099 * math.hypot(dx, dy)          # L4: a path bows 9.9% of its chord
    nx, ny = -dy / (math.hypot(dx, dy) or 1), dx / (math.hypot(dx, dy) or 1)
    return f"M{ax} {ay} Q{mx + nx * bow * 2:.1f} {my + ny * bow * 2:.1f} {bx} {by}"


def mind_svg():
    by = {n[0]: n for n in NODES}
    edges = "".join(f'<path class="edge" data-a="{a}" data-b="{b}" d="{edge_path(by[a], by[b])}"/>' for a, b in EDGES)
    nodes = []
    for i, (nid, x, y, mind, patch, ms, ps) in enumerate(NODES):
        w = 24 + len(patch) * 8.6
        nodes.append(
            f'<g class="mnode" data-id="{nid}" tabindex="0" role="button" transform="translate({x} {y})" data-mind="{e(ms)}" data-patch="{e(ps)}" aria-label="{e(mind)}, as a node: {e(patch)}">'
            f'<circle class="sh-mind" r="30"/><rect class="sh-patch" x="{-w / 2:.0f}" y="-22" width="{w:.0f}" height="44" rx="12"/>'
            f'<text class="l-mind" y="5" text-anchor="middle">{e(mind)}</text><text class="l-patch" y="5" text-anchor="middle">{e(patch)}</text></g>')
    return (f'<svg class="mind-svg" viewBox="0 0 600 410" role="group" aria-label="The same eight connections shown as ideas in a mind and as nodes in a patch">'
            f'<g class="edges">{edges}</g><circle class="thought" r="6" cx="-20" cy="-20"/><g class="nodes">{"".join(nodes)}</g></svg>')


def loop_svg():
    pts = []
    for k in range(0, 361, 6):
        t = math.radians(k)
        d = 1 + math.sin(t) ** 2
        pts.append((300 + 250 * math.cos(t) / d, 150 + 250 * math.sin(t) * math.cos(t) / d))
    path = "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in pts) + "Z"
    return (f'<svg class="loop-svg" viewBox="0 0 600 300" role="img" aria-label="A loop in the shape of the Infinite mark, with a point travelling around it">'
            f'<path class="loop-line" d="{path}"/><circle class="loop-dot" r="9" cx="550" cy="150"/>'
            f'<text class="loop-t t-human" x="425" y="156" text-anchor="middle">you</text><text class="loop-t t-struct" x="175" y="156" text-anchor="middle">structure</text></svg>')


ICONS = {"memory": "history", "rhythm": "metronome", "colour": "family-compositing", "voice": "family-synths", "motion": "family-modulators",
         "texture": "grid-dots", "space": "family-audio-effects", "light": "family-3d"}
SCENES = [("scatter", "grid-dots", "Ideas"), ("mind", "pointer", "A mind"), ("patch", "mod-matrix", "A patch"), ("loop", "loop", "You and structure"), ("one", "star", "One")]
CAPS = [("Why Infinite exists", "One canvas for pictures, sound and code, so an idea can travel from your head to the screen or the speakers without switching tools."),
        ("A graph, because ideas connect", "A patch is a map of how things connect, and ideas connect in more than a line."),
        ("Anything patches into anything", "Each node does one job and each cable says what feeds what. A sound can move a picture and a picture can move a sound."),
        ("Flow, with a machine that follows you", "Controls stay live while a patch plays. It listens, it continues, and you decide what stays."),
        ("Free, for everyone", "Free and open source on macOS, Windows and Linux, so the tools are not a privilege.")]
PRINCIPLES = [("Performance", "power", "Fast, light and efficient, measured and published."), ("Accuracy", "snap", "Maths from the formula, never by eye."),
              ("Innovation", "star", "New ways to make, tried in the open."), ("Connection", "loop", "Anything patches into anything.")]


def main_html():
    bub = "".join(f'<button class="pb" type="button" data-id="{nid}" style="--i:{i}" aria-label="{e(mind)}">{ga.icon(ICONS[nid])}</button>' for i, (nid, x, y, mind, patch, ms, ps) in enumerate(NODES))
    bub += f'<span class="pb pb-x" data-id="human" aria-hidden="true">{ga.icon("hand")}</span><span class="pb pb-x" data-id="machine" aria-hidden="true">{ga.icon("family-prediction")}</span>'
    nav = "".join(f'<button class="pn" type="button" data-go="{i}" aria-label="{e(lab)}">{ga.icon(ic)}</button>' for i, (sid, ic, lab) in enumerate(SCENES))
    who = "".join(f'<a class="pw" href="{s}.html">{ga.icon(ga.AUD_ICON[s])}<span>{e(n.replace("Developers and agents", "Developers"))}</span></a>' for s, h, n, d in AUD)
    ids = [n[0] for n in NODES]
    edges = ",".join(f"{ids.index(a)}-{ids.index(b)}" for a, b in EDGES)
    caps = "".join(f'<div class="ph-cap" data-k="{i}"><h2>{e(t)}</h2><p>{e(d)}</p></div>' for i, (t, d) in enumerate(CAPS))
    prin = "".join(f'<li>{ga.icon(ic)}<b>{e(t)}</b><span>{e(d)}</span></li>' for t, ic, d in PRINCIPLES)
    faq = "".join(f'<details class="ph-q"><summary>{e(q)}</summary><p>{e(a)}</p></details>' for q, a in FAQ)
    return f'''
  <main>
    <section class="ph" id="journey" aria-label="A journey through the ideas behind Infinite" data-edges="{e(edges)}">
      <div class="ph-stage">
        <h1 class="ph-title">Philosophy</h1>
        <div class="ph-glow" aria-hidden="true"></div>
        <svg class="ph-edges" aria-hidden="true"><g class="ph-lines"></g><circle class="ph-flow" r="5"/></svg>
        <svg class="ph-loop" viewBox="-300 -80 600 160" aria-hidden="true" preserveAspectRatio="xMidYMid meet"><defs><linearGradient id="phg" x1="0" x2="1"><stop offset="0" stop-color="#F57F66"/><stop offset=".5" stop-color="#C7AFC0"/><stop offset="1" stop-color="#A9CDF1"/></linearGradient><radialGradient id="phb" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#FFFFFF"/><stop offset=".35" stop-color="#FF9878"/><stop offset="1" stop-color="#B53700"/></radialGradient></defs><path class="ph-mark" d="" pathLength="1"/><circle class="ph-ball" r="9" fill="url(#phb)" cx="0" cy="0"/></svg>
        <div class="ph-bubbles">{bub}</div>
        <div class="ph-caps">{caps}</div>
        <span class="ph-lobe ph-lobe-a" aria-hidden="true">structure</span><span class="ph-lobe ph-lobe-b" aria-hidden="true">you</span>
        <nav class="ph-nav" aria-label="Chapters">{nav}</nav>
        <div class="ph-end"><div class="ph-who">{who}</div><a class="btn btn-primary ph-dl" href="index.html#setup">Download</a></div>
        <span class="ph-hint" aria-hidden="true">{ga.icon("chevron-down")}</span>
      </div>
    </section>
    <section class="ph-after">
      <ul class="ph-prin">{prin}</ul>
      <div class="ph-faq">{faq}</div>
    </section>
'''


def ld():
    page = {"@context": "https://schema.org", "@type": "AboutPage", "name": TITLE, "url": BASE + "philosophy.html", "description": DESC,
            "isPartOf": {"@type": "WebSite", "name": "Infinite", "url": BASE},
            "about": {"@type": "SoftwareApplication", "name": "Infinite", "applicationCategory": "MultimediaApplication", "operatingSystem": "macOS, Windows, Linux", "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"}}}
    faq = {"@context": "https://schema.org", "@type": "FAQPage", "mainEntity": [{"@type": "Question", "name": q, "acceptedAnswer": {"@type": "Answer", "text": a}} for q, a in FAQ]}
    crumbs = {"@context": "https://schema.org", "@type": "BreadcrumbList", "itemListElement": [{"@type": "ListItem", "position": 1, "name": "Infinite", "item": BASE}, {"@type": "ListItem", "position": 2, "name": "Philosophy", "item": BASE + "philosophy.html"}]}
    return [page, faq, crumbs]


def build():
    return ga.shell("philosophy.html", TITLE, DESC, ld(), main_html(), extra_head='\n  <link rel="stylesheet" href="philosophy.css?v=b4d7db93">', extra_js='\n  <script src="philosophy.js?v=9" defer></script>')


def main():
    p = os.path.join(W, "philosophy.html")
    new = build()
    old = open(p, encoding="utf-8").read() if os.path.exists(p) else ""
    if "--check" in sys.argv:
        print("differ: philosophy.html" if old != new else "differ: none")
        return 1 if old != new else 0
    if old != new:
        open(p, "w", encoding="utf-8").write(new)
    print("written: philosophy.html")
    return 0


if __name__ == "__main__":
    sys.exit(main())
