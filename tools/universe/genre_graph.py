#!/usr/bin/env python3
"""Connected Genre <-> Sub-Genre <-> Sound-Category Network & Multi-Modal Fingerprint Engine.
Unifies 7 musical/acoustic domains across all 34 sub-genres, 12 macro families, and 16 sound categories:
  1. Groove & Micro-Timing (pulse density, syncopation, swing, humanize ms, BPM band)
  2. MIDI & Harmony (mode brightness, chord voicing size, melodic step/leap contour)
  3. Note Expression (learned from 394k MIDI note_expression pieces: vel mean/std, gate, glide, strum)
  4. Directive Synth Voice Archetypes (Bass 0..5, Keys 0..5, Leads 0..5)
  5. Audio FX & Knob Topology (sidechain, drive, delay, chorus, reverb, mod depth)
  6. 32-Band -> 5-Band Spectral & Dynamic Fingerprint (Sub, Low, Mid, Presence, Air, Crest, Silence, Whistle)
  7. 5 perceptual pillars named after the Spotify/Essentia features (Danceability, Energy, Valence,
     Brightness, SpectralContrast) but hand-written formulas over the style card: no Spotify or Essentia model
Blends each style's voice-archetype weights 22% toward its top-4 neighbours (one smoothing step) so neighbours share strength."""
import argparse, collections, json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spectral_gates as SG

HERE = os.path.dirname(os.path.abspath(__file__))
CATALOG = os.path.join(HERE, "..", "catalog", "styles_music.json")
STORE = os.path.expanduser("~/Infinite/training-data/store")
MODELS = os.path.expanduser("~/Infinite/training-data/models")

# Scale brightness / valence rank in [0, 1] (MusicTime ScaleType 0..13)
# 0 maj, 1 min, 2 harm min, 3 mel min, 4 dorian, 5 phrygian, 6 lydian, 7 mixolydian,
# 8 locrian, 9 maj pent, 10 min pent, 11 blues, 12 whole tone, 13 chromatic
SCALE_VALENCE = {
    0: 0.90, 6: 0.95, 9: 0.88, 7: 0.78, 12: 0.65,
    4: 0.55, 3: 0.48, 10: 0.45, 11: 0.42, 1: 0.35,
    2: 0.28, 13: 0.25, 5: 0.18, 8: 0.10,
}

# Scale tension / dark-dissonance rank in [0, 1]
SCALE_TENSION = {
    8: 0.95, 5: 0.88, 13: 0.85, 2: 0.80, 12: 0.68,
    11: 0.58, 3: 0.52, 1: 0.42, 10: 0.38, 4: 0.32,
    7: 0.25, 0: 0.15, 6: 0.22, 9: 0.10,
}

# Scale cognitive / modal complexity rank in [0, 1]
SCALE_COMPLEXITY = {
    13: 0.92, 12: 0.86, 3: 0.82, 2: 0.78, 8: 0.76,
    6: 0.72, 5: 0.68, 4: 0.64, 11: 0.60, 7: 0.55,
    1: 0.42, 0: 0.38, 10: 0.32, 9: 0.28,
}

# Two-axis affect (Russell 1980 circumplex): valence (unpleasant .. pleasant) and arousal (calm .. excited),
# each in [0, 1] with 0.5 neutral. Cues follow Gabrielsson & Lindstrom (2010) and Juslin (2001): mode, consonance
# and contour carry valence; tempo, density, loudness and register spread carry arousal.
AFFECT_AXES = ["valence", "arousal"]

# Chord-pack mood words (from file names) placed on the circumplex, after the Warriner et al. (2013) word norms.
MOOD_VA = {
    "Joyful": (0.90, 0.70), "Triumphant": (0.82, 0.85), "Hopeful": (0.76, 0.52), "Playful": (0.82, 0.68),
    "Excited": (0.78, 0.92), "Empowered": (0.74, 0.80), "Sad": (0.12, 0.28), "Lonely": (0.18, 0.22),
    "Tender": (0.64, 0.25), "Anguished": (0.10, 0.78), "Nostalgic": (0.45, 0.32), "Romantic": (0.72, 0.38),
    "Mysterious": (0.40, 0.48), "Surprised": (0.62, 0.82), "Rebellious": (0.30, 0.86), "Dark": (0.20, 0.55),
    "Fearful": (0.12, 0.80), "Dramatic": (0.30, 0.78), "Peaceful": (0.78, 0.12), "Spiritual": (0.66, 0.24),
    "Relaxed": (0.76, 0.15),
}

