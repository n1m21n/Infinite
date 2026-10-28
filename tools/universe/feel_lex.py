"""Shared lexicons and rules for the feel dataset (feel_dataset.py). Everything here is grade C (inference from the literature in
docs/plans/data-universe/feel/*.md) until the weights are fitted on our own labelled data (C5). Nothing here is a measurement."""

# Mood word -> (valence V, energy arousal E, tension arousal T), each -1..1. Russell circumplex placement refined with the
# Eerola 2012 split of arousal (energy vs tension): calm-tense words sit low E / high T (Eerie, Ominous), excited words high E.
MOOD = {
    "Dreamlike": (.35, -.55, -.10), "Hopeful": (.60, .25, -.10), "Spacey": (.10, -.45, -.05), "Meditative": (.30, -.80, -.45),
    "Floating": (.40, -.60, -.30), "Longing": (-.25, -.35, .15), "Peaceful": (.65, -.75, -.60), "Mysterious": (-.10, -.30, .50),
    "Tense": (-.45, .15, .85), "Angry": (-.70, .80, .75), "Dramatic": (-.10, .55, .60), "Intense": (-.15, .80, .70),
    "Powerful": (.30, .85, .35), "Urgent": (-.20, .75, .75), "Distant": (-.10, -.50, .05), "Euphoric": (.90, .85, -.20),
    "Majestic": (.60, .50, .20), "Energetic": (.60, .90, .10), "Light": (.55, .10, -.40), "Upbeat": (.75, .65, -.20),
    "Childlike": (.60, .30, -.45), "Suspenseful": (-.35, .15, .90), "Nostalgic": (.15, -.30, -.05), "Fun": (.80, .65, -.30),
    "Joyful": (.90, .65, -.35), "Mystical": (.20, -.25, .25), "Dark": (-.60, -.10, .45), "Disturbing": (-.75, .20, .85),
    "Strange": (-.15, .00, .55), "Triumphant": (.80, .75, .10), "Ominous": (-.65, -.05, .75), "Eerie": (-.50, -.25, .65),
    "Nightmarish": (-.85, .30, .90), "Nervous": (-.40, .45, .70), "Robotic": (-.20, .25, .30), "Silly": (.60, .45, -.40),
    "Party": (.80, .85, -.15), "Mournful": (-.75, -.55, -.05), "Somber": (-.60, -.60, .05), "Primal": (-.10, .75, .45),
    "Scary": (-.70, .35, .85), "Funky": (.65, .70, -.05), "Deep": (-.05, -.35, .15), "Magical": (.65, -.05, -.10),
    "Cinematic": (.10, .40, .30), "Ambient": (.20, -.70, -.35), "Warm": (.55, -.30, -.50), "Airy": (.50, -.35, -.40),
    "Beautiful": (.75, -.25, -.30),
    # extra words found in chord-progression file names, EMOPIA, game-music labels and character tags
    "Relaxed": (.55, -.65, -.65), "Playful": (.75, .50, -.35), "Joy": (.90, .65, -.35), "Sad": (-.75, -.60, -.05),
    "Happy": (.85, .55, -.35), "Calm": (.50, -.80, -.70), "Serene": (.60, -.80, -.65), "Tranquil": (.55, -.80, -.70),
    "Melancholic": (-.55, -.55, -.05), "Melancholy": (-.55, -.55, -.05), "Wistful": (-.15, -.45, -.10), "Bittersweet": (-.05, -.35, .00),
    "Romantic": (.60, -.20, -.30), "Tender": (.55, -.55, -.55), "Gentle": (.50, -.65, -.60), "Soothing": (.55, -.75, -.70),
    "Sorrowful": (-.80, -.55, .00), "Anxious": (-.55, .45, .80), "Fearful": (-.70, .40, .80), "Aggressive": (-.60, .85, .70),
    "Epic": (.40, .85, .40), "Heroic": (.70, .75, .20), "Adventurous": (.65, .60, .10), "Whimsical": (.60, .30, -.30),
    "Magic": (.65, -.05, -.10), "Mystery": (-.10, -.30, .50), "Suspense": (-.35, .15, .90), "Tension": (-.45, .15, .85),
    "Bright": (.65, .45, -.15), "Cheerful": (.85, .55, -.40), "Optimistic": (.75, .40, -.25), "Uplifting": (.80, .50, -.20),
    "Inspiring": (.75, .45, -.10), "Confident": (.60, .55, .00), "Determined": (.20, .60, .30), "Serious": (-.20, .10, .35),
    "Contemplative": (.10, -.65, -.25), "Reflective": (.05, -.55, -.20), "Pensive": (-.15, -.55, -.05), "Dreamy": (.35, -.55, -.10),
    "Haunting": (-.55, -.20, .70), "Haunted": (-.55, -.20, .70), "Creepy": (-.65, .00, .80), "Sinister": (-.75, .10, .80),
    "Brooding": (-.45, -.30, .45), "Gloomy": (-.65, -.50, .10), "Grim": (-.70, -.10, .45), "Bleak": (-.70, -.45, .15),
    "Lonely": (-.55, -.50, .00), "Yearning": (-.20, -.30, .20), "Hopeless": (-.80, -.55, .00), "Dejected": (-.70, -.55, -.05),
    "Triumph": (.80, .75, .10), "Victory": (.85, .75, .00), "Glorious": (.80, .65, .05), "Grand": (.55, .55, .25),
    "Exciting": (.70, .85, .15), "Thrilling": (.50, .85, .40), "Driving": (.15, .80, .40), "Frantic": (-.10, .90, .80),
    "Chaotic": (-.35, .85, .85), "Wild": (.10, .85, .50), "Fierce": (-.30, .85, .65), "Brutal": (-.65, .85, .70),
    "Playfulness": (.75, .50, -.35), "Humorous": (.70, .40, -.35), "Quirky": (.35, .35, .20), "Eccentric": (.20, .40, .35),
    "Sentimental": (.25, -.45, -.15), "Touching": (.35, -.40, -.20), "Emotional": (.10, -.10, .10), "Passionate": (.35, .60, .35),
    "Sensual": (.40, -.10, -.20), "Intimate": (.45, -.50, -.40), "Cozy": (.65, -.55, -.65), "Comforting": (.65, -.60, -.65),
    "Hypnotic": (.10, -.20, .15), "Trance": (.30, .20, .05), "Ethereal": (.40, -.55, -.20), "Celestial": (.60, -.30, -.20),
    "Divine": (.70, -.30, -.20), "Sacred": (.45, -.50, -.30), "Spiritual": (.40, -.55, -.35), "Solemn": (-.25, -.55, .10),
    "Triumphant ": (.80, .75, .10), "Noble": (.55, .15, .00), "Proud": (.65, .40, .05), "Stately": (.40, .00, .00),
    "Neutral": (.00, .00, .00), "Mellow": (.40, -.55, -.50), "Smooth": (.50, -.35, -.45), "Chill": (.45, -.60, -.55),
    "Groovy": (.65, .55, -.15), "Danceable": (.70, .75, -.10), "Bouncy": (.70, .60, -.30), "Lively": (.75, .70, -.15),
    "Rhythmic": (.20, .55, .10), "Pulsing": (.10, .60, .30), "Moving": (.20, .35, .10), "Dirty": (-.40, .50, .55),
    "Aggressive ": (-.60, .85, .70), "Phat": (.25, .55, .00), "Lofi": (.10, -.30, -.10), "Glitchy": (-.25, .50, .60),
    "Clean": (.45, .00, -.30), "Spacious": (.45, -.35, -.25), "Soft": (.50, -.55, -.55), "Thin": (-.20, -.15, .20),
    "Synthetic": (.00, .15, .15), "Harsh": (-.65, .55, .75), "Sweet": (.75, -.20, -.50), "Rich": (.55, .00, -.15),
    "Rebellious": (-.15, .75, .55), "Surprised": (.30, .55, .45), "Excited": (.75, .85, .05), "Empowered": (.65, .70, .15), "Anguished": (-.85, .35, .70),
    "Hollow": (-.35, -.35, .30), "Cold": (-.45, -.25, .30), "Hot": (.25, .65, .35), "Lush": (.65, -.20, -.35),
}

