#!/usr/bin/env python3
"""Writes handheld.inf: a pocket game console built from 3D nodes, staged in a 3D View in front of a
Y2K poster wall, a GLSL game on its screen, and chiptune game sounds from Piano Rolls. Run: python3 gen.py (writes handheld.inf next to this file, then lays it out)."""
import os, sys

nodes = []   # (id, category, type, lines)
wires = []

def node(nid, cat, typ, **kv):
    lines = []
    if "_uid" in kv:
        lines.append(f"uid {kv.pop('_uid')}")
    for k, v in kv.items():
        if isinstance(v, bool):
            lines.append(f"b {k} {int(v)}")
        elif isinstance(v, tuple):
            lines.append(f"c {k} {v[0]} {v[1]} {v[2]}")
        elif isinstance(v, int):
            lines.append(f"i {k} {v}")
        elif isinstance(v, float):
            lines.append(f"f {k} {v}")
        else:
            lines.append(f"s {k} {v}")
    nodes.append((nid, cat, typ, lines))

def js(code):
    return code.strip().replace("\n", "\\n")

# ---------------------------------------------------------------- geometry
BEIGE = (0.80, 0.70, 0.64)

SHELL = r"""
// Speckled beige plastic for the shell: fine grain plus sparse dark and lime flecks.
vec2 px = uv * 1024.0;
vec3 col = vec3(0.80, 0.70, 0.64) * (0.97 + 0.05 * rand(floor(px)));
float r = rand(floor(px / 6.0));
if (r > 0.993) col = vec3(0.30, 0.26, 0.24);
else if (r > 0.989) col = vec3(0.78, 0.95, 0.20);
return vec4(col, 1.0);
"""
node("shellTex", "Source", "Formula", width=1024.0, height=1024.0, formula=SHELL.strip().replace("\n", "\\n"))
node("body", "3D", "Rounded Cube", tube=0.12, detail=48,
     scaleX=1.6, scaleY=2.1, scaleZ=0.34, color=(1.0, 1.0, 1.0), metallic=0.15, roughness=0.5,
     clearcoat=0.15)
node("bezel", "3D", "Rounded Cube", tube=0.14, detail=36, posY=0.36, posZ=0.165,
     scaleX=1.32, scaleY=0.98, scaleZ=0.03, color=(0.02, 0.02, 0.025), metallic=0.0,
     roughness=0.18, clearcoat=1.0, clearcoatRoughness=0.05)
node("screen", "3D", "Plane", posY=0.37, posZ=0.182, scaleX=1.22, scaleY=0.9,
     color=(1.0, 1.0, 1.0), roughness=0.25, metallic=0.0)
node("glass", "3D", "Material", color=(1.0, 1.0, 1.0), roughness=0.3, metallic=0.0, emission=1.2,
     emissionColor=(1.0, 1.0, 1.0))

