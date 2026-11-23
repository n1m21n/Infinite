#!/usr/bin/env python3
"""BCI results deck: 14 slides on one brand template, rendered to PNG (slide 7 to an animated GIF).

One template for every slide (docs/brand/BRAND.md):
  type   Geist only, local OFL files in art/brand/fonts. Five steps, nothing else:
         title 40/800, heading 22/600, takeaway 18/600, body 15/500, label 12 Geist Mono caps.
  colour paper ground, mist cards, white inner cards; text ink / ink-2 / signal; midnight tags on photos.
  mark   art/brand/logo/svg/mark.svg, gradient, 64 px wide, top right, centred on the title line.

usage: build_deck.py OUT_DIR [LIGHT_FRAMES_DIR]
  writes OUT_DIR/sNN.html + sNN.png (s07.gif from LIGHT_FRAMES_DIR, made by light_frames.py)
"""
import base64
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
ASSETS = os.path.join(ROOT, "docs", "plans", "bci", "deck")
FONTS = os.path.join(ROOT, "art", "brand", "fonts")
MARK = os.path.join(ROOT, "art", "brand", "logo", "svg", "mark.svg")
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"


def uri(path, mime=None):
    mime = mime or {"png": "image/png", "jpg": "image/jpeg", "ttf": "font/ttf", "svg": "image/svg+xml"}[path.rsplit(".", 1)[1]]
    with open(path, "rb") as f:
        return f"data:{mime};base64," + base64.b64encode(f.read()).decode()


def asset(name):
    return uri(os.path.join(ASSETS, name))


CSS = """
@font-face{font-family:G;font-weight:500;src:url(%(m)s)}
@font-face{font-family:G;font-weight:600;src:url(%(s)s)}
@font-face{font-family:G;font-weight:800;src:url(%(x)s)}
@font-face{font-family:GM;font-weight:500;src:url(%(mono)s)}
:root{--paper:#FBFAF6;--s3:#F5F5F8;--line:#DAD8E6;--white:#FFFFFF;--ink:#12152A;--ink2:#404660;--signal:#4A54D6;
  --e1:0 1px 1px rgba(26,31,54,.04),0 4px 10px rgba(26,31,54,.05),0 14px 32px rgba(26,31,54,.06);
  --glass:rgba(255,255,255,.78)}
*{box-sizing:border-box;margin:0;padding:0}
body{width:1280px;height:720px;background:var(--paper);font-family:G;font-weight:500;font-size:15px;line-height:1.45;
  color:var(--ink2);position:relative;overflow:hidden}
h1{position:absolute;left:64px;top:44px;font-size:40px;line-height:48px;font-weight:800;color:var(--ink);letter-spacing:-.5px}
.mark{position:absolute;right:64px;top:54px;width:64px}
h2{font-size:22px;line-height:1.25;font-weight:600;color:var(--ink)}
b{font-weight:600;color:var(--ink)}
.lab{font-size:15px;line-height:1.4;font-weight:600;color:var(--ink)}
.lab[style*="ink2"]{font-weight:500}
.card{position:absolute;background:var(--white);border-radius:24px;padding:24px;box-shadow:var(--e1)}
.in{background:var(--s3);border-radius:16px;padding:14px 16px}
.take{position:absolute;left:64px;width:1152px;top:640px;font-size:18px;line-height:1.35;font-weight:600;color:var(--signal)}
.tag{display:inline-block;font-size:15px;font-weight:600;color:var(--ink);
  background:var(--glass);-webkit-backdrop-filter:blur(20px) saturate(1.6);backdrop-filter:blur(20px) saturate(1.6);
  border-radius:999px;padding:5px 12px;box-shadow:inset 0 1px 0 rgba(255,255,255,.95),0 4px 12px rgba(26,31,54,.08)}
.tag.sig{background:var(--signal);color:var(--white)}
.row{display:flex;gap:24px}
.chip{display:inline-block;background:var(--s3);color:var(--ink);border-radius:999px;padding:6px 14px;margin:0 8px 8px 0}
.chip.on{background:var(--signal);color:var(--white)}
.chip.off{color:var(--ink2);opacity:.6}
img{display:block}
.ph{object-fit:cover;border-radius:16px}
.arrow{color:var(--signal);font-size:22px;font-weight:600}
"""


