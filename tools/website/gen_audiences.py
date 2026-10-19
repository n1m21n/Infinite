#!/usr/bin/env python3
"""Builds the audience pages: artists, musicians, VJs, creative coders (developers.html is hand-written).

What: writes website/artists.html, musicians.html, vjs.html and creative-coders.html from the AUDIENCES table below:
      a hero with a live patch strip (nodes, cables, a travelling dot), three cards of what the person gets,
      starting points that link to real library pages, and the other audiences. Styles are website/audience.css.
Why:  one page per audience so people and agents land on their own use of Infinite. One template keeps motion,
      layout and brand identical; the facts live in one table that can be checked against the README.
Usage: python3 tools/website/gen_audiences.py     then python3 tools/website/sync_chrome.py
       python3 tools/website/gen_audiences.py --check
Exit codes: 0 written (or in sync), 1 differs under --check, 2 a linked page does not exist.
"""
import html, json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sync_chrome as chrome

W = chrome.W
BASE = "https://infinitedevices.org/"
e = html.escape
CAT = {"Source": "#4ADE80", "3D": "#38BDF8", "Compositing": "#818CF8", "Effects": "#FACC15", "Modulators": "#A3E635",
       "Notes": "#4CD964", "Synths": "#6992F6", "Audio FX": "#53C7E3", "Utility": "#A3A9BA", "Field": "#FC963C", "Output": "#A3A9BA"}

AUDIENCES = [
    {"slug": "artists", "name": "artists", "hand": "paint",
     "title": "Infinite for artists: images that move, made on one canvas",
     "h1": "Infinite for artists", "desc": "Layer shapes, video and 3D, add trails and glitch, and let sound move it. Free for macOS, Windows and Linux.",
     "lead": "Build images that move. Layer shapes, video and 3D, smear them with trails, and let a sound or an LFO move any knob. No code to start; code when you want it.",
     "flow": [("Source", "Shapes", "knobs"), ("Effects", "Glitch", "knobs"), ("Compositing", "Trails", "knobs"), ("Utility", "Output", "")],
     "cards": [("32 blend modes", "Stack any picture on any other: images, folders of stills, hardware-accelerated video, camera, paint and noise ramps."),
               ("3D with real materials", "Procedural geometry, lights and a Render 3D node, so a shape can turn, instance and throw shadows in real time."),
               ("Write a picture as a formula", "Field Pixel and the live GLSL editor turn a few lines into a shader. Presets show how, and every value can be modulated.")],
     "start": [("first-image", "First image"), ("layered-blends", "Layered blends"), ("glitch-feedback", "Glitch feedback"), ("iridescent-orb", "Iridescent orb"), ("trail-glitch", "Trail glitch")],
     "extra": ("artworks.html", "See the art gallery")},
    {"slug": "musicians", "name": "musicians", "hand": "play",
     "title": "Infinite for musicians: synths, effects and plugins in one graph",
     "h1": "Infinite for musicians", "desc": "Wavetable, FM, analog and string synths, tape delay, convolution reverb, VST3 and AU hosting, and modulation on every knob.",
     "lead": "Patch a sound the way you think about it. Notes go into a synth, the synth into effects, and any knob can be moved by an LFO, an envelope or something you can see.",
     "flow": [("Notes", "MIDI Notes", "knobs"), ("Synths", "Wavetable", "knobs"), ("Audio FX", "Delay", "knobs"), ("Utility", "Audio Out", "")],
     "cards": [("Synths with depth", "Wavetable, FM with four operators, Analog, a modelled String, Partials resynthesis and a Metallic resonator. Chords on Analog cost about a third of the CPU they did before v0.6."),
               ("Effects that colour", "Saturator with three stages, Convolution Reverb you design or load, tape Delay with wow and flutter, Dynamics in clean, bus, opto and FET styles, and 79 factory presets."),
               ("Your plugins, modulated", "Host VST3 and Audio Unit plugins, give them a sidechain, and move their knobs with LFOs, MSEG, envelopes or audio. MPE notes reach each voice.")],
     "start": [("first-sound", "First sound"), ("synths", "Synths"), ("audio-effects", "Audio effects"), ("pad-wash", "Pad wash"), ("delay-wash", "Delay wash"), ("stretched-sampler", "Stretched sampler")],
     "extra": ("blog/modulate-vst3-plugins-with-visuals-and-lfos.html", "Modulate a plugin: the guide")},
    {"slug": "vjs", "name": "VJs", "hand": "perform",
     "title": "Infinite for VJs: live visuals that follow the music",
     "h1": "Infinite for VJs", "desc": "Sound-driven visuals with Syphon, Spout and NDI out, Show Set patch switching on the bar, a show lock and a panic key. Free.",
     "lead": "Run visuals that follow the sound, send them to a projector or another app, and switch patches on the bar without a black frame. Built to stay up for a whole set.",
     "flow": [("Source", "Video", "knobs"), ("Compositing", "Trails", "knobs"), ("Effects", "Colour", "knobs"), ("Output", "Syphon Out", "")],
     "cards": [("Send it anywhere", "Syphon on macOS, Spout on Windows and NDI over the network, in and out, so Infinite can feed a projector, a mixer or another app."),
               ("Switch on the bar", "Show Set steps through patches with PageDown and PageUp, a MIDI program change or OSC. The switch lands on the next bar, the old sound fades under the new, and the projector never goes black."),
               ("Safe on stage", "Show lock stops accidental deletes and drags while knobs still play. Panic silences everything and freezes the projectors. A pre-show check lists missing media, plugins and MIDI devices.")],
     "start": [("first-music-image", "Moves with the music"), ("glitch-feedback", "Glitch feedback"), ("trail-glitch", "Trail glitch"), ("occult-mandala", "Occult mandala"), ("lens-spectral", "Lens spectral")],
     "extra": ("blog/free-touchdesigner-alternative-for-audio-reactive-visuals.html", "Coming from TouchDesigner?")},
    {"slug": "creative-coders", "name": "creative coders and technologists", "hand": "code",
     "title": "Infinite for creative coders and technologists: Field, sketches and shaders",
     "h1": "Infinite for creative coders", "desc": "Write shaders, per-sample audio kernels, 3D surfaces and JavaScript sketches in one node graph with live feedback. Free and open source.",
     "lead": "Write what a node does. Field is Infinite's own small language: one program can be a shader, a synth, an effect, a note pattern or a mesh deformer, and the compiler works out where it runs.",
     "flow": [("Field", "Field Pixel", "knobs"), ("Modulators", "LFO", "knobs"), ("3D", "Render 3D", ""), ("Utility", "Output", "")],
     "cards": [("One language, many nodes", "Field Pixel, Field Synth, Field Effect, Field Notes, Field Primitive and Field Modifier share one syntax. Types and rates are inferred, so a sine reads the same on the GPU and at 48 kHz."),
               ("JavaScript sketches", "A Sketch node draws 2D images from JavaScript, and a 3D sketch builds meshes. The library has Lorenz and Clifford attractors, Lotka-Volterra and Rule 110, running live."),
               ("Everything can be driven", "Modulate any knob, script a patch over OSC on 127.0.0.1:9100, or render it headless from the command line. Patches are plain text.")],
     "code": 'state float phase = 0\nphase = (phase + freq / sr) % 1.0\nsaw = phase * 2.0 - 1.0\nout = saw * 0.4 * gate',
     "code_note": "A Field Synth: a per-sample sawtooth. The same four lines run in a Field Synth node and in headless renders.",
     "start": [("field", "Field templates"), ("lorenz-attractor", "Lorenz attractor"), ("clifford-attractor", "Clifford attractor"), ("rule-110", "Rule 110"), ("lotka-volterra", "Lotka-Volterra")],
     "extra": ("blog/field-language-write-what-a-node-does.html", "Read: Field in one post")},
]
ALL = [(a["slug"] + ".html", a["name"]) for a in AUDIENCES]