PLASTIC = r"""
// Coloured plastic parts: buttons, clover, tag, cartridge, SD card, plug and cable.
function pill(w, h, d) {   // a stadium in XY, d thick, facing +z
  box(w - h, h, d);
  push(); translate(-(w - h) / 2, 0, 0); rotateX(HALF_PI); cylinder(h / 2, d, 32); pop();
  push(); translate((w - h) / 2, 0, 0); rotateX(HALF_PI); cylinder(h / 2, d, 32); pop();
}
function button(x, y, col, press) {
  push(); translate(x, y, 0.17 - 0.035 * press); rotateX(HALF_PI);
  fill(col[0], col[1], col[2]);
  translate(0, 0.045, 0); cylinder(0.105, 0.09, 48);
  translate(0, 0.045, 0); scale(1, 0.32, 1); sphere(0.104, 32);
  pop();
}
// Same clock as the screen and the sound: a hop every second (A), the coin half a second later (B).
function pulse(x) { return x >= 0 && x < 0.14 ? sin(PI * x / 0.14) : 0; }
function draw(t) {
  const ph = t - floor(t);
  button(-0.56, -0.55, [0.12, 0.33, 0.85], pulse(ph));
  button(-0.31, -0.55, [0.93, 0.70, 0.13], pulse(ph - 0.47));
  button(-0.06, -0.55, [0.88, 0.22, 0.16], 0);
  // clover
  fill(0.62, 0.80, 0.93);
  for (let i = 0; i < 4; i++) {
    push(); translate(0.47, -0.22, 0.175); rotateZ(i * HALF_PI + PI / 4); translate(0.04, 0, 0);
    scale(1, 1, 0.35); sphere(0.042, 20); pop();
  }
  // white slot under the screen
  fill(0.97, 0.97, 0.96);
  push(); translate(0, -0.2, 0.175); pill(0.3, 0.07, 0.02); pop();
  // knob cap
  fill(0.98, 0.80, 0.18);
  push(); translate(0.47, -0.62, 0.33); rotateX(HALF_PI); cylinder(0.155, 0.03, 64); pop();
  // keychain tag on top
  fill(0.98, 0.82, 0.20);
  push(); translate(-0.32, 1.1, 0.0); rotateZ(radians(4));
  box(0.32, 0.2, 0.07); translate(0, 0.1, 0); rotateX(HALF_PI); cylinder(0.16, 0.07, 48); pop();
  // SD card, right side
  fill(0.30, 0.38, 0.92);
  push(); translate(0.83, 0.55, 0.0); rotateZ(radians(-3)); box(0.14, 0.36, 0.035); pop();
  fill(0.97, 0.97, 0.97);
  push(); translate(0.86, 0.55, 0.019); rotateZ(radians(-3)); plane(0.06, 0.28); pop();
  // red cartridge, bottom left
  fill(0.94, 0.33, 0.20);
  push(); translate(-0.4, -1.1, 0.02); box(0.42, 0.22, 0.1); pop();
  // red side switch and pink slot, left face
  fill(0.90, 0.30, 0.25);
  push(); translate(-0.805, 0.82, 0); rotateY(-HALF_PI); pill(0.16, 0.05, 0.02); pop();
  fill(0.90, 0.35, 0.55);
  push(); translate(-0.805, -0.55, 0); rotateY(-HALF_PI); rotateZ(HALF_PI); pill(0.16, 0.05, 0.02); pop();
  // USB plug and cable
  fill(0.96, 0.96, 0.95);
  push(); translate(0.12, -1.11, 0); box(0.14, 0.12, 0.08); pop();
  let px = 0.12, py = -1.17, pz = 0;
  for (let i = 1; i <= 40; i++) {
    const s = i / 40;
    const x = 0.12 + 0.9 * s * s, y = -1.17 - 0.9 * s + 0.2 * s * s, z = 0.25 * s;
    tube(px, py, pz, x, y, z, 0.025, 12);
    push(); translate(x, y, z); sphere(0.025, 8); pop();
    px = x; py = y; pz = z;
  }
}
"""

DARK = r"""
// Dark rubber and holes: the big knob, grille slots, screw slots, port, tag hole, smiley face.
function draw(t) {
  fill(0.05, 0.05, 0.055);
  push(); translate(0.47, -0.62, 0.25); rotateX(HALF_PI); cylinder(0.205, 0.16, 64); pop();
  for (let i = 0; i < 10; i++) {   // speaker grille on the left face
    push(); translate(-0.802, 0.05 + i * 0.05, 0); box(0.01, 0.022, 0.18); pop();
  }
  for (const [x, y] of [[-0.72, 0.97], [0.72, 0.97], [-0.72, -0.97], [0.72, -0.97]]) {
    push(); translate(x, y, 0.183); rotateZ(0.6); box(0.05, 0.008, 0.004); pop();
  }
  push(); translate(-0.32, 1.24, 0); rotateX(HALF_PI); cylinder(0.035, 0.075, 24); pop();
  // smiley on the knob cap, turning slowly
  push(); translate(0.47, -0.62, 0); rotateZ(0.35 * sin(t * 0.8)); translate(-0.47, 0.62, 0);
  for (const x of [-0.045, 0.045]) { push(); translate(0.47 + x, -0.58, 0.346); scale(0.6, 1, 0.3); sphere(0.018, 12); pop(); }
  for (let i = 0; i <= 16; i++) {
    const a = PI * (1.15 + 0.7 * i / 16);
    push(); translate(0.47 + 0.07 * cos(a), -0.63 + 0.07 * sin(a), 0.346); scale(1, 1, 0.3); sphere(0.009, 8); pop();
  }
  pop();
}
"""

