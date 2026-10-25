#!/usr/bin/env python3
"""Style cards for every art style: layers (what shapes, how arranged, how sized and coloured), finish chain, and the meaning that explains the look.
Palette comes from styles_art.json via styled.art_styles(); card ids equal art style ids. stack.compile_card turns a card into a patch."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stack

L = lambda **k: k
RECIPES = {
 "y2k_acid": ("Chrome-era optimism: glossy blobs, stars and rings in hot pink and cyan, wobbling like liquid metal.", 3,
   [L(mark="ring", arr="spiral", n=(10, 18), size=(0.08, 0.3), col=[0, 1], line=10, spin=1), L(mark="circle", arr="scatter", n=(4, 8), size=(0.1, 0.3), col=[0, 1, 2]), L(mark="cross", arr="thirds", n=(3, 6), size=(0.1, 0.25), col=[2], rot="free")], ["bloom", "twirl"]),
 "glitch_datamosh": ("Compression failing on purpose: blocks of the picture slip, repeat and smear against a dark frame.", 0,
   [L(mark="square", arr="grid", n=(40, 80), size=(0.05, 0.14), col=[1, 2, 3]), L(mark="bar", arr="bands", n=(6, 12), size=(0.3, 0.9), col=[1, 2, 3], rot="snap")], ["glitch", "pixelate"]),
 "ditherpunk": ("Two inks and a threshold: every tone is made of dots, a limit turned into a look.", 1,
   [L(arr="cells", n=(26, 40), col=[0, 1], field="sine", freq=1.4)], ["dither"]),
 "neo_brutalist": ("Raw blocks, hard outlines and one loud accent: a page that refuses to be polite.", 0,
   [L(mark="square", arr="thirds", n=(3, 5), size=(0.2, 0.5), col=[2, 3], line=10), L(text=["NO", "RAW", "OK", "GRID", "!"], arr="thirds", n=(2, 3), size=(0.2, 0.4), col=[1], tscale=1.2)], ["edge outline"]),
 "technical_blueprint": ("Construction lines on blue paper: the drawing before the object, measured and labelled.", 0,
   [L(arr="stripes", n=(14, 20), col=[1], line=2, freq=0.1, warp=0, amp=0.0), L(mark="ring", arr="concentric", n=(4, 7), col=[1], line=3), L(mark="rule", arr="bands", n=(4, 8), size=(0.3, 0.9), col=[2], rot="snap"), L(text=["A-A", "1:20", "R40", "SEC"], arr="thirds", n=(3, 5), size=(0.05, 0.1), col=[1], tscale=0.9)], []),
 "retro_surreal": ("Mid-century print colours and impossible scale: a calm picture with something quietly wrong.", 3,
   [L(mark="circle", arr="thirds", n=(1, 2), size=(0.4, 0.7), col=[1]), L(mark="half", arr="axis", n=(2, 4), size=(0.15, 0.4), col=[0, 2], rot="snap"), L(mark="tri", arr="diagonal", n=(3, 5), size=(0.1, 0.25), col=[3])], ["halftone", "addnoise"]),
 "analog_crt": ("A glowing tube: scanlines, rounded glass and signal bleeding into black.", 1,
   [L(arr="stripes", n=(34, 50), col=[0], line=3, freq=1.0, warp=1, amp=0.01), L(mark="ring", arr="concentric", n=(3, 6), col=[0, 2], line=4)], ["lensdistortion", "vignette", "bloom"]),
 "thundr_brutalist": ("Heavy black slabs, white gaps and yellow warnings: poster as a loud sign.", 0,
   [L(mark="bar", arr="bands", n=(4, 7), size=(0.5, 1.0), col=[0], rot="snap"), L(mark="square", arr="thirds", n=(2, 3), size=(0.2, 0.45), col=[2, 3]), L(text=["STOP", "NOW", "RAW"], arr="thirds", n=(1, 2), size=(0.3, 0.5), col=[1], tscale=1.3)], ["halftone"]),
 "chavetta_montage": ("Cut-out photo-collage logic: torn rectangles layered at odd angles, printed grain on top.", 0,
   [L(mark="square", arr="scatter", n=(8, 14), size=(0.1, 0.35), col=[1, 2, 3], rot="free"), L(mark="bar", arr="scatter", n=(3, 6), size=(0.2, 0.5), col=[0], rot="free")], ["halftone", "addnoise"]),
 "lilliputian_surreal": ("Tiny figures in a huge quiet space: scale jumps and soft light make the ordinary strange.", 0,
   [L(mark="circle", arr="thirds", n=(1, 1), size=(0.6, 0.8), col=[1]), L(mark="tri", arr="scatter", n=(6, 12), size=(0.02, 0.05), col=[2])], ["gaussianblur", "vignette"]),
 "ukiyoe_cartography": ("Flat colour planes, bold outline and rhythmic wave: maps and prints from one hand.", 2,
   [L(arr="waves", n=(5, 8), col=[0, 3, 2], amp=0.1, line=3), L(mark="circle", arr="thirds", n=(1, 1), size=(0.25, 0.35), col=[1]), L(mark="ring", arr="thirds", n=(1, 2), size=(0.1, 0.2), col=[1], line=5)], ["posterize"]),
 "indie_psychedelic": ("Melting colour and repeated eyes: the picture breathes in warm saturated loops.", 3,
   [L(mark="ring", arr="concentric", n=(12, 20), col=[0, 1, 2, 3], line=14), L(mark="circle", arr="spiral", n=(14, 26), size=(0.05, 0.14), col=[2, 1])], ["twirl", "bloom"]),
 "riso_manga_zine": ("Two or three inks misregistered on cheap paper, with halftone dots and panel borders.", 3,
   [L(mark="circle", arr="grid", n=(40, 70), size=(0.03, 0.08), col=[0]), L(mark="circle", arr="thirds", n=(2, 3), size=(0.25, 0.5), col=[1, 2]), L(mark="rule", arr="bands", n=(3, 5), size=(0.5, 1.0), col=[3], rot="snap")], ["halftone"]),
 "liminal_void": ("An empty corridor of light: almost nothing, lit and soft, slightly wrong.", 3,
   [L(mark="square", arr="thirds", n=(1, 2), size=(0.5, 0.9), col=[0, 3]), L(mark="rule", arr="bands", n=(2, 3), size=(0.8, 1.0), col=[2])], ["gaussianblur", "vignette", "addnoise"]),
 "vaporwave_collage": ("Pastel neon, marble busts and grids from a lost mall: nostalgia that knows it is fake.", 0,
   [L(arr="horizon", n=(8, 12), col=[1], y=0.6), L(arr="sun", col=[0], cut=3, r=0.3, y=0.4), L(mark="tri", arr="thirds", n=(2, 4), size=(0.1, 0.25), col=[2, 3], rot="snap")], ["bloom", "ripple"]),
 "synthwave_grid": ("Night road to a striped sun: a neon grid running to the horizon.", 0,
   [L(arr="sun", col=[1], cut=0, r=0.34, y=0.42), L(arr="horizon", n=(10, 14), col=[2], y=0.58, line=3), L(mark="circle", arr="scatter", n=(10, 20), size=(0.005, 0.012), col=[3])], ["bloom", "radialblur"]),
 "cyberpunk_neon": ("Rain, signs and magenta-cyan light against black: a city seen through glow.", 0,
   [L(mark="bar", arr="bands", n=(6, 10), size=(0.1, 0.5), col=[1, 2, 3], rot="snap"), L(mark="square", arr="grid", n=(30, 60), size=(0.02, 0.06), col=[2, 1]), L(text=["24H", "BAR", "雨", "NEON"], arr="thirds", n=(2, 3), size=(0.1, 0.2), col=[1, 2], tscale=1.0)], ["bloom", "glitch"]),
 "generative_plotter": ("One pen, one idea, repeated: lines laid on cream paper by a machine's patience.", 0,
   [L(arr="stripes", n=(40, 60), col=[1], line=1.5, freq=3, warp=8, amp=0.03), L(mark="ring", arr="spiral", n=(14, 30), size=(0.04, 0.2), col=[2], line=2)], []),
 "demoscene_plasma": ("Sine waves summed into flowing colour: maths as a screen saver you cannot stop watching.", 0,
   [L(arr="cells", n=(36, 60), col=[0, 1, 2, 3], field="sine", freq=2.2, drift=0.2)], ["bloom"]),
 "vector_wireframe": ("Only edges: green lines on black, the skeleton of a solid.", 0,
   [L(arr="horizon", n=(12, 16), col=[1], y=0.5, line=2), L(mark="ring", arr="concentric", n=(6, 10), col=[1], line=2), L(mark="tri", arr="radial", n=(8, 14), size=(0.08, 0.18), col=[1], rot="radial", fill=0, line=2)], ["bloom"]),
 "pixel_art_8bit": ("Four greens and square pixels: a screen with nothing but position and tone.", 0,
   [L(arr="cells", n=(30, 44), col=[0, 1, 2, 3], field="noise", freq=3.0, drift=0.02), L(mark="square", arr="grid", n=(6, 16), size=(0.05, 0.1), col=[3, 0], rot="none")], ["pixelate", "posterize"]),
 "bauhaus_geometry": ("Primary colour, pure geometry and right angles: a school teaching that form follows function.", 0, None, []),
 "suprematist_blocks": ("Pure forms floating in white: black squares and red bars on a tilt, feeling instead of objects.", 0,
   [L(mark="square", arr="diagonal", n=(2, 4), size=(0.15, 0.45), col=[1], rot="free"), L(mark="bar", arr="scatter", n=(3, 6), size=(0.2, 0.5), col=[2], rot="free"), L(mark="circle", arr="thirds", n=(1, 2), size=(0.06, 0.18), col=[1, 2])], []),
 "cymatics_sine": ("Sound made visible: sand on a plate settling into rings and nodes.", 0,
   [L(mark="ring", arr="concentric", n=(18, 30), col=[1], line=2), L(arr="stripes", n=(24, 36), col=[2], line=1.5, freq=4, warp=3, amp=0.02)], ["symmetry", "bloom"]),
 "brutalist_architecture": ("Poured concrete, repeated openings, hard shadow: mass first, ornament never.", 2,
   [L(mark="square", arr="grid", n=(18, 36), size=(0.05, 0.1), col=[2]), L(mark="bar", arr="bands", n=(3, 5), size=(0.6, 1.0), col=[1, 2], rot="snap")], ["vignette"]),
 "holographic_iridescent": ("Colour that shifts with angle: soft blobs and ribbons of pearl, pink and mint.", 0,
   [L(mark="circle", arr="spiral", n=(8, 16), size=(0.2, 0.5), col=[0, 1, 2]), L(arr="stripes", n=(12, 18), col=[3, 1], line=18, freq=1.5, warp=3, amp=0.06)], ["bloom", "kaleidoscope"]),
 "acid_graphics": ("Fluorescent yellow and magenta, warped type and ripped shapes: rave flyer energy.", 1,
   [L(mark="ring", arr="concentric", n=(8, 14), col=[0, 2], line=16), L(text=["ACID", "RAVE", "909", "ON"], arr="thirds", n=(2, 3), size=(0.2, 0.4), col=[0, 2], tscale=1.2), L(mark="cross", arr="scatter", n=(3, 7), size=(0.08, 0.2), col=[2], rot="free")], ["twirl"]),
 "minimal_signal": ("One accent on near-black: a dot, a line, a hint of a wave: restraint as the loudest move.", 0,
   [L(arr="stripes", n=(6, 10), col=[1], line=2, freq=1.5, warp=2, amp=0.03), L(mark="circle", arr="thirds", n=(1, 1), size=(0.04, 0.08), col=[2])], []),
 "data_landscape": ("Numbers as terrain: layered ridges with quiet blues and one bright peak.", 0,
   [L(arr="waves", n=(10, 16), col=[1, 0, 2], amp=0.14, line=2)], ["vignette"]),
 "swiss_typographic": ("A grid, one accent colour and type as image: order and neutrality so the message leads.", 0, None, []),
}


def card_for(aid, art):
    if aid == "op_art":
        return dict(stack.CARDS["op_art"])
    if aid in RECIPES and RECIPES[aid][2] is None:   # hand cards that live in stack.py
        c = dict(stack.CARDS["bauhaus" if aid == "bauhaus_geometry" else "swiss"]); c["id"] = aid
        return c
    m, bg, layers, finish = RECIPES[aid]
    pal = art["palette"]
    return dict(id=aid, meaning=m, palette=pal, bg=min(bg, len(pal) - 1), layers=[dict(l, col=[i for i in l.get("col", range(len(pal))) if i < len(pal)] or [0]) for l in layers], finish=finish)


def sheet(seed, out, binary, ids=None):
    import subprocess, universe as u, styled
    from PIL import Image, ImageDraw
    facts = u.load_facts(); A = styled.art_styles(); ids = ids or sorted(A)
    os.makedirs(out, exist_ok=True)
    cols = 8; rows = (len(ids) + cols - 1) // cols
    sh = Image.new("RGB", (270 * cols, 480 * rows), (30, 30, 30)); dr = ImageDraw.Draw(sh)
    for k, aid in enumerate(ids):
        inf = os.path.join(out, "%s_%d.inf" % (aid, seed))
        open(inf, "w").write(stack.compile_card(card_for(aid, A[aid]), seed, facts))
        d = os.path.join(out, "%s_%d" % (aid, seed)); os.makedirs(d, exist_ok=True)
        subprocess.run([binary, "--frame", inf, "0", d + "/", "--size", "270x480"], capture_output=True, text=True, timeout=120)
        pngs = [f for f in os.listdir(d) if f.endswith(".png")]
        x, y = (k % cols) * 270, (k // cols) * 480
        if pngs:
            sh.paste(Image.open(os.path.join(d, pngs[0])).convert("RGB").resize((270, 480)), (x, y))
        dr.rectangle([x, y, x + 270, y + 14], fill=(0, 0, 0)); dr.text((x + 3, y + 1), aid + (" (no image)" if not pngs else ""), fill=(255, 255, 255))
    sh.save(os.path.join(out, "sheet_%d.png" % seed)); print("->", os.path.join(out, "sheet_%d.png" % seed))


if __name__ == "__main__":
    import universe as u
    sheet(int(sys.argv[1]), sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else u.DEFAULT_BIN)
