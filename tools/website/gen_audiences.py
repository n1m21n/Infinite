#!/usr/bin/env python3
"""Builds the audience pages: artists, musicians, VJs, creative coders and developers.

What: writes website/artists.html, musicians.html, vjs.html, creative-coders.html and developers.html from the AUDIENCES table below:
      a hero with a live WebGL shader and a 3D render, three cards, a bench of real node screenshots, a lab you can play with,
      a reel of real patches, starting points that link to library pages, and the other audiences.
      Styles are website/audience.css and behaviour is website/audience.js; the developers page also reads tools/website/developers_body.html.
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
     "lead": "Build images that move. Layer shapes, video and 3D, add trails, and let a sound or an LFO move any knob. No code needed.",
     "facts": ["Free", "macOS, Windows, Linux", "No code to start"],
     "hero": {"shader": "ink", "img": "brand_ball", "alt": "A 3D iridescent ball, the Infinite brand ball", "tall": False},
     "cards": [("32 blend modes", "Stack any picture on any other: images, folders of stills, hardware-accelerated video, camera, paint and noise ramps."),
               ("3D with real materials", "Procedural geometry, lights and a Render 3D node, so a shape can turn, instance and throw shadows in real time."),
               ("Write a picture as a formula", "Field Pixel and the live GLSL editor turn a few lines into a shader. Presets show how, and every value can be modulated.")],
     "bench": {"title": "Nodes you will patch first", "sub": "Each box is a node. Cables carry pictures from one to the next, and every number on it is a knob.",
               "items": [("shape", "Shape", "Draw a shape with fill, stroke and feather."), ("blend", "Blend", "Stack one picture on another with 32 blend modes."), ("trails", "Trails", "Leave a fading trail: decay, zoom, rotate and hue shift."),
                         ("glitch", "Glitch", "Tear the picture into slices. Amount, speed and seed."), ("kaleidoscope", "Kaleidoscope", "Mirror the picture into segments.")]},
     "lab": {"kind": "shader", "shader": "orb", "s": 0, "title": "Turn a knob, change the picture",
             "text": "This orb is the Iridescent Orb preset from the library, running here as a shader. In Infinite every value below is a knob you can also hand to an LFO or to a sound.",
             "ctl": [("size", "Size", 455, 0.05, 0.6, 2, ""), ("wobble", "Wobble", 267, 0.0, 0.15, 2, "")],
             "code": "param float size = 0.3 [0.05, 0.6];\nparam float wobble = 0.04 [0.0, 0.15];\n// ...\nrad = size * (1.0 + wobble * sin(3.0 * ang + t * 1.1)\n  + wobble * 0.5 * sin(5.0 * ang - t * 1.3));\nedge = 1.0 - smoothstep(rad - aa, rad, r);",
             "note": "Field source from the Iridescent Orb preset. The canvas is the same formula as WebGL."},
     "reel": {"title": "Made in Infinite", "sub": "Pieces from the art gallery and starting points from the library.", "href": "artworks.html",
              "items": [("assets/museum/06.jpg", 720, 960, "From the art gallery"), ("assets/library/previews/iridescent-orb.jpg", 900, 600, "Iridescent orb, library"), ("assets/museum/10.jpg", 720, 960, "From the art gallery"),
                        ("assets/library/previews/liquid-glass.jpg", 900, 600, "Liquid glass, library"), ("assets/museum/08.jpg", 720, 960, "From the art gallery"), ("assets/library/previews/ordered-dither.jpg", 900, 600, "Ordered dither, library"), ("assets/museum/02.jpg", 720, 960, "From the art gallery")]},
     "start": [("first-image", "First image"), ("layered-blends", "Layered blends"), ("glitch-feedback", "Glitch feedback"), ("iridescent-orb", "Iridescent orb"), ("trail-glitch", "Trail glitch")],
     "extra": ("artworks.html", "See the art gallery")},
    {"slug": "musicians", "name": "musicians", "hand": "play",
     "title": "Infinite for musicians: synths, effects and plugins in one graph",
     "h1": "Infinite for musicians", "desc": "Wavetable, FM, analog and string synths, tape delay, convolution reverb, VST3 and AU hosting, and modulation on every knob.",
     "lead": "Patch a sound the way you think about it. Notes go into a synth, then effects, and any knob can follow an LFO or an envelope.",
     "facts": ["VST3 and AU hosting", "MPE notes", "79 effect presets"],
     "hero": {"shader": "wave", "img": "node_lfo", "alt": "A 3D render of the LFO node", "tall": False},
     "cards": [("Synths with depth", "Wavetable, FM with four operators, Analog, a modelled String, Partials resynthesis and a Metallic resonator. Chords on Analog cost about a third of the CPU they did before v0.6."),
               ("Effects that colour", "Saturator with three stages, Convolution Reverb you design or load, tape Delay with wow and flutter, Dynamics in clean, bus, opto and FET styles, and 79 factory presets."),
               ("Your plugins, modulated", "Host VST3 and Audio Unit plugins, give them a sidechain, and move their knobs with LFOs, MSEG, envelopes or audio. MPE notes reach each voice.")],
     "bench": {"title": "A signal chain, node by node", "sub": "Notes in, a voice, colour, then out. Each node is one job, and each knob can be modulated.",
               "items": [("midi-notes", "MIDI Notes", "Notes go into the graph from here."), ("analog", "Analog", "Oscillators, a filter and an envelope in one voice."), ("fm", "FM", "Four operators."),
                         ("delay", "Delay", "Tape delay with wow and flutter."), ("dynamics", "Dynamics", "Clean, bus, opto and FET styles."), ("lfo", "LFO", "Moves any knob in the patch.")]},
     "lab": {"kind": "synth", "title": "Move the cutoff, then let an LFO move it",
             "text": "A sawtooth through a low-pass filter. Drag the cutoff to hear the shape change, then raise the LFO and watch the knob move itself. In Infinite this is a Field Synth, a filter and an LFO node.",
             "ctl": [("cut", "Cutoff", 550, 80, 7240, 0, " Hz"), ("lfo", "LFO amount", 400, 0, 100, 0, "%")],
             "code": "state float phase = 0\nphase = (phase + freq / sr) % 1.0\nsaw = phase * 2.0 - 1.0\nout = saw * 0.4 * gate",
             "note": "The four lines are a Field Synth. This demo uses your browser's audio, so you can hear the idea."},
     "reel": {"title": "Patches to open", "sub": "Each is a free patch with a picture of the graph.", "href": "library.html",
              "items": [("assets/community/pad-wash.webp", 960, 600, "Pad wash"), ("assets/community/delay-wash.webp", 960, 600, "Delay wash"), ("assets/community/drum-reverb.webp", 960, 600, "Drum reverb"),
                        ("assets/community/stretched-sampler.webp", 960, 600, "Stretched sampler"), ("assets/community/oscillator-reverb.webp", 960, 600, "Oscillator reverb"), ("assets/community/filtered-mixer.webp", 960, 600, "Filtered mixer")]},
     "start": [("first-sound", "First sound"), ("synths", "Synths"), ("audio-effects", "Audio effects"), ("pad-wash", "Pad wash"), ("delay-wash", "Delay wash"), ("stretched-sampler", "Stretched sampler")],
     "extra": ("blog/modulate-vst3-plugins-with-visuals-and-lfos.html", "Modulate a plugin: the guide")},
    {"slug": "vjs", "name": "VJs", "hand": "perform",
     "title": "Infinite for VJs: live visuals that follow the music",
     "h1": "Infinite for VJs", "desc": "Sound-driven visuals with Syphon, Spout and NDI out, Show Set patch switching on the bar, a show lock and a panic key. Free.",
     "lead": "Run visuals that follow the sound, send them to a projector, and switch patches on the bar without a black frame.",
     "facts": ["Syphon, Spout, NDI", "Switch on the bar", "Show lock and panic"],
     "hero": {"shader": "pulse", "img": "node_hero", "alt": "A 3D render of an Infinite node with a cable", "tall": False},
     "cards": [("Send it anywhere", "Syphon on macOS, Spout on Windows and NDI over the network, in and out, so Infinite can feed a projector, a mixer or another app."),
               ("Switch on the bar", "Show Set steps through patches with PageDown and PageUp, a MIDI program change or OSC. The switch lands on the next bar, the old sound fades under the new, and the projector never goes black."),
               ("Safe on stage", "Show lock stops accidental deletes and drags while knobs still play. Panic silences everything and freezes the projectors. A pre-show check lists missing media, plugins and MIDI devices.")],
     "bench": {"title": "A rig in five nodes", "sub": "Pictures in, sound to drive them, effects, then out to the projector.",
               "items": [("video", "Video", "Hardware-accelerated video in."), ("audio-analyze", "Audio Analyze", "Turns sound into numbers a knob can follow."), ("trails", "Trails", "A fading trail on every beat."),
                         ("syphon-out", "Syphon Out", "Send the picture to another app on macOS."), ("ndi-out", "NDI Out", "Send it over the network.")]},
     "lab": {"kind": "show", "title": "Switch on the bar",
             "text": "Tap a scene while the beat runs. It waits for the next bar and then fades in, the way Show Set switches patches. Change the tempo and the visuals follow.",
             "ctl": [("bpm", "Tempo", 500, 60, 180, 0, " BPM")],
             "pads": ["Tunnel", "Kaleidoscope", "Grid"],
             "note": "A browser demo of the idea. In the app each scene is a whole patch."},
     "reel": {"title": "Looks to start from", "sub": "Free patches with a picture of the result.", "href": "library.html",
              "items": [("assets/community/glitch-feedback.webp", 960, 600, "Glitch feedback"), ("assets/community/trail-glitch.webp", 960, 600, "Trail glitch"), ("assets/community/occult-mandala.webp", 960, 600, "Occult mandala"),
                        ("assets/community/lens-spectral.webp", 960, 600, "Lens spectral"), ("assets/community/terrain-mandala.webp", 640, 640, "Terrain mandala"), ("assets/community/layered-blends.webp", 960, 600, "Layered blends")]},
     "start": [("first-music-image", "Moves with the music"), ("glitch-feedback", "Glitch feedback"), ("trail-glitch", "Trail glitch"), ("occult-mandala", "Occult mandala"), ("lens-spectral", "Lens spectral")],
     "extra": ("blog/free-touchdesigner-alternative-for-audio-reactive-visuals.html", "Coming from TouchDesigner?")},
    {"slug": "creative-coders", "name": "creative coders and technologists", "hand": "code",
     "title": "Infinite for creative coders and technologists: Field, sketches and shaders",
     "h1": "Infinite for creative coders", "desc": "Write shaders, per-sample audio kernels, 3D surfaces and JavaScript sketches in one node graph with live feedback. Free and open source.",
     "lead": "Write what a node does. Field is Infinite's own small language: one program can be a shader, a synth, an effect or a mesh deformer.",
     "facts": ["Field language", "JavaScript sketches", "OSC on 127.0.0.1:9100"],
     "hero": {"shader": "dots", "img": "node_field_pixel", "alt": "A 3D render of the Field Pixel node", "tall": False},
     "cards": [("One language, many nodes", "Field Pixel, Field Synth, Field Effect, Field Notes, Field Primitive and Field Modifier share one syntax. Types and rates are inferred, so a sine reads the same on the GPU and at 48 kHz."),
               ("JavaScript sketches", "A Sketch node draws 2D images from JavaScript, and a 3D sketch builds meshes. The library has Lorenz and Clifford attractors, Lotka-Volterra and Rule 110, running live."),
               ("Everything can be driven", "Modulate any knob, script a patch over OSC on 127.0.0.1:9100, or render it headless from the command line. Patches are plain text.")],
     "bench": {"title": "Where your code runs", "sub": "Each of these nodes takes a few lines of Field or JavaScript.",
               "items": [("fieldpixel", "Field Pixel", "A shader in a few lines."), ("field-synth", "Field Synth", "A per-sample audio kernel."), ("field-modifier", "Field Modifier", "Deform and shade a mesh."),
                         ("sketch", "Sketch", "Draw from JavaScript."), ("render-3d", "Render 3D", "Lights, materials and shadows.")]},
     "lab": {"kind": "shader", "shader": "dots", "s": 1, "title": "Change two numbers",
             "text": "This is the Phyllotaxis Bloom preset from the library: each seed sits at the golden angle from the last. Change the seed count and the turn, the two params the preset exposes.",
             "ctl": [("turn", "turn", 544, -90, 90, 0, " deg/s"), ("seeds", "seeds", 250, 60, 1500, 0, "")],
             "code": "param float seeds = 420.0 [60.0, 1500.0];\nparam float turn = 8.0 [-90.0, 90.0];\n// ...\nsc = 0.46 / sqrt(seeds);\nrot = turn * 0.0174533 * t;\nsa0 = kk0 * 2.399963 + rot;   // golden angle",
             "note": "Abridged Field source from the Phyllotaxis Bloom preset. The canvas is the same formula as WebGL."},
     "reel": {"title": "Maths you can open", "sub": "Each one is a free patch that runs live.", "href": "library.html",
              "items": [("assets/library/previews/lorenz-attractor.jpg", 900, 600, "Lorenz attractor"), ("assets/library/previews/clifford-attractor.jpg", 900, 600, "Clifford attractor"), ("assets/library/previews/rule-110.jpg", 900, 600, "Rule 110"),
                        ("assets/library/previews/lotka-volterra.jpg", 900, 600, "Lotka-Volterra"), ("assets/library/previews/scale-free-network.jpg", 900, 600, "Scale-free network"), ("assets/library/previews/phyllotaxis-bloom.jpg", 900, 600, "Phyllotaxis bloom")]},
     "start": [("field", "Field templates"), ("lorenz-attractor", "Lorenz attractor"), ("clifford-attractor", "Clifford attractor"), ("rule-110", "Rule 110"), ("lotka-volterra", "Lotka-Volterra")],
     "extra": ("blog/field-language-write-what-a-node-does.html", "Read: Field in one post")},
    {"slug": "developers", "name": "developers", "hand": "headless", "body": "developers_body.html",
     "title": "Infinite for developers: headless CLI, Python SDK and MCP server",
     "h1": "Infinite for developers and agents", "desc": "Run Infinite without a window. Render video and audio from a text patch, call it from Python, or give an AI agent the nine MCP tools. Free and open source.",
     "lead": "Every patch is a text file the app can render with no window open. Use the command line, Python or an AI agent over MCP.",
     "facts": ["Headless", "Python SDK", "9 MCP tools"],
     "hero": {"shader": "sheet", "img": "patch", "alt": "A 3D render of two Infinite nodes joined by a cable", "tall": False},
     "cards": [("Text in, files out", "A patch is plain text. Validate it, print the graph that was built, then render stills, a contact sheet, video or an audio report."),
               ("No window, no display", "The renderer runs from the command line, so it fits a script, a cron job or a CI step. Hardware inputs are refused in headless mode."),
               ("Built for agents", "Nine MCP tools let an agent write a shader, a synth or a mesh and see the result. Every tool result carries the credit line.")],
     "lab": {"kind": "term", "title": "Three ways in",
             "text": "Pick a way to call the same engine. The sheet on the right shows the idea: a patch rendered to frames, one after another.",
             "tabs": [("Command line", 'Infinite --validate hello.inf\nInfinite --explain hello.inf\nInfinite --frame hello.inf 0,1,2 pics/ --contact-sheet sheet.png\nInfinite --render hello.inf hello.mp4 --duration 4 --fps 30\nInfinite --audio-summary hello.inf hello.json',
                       'The <a href="assets/Infinite_Headless_User_Guide.pdf" target="_blank" rel="noopener">headless guide (PDF)</a> covers every flag and exit code.'),
                      ("Python", 'pip install infinite-daw\npython3\n>>> import infinite\n>>> infinite.field_synth("state float phase = 0\\nphase = (phase + freq / sr) % 1.0\\nout = sin(6.28318 * phase) * 0.4 * gate", out_wav="synth.wav", duration=3)',
                       'Available on PyPI. <a href="https://pypi.org/project/infinite-daw/" target="_blank" rel="noopener">PyPI package</a> and <a href="https://github.com/n1m21n/Infinite/tree/main/tools/python_sdk" target="_blank" rel="noopener">source</a>.'),
                      ("MCP", 'npx -y infinite-engine init\n\n# Or manual mcp.json:\n{\n  "mcpServers": {\n    "infinite": {\n      "command": "npx",\n      "args": ["-y", "infinite-engine", "serve"]\n    }\n  }\n}',
                       'One command configures Claude Desktop, Cursor and Windsurf. <a href="https://www.npmjs.com/package/infinite-engine" target="_blank" rel="noopener">npm package</a> and <a href="https://smithery.ai" target="_blank" rel="noopener">Smithery registry</a>.')],
             "note": "Illustration of the flow; the guide lists the real output files."},
     "start": [], "extra": ("assets/Infinite_Headless_User_Guide.pdf", "Headless guide (PDF)")},
]
ALL = [(a["slug"] + ".html", a["name"]) for a in AUDIENCES]
AUD_ICON = {"artists": "family-compositing", "musicians": "family-notes", "vjs": "perform", "creative-coders": "edit", "developers": "cube"}


def icon(name):
    """Brand glyph from art/icons/src, inlined so a pill needs no request (BRAND.md section 7: icon beside a label)."""
    svg = open(os.path.join(chrome.ROOT, "art", "icons", "src", name + "-20.svg"), encoding="utf-8").read().strip()
    return svg.replace("<svg ", '<svg class="ic" aria-hidden="true" ', 1).replace("\n", "")


def check_links():
    bad = []
    for a in AUDIENCES:
        for sl, _ in a["start"]:
            if not os.path.exists(os.path.join(W, "library", sl + ".html")):
                bad.append("library/" + sl + ".html")
        need = [a["extra"][0]]
        need += ["assets/nodes/%s.webp" % n for n, _, _ in a.get("bench", {}).get("items", [])]
        need += [r[0] for r in a.get("reel", {}).get("items", [])]
        for f in need:
            if not os.path.exists(os.path.join(W, f)):
                bad.append(f)
    return bad


def hl(code):
    out = []
    for line in code.split("\n"):
        t = e(line)
        if "//" in t:
            i = t.index("//"); t = t[:i] + '<span class="c">' + t[i:] + "</span>"
        for k in ("param float", "state float"):
            t = t.replace(k, '<span class="k">' + k + "</span>")
        out.append(t)
    return "\n".join(out)


def ctl(c):
    cid, label, val, lo, hi, dp, unit = c
    fmt = "" if cid == "cut" else ' data-fmt="1"'
    return (f'<div class="ctl"><label for="c-{cid}">{e(label)}</label><output for="c-{cid}"></output>'
            f'<input id="c-{cid}" type="range" min="0" max="1000" step="1" value="{val}"{fmt} data-lo="{lo}" data-hi="{hi}" data-dp="{dp}" data-unit="{e(unit)}"></div>')


def lab_html(a):
    L = a["lab"]; k = L["kind"]
    side = f'<h3>{e(L["title"])}</h3>'
    ctls = "".join(ctl(c) for c in L.get("ctl", []))
    code = f'<pre class="aud-code"><code>{hl(L["code"])}</code></pre>' if L.get("code") else ""
    note = ""
    if k == "shader":
        return f'''<div class="lab" data-lab="shader" data-shader="{L["shader"]}" data-s="{L.get("s", 0)}">
          <div class="lab-view"><canvas aria-label="A live shader you can change with the sliders" role="img"></canvas></div>
          <div class="lab-side">{side}{ctls}{code}{note}</div></div>'''
    if k == "synth":
        return f'''<div class="lab" data-lab="synth">
          <div class="lab-view"><canvas aria-label="A sawtooth waveform through a low-pass filter" role="img"></canvas></div>
          <div class="lab-side">{side}{ctls}<button class="play" type="button" aria-pressed="false">Play a note</button>{code}{note}</div></div>'''
    if k == "show":
        pads = "".join(f'<button class="pad{" live" if i == 0 else ""}" type="button"><span>{e(n)}</span><small>scene {i + 1}</small></button>' for i, n in enumerate(L["pads"]))
        return f'''<div class="lab" data-lab="show">
          <div class="lab-view"><canvas aria-label="Beat-driven visuals that switch scene on the bar" role="img"></canvas></div>
          <div class="lab-side">{side}{ctls}<div class="pads">{pads}</div>
            <div class="beats" aria-hidden="true"><i class="on"></i><i></i><i></i><i></i><span class="bar-state" aria-live="polite">Playing scene 1</span></div>{note}</div></div>'''
    tabs = "".join(f'<button role="tab" type="button" id="t{i}" aria-controls="p{i}" aria-selected="{"true" if i == 0 else "false"}">{e(n)}</button>' for i, (n, _, _) in enumerate(L["tabs"]))
    panels = "".join(f'<div role="tabpanel" id="p{i}" aria-labelledby="t{i}" class="term-panel"{"" if i == 0 else " hidden"}><div style="position:relative"><pre class="aud-code"><code>{e(c)}</code></pre><button class="copy" type="button">Copy</button></div><p>{h}</p></div>' for i, (_, c, h) in enumerate(L["tabs"]))
    return f'''<div class="lab" data-lab="term">
          <div class="lab-side">{side}<div class="cmd-tabs" role="tablist" aria-label="Ways to call Infinite">{tabs}</div>{panels}<button class="run" type="button">Render again</button>{note}</div>
          <div class="lab-view"><canvas aria-label="A contact sheet of twelve frames filling in one by one" role="img"></canvas></div></div>'''


def png_size(n):
    from PIL import Image   # the shots are 2x captures: width and height are CSS pixels, half the file
    with Image.open(os.path.join(W, "assets", "nodes", n + ".webp")) as im:
        w, h = im.size
    return f' width="{w // 2}" height="{h // 2}"'


def bench_image(slug, items):
    """One landscape strip of the node shots, equal height, on a transparent ground (the card supplies midnight)."""
    from PIL import Image
    H, GAP, PAD = 380, 32, 8
    ims = []
    for n, _, _ in items:
        with Image.open(os.path.join(W, "assets", "nodes", n + ".webp")) as im:
            im = im.convert("RGBA")
            ims.append(im.resize((round(im.width * H / im.height), H), Image.LANCZOS))
    width = sum(i.width for i in ims) + GAP * (len(ims) - 1) + 2 * PAD
    out = Image.new("RGBA", (width, H + 2 * PAD), (0, 0, 0, 0))
    x = PAD
    for i in ims:
        out.paste(i, (x, PAD), i)
        x += i.width + GAP
    out.save(os.path.join(W, "assets", "nodes", f"bench-{slug}.webp"), "WEBP", quality=82, method=6)
    return out.size


def main_html(a, rel):
    float3d = ""
    if a["hero"]["img"] == "brand_ball":
        float3d = f'          <span class="vstage-float"><img class="vstage-3d" src="assets/3d/brand_ball.webp" alt="{e(a["hero"]["alt"])}" width="700" height="700" decoding="async"></span>\n'
    facts = "".join(f"<li>{e(f)}</li>" for f in a["facts"])
    cards = "\n".join(f'          <article class="aud-card reveal" style="--i:{n}"><span class="num">{n + 1:02d}</span><h3>{e(t)}</h3><p>{e(d)}</p></article>' for n, (t, d) in enumerate(a["cards"]))
    bench = ""
    if a.get("bench"):
        B = a["bench"]
        w, h = bench_image(a["slug"], B["items"])
        names = "".join(f"<li>{e(t)}</li>" for _, t, _ in B["items"])
        alt = "; ".join(f"{t}: {d}" for _, t, d in B["items"])
        bench = f'''
      <section class="aud-section reveal">
        <h2>{e(B["title"])}</h2>
        <p class="sec-sub">{e(B["sub"])}</p>
        <div class="bench"><img class="bench-img" src="assets/nodes/bench-{a["slug"]}.webp" alt="{e(alt)}" width="{w}" height="{h}" loading="lazy" decoding="async"></div>
      </section>'''
    reel = ""
    if a.get("reel"):
        R = a["reel"]
        figs = "".join(f'<figure><a class="frame" style="--ar:{w} / {h}" href="{R["href"]}"><img src="{src}" alt="{e(c)}" width="{w}" height="{h}" loading="lazy" decoding="async"></a><figcaption>{e(c)}</figcaption></figure>' for src, w, h, c in R["items"])
        reel = f'''
      <section class="aud-section reveal">
        <div class="sec-head"><div><h2>{e(R["title"])}</h2><p class="sec-sub">{e(R["sub"])}</p></div>
          <div class="reel-ctl"><button class="reel-btn reel-prev" type="button" aria-label="Scroll back">&#8592;</button><button class="reel-btn reel-next" type="button" aria-label="Scroll forward">&#8594;</button></div></div>
        <div class="reel-wrap"><div class="reel">{figs}</div></div>
      </section>'''
    start = ""
    if a["start"]:
        links = "\n".join(f'          <a href="library/{sl}.html">{icon("play")}{e(t)}</a>' for sl, t in a["start"])
        start = f'''
      <section class="aud-section reveal">
        <h2>Start with</h2>
        <div class="aud-start">
{links}
        </div>
      </section>'''
    body = ""
    if a.get("body"):
        body = '      <section class="aud-section reveal dev-body">\n' + open(os.path.join(os.path.dirname(os.path.abspath(__file__)), a["body"]), encoding="utf-8").read() + "      </section>"
    lab = lab_html(a)
    others = "\n".join(f'          <a href="{h}">{icon(AUD_ICON[h[:-5]])}{e(n)}</a>' for h, n in ALL if h != rel)
    return f'''
  <main>
    <section class="ah">
      <div class="container ah-grid">
        <div class="ah-text">
          <span class="blog-hand hand">{e(a["hand"])}</span>
          <h1 class="slab">{e(a["h1"])}</h1>
          <p class="lead">{e(a["lead"])}</p>
          <div class="aud-cta">
            <a href="index.html#setup" class="btn btn-primary">Download Infinite</a>
            <a href="{a["extra"][0]}" class="btn btn-ghost">{e(a["extra"][1])}</a>
          </div>
          <ul class="facts">{facts}</ul>
        </div>
        <div class="vstage">
          <canvas data-shader="{a["hero"]["shader"]}" aria-hidden="true"></canvas>
{float3d}          <span class="vstage-tag">live shader · move the pointer</span>
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
{bench}
      <section class="aud-section reveal">
        {lab}
      </section>
{reel}
{start}
{body}
      <section class="aud-section reveal">
        <h2>Infinite for everyone else</h2>
        <div class="aud-others">
{others}
        </div>
      </section>
    </div>
    '''


def artists_main(a, rel, hero="blob"):
    """Immersive artists page: a live iridescent blob you steer, three glass bubbles that morph it, a swipe strip of nodes, knobs, a reel."""
    L = a["lab"]
    if hero == "cables":
        bub = [("synth", "family-synths", "Synths", a["cards"][0][0]), ("fx", "family-audio-effects", "Effects", a["cards"][1][0]), ("plug", "mod-matrix", "Plugins", a["cards"][2][0])]
        tag, caps, ghost = "Pull a cable.", "|".join(c[0] for c in a["cards"]), ("Guide", "library")
    else:
        bub = [("layers", "family-compositing", "Layer", a["cards"][0][0]), ("solid", "cube", "3D", a["cards"][1][0]), ("formula", "edit", "Formula", a["cards"][2][0])]
        tag, caps, ghost = "Images that move.", "32 blend modes|3D with real materials|A picture from a formula", ("Gallery", "viewport")
    bubbles = "".join(f'<button type="button" class="ax-bub{" on" if i == 0 else ""}" role="tab" aria-selected="{"true" if i == 0 else "false"}" data-k="{i}" aria-label="{e(c)}: {e(a["cards"][i][1])}">{icon(ic)}<span>{e(t)}</span></button>' for i, (_, ic, t, c) in enumerate(bub))
    nodes = "".join(f'<figure class="ax-node"><img src="assets/nodes/{n}.webp" alt="{e(t)}: {e(d)}" loading="lazy" decoding="async"><figcaption>{e(t)}</figcaption></figure>' for n, t, d in a["bench"]["items"])
    figs = "".join(f'<a class="ax-shot" style="--ar:{w} / {h}" href="{a["reel"]["href"]}"><img src="{src}" alt="{e(al)}" width="{w}" height="{h}" loading="lazy" decoding="async"></a>' for src, w, h, al in a["reel"]["items"])
    start = "".join(f'<a href="library/{sl}.html">{icon("play")}{e(t)}</a>' for sl, t in a["start"])
    others = "".join(f'<a href="{h}">{icon(AUD_ICON[h[:-5]])}{e(n)}</a>' for h, n in ALL if h != rel)
    lab = lab_html(a)
    lab = lab.replace('<h3>' + e(L["title"]) + '</h3>', '')
    import re
    lab = re.sub(r'(<pre class="aud-code">.*?</pre>)', lambda m: '<details class="ax-code"><summary>' + icon("edit") + '<span>Code</span></summary>' + m.group(1) + '</details>', lab, flags=re.S)
    return f'''
  <main class="ax-main">
    <section class="ax">
      <div class="container">
        <div class="ax-stage" data-k="0" data-hero="{hero}" data-caps="{e(caps)}">
          <canvas class="ax-gl" aria-hidden="true"></canvas>
          <div class="ax-copy">
            <span class="blog-hand hand">{e(a["hand"])}</span>
            <h1>{e(a["h1"])}</h1>
            <p>{e(tag)}</p>
          </div>
          <div class="ax-hint" aria-hidden="true">{icon("pointer")}<span>Drag</span></div>
          <div class="ax-low">
            <div class="ax-bubs" role="tablist" aria-label="What you can do">{bubbles}</div>
            <p class="ax-cap" aria-live="polite">{e(a["cards"][0][0])}</p>
          </div>
          <div class="ax-cta">
            <a href="index.html#setup" class="btn btn-primary">Download</a>
            <a href="{a["extra"][0]}" class="ax-ghost" aria-label="{e(a["extra"][1])}">{icon(ghost[1])}<span>{ghost[0]}</span></a>
          </div>
        </div>
      </div>
    </section>
    <div class="container">
      <section class="aud-section reveal">
        <h2 class="ax-h">Nodes</h2>
        <div class="ax-strip"><div class="ax-nodes">{nodes}</div></div>
      </section>
      <section class="aud-section reveal ax-lab">
        <h2 class="ax-h">Knobs</h2>
        {lab}
      </section>
      <section class="aud-section reveal">
        <h2 class="ax-h">Made in Infinite</h2>
        <div class="ax-strip"><div class="ax-reel">{figs}</div></div>
      </section>
      <section class="aud-section reveal">
        <h2 class="ax-h">Start with</h2>
        <div class="aud-start">{start}</div>
      </section>
      <section class="aud-section reveal">
        <h2 class="ax-h">For everyone else</h2>
        <div class="aud-others">{others}</div>
      </section>
    </div>
'''


def page(a):
    rel = a["slug"] + ".html"
    ld = {"@context": "https://schema.org", "@type": "WebPage", "name": a["title"], "url": BASE + rel, "description": a["desc"],
          "isPartOf": {"@type": "WebSite", "name": "Infinite", "url": BASE}, "about": {"@type": "SoftwareApplication", "name": "Infinite", "applicationCategory": "MultimediaApplication", "operatingSystem": "macOS, Windows, Linux", "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"}}}
    if a["slug"] == "artists":
        return shell(rel, a["title"], a["desc"], [ld], artists_main(a, rel), extra_head='\n  <link rel="stylesheet" href="artists.css?v=5f17f821">', extra_js='\n  <script src="artists.js?v=8" defer></script>')
    if a["slug"] == "musicians":
        return shell(rel, a["title"], a["desc"], [ld], artists_main(a, rel, "cables"), extra_head='\n  <link rel="stylesheet" href="artists.css?v=5f17f821">', extra_js='\n  <script src="artists.js?v=8" defer></script>\n  <script type="module" src="musicians.js?v=2"></script>')
    return shell(rel, a["title"], a["desc"], [ld], main_html(a, rel))


def shell(rel, title, desc, lds, main, extra_head="", extra_js=""):
    """The whole document around a page's <main>: head, one header, one footer, reveal-on-scroll. Shared with gen_philosophy.py."""
    url = BASE + rel
    ldtags = "\n  ".join(f'<script type="application/ld+json">{json.dumps(x, ensure_ascii=False)}</script>' for x in lds)
    faq = os.path.join(os.path.dirname(os.path.abspath(__file__)), "faq_%s.html" % rel.replace(".html", ""))
    if os.path.exists(faq):   # FAQPage schema kept as a file per audience page, so a regenerate does not drop it
        ldtags += "\n" + open(faq, encoding="utf-8").read().rstrip("\n")
    return f'''<!DOCTYPE html>
<html lang="en" data-mode="paper">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#FBFAF6">
  <script>window.INFINITE_DEFAULT_MODE = 'paper';</script>
  <script src="theme.js?v=2"></script>
  <title>{e(title)}</title>
  <meta name="description" content="{e(desc)}">
  <link rel="canonical" href="{url}">
  <meta property="og:title" content="{e(title)}">
  <meta property="og:description" content="{e(desc)}">
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
  <link rel="stylesheet" href="brand.css?v=e96b24bf">
  <link rel="stylesheet" href="style.css?v={chrome.CSS_VERSION}">
  <link rel="stylesheet" href="glass.css?v=c25dae44">
  <link rel="stylesheet" href="blog.css?v=61bae151">
  <link rel="stylesheet" href="audience.css?v=beb744f0">
  {ldtags}{extra_head}
  <script>document.documentElement.classList.add('js-reveal')</script>
</head>
<body class="blog-page">
  {chrome.header_html(rel)}

{main}
  </main>

  {chrome.footer_html(rel)}
  <script src="site.js?v={chrome.JS_VERSION}" defer></script>
  <script src="audience.js?v=6" defer></script>{extra_js}
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