METAL = r"""
// Bright metal: corner screws and the knurled thumb wheel on the left.
function draw(t) {
  fill(0.80, 0.80, 0.82);
  for (const [x, y] of [[-0.72, 0.97], [0.72, 0.97], [-0.72, -0.97], [0.72, -0.97]]) {
    push(); translate(x, y, 0.175); rotateX(HALF_PI); cylinder(0.035, 0.014, 24); pop();
  }
  push(); translate(-0.86, -0.12, 0); rotateZ(HALF_PI);
  cylinder(0.11, 0.1, 64);
  for (let i = 0; i < 36; i++) { push(); rotateY(TAU * i / 36); translate(0.11, 0, 0); box(0.012, 0.1, 0.012); pop(); }
  translate(0, -0.06, 0); cylinder(0.06, 0.04, 32);
  pop();
}
"""

node("plastic", "3D", "Sketch 3D", code=js(PLASTIC))
node("plasticMat", "3D", "Material", color=(1.0, 1.0, 1.0), metallic=0.0, roughness=0.32,
     clearcoat=0.5, clearcoatRoughness=0.15)
node("dark", "3D", "Sketch 3D", code=js(DARK))
node("darkMat", "3D", "Material", color=(1.0, 1.0, 1.0), metallic=0.0, roughness=0.7)
node("metal", "3D", "Sketch 3D", code=js(METAL), animate=False)
node("metalMat", "3D", "Material", color=(1.0, 1.0, 1.0), metallic=1.0, roughness=0.25)
node("play", "3D", "Plane", posX=-0.5, posY=-0.27, posZ=0.1715, rotZ=0.14, scaleX=0.44,
     scaleY=0.22, color=(1.0, 1.0, 1.0), metallic=0.0, roughness=0.45)
node("playMat", "3D", "Material", color=(1.0, 1.0, 1.0), metallic=0.0, roughness=0.45,
     alphaCutoff=0.5, textureAlpha=True)
node("badge", "3D", "Text 3D", text="Infinite", font="Helvetica", depth=0.006,
     posX=0.0, posY=-0.92, posZ=0.172, scale=0.06, color=(0.55, 0.47, 0.43), metallic=0.4,
     roughness=0.5)

# the 3D View places the Group as one item and keeps each part's own material
node("group", "3D", "Group 3D", _uid=2001)

# Chrome pixel title behind the console: 5 x 7 cubes per letter. The pixels sweep in over the
# first bar, then a wave runs across them on every beat.
TITLE = """
const FONT = {
  P: ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
  L: ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
  A: [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
  Y: ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
};
const S = 0.12;
function draw(t) {
  fill(1, 1, 1);
  const word = "PLAY";
  const cols = word.length * 6 - 1;
  for (let l = 0; l < word.length; l++) {
    const rows = FONT[word[l]];
    for (let r = 0; r < 7; r++) {
      for (let c = 0; c < 5; c++) {
        if (rows[r][c] !== "#") continue;
        const col = l * 6 + c;
        const x = 0.3 + (col - (cols - 1) / 2) * S;
        const y = 1.62 + (6 - r) * S;
        const grow = Math.max(0, Math.min(1, (t - col * 0.06) * 5.0));
        const k = grow < 1 ? grow * (1.0 + 0.3 * sin(PI * grow)) : 1;
        const beat = t * 2.0;
        const wave = 0.06 * pow(max(0, sin(PI * (beat - floor(beat)) * 1.0 - col * 0.12)), 4);
        if (k <= 0) continue;
        push(); translate(x, y, -0.8 + wave); scale(k, k, k); box(S * 0.94, S * 0.94, S * 0.9); pop();
      }
    }
  }
}
"""
node("title", "3D", "Sketch 3D", code=js(TITLE))
node("chrome", "3D", "Material", _uid=2002, color=(0.86, 0.87, 0.90), metallic=1.0, roughness=0.22,
     clearcoat=1.0, clearcoatRoughness=0.05)