# Axis -> contribution to V, E, T (grade C). Signs follow Eerola 2013 / Friberg 2006 / Juslin 2003; magnitudes are placeholders to be
# replaced by the fit in C5. Axis values are 0..1 (0.5 = neutral).
AX_VET = {
    "loudness":   (0.00, 0.30, 0.10), "brightness": (0.20, 0.25, 0.10), "sharpness": (-0.20, 0.20, 0.30), "warmth": (0.30, -0.10, -0.20),
    "weight":     (-0.05, 0.15, 0.00), "fullness": (0.15, 0.10, -0.05), "noisiness": (-0.25, 0.15, 0.30), "roughness": (-0.30, 0.10, 0.40),
    "attack":     (0.00, 0.30, 0.15), "decay": (0.15, -0.20, -0.15), "movement": (0.05, 0.20, 0.10), "width": (0.15, 0.00, -0.10),
    "pitch":      (0.15, 0.10, 0.10), "spread": (0.05, 0.05, 0.10), "tension": (-0.30, 0.10, 0.60), "harmonic_rhythm": (0.00, 0.15, 0.10),
    "density":    (0.00, 0.30, 0.15), "contour": (0.10, 0.10, 0.05),
}
AXES = list(AX_VET)


def vet_from_axes(ax):
    """Linear rule (grade C): sum of weight * (axis - 0.5), scaled x2 and clipped to -1..1."""
    out = []
    for k in range(3):
        s = sum(AX_VET[a][k] * (ax.get(a, 0.5) - 0.5) for a in AX_VET if ax.get(a) is not None)
        out.append(max(-1.0, min(1.0, 2.0 * s)))
    return tuple(round(x, 3) for x in out)


