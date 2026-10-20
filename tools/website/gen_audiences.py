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
     "cards": [("Layers and blends", "Stack any picture on any other: images, folders of stills, hardware-accelerated video, camera, paint and noise ramps."),
               ("3D with real materials", "Procedural geometry, lights and a Render 3D node, so a shape can turn, instance and throw shadows in real time."),
               ("Write a picture as a formula", "Field Pixel and the live GLSL editor turn a few lines into a shader. Presets show how, and every value can be modulated.")],
     "bench": {"title": "Nodes you will patch first", "sub": "Each box is a node. Cables carry pictures from one to the next, and every number on it is a knob.",
               "items": [("shape", "Shape", "Draw a shape with fill, stroke and feather."), ("blend", "Blend", "Stack one picture on another with a blend mode."), ("trails", "Trails", "Leave a fading trail: decay, zoom, rotate and hue shift."),
                         ("glitch", "Glitch", "Tear the picture into slices. Amount, speed and seed."), ("kaleidoscope", "Kaleidoscope", "Mirror the picture into segments.")]},
     "lab": {"kind": "print", "title": "Print it in dots",
             "text": "A halftone screen, the way a poster is printed: each dot's size is the brightness under it. Pick a picture, change the dot and the angle, and move the light with your pointer.",
             "pics": ["Orb", "Rings", "Clouds"],
             "ctl": [("dot", "Dot", 400, 4, 24, 0, " px"), ("angle", "Angle", 500, 0, 90, 0, "°")],
             "code": "param float dot = 12 [4, 24];\nparam float angle = 45 [0, 90];\n// one dot per cell of a turned grid\nq = rotate(P, angle);\ncell = floor(q / dot);\nL = luma(sample(centre(cell)));\nr = sqrt(L) * dot * 0.62;\nout = d < r ? ink : ground;",
             "note": "The same idea as the Halftone and Ordered Dither presets in the library."},
     "reel": {"title": "Made in Infinite", "sub": "Pieces from the art gallery and starting points from the library.", "href": "artworks.html",
              "items": [("assets/museum/06.jpg", 720, 960, "Neon mushroom"), ("assets/library/previews/iridescent-orb.jpg", 900, 600, "Iridescent orb, library"), ("assets/museum/10.jpg", 720, 960, "Rainbow spiral"),
                        ("assets/library/previews/liquid-glass.jpg", 900, 600, "Liquid glass, library"), ("assets/museum/08.jpg", 720, 960, "Halftone face"), ("assets/library/previews/ordered-dither.jpg", 900, 600, "Ordered dither, library"), ("assets/museum/02.jpg", 720, 960, "Mirrored portrait")]},
     "start": [("first-image", "First image"), ("layered-blends", "Layered blends"), ("glitch-feedback", "Glitch feedback"), ("iridescent-orb", "Iridescent orb"), ("trail-glitch", "Trail glitch")],
     "extra": ("artworks.html", "See the art gallery")},
    {"slug": "musicians", "name": "musicians", "hand": "play",
     "title": "Infinite for musicians: synths, effects and plugins",
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
     "title": "Infinite for creative coders: Field, sketches and shaders",
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
     "title": "Infinite for developers: CLI, Python SDK and MCP",
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


COPY_ICON = ('<svg class="ic ic-copy" aria-hidden="true" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="7" y="7" width="9" height="9" rx="2"/><path d="M13 7V5.5A1.5 1.5 0 0 0 11.5 4h-6A1.5 1.5 0 0 0 4 5.5v6A1.5 1.5 0 0 0 5.5 13H7"/></svg>'
             '<svg class="ic ic-done" aria-hidden="true" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4.5 10.5 8.5 14.5 15.5 6"/></svg>')
COPY_BTN = '<button class="copy" type="button" aria-label="Copy"%s>' + COPY_ICON + '</button>'   # the one standard copy control: an icon, never a text button


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
    if k == "print":
        tiles = "".join(f'<button class="pr-tile" type="button" role="radio" aria-checked="{"true" if i == 0 else "false"}" data-name="{e(n)}"><span>{e(n)}</span></button>' for i, n in enumerate(L["pics"]))
        return f'''<div class="lab ax-print" data-lab="print">
          <div class="lab-view"><canvas aria-label="A picture printed as a halftone screen of dots" role="img"></canvas>
            <p class="pr-cap" aria-live="polite">{e(L["pics"][0])}</p></div>
          <div class="lab-side">{side}<div class="pr-tiles" role="radiogroup" aria-label="Picture">{tiles}</div>{ctls}{code}{note}</div></div>'''
    tabs = "".join(f'<button role="tab" type="button" id="t{i}" aria-controls="p{i}" aria-selected="{"true" if i == 0 else "false"}">{e(n)}</button>' for i, (n, _, _) in enumerate(L["tabs"]))
    panels = "".join(f'<div role="tabpanel" id="p{i}" aria-labelledby="t{i}" class="term-panel"{"" if i == 0 else " hidden"}><div style="position:relative"><pre class="aud-code"><code>{e(c)}</code></pre>{COPY_BTN % ""}</div><p>{h}</p></div>' for i, (_, c, h) in enumerate(L["tabs"]))
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


VIDEO_ID = "PoqHY7yMIyQ"
DEV_FILM = (VIDEO_ID, "your coding agent can now work with this DAW.")
def video_embed():
    return f'<div class="yt-embed ax-video"><iframe src="https://www.youtube-nocookie.com/embed/{VIDEO_ID}?rel=0" title="Infinite video" loading="lazy" allow="accelerometer; encrypted-media; gyroscope; picture-in-picture; fullscreen" allowfullscreen referrerpolicy="strict-origin-when-cross-origin"></iframe></div>'

MUSIC_FILM = ("n2ASWDUIW-4", "sound design just better.")   # the musicians film (youtube.com/@n1m21n)
def film_poster(vid, title):
    """Click-to-load: a thumbnail and a play button; site.js swaps in the player, so YouTube loads nothing until asked."""
    return (f'<div class="yt-embed ax-video" data-id="{vid}" data-title="{e(title)}"><button class="yt-poster" type="button" aria-label="Play the film: {e(title)}">'
            f'<img src="https://i.ytimg.com/vi/{vid}/maxresdefault.jpg" alt="{e(title)}, a film made in Infinite" width="1280" height="720" loading="lazy" decoding="async">'
            f'<span class="yt-play"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 5.5v13l11-6.5z" fill="currentColor"/></svg></span>'
            f'<span class="yt-label">{e(title)}</span></button></div>')

def main_html(a, rel):
    float3d = ""
    if a["hero"]["img"] == "brand_ball":
        float3d = f'          <span class="vstage-float"><img class="vstage-3d" src="assets/3d/brand_ball.webp" alt="{e(a["hero"]["alt"])}" width="700" height="700" decoding="async"></span>\n'
    facts = "".join(f"<li>{e(f)}</li>" for f in a["facts"])
    cards = "\n".join(f'          <article class="aud-card reveal" style="--i:{n}"><h2>{e(t)}</h2><p>{e(d)}</p></article>' for n, (t, d) in enumerate(a["cards"]))
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


DIGITS = ["01110100011001110101110011000101110", "00100011000010000100001000010001110", "01110100010000100010001000100011111",
          "11111000100010000010000011000101110", "00010001100101010010111110001000010", "11111100001111000001000011000101110"]   # field.js 5x7 digits


def dot_digits(n):
    """A two-digit number as a 5x7 dot matrix (the programmed field's digits; ref: the lime dot-matrix glyph sheet)."""
    out, x0 = [], 0
    for ch in f"{n:02d}":
        g = DIGITS[int(ch)]
        out += [f'<circle cx="{x0 + (k % 5) * 4 + 2}" cy="{(k // 5) * 4 + 2}" r="1.5"/>' for k in range(35) if g[k] == "1"]
        x0 += 24
    return f'<svg class="dm" viewBox="0 0 44 28" aria-hidden="true">{"".join(out)}</svg>'


SHORT = {"artists": "Artists", "musicians": "Musicians", "vjs": "VJs", "creative-coders": "Creative coders", "developers": "Developers"}   # as the nav names them


def others_html(rel):
    """The other audiences as keycaps: a dot-matrix number, the icon, the name (refs: Swapper Arcade tiles, Writtl's 3D buttons)."""
    return "".join(f'<a class="ax-key" href="{h}"><span class="ax-key-top">{icon(AUD_ICON[h[:-5]])}<b>{SHORT[h[:-5]]}</b></span></a>'
                   for i, (h, n) in enumerate([x for x in ALL if x[0] != rel]))


def reel_href(a, src):
    """Where a reel tile goes: a library thumbnail opens that patch's page; art from the gallery opens the gallery."""
    return "library/%s.html" % os.path.splitext(os.path.basename(src.split("?")[0]))[0] if src.startswith("assets/library/thumbs/") else a["reel"]["href"]


def lib_thumb(name):
    """The library's own thumbnail for a preset id or an old picture path (assets/.../<id>.ext): the render for art presets, knobs and cables for the rest. None if there is none."""
    i = os.path.splitext(os.path.basename(name))[0]
    for ext in ("webp", "svg"):
        if os.path.exists(os.path.join(W, "assets", "library", "thumbs", i + "." + ext)): return "assets/library/thumbs/%s.%s?v=4" % (i, ext)
    return None


def start_html(start):
    """Start-with patches as a bento of their preview pictures; the first is the big tile (refs: units. and UNLOCKED bentos)."""
    import re as _re
    tiles = []
    for i, (sl, t) in enumerate(start):
        m = _re.search(r'og:image" content="https://infinitedevices\.org/([^"]+)"', open(os.path.join(W, "library", sl + ".html")).read())
        src = lib_thumb(sl) or (m.group(1) if m and os.path.exists(os.path.join(W, m.group(1))) else "")
        img = f'<img src="{src}" alt="" width="720" height="450" loading="lazy" decoding="async">' if src else ""
        tiles.append(f'<a class="ax-bento-t{" big" if i == 0 else ""}" href="library/{sl}.html">{img}<span class="ax-bento-l">{e(t)}</span></a>')
    return f'<div class="ax-bento">{"".join(tiles)}</div>'


def dv_number(html):
    """The developers docs sections get the same poster numerals as the other audience pages."""
    import re as _re
    n = len(_re.findall(r'<section class="dv-sec[^"]*"[^>]*><h2>', html)); k = [0]
    def f(m):
        k[0] += 1
        return m.group(0) + f'<span class="ax-n" aria-hidden="true">{k[0]:02d}<i>/ {n:02d}</i></span>'
    return _re.sub(r'<section class="dv-sec[^"]*"[^>]*><h2>', f, html)


def number_heads(html):
    """Index every section heading poster-style: 01 / 06 (ref: the Dennis Ferrer gallery poster)."""
    import re as _re
    n = len(_re.findall(r'<h2 class="ax-h">', html)); k = [0]
    def f(m):
        k[0] += 1
        return f'<h2 class="ax-h"><span class="ax-n" aria-hidden="true">{k[0]:02d}<i>/ {n:02d}</i></span>'
    return _re.sub(r'<h2 class="ax-h">', f, html)


# The Nodes deck on each audience page: (file in assets/nodes/hd/, title, one line, category). Pictures come from scripts/node_screenshot.py
# (zoomed captures of the running app), compressed to webp by tools/website/node_shots_to_webp.py.
NODE_DECK = {
    "artists": [("render-3d", "Render 3D", "Lights, materials and shadows, drawn in real time.", "3D"), ("sphere", "Sphere", "A procedural sphere to light and instance.", "3D"),
                ("torus-knot", "Torus Knot", "A knotted ring with p and q knobs.", "3D"), ("supershape", "Supershape", "One formula, hundreds of organic forms.", "3D"),
                ("metaballs", "Metaballs", "Blobs that merge as they touch.", "3D"), ("cloth", "Cloth", "A surface that drapes and moves.", "3D"),
                ("light", "Light", "A light with colour, falloff and shadow.", "3D"), ("material", "Material", "Colour, metal and roughness for a surface.", "3D"),
                ("text-3d", "Text 3D", "Type with depth, ready to turn.", "3D"), ("projection", "Projection", "Wrap a picture onto geometry.", "3D"),
                ("displacement", "Displacement", "Push a mesh with a picture.", "3D"), ("layer-stack", "Layer Stack", "Stack many pictures in one node.", "Compositing"),
                ("blend", "Blend", "Stack one picture on another with a blend mode.", "Compositing"), ("color-ramp", "Color Ramp", "Map brightness to a gradient you design.", "Compositing"),
                ("curves", "Curves", "Shape tone with a drawn curve.", "Compositing"), ("trails", "Trails", "A fading trail: decay, zoom, rotate, hue.", "Effects"),
                ("glitch", "Glitch", "Tear the picture into slices.", "Effects"), ("bloom", "Bloom", "Let bright parts glow.", "Effects"),
                ("halftone", "Halftone", "Turn a picture into a screen of dots.", "Effects"), ("kaleidoscope", "Kaleidoscope", "Mirror the picture into segments.", "Effects")],
    "musicians": [("midi-notes", "MIDI Notes", "Notes go into the graph from here.", "Notes"), ("arpeggiator", "Arpeggiator", "Turn a chord into a pattern.", "Notes"),
                  ("drum-sequencer", "Drum Sequencer", "Steps, swing and velocity.", "Notes"), ("analog", "Analog", "Oscillators, a filter and an envelope in one voice.", "Synths"),
                  ("fm", "FM", "Four operators.", "Synths"), ("wavetable", "Wavetable", "Scan through a table of shapes.", "Synths"),
                  ("string", "String", "A modelled plucked string.", "Synths"), ("partials", "Partials", "Resynthesis from a handful of partials.", "Synths"),
                  ("metallic", "Metallic", "A resonator for bells and plates.", "Synths"), ("equation-synth", "Equation Synth", "A voice from a formula.", "Synths"),
                  ("sampler", "Sampler", "Play recordings across the keys.", "Synths"), ("granular", "Granular", "Clouds of tiny grains.", "Synths"),
                  ("paulstretch", "PaulStretch", "Stretch a sound into a drone.", "Audio FX"), ("delay", "Delay", "Tape delay with wow and flutter.", "Audio FX"),
                  ("reverb", "Reverb", "A room you can size and colour.", "Audio FX"), ("convolution-reverb", "Convolution Reverb", "Design a space or load one.", "Audio FX"),
                  ("chorus", "Chorus", "Thicken a sound with moving copies.", "Audio FX"), ("phaser", "Phaser", "Sweeping notches in the spectrum.", "Audio FX"),
                  ("eq", "EQ", "Cut and lift bands.", "Audio FX"), ("saturator", "Saturator", "Three stages of warmth and grit.", "Audio FX"),
                  ("dynamics", "Dynamics", "Clean, bus, opto and FET styles.", "Audio FX"), ("lfo", "LFO", "Moves any knob in the patch.", "Modulators"),
                  ("envelope", "Envelope", "A shape over time for any knob.", "Modulators")],
    "vjs": [("video", "Video", "Hardware-accelerated video in.", "Source"), ("camera", "Camera", "A live camera feed.", "Source"),
            ("audio-analyze", "Audio Analyze", "Turns sound into numbers a knob can follow.", "Audio"), ("audio-to-cv", "Audio to CV", "Make a level drive any knob.", "Audio"),
            ("audio-displacement", "Audio Displacement", "Let the music push the picture.", "Audio"), ("audio-ribbon", "Audio Ribbon", "A ribbon drawn by the waveform.", "Audio"),
            ("audio-texture", "Audio Texture", "Sound as a texture.", "Audio"), ("audio-color-ramp", "Audio Color Ramp", "Colour that follows the level.", "Audio"),
            ("trails", "Trails", "A fading trail on every beat.", "Effects"), ("feedback", "Feedback", "Feed the output back into itself.", "Effects"),
            ("kaleidoscope", "Kaleidoscope", "Mirror the picture into segments.", "Effects"), ("glitch", "Glitch", "Tear the picture into slices.", "Effects"),
            ("bloom", "Bloom", "Let bright parts glow.", "Effects"), ("particle-system", "Particle System", "Thousands of points with forces.", "3D"),
            ("syphon-out", "Syphon Out", "Send the picture to another app on macOS.", "Output"), ("ndi-out", "NDI Out", "Send it over the network.", "Output")],
    "creative-coders": [("sketch", "Sketch", "Draw from JavaScript.", "Code"), ("sketch-3d", "Sketch 3D", "Build meshes from JavaScript.", "Code"),
                        ("fieldpixel", "Field Pixel", "A shader in a few lines.", "Field"), ("field-effect", "Field Effect", "An audio or picture effect in Field.", "Field"),
                        ("field-synth", "Field Synth", "A per-sample audio kernel.", "Field"), ("field-notes", "Field Notes", "Generate notes with code.", "Field"),
                        ("field-primitive", "Field Primitive", "A mesh from a formula.", "Field"), ("field-modifier", "Field Modifier", "Deform and shade a mesh.", "Field"),
                        ("field-graph", "Field Graph", "Plot a function as it runs.", "Field"), ("formula", "Formula", "One expression, one output.", "Field"),
                        ("render-3d", "Render 3D", "Lights, materials and shadows.", "3D"), ("reaction-diffusion", "Reaction Diffusion", "Chemistry that draws spots and stripes.", "Simulation"),
                        ("klein-bottle", "Klein Bottle", "A surface with no inside.", "Simulation"), ("wave-terrain", "Wave Terrain", "Read a landscape as a sound.", "Simulation"),
                        ("particle-system", "Particle System", "Thousands of points with forces.", "Simulation")],
}


def node_deck(a):
    """The Nodes section: a fan deck of zoomed node pictures. Swipe, drag, wheel, arrow keys or tap; it loops (website/nodedeck.js, engine deck.js)."""
    from PIL import Image
    cards = []
    for i, (f, t, d, c) in enumerate(NODE_DECK[a["slug"]]):
        im = Image.open(os.path.join(W, "assets", "nodes", "hd", f + ".webp"))
        src = f"assets/nodes/hd/{f}.webp"
        img = f'src="{src}"' if i < 5 else f'data-src="{src}"'
        cards.append(f'<figure class="nd-card" data-title="{e(t)}" data-desc="{e(d)}" style="--cat:{CAT.get(c, "#818CF8")}"><span class="nd-cat">{e(c)}</span><img {img} alt="{e(t)} node: {e(d)}" width="{im.size[0]}" height="{im.size[1]}" decoding="async" draggable="false"></figure>')
    return (f'<div class="nd" data-nodedeck><div class="nd-grid" tabindex="0" aria-roledescription="carousel" aria-label="Nodes. Swipe or use the arrow keys.">{"".join(cards)}</div>'
            f'<div class="nd-hud"><button type="button" class="nd-btn" data-nd="-1" aria-label="Previous node"><svg viewBox="0 0 20 20" aria-hidden="true"><path d="M12.5 4.5 7 10l5.5 5.5"/></svg></button>'
            f'<div class="nd-info" aria-live="polite"><b class="nd-t"></b><span class="nd-d"></span></div>'
            f'<button type="button" class="nd-btn" data-nd="1" aria-label="Next node"><svg viewBox="0 0 20 20" aria-hidden="true"><path d="M7.5 4.5 13 10l-5.5 5.5"/></svg></button></div></div>')


def artists_main(a, rel, hero="blob"):
    """Immersive artists page: a live iridescent blob you steer, three glass bubbles that morph it, a swipe strip of nodes, knobs, a reel."""
    L = a["lab"]
    if hero == "cables":
        bub = [("synth", "family-synths", "Synths", a["cards"][0][0]), ("fx", "family-audio-effects", "Effects", a["cards"][1][0]), ("plug", "mod-matrix", "Plugins", a["cards"][2][0])]
        tag, caps, ghost = "Pull a cable.", "|".join(c[0] for c in a["cards"]), ("Guide", "library")
    elif hero == "math":
        bub = [("orbit", "loop", "Orbit", a["cards"][0][0]), ("bloom", "star", "Bloom", a["cards"][1][0]), ("cells", "grid-dots", "Cells", a["cards"][2][0])]
        tag, caps, ghost = "Change two numbers.", "Lorenz attractor|Phyllotaxis bloom|Rule 110", ("Library", "library")
    elif hero == "shader":
        bub = [("out", "viewport", "Output", a["cards"][0][0]), ("beat", "metronome", "Beat", a["cards"][1][0]), ("safe", "power", "Safe", a["cards"][2][0])]
        tag, caps, ghost = "Play the room.", "|".join(c[0] for c in a["cards"]), ("Guide", "library")
    else:
        bub = [("layers", "family-compositing", "Layer", a["cards"][0][0]), ("solid", "cube", "3D", a["cards"][1][0]), ("formula", "edit", "Formula", a["cards"][2][0])]
        tag, caps, ghost = "Images that move.", "Layers|3D with real materials|A picture from a formula", ("Gallery", "viewport")
    bubbles = "".join(f'<button type="button" class="ax-bub{" on" if i == 0 else ""}" role="tab" aria-selected="{"true" if i == 0 else "false"}" data-k="{i}" aria-label="{e(c)}: {e(a["cards"][i][1])}">{icon(ic)}<span>{e(t)}</span></button>' for i, (_, ic, t, c) in enumerate(bub))
    watch = f'      <section class="aud-section reveal">\n        <h2 class="ax-h">Watch</h2>\n        {film_poster(*MUSIC_FILM)}\n      </section>\n' if hero == "cables" else ""
    items = [((lib_thumb(src), 720, 450, al) if "assets/museum" not in src and lib_thumb(src) else (src, w, h, al)) for src, w, h, al in a["reel"]["items"]]
    figs = "".join(f'<a class="ax-shot" style="--ar:{w} / {h}" href="{reel_href(a, src)}"><img src="{src}" alt="{e(al)}" width="{w}" height="{h}" loading="lazy" decoding="async"><span class="ax-ttl">{e(al.replace(", library", ""))}</span></a>' for src, w, h, al in items)
    start = "".join(f'<a href="library/{sl}.html">{icon("play")}{e(t)}</a>' for sl, t in a["start"])
    others = "".join(f'<a href="{h}">{icon(AUD_ICON[h[:-5]])}{e(n)}</a>' for h, n in ALL if h != rel)
    lab = lab_html(a)
    lab = lab.replace('<h3>' + e(L["title"]) + '</h3>', '')
    import re
    lab = re.sub(r'(<pre class="aud-code">.*?</pre>)', lambda m: '<div class="ax-code">' + m.group(1) + COPY_BTN % "" + '</div>', lab, flags=re.S)
    if hero == "cables":
        knob = lambda key, label, val: f'<div class="mx-knob" data-k="{key}" data-v="{val}"><div class="mx-dial" role="slider" tabindex="0" aria-label="{label}" aria-valuemin="0" aria-valuemax="100" aria-valuenow="{round(val * 100)}"><svg viewBox="0 0 64 64" aria-hidden="true"><path class="mx-track" d="M14.5 49.5A25 25 0 1 1 49.5 49.5"/><path class="mx-arc" d="M14.5 49.5A25 25 0 1 1 49.5 49.5"/></svg><span class="mx-cap"></span><span class="mx-shine"></span></div><span>{label}</span><output>{round(val * 100)}</output></div>'
        lab = (f'<div class="mx-rack"><div class="mx-top"><button type="button" class="mx-play" aria-pressed="false" aria-label="Play a chord">{icon("play")}<span>Play</span></button><canvas class="mx-scope" width="600" height="120" aria-hidden="true"></canvas></div>'
               f'<div class="mx-knobs">{knob("cut", "Cutoff", 0.55)}{knob("res", "Resonance", 0.25)}{knob("rev", "Reverb", 0.4)}{knob("lfo", "Wobble", 0.2)}</div></div>')
    return (f'''
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
            <div class="ax-cta">
              <a href="index.html#setup" class="btn btn-primary">Download</a>
              <a href="{a["extra"][0]}" class="ax-ghost" aria-label="{e(a["extra"][1])}">{icon(ghost[1])}<span>{ghost[0]}</span></a>
            </div>
          </div>
        </div>
      </div>
    </section>
    <div class="container">
      <section class="aud-section reveal">
        <h2 class="ax-h">Nodes</h2>
        {node_deck(a)}
      </section>
      <section class="aud-section reveal ax-lab">
        <h2 class="ax-h">{"Play" if hero == "cables" else "Perform" if hero == "shader" else "Tweak" if hero == "math" else "Halftone"}</h2>
        {lab}
      </section>
{watch}      <section class="aud-section reveal">
        <h2 class="ax-h">Made in Infinite</h2>
        <div class="ax-strip"><div class="ax-reel">{figs}</div></div>
      </section>
      <section class="aud-section reveal">
        <h2 class="ax-h">Start with</h2>
        {start_html(a["start"])}
      </section>
      <section class="aud-section reveal">
        <h2 class="ax-h">For everyone else</h2>
        <div class="ax-keys">{others_html(rel)}</div>
      </section>
    </div>
''')


def dev_main(a, rel):
    """Developers: a docs layout. Short hero with the one-line install, a sticky contents column, and every section a solid card."""
    import re
    raw = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), a["body"]), encoding="utf-8").read()
    feed = re.search(r'<p class="blog-feed">.*?</p>', raw, re.S)
    chunks = [c for c in raw.split('<div class="dev-wide">')[1:]]
    secs = []
    for c in chunks:
        c = c.rsplit("</div>", 1)[0] if c.count("<table") == 0 or c.count("</div>") > 1 else c
        m = re.search(r"<h2>(.*?)</h2>", c, re.S); title = re.sub("<[^>]+>", "", m.group(1)); body = c.replace(m.group(0), "", 1)
        body = re.sub(r"</div>\s*$", "", body.strip()) if body.strip().endswith("</div>") and body.count("</div>") > body.count("<div") else body
        secs.append((re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-"), title, body))
    toc_items = [("start", "Quick start", "play")] + [(i, t, ic) for (i, t, _), ic in zip(secs, ("power", "edit", "family-utility", "settings", "browser"))]
    toc = "".join(f'<a href="#{i}">{icon(ic)}<span>{e(t)}</span></a>' for i, t, ic in toc_items)
    facts = "".join(f"<li>{e(f)}</li>" for f in a["facts"])
    cards = "".join(f'<article class="dv-card"><h2>{e(t)}</h2><p>{e(d)}</p></article>' for n, (t, d) in enumerate(a["cards"]))
    sections = "".join(f'<section class="dv-sec" id="{i}"><h2>{e(t)}</h2>{b}</section>' for i, t, b in secs)
    return (f'''
  <main class="dv">
    <section class="dv-hero">
      <div class="container">
        <span class="blog-hand hand">{e(a["hand"])}</span>
        <h1 class="slab">{e(a["h1"])}</h1>
        <p class="lead">{e(a["lead"])}</p>
        <div class="dv-cmd"><code>npx -y infinite-engine init</code>{COPY_BTN % ' data-copy="npx -y infinite-engine init"'}</div>
        <ul class="facts">{facts}</ul>
        <div class="aud-cta"><a href="index.html#setup" class="btn btn-primary">Download Infinite</a><a href="{a["extra"][0]}" class="btn btn-ghost">{e(a["extra"][1])}</a></div>
      </div>
    </section>
    <div class="container dv-grid">
      <nav class="dv-toc" aria-label="On this page">{toc}</nav>
      <div class="dv-main">
        <div class="dv-cards">{cards}</div>
        <section class="dv-sec dv-lab" id="start"><h2>Quick start</h2>{lab_html(a)}</section>
        <section class="dv-sec" id="watch"><h2>Watch</h2>{film_poster(*DEV_FILM)}</section>
        {sections}
        {feed.group(0) if feed else ""}
        <section class="dv-sec"><h2>For everyone else</h2><div class="ax-keys">{others_html(rel)}</div></section>
      </div>
    </div>
  </main>
''')


def page(a):
    rel = a["slug"] + ".html"
    ld = {"@context": "https://schema.org", "@type": "WebPage", "name": a["title"], "url": BASE + rel, "description": a["desc"],
          "isPartOf": {"@type": "WebSite", "name": "Infinite", "url": BASE}, "about": {"@type": "SoftwareApplication", "name": "Infinite", "applicationCategory": "MultimediaApplication", "operatingSystem": "macOS, Windows, Linux", "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"}}}
    if a["slug"] == "artists":
        return shell(rel, a["title"], a["desc"], [ld], artists_main(a, rel), extra_head='\n  <link rel="stylesheet" href="artists.css?v=' + chrome._hash("artists.css") + '">', extra_js='\n  <script src="deck.js?v=2" defer></script>\n  <script src="nodedeck.js?v=1" defer></script>\n  <script src="artists.js?v=11" defer></script>')
    if a["slug"] == "creative-coders":
        return shell(rel, a["title"], a["desc"], [ld], artists_main(a, rel, "math"), extra_head='\n  <link rel="stylesheet" href="artists.css?v=' + chrome._hash("artists.css") + '">', extra_js='\n  <script src="deck.js?v=2" defer></script>\n  <script src="nodedeck.js?v=1" defer></script>\n  <script src="artists.js?v=11" defer></script>\n  <script src="ccode.js?v=4" defer></script>')
    if a["slug"] == "vjs":
        return shell(rel, a["title"], a["desc"], [ld], artists_main(a, rel, "shader"), extra_head='\n  <link rel="stylesheet" href="artists.css?v=' + chrome._hash("artists.css") + '">', extra_js='\n  <script src="deck.js?v=2" defer></script>\n  <script src="nodedeck.js?v=1" defer></script>\n  <script src="artists.js?v=11" defer></script>\n  <script src="vjs.js?v=5" defer></script>')
    if a["slug"] == "musicians":
        return shell(rel, a["title"], a["desc"], [ld], artists_main(a, rel, "cables"), extra_head='\n  <link rel="stylesheet" href="artists.css?v=' + chrome._hash("artists.css") + '">', extra_js='\n  <script src="deck.js?v=2" defer></script>\n  <script src="nodedeck.js?v=1" defer></script>\n  <script src="artists.js?v=11" defer></script>\n  <script type="module" src="musicians.js?v=4"></script>\n  <script src="mxrack.js?v=3" defer></script>')
    if a["slug"] == "developers":
        return shell(rel, a["title"], a["desc"], [ld], dev_main(a, rel), extra_head='\n  <link rel="stylesheet" href="dev.css?v=' + chrome._hash("dev.css") + '">', extra_js='\n  <script src="dev.js?v=3" defer></script>')
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
  <meta name="theme-color" content="#151930">
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
  <link rel="stylesheet" href="brand.css?v=42bdb72d">
  <link rel="stylesheet" href="style.css?v={chrome.CSS_VERSION}">
  <link rel="stylesheet" href="glass.css?v=c25dae44">
  <link rel="stylesheet" href="blog.css?v=d7c115a7">
  <link rel="stylesheet" href="audience.css?v=dd614df4">
  {ldtags}{extra_head}
  <script>document.documentElement.classList.add('js-reveal')</script>
</head>
<body class="blog-page">
  {chrome.header_html(rel)}

{main}
  </main>

  {chrome.footer_html(rel)}
  <script src="site.js?v={chrome.JS_VERSION}" defer></script>
  <script src="audience.js?v=11" defer></script>{extra_js}
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