# Camera: a three-quarter product framing; the Modulation band moves it (sway, rise, push-in, roll, kick punch).
node("cam", "3D", "Camera", _uid=2004, fov=28.0, distance=10.0, azimuth=90.0, elevation=6.0, targetY=0.55)
node("key", "3D", "Light", _uid=2005, type=4, azimuth=50.0, elevation=32.0, distance=7.0, intensity=5.0,
     spotAngle=40.0, spotPenumbra=0.6, shadowRadius=0.6, color=(1.0, 0.96, 0.90))
node("fill", "3D", "Light", _uid=2006, type=0, azimuth=150.0, elevation=10.0, intensity=0.6,
     color=(0.80, 0.87, 1.0), castShadow=False)
node("rimL", "3D", "Light", _uid=2007, type=0, azimuth=-80.0, elevation=30.0, intensity=2.4,
     color=(0.92, 1.0, 0.80), castShadow=False)

# ---------------------------------------------------------------- screen visual
GAME = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "game.glsl")).read()
node("game", "Source", "Formula", width=640.0, height=480.0, formula=GAME.strip().replace("\n", "\\n"))
node("hud", "Source", "Text", text="...INFINITE 2026", fontName="Menlo", fontSize=22.0,
     fitToBox=False, posX=0.78, posY=0.93, tracking=0.08, width=640.0, height=480.0)
node("screenmix", "Compositing", "Layer Stack")
node("stickerBack", "Source", "Text", text="Play", fontName="Arial Rounded MT Bold", fontSize=300.0,
     color=(1.0, 1.0, 1.0), outlineWidth=20.0, outlineColor=(1.0, 1.0, 1.0), fitToBox=False,
     width=1024.0, height=512.0)
node("stickerInk", "Source", "Text", text="Play", fontName="Arial Rounded MT Bold", fontSize=300.0,
     color=(0.96, 0.45, 0.58), fitToBox=False, width=1024.0, height=512.0)
node("sticker", "Compositing", "Layer Stack")

# ---------------------------------------------------------------- poster wall and the 3D View
POSTER = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "poster.glsl")).read()
node("poster", "Source", "Formula", width=1080.0, height=1350.0, formula=POSTER.strip().replace("\n", "\\n"))
node("wall", "3D", "Plane", color=(1.0, 1.0, 1.0), roughness=0.85)
node("wallMat", "3D", "Material", _uid=2003, color=(1.0, 1.0, 1.0), roughness=0.85, metallic=0.0, emission=0.07,
     emissionColor=(1.0, 1.0, 1.0))

# 3D View places everything without cables: the console floats in front of the poster wall and casts a
# real shadow on it. The camera does the moving: an orbit, a crane bob and a push.
ITEMS = [
    (1, 2001, (0.0, 0.15, 0.0), (4.0, -24.0, 0.0), (1.0, 1.0, 1.0), "Console"),
    (2, 2002, (0.0, 0.0, -0.4), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), "PLAY title"),
    (3, 2003, (0.0, 0.9, -1.3), (0.0, 0.0, 0.0), (7.6, 9.5, 1.0), "Poster wall"),
    (4, 2004, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), "Camera"),
    (5, 2005, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), "Key"),
    (6, 2006, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), "Fill"),
    (7, 2007, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), "Rim"),
]
scene = ["next 8", "camera 4", "world 0 1 0.30 0.30 0.33 1 0"]
for i, u, p, r, k, name in ITEMS:
    scene.append("item %d %d %g %g %g %g %g %g %g %g %g 1" % (i, u, *p, *r, *k))
    scene.append("label %d %s" % (i, name))
