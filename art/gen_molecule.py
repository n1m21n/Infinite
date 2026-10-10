#!/usr/bin/env python3
"""Generates molecule.inf: frosted-glass molecules whose bulbs breathe with the sound.
Art prompt: macro still-life of translucent pale-blue glass molecules on pure black, lavender bulb
ends with thin white seams, soft rim light; every bulb listens to its own frequency band."""
import math, random

random.seed(11)
out = ["infinite-patch 1",
       "# Glass molecules (see tools: art/gen_molecule.py). Bulbs = spectrum bands, hubs = level, light = onsets."]
N = [0]
mods = []
exprs = []
wires = []


def node(cat, typ, nid, body=""):
    N[0] += 1
    out.append(f"node {N[0]} {cat} {typ}\n  id {nid}\n{body}end")
    return nid


def vec(a, b):
    return [b[i] - a[i] for i in range(3)]


def norm(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / l for x in v]


def euler_y_to(t):
    """rotX/rotZ (degrees) that carry +Y to direction t, for model = T*Rx*Ry*Rz*S."""
    t = norm(t)
    rz = math.degrees(math.atan2(-t[0], math.hypot(t[1], t[2])))
    rx = math.degrees(math.atan2(t[2], t[1]))
    return rx, rz


def geo(cat_type, nid, pos, scale=(1, 1, 1), rot=(0, 0, 0), extra=""):
    b = (f"  f posX {pos[0]:.4f}\n  f posY {pos[1]:.4f}\n  f posZ {pos[2]:.4f}\n"
         f"  f scaleX {scale[0]:.4f}\n  f scaleY {scale[1]:.4f}\n  f scaleZ {scale[2]:.4f}\n"
         f"  f rotX {rot[0]:.3f}\n  f rotZ {rot[2]:.3f}\n  f rotY {rot[1]:.3f}\n") + extra
    return node("3D", cat_type, nid, b)


GLASS = ("  f roughness 0.28\n  f metallic 0\n  f opacity 0.9\n  f transmission 0.35\n  f ior 1.45\n"
         "  f specular 0.9\n  f clearcoat 0.9\n  f clearcoatRoughness 0.08\n  f sheen 0.5\n"
         "  c sheenColor 0.8 0.85 1\n  f subsurface 0.25\n  c subsurfaceColor 0.55 0.75 0.95\n")
BODY = "  c color 0.62 0.82 0.93\n" + GLASS + "  f iridescence 0.15\n"
BULB = "  c color 0.72 0.66 0.93\n" + GLASS + "  f iridescence 0.35\n"
SEAM = ("  c color 1 1 1\n  f opacity 0.9\n  f roughness 0.2\n  c emissionColor 0.85 0.92 1\n  f emission 0.4\n"
        "  f tube 0.06\n")

hubs = [(0.0, 0.85, 0.1), (-1.0, -0.05, 0.55), (0.95, -0.15, -0.1), (-0.15, -0.95, 0.35)]
hub_r = [0.48, 0.42, 0.46, 0.40]
body, bulbs, seams = [], [], []
units = []  # (transform ids, centre, size, band) for modulation


def xform(nid, src, c, sc=1.0, extra="", rot=(0, 0, 0)):
    node("3D", "Transform", nid, f"  f scaleX {sc:.4f}\n  f scaleY {sc:.4f}\n  f scaleZ {sc:.4f}\n  f rotX {rot[0]:.3f}\n  f rotY {rot[1]:.3f}\n  f rotZ {rot[2]:.3f}\n  f offsetX {c[0]:.4f}\n  f offsetY {c[1]:.4f}\n  f offsetZ {c[2]:.4f}\n" + extra)
    wires.append(f"geo {nid} 0 {src}")
    return nid