# Directive archetype weights per family/subgenre:
# Bass kinds: 0=sub, 1=808, 2=reese, 3=plucked, 4=fm, 5=acid
# Keys kinds: 0=epiano, 1=organ, 2=felt_piano, 3=vibes, 4=clav, 5=pipes
# Lead kinds: 0=brass, 1=reed, 2=supersaw, 3=pulse, 4=vowel, 5=fm_lead
FAMILY_ARCHETYPES = {
    "house": {
        "bass": [0.28, 0.10, 0.04, 0.32, 0.18, 0.08],
        "keys": [0.32, 0.34, 0.10, 0.10, 0.12, 0.02],
        "lead": [0.18, 0.12, 0.26, 0.18, 0.12, 0.14],
    },
    "techno": {
        "bass": [0.34, 0.12, 0.14, 0.06, 0.18, 0.16],
        "keys": [0.10, 0.18, 0.08, 0.12, 0.32, 0.20],
        "lead": [0.10, 0.08, 0.28, 0.24, 0.08, 0.22],
    },
    "trance": {
        "bass": [0.24, 0.04, 0.18, 0.28, 0.10, 0.16],
        "keys": [0.18, 0.16, 0.14, 0.12, 0.10, 0.30],
        "lead": [0.14, 0.06, 0.48, 0.14, 0.08, 0.10],
    },
    "drum_and_bass": {
        "bass": [0.30, 0.12, 0.38, 0.02, 0.16, 0.02],
        "keys": [0.36, 0.14, 0.24, 0.12, 0.06, 0.08],
        "lead": [0.10, 0.10, 0.32, 0.14, 0.12, 0.22],
    },
    "breaks": {
        "bass": [0.22, 0.24, 0.22, 0.08, 0.14, 0.10],
        "keys": [0.24, 0.24, 0.10, 0.12, 0.22, 0.08],
        "lead": [0.16, 0.12, 0.26, 0.22, 0.08, 0.16],
    },
    "garage": {
        "bass": [0.34, 0.18, 0.20, 0.10, 0.16, 0.02],
        "keys": [0.40, 0.26, 0.14, 0.10, 0.06, 0.04],
        "lead": [0.10, 0.14, 0.16, 0.16, 0.26, 0.18],
    },
    "dub": {
        "bass": [0.38, 0.22, 0.22, 0.02, 0.14, 0.02],
        "keys": [0.22, 0.26, 0.14, 0.14, 0.14, 0.10],
        "lead": [0.12, 0.14, 0.20, 0.18, 0.14, 0.22],
    },
    "ambient": {
        "bass": [0.52, 0.04, 0.22, 0.08, 0.12, 0.02],
        "keys": [0.26, 0.12, 0.32, 0.18, 0.00, 0.12],
        "lead": [0.14, 0.18, 0.10, 0.10, 0.32, 0.16],
    },
    "idm": {
        "bass": [0.20, 0.12, 0.12, 0.14, 0.26, 0.16],
        "keys": [0.20, 0.10, 0.22, 0.22, 0.14, 0.12],
        "lead": [0.08, 0.14, 0.14, 0.22, 0.14, 0.28],
    },
    "downtempo": {
        "bass": [0.36, 0.16, 0.08, 0.26, 0.10, 0.04],
        "keys": [0.40, 0.12, 0.28, 0.14, 0.04, 0.02],
        "lead": [0.16, 0.24, 0.10, 0.14, 0.22, 0.14],
    },
    "retro": {
        "bass": [0.18, 0.06, 0.14, 0.34, 0.16, 0.12],
        "keys": [0.28, 0.18, 0.12, 0.14, 0.18, 0.10],
        "lead": [0.30, 0.10, 0.28, 0.18, 0.04, 0.10],
    },
    "hard": {
        "bass": [0.16, 0.26, 0.22, 0.02, 0.12, 0.22],
        "keys": [0.08, 0.22, 0.04, 0.06, 0.30, 0.30],
        "lead": [0.12, 0.06, 0.44, 0.20, 0.04, 0.14],
    },
    "piano": {
        "bass": [0.44, 0.02, 0.04, 0.38, 0.10, 0.02],
        "keys": [0.28, 0.06, 0.46, 0.14, 0.02, 0.04],
        "lead": [0.14, 0.26, 0.06, 0.08, 0.30, 0.16],
    },
}