def page(n, title, body, take=None):
    fonts = {"m": uri(os.path.join(FONTS, "Geist-Medium.ttf")), "s": uri(os.path.join(FONTS, "Geist-SemiBold.ttf")),
             "x": uri(os.path.join(FONTS, "Geist-ExtraBold.ttf")), "mono": uri(os.path.join(FONTS, "GeistMono-Medium.ttf"))}
    tk = f'<div class="take">{take}</div>' if take else ""
    return (f'<!doctype html><html><head><meta charset="utf-8"><title>Slide {n}</title><style>{CSS % fonts}</style></head>'
            f'<body><h1>{title}</h1><img class="mark" src="{uri(MARK)}" alt="Infinite">{body}{tk}</body></html>')


# ------------------------------------------------------------------------------------------------ slides
def s01():
    cols = [("Mind", ["Neuroscience", "Psychology", "Consciousness", "Perception"]),
            ("Making", ["3D modelling and graphics", "Interactive art", "Design", "Creative technology"]),
            ("Sound", ["Electronic music", "Sound synthesis", "Algorithmic and data music", "Interactive music"])]
    body = ""
    for i, (h, items) in enumerate(cols):
        lis = "".join(f"<div>{t}</div>" for t in items)
        body += (f'<div class="card" style="left:{64 + i * 392}px;top:128px;width:368px;height:232px">'
                 f'<div class="lab">{h}</div><div style="margin-top:12px;line-height:1.7">{lis}</div></div>')
    # Three S-curves from each card's centre into one point above the Infinite card; the right curve is the
    # exact mirror of the left (x -> 1152 - x), the middle one is straight, one shared arrowhead.
    sig = "#4A54D6"
    paths = ["M184 6 C184 30 576 14 576 38", "M576 6 L576 38", "M968 6 C968 30 576 14 576 38"]
    body += ('<svg style="position:absolute;left:64px;top:364px" width="1152" height="48" fill="none">'
             + "".join(f'<path d="{d}" stroke="{sig}" stroke-width="2" stroke-linecap="round"/>' for d in paths)
             + "".join(f'<circle cx="{x}" cy="6" r="3.5" fill="{sig}"/>' for x in (184, 576, 968))
             + f'<path d="M569.5 37 L576 46 L582.5 37 Z" fill="{sig}"/>'
             + '</svg>')
    body += ('<div class="card" style="left:64px;top:416px;width:1152px;height:120px;text-align:center;padding-top:28px">'
             '<h2 style="font-size:40px;line-height:44px;font-weight:800">Infinite</h2>'
             '<div style="margin-top:8px">One canvas where all three can be played</div></div>')
    links = [("Infinite", "infinitedevices.org"), ("Code", "github.com/n1m21n"),
             ("Music", "n1m21n.bandcamp.com"), ("Instagram", "@n1m21n")]
    body += '<div class="row" style="position:absolute;left:64px;top:584px;width:1152px">'
    body += "".join(f'<div style="flex:1"><div class="lab">{a}</div><b>{b}</b></div>' for a, b in links) + "</div>"
    return "Naman Soni", body, None


def s02():
    iw, ih = 700, 387
    k = iw / 1988
    tags = [("Pictures", 24, 322), ("Cables", 440, 138), ("Sound", 1150, 198), ("Output", 1640, 22)]
    t = "".join(f'<span class="tag sig" style="position:absolute;left:{24 + x * k:.0f}px;top:{24 + y * k:.0f}px">{n}</span>'
                for n, x, y in tags)
    body = (f'<div class="card" style="left:64px;top:128px;width:{iw + 48}px;height:{ih + 48}px">'
            f'<img class="ph" src="{asset("infinite_patch.png")}" style="width:{iw}px;height:{ih}px">{t}</div>')
    rows = [("What it is", "Nodes joined by cables, creating sound, images and 3D geometry."),
            ("Who it is for", "Musicians, VJs, creative coders, visual artists and experimenters."),
            ("Why it connects", "Sound, image and geometry are not separate. Like nature, they are one connected whole, "
                                "so they share one canvas, in real time."),
            ("What is next", "Body signals as instruments: tiny muscle movements, heart rate from Apple Watch sensors, "
                             "and brain signals from EEG.")]
    body += '<div style="position:absolute;left:844px;top:128px;width:372px">'
    body += "".join(f'<div style="margin-bottom:18px"><div class="lab">{a}</div><div>{b}</div></div>' for a, b in rows)
    body += "</div>"
    return "Infinite", body, None