node("view", "3D", "3D View", width=1080.0, height=1350.0, shadows=True, shadowQuality=2, shadowSoftness=3.0,
     shadowStrength=0.85, ao=0.6, supersample=2, samples=3, exposure=1.0, bloom=0.1, bloomThreshold=1.2,
     ambient=(0.12, 0.12, 0.14), envIntensity=0.6, tonemap=1, dofAperture=0.02, focusDistance=10.0, scene="\\n".join(scene))

label = lambda nid, text, x, y, size, font: node(nid, "Source", "Text", text=text, fontName=font, fontSize=size,
     color=(0.05, 0.05, 0.06), posX=x, posY=y, align=1, fitToBox=False, width=1080.0, height=1350.0)
label("press", "PRESS START", 0.5, 0.93, 40.0, "Arial Black")
node("blink", "Compositing", "opacity", uOpacity=1.0)
node("comp", "Compositing", "Layer Stack")
node("grain", "Effects", "addnoise", uAmount=0.03)
node("out", "Utility", "Output", includeAudio=True)


# ---------------------------------------------------------------- sound
LANES = []   # (node id, uid, notes [(tick, length, pitch, vel)], loop ticks)
PPQ = 960
def roll(nid, uid, steps, rate, gate=70.0, vel=100):
    """A Piano Roll node and its MIDI lane: one looping clip that plays `steps` (MIDI pitch or 0 = rest)."""
    step = int(rate * PPQ)
    notes = [(i * step, max(1, int(step * gate / 100)), n, vel) for i, n in enumerate(steps) if n]
    node(nid, "Notes", "Piano Roll", _uid=uid)
    LANES.append((nid, uid, notes, step * len(steps)))

# two bars of 8ths, A minor: Am | F G
roll("leadRoll", 3001, [69, 72, 76, 72, 81, 79, 76, 72, 77, 76, 72, 69, 74, 76, 79, 0], 0.5, 60.0, 96)
roll("bassRoll", 3002, [45, 57, 45, 57, 45, 57, 45, 57, 41, 53, 41, 53, 43, 55, 43, 55], 0.5, 45.0, 100)
roll("kickRoll", 3003, [36, 0, 0, 36, 0, 0, 0, 0, 36, 0, 0, 36, 0, 0, 0, 0], 0.5, 30.0, 120)
roll("snareRoll", 3004, [0, 0, 0, 0, 60, 0, 0, 0, 0, 0, 0, 0, 60, 0, 0, 0], 0.5, 30.0, 110)
roll("hatRoll", 3005, [0, 60, 0, 60, 0, 60, 0, 60, 0, 60, 0, 60, 0, 60, 60, 60], 0.5, 20.0, 90)
# one hop = two beats in 32nds: jump sweep on the hop, two-note coin at the top of it
roll("sfxRoll", 3006, [72, 76, 79, 84, 0, 0, 0, 0, 83, 88, 0, 0, 0, 0, 0, 0], 0.125, 90.0, 100)

node("lead", "Synths", "Oscillator", waveform=3, volume=0.5, ampAttack=2.0, ampDecay=120.0,
     ampSustain=0.6, ampRelease=80.0)
node("bass", "Synths", "Oscillator", waveform=1, volume=0.9, ampAttack=2.0, ampDecay=200.0,
     ampSustain=0.8, ampRelease=40.0)
node("kick", "Synths", "Oscillator", waveform=1, volume=1.0, octave=-1, ampAttack=1.0, ampDecay=90.0,
     ampSustain=0.0, ampRelease=30.0)
node("snare", "Synths", "Analog", osc1Vol=0.0, osc2Vol=0.0, noise=1.0, cutoff=7000.0,
     attack=1.0, decay=130.0, sustain=0.0, release=60.0, volume=0.7)