# Sub-genre specific directive overrides (where a sub-genre has an unmistakable signature sound)
SUBGENRE_OVERRIDES = {
    "acid_house":        {"bass": [0.08, 0.04, 0.04, 0.06, 0.08, 0.70]},
    "neurofunk":         {"bass": [0.15, 0.05, 0.55, 0.02, 0.20, 0.03]},
    "liquid_dnb":        {"bass": [0.42, 0.10, 0.32, 0.04, 0.10, 0.02], "keys": [0.45, 0.10, 0.30, 0.10, 0.02, 0.03]},
    "jungle":            {"bass": [0.40, 0.38, 0.12, 0.02, 0.06, 0.02]},
    "amapiano":          {"bass": [0.25, 0.45, 0.04, 0.10, 0.14, 0.02], "keys": [0.48, 0.18, 0.20, 0.08, 0.04, 0.02]},
    "footwork":          {"bass": [0.38, 0.46, 0.06, 0.04, 0.04, 0.02]},
    "dubstep":           {"bass": [0.30, 0.15, 0.25, 0.02, 0.25, 0.03]},
    "lofi_beats":        {"bass": [0.42, 0.14, 0.02, 0.32, 0.08, 0.02], "keys": [0.44, 0.06, 0.36, 0.10, 0.02, 0.02]},
    "solo_piano":        {"keys": [0.22, 0.02, 0.62, 0.10, 0.02, 0.02]},
    "neo_classical":     {"keys": [0.20, 0.06, 0.54, 0.12, 0.00, 0.08]},
    "ambient_piano":     {"keys": [0.24, 0.04, 0.52, 0.14, 0.00, 0.06]},
    "synthwave":         {"bass": [0.14, 0.04, 0.12, 0.46, 0.14, 0.10], "lead": [0.34, 0.08, 0.32, 0.14, 0.02, 0.10]},
    "psytrance":         {"bass": [0.18, 0.02, 0.10, 0.42, 0.10, 0.18]},
}

# Sound Category (Part 0..15) spectral & dynamic envelope bounds:
# Bands in AudioSummary (32 log-spaced bands 20 Hz..20 kHz):
#   Sub+Low: bands 0..10 (20..250 Hz)
#   Mid:     bands 11..21 (250..2500 Hz)
#   High:    bands 22..31 (2.5..20 kHz)
PART_NAMES = [
    "full", "synths", "pads", "plucks", "percussion", "drums", "bass", "fx",
    "foley", "keys", "leads", "textures", "kicks", "snares", "hats", "toms"
]

PART_SPECTRAL_GATES = {
    # part: (min_low_share, max_low_share, min_mid_share, min_high_share, max_high_share, min_crest, max_silence_ratio, max_whistle_ratio)
    "full":       (0.12, 0.82, 0.08, 0.01, 0.55, 1.35, 0.55, 0.48),
    "synths":     (0.02, 0.85, 0.10, 0.01, 0.65, 1.30, 0.65, 0.52),
    "pads":       (0.02, 0.80, 0.12, 0.005, 0.50, 1.22, 0.45, 0.45),
    "plucks":     (0.01, 0.78, 0.12, 0.02, 0.65, 1.65, 0.75, 0.50),
    "percussion": (0.01, 0.75, 0.10, 0.03, 0.80, 1.85, 0.85, 0.52),
    "drums":      (0.15, 0.85, 0.06, 0.02, 0.65, 1.80, 0.75, 0.50),
    "bass":       (0.32, 0.96, 0.02, 0.00, 0.35, 1.25, 0.68, 0.52),
    "fx":         (0.00, 0.92, 0.03, 0.005, 0.85, 1.22, 0.82, 0.60),
    "foley":      (0.00, 0.88, 0.05, 0.01, 0.85, 1.60, 0.90, 0.58),
    "keys":       (0.02, 0.82, 0.15, 0.005, 0.55, 1.35, 0.68, 0.48),
    "leads":      (0.00, 0.70, 0.18, 0.02, 0.70, 1.30, 0.72, 0.50),
    "textures":   (0.01, 0.88, 0.06, 0.005, 0.65, 1.20, 0.45, 0.48),
    "kicks":      (0.42, 0.98, 0.01, 0.00, 0.30, 1.75, 0.85, 0.55),
    "snares":     (0.05, 0.75, 0.15, 0.05, 0.75, 1.85, 0.88, 0.52),
    "hats":       (0.00, 0.25, 0.02, 0.42, 0.99, 1.75, 0.88, 0.58),
    "toms":       (0.25, 0.94, 0.04, 0.00, 0.40, 1.75, 0.88, 0.55),
}

# Calibrated on real renders by spectral_gates.py; the table above is the fallback before that has run.
_SG_FILE = os.path.join(MODELS, "shippable", "spectral_gates.json")
if os.path.exists(_SG_FILE):
    PART_SPECTRAL_GATES = {k: tuple(v) for k, v in json.load(open(_SG_FILE))["gates"].items()}