def s03():
    def rows(r):
        return "".join(f'<div style="display:flex;gap:12px;margin-top:10px"><div class="lab" style="width:56px;padding-top:2px">{a}</div>'
                       f'<div style="flex:1">{b}</div></div>' for a, b in r)
    body = (f'<div class="card" style="left:64px;top:128px;width:564px;height:480px">'
            f'<h2>Alvin Lucier, 1965</h2>'
            f'<img class="ph" src="{asset("s3_lucier.jpg")}" style="width:516px;height:250px;margin-top:14px">'
            + rows([("What", "Brainwaves played a gong, drums and cymbals."),
                    ("How", "Calm, eyes closed: the signal shook the speakers."),
                    ("Gear", "Scalp electrodes, amplifier, speakers, percussion.")]) + "</div>")
    body += (f'<div class="card" style="left:652px;top:128px;width:564px;height:480px">'
             f'<h2>Benn Jordan, 2023</h2><div class="row" style="gap:16px;margin-top:14px">'
             f'<img class="ph" src="{asset("s3_drums.jpg")}" style="width:250px;height:250px">'
             f'<img class="ph" src="{asset("s3_patch.jpg")}" style="width:250px;height:250px"></div>'
             + rows([("What", "A musician wearing a consumer EEG headband."),
                     ("How", "Focus and stress readings control synth sounds."),
                     ("Gear", "EEG headband, laptop, software synths, drum kit.")]) + "</div>")
    return "Inspiration", body, None


def s04():
    wave = ('<svg width="320" height="150" viewBox="0 0 320 150"><line x1="10" y1="75" x2="310" y2="75" stroke="#404660" stroke-opacity=".3"/>'
            '<path d="' + "M10 75 " + " ".join(f"L{10 + i * 3} {75 - 52 * __import__('math').sin(i * 3 / 300 * 2 * 3.14159):.1f}" for i in range(101))
            + '" stroke="#4A54D6" stroke-width="4" fill="none"/></svg>')
    import random
    rng = random.Random(3)
    pal = ["#F57F66", "#D99A9C", "#C7AFC0", "#A9CDF1", "#4A54D6", "#12152A", "#FFFFFF"]
    px = "".join(f'<rect x="{40 + c * 30}" y="{4 + r * 30}" width="27" height="27" rx="3" fill="{rng.choice(pal)}"/>'
                 for r in range(5) for c in range(8))
    pix = f'<svg width="320" height="150" viewBox="0 0 320 150">{px}</svg>'
    pts = []
    for i in range(14):
        for j in range(5):
            import math
            a = i / 14 * 2 * math.pi
            x = 160 + 100 * math.cos(a) * (0.4 + 0.15 * j)
            y = 75 + 26 * math.sin(a) * (0.4 + 0.15 * j) - (j - 2) * 14
            pts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="#4A54D6"/>')
    p3 = f'<svg width="320" height="150" viewBox="0 0 320 150">{"".join(pts)}</svg>'
    cards = [("Music", "Waves", asset("s4_sound.png"), "A wave that repeats. How many times a second it repeats (Hz) is the pitch you hear.",
              "440 Hz, one cycle"),
             ("Images", "Pixels", asset("s4_image.png"), "A grid of tiny dots. Each dot is a mix of red, green and blue.", "8 x 5 dots"),
             ("3D", "Points", asset("s4_geometry.png"), "Shapes made of points in space. Each point is three numbers: x, y and z.", "70 points")]
    body = ""
    for i, (lab, h, art, txt, note) in enumerate(cards):
        body += (f'<div class="card" style="left:{64 + i * 392}px;top:128px;width:368px;height:472px">'
                 f'<div class="lab" style="color:var(--ink2)">{lab}</div><h2 style="margin-top:4px">{h}</h2>'
                 f'<img class="ph" src="{art}" style="width:320px;height:250px;margin-top:16px">'
                 f'<div style="margin-top:16px">{txt}</div></div>')
    return ("Everything is numbers", body,
            "A brain headset gives numbers too. Infinite turns them into 0 to 1 signals that drive waves, pixels and points.")


def box(x, y, w, t, on=False):
    bg = "var(--signal)" if on else "var(--s3)"
    fg = "var(--white)" if on else "var(--ink)"
    return (f'<div style="position:absolute;left:{x}px;top:{y}px;width:{w}px;height:56px;border-radius:10px;background:{bg};'
            f'color:{fg};font-weight:600;display:flex;align-items:center;justify-content:center">{t}</div>')


