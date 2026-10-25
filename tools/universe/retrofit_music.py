#!/usr/bin/env python3
"""Apply the per-style chord / note-effect / sidechain / motif / groove / fill choices to tools/catalog/styles_music.json (idempotent: re-run after editing the table)."""
import json, os
P = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "catalog", "styles_music.json")
H = {"notefx": ["Humanizer"]}
CH = lambda size, beats, strum=20, human=8: {"size": size, "beats": beats, "strum": strum, "human": human}
T = {
    "acid_house": dict(motif=dict(synth=["Analog", "Wavetable"], density=0.8, rate=0.25, octave=1, gate=45, gain=-9), groove=0.4, fill=0.5),
    "tech_house": dict(sidechain=True, chords=CH([3], 2.0, 10, 6), notefx=["Humanizer"], gain=-12, motif=dict(synth=["Analog", "Field Synth"], density=0.35, octave=2, gate=40, gain=-12), groove=0.7),
    "french_filter_house": dict(sidechain=True, chords=CH([4], 4.0, 25, 8), notefx=["Humanizer"], gain=-8, motif=dict(synth=["Analog"], density=0.5, octave=2, gain=-12), groove=0.6),
    "progressive_house": dict(motif=dict(synth=["Analog", "Wavetable"], density=0.45, rate=0.5, octave=2, notefx=["Note Echo"], gain=-12), fill=0.7),
    "detroit_techno": dict(chords=CH([4], 8.0, 30, 6), notefx=["Humanizer", "Note Echo"], gain=-10, motif=dict(synth=["Analog", "Wavetable"], density=0.3, octave=2, gain=-12), groove=0.5),
    "minimal_techno": dict(motif=dict(synth=["Analog", "Field Synth"], density=0.25, octave=2, gate=30, gain=-11), groove=0.8, fill=0.3),
    "dub_techno": dict(chords=CH([3], 4.0, 30, 5), notefx=["Note Echo"], gain=-9, groove=0.3, fill=0.2),
    "industrial_techno": dict(trim=-7.0, motif=dict(synth=["Analog", "Wavetable"], density=0.4, octave=1, gate=70, gain=-12), groove=0.3, fill=0.6),
    "classic_trance": dict(sidechain=True, chords=CH([3, 4], 4.0, 10, 0), gain=-10, motif=dict(synth=["Analog", "Wavetable"], density=0.65, rate=0.25, octave=3, notefx=["Note Echo"], gain=-11), fill=0.9),
    "psytrance": dict(motif=dict(synth=["Analog", "Wavetable"], density=0.55, octave=3, gate=50, gain=-12), fill=0.6),
    "liquid_dnb": dict(chords=CH([4], 8.0, 35, 10), notefx=["Humanizer"], gain=-9, groove=0.7, fill=0.7),
    "neurofunk": dict(motif=dict(synth=["Analog", "Wavetable"], density=0.5, octave=1, gate=65, gain=-11), groove=0.4, fill=0.6),
    "jungle": dict(groove=0.9, fill=0.8),
    "uk_garage": dict(chords=CH([3, 4], 2.0, 15, 10), notefx=["Humanizer"], gain=-10, motif=dict(synth=["Analog", "Field Synth"], density=0.4, octave=2, gain=-12), groove=0.8),
    "dubstep": dict(motif=dict(synth=["Analog", "Wavetable"], density=0.3, rate=0.5, octave=1, gate=80, gain=-10), fill=0.4),
    "breakbeat": dict(motif=dict(synth=["Analog", "Field Synth"], density=0.4, octave=2, gain=-12), groove=0.8, fill=0.7),
    "electro": dict(motif=dict(synth=["Analog", "Wavetable"], density=0.5, octave=2, gate=40, gain=-11), groove=0.3),
    "dark_ambient": dict(chords=CH([3, 4], 8.0, 60, 12), notefx=["Humanizer", "Note Echo"], gain=3),
    "glitch": dict(motif=dict(synth=["Analog", "Field Synth"], density=0.6, octave=3, gate=25, notefx=["Note Strum"], gain=-12), groove=1.0, fill=0.8),
    "idm": dict(motif=dict(synth=["Analog", "Field Synth", "Wavetable"], density=0.55, octave=2, notefx=["Humanizer"], gain=-12), groove=1.0, fill=0.8),
    "downtempo": dict(chords=CH([4], 4.0, 30, 12), notefx=["Humanizer", "Velocity Curve"], gain=-9, motif=dict(synth=["Analog", "Field Synth"], density=0.35, rate=0.5, octave=2, notefx=["Note Echo"], gain=-12), groove=0.8),
    "synthwave": dict(sidechain=True, chords=CH([3, 4], 4.0, 5, 4), notefx=["Humanizer"], gain=-10, motif=dict(synth=["Analog", "Wavetable"], density=0.6, octave=3, gate=70, notefx=["Note Echo"], gain=-11)),
    "vaporwave": dict(chords=CH([4], 4.0, 40, 10), notefx=["Humanizer", "Note Echo"], gain=-9, groove=0.2, fill=0.1),
    "footwork": dict(motif=dict(synth=["Analog", "Field Synth"], density=0.5, octave=2, gate=35, gain=-12), groove=1.0, fill=0.6),
    "gabber": dict(trim=-13.0, motif=dict(synth=["Analog", "Wavetable"], density=0.45, octave=2, gate=70, gain=-11), fill=0.8),
    "deep_house": dict(motif=dict(synth=["Analog", "Field Synth"], density=0.3, octave=2, gate=50, gain=-13), groove=0.6),
    "amapiano": dict(groove=0.9, fill=0.5, motif=dict(synth=["Analog", "Field Synth"], density=0.35, octave=2, gain=-12)),
    "future_garage": dict(groove=0.6, fill=0.3),
    "lofi_beats": dict(groove=0.9, fill=0.4, motif=dict(synth=["Analog", "Field Synth"], density=0.3, rate=0.5, octave=2, gain=-13)),
}
d = json.load(open(P))
for s in d["styles"]:
    s.update(T.get(s["id"], {}))
json.dump(d, open(P, "w"), indent=1)
print("styles updated:", sum(1 for s in d["styles"] if s["id"] in T))