def check_links():
    bad = []
    for a in AUDIENCES:
        for s, _ in a["start"]:
            if not os.path.exists(os.path.join(W, "library", s + ".html")):
                bad.append("library/" + s + ".html")
        if not os.path.exists(os.path.join(W, a["extra"][0])):
            bad.append(a["extra"][0])
    return bad


def page(a):
    rel = a["slug"] + ".html"
    url = BASE + rel
    nodes = []
    for n, (cat, name, knobs) in enumerate(a["flow"]):
        if n:
            nodes.append(f'<span class="cable" style="--i:{n}" aria-hidden="true"></span>')
        k = '<span class="knob" aria-hidden="true">' + "".join(f'<i style="--r:{r}deg;--i:{n + j}"></i>' for j, r in enumerate((-40, 20, 70))) + "</span>" if knobs else ""
        nodes.append(f'<div class="patch-node" style="--i:{n};--c:{CAT.get(cat, "#818CF8")}"><span class="cat">{e(cat)}</span><b>{e(name)}</b>{k}</div>')
    cards = "\n".join(f'          <article class="aud-card reveal" style="--i:{n}"><h3>{e(t)}</h3><p>{e(d)}</p></article>' for n, (t, d) in enumerate(a["cards"]))
    start = "\n".join(f'          <a href="library/{s}.html">{e(t)}</a>' for s, t in a["start"])
    code = ""
    if a.get("code"):
        code = f'''
        <section class="aud-section reveal">
          <h2>What it looks like</h2>
          <pre class="aud-code"><code>{e(a["code"])}</code></pre>
          <p class="aud-note">{e(a["code_note"])}</p>
        </section>'''
    others = "\n".join(f'          <a href="{h}">{e(n)}</a>' for h, n in [("developers.html", "developers")] + ALL if h != rel)
    ld = {"@context": "https://schema.org", "@type": "WebPage", "name": a["title"], "url": url, "description": a["desc"],
          "isPartOf": {"@type": "WebSite", "name": "Infinite", "url": BASE}, "about": {"@type": "SoftwareApplication", "name": "Infinite", "applicationCategory": "MultimediaApplication", "operatingSystem": "macOS, Windows, Linux", "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"}}}
    return f'''<!DOCTYPE html>
<html lang="en" data-mode="paper">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#FBFAF6">
  <script>window.INFINITE_DEFAULT_MODE = 'paper';</script>
  <script src="theme.js?v=2"></script>
  <title>{e(a["title"])}</title>
  <meta name="description" content="{e(a["desc"])}">
  <link rel="canonical" href="{url}">
  <meta property="og:title" content="{e(a["title"])}">
  <meta property="og:description" content="{e(a["desc"])}">
  <meta property="og:image" content="{BASE}assets/og-image.jpg">
  <meta property="og:url" content="{url}">
  <meta property="og:type" content="website">
  <meta property="og:site_name" content="Infinite">
  <meta name="twitter:card" content="summary_large_image">
  <link rel="alternate" type="text/plain" href="llms.txt" title="Plain-text summary for language models">
  <link rel="icon" href="assets/favicon.svg" type="image/svg+xml">
  <link rel="icon" href="assets/favicon-32.png" sizes="32x32">
  <link rel="apple-touch-icon" href="assets/apple-touch-icon.png">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Caveat:wght@600;700&family=Geist:wght@400;500;600;700;800&family=Geist+Mono:wght@400;500;600&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="brand.css?v=3">
  <link rel="stylesheet" href="style.css?v={chrome.CSS_VERSION}">
  <link rel="stylesheet" href="glass.css?v=4">
  <link rel="stylesheet" href="blog.css?v=2">
  <link rel="stylesheet" href="audience.css?v=1">
  <script type="application/ld+json">{json.dumps(ld, ensure_ascii=False)}</script>
  <script>document.documentElement.classList.add('js-reveal')</script>
</head>
<body class="blog-page">
  {chrome.header_html(rel)}

  <main>
    <section class="section aud-hero">
      <div class="container">
        <span class="blog-hand hand">{e(a["hand"])}</span>
        <h1 class="slab">{e(a["h1"])}</h1>
        <p class="lead">{e(a["lead"])}</p>
        <div class="aud-cta">
          <a href="index.html#setup" class="btn btn-primary">Download Infinite</a>
          <a href="{a["extra"][0]}" class="btn btn-ghost">{e(a["extra"][1])}</a>
        </div>
        <div class="patch" role="img" aria-label="A patch: {e(' into '.join(n for _, n, _ in a['flow']))}">
          {"".join(nodes)}
        </div>
      </div>
    </section>

    <div class="container">
      <section class="aud-section">
        <h2>What you get</h2>
        <div class="aud-grid">
{cards}
        </div>
      </section>
{code}
      <section class="aud-section reveal">
        <h2>Start with</h2>
        <div class="aud-start">
{start}
        </div>
        <p class="aud-note">Each one is a free patch you can download and open.</p>
      </section>

      <section class="aud-section reveal">
        <h2>Infinite for everyone else</h2>
        <div class="aud-others">
{others}
        </div>
      </section>
    </div>
  </main>

  {chrome.footer_html(rel)}
  <script src="site.js?v={chrome.JS_VERSION}" defer></script>
  <script>
    (function () {{
      var els = document.querySelectorAll('.reveal');
      if (!('IntersectionObserver' in window)) {{ els.forEach(function (x) {{ x.classList.add('in'); }}); return; }}
      var io = new IntersectionObserver(function (es) {{ es.forEach(function (en) {{ if (en.isIntersecting) {{ en.target.classList.add('in'); io.unobserve(en.target); }} }}); }}, {{ threshold: 0.15 }});
      els.forEach(function (x) {{ io.observe(x); }});
    }})();
  </script>
</body>
</html>
'''


def main():
    bad = check_links()
    if bad:
        print("missing:", ", ".join(bad)); return 2
    check = "--check" in sys.argv
    diff = []
    for a in AUDIENCES:
        p = os.path.join(W, a["slug"] + ".html")
        new = page(a)
        old = open(p, encoding="utf-8").read() if os.path.exists(p) else ""
        if old != new:
            diff.append(a["slug"] + ".html")
            if not check:
                open(p, "w", encoding="utf-8").write(new)
    print(("differ: " if check else "written: ") + (", ".join(diff) or "none"))
    return 1 if (check and diff) else 0


if __name__ == "__main__":
    sys.exit(main())