def axes_from_vet(v, e, t):
    """Inverse rule (grade C): the axis targets a mood implies. Only the axes with a literature link move; the rest stay 0.5."""
    c = lambda x: round(max(0.0, min(1.0, x)), 3)
    return {"loudness": c(.5 + .35 * e), "brightness": c(.5 + .20 * v + .20 * e), "sharpness": c(.5 + .25 * t + .10 * e - .10 * v),
            "warmth": c(.5 + .25 * v - .15 * e - .10 * t), "weight": c(.5 + .10 * e), "noisiness": c(.5 + .25 * t - .10 * v),
            "roughness": c(.5 + .35 * t - .15 * v), "attack": c(.5 + .30 * e + .10 * t), "decay": c(.5 - .20 * e + .10 * v),
            "movement": c(.5 + .25 * e), "density": c(.5 + .30 * e + .10 * t), "tension": c(.5 + .45 * t - .20 * v),
            "width": c(.5 + .15 * v), "harmonic_rhythm": c(.5 + .20 * e)}


def nearest_moods(v, e, t, k=3):
    names = [m for m in MOOD if not m.endswith(" ")]
    d = sorted(names, key=lambda m: (MOOD[m][0] - v) ** 2 + (MOOD[m][1] - e) ** 2 + (MOOD[m][2] - t) ** 2)
    return d[:k]


def words_vet(words):
    hit = [MOOD[w] for w in words if w in MOOD]
    if not hit: return None
    return tuple(round(sum(h[i] for h in hit) / len(hit), 3) for i in range(3))