def s05():
    d = (f'<div class="card" style="left:64px;top:128px;width:624px;height:384px">'
         f'<img class="ph" src="{asset("s5_illustration.png")}" style="width:576px;height:312px"></div>')
    rows = [("The signal", "A number from 0 to 1 that keeps changing. The cable carries it to a knob."),
            ("Today", "An LFO, a simple repeating wave, moves the picture."),
            ("With a brain", "Swap the LFO for brain signals. The same cable now lets your brain move a picture, a sound or a 3D shape."),
            ("The brain node", "Brain Source turns a headset's raw brain voltage into 24 of these signals.")]
    d += '<div style="position:absolute;left:728px;top:128px;width:488px">'
    d += "".join(f'<div style="margin-bottom:22px"><div class="lab">{a}</div><div>{b}</div></div>' for a, b in rows) + "</div>"
    return "Swap the LFO for a brain", d, None


def s06():
    steps = [("Sensors", "4 sensors, 256 numbers a second"), ("Clean", "Remove mains hum and slow drift"),
             ("Split", "5 brain rhythms, plus blink and jaw"), ("Out", "24 signals from 0 to 1, plus picture and sound")]
    b = (f'<div class="card" style="left:64px;top:128px;width:1152px;height:324px">'
         f'<img class="ph" src="{asset("s6_inside.png")}" style="width:1104px;height:265px"></div>')
    cards = [("Why these channels", "A Muse 2 has 4 dry sensors: two on the forehead, two behind the ears. We keep only what "
                                    "they show reliably: calm (alpha), blinks, jaw and brow."),
             ("Where the mock data comes from", "A built-in simulator makes fake brain waves: calm, focus and dream states, "
                                                "with blinks and jaw clenches. Four public datasets add real recordings."),
             ("How it is tested", "Automatic checks: calm against focus, blink against jaw, then real recordings, where "
                                  "closing the eyes must raise alpha.")]
    for i, (h, t) in enumerate(cards):
        b += (f'<div style="position:absolute;left:{64 + i * 392 + 24}px;top:480px;width:340px">'
              f'<div class="lab">{h}</div><div style="margin-top:10px">{t}</div></div>')
    return "Inside the Brain node", b, None


def s07(open_img, closed_img, live=True):
    b = ('<div class="card" style="left:64px;top:128px;width:704px;height:472px">'
         f'<div class="lab">Same person, same Muse headband{", playing live from the recording" if live else ""}</div>'
         '<div class="row" style="gap:16px;margin-top:16px">')
    for img, tag, sig, cap, sub in [(open_img, "Eyes open", False, "Dim, cool, restless", "no strong rhythm above your own baseline"),
                                    (closed_img, "Eyes closed", True, "Glows gold", "the 10 Hz alpha wave rises and paints it")]:
        b += (f'<div style="width:320px"><img class="ph" src="{img}" style="width:320px;height:180px">'
              f'<div style="margin-top:12px"><span class="tag{" sig" if sig else ""}">{tag}</span></div>'
              f'<h2 style="margin-top:8px">{cap}</h2><div>{sub}</div></div>')
    b += ('</div><div class="in" style="margin-top:16px"><b>How:</b> each sensor sits where it is on the head (forehead top, '
          'ears bottom). Its <b>strength</b> sets brightness, its <b>rhythm</b> sets colour (slow red, alpha gold, fast blue), '
          'its <b>wave</b> pushes the rings, muscle buzz adds grain.</div></div>')
    # chart: 0..100% -> 0..220 px
    H, base = 200, 316
    b += '<div class="card" style="left:792px;top:128px;width:424px;height:472px"><div class="lab">Can you tell eyes closed from the picture?</div></div>'
    b += f'<div style="position:absolute;left:868px;width:324px;top:{128 + base - H // 2}px;border-top:1.5px dashed #5A6180"></div>'
    b += f'<div class="lab" style="position:absolute;left:816px;top:{128 + base - H // 2 - 9}px;color:var(--ink2)">50%</div>'
    for i, (v, lab, sub, on) in enumerate([(74, "Raw brain image", "spectrogram", False), (68, "Light alone", "no calibration", False),
                                           (89, "Light + 60 s calibration", "112 of 126", True)]):
        x = 880 + i * 108
        h = v / 100 * H
        col = "var(--signal)" if on else "var(--ink2)"
        b += (f'<div style="position:absolute;left:{x}px;top:{128 + base - h:.0f}px;width:84px;height:{h:.0f}px;background:{col};border-radius:8px 8px 0 0"></div>'
              f'<h2 style="position:absolute;left:{x}px;width:84px;text-align:center;top:{128 + base - h - 34:.0f}px;color:{col}">{v}%</h2>'
              f'<div style="position:absolute;left:{x - 12}px;width:108px;text-align:center;top:{128 + base + 10}px;line-height:1.3">'
              f'{"<b>" + lab + "</b>" if on else lab}<div class="lab" style="color:var(--ink2);margin-top:2px">{sub}</div></div>')
    b += (f'<div style="position:absolute;left:868px;width:324px;top:{128 + base}px;border-top:1px solid #DAD8E6"></div>'
          '<div class="lab" style="position:absolute;left:816px;width:376px;top:544px;color:var(--ink2)">'
          '126 pairs of 10 s clips, public Muse 2 data.<br>Judged by computer, human test next.</div>')
    return ("Brain Light: the brain paints every pixel", b,
            "A cheap headband can paint a picture you can read: with 60 s of your own calibration, eyes closed turns the light gold 89% of the time.")