node("hat", "Synths", "Analog", osc1Vol=0.0, osc2Vol=0.0, noise=1.0, cutoff=16000.0,
     attack=1.0, decay=28.0, sustain=0.0, release=20.0, volume=0.45)
node("sfx", "Synths", "Oscillator", waveform=3, volume=0.45, ampAttack=1.0, ampDecay=60.0,
     ampSustain=0.7, ampRelease=260.0)
node("echo", "AudioEffects", "Delay", mix=0.18, feedback=30.0)
node("mix", "Utility", "Mixer", gain0=-6.0, gain1=-3.0, gain2=-2.0, gain3=-9.0, gain4=-15.0,
     gain5=-7.0, pan0=-0.15, pan4=0.3, pan5=0.1)
node("crush", "AudioEffects", "Bitcrush", bits=8.0, rate=16000.0, mix=0.7)
node("limit", "AudioEffects", "Limiter", threshold=-1.0)
node("speaker", "Utility", "Audio Out")

# camera: a slow sway (8 bars), a rise (8 bars), a push-in breath (4 bars), a drifting roll, a punch-in on every kick
node("sway", "Modulators", "LFO", rateBeats=32.0, seed=7.0)
node("rise", "Modulators", "LFO", rateBeats=32.0, phase=0.25, seed=8.0)
node("push", "Modulators", "LFO", rateBeats=16.0, phase=0.5, seed=9.0)
node("drift", "Modulators", "Random", rate=0.25, smooth=1.0, seed=11.0)
node("kickEar", "Modulators", "Audio Analyze", attack=0.02, release=0.18)
# a crane bob every bar
node("bob", "Modulators", "LFO", rateBeats=4.0, seed=13.0)
# PRESS START blinks on the beat
node("blinker", "Modulators", "LFO", shape=4, rateBeats=1.0, seed=12.0)

wires += [
    "cable screenmix input game", "cable screenmix input_2 hud",
    "cable screen 0 screenmix",
    "geo glass geo screen", "cable glass emission screenmix", "cable glass albedo screenmix",
    "cable sticker input stickerBack", "cable sticker input_2 stickerInk",
    "cable play 0 sticker", "geo playMat geo play", "cable playMat albedo sticker",
    "geo plasticMat geo plastic", "geo darkMat geo dark", "geo metalMat geo metal",
    "geo group geo_a body", "geo group geo_b bezel", "geo group geo_c glass",
    "geo group geo_d plasticMat", "geo group geo_e darkMat", "geo group geo_f metalMat",
    "geo group geo_g playMat", "geo group geo_h badge",
    "geo chrome geo title", "geo wallMat geo wall", "cable wallMat albedo poster", "cable wallMat emission poster",
    "cable body 0 shellTex",
    "cable blink input press", "cable comp input view", "cable comp input_2 blink",
    "cable grain input comp", "cable out 0 grain",
    "note lead notes leadRoll", "note bass notes bassRoll", "note kick notes kickRoll",
    "note snare notes snareRoll", "note hat notes hatRoll", "note sfx notes sfxRoll",
    "aud echo audio lead",
    "aud mix in_1 echo", "aud mix in_2 bass", "aud mix in_3 kick", "aud mix in_4 snare",
    "aud mix in_5 hat", "aud mix in_6 sfx",
    "aud crush audio mix", "aud limit audio crush", "aud speaker audio limit", "aud out audio limit",
    "aud kickEar audio kick",
    "mod cam azimuth sway 0 0 1 0.5 76 104",
    "mod cam elevation rise 0 0 1 0.5 2 12",
    "mod cam distance push 0 0 1 0.5 9.2 10.6",
    "mod cam roll drift 0 0 1 0.5 -2.5 2.5",
    "mod cam fov kickEar 0 0 1 0.5 28 26.8",
    "mod poster knobA kickEar 0 0 1 0.5 0 1",
    "mod cam targetY bob 0 0 1 0.5 0.5 0.6",
    "mod blink uOpacity blinker 0 0 1 0.5 0 1",
]