# ---- instrument families: axis prior (0..1), role, mix slot, best models. Grade C (conventional practice + tables in feel/B).
# fields: bright warm weight attack decay noisy loud dens | role, band, level_dB(re mix), pan, send_dB, hpf_Hz, ratio | engine, note part, register, rhythm, vel
FAM = {
    "bass":     dict(ax=dict(brightness=.25, warmth=.70, weight=.90, attack=.55, decay=.50, noisiness=.15, loudness=.70, density=.45, pitch=.10),
                     role="bass", band="sub+bass 40-250 Hz, definition 700-1.5k", level=-6, pan="C", send=-30, hpf=30, ratio="3:1 slow attack",
                     engine="Analog or Oscillator (sub + saw)", part="bass_seq", reg="MIDI 28-55 (E1-G3)", rhythm="root-led, 8th/16th, locked to kick", vel="70-110"),
    "drums":    dict(ax=dict(brightness=.60, warmth=.35, weight=.45, attack=.95, decay=.15, noisiness=.55, loudness=.80, density=.60, pitch=.40),
                     role="kick/snare/hat/perc (per hit)", band="kick 60-120+click 2-5k; snare 200-400+4k crack; hats 6-16k", level=-8, pan="C (kick/snare), hats +-20..60", send=-14, hpf=30,
                     ratio="4:1 fast attack (kick/snare), none (hats)", engine="Oscillator (kick), Metallic (hat/cymbal), Analog/noise (snare); or Sampler", part="drum_groove",
                     reg="GM drum map 35-81", rhythm="grid 16ths, swing by genre, backbeat on 2 and 4", vel="60-127, hats 40-100"),
    "synth":    dict(ax=dict(brightness=.65, warmth=.45, weight=.45, attack=.55, decay=.55, noisiness=.30, loudness=.65, density=.55, pitch=.50),
                     role="lead/pad/pluck (by envelope)", band="body 200-2k, presence 4-8k", level=-5, pan="C (lead), wide (pad)", send=-10, hpf=120, ratio="2:1",
                     engine="Wavetable or Analog; Oscillator for simple leads", part="melody_contour or chord_prog", reg="MIDI 48-84", rhythm="phrase-based, 1-2 bar motifs", vel="60-110"),
    "texture":  dict(ax=dict(brightness=.45, warmth=.50, weight=.30, attack=.20, decay=.90, noisiness=.60, loudness=.35, density=.40, pitch=.45, movement=.75, width=.85),
                     role="texture/FX bed", band="8k+ air, low presence", level=-12, pan="wide", send=-4, hpf=200, ratio="none",
                     engine="Wavetable (evolving) or String; noise sources", part="chord_prog (sustained) or none", reg="any, often MIDI 36-96", rhythm="sustained, very slow changes", vel="40-90"),
    "choir":    dict(ax=dict(brightness=.45, warmth=.75, weight=.45, attack=.25, decay=.75, noisiness=.20, loudness=.55, density=.50, pitch=.50, width=.70),
                     role="pad/vocal bed", band="200-2k body, formants 500-3k", level=-8, pan="wide", send=-8, hpf=120, ratio="2:1 slow",
                     engine="Wavetable (vowel) or Sampler", part="chord_prog", reg="MIDI 48-84 (SATB spread)", rhythm="long chords, 1-4 bars", vel="50-95"),
    "piano":    dict(ax=dict(brightness=.55, warmth=.60, weight=.50, attack=.85, decay=.55, noisiness=.12, loudness=.60, density=.60, pitch=.50),
                     role="harmonic comping / lead", band="body 200-2k, attack 3-5k", level=-6, pan="C or slightly wide", send=-14, hpf=60, ratio="2:1",
                     engine="Sampler (unmeasured) or FM (EP) / String", part="chord_prog + melody_contour", reg="MIDI 36-96", rhythm="comping 8ths, arpeggios, held chords", vel="40-115 wide range"),
    "strings":  dict(ax=dict(brightness=.50, warmth=.70, weight=.45, attack=.35, decay=.70, noisiness=.18, loudness=.55, density=.55, pitch=.50, width=.70),
                     role="pad/ensemble swell", band="200-3k", level=-8, pan="wide", send=-8, hpf=100, ratio="2:1 slow",
                     engine="String or Wavetable", part="chord_prog + counter-melody", reg="MIDI 36-96", rhythm="long notes, swells, tremolo", vel="40-110"),
    "mallets":  dict(ax=dict(brightness=.60, warmth=.45, weight=.30, attack=.95, decay=.45, noisiness=.12, loudness=.45, density=.40, pitch=.65),
                     role="pluck / ostinato", band="500-5k", level=-9, pan="C or offset", send=-10, hpf=150, ratio="none",
                     engine="FM or Metallic", part="melody_contour (ostinato)", reg="MIDI 60-96", rhythm="16th ostinato or sparse hits", vel="50-100"),
    "guitar":   dict(ax=dict(brightness=.55, warmth=.60, weight=.40, attack=.85, decay=.50, noisiness=.18, loudness=.55, density=.55, pitch=.40),
                     role="rhythm / pluck / lead", band="body 200-1k, pick 3-5k", level=-7, pan="+-30 (double L/R)", send=-14, hpf=80, ratio="3:1",
                     engine="String (Karplus-Strong)", part="chord_prog strum or melody_contour", reg="MIDI 40-84", rhythm="strum 8ths/16ths, arpeggio", vel="50-110"),
    "keys":     dict(ax=dict(brightness=.50, warmth=.60, weight=.45, attack=.75, decay=.55, noisiness=.15, loudness=.55, density=.55, pitch=.50),
                     role="comping", band="200-2k", level=-7, pan="C or +-20", send=-12, hpf=80, ratio="2:1",
                     engine="FM (EP) / Wavetable / Sampler", part="chord_prog", reg="MIDI 40-88", rhythm="offbeat stabs, held voicings", vel="50-105"),
    "woodwind": dict(ax=dict(brightness=.50, warmth=.65, weight=.35, attack=.45, decay=.55, noisiness=.30, loudness=.50, density=.40, pitch=.60),
                     role="lead / counter-line", band="400-4k", level=-6, pan="off-centre", send=-10, hpf=150, ratio="2:1",
                     engine="String or FM (breath noise from noise source)", part="melody_contour", reg="MIDI 55-96", rhythm="legato phrases with breaths (rests)", vel="45-100"),
    "brass":    dict(ax=dict(brightness=.65, warmth=.55, weight=.55, attack=.60, decay=.45, noisiness=.25, loudness=.80, density=.45, pitch=.45),
                     role="stab / lead / fanfare", band="300-3k, bright 3-6k", level=-5, pan="off-centre / section wide", send=-10, hpf=100, ratio="3:1",
                     engine="Analog (saw + filter env)", part="melody_contour or chord stabs", reg="MIDI 40-84", rhythm="stabs on offbeats, held fanfares", vel="70-120"),
    "world":    dict(ax=dict(brightness=.55, warmth=.55, weight=.35, attack=.70, decay=.50, noisiness=.30, loudness=.50, density=.50, pitch=.50),
                     role="colour / lead", band="300-4k", level=-8, pan="off-centre", send=-10, hpf=120, ratio="2:1",
                     engine="String or FM; Sampler for exact timbre", part="melody_contour", reg="instrument-specific", rhythm="modal figures, ornament", vel="50-105"),
    "other":    dict(ax=dict(brightness=.50, warmth=.50, weight=.40, attack=.55, decay=.50, noisiness=.30, loudness=.50, density=.50, pitch=.50),
                     role="unspecified (assign after render)", band="unknown", level=-9, pan="C", send=-12, hpf=100, ratio="2:1",
                     engine="Sampler / classify first", part="chord_prog or melody_contour after classification", reg="measure", rhythm="measure", vel="50-105"),
}