def s08():
    iw, ih = 1022, 380
    b = (f'<div class="card" style="left:64px;top:116px;width:1152px;height:428px;padding:24px 65px">'
         f'<img class="ph" src="{asset("s6b_sculpt_shot.png")}" style="width:{iw}px;height:{ih}px"></div>')
    cols = [("Shift-grab a knob", "Hold Shift and grab any wired knob while you are in a mental state: calm, focused, surprised."),
            ("Each release is an anchor", "It stores the knob position and where your mind was in a 6D brain space: arousal, "
                                          "valence, entropy, coherence, brow, jaw."),
            ("Then the mind moves it", "A smooth Gaussian blend between anchors moves every bound knob as your mind travels "
                                       "between those states.")]
    b += '<div class="row" style="position:absolute;left:64px;top:568px;width:1152px">'
    b += "".join(f'<div style="flex:1"><div class="lab">0{i + 1} {a}</div><div>{t}</div></div>' for i, (a, t) in enumerate(cols))
    b += "</div>"
    return "Control layer: teach the brain your mapping", b, None


def s09():
    b = (f'<div class="card" style="left:64px;top:128px;width:400px;height:396px">'
         f'<img class="ph" src="{asset("s7_headband.png")}" style="width:352px;height:325px;margin-top:10px"></div>')
    b += ('<div style="position:absolute;left:64px;top:536px;width:400px;line-height:1.5">'
          '<b>AF7, AF8</b> forehead: blinks and eye movement<br><b>TP9, TP10</b> behind the ears: alpha and jaw</div>')
    groups = [("Contact, pulse, direct", ["presence", "heart", "image", "audio"], ""),
              ("Brain rhythms", ["delta", "theta", "alpha", "beta", "gamma"], "alpha"),
              ("Face and muscle", ["blink", "gaze L/R", "brow", "jaw L", "jaw R"], ""),
              ("Old mood-style pins, not trusted on a Muse", ["arousal", "valence", "entropy", "coherence", "density",
               "smoothness", "motion", "space", "tension", "drift", "surge", "stillness"], "off")]
    b += '<div class="card" style="left:488px;top:128px;width:728px;height:396px">'
    for h, chips, mode in groups:
        cs = "".join(f'<span class="chip{" on" if c == mode else (" off" if mode == "off" else "")}">{c}</span>' for c in chips)
        b += f'<div class="lab">{h}</div><div style="margin:6px 0 6px">{cs}</div>'
    b += "</div>"
    sets = ["device", "synthetic preset", "blink filter", "smoothing", "hold ms", "artifact blanking", "startle guard",
            "play baked take", "calibrate"]
    b += ('<div class="card" style="left:488px;top:536px;width:728px;height:136px;padding:16px 24px"><div class="lab">Node settings</div>'
          '<div style="margin-top:6px">' + "".join(f'<span class="chip">{c}</span>' for c in sets) + "</div></div>")
    return "The headband and its outputs", b, None