def groove_density_and_sync(drums):
    weights = [4, 1, 2, 1, 3, 1, 2, 1, 3, 1, 2, 1, 3, 1, 2, 1]
    hits = 0
    sync = 0.0
    for lane, pat in drums.items():
        s = (pat + "." * 16)[:16]
        for i, ch in enumerate(s):
            if ch != ".":
                hits += 1
                if weights[i] == 1 and s[(i + 1) % 16] == ".":
                    sync += 1.0
                elif weights[i] == 2:
                    sync += 0.35
    return hits / (8.0 * 16.0), min(1.0, sync / max(1, hits) * 1.5)


def compute_style_pillars(st):
    """Compute the 5 heuristic perceptual pillars for a style card (named after Spotify/EchoNest/Essentia features):
       [Danceability, Energy, Valence, Brightness, SpectralContrast] in [0, 1]^5."""
    bpm_mid = 0.5 * (st["bpm"][0] + st["bpm"][1])
    drums = st.get("drums", {})
    has_kick = "x" in drums.get("kick", "") or "o" in drums.get("kick", "")
    dens, sync = groove_density_and_sync(drums)
    fam = st["family"]

    # 1. Danceability: beat stability + tempo optimality around 110..135 BPM + groove pocket
    tempo_dance = math.exp(-((bpm_mid - 122.0) / 32.0) ** 2)
    pulse_clarity = 0.85 if fam in ("house", "techno", "trance", "garage", "hard") else (0.72 if has_kick else 0.15)
    pocket = 1.0 - abs(sync - 0.32)
    danceability = max(0.05, min(0.98, 0.45 * pulse_clarity + 0.35 * tempo_dance + 0.20 * pocket))
    if not has_kick:
        danceability *= 0.28

    # 2. Energy: tempo drive + drum density + distortion/drive/mod intensity
    fx_set = set(st.get("fx", []))
    drive_bonus = 0.18 if ("Drive" in fx_set or "Saturator" in fx_set or "Bitcrush" in fx_set) else 0.0
    bpm_norm = max(0.0, min(1.0, (bpm_mid - 65.0) / 115.0))
    energy = max(0.08, min(0.98, 0.38 * bpm_norm + 0.32 * min(1.0, dens * 3.5) + 0.15 * st.get("mod", 0.3) + drive_bonus + (0.10 if has_kick else 0.0)))

    # 3. Valence: scale mood + chord warmth + rhythmic bounce
    scales = st.get("scale", [1])
    scale_val = sum(SCALE_VALENCE.get(sc, 0.45) for sc in scales) / max(1, len(scales))
    swing_lift = min(0.15, st.get("swing", 0.0) * 0.6)
    valence = max(0.08, min(0.95, 0.65 * scale_val + 0.20 * tempo_dance + swing_lift))

    # 4. Brightness: synth pool harmonic richness + motif octave + filter/FX air
    synths = set(st.get("synth", []))
    bright_base = 0.62 if ("FM" in synths or "Metallic" in synths or "Wavetable" in synths) else 0.42
    if fam in ("ambient", "dub", "downtempo") or st["id"] in ("dub_techno", "lofi_beats", "dark_ambient", "vaporwave"):
        bright_base -= 0.18
    elif fam in ("trance", "hard", "idm") or st["id"] in ("acid_house", "classic_trance", "psytrance", "gabber"):
        bright_base += 0.18
    brightness = max(0.12, min(0.92, bright_base))

    # 5. Spectral Contrast: dynamic punch / transient separation vs lush washed wall-of-sound
    reverb_wash = 0.20 if (fam == "ambient" or st["id"] in ("vaporwave", "dub_techno", "drone_kosmische")) else 0.0
    sidechain_punch = 0.15 if st.get("sidechain") else 0.0
    contrast = max(0.18, min(0.94, 0.55 + sidechain_punch + 0.20 * min(1.0, dens * 3.0) - reverb_wash))

    return {
        "danceability": round(danceability, 4),
        "energy": round(energy, 4),
        "valence": round(valence, 4),
        "brightness": round(brightness, 4),
        "contrast": round(contrast, 4),
    }


def _clamp(x, lo=0.04, hi=0.98):
    return max(lo, min(hi, x))