# name word -> axis nudge (added to the family prior, grade C)
WORD = {
    "dark": dict(brightness=-.25, warmth=.10), "bright": dict(brightness=.25), "soft": dict(attack=-.25, loudness=-.15, sharpness=-.15),
    "hard": dict(attack=.20, sharpness=.20), "warm": dict(warmth=.25, brightness=-.10), "cold": dict(warmth=-.25, brightness=.10),
    "pad": dict(attack=-.30, decay=.30, movement=.15), "pluck": dict(attack=.30, decay=-.30), "lead": dict(brightness=.15, loudness=.10),
    "bass": dict(weight=.25, brightness=-.20), "sub": dict(weight=.30, brightness=-.30), "dirty": dict(noisiness=.25, roughness=.25),
    "distort": dict(noisiness=.25, roughness=.30, sharpness=.20), "drone": dict(decay=.30, attack=-.20, movement=-.10),
    "ambient": dict(decay=.25, attack=-.20, loudness=-.10), "atmos": dict(decay=.25, attack=-.20, width=.20), "noise": dict(noisiness=.35),
    "hat": dict(brightness=.30, weight=-.30, decay=-.25), "kick": dict(weight=.30, brightness=-.15), "snare": dict(noisiness=.20, brightness=.10),
    "cymbal": dict(brightness=.30, decay=.15, noisiness=.30), "tom": dict(weight=.15), "clap": dict(noisiness=.25, attack=.15),
    "bell": dict(brightness=.20, decay=.20), "organ": dict(attack=-.10, decay=.25), "choir": dict(warmth=.15, width=.20),
    "sweep": dict(movement=.35), "riser": dict(movement=.30, tension=.30), "fx": dict(noisiness=.15), "stab": dict(attack=.25, decay=-.20),
    "arp": dict(density=.30, movement=.20), "seq": dict(density=.25, movement=.20), "slow": dict(attack=-.15, movement=-.10),
    "fast": dict(attack=.15, movement=.15), "wide": dict(width=.30), "mono": dict(width=-.30), "thin": dict(weight=-.25, fullness=-.25),
    "fat": dict(weight=.20, fullness=.30), "phat": dict(weight=.20, fullness=.30), "huge": dict(fullness=.30, loudness=.15),
    "tiny": dict(fullness=-.25, loudness=-.15), "lofi": dict(brightness=-.20, noisiness=.20), "lo-fi": dict(brightness=-.20, noisiness=.20),
    "glitch": dict(noisiness=.20, movement=.25, roughness=.20), "808": dict(weight=.30, decay=.15), "909": dict(attack=.15),
    "strings": dict(warmth=.10), "violin": dict(brightness=.10), "cello": dict(weight=.10, warmth=.15), "flute": dict(brightness=.15, noisiness=.10),
    "trumpet": dict(brightness=.20, loudness=.15), "horn": dict(warmth=.10), "tuba": dict(weight=.25), "harp": dict(attack=.15, decay=.15),
    "vocal": dict(warmth=.10), "voice": dict(warmth=.10), "choral": dict(warmth=.15), "whisper": dict(noisiness=.25, loudness=-.25),
}

