#!/usr/bin/env python3
"""Generates assets/templates/*.inf, then lays each out with tools/patch-layout.py.
    python3 tools/templates/gen_templates.py
Edit the T(...) entries below, rerun, commit the .inf files. Each template is small, runs on its own
and explains itself with Comment nodes (one header per band, one note per node group)."""
import os, subprocess, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "assets", "templates")

def comment(text, near=None, band=None, w=300, h=None):
    h = h or max(110, 60 + 22 * (len(text) // 34 + 1))
    hint = f"# near {near}" if near else f"# band {band}"
    return f"{hint}\nnode {{n}} Compositing Comment\n  s text {text}\n  f width {w}\n  f height {h}\nend\n"

class T:
    all = []
    def __init__(s, slug, title, blurb, nodes, wires, notes, bands, extra=()):
        s.slug, s.title, s.blurb, s.nodes, s.wires, s.notes, s.bands, s.extra = slug, title, blurb, nodes, wires, notes, bands, extra
        T.all.append(s)
    def text(s):
        if any(n[0]=="speakers" for n in s.nodes) and not any(n[0]=="out" for n in s.nodes):
            last = s.wires[-1].split()[-1]
            s.nodes = s.nodes + [("ana","Modulators","Audio Analyze",[]), ("pic","Source","Shape",["c fillColor 1 0.45 0.3","f size 0.2"]), OUTN]
            s.wires = s.wires + [f"aud ana 0 {last}", "cable out 0 pic", "mod pic sizeX ana 0 0 1 0.5 0.12 0.4", "mod pic sizeY ana 0 0 1 0.5 0.12 0.4"]
            s.notes = s.notes + [("pic","A small picture that follows the sound, so you can see it as well as hear it.")]
        out = ["infinite-patch 1", f"# {s.title}: {s.blurb}"]
        i = 1
        for nid, cat, typ, params in s.nodes:
            out.append(f"node {i} {cat} {typ}\n  id {nid}")
            for p in params: out.append("  " + p)
            out.append("end"); i += 1
        for band, text in s.bands.items():
            out.append(comment(text, band=band, w=340, h=120).replace("{n}", str(i)).rstrip()); i += 1
        for nid, text in s.notes:
            out.append(comment(text, near=nid).replace("{n}", str(i)).rstrip()); i += 1
        out += s.wires + list(s.extra)
        return "\n".join(out) + "\n"

OUTN = ("out", "Utility", "Output", [])
SPK = ("speakers", "Utility", "Audio Out", [])
def src_shape(i="shape", extra=()): return (i, "Source", "Shape", ["c fillColor 1 0.45 0.3"] + list(extra))

# ---------------- first patches ----------------
T("first-sound", "First sound", "press play and hear notes",
  [("notes","Notes","Random Note Generator",[]), ("synth","Synths","Analog",["f volume 0.5"]), SPK],
  ["note synth 0 notes", "aud speakers 0 synth"],
  [("notes","The Notes box plays a new random note every quarter beat. Try changing Rate, or the Low and High note range."),
   ("synth","The Analog synth turns each note into a sound. Try turning Volume down or changing the Wave of oscillator 1."),
   ("speakers","Audio Out sends the sound to your speakers. Every sound needs to reach one of these.")],
  {"Sound": "First sound. Notes go into a synth, the synth goes to the speakers. Follow the wires left to right."})

T("first-image", "First image", "a shape on a colour wash",
  [("wash","Source","Ramp",["c stopColor0 0.05 0.1 0.35","c stopColor1 0.55 0.2 0.6"]), src_shape("shape",["f size 0.3"]), ("mix","Compositing","Layer Stack",[]), OUTN],
  ["cable mix 0 wash", "cable mix 1 shape", "cable out 0 mix"],
  [("wash","A Ramp makes a smooth blend between colours. Try changing Angle or the stop colours."),
   ("shape","A Shape draws a simple form. Try changing Size, Rotation or the Shape Type."),
   ("mix","Layer Stack puts one picture on top of another. The top input sits over the bottom one."),
   ("out","Output is what you see in the big preview and what gets exported.")],
  {"Picture": "First image. Two pictures are stacked, then shown. Click a node and change any slider."})

T("first-music-image", "Moves with the music", "the shape grows with the sound",
  [("notes","Notes","Random Note Generator",[]), ("synth","Synths","Analog",["f volume 0.5"]), SPK,
   ("hear","Modulators","Audio Analyze",[]), src_shape("shape"),
   ("glow","Effects","bloom",[]), OUTN],
  ["note synth 0 notes", "aud speakers 0 synth", "aud hear 0 synth", "cable glow 0 shape", "cable out 0 glow",
   "mod shape sizeX hear 0 0 1 0.5 0.12 0.4", "mod shape sizeY hear 0 0 1 0.5 0.12 0.4"],
  [("hear","Audio Analyze listens to the sound and turns it into moving numbers (level, low, mid, high). Try a different output as the source."),
   ("shape","The orange dots on Size X and Size Y means it is driven by the music. Change its Depth to make it move more or less."),
   ("glow","Bloom adds a glow to bright parts. Try raising Intensity."),
   ("synth","This synth is the sound that the picture follows.")],
  {"Picture": "A picture that moves with the music.", "Sound": "The sound. Notes, synth, speakers."})

# ---------------- by category ----------------
T("source", "Source", "pictures from nothing",
  [("noise","Source","Noise",["f scale 4","c lowColor 0.05 0.1 0.3"]), ("ring","Source","Ring",["f size 0.3","c fillColor 1 0.75 0.3"]), ("mix","Compositing","Blend",[]), OUTN],
  ["cable mix 0 noise", "cable mix 1 ring", "cable out 0 mix"],
  [("noise","Noise makes cloudy texture. Try Scale for bigger or smaller clouds, and Speed for how fast it drifts."),
   ("ring","Source nodes need no input. Circle, Star, Text, Ramp and the rest all work like this one. Try Size."),
   ("mix","Blend puts two pictures together.")],
  {"Picture": "Sources make a picture out of nothing. Add any Source node and wire it to Output."})

T("compositing", "Compositing", "layers, trails and feedback",
  [src_shape("shape",["f size 0.18","f posX 0.3"]), ("tr","Compositing","Trails",[]), ("lay","Compositing","Layer Stack",[]),
   ("bg","Source","Ramp",["c stopColor0 0.05 0.1 0.35","c stopColor1 0.55 0.2 0.6"]), OUTN],
  ["cable tr 0 shape", "cable lay 0 bg", "cable lay 1 tr", "cable out 0 lay",
   "mod shape posY lfo 0 0 0.3 0.5", "mod shape posX lfo2 0 0 0.3 0.5"],
  [("tr","Trails leaves a fading copy of what came before. Try Decay: higher is longer trails."),
   ("lay","Layer Stack puts pictures on top of each other. Its inputs go bottom to top."),
   ("bg","This colour wash is the bottom layer.")],
  {"Picture": "Compositing combines and reshapes pictures: stack them, fade them, trail them."},
  extra=["node 90 Modulators LFO\n  id lfo\n  f rateBeats 5\nend", "node 91 Modulators LFO\n  id lfo2\n  f rateBeats 3\nend"])

T("effects", "Effects", "change a picture",
  [("noise","Source","Noise",[]), ("kal","Effects","kaleidoscope",[]), ("glow","Effects","bloom",[]), ("cr","Compositing","Color Ramp",[]), OUTN],
  ["cable cr 0 noise", "cable kal 0 cr", "cable glow 0 kal", "cable out 0 glow"],
  [("cr","Color Ramp paints the picture using a few colours. Click a colour stop to change it."),
   ("kal","Kaleidoscope mirrors the picture into segments. Try Segments and Rotation."),
   ("glow","Bloom adds a soft glow. Effects chain left to right; try removing one to see what it did.")],
  {"Picture": "Effects take a picture in and give a changed one out. Chain as many as you like."})

T("3d", "3D", "a shape seen from any side",
  [("knot","3D","Torus Knot",[]), ("cam","3D","Camera",["f orbitPerBeat 0.2"]), ("sun","3D","Light",[]), ("view","3D","Render 3D",[]), OUTN],
  ["geo view 0 knot", "geo view camera cam", "geo view light_1 sun", "cable out 0 view"],
  [("knot","A 3D shape. Try Knot P and Knot Q for different twists, or swap it for a Sphere or Cube."),
   ("cam","The Camera decides the view. Orbit Per Beat makes it circle the shape."),
   ("sun","The Light shades the shape. Try moving its Azimuth."),
   ("view","Render 3D turns shapes, camera and light into a picture.")],
  {"Picture": "3D patches have four parts: a shape, a camera, a light and Render 3D to draw them."})

T("modulators", "Modulators", "numbers that move other controls",
  [src_shape("shape"), ("lfo","Modulators","LFO",["f rateBeats 4"]), ("rnd","Modulators","Random",[]), OUTN],
  ["cable out 0 shape", "mod shape sizeX lfo 0 0 0.25 0.5", "mod shape rotation rnd 0 0 90 0.5"],
  [("lfo","An LFO is a steady wave. Here it makes the shape breathe. Try Rate, or a different wave shape."),
   ("rnd","Random wanders between values. Here it turns the shape. Try Smooth for gentler movement."),
   ("shape","Controls with an orange dot are being moved. Drag a modulator onto any control to do the same.")],
  {"Modulation": "Modulators produce numbers over time. Wire them to controls to make anything move."})

T("macros", "Macros", "one knob that moves several things",
  [src_shape("shape"), ("knob","Macros","Macro Knob",["s label Energy"]), OUTN],
  ["cable out 0 shape", "mod shape sizeX knob 0 0 0.3 0.3", "mod shape rotation knob 0 0 180 0"],
  [("knob","A Macro Knob is a control you make yourself. Turn it, and everything wired to it moves together. Rename it with Label."),
   ("shape","Both Size X and Rotation follow the one knob. Add more wires to give it more to do.")],
  {"Modulation": "Macros are knobs, sliders and buttons you can play live."})

T("notes", "Notes", "notes that make patterns",
  [("seq","Notes","Note Sequencer",[]), ("arp","Notes","Arpeggiator",[]), ("synth","Synths","Analog",[]), SPK],
  ["note arp 0 seq", "note synth 0 arp", "aud speakers 0 synth"],
  [("seq","The Sequencer plays a row of steps. Click steps to turn them on, and drag them to change notes."),
   ("arp","The Arpeggiator turns each note into a run of notes. Try Mode and Octaves."),
   ("synth","Any synth can play these notes.")],
  {"Sound": "Notes nodes make and change musical notes. Wire them into a synth to hear them."})

T("synths", "Synths", "sounds from notes",
  [("notes","Notes","Random Note Generator",["f rateBeats 0.5"]), ("synth","Synths","Wavetable",[]), SPK],
  ["note synth 0 notes", "aud speakers 0 synth"],
  [("notes","Plays a random note twice a beat."),
   ("synth","The Wavetable synth morphs between wave shapes. Try Position to change the tone, and the Filter."),
   ("speakers","Sound reaches your speakers here.")],
  {"Sound": "Synths turn notes into sound. Analog, Wavetable, Sampler and others all take notes in."})

T("audio-effects", "Audio Effects", "shape the sound",
  [("notes","Notes","Random Note Generator",[]), ("synth","Synths","Analog",[]), ("echo","AudioEffects","Delay",[]),
   ("room","AudioEffects","Reverb",[]), SPK],
  ["note synth 0 notes", "aud echo 0 synth", "aud room 0 echo", "aud speakers 0 room"],
  [("echo","Delay repeats the sound. Try Feedback for more repeats and Mix for how loud they are."),
   ("room","Reverb makes the sound feel like it is in a room. Try Size and Decay."),
   ("synth","Effects sit between the sound and the speakers, in order.")],
  {"Sound": "Audio effects change a sound. Wire them in a row between the synth and the speakers."})

T("prediction", "Prediction", "notes that keep going on their own",
  [("pred","Prediction","Predictive Notes",["i sourceMode 1"]), ("synth","Synths","Analog",["f volume 0.5"]), SPK],
  ["note synth 0 pred", "aud speakers 0 synth"],
  [("pred","Predictive Notes can play on its own (Movement mode, as here). Switch Source to Notes, wire a note chain in and press Learn, and it learns your pattern and keeps playing in your style. Stray sets how far it wanders."),
   ("synth","Plays what Prediction makes.")],
  {"Sound": "Green nodes learn from what you play and carry on in your style."})

T("utility", "Utility", "mix and meter sound",
  [("a","Notes","Random Note Generator",[]), ("b","Notes","Random Note Generator",["i rangeLow 36","i rangeHigh 48"]),
   ("s1","Synths","Analog",["f volume 0.4"]), ("s2","Synths","Analog",["f volume 0.4"]), ("mixer","Utility","Mixer",[]),
   ("meter","Utility","Audio Meter",[]), SPK],
  ["note s1 0 a", "note s2 0 b", "aud mixer in_1 s1", "aud mixer in_2 s2", "aud meter 0 mixer", "aud speakers 0 meter"],
  [("mixer","The Mixer blends several sounds. Each channel has a volume, a pan and mute/solo."),
   ("meter","The Meter shows how loud the sound is. It passes sound through unchanged."),
   ("b","The second voice plays lower notes.")],
  {"Sound": "Utility nodes route and check things: mixers, meters, outputs."})

T("field", "Field", "write a picture as a formula",
  [("pix","Source","FieldPixel",["s code col = vec3(0.5 + 0.5 * sin(uv.x * 10.0 + t), 0.5 + 0.5 * sin(uv.y * 10.0 - t), 0.6);"]), OUTN],
  ["cable out 0 pix"],
  [("pix","Field draws each pixel from a formula. uv is the position, t is time. Double-click to open the editor and change the 10.0 values.")],
  {"Picture": "Field lets you write your own picture, sound or shape in a few lines."})

def main():
    os.makedirs(OUT, exist_ok=True)
    for t in T.all:
        p = os.path.join(OUT, t.slug + ".inf")
        open(p, "w").write(t.text())
        subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "patch-layout.py"), p])
    first = {"first-sound", "first-image", "first-music-image"}
    with open(os.path.join(OUT, "index.txt"), "w") as f:
        f.write("# slug|group|title|one line. Written by tools/templates/gen_templates.py; read by the Templates window.\n")
        for t in T.all:
            f.write(f"{t.slug}|{'First patches' if t.slug in first else 'By node family'}|{t.title}|{t.blurb}\n")
    print(len(T.all), "templates")
main()