def compute_style_descriptors(st, pillars):
    """Two style descriptors that are not feelings: harmonic/rhythmic complexity and groove pocket, in [0, 1]."""
    sid, fam = st["id"], st["family"]
    scales = st.get("scale", [1])
    _, sync = groove_density_and_sync(st.get("drums", {}))
    chord_sizes = st.get("chords", {}).get("sizes", [3])
    synths = set(st.get("synth", []))
    sc_cmp = sum(SCALE_COMPLEXITY.get(sc, 0.45) for sc in scales) / max(1, len(scales))
    ext_bonus = 0.22 if (max(chord_sizes) if chord_sizes else 3) >= 4 else 0.0
    synth_cmp = 0.16 if ("FM" in synths or "Partials" in synths or "Granular" in synths) else 0.06
    cmp_ = 0.30 * sc_cmp + 0.25 * sync + ext_bonus + synth_cmp + 0.15 * st.get("groove", 0.5)
    if fam in ("experimental", "idm", "piano") or sid in ("idm", "glitch", "footwork", "neurofunk", "detroit_techno", "neo_classical", "liquid_dnb", "breakbeat"):
        cmp_ += 0.24
    sw_bonus = min(0.22, st.get("swing", 0.0) * 1.1)
    pocket = 0.38 * pillars["danceability"] + 0.30 * sync + 0.18 * st.get("groove", 0.5) + sw_bonus
    if sid in ("deep_house", "minimal_techno", "dub_techno", "amapiano", "uk_garage", "acid_house", "jungle", "footwork", "breakbeat", "psytrance", "tech_house", "french_filter_house"):
        pocket += 0.18
    return {"complexity": round(_clamp(cmp_), 4), "pocket": round(_clamp(pocket), 4)}


def compute_style_affect(st, pillars):
    """Valence and arousal of a style card. Valence: mode, brightness, bounce. Arousal: tempo, energy, drum density."""
    scales = st.get("scale", [1])
    bpm_mid = 0.5 * (st["bpm"][0] + st["bpm"][1])
    dens, _ = groove_density_and_sync(st.get("drums", {}))
    sc_val = sum(SCALE_VALENCE.get(sc, 0.45) for sc in scales) / max(1, len(scales))
    sc_ten = sum(SCALE_TENSION.get(sc, 0.40) for sc in scales) / max(1, len(scales))
    v = 0.45 * sc_val + 0.25 * pillars["valence"] + 0.15 * pillars["brightness"] + 0.10 * pillars["danceability"] + 0.05 - 0.15 * (sc_ten - 0.4)
    tempo = max(0.0, min(1.0, (bpm_mid - 60.0) / 120.0))
    a = 0.40 * tempo + 0.30 * pillars["energy"] + 0.20 * min(1.0, dens * 3.2) + 0.10 * pillars["contrast"]
    return {"valence": round(_clamp(v), 4), "arousal": round(_clamp(a), 4)}


def tension_of(va):
    """High arousal with low valence: the circumplex's tense/angry quadrant."""
    return max(0.0, min(1.0, 2.0 * va["arousal"] * (1.0 - va["valence"])))