# preset character tag -> axis nudge
CHARACTER = {
    "Bright": dict(brightness=.25, sharpness=.10), "Dark": dict(brightness=-.25, warmth=.10), "Warm": dict(warmth=.25, brightness=-.10),
    "Clean": dict(noisiness=-.20, roughness=-.20), "Dirty": dict(noisiness=.25, roughness=.25), "Aggressive": dict(attack=.20, roughness=.25, loudness=.15, sharpness=.20),
    "Moving": dict(movement=.30), "Phat": dict(weight=.20, fullness=.30), "Thin": dict(weight=-.25, fullness=-.25), "Soft": dict(attack=-.20, loudness=-.20, sharpness=-.15),
    "Spacious": dict(width=.25, decay=.25), "Lofi": dict(brightness=-.20, noisiness=.20), "Glitchy": dict(noisiness=.15, movement=.25, roughness=.15),
    "Synthetic": dict(), "Digital": dict(brightness=.10, noisiness=.05), "Monophonic": dict(density=-.15, width=-.10), "Tempo-synced": dict(movement=.20, harmonic_rhythm=.10),
    "Evolving": dict(movement=.30, decay=.15), "Percussive": dict(attack=.30, decay=-.20), "Organic": dict(noisiness=.10, warmth=.10),
    "Acoustic": dict(noisiness=.05, warmth=.10), "Analog": dict(warmth=.15), "Ambient": dict(decay=.25, attack=-.20), "Cold": dict(warmth=-.20),
    "Distorted": dict(noisiness=.25, roughness=.30, sharpness=.15), "Polyphonic": dict(density=.15, fullness=.10), "Sequence": dict(density=.20, movement=.15),
}