band = 0
for hi, (h, r) in enumerate(zip(hubs, hub_r)):
    hid = geo("Sphere", f"hubsrc{hi}", (0, 0, 0), extra="  i detail 36\n")
    body.append(xform(f"hub{hi}", hid, h, 2 * r))
    units.append(("hub", [f"hub{hi}"], r, 0))
    centre = norm(h)
    for ai in range(3):
        d = norm([centre[0] + random.uniform(-1.1, 1.1), centre[1] + random.uniform(-1.1, 1.1),
                  centre[2] + random.uniform(-1.1, 1.1)])
        L = random.uniform(0.5, 0.8)
        w = random.uniform(0.34, 0.44)
        bd = random.uniform(0.42, 0.6)
        start = [h[i] + d[i] * (r * 0.6) for i in range(3)]
        mid = [start[i] + d[i] * L / 2 for i in range(3)]
        rx, rz = euler_y_to([-x for x in d])  # apex (+Y) toward the hub
        body.append(geo("Cone", f"arm{hi}_{ai}", mid, (w, L, w), (rx, 0, rz)))
        tip = [start[i] + d[i] * (L + bd * 0.1) for i in range(3)]
        bsrc = geo("Sphere", f"bulbsrc{hi}_{ai}", (0, 0, 0), extra="  i detail 28\n")
        sd = norm([random.uniform(-1, 1) for _ in range(3)])
        srx, srz = euler_y_to(sd)
        ssrc = geo("Torus", f"seamsrc{hi}_{ai}", (0, 0, 0), (1, 1, 1), (0, 0, 0), extra="  f tube 0.06\n")
        bulbs.append(xform(f"bulb{hi}_{ai}", bsrc, tip, bd))
        seams.append(xform(f"seam{hi}_{ai}", ssrc, tip, bd * 1.02, rot=(srx, 0, srz)))
        units.append(("bulb", [f"bulb{hi}_{ai}", f"seam{hi}_{ai}"], bd, band % 8))
        band += 1
for a_, b_ in [(0, 1), (0, 2), (1, 3), (2, 3), (1, 2)]:
    pa, pb = hubs[a_], hubs[b_]
    v = vec(pa, pb)
    L = math.sqrt(sum(x * x for x in v))
    mid = [(pa[i] + pb[i]) / 2 for i in range(3)]
    rx, rz = euler_y_to(v)
    body.append(geo("Cylinder", f"bridge{a_}{b_}", mid, (0.2, L, 0.2), (rx, 0, rz)))

jn = [0]


def join_tree(items):
    level = list(items)
    while len(level) > 1:
        nxt = []
        for i in range(0, len(level), 4):
            grp = level[i:i + 4]
            if len(grp) == 1:
                nxt.append(grp[0]); continue
            jid = node("3D", "Join Geometry", f"join{jn[0]}"); jn[0] += 1
            for k, g in enumerate(grp):
                wires.append(f"geo {jid} {k} {g}")
            nxt.append(jid)
        level = nxt
    return level[0]


def material(nid, src, body_txt):
    node("3D", "Material", nid, body_txt)
    wires.append(f"geo {nid} 0 {src}")
    return nid


GL = ("  f roughness 0.28\n  f metallic 0\n  f opacity 0.9\n  f transmission 0.35\n  f ior 1.45\n"
      "  f specular 0.9\n  f clearcoat 0.9\n  f clearcoatRoughness 0.08\n  f sheen 0.5\n"
      "  c sheenColor 0.8 0.85 1\n  f subsurface 0.25\n  c subsurfaceColor 0.55 0.75 0.95\n")
mat_body = material("matbody", join_tree(body), "  c color 0.62 0.82 0.93\n" + GL + "  f iridescence 0.15\n")
mat_bulb = material("matbulb", join_tree(bulbs), "  c color 0.72 0.66 0.93\n" + GL + "  f iridescence 0.35\n")
mat_seam = material("matseam", join_tree(seams),
                    "  c color 1 1 1\n  f opacity 0.9\n  f roughness 0.2\n  c emissionColor 0.85 0.92 1\n  f emission 0.4\n")
root_wires = [f"geo scene 0 {mat_body}", f"geo scene 1 {mat_bulb}", f"geo scene 2 {mat_seam}"]

# camera, lights, render
node("3D", "Camera", "cam", "  f distance 7.2\n  f elevation 12\n  f azimuth 20\n  f fov 40\n  f orbitPerBeat 0.08\n")
node("3D", "Light", "key", "  f azimuth 35\n  f elevation 40\n  f intensity 2.6\n  c color 0.85 0.92 1\n")
node("3D", "Light", "rimlight", "  f azimuth -160\n  f elevation 15\n  f intensity 2.4\n  c color 0.7 0.75 1\n")
node("3D", "Render 3D", "scene",
     "  f width 1280\n  f height 1280\n  c bg 0 0 0\n  b envAsBackground 0\n  c ambient 0.25 0.3 0.45\n  f rim 0.8\n"
     "  c envSky 0.55 0.7 0.95\n  c envHorizon 0.25 0.3 0.45\n  c envGround 0.02 0.02 0.04\n  f envIntensity 2.5\n"
     "  i samples 3\n  b shadows 0\n  f exposure 1.1\n")
wires += root_wires + ["geo scene 4 cam", "geo scene 5 key", "geo scene 6 rimlight"]