def s10():
    fake = [("Calm", "strong 10 Hz alpha wave"), ("Focus", "faster beta, low alpha"), ("Dream", "slow 5 Hz theta waves"),
            ("Micro-gesture", "blinks + jaw clenches")]
    b = ('<div class="card" style="left:64px;top:128px;width:368px;height:472px"><div class="lab">Fake brains for testing</div>'
         '<h2 style="margin-top:4px">Built-in simulator</h2><div style="margin-top:6px">SyntheticCortex runs with no headset.</div>')
    b += "".join(f'<div class="in" style="margin-top:12px"><b>{a}</b><div>{t}</div></div>' for a, t in fake) + "</div>"
    real = [("PhysioNet (eegmmidb)", "109 people", "Gel lab cap, 64 sensors", "Clean lab baseline for eyes-open vs eyes-closed alpha."),
            ("GIPSA", "20 people", "Gel lab cap, 16 sensors", "Repeated 10 s blocks confirming alpha behind the ears."),
            ("HEROIC", "14 to 15 people", "Real Muse 2, 4 dry sensors", "Honest consumer test: weak dry signal and cross-day drift."),
            ("EEG-EyeBlinks", "46 people", "OpenBCI, forehead", "2,300+ labelled blinks to score our blink switch.")]
    b += ('<div class="card" style="left:456px;top:128px;width:760px;height:472px"><div class="lab">Real brains for proof: 4 public datasets</div>')
    for n, p, g, w in real:
        b += (f'<div class="in" style="margin-top:12px;display:flex;gap:16px"><div style="width:200px"><b>{n}</b>'
              f'<div>{p}</div></div><div style="flex:1"><div class="lab" style="color:var(--ink2)">{g}</div><div>{w}</div></div></div>')
    b += "</div>"
    return "Where the data comes from", b, None


def s11():
    b = ""
    for i, (img, h, t) in enumerate([("ref_gel_cap.jpg", "Gel lab cap", "16 to 64 wet sensors"),
                                     ("ref_muse2.jpg", "Muse 2 headband", "4 dry metal pads")]):
        b += (f'<div class="card" style="left:{64 + i * 272}px;top:128px;width:248px;height:472px">'
              f'<img class="ph" src="{asset(img)}" style="width:200px;height:300px">'
              f'<h2 style="margin-top:14px">{h}</h2><div>{t}</div></div>')
    b += '<div class="card" style="left:608px;top:128px;width:608px;height:472px"><div class="lab">Alpha rise when eyes close (closed / open)</div>'
    W = 380 / 4.0  # px per 1.0x
    for gi, (g, vals) in enumerate([("Forehead (AF7, AF8)", [(2.25, "Gel cap", "2.25x"), (1.0, "Muse 2", "~1.0x")]),
                                    ("Behind the ears (TP9, TP10)", [(3.59, "Gel cap", "3.59x"), (1.44, "Muse 2", "1.44x")])]):
        y = 64 + gi * 168
        b += f'<div style="position:absolute;left:24px;top:{y}px"><b>{g}</b></div>'
        for k, (v, who, txt) in enumerate(vals):
            yy = y + 32 + k * 52
            col = "var(--ink2)" if who == "Gel cap" else "var(--signal)"
            b += (f'<div class="lab" style="position:absolute;left:24px;top:{yy + 12}px;color:var(--ink2)">{who}</div>'
                  f'<div style="position:absolute;left:112px;top:{yy}px;width:{v * W:.0f}px;height:40px;background:{col};border-radius:0 8px 8px 0"></div>'
                  f'<h2 style="position:absolute;left:{112 + v * W + 10:.0f}px;top:{yy + 6}px;color:{col}">{txt}</h2>')
    b += (f'<div style="position:absolute;left:{112 + W:.0f}px;top:84px;height:330px;border-left:1.5px dashed #5A6180"></div>'
          f'<div class="lab" style="position:absolute;left:{112 + W - 40:.0f}px;top:420px;color:var(--ink2)">1.0x = no change</div></div>')
    return ("Experiment 1: gel cap vs Muse", b,
            "On a dry Muse 2, the forehead barely moves when you close your eyes, and behind the ears is 1.44x (under half of a gel cap).")