NOTES = [
    ("band Picture", "Pocket console, Picture band. Primitives and Sketch 3D build the device, a GLSL game plays on its screen, and the 3D View stages it in front of a Y2K poster wall."),
    ("band Sound", "Sound band. Six Piano Rolls (open one to edit its notes) play chiptune at 120 bpm: square lead, triangle bass, noise drums, jump and coin blips locked to the hero's hops."),
    ("band Modulation", "LFOs sway, raise and push the camera; a Random drifts the roll. The camera orbits the console, cranes on every bar and punches in on the kick, which also flashes the glyphs."),
    ("near view", "3D View: the console floats in front of the poster wall and casts a real shadow on it. Double-click to open the panel and move, light or reframe anything."),
    ("near title", "PLAY in chrome pixel cubes behind the console. Sweeps in, then a wave on every beat."),
    ("near poster", "The poster in one Formula: paper, a checkerboard band, lime glyph panels that re-roll every beat, old-OS windows. It textures the wall."),
    ("near shellTex", "Speckled plastic texture for the shell."),
    ("near body", "Rounded Cubes make the shell and the screen bezel."),
    ("near game", "The game is one Formula: a 160 x 120 side-scroller. The hero hops every second; the coin comes half a second later."),
    ("near screenmix", "Game plus HUD text, sent to the screen Material as albedo and emission."),
    ("near sticker", "Two Text layers make the Play sticker: white outline under pink ink. Its Material cuts the alpha."),
    ("near plastic", "Sketch 3D in code: buttons, clover, tag, SD card, cartridge and cable. The buttons press on the same clock as the game."),
    ("near dark", "Knob, speaker grille, screw slots and the smiley, which turns slowly."),
    ("near metal", "Screws and the knurled side wheel."),
    ("near key", "Soft area key from the upper right casts the shadow; cool fill, lime rim."),
    ("near sfxRoll", "32nd notes, two beats per hop: a rising jump arpeggio, then a two-note coin."),
    ("near mix", "Mixer into an 8-bit Bitcrush and a Limiter at -1 dB."),
]

def write(path):
    with open(path, "w") as f:
        f.write("infinite-patch 1\n# A pocket game console: 3D nodes, a GLSL game on its screen, chiptune sounds.\n")
        for i, (nid, cat, typ, lines) in enumerate(nodes, 1):
            f.write(f"node {i} {cat} {typ}\n  id {nid}\n")
            for l in lines:
                f.write(f"  {l}\n")
            f.write("end\n")
        i = len(nodes) + 1
        for hint, text in NOTES:
            w = 340 if hint.startswith("band") else 300
            h = max(110, 60 + 22 * (len(text) // 34 + 1))
            f.write(f"# {hint}\nnode {i} Compositing Comment\n  s text {text}\n  f width {w}\n  f height {h}\nend\n")
            i += 1
        for w in wires:
            f.write(w + "\n")
        f.write("transport 120 4 4 0 0\n")
        BARS = 64   # each lane is one clip, 64 bars long, looping its pattern
        for k, (nid, uid, notes, loop) in enumerate(LANES):
            f.write(f"stream 2 0 1 0 0 1 0 {nid[:-4].capitalize()}\nstreamid {k} {4001 + k}\n")
            f.write(f"cliptick {k} {5001 + k} 0 {BARS * 4 * PPQ} {uid} 0 0 0 0 1 0 0 0 0 {nid[:-4].capitalize()}\n")
            f.write(f"streamnote {k} {uid} 0\n")
            f.write(f"clipmidi {k} {5001 + k} {len(notes) + 1} 0 1 0 {loop} 0 1 0\n")
            for j, (st, ln, p, v) in enumerate(notes, 1):
                f.write(f"clipnote {k} {5001 + k} id={j} s={st} l={ln} p={p} v={v}\n")

import subprocess
here = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "handheld.inf")
write(out)
subprocess.check_call([sys.executable, os.path.join(here, "..", "..", "..", "tools", "patch-layout.py"), out, "--wide"])