def infer_piece_affect(r):
    """Valence and arousal of one corpus piece from its own notes. Arousal is None where the piece carries no
       tempo, density or loudness cue (chord progressions without a tempo)."""
    t = r.get("type", "")
    v, a = 0.5, None
    if t == "chord_prog":
        chords = r.get("chords", [])
        n_c = max(1, len(chords))
        minor_sh = sum(1 for c in chords if c.get("minor")) / n_c
        dim_sh = sum(1 for c in chords if "dim" in c.get("ext", "") or c.get("acc")) / n_c
        is_min_mode = 1.0 if str(r.get("mode", "")).lower().startswith("min") else 0.0
        v = 0.20 + 0.55 * (1.0 - minor_sh) + 0.15 * (1.0 - is_min_mode) - 0.10 * dim_sh
        if r.get("bpm"):
            a = max(0.0, min(1.0, (float(r["bpm"]) - 60.0) / 120.0))
        moods = [m for m in r.get("mood", []) if m in MOOD_VA]
        if moods:
            mv = sum(MOOD_VA[m][0] for m in moods) / len(moods)
            ma = sum(MOOD_VA[m][1] for m in moods) / len(moods)
            v = 0.35 * v + 0.65 * mv
            a = ma if a is None else 0.35 * a + 0.65 * ma
    elif t == "melody_contour":
        P_ = r.get("pitches", [])
        steps = r.get("steps") or [b - a_ for a_, b in zip(P_, P_[1:])]
        if P_ and steps:
            n_s = len(steps)
            up_bias = sum((s > 0) - (s < 0) for s in steps) / n_s
            leap_rate = sum(1 for s in steps if abs(s) >= 4) / n_s
            tens_rate = sum(1 for s in steps if abs(s) in (1, 6, 10, 11)) / n_s
            span = max(P_) - min(P_)
            reg = sum(P_) / len(P_)
            root_pc = P_[0] % 12
            pcs = set((p - root_pc) % 12 for p in P_)
            is_minor = 1.0 if (3 in pcs and 4 not in pcs) else (0.0 if (4 in pcs and 3 not in pcs) else 0.45)
            v = (0.45 * (1.0 - is_minor) + 0.25 * max(0.0, min(1.0, 0.5 + up_bias * 0.9))
                 + 0.15 * max(0.0, min(1.0, (reg - 52.0) / 24.0)) + 0.15 * (1.0 - min(1.0, tens_rate * 2.5)))
            a = 0.45 * min(1.0, leap_rate * 2.2) + 0.35 * min(1.0, span / 18.0) + 0.20 * max(0.0, min(1.0, (reg - 48.0) / 36.0))
            if r.get("rate"):
                # listener labels (EMOPIA, VGMIDI): note rate ranks arousal at AUC 0.88 / 0.68 on both sets, while
                # leaps, span and register flip sign between them, so the rate leads where the row has its rhythm
                a = 0.75 * max(0.0, min(1.0, (float(r["rate"]) - 0.5) / 3.5)) + 0.25 * a
    elif t == "drum_groove":
        lanes = r.get("lanes", [])
        dens, sync = groove_density_and_sync({str(i): lanes[i] for i in range(min(8, len(lanes))) if lanes[i]})
        k_hits = sum(1 for c in ((lanes[0] if lanes else "") + "." * 16)[:16] if c != ".")
        a = 0.50 * min(1.0, dens * 3.5) + 0.25 * min(1.0, k_hits / 5.0)
        a += 0.25 * (max(0.0, min(1.0, (float(r["bpm"]) - 60.0) / 120.0)) if r.get("bpm") else 0.5)
        vel = r.get("vel")
        if vel:
            a = 0.8 * a + 0.2 * (sum(int(c) for c in vel if c.isdigit()) / max(1, sum(c.isdigit() for c in vel)) / 9.0)
        v = 0.45 + 0.20 * sync  # drums alone carry little valence: syncopation reads as playful
    elif t == "bass_seq":
        st_ = r.get("steps", "")
        dens = sum(1 for c in st_ if c != ".") / max(1, len(st_))
        a = 0.65 * min(1.0, dens / 0.6) + 0.35 * (max(0.0, min(1.0, (float(r["bpm"]) - 60.0) / 120.0)) if r.get("bpm") else 0.5)
        if r.get("mode"):
            v = 0.62 if str(r["mode"]).lower().startswith("maj") else 0.36
    elif t == "note_expression":
        vs = r.get("vel") or r.get("vels") or []
        if vs:
            vm = sum(vs) / len(vs) / 127.0
            vsd = math.sqrt(sum((x / 127.0 - vm) ** 2 for x in vs) / len(vs))
            a = 0.70 * vm + 0.30 * min(1.0, vsd * 4.0)
    return {"valence": round(_clamp(v), 4), "arousal": None if a is None else round(_clamp(a), 4)}


# MIDI shaping at the four circumplex corners (valence, arousal); any point blends them bilinearly.
# keys: step_bias, leap_prob, tension_iv_prob, chord_ext_bonus, vel_bias, gate_scale, humanize_scale, sync_target, euclid_prob
AFFECT_CORNERS = {
    (1, 1): {"step_bias": +0.30, "leap_prob": 0.30, "tension_iv_prob": 0.06, "chord_ext_bonus": 0.18, "vel_bias": +0.14, "gate_scale": 0.85, "humanize_scale": 0.80, "sync_target": 0.36, "euclid_prob": 0.38},  # joyful, excited
    (0, 1): {"step_bias": -0.15, "leap_prob": 0.34, "tension_iv_prob": 0.28, "chord_ext_bonus": 0.20, "vel_bias": +0.16, "gate_scale": 0.85, "humanize_scale": 0.80, "sync_target": 0.40, "euclid_prob": 0.50},  # tense, angry
    (0, 0): {"step_bias": -0.42, "leap_prob": 0.12, "tension_iv_prob": 0.08, "chord_ext_bonus": 0.40, "vel_bias": -0.16, "gate_scale": 1.28, "humanize_scale": 1.35, "sync_target": 0.18, "euclid_prob": 0.15},  # sad, tired
    (1, 0): {"step_bias": +0.08, "leap_prob": 0.24, "tension_iv_prob": 0.02, "chord_ext_bonus": 0.50, "vel_bias": -0.18, "gate_scale": 1.38, "humanize_scale": 1.25, "sync_target": 0.14, "euclid_prob": 0.18},  # calm, content
}


def affect_directive(v, a):
    w = {(1, 1): v * a, (0, 1): (1 - v) * a, (0, 0): (1 - v) * (1 - a), (1, 0): v * (1 - a)}
    keys = AFFECT_CORNERS[(1, 1)].keys()
    return {k: sum(w[c] * AFFECT_CORNERS[c][k] for c in w) for k in keys}