# image chain
node("Effects", "bloom", "glow", "  f uThreshold 0.5\n  f uIntensity 1.1\n  f uRadius 6\n")
node("Compositing", "Trails", "ghost", "  f decay 0.55\n  f zoom 1.004\n  f driftY 0.0005\n")
node("Effects", "lensdistortion", "lens", "  f uBarrel 0.08\n  f uChroma 0.5\n")
node("Compositing", "color adjustments", "grade", "  f uContrast 0.12\n  f uSaturation 1.1\n  f uVibrance 0.2\n")
node("Effects", "vignette", "vig", "  f uAmount 0.7\n")
node("Effects", "addnoise", "grain", "  f uAmount 0.03\n")
node("Utility", "Output", "out")
wires += ["cable glow 0 scene", "cable ghost 0 glow", "cable lens 0 ghost", "cable grade 0 lens",
          "cable vig 0 grade", "cable grain 0 vig", "cable out 0 grain"]

# music: pad + bells, effects chain, analysis
node("Notes", "Random Note Generator", "padnotes", "  i rangeLow 48\n  i rangeHigh 67\n  i rateMode 0\n  f rateBeats 2\n  i maxStep 3\n")
node("Notes", "Random Note Generator", "bellnotes", "  i rangeLow 72\n  i rangeHigh 91\n  i rateMode 0\n  f rateBeats 0.5\n  i maxStep 5\n")
node("Synths", "Wavetable", "pad", "  f volume 0.7\n  i a.unison 5\n  f a.detune 18\n  f a.stereoWidth 0.9\n  f a.ampAttack 400\n  f a.ampRelease 1500\n  f a.cutoff 2500\n  b b.on 1\n  i b.octave -1\n  f b.volume 0.5\n")
node("Synths", "Metallic", "bell", "  f volume 0.55\n  f decay 3.2\n  f stiffness 0.35\n")
node("AudioEffects", "Chorus", "padchorus", "  f mix 0.45\n  f rate 0.3\n  f depth 6\n")
node("AudioEffects", "Delay", "belldelay", "  f mix 0.4\n  f feedback 45\n")
node("Utility", "Mixer", "mix", "  i channels 2\n")
node("AudioEffects", "Reverb", "hall", "  f mix 0.4\n  f size 0.8\n  f decay 4\n")
node("AudioEffects", "Limiter", "lim")
node("Modulators", "Audio Analyze", "ana", "  f gain 1.6\n  f attack 0.35\n  f release 0.3\n")
wires += ["note pad 0 padnotes", "note bell 0 bellnotes", "aud padchorus 0 pad", "aud belldelay 0 bell",
          "aud mix 0 padchorus", "aud mix 1 belldelay", "aud hall 0 mix", "aud lim 0 hall",
          "aud out 1 lim", "aud ana 0 lim"]

# slow movers
node("Modulators", "LFO", "slow", "  i shape 0\n  f rateBeats 32\n")
node("Modulators", "LFO", "drift", "  i shape 0\n  f rateBeats 11\n")


def M(dst, key, src, o, lo, hi):
    mods.append(f"mod {dst} {key} {src} {o} 0 1 0.5 {lo:.4f} {hi:.4f}")


# analyzer outputs: 0 level,1 low,2 mid,3 high,4 onset,5..12 b1..b8
def MS(nids, src, o, lo, hi, size):
    for n_ in nids:
        sz = size * (1.02 if n_.startswith("seam") else 1)
        for ax in "XYZ":
            M(n_, "scale" + ax, src, o, lo * sz, hi * sz)


for kind, ids_, size, bnd in units:
    if kind == "bulb":
        MS(ids_, "ana", 5 + bnd, 0.75, 1.6, size)
    else:
        MS(ids_, "ana", 0, 0.9, 1.3, 2 * size)
M("matbulb", "iridescence", "ana", 3, 0.2, 0.95)
M("matbulb", "transmission", "ana", 2, 0.5, 0.95)
M("matseam", "emission", "ana", 4, 0.2, 2.5)
M("matbody", "sheen", "ana", 1, 0.3, 1.0)
M("key", "intensity", "ana", 4, 2.0, 4.0)
M("rimlight", "azimuth", "slow", 0, 120, 179)
M("cam", "elevation", "drift", 0, 4, 28)
M("cam", "distance", "ana", 1, 7.7, 6.6)
M("glow", "uIntensity", "ana", 0, 0.7, 2.2)
M("lens", "uChroma", "ana", 3, 0.2, 1.4)
M("ghost", "decay", "ana", 1, 0.35, 0.8)
M("grade", "uHueShift", "slow", 0, -0.08, 0.1)
M("pad", "a.cutoff", "slow", 0, 900, 7000)
M("hall", "mix", "ana", 0, 0.25, 0.6)

out += wires + mods
open("molecule.inf", "w").write("\n".join(out) + "\n")
print(N[0], "nodes,", len(mods), "mods")