def s12():
    steps = [("30 s eyes open", "Records your normal alert baseline."), ("30 s eyes closed", "Records your calm alpha baseline."),
             ("12 s blink 5 times", "Learns how tall and wide your blinks are.")]
    b = ('<div class="card" style="left:64px;top:128px;width:448px;height:472px"><div class="lab">72-second sound check</div>'
         '<h2 style="margin-top:4px">Fits you, not an average person</h2>')
    b += "".join(f'<div class="in" style="margin-top:12px"><div class="lab">0{i + 1}</div><b>{a}</b><div>{t}</div></div>'
                 for i, (a, t) in enumerate(steps))
    b += '<div style="margin-top:14px"><b>Total: 72 seconds</b>, once per session.</div></div>'
    b += '<div class="card" style="left:536px;top:128px;width:680px;height:472px"><div class="lab">Eyes-open vs eyes-closed accuracy (Muse 2)</div></div>'
    H, base = 260, 440
    b += f'<div style="position:absolute;left:640px;width:552px;top:{128 + base - H // 2}px;border-top:1.5px dashed #5A6180"></div>'
    b += f'<div class="lab" style="position:absolute;left:560px;top:{128 + base - H // 2 - 18}px;color:var(--ink2);line-height:1.2">50%<br>coin flip</div>'
    for i, (v, a, t, on) in enumerate([(68, "Fixed rule", "same day", False), (50, "Fixed rule", "later day", False),
                                       (88.5, "Calibrated", "same day", True), (85.5, "Calibrated", "next day", True)]):
        x = 656 + i * 136
        h = v / 100 * H
        col = "var(--signal)" if on else "var(--ink2)"
        b += (f'<div style="position:absolute;left:{x}px;top:{128 + base - h:.0f}px;width:96px;height:{h:.0f}px;background:{col};border-radius:8px 8px 0 0"></div>'
              f'<h2 style="position:absolute;left:{x}px;width:96px;text-align:center;top:{128 + base - h - 34:.0f}px;color:{col}">{v:g}%</h2>'
              f'<div style="position:absolute;left:{x - 12}px;width:120px;text-align:center;top:{128 + base + 8}px;line-height:1.3">'
              f'<b>{a}</b><br>{t}</div>')
    b += f'<div style="position:absolute;left:640px;width:552px;top:{128 + base}px;border-top:1px solid #DAD8E6"></div>'
    b += ('<div class="lab" style="position:absolute;left:560px;top:624px;width:640px;color:var(--ink2);text-transform:none;letter-spacing:0;display:none"></div>')
    b += ('<div style="position:absolute;left:560px;top:600px"></div>')
    return ("Experiment 2: the 72-second calibration", b,
            "Personal calibration makes a cheap dry headband actually work: 88.5% same day, 85.5% next day "
            "(public HEROIC Muse 2 data, 64 sessions).")


def s13():
    steps = [("Calibrated signal", "Alpha (0 to 1) + blink", "The 72 s profile turns raw Muse waves into a steady 0 to 1 meter."),
             ("Infinite patch", "Two cables", "Picture cable to FieldPixel Bell (ring glow, blink overlay). Sound cable to Gain + Reverb (swell, 32x brain tone)."),
             ("Live audio + visuals", "Alpha Bell", "Calm, eyes closed raises alpha: the ring glows and the sound swells. Focus dims and quiets.")]
    b = ""
    for i, (l, h, t) in enumerate(steps):
        x = 64 + i * 392
        b += (f'<div class="card" style="left:{x}px;top:128px;width:368px;height:176px"><div class="lab">0{i + 1} {l}</div>'
              f'<h2 style="margin-top:4px">{h}</h2><div style="margin-top:6px">{t}</div></div>')
        if i < 2:
            b += f'<div class="arrow" style="position:absolute;left:{x + 372}px;top:196px">&#8594;</div>'
    for i, (img, tag, sig, h, read) in enumerate([("bell_calm.png", "High alpha", True, "Calm (eyes closed)", "Luma 0.41, sound -13.6 LUFS, 369 Hz"),
                                                  ("bell_focus.png", "Low alpha", False, "Focus (eyes open)", "Luma 0.13, sound -24.2 LUFS, quiet")]):
        x = 64 + i * 584
        b += (f'<div class="card" style="left:{x}px;top:328px;width:568px;height:280px;display:flex;gap:20px">'
              f'<img class="ph" src="{asset(img)}" style="width:232px;height:232px">'
              f'<div><span class="tag{" sig" if sig else ""}">{tag}</span><h2 style="margin-top:10px">{h}</h2>'
              f'<div style="margin-top:6px">{read}</div><div class="lab" style="margin-top:14px;color:var(--ink2)">Rendered live from the Alpha Bell patch</div></div></div>')
    return ("Visual results: sound and picture", b,
            "The same calibrated 0 to 1 signal drives Infinite's shader and synth live: calm glows and swells, focus dims.")