def quadrant(va):
    return ("+" if va["valence"] >= 0.5 else "-") + "v" + ("+" if (va["arousal"] or 0.5) >= 0.5 else "-") + "a"


def extract_corpus_expression_stats(store_dir):
    """Velocity statistics and the valence/arousal quadrant spread of the corpus."""
    vel_means, vel_stds, bend_rates = [], [], []
    quad_counts = collections.Counter()
    p = os.path.join(os.path.expanduser(store_dir), "inbox.jsonl")
    if os.path.exists(p):
        with open(p) as f:
            for i, ln in enumerate(f):
                if i > 80000:
                    break
                r = json.loads(ln)
                if not r.get("ok"):
                    continue
                t = r.get("type")
                if t == "note_expression":
                    vs = r.get("vel") or r.get("vels") or []
                    if vs:
                        m = sum(vs) / len(vs) / 127.0
                        vel_means.append(m)
                        vel_stds.append(math.sqrt(sum((v / 127.0 - m) ** 2 for v in vs) / len(vs)))
                    bs = r.get("bend") or r.get("bends") or []
                    if bs:
                        bend_rates.append(sum(1 for b in bs if abs(b) > 0.02) / len(bs))
                if t in ("melody_contour", "drum_groove", "bass_seq", "chord_prog"):
                    va = infer_piece_affect(r)
                    if va["arousal"] is not None:
                        quad_counts[quadrant(va)] += 1
    for name in ("chords.jsonl", "public_drums.jsonl"):
        p_more = os.path.join(os.path.expanduser(store_dir), name)
        if not os.path.exists(p_more):
            continue
        with open(p_more) as f:
            for i, ln in enumerate(f):
                if i > 15000:
                    break
                r = json.loads(ln)
                if r.get("ok") and r.get("type") in ("chord_prog", "drum_groove"):
                    va = infer_piece_affect(r)
                    if va["arousal"] is not None:
                        quad_counts[quadrant(va)] += 1
    return {
        "corpus_vel_mean": round(sum(vel_means) / max(1, len(vel_means)), 4) if vel_means else 0.63,
        "corpus_vel_std": round(sum(vel_stds) / max(1, len(vel_stds)), 4) if vel_stds else 0.11,
        "corpus_bend_rate": round(sum(bend_rates) / max(1, len(bend_rates)), 4) if bend_rates else 0.08,
        "n_velocity_pieces": len(vel_means),
        "n_affect_pieces": sum(quad_counts.values()),
        "corpus_quadrants": dict(quad_counts),
    }