def nudge(base, *dicts):
    ax = dict(base)
    for d in dicts:
        for k, dv in d.items():
            ax[k] = max(0.0, min(1.0, ax.get(k, 0.5) + dv))
    return ax


def fmt_ax(ax, grade=None):
    return ";".join("%s=%.2f%s" % (k, ax[k], ("" if grade is None else grade.get(k, ""))) for k in AXES if k in ax)


# keyword rules for knobs we have not measured (grade C): key words -> (axis effects when the knob rises)
KNOB_RULES = [
    (("cutoff", "freq", "tone", "bright", "hicut", "highcut", "lowpass", "lpf"), dict(brightness=+1, sharpness=+.5, warmth=-.5)),
    (("locut", "lowcut", "highpass", "hpf", "hpfreq"), dict(weight=-1, warmth=-.5, brightness=+.3)),
    (("reso", "resonance", "q", "peak"), dict(sharpness=+.8, roughness=+.4, tension=+.4)),
    (("drive", "dist", "sat", "overdrive", "fuzz", "crush", "bit"), dict(roughness=+1, noisiness=+.6, brightness=+.4, loudness=+.4)),
    (("gain", "volume", "level", "amp", "out", "trim", "master"), dict(loudness=+1)),
    (("attack", "atk", "a_"), dict(attack=-1)), (("decay", "release", "rel", "sustain", "hold", "length", "tail"), dict(decay=+1)),
    (("mix", "wet", "dry", "amount", "depth", "intensity"), dict(movement=+.3)),
    (("size", "room", "space", "reverb", "hall", "predelay", "feedback", "fb", "time", "delay"), dict(decay=+.8, width=+.4)),
    (("rate", "speed", "lfo", "freq1"), dict(movement=+1)), (("width", "spread", "stereo", "pan", "stereowidth"), dict(width=+1)),
    (("detune", "unison", "voices", "chorus", "fine"), dict(fullness=+.8, width=+.4, roughness=+.2)),
    (("noise", "grain", "hiss"), dict(noisiness=+1)), (("glide", "portamento"), dict(contour=+.5)),
    (("pitch", "semi", "oct", "transpose", "note", "key"), dict(pitch=+1)), (("pulse", "pw", "shape", "wave"), dict(brightness=+.4, warmth=-.2)),
    (("fm", "ratio", "index", "ring", "inharm"), dict(roughness=+.6, brightness=+.5, tension=+.4)),
    (("density", "count", "steps", "division", "subdiv", "repeat", "gate"), dict(density=+1)), (("swing", "shuffle", "humanize", "random", "chance", "prob"), dict(movement=+.5)),
    (("velocity", "vel", "accent", "dynamics"), dict(loudness=+.6, attack=-.3)), (("threshold", "ratio_c", "compress", "limit"), dict(loudness=+.3, attack=+.3)),
]