def s14():
    qs = [("01 Research and approaches", "Papers and parallel methods",
           ["Which key papers should we read on dry EEG and adaptive calibration?",
            "What other signal-cleaning or ML methods work best with just 4 sensors?",
            "How do other teams separate real alpha from subtle eye or muscle noise?"]),
          ("02 Universities, labs and funding", "Who else is tackling this",
           ["Which university labs or researchers work on creative or consumer BCI?",
            "Are there student collaborations, pilot grants, or open research programs?",
            "What task or benchmark would convince a neuroscience reviewer?"]),
          ("03 Headsets and cheap hardware", "Getting live devices to test",
           ["Is a ~$250 Muse 2 credible enough for a 3 to 5 person pilot?",
            "Or do we need an 8-channel OpenBCI (~$600 to $1,200) over the back of the head?",
            "Are there cheaper paths, refurbished units, or university lab loans?"]),
          ("04 Signal, pilot and ethics", "Practical next steps",
           ["Is 1.12 s lag (82% accuracy) fast enough, or should we keep 6 s (88.5%)?",
            "How should we test cross-day drift and learning over multi-day practice?",
            "What ethics and consent steps are needed for a 15-minute live pilot?"])]
    b = ""
    for i, (l, h, items) in enumerate(qs):
        x, y = 64 + (i % 2) * 588, 120 + (i // 2) * 256
        li = "".join(f'<div style="margin-top:6px;padding-left:16px;text-indent:-16px">&#8226;&nbsp; {t}</div>' for t in items)
        b += (f'<div class="card" style="left:{x}px;top:{y}px;width:564px;height:236px;padding:20px 24px"><div class="lab">{l}</div>'
              f'<h2 style="margin-top:2px">{h}</h2>{li}</div>')
    return ("Help needed: research, headsets and next steps", b,
            "Next step: a 15-minute live pilot with 3 to 5 people once we align on headset, methods and ethics.")


def shoot(html_path, png_path):
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                    "--window-size=1280,720", "--virtual-time-budget=4000", f"--screenshot={png_path}",
                    "file://" + html_path], check=True, capture_output=True)


def write(out, name, n, parts):
    p = os.path.join(out, name + ".html")
    with open(p, "w") as f:
        f.write(page(n, *parts))
    shoot(p, os.path.join(out, name + ".png"))


def main(out, frames=None):
    from PIL import Image
    os.makedirs(out, exist_ok=True)
    fns = [s01, s02, s03, s04, s05, s06, None, s08, s09, s10, s11, s12, s13, s14]
    for n, fn in enumerate(fns, 1):
        if fn:
            write(out, f"s{n:02d}", n, fn())
    if not frames:
        return
    names = sorted(f for f in os.listdir(frames) if f.startswith("open_"))
    shots = []
    for i, f in enumerate(names):
        o = uri(os.path.join(frames, f))
        c = uri(os.path.join(frames, f.replace("open_", "closed_")))
        write(out, f"s07_f{i:02d}", 7, s07(o, c))
        shots.append(Image.open(os.path.join(out, f"s07_f{i:02d}.png")).convert("RGB"))
    shots[len(shots) // 2].save(os.path.join(out, "s07.png"))
    # one shared palette from a strip of frames so the static parts never flicker
    strip = Image.new("RGB", (1280, 720 * 4))
    for k in range(4):
        strip.paste(shots[k * len(shots) // 4], (0, 720 * k))
    mark = shots[0].crop((1152, 50, 1216, 86)).resize((1280, 720), Image.NEAREST)
    strip.paste(mark, (0, 720 * 3))
    pal = strip.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    gif = [s.quantize(palette=pal, dither=Image.Dither.NONE) for s in shots]
    gif[0].save(os.path.join(out, "s07.gif"), save_all=True, append_images=gif[1:], duration=94, loop=0, optimize=True)
    for i in range(len(shots)):
        os.remove(os.path.join(out, f"s07_f{i:02d}.png"))
        os.remove(os.path.join(out, f"s07_f{i:02d}.html"))
    print("s07.gif", os.path.getsize(os.path.join(out, "s07.gif")) // 1024, "KB,", len(shots), "frames")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