def build_genre_network(catalog_path=CATALOG, store_dir=STORE):
    cat = json.load(open(catalog_path))
    styles = cat["styles"]
    expr_stats = extract_corpus_expression_stats(store_dir)

    raw_nodes = []
    for idx, st in enumerate(styles):
        fam = st["family"]
        sid = st["id"]
        pillars = compute_style_pillars(st)
        affect = compute_style_affect(st, pillars)
        desc = compute_style_descriptors(st, pillars)
        base_arch = FAMILY_ARCHETYPES.get(fam, FAMILY_ARCHETYPES["house"])
        ovr = SUBGENRE_OVERRIDES.get(sid, {})
        arch = {
            "bass": list(ovr.get("bass", base_arch["bass"])),
            "keys": list(ovr.get("keys", base_arch["keys"])),
            "lead": list(ovr.get("lead", base_arch["lead"])),
        }
        bpm_mid = 0.5 * (st["bpm"][0] + st["bpm"][1])
        dens, sync = groove_density_and_sync(st.get("drums", {}))
        chords = st.get("chords", {})
        motif = st.get("motif", {})
        expr = {
            "vel_mean": round(0.56 + 0.28 * pillars["energy"] - 0.08 * (1.0 - affect["valence"]), 3),
            "vel_spread": round(0.08 + 0.14 * st.get("groove", 0.5) + 0.08 * desc["complexity"], 3),
            "gate_pct": float(motif.get("gate", 85 if sid == "acid_house" else (78 if fam in ("ambient", "piano") else 55))),
            "strum_ms": float(chords.get("strum", 12 if fam in ("house", "piano", "downtempo") else 0)),
            "human_ms": float(chords.get("human", 14 if fam in ("downtempo", "garage", "piano") else 4)),
            "glide_prob": round(0.75 if sid in ("acid_house", "amapiano", "dubstep", "neurofunk") else (0.35 if fam in ("drum_and_bass", "garage", "trance") else 0.12), 3),
        }
        raw_nodes.append({
            "index": idx,
            "id": sid,
            "family": fam,
            "bpm": st["bpm"],
            "bpm_mid": bpm_mid,
            "swing": st.get("swing", 0.0),
            "groove_density": round(dens, 4),
            "syncopation": round(sync, 4),
            "sidechain": bool(st.get("sidechain", False)),
            "pillars": pillars,
            "affect": affect,
            "descriptors": desc,
            "archetypes": arch,
            "expression": expr,
        })

    # Compute weighted adjacency matrix W_ij between all 34 sub-genres using BPM + Pillars + Affect + Groove
    n = len(raw_nodes)
    adj = [[0.0] * n for _ in range(n)]
    neighbors = {}
    for i in range(n):
        ni = raw_nodes[i]
        pi = ni["pillars"]
        ei = ni["affect"]
        row_nb = []
        for j in range(n):
            if i == j:
                adj[i][j] = 1.0
                continue
            nj = raw_nodes[j]
            pj = nj["pillars"]
            ej = nj["affect"]
            d_bpm = ((ni["bpm_mid"] - nj["bpm_mid"]) / 45.0) ** 2
            d_pil = sum((pi[k] - pj[k]) ** 2 for k in pi) / 5.0
            d_emo = sum((ei[k] - ej[k]) ** 2 for k in ei) / 2.0
            d_fam = 0.0 if ni["family"] == nj["family"] else 0.35
            d_sync = (ni["syncopation"] - nj["syncopation"]) ** 2
            dist2 = 0.25 * d_bpm + 0.25 * d_pil + 0.20 * d_emo + 0.18 * d_fam + 0.12 * d_sync
            w = math.exp(-dist2 / 0.18)
            adj[i][j] = w
            row_nb.append((w, j, nj["id"]))
        row_nb.sort(reverse=True)
        neighbors[ni["id"]] = [{"id": sid, "index": j, "weight": round(w, 4)} for w, j, sid in row_nb[:4]]
        ni["neighbors"] = neighbors[ni["id"]]

    # One neighbour-blend step (alpha = 0.22) on the voice archetypes: connected sub-genres share statistical strength
    # while keeping 78% of their directive signature.
    alpha = 0.22
    for i in range(n):
        ni = raw_nodes[i]
        nbs = ni["neighbors"]
        w_sum = sum(nb["weight"] for nb in nbs) or 1.0
        for role in ("bass", "keys", "lead"):
            orig = ni["archetypes"][role]
            smoothed = []
            for k in range(len(orig)):
                nb_val = sum(nb["weight"] * raw_nodes[nb["index"]]["archetypes"][role][k] for nb in nbs) / w_sum
                smoothed.append((1.0 - alpha) * orig[k] + alpha * nb_val)
            tot = sum(smoothed) or 1.0
            ni["archetypes"][role] = [round(v / tot, 4) for v in smoothed]

    return {
        "n_styles": n,
        "affect_axes": AFFECT_AXES,
        "affect_corners": {"%d%d" % c: d for c, d in AFFECT_CORNERS.items()},
        "corpus_expression": expr_stats,
        "styles": raw_nodes,
        "part_spectral_gates": PART_SPECTRAL_GATES,
    }


def verify_spectral_critic(band_power, crest, silence_ratio, correlation, part_name="full"):
    """Evaluate a 32-band AudioSummary spectrum + dynamics against a part's spectral gate
       (spectral_gates.verify: the same checks as SampleCriticData::VerifyAudioSpectrum)."""
    if sum(max(0.0, x) for x in band_power) <= 1e-12:
        return False, ["silent_spectrum"]
    g = PART_SPECTRAL_GATES.get(part_name, PART_SPECTRAL_GATES["full"])
    r = {"bands": band_power, "peak": crest, "rms": 1.0, "silence": silence_ratio, "corr": correlation}
    fails = SG.verify(g, SG.features(r))
    return len(fails) == 0, fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", default=MODELS)
    a = ap.parse_args()
    net = build_genre_network()
    for scope in ("local", "shippable"):
        d = os.path.join(os.path.expanduser(a.models), scope)
        os.makedirs(d, exist_ok=True)
        out_p = os.path.join(d, "genre_graph.json")
        json.dump(net, open(out_p, "w"), indent=2)
    print("Built connected Genre-Subgenre Graph: %d styles, 16 sound categories" % net["n_styles"])
    for st in net["styles"][:8]:
        nbs = ", ".join("%s(%.2f)" % (x["id"], x["weight"]) for x in st["neighbors"][:3])
        p = st["pillars"]
        print("  %-20s [%-13s] D=%.2f E=%.2f V=%.2f B=%.2f C=%.2f | neighbors: %s" % (
            st["id"], st["family"], p["danceability"], p["energy"], p["valence"], p["brightness"], p["contrast"], nbs))


if __name__ == "__main__":
    main()
