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


def main_html():
    who = "\n".join(f'          <a class="who-card reveal" style="--i:{i}" href="{s}.html">{ga.icon(ga.AUD_ICON[s])}<b>{e(n)}</b><span>{e(d)}</span></a>' for i, (s, h, n, d) in enumerate(AUD))
    protect = [("Nothing between you and the result", "Controls stay live while a patch plays. You change a value and you see or hear it change."),
               ("One canvas", "Pictures, sound, 3D and code are nodes in one graph, so nothing is exported from one tool to be imported into another."),
               ("Hands before menus", "Any knob can be moved by a controller, an LFO, an envelope or the audio itself, so the instrument is played, not configured."),
               ("Patch the way you think", "Start with one node, add whatever comes to mind next, and connect it. There is no fixed order you must follow.")]
    prot = "\n".join(f'          <article class="aud-card reveal" style="--i:{i}"><span class="num">{i + 1:02d}</span><h3>{e(t)}</h3><p>{e(d)}</p></article>' for i, (t, d) in enumerate(protect))
    machine = [("It listens", "Audio Analyze turns sound into numbers a knob can follow, so a picture can lean on a bassline without a single keyframe.", None),
               ("It continues", "Predictive nodes such as Predictive Notes and Predictive Rhythm carry on a pattern you started. You keep what you like and patch away what you do not.", None),
               ("It can use the same tools", "Patches are plain text, so an AI agent can read and write them. Headless rendering, a Python SDK and nine MCP tools give an agent the graph you work in. The person decides what stays.", ("developers.html", "For developers and agents"))]
    mach = "\n".join(f'          <article class="aud-card reveal" style="--i:{i}"><span class="num">{i + 1:02d}</span><h3>{e(t)}</h3><p>{e(d)}</p>' + (f'<p><a class="inline" href="{l[0]}">{e(l[1])}</a></p>' if l else "") + "</article>" for i, (t, d, l) in enumerate(machine))
    principles = [("Connection", "Anything patches into anything. This is the belief the rest is built on."), ("Live", "Anything that shows a value keeps moving: meters fall, waveforms scroll, knobs breathe."),
                  ("Measured", "Motion follows the music. Every duration is a note value at the transport tempo: 125, 188, 333, 500 and 667 ms at 120 BPM."),
                  ("Calm", "One accent colour, used once in a frame. Plain words, no hype, no exclamation marks."),
                  ("Honest", "Speeds are measured and published on the benchmarks page, and the facts about each node are generated from the code."),
                  ("Free", "Free to use and open source, on macOS, Windows and Linux, so the tools are not a privilege.")]
    prin = "\n".join(f'          <li class="reveal" style="--i:{i}"><b>{e(t)}</b><span>{e(d)}</span></li>' for i, (t, d) in enumerate(principles))
    springs = "".join(f'<button class="spring" type="button" data-spring="{s}" aria-label="Play the {s} spring"><span class="spring-name">{s}</span><span class="spring-track"><i class="spring-dot"></i></span><span class="spring-note">{n}</span></button>'
                      for s, n in [("slab", "12.6% overshoot"), ("letter", "9.5%"), ("soft", "0.2%"), ("press", "none")])
    faq = "\n".join(f'          <details class="faq-item"><summary>{e(q)}</summary><p>{e(a)}</p></details>' for q, a in FAQ)
    return f'''
  <main>
    <section class="ah">
      <div class="container ah-grid">
        <div class="ah-text">
          <span class="blog-hand hand">why</span>
          <h1 class="slab">Why Infinite exists</h1>
          <p class="lead">Infinite is one canvas for pictures, sound and code. It is built so an idea can travel from your head to the screen or the speakers without you stopping to switch tools.</p>
          <div class="aud-cta">
            <a href="index.html#setup" class="btn btn-primary">Download Infinite</a>
            <a href="#who" class="btn btn-ghost">Who it is for</a>
          </div>
          <ul class="facts"><li>Free and open source</li><li>macOS, Windows, Linux</li><li>A DAW for you</li></ul>
        </div>
        <div class="vstage">
          <canvas data-shader="mind" aria-hidden="true"></canvas>
          <span class="vstage-float"><img class="vstage-3d" src="assets/3d/logo_3d.webp" alt="The Infinite mark as a 3D object" width="700" height="394" decoding="async"></span>
          <span class="vstage-tag">live shader · move the pointer</span>
        </div>
      </div>
    </section>

    <div class="container phil">
      <section class="aud-section reveal" id="mission">
        <div class="split">
          <div class="split-text">
            <span class="blog-hand hand">mission</span>
            <h2>Creative liberation, through tools that stay out of the way</h2>
            <div class="ptext">
              <p>Making things should feel like thinking out loud. Too much creative software asks you to stop, change tool, export, import, and then remember what you were doing. Infinite exists to remove those stops.</p>
              <p>The name is a promise about reach, not size. When connections are free, the number of things you can make has no obvious end.</p>
              <p>It is free and open source, and it runs on macOS, Windows and Linux. A tool that frees people to make should not be a privilege.</p>
            </div>
          </div>
          <blockquote class="pull"><p>Anything patches into anything.</p><ul><li>A sound moves a picture.</li><li>A picture moves a sound.</li><li>A line of code moves both.</li></ul><cite>The belief behind Infinite</cite></blockquote>
        </div>
      </section>

      <section class="aud-section" id="who">
        <h2>One canvas, many people</h2>
        <p class="sec-sub">The same graph serves very different work. Pick the page that is closest to yours.</p>
        <div class="who-grid">
{who}
        </div>
      </section>

      <section class="aud-section reveal" id="graph">
        <div class="split">
          <div class="split-text">
            <h2>A graph, because ideas connect</h2>
            <div class="ptext">
              <p>A timeline puts your work in a line. A stack of menus puts it in a hierarchy. Thinking does neither: one idea leads to three others, and two of those meet again later. A patch is a map of exactly that. Each node does one job, each cable says what feeds what, and you can see the whole shape of what you are making at once.</p>
              <p>A node graph is also a mind map. Brains are not patches, and we do not claim they are, but the comparison is useful: parts that do different jobs, joined by many connections, where the interesting results come from what talks to what.</p>
              <p>Switch between the two views. The connections stay the same; only the names change.</p>
            </div>
          </div>
          <figure class="mind" data-view="mind">
            <div class="seg" role="group" aria-label="View"><button type="button" data-v="mind" aria-pressed="true">A mind</button><button type="button" data-v="patch" aria-pressed="false">A patch</button></div>
            {mind_svg()}
            <figcaption class="mind-cap" aria-live="polite">Tap or hover an idea to see what it connects to. One thought, in Ember, travels the connections.</figcaption>
          </figure>
        </div>
      </section>

      <section class="aud-section reveal" id="flow">
        <div class="split flip">
          <figure class="loop-fig">{loop_svg()}<figcaption>Two ends, one loop. Ember is the person acting, Signal is the structure that answers, and Midnight is the space between.</figcaption></figure>
          <div class="split-text">
            <h2>Flow, and what breaks it</h2>
            <div class="ptext">
              <p>Flow is the state where doing and thinking are the same act. It breaks easily: a dialog, a loading bar, a menu three levels deep, a sound that arrives late. A stream of ideas needs somewhere to land at the speed it arrives.</p>
              <p>Infinite is designed around not breaking that stream, whether you are making music, an image or a live set. Four things are protected.</p>
            </div>
          </div>
        </div>
        <div class="aud-grid four">
{prot}
        </div>
      </section>

      <section class="aud-section reveal" id="machines">
        <h2>The machine follows you</h2>
        <p class="sec-sub">A good instrument does what the hand means, then gets out of the way. The machine listens, carries connections and offers; the person decides.</p>
        <div class="aud-grid">
{mach}
        </div>
      </section>

      <section class="aud-section reveal" id="design">
        <h2>How it is designed</h2>
        <p class="sec-sub">The look can change. The movement and the beliefs do not.</p>
        <ol class="principles">
{prin}
        </ol>
        <div class="springs-card">
          <div><h3>Four springs, one family</h3><p>Press one. Each is a measured curve from the brand book, tuned so the peak of its overshoot lands on a beat at 120 BPM.</p></div>
          <div class="springs">{springs}</div>
        </div>
      </section>

      <section class="aud-section reveal" id="faq">
        <h2>Questions</h2>
        <div class="faq">
{faq}
        </div>
      </section>

      <section class="aud-section reveal cta-end">
        <h2>Make the first thing</h2>
        <p class="sec-sub">Download Infinite, open a library patch, and turn one knob.</p>
        <div class="aud-cta"><a href="index.html#setup" class="btn btn-primary">Download Infinite</a><a href="library.html" class="btn btn-ghost">Open the library</a></div>
      </section>
    </div>
'''


def ld():
    page = {"@context": "https://schema.org", "@type": "AboutPage", "name": TITLE, "url": BASE + "philosophy.html", "description": DESC,
            "isPartOf": {"@type": "WebSite", "name": "Infinite", "url": BASE},
            "about": {"@type": "SoftwareApplication", "name": "Infinite", "applicationCategory": "MultimediaApplication", "operatingSystem": "macOS, Windows, Linux", "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"}}}
    faq = {"@context": "https://schema.org", "@type": "FAQPage", "mainEntity": [{"@type": "Question", "name": q, "acceptedAnswer": {"@type": "Answer", "text": a}} for q, a in FAQ]}
    crumbs = {"@context": "https://schema.org", "@type": "BreadcrumbList", "itemListElement": [{"@type": "ListItem", "position": 1, "name": "Infinite", "item": BASE}, {"@type": "ListItem", "position": 2, "name": "Philosophy", "item": BASE + "philosophy.html"}]}
    return [page, faq, crumbs]


def build():
    return ga.shell("philosophy.html", TITLE, DESC, ld(), main_html(), extra_head='\n  <link rel="stylesheet" href="philosophy.css?v=6">', extra_js='\n  <script src="philosophy.js?v=3" defer></script>')


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
