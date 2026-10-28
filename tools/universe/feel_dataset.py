#!/usr/bin/env python3
"""Build the five feel tables (ml-venv python). Run feel_measure.py first (knob slopes).
  Instruments / Sounds / Note Model / Mix / Nodes-Knobs  ->  docs/plans/data-universe/feel/dataset/*.csv  and  feel_dataset.xlsx
Evidence column per row:  M measured on our data, D read from a dataset label or file, L literature number, R rule from feel_lex.py (grade C).
"""
import csv, glob, json, math, os, re, sys, collections
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import feel_lex as L

ROOT = os.path.expanduser("~/Infinite")
OUT = os.path.join(HERE, "../../docs/plans/data-universe/feel/dataset")
ATLAS = os.path.join(ROOT, "training-data/atlas")
MIDI = os.path.join(ROOT, "datasets/midi/data")
os.makedirs(OUT, exist_ok=True)
MEAS = json.load(open(os.path.join(OUT, "_measured.json")))
TABLES = {}


def save(name, header, rows):
    TABLES[name] = (header, rows)
    with open(os.path.join(OUT, name + ".csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(header); w.writerows(rows)
    print("%-22s %6d rows  %2d cols" % (name, len(rows), len(header)))


def pct(arr):
    """value -> percentile 0..1 among finite values"""
    a = np.array(arr, float); ok = np.isfinite(a); order = np.sort(a[ok])
    return lambda x: float(np.searchsorted(order, x) / max(1, len(order)))


def r2(x): return round(float(x), 3)


# =====================================================================================================================
# shared data
# =====================================================================================================================
CL = json.load(open(os.path.join(ATLAS, "clusters.json")))
CMX = json.load(open(os.path.join(ATLAS, "character_matrix.json")))
GEN = json.load(open(os.path.join(ATLAS, "genres.json")))["genres"]
INS = [json.loads(l) for l in open(os.path.join(ATLAS, "instruments_clustered.jsonl"))]
CLUSTERS = {}
for fam, fd in CL["families"].items():
    for c in fd["clusters"]: CLUSTERS[c["id"]] = dict(c, family=fam)
C_PCT = {k: pct([c[k] for c in CLUSTERS.values() if c.get(k) is not None]) for k in ("centroid_hz", "decay_ms", "attack_ms")}


def cluster_vec(c):
    f = lambda k: C_PCT[k](c[k]) if c.get(k) is not None else 0.5
    return np.array([f("centroid_hz"), f("decay_ms"), 1 - f("attack_ms")])


def nearest_clusters(ax, fam=None, k=3):
    t = np.array([ax.get("brightness", .5), ax.get("decay", .5), ax.get("attack", .5)])
    cands = [c for c in CLUSTERS.values() if fam is None or c["family"] == fam]
    cands.sort(key=lambda c: float(np.sum((cluster_vec(c) - t) ** 2)))
    return ["%s (%s)" % (c["id"], c["name"]) for c in cands[:k]]


def top_effects(ax, k=3):
    """effects with positive lift for the characters this axis vector implies (character_matrix, measured on preset chains)"""
    chars = []
    if ax.get("brightness", .5) > .66: chars.append("bright")
    if ax.get("brightness", .5) < .34: chars.append("dark")
    if ax.get("warmth", .5) > .66: chars.append("warm")
    if ax.get("decay", .5) > .66 or ax.get("width", .5) > .7: chars.append("spacious")
    if ax.get("noisiness", .5) > .6 or ax.get("roughness", .5) > .6: chars.append("dirty")
    if ax.get("attack", .5) > .75 and ax.get("loudness", .5) > .6: chars.append("aggressive")
    if ax.get("weight", .5) > .7: chars.append("phat")
    if ax.get("movement", .5) > .65: chars.append("moving")
    sc = collections.defaultdict(float); n = collections.Counter()
    for ch in chars:
        for eff, d in CMX["effects"].get(ch, {}).items():
            if d["lift"] > 0 and d["p"] < .1: sc[eff] += d["lift"]; n[eff] += d["n"]
    best = sorted(sc, key=lambda e: -sc[e])[:k]
    return "; ".join("%s (lift %+.2f, n=%d)" % (e, sc[e], n[e]) for e in best) or "none significant (p<.1)", chars


def engine_knobs(engine, axis="brightness", k=3):
    e = MEAS["synth"].get(engine, {}).get("knobs", [])
    ks = [x for x in e if x.get("slope")]
    ks.sort(key=lambda x: -abs(x["slope"].get(axis, 0)))
    return "; ".join("%s %s%.2f" % (x["knob"], "+" if x["slope"][axis] >= 0 else "-", abs(x["slope"][axis])) for x in ks[:k])


def eng_key(s):
    for e in ("Analog", "Oscillator", "Wavetable", "FM", "String", "Metallic"):
        if e.split()[0] in s: return e
    return None


# =====================================================================================================================
# 1. INSTRUMENTS (14,161 rows)
# =====================================================================================================================
def table_instruments():
    A = [r["audio"] for r in INS if r.get("audio")]
    P = {k: pct([a[k] for a in A if a.get(k) is not None]) for k in ("centroid", "flatness", "attack_ms", "decay_ms", "rms", "low", "mid", "high")}
    header = ["id", "Instrument", "source", "family", "keys_lo", "keys_hi", "vel_layers", "zones",
              "audio_measured", "centroid_Hz", "flatness", "attack_ms", "decay_ms", "rms", "low_share", "mid_share", "high_share",
              "22 Characteristics Mapped (axis=value; M measured from its audio, else P prior)", "V", "E", "T",
              "Axis/Feelings Mapped (nearest moods)", "Best Sound Model to be used", "Best Knobs to steer (measured, brightness slope)",
              "Best Note Model to be used", "Best Effects (positive lift, p<.1)", "Mix Model (role | band | level dB | pan | reverb send dB | HPF Hz | comp)",
              "Cluster Mapping", "cluster_assignment", "Evidence"]
    rows = []
    for r in INS:
        fam = r["family"] if r["family"] in L.FAM else "other"; F = L.FAM[fam]
        name = r["name"]; words = re.findall(r"[a-z0-9]+", name.lower())
        ax = L.nudge(F["ax"], *[L.WORD[w] for w in words if w in L.WORD])
        grade = {a: "P" for a in ax}
        au = r.get("audio")
        g = lambda k: (au or {}).get(k)
        if au:
            src = {"brightness": ("centroid", False), "noisiness": ("flatness", False), "attack": ("attack_ms", True), "decay": ("decay_ms", False),
                   "loudness": ("rms", False), "weight": ("low", False), "sharpness": ("high", False), "warmth": ("mid", False)}
            m = {}
            for a, (fld, inv) in src.items():
                if au.get(fld) is not None:
                    x = P[fld](au[fld]); m[a] = 1 - x if inv else x
            for a, v in m.items(): ax[a] = r2(v); grade[a] = "M"
        ax = {a: r2(v) for a, v in ax.items()}
        v, e, t = L.vet_from_axes(ax)
        fx, chars = top_effects(ax)
        eng = eng_key(F["engine"]) or "Sampler"
        cl = r.get("cluster"); cn = CLUSTERS.get(cl, {}).get("name", "")
        if cl: cmap = "%s (%s)" % (cl, cn)
        else: cmap = "needs render -> measure 22 axes -> nearest: " + " | ".join(nearest_clusters(ax, fam, 2))
        mix = " | ".join([F["role"], F["band"], "%+d" % F["level"], F["pan"], "%d" % F["send"], str(F["hpf"]), F["ratio"]])
        rows.append([r["id"], name, r["source"], r["family"], (r.get("keys") or ["", ""])[0], (r.get("keys") or ["", ""])[1], r.get("vel_layers"), r.get("zones"),
                     "yes" if au else "no", g("centroid"), g("flatness"), g("attack_ms"), g("decay_ms"), g("rms"),
                     g("low"), g("mid"), g("high"), L.fmt_ax(ax, grade), v, e, t, ", ".join(L.nearest_moods(v, e, t)),
                     F["engine"], engine_knobs(eng, "brightness") if eng in MEAS["synth"] else "n/a (engine unmeasured)",
                     "%s | register %s | rhythm %s | velocity %s" % (F["part"], F["reg"], F["rhythm"], F["vel"]), fx, mix, cmap,
                     r.get("assigned") or "unassigned", "M+R" if au else "R (prior, render to confirm)"])
    save("1_Instruments", header, rows)


# =====================================================================================================================
# 2. SOUNDS (preset vocabulary, clusters, families)
# =====================================================================================================================
def table_sounds():
    C = collections.defaultdict(collections.Counter); eng = collections.Counter(); n = 0
    for f in glob.glob(os.path.join(ROOT, "training-data/presets/*.jsonl")):
        for l in open(f):
            try: r = json.loads(l)
            except Exception: continue
            n += 1; eng[r.get("engine", "?")] += 1
            t = r.get("tags")
            if isinstance(t, dict):
                for k in ("mood", "genre", "type", "types", "character", "characters", "features", "preset_style", "bank", "complexity", "osc_type"):
                    v = t.get(k)
                    for x in (v if isinstance(v, list) else [v] if v else []): C[k][str(x).strip()] += 1
    header = ["Sound", "kind", "preset_count", "22 Characteristics Mapped (target axes)", "V", "E", "T", "Axis/Feelings Mapped (nearest moods)",
              "Best Sound Model to be used", "Best Effects (positive lift)", "Cluster Mapping (nearest)", "Evidence"]
    rows = []

    def add(label, kind, cnt, ax, vet=None, model=None, note="R"):
        ax = {a: r2(x) for a, x in ax.items()}
        v, e, t = vet if vet else L.vet_from_axes(ax)
        fx, _ = top_effects(ax)
        fam = None
        for w, fm in (("bass", "bass"), ("pad", "texture"), ("lead", "synth"), ("pluck", "synth"), ("key", "keys"), ("piano", "piano"), ("string", "strings"),
                      ("drum", "drums"), ("perc", "drums"), ("kit", "drums"), ("brass", "brass"), ("guitar", "guitar"), ("choir", "choir"), ("vocal", "choir"),
                      ("fx", "texture"), ("atmos", "texture"), ("ambient", "texture"), ("mallet", "mallets"), ("bell", "mallets"), ("organ", "keys")):
            if w in label.lower(): fam = fm; break
        eng_ = model or (L.FAM[fam]["engine"] if fam else "Wavetable or Analog (default; classify by axes)")
        rows.append([label, kind, cnt, L.fmt_ax(ax), v, e, t, ", ".join(L.nearest_moods(v, e, t)), eng_, fx, " | ".join(nearest_clusters(ax, fam, 2)), note])

    base = {a: .5 for a in L.AXES}
    for m, cnt in C["mood"].most_common():
        vet = L.MOOD.get(m)
        if vet: add(m, "mood (preset tag)", cnt, L.axes_from_vet(*vet) | {a: .5 for a in ("pitch", "spread", "contour", "fullness") if a not in L.axes_from_vet(*vet)}, vet, None, "R (VET from Russell/Eerola placement)")
    for g, cnt in C["genre"].most_common():
        gid = re.sub(r"[^a-z0-9]+", "_", g.lower()).strip("_")
        gg = GEN.get(gid) or GEN.get(gid.replace("_", ""))
        if gg and (gg.get("declared") or {}).get("pillars"):
            p = gg["declared"]["pillars"]; ev = (p["valence"] * 2 - 1, p["energy"] * 2 - 1, (1 - p["valence"]) * p["energy"])
            ax = L.axes_from_vet(*ev); ax["brightness"] = p["brightness"]
            add(g, "genre (preset tag)", cnt, ax, tuple(r2(x) for x in ev), None, "D (declared pillars of genres.json) + R")
        else:
            add(g, "genre (preset tag)", cnt, dict(base), None, None, "R (no pillar card: neutral)")
    for ch, cnt in C["characters"].most_common() + C["character"].most_common(60):
        words = [w.strip() for w in re.split(r"[,;]", ch) if w.strip()]
        ax = L.nudge(base, *[L.CHARACTER.get(w, {}) for w in words])
        add(ch, "character tag(s)", cnt, ax, None, None, "R")
    for ty, cnt in C["type"].most_common():
        words = re.findall(r"[a-z0-9]+", ty.lower()); ax = L.nudge(base, *[L.WORD[w] for w in words if w in L.WORD])
        add(ty, "preset type", cnt, ax, None, None, "R")
    for ty, cnt in C["types"].most_common():
        words = re.findall(r"[a-z0-9]+", ty.lower()); ax = L.nudge(base, *[L.WORD[w] for w in words if w in L.WORD])
        add(ty, "preset type list", cnt, ax, None, None, "R")
    for ft, cnt in C["features"].most_common(300):
        words = [w.strip() for w in re.split(r"[,;]", ft) if w.strip()]; ax = L.nudge(base, *[L.CHARACTER.get(w, {}) for w in words])
        add(ft, "feature tags", cnt, ax, None, None, "R")
    PN = collections.Counter()
    for f in glob.glob(os.path.join(ROOT, "training-data/presets/*.jsonl")):
        for l in open(f):
            try: r = json.loads(l)
            except Exception: continue
            t = r.get("tags")
            nm = (t.get("patch") if isinstance(t, dict) and t.get("patch") else r.get("name"))
            if nm: PN[re.sub(r"\s+", " ", str(nm)).strip()] += 1
    for nm, cnt in PN.most_common(4000):
        words = re.findall(r"[a-z0-9]+", nm.lower()); ax = L.nudge(base, *[L.WORD[w] for w in words if w in L.WORD])
        if ax != base: add(nm, "preset name (keyword-mapped)", cnt, ax, None, None, "R (name keywords)")
    for b, cnt in C["bank"].most_common(): add(b, "bank", cnt, dict(base), None, None, "R (neutral)")
    for fam, fd in CL["families"].items():
        for c in fd["clusters"]:
            ax = dict(L.FAM.get(fam, L.FAM["other"])["ax"]); ax["brightness"] = r2(cluster_vec(c)[0]); ax["decay"] = r2(cluster_vec(c)[1]); ax["attack"] = r2(cluster_vec(c)[2])
            add("%s [%s]" % (c["name"], c["id"]), "instrument cluster (%s)" % fam, c["size"], ax, None, None, "M (centroid/decay/attack of its members) + R")
    for fam, F in L.FAM.items(): add(fam, "instrument family", sum(1 for r in INS if r["family"] == fam), dict(F["ax"]), None, F["engine"], "R (family prior)")
    save("2_Sounds", header, rows)


# =====================================================================================================================
# 3. NOTE MODEL
# =====================================================================================================================
KS_MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
KS_MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
NOTE = "CDEFGAB"


def midi_stats(path):
    import mido
    try: mf = mido.MidiFile(path)
    except Exception: return None
    tpb = mf.ticks_per_beat or 480; tempo = None; ons = []; pc = np.zeros(12); vel = []; pit = []; open_ = {}
    for tr in mf.tracks:
        t = 0
        for m in tr:
            t += m.time
            if m.type == "set_tempo" and tempo is None: tempo = 60e6 / m.tempo
            if m.type == "note_on" and m.velocity > 0 and getattr(m, "channel", 0) != 9: ons.append(t); vel.append(m.velocity); pit.append(m.note); open_[(m.channel, m.note)] = (t, m.note)
            elif (m.type == "note_off" or (m.type == "note_on" and m.velocity == 0)) and (m.channel, m.note) in open_:
                t0, nn = open_.pop((m.channel, m.note)); pc[nn % 12] += max(1, t - t0)
    if not ons: return None
    ons = np.array(ons); bpm = tempo or 120.0; end = ons.max() / tpb
    pos = (ons % tpb) / tpb; offb = float(np.mean(~((pos < .06) | (pos > .94))))
    s16 = np.unique((ons / (tpb / 4)).round().astype(int)); nbar = max(1, int(math.ceil(end / 4 + 1e-9)))
    rest = 1 - len(np.unique(s16 // 1)) / (nbar * 16.0)
    if pc.sum() == 0: pc = np.bincount(np.array(pit) % 12, minlength=12).astype(float)
    cm = max((np.corrcoef(np.roll(KS_MAJ, k), pc)[0, 1], k) for k in range(12)); cn = max((np.corrcoef(np.roll(KS_MIN, k), pc)[0, 1], k) for k in range(12))
    key_ = (NOTE_NAMES[cm[1]] + " major") if cm[0] >= cn[0] else (NOTE_NAMES[cn[1]] + " minor")
    return dict(n=len(ons), bpm=bpm, lo=min(pit), hi=max(pit), vel=float(np.mean(vel)), veldev=float(np.std(vel)), nps=len(ons) / max(1e-3, end * 60 / bpm),
                offbeat=offb, rest=max(0.0, rest), modeness=float(cm[0] - cn[0]), key=key_, pitch_mean=float(np.mean(pit)), bars=nbar)


NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def rule_vet_from_cues(st):
    """linear cue rule (grade C): mode, tempo, pitch, loudness, attack/density, offbeat -> V E T (Eerola 2013 / Hunter 2010 directions)"""
    mode = max(-1, min(1, st["modeness"] * 2)); tempo = max(-1, min(1, (st["bpm"] - 110) / 70)); pitch = max(-1, min(1, (st["pitch_mean"] - 60) / 24))
    loud = max(-1, min(1, (st["vel"] - 70) / 40)); dens = max(-1, min(1, (st["nps"] - 2.5) / 3)); off = max(-1, min(1, (st["offbeat"] - .35) / .3))
    V = .35 * mode + .25 * tempo + .20 * pitch + .05 * loud
    E = .35 * tempo + .30 * loud + .20 * dens + .10 * off
    T = -.25 * mode + .10 * tempo - .10 * pitch + .15 * loud + .20 * dens + .15 * off
    return tuple(round(max(-1, min(1, x * 1.6)), 3) for x in (V, E, T))


def pl_dissonance(freqs, amps):
    """Plomp-Levelt / Sethares sensory dissonance (grade L: published formula; numbers computed here)"""
    d = 0.0
    for i in range(len(freqs)):
        for j in range(i + 1, len(freqs)):
            f1, f2 = sorted((freqs[i], freqs[j])); s = .24 / (.0207 * f1 + 18.96)
            d += amps[i] * amps[j] * (math.exp(-3.5 * s * (f2 - f1)) - math.exp(-5.75 * s * (f2 - f1)))
    return d


def chord_dis(semis, root=261.63, nh=6):
    f, a = [], []
    for s in semis:
        for h in range(1, nh + 1): f.append(root * 2 ** (s / 12) * h); a.append(1.0 / h)
    return pl_dissonance(f, a)


def table_notes():
    header = ["Note Model item", "kind", "key / value", "n_files_or_count", "22 Characteristics Mapped", "V", "E", "T",
              "Axis/Feelings Mapped (nearest moods)", "Best Note Model to be used", "measured / looked-up numbers", "Evidence"]
    rows = []

    def add(item, kind, val, n, ax, vet, best, num, ev):
        ax = {a: r2(x) for a, x in ax.items()}; v, e, t = vet
        rows.append([item, kind, val, n, L.fmt_ax(ax), v, e, t, ", ".join(L.nearest_moods(v, e, t)), best, num, ev])

    # ---- tempo: every integer BPM 40..220
    gen_bpm = [(g, d["declared"]["bpm"]) for g, d in GEN.items() if d.get("declared", {}).get("bpm")]
    for bpm in range(40, 221):
        e = max(-1, min(1, (bpm - 115) / 65)); v = max(-1, min(1, .25 * (bpm - 105) / 60)); t = .15 * e
        beat = 60000 / bpm; swing = max(1.0, 3.5 - (bpm - 80) * (2.5 / 220)); name = next(n for lim, n in ((60, "Largo"), (76, "Adagio"), (108, "Andante"), (120, "Moderato"), (156, "Allegro"), (176, "Vivace"), (999, "Presto")) if bpm < lim)
        gl = [g for g, (lo, hi) in gen_bpm if lo <= bpm <= hi]
        ax = L.axes_from_vet(v, e, t); ax["movement"] = r2(min(1, bpm / 200)); ax["density"] = r2(min(1, .3 + bpm / 300))
        add("%d BPM" % bpm, "tempo", bpm, len(gl), ax, (r2(v), r2(e), r2(t)), "rhythm model: grid 16ths; swing ratio %.2f:1; density by tempo" % swing,
            "beat %.0f ms; 16th %.0f ms; JND 6 ms = %.1f%% of a 16th; tempo word %s; genres with this BPM: %s" % (beat, beat / 4, 600 / (beat / 4), name, ", ".join(gl[:8]) or "none"),
            "L (Eerola 2013 tempo beta; Hunter 2010 tempo > mode) + D (genre bpm) + R (placeholder scaling)")

    # ---- scales and modes
    SC = {"Ionian (major)": [0, 2, 4, 5, 7, 9, 11], "Dorian": [0, 2, 3, 5, 7, 9, 10], "Phrygian": [0, 1, 3, 5, 7, 8, 10], "Lydian": [0, 2, 4, 6, 7, 9, 11],
          "Mixolydian": [0, 2, 4, 5, 7, 9, 10], "Aeolian (natural minor)": [0, 2, 3, 5, 7, 8, 10], "Locrian": [0, 1, 3, 5, 6, 8, 10], "Harmonic minor": [0, 2, 3, 5, 7, 8, 11],
          "Melodic minor": [0, 2, 3, 5, 7, 9, 11], "Phrygian dominant": [0, 1, 4, 5, 7, 8, 10], "Lydian dominant": [0, 2, 4, 6, 7, 9, 10], "Altered": [0, 1, 3, 4, 6, 8, 10],
          "Major pentatonic": [0, 2, 4, 7, 9], "Minor pentatonic": [0, 3, 5, 7, 10], "Blues": [0, 3, 5, 6, 7, 10], "Whole tone": [0, 2, 4, 6, 8, 10], "Diminished (W-H)": [0, 2, 3, 5, 6, 8, 9, 11],
          "Chromatic": list(range(12)), "Hungarian minor": [0, 2, 3, 6, 7, 8, 11], "Double harmonic": [0, 1, 4, 5, 7, 8, 11], "Hirajoshi": [0, 2, 3, 7, 8], "In (Japanese)": [0, 1, 5, 7, 8],
          "Hexatonic (Ritusen)": [0, 2, 5, 7, 9], "Enigmatic": [0, 1, 4, 6, 8, 10, 11], "Bebop dominant": [0, 2, 4, 5, 7, 9, 10, 11]}
    bright = {"Lydian": .9, "Ionian (major)": .75, "Mixolydian": .65, "Dorian": .5, "Aeolian (natural minor)": .35, "Phrygian": .2, "Locrian": .05}
    for name, iv in SC.items():
        sh = sum(1 for a, b in zip(iv, iv[1:] + [12]) if b - a == 1); br = bright.get(name, .5 + .06 * (3 - sum(1 for i in iv if i in (3, 8, 10, 1))))
        modeness = (1 if 4 in iv else -1) * .5 + (.25 if 9 in iv else -.25 if 8 in iv else 0)
        v = max(-1, min(1, modeness + .2 * (br - .5))); e = .1 + .1 * (br - .5); t = .15 + .08 * sh + (.25 if name in ("Locrian", "Altered", "Enigmatic", "Diminished (W-H)", "Chromatic") else 0)
        ax = L.axes_from_vet(v, e, t); ax["harmonic_rhythm"] = .5; ax["tension"] = r2(.4 + t)
        add(name, "scale / mode", ",".join(map(str, iv)), len(iv), ax, (r2(v), r2(e), r2(t)), "note model: pitch-class set; weights tonic/dominant; avoid-note list per chord",
            "%d notes; %d semitone steps; brightness rank %.2f (Lydian brightest, Locrian darkest); thirds %s" % (len(iv), sh, br, "major" if 4 in iv else "minor" if 3 in iv else "none"),
            "L (Hunter 2010, Hong 2018: mode and V) + R (scale placement)")

    # ---- intervals 0..24 semitones with computed sensory dissonance
    d_all = [chord_dis([0, s]) for s in range(25)]; mx = max(d_all)
    NAMES = ["unison", "minor 2nd", "major 2nd", "minor 3rd", "major 3rd", "perfect 4th", "tritone", "perfect 5th", "minor 6th", "major 6th", "minor 7th", "major 7th", "octave"]
    for s in range(25):
        nm = NAMES[s] if s < 13 else NAMES[s - 12] + " + octave"; rough = d_all[s] / mx
        v = r2(.6 - 1.4 * rough) if s not in (0, 12, 24) else .3; t = r2(-.3 + 1.5 * rough); e = r2(.1 * rough)
        ax = L.axes_from_vet(v, e, t); ax["roughness"] = r2(rough); ax["tension"] = r2(.3 + .6 * rough)
        add("interval %d (%s)" % (s, nm), "interval", s, 2, ax, (v, e, t), "note model: harmony weights (consonant intervals on strong beats; use dissonant ones as passing)",
            "Plomp-Levelt dissonance %.3f (rel. %.2f) with 6 harmonics at 1/n on C4" % (d_all[s], rough), "L (Plomp-Levelt 1965 / Sethares curve, computed)")

    # ---- chord qualities
    CQ = {"major": [0, 4, 7], "minor": [0, 3, 7], "diminished": [0, 3, 6], "augmented": [0, 4, 8], "sus2": [0, 2, 7], "sus4": [0, 5, 7], "power (5)": [0, 7], "6": [0, 4, 7, 9], "m6": [0, 3, 7, 9],
          "maj7": [0, 4, 7, 11], "7 (dominant)": [0, 4, 7, 10], "m7": [0, 3, 7, 10], "m(maj7)": [0, 3, 7, 11], "m7b5 (half-dim)": [0, 3, 6, 10], "dim7": [0, 3, 6, 9], "7sus4": [0, 5, 7, 10],
          "maj9": [0, 4, 7, 11, 14], "9": [0, 4, 7, 10, 14], "m9": [0, 3, 7, 10, 14], "add9": [0, 4, 7, 14], "madd9": [0, 3, 7, 14], "11": [0, 4, 7, 10, 14, 17], "m11": [0, 3, 7, 10, 14, 17],
          "13": [0, 4, 7, 10, 14, 21], "maj13": [0, 4, 7, 11, 14, 21], "7b9": [0, 4, 7, 10, 13], "7#9": [0, 4, 7, 10, 15], "7b5": [0, 4, 6, 10], "7#5": [0, 4, 8, 10], "7#11": [0, 4, 7, 10, 18],
          "maj7#11": [0, 4, 7, 11, 18], "7alt": [0, 4, 8, 10, 13], "6/9": [0, 4, 7, 9, 14], "quartal": [0, 5, 10], "cluster (0,1,2)": [0, 1, 2], "mu chord": [0, 2, 4, 7]}
    dd = {k: chord_dis(v) for k, v in CQ.items()}; mxc = max(dd.values())
    for name, iv in CQ.items():
        rough = dd[name] / mxc; maj = 4 in iv; mino = 3 in iv
        v = r2(.45 * maj - .5 * mino - .9 * rough + .2); e = r2(.1 * rough + .05 * len(iv) / 6); t = r2(-.4 + 1.6 * rough)
        ax = L.axes_from_vet(v, e, t); ax["roughness"] = r2(rough); ax["tension"] = r2(.25 + .7 * rough); ax["fullness"] = r2(min(1, len(iv) / 6))
        add("chord " + name, "chord quality", ",".join(map(str, iv)), len(iv), ax, (v, e, t), "note model: voicing from quality + register; resolve tension chords (dom7/dim/aug/alt) to stable chords",
            "Plomp-Levelt dissonance %.3f (rel. %.2f); %d notes" % (dd[name], rough, len(iv)), "L (published formula, computed) + R (V/E/T scaling)")

    # ---- registers: every MIDI pitch
    for p in range(21, 109):
        hz = 440 * 2 ** ((p - 69) / 12); band = next(n for lim, n in ((60, "sub"), (250, "bass"), (500, "low-mid"), (2000, "mid"), (5000, "upper-mid"), (10000, "brilliance"), (99999, "air")) if hz < lim)
        pv = (p - 60) / 24; v = r2(.25 * max(-1, min(1, pv))); e = r2(.12 * max(-1, min(1, pv))); t = r2(-.1 * max(-1, min(1, pv)) + (.15 if p > 96 or p < 28 else 0))
        ax = {"pitch": r2((p - 21) / 87), "brightness": r2(min(1, hz / 4000)), "weight": r2(max(0, 1 - hz / 500))}
        role = "kick/sub bass" if p < 36 else "bass" if p < 52 else "low harmony / pads" if p < 64 else "melody / chords" if p < 84 else "bells / top lines" if p < 100 else "air / sparkle"
        add("MIDI %d (%s%d)" % (p, NOTE_NAMES[p % 12], p // 12 - 1), "register", p, 1, ax, (v, e, t), "note model register choice: " + role, "%.1f Hz; band %s; ERB %.0f Hz" % (hz, band, 24.7 * (4.37 * hz / 1000 + 1)),
            "L (Hong 2018, Eerola 2013: higher pitch -> V up, E up) + computed (Hz, ERB)")

    # ---- velocity: every value
    for vv in range(1, 128, 1):
        db = 40 * math.log10(vv / 127); e = r2((vv - 64) / 64 * .8); v = r2((vv - 64) / 64 * .05); t = r2((vv - 64) / 64 * .25 + (.1 if vv < 20 else 0))
        ax = {"loudness": r2(vv / 127), "attack": r2(.4 + .5 * vv / 127), "sharpness": r2(.3 + .5 * vv / 127)}
        add("velocity %d" % vv, "velocity", vv, 1, ax, (v, e, t), "note model: velocity curve per part (accent on beat 1/3, ghost notes 20-45)", "approx %.1f dB re full-scale velocity (40*log10(v/127))" % db,
            "L (Eerola 2013: loudness beta largest for E) + R")

    # ---- rest ratio / syncopation / swing / micro-timing / meters / articulation
    for rr in range(0, 100, 5):
        e = r2(.5 - rr / 100 * 1.2); t = r2(-.1 + rr / 100 * .3); v = 0.0
        ax = {"density": r2(1 - rr / 100), "movement": r2(.8 - rr / 140), "decay": r2(.3 + rr / 150)}
        add("rest ratio %d%%" % rr, "silence (rest share of 16th slots)", rr, 1, ax, (v, e, t), "note model: rest pattern; keep breath rests 1 beat, drop-out 0.5-2 s, full stop >2 s dramatic",
            "target ranges: vocal activity 60-80%; kick 68% (De Man); pads 84-99%", "L (Margulis 2007 context effect; De Man 2014) + R")
    for sy in range(0, 101, 10):
        e = r2(-.3 + sy / 100 * 0.9) if sy <= 60 else r2(.24 - (sy - 60) / 40 * .4); ax = {"density": r2(.4 + sy / 250), "movement": r2(sy / 100), "contour": r2(.3 + sy / 200)}
        add("syncopation %d%%" % sy, "syncopation (off-beat onset share)", sy, 1, ax, (r2(.2 * math.sin(sy / 100 * math.pi)), e, r2(.1 + sy / 400)), "rhythm model: place accents off the beat; groove peaks at medium syncopation",
            "Witek 2014 inverted-U: groove/pleasure max at medium syncopation, falls at high", "L (Witek 2014) + R")
    for sw in (1.0, 1.2, 1.5, 1.8, 2.0, 2.2, 2.5, 3.0, 3.5):
        ax = {"movement": r2(.9 - abs(sw - 2) * .15), "attack": .5}
        add("swing %.1f:1" % sw, "swing ratio (beat : upbeat of 8th pair)", sw, 1, ax, (r2(.15 if 1.5 <= sw <= 2.5 else 0), r2(.15), 0.0), "rhythm model: delay every 2nd 8th; ratio falls as tempo rises (3.5:1 at slow, 1:1 above ~200 bpm)",
            "triplet feel = 2:1; straight = 1:1; JND of onset shift ~6 ms", "L (Friberg & Sundstrom 2002) + R")
    for ms in (0, 3, 6, 10, 15, 20, 30, 45):
        ax = {"movement": r2(.4 + ms / 100), "attack": r2(.7 - ms / 120)}
        add("micro-timing +-%d ms" % ms, "micro-timing spread", ms, 1, ax, (r2(.1 if ms <= 15 else -.1), r2(-.05), r2(.03 * ms / 10)), "rhythm model: humanise per part; keep kick/bass tight (<=6 ms), hats/pads looser (10-20 ms)",
            "below ~6 ms is not heard as timing; 10-30 ms reads as laid-back / loose", "L (Madison 2011; Iyer 2002 microtiming) + R")
    for m in ("2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8", "5/8", "7/4", "11/8", "15/16"):
        n_, d_ = map(int, m.split("/")); odd = n_ in (5, 7, 11, 15)
        add("meter " + m, "meter", m, n_, {"movement": r2(.5 + .1 * odd), "harmonic_rhythm": .5}, (r2(.1 if not odd else -.05), r2(.05 + .1 * odd), r2(.1 * odd)),
            "rhythm model: accent grid for " + m, "%d beats of 1/%d; typical groups %s" % (n_, d_, "3+2/2+3" if n_ == 5 else "2+2+3/3+2+2" if n_ == 7 else "3+3 or 2+2+2" if m == "6/8" else "4 beats" if m == "4/4" else "-"), "R")
    for art, ratio in (("legato", 1.0), ("tenuto", .9), ("normal", .75), ("detached", .6), ("staccato", .4), ("staccatissimo", .2)):
        v = {"legato": -.1, "tenuto": -.05, "normal": 0, "detached": .05, "staccato": .14, "staccatissimo": .15}[art]; e = {"legato": -.15, "tenuto": -.1, "normal": 0, "detached": .1, "staccato": .2, "staccatissimo": .2}[art]
        add("articulation " + art, "articulation (note length / IOI)", ratio, 1, {"decay": r2(ratio), "attack": r2(1 - ratio * .4)}, (v, e, r2(.05 * (1 - ratio))), "note model: note length = ratio x inter-onset interval",
            "Eerola 2013 articulation beta: happy +.14 (shorter), sad -.18, peaceful -.10", "L (Eerola 2013) + R")

    # ---- genres
    for g, d in GEN.items():
        dc = d.get("declared") or {}; p = dc.get("pillars")
        if not p: continue
        v = r2(p["valence"] * 2 - 1); e = r2(p["energy"] * 2 - 1); t = r2((1 - p["valence"]) * p["energy"])
        ax = L.axes_from_vet(v, e, t); ax["brightness"] = p["brightness"]
        add("genre " + g, "genre card", "bpm %s swing %s" % (dc.get("bpm"), dc.get("swing")), d["evidence"]["n_rows"] if d.get("evidence") else 0, ax, (v, e, t),
            "genre model: groove %s; drums %s" % (dc.get("groove"), ";".join("%s=%s" % (k, x) for k, x in (dc.get("drums") or {}).items() if x)[:120]),
            "pillars danceability %.2f energy %.2f valence %.2f brightness %.2f contrast %.2f" % (p["danceability"], p["energy"], p["valence"], p["brightness"], p["contrast"]), "D (hand-written catalog card, not measured)")

    # ---- MIDI files: measured stats + labels
    def midi_rows(paths, kind, label_fn, best):
        done = 0; vr, wr = [], []
        for pth in paths:
            st = midi_stats(pth)
            if not st: continue
            lab = label_fn(pth)
            if not lab: continue
            name, words, vet_lab, extra = lab
            v, e, t = rule_vet_from_cues(st)
            if vet_lab: vr.append((vet_lab[0], v)); wr.append((vet_lab[1], e))
            vv = vet_lab or (v, e, t)
            ax = L.axes_from_vet(*vv); ax["pitch"] = r2((st["pitch_mean"] - 21) / 87); ax["density"] = r2(min(1, st["nps"] / 8)); ax["movement"] = r2(min(1, st["bpm"] / 200))
            ax["loudness"] = r2(st["vel"] / 127)
            num = "bpm %.0f; notes %d; pitch %d-%d; vel %.0f; notes/beat %.2f; offbeat %.0f%%; rests %.0f%%; key %s; majorness %+.2f; rule-VET (%.2f,%.2f,%.2f)" % (
                st["bpm"], st["n"], st["lo"], st["hi"], st["vel"], st["nps"], st["offbeat"] * 100, st["rest"] * 100, st["key"], st["modeness"], v, e, t)
            rows.append([name, kind, extra, 1, L.fmt_ax(ax), r2(vv[0]), r2(vv[1]), r2(vv[2]), ", ".join(L.nearest_moods(*vv)), best, num,
                         "D (label from file/dataset) + M (measured from the MIDI)" if vet_lab else "M (measured) + R"])
            done += 1
        return done, vr, wr

    # chord progressions (names carry a key, numerals and human mood words)
    unknown = collections.Counter(); chord_files = sorted(glob.glob(os.path.join(MIDI, "chord_progressions/**/*.mid"), recursive=True)) + sorted(glob.glob(os.path.join(MIDI, "modal_chord_progressions/**/*.mid"), recursive=True))
    def chord_label(p):
        stem = os.path.splitext(os.path.basename(p))[0]; parts = [x.strip() for x in stem.split(" - ")]
        folder = os.path.relpath(p, MIDI).split(os.sep)[:2]
        if len(parts) >= 3: key_, prog, mood = parts[0], parts[1], parts[-1]
        else: key_, prog, mood = "", stem, ""
        words = re.findall(r"[A-Za-z]+", mood)
        for w in words:
            if w not in L.MOOD: unknown[w] += 1
        vet = L.words_vet(words)
        toks = prog.split(); cad = " ".join(toks[-2:]) if len(toks) >= 2 else prog
        flags = ",".join(f for f, rx in (("7th", r"7|9|13"), ("sus", "sus"), ("dim/aug", r"dim|aug|°|\+"), ("borrowed/chromatic", r"[b#]"), ("slash", "/")) if re.search(rx, prog))
        return stem, words, vet, "%s | %s | %s | %d chords | cadence %s | %s | words: %s" % (folder[0].replace("_", " "), key_ or "key?", prog, len(toks), cad, flags or "diatonic triads", mood)
    n1, vr1, wr1 = midi_rows(chord_files, "chord progression", chord_label, "chord model: progression in roman numerals (transpose to key); voicing by register; cadence type per mood")
    print("chord progression files", len(chord_files), "rows", n1, "unknown mood words", len(unknown), unknown.most_common(40))

    # EMOPIA
    emo = sorted(glob.glob(os.path.join(MIDI, "emopia_piano_emotions/EMOPIA_1.0/midis/*.mid")))
    QV = {"1": (.65, .65, -.10), "2": (-.60, .65, .65), "3": (-.65, -.60, .10), "4": (.60, -.60, -.50)}
    def emo_label(p):
        q = os.path.basename(p)[1]; return os.path.basename(p)[:-4], [], QV.get(q), "EMOPIA quadrant Q%s (Q1 +V+A, Q2 -V+A, Q3 -V-A, Q4 +V-A)" % q
    n2, vr2, wr2 = midi_rows(emo, "emotion-labelled piano clip (EMOPIA)", emo_label, "note model training row: clip with human emotion quadrant")

    # VGMIDI
    vg = []
    try:
        for r in csv.DictReader(open(os.path.join(MIDI, "vgmidi_game_tracks/vgmidi_labelled.csv"))): vg.append(r)
    except Exception: pass
    vgd = {os.path.join(MIDI, "vgmidi_game_tracks/labelled/midi", os.path.basename(r["midi"])): r for r in vg}
    def vg_label(p):
        r = vgd[p]; return "%s - %s" % (r["game"], r["piece"]), [], (float(r["valence"]) * .7, float(r["arousal"]) * .7, 0.0), "VGMIDI phrase valence %s arousal %s" % (r["valence"], r["arousal"])
    n3, vr3, wr3 = midi_rows([p for p in vgd if os.path.exists(p)], "emotion-labelled game phrase (VGMIDI)", vg_label, "note model training row: phrase with human valence/arousal")

    # Groove MIDI drum loops
    gi = os.path.join(MIDI, "groove_drumset/groove/info.csv"); n4 = 0
    if os.path.exists(gi):
        for r in csv.DictReader(open(gi)):
            ax = {"movement": r2(min(1, float(r["bpm"]) / 200)), "density": .5}
            rows.append(["groove %s" % r["id"], "drum groove (Groove MIDI)", "%s | %s bpm | %s | %s" % (r.get("style", ""), r["bpm"], r.get("beat_type", ""), r.get("time_signature", "")), 1, L.fmt_ax(ax),
                         0.0, r2(max(-1, min(1, (float(r["bpm"]) - 115) / 65))), 0.0, "", "drum model: human-played groove (velocity + micro-timing), style %s" % r.get("style", ""),
                         "bpm %s; duration %s s; split %s" % (r["bpm"], r.get("duration", ""), r.get("split", "")), "D (dataset labels)"]); n4 += 1
    print("midi rows: chord %d, emopia %d, vgmidi %d, groove %d" % (n1, n2, n3, n4))
    # how well does the cue rule agree with the human labels?
    for nm, vr in (("chord V", vr1), ("chord E", wr1), ("emopia V", vr2), ("emopia E", wr2), ("vgmidi V", vr3), ("vgmidi E", wr3)):
        if len(vr) > 10:
            a = np.array(vr); print("agreement rule vs label, %s: r=%.3f over %d" % (nm, np.corrcoef(a[:, 0], a[:, 1])[0, 1], len(a)))
    save("3_NoteModel", header, rows)


# =====================================================================================================================
# 4. MIX
# =====================================================================================================================
BANDS = [20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000]
# role: (body_lo, body_hi, presence_Hz, air_weight, pan, level_dB, send_dB, hpf, ratio, notes)
ROLES = {
    "kick": (45, 120, 3500, .02, "C", -13, -40, 30, "4:1 fast", "beater click 2-5k"), "bass": (50, 250, 1000, .0, "C", -6, -40, 30, "3:1 slow", "definition 700-1.5k; mono below 150"),
    "snare": (150, 400, 4400, .08, "C or +-5", -8, -12, 80, "4:1 fast", "crack 3-6k"), "clap": (500, 1500, 2500, .10, "C", -10, -10, 150, "3:1", "room send"),
    "hats": (6000, 12000, 9000, .30, "+-20..60", -12, -22, 300, "none", "high shelf only"), "cymbals": (4000, 12000, 8000, .40, "+-40..80", -14, -18, 250, "none", "wash"),
    "toms/perc": (80, 400, 3000, .10, "spread", -11, -14, 60, "3:1", ""), "lead synth": (250, 2000, 5000, .08, "C / near", -5, -10, 150, "2:1", "presence 4-8k"),
    "pad": (150, 1500, 3000, .15, "wide", -8, -6, 120, "2:1 slow", "low presence, wide"), "pluck/arp": (400, 3000, 5000, .10, "off-centre", -9, -10, 200, "none", ""),
    "piano/keys": (150, 2500, 4000, .10, "C or +-20", -7, -14, 80, "2:1", "attack 3-5k"), "guitar": (100, 1500, 3500, .08, "+-30 doubled", -7, -14, 80, "3:1", "pick 3-5k"),
    "vocal/choir": (200, 1800, 4000, .15, "C (lead), wide (choir)", -3, -15, 100, "3:1", "lead ~ -2.7 LU re mix; air 10k+"), "strings": (150, 2500, 3500, .10, "wide", -8, -8, 100, "2:1 slow", ""),
    "brass/woodwind": (250, 3000, 3500, .08, "off-centre", -6, -10, 120, "3:1", ""), "texture/FX": (2000, 12000, 8000, .50, "wide", -12, -4, 200, "none", "air layer"),
}


def band_share(role, f):
    lo, hi, pres, air, *_ = ROLES[role]; mid = math.sqrt(lo * hi); sig = max(.45, math.log2(hi / lo) / 2.4)
    body = math.exp(-0.5 * (math.log2(f / mid) / sig) ** 2); p = .35 * math.exp(-0.5 * (math.log2(f / pres) / .45) ** 2); a = air * (1 / (1 + math.exp(-(math.log2(f / 9000)) * 2)))
    return body + p + a


def table_mix():
    header = ["Mix item", "kind", "subject / band", "numbers", "22 Characteristics Mapped", "V", "E", "T", "Axis/Feelings Mapped", "Mix Model (action)", "Cluster Mapping (which instrument families it applies to)", "Evidence"]
    rows = []
    FEEL = [(60, "sub: felt not heard, speaker load", "weight, loudness", "high-pass non-bass below 30-40 Hz"), (120, "boom / foundation", "weight, warmth", "shelf -2 dB if boomy; keep kick/bass split"),
            (300, "warmth / body (mud above)", "warmth, fullness", "cut 2-4 dB at 200-300 Hz on pads/keys if muddy"), (600, "boxy / hollow", "fullness", "cut 400-600 Hz on boxy sources"),
            (2000, "honk / nasal / vowel region", "vowel, fullness", "notch if nasal; this is the formant region"), (5000, "presence / attack / harsh (ear most sensitive)", "sharpness, roughness, attack", "dip 2-4 dB on pads to make room for leads; add on dull sources"),
            (9000, "brilliance / sibilance / sizzle", "brightness, noisiness", "de-ess 6-8k; shelf up for sparkle"), (20001, "air / openness / noise floor", "brightness, space", "shelf +1-3 dB at 12k for air; low-pass texture to taste")]
    share = {r: [band_share(r, f) for f in BANDS] for r in ROLES}
    tot = [sum(share[r][i] for r in ROLES) for i in range(len(BANDS))]
    for i, f in enumerate(BANDS):
        feel = next(x for x in FEEL if f < x[0]); owner = max(ROLES, key=lambda r: share[r][i]); crowd = [r for r in ROLES if share[r][i] / max(1e-9, tot[i]) > .12]
        ax = {"brightness": r2(min(1, f / 8000)), "weight": r2(max(0, 1 - f / 600))}
        rows.append(["%g Hz third-octave" % f, "frequency band", "ISO 1/3 octave", "ERB %.0f Hz; bark %.1f; role owning the band %s; crowded by %s" % (24.7 * (4.37 * f / 1000 + 1), 13 * math.atan(.00076 * f) + 3.5 * math.atan((f / 7500) ** 2), owner, ", ".join(crowd) or "none"),
                     L.fmt_ax(ax), 0, 0, 0, "", feel[3], "all families; owner role above", "L (ERB Glasberg & Moore 1990) + R (role slot model)"])
    for r, row in ROLES.items():
        for i, f in enumerate(BANDS):
            s = share[r][i] / sum(share[r]); frac = share[r][i] / tot[i]
            rows.append(["%s @ %g Hz" % (r, f), "role x band energy share", r, "share of this role's energy %.1f%%; role's share of the band %.0f%%; %s" % (s * 100, frac * 100, "OWNS band" if frac > .35 else "contested" if frac > .15 else "minor"),
                         "", 0, 0, 0, "", ("keep and protect" if frac > .35 else "carve with EQ/side-chain if another role owns it" if frac > .15 else "can high/low-pass here"), r, "R (gaussian slot model of B table 3.4; replace with per-stem band energy from our renders)"])
    names = list(ROLES)
    for a in range(len(names)):
        for b in range(a + 1, len(names)):
            ov = sum(min(share[names[a]][i] / sum(share[names[a]]), share[names[b]][i] / sum(share[names[b]])) for i in range(len(BANDS)))
            worst = max(range(len(BANDS)), key=lambda i: min(share[names[a]][i] / sum(share[names[a]]), share[names[b]][i] / sum(share[names[b]])))
            fix = "separate in time (side-chain duck) or pan; one gets the sub" if ov > .6 else "EQ carve at %g Hz or pan apart" % BANDS[worst] if ov > .35 else "no action"
            rows.append(["%s vs %s" % (names[a], names[b]), "masking pair", "overlap", "overlap %.2f (sum_b min(Ea,Eb)/sum Ea, B formula); worst band %g Hz" % (ov, BANDS[worst]), "", 0, 0, 0, "", fix, "%s; %s" % (names[a], names[b]), "R + L (B 3.4 overlap formula)"])
    # role x genre
    for g, d in GEN.items():
        p = (d.get("declared") or {}).get("pillars")
        if not p: continue
        en, dn, val, br, ct = p["energy"], p["danceability"], p["valence"], p["brightness"], p["contrast"]
        lufs = -16 + 9 * en; crest = 14 - 7 * en; sm = .1 + .9 * (1 - dn) * (1 - .5 * en); rt60 = .4 + 3.5 * (1 - en) * (1 - dn)
        rows.append(["genre master: " + g, "genre mix target", g, "LUFS %.1f; crest %.1f dB; side/mid %.2f; low-band (<150 Hz) corr >0.95; RT60 %.1f s; spectral tilt bright %.2f" % (lufs, crest, sm, rt60, br), "", r2(val * 2 - 1), r2(en * 2 - 1), 0, "",
                     "master chain: limiter to %.1f LUFS; keep crest >= %.1f dB; width side/mid %.2f" % (lufs, crest, sm), "all", "R (interpolates dance -6.2..-11.3 LUFS, pop S/M 0.1-0.4, ambient 0.3-1.0 from B 3.1/3.3; replace with measured Harmonix/our renders)"])
        for r, row in ROLES.items():
            lo, hi, pres, air, pan, lvl, send, hpf, ratio, note = row
            drumsbass = r in ("kick", "bass", "snare", "clap")
            lvl_g = lvl + (3 * (en - .5) if drumsbass else 1.5 * (br - .5) if r in ("hats", "cymbals", "lead synth") else 0)
            send_g = send + 8 * (1 - en) - 4 * dn + (-3 if drumsbass else 0)
            wide = r in ("pad", "strings", "texture/FX", "hats", "cymbals") or "wide" in pan
            rows.append(["%s in %s" % (r, g), "role x genre", g, "level %+.1f dB; reverb send %+.1f dB; RT60 %.1f s; pan %s%s; HPF %d Hz; comp %s" % (lvl_g, send_g, rt60, pan, " (widen %.0f%%)" % (100 * min(1, sm * 1.4)) if wide else "", hpf, ratio),
                         "", r2(val * 2 - 1), r2(en * 2 - 1), 0, "", "set gain/send/pan; %s" % note, r, "R (role table B 3.4 shifted by genre pillars; grade C)"])
    SEC = [("intro", "4-8 bars", .30, "1-3 layers", "filter closed, sparse", "T low"), ("verse", "8-16", .40, "3-5", "full range, sparser top", "E low-mid"),
           ("pre-chorus", "4-8", .55, "4-6", "rising filter/riser", "T up"), ("build-up", "8-16 (8 typical)", .60, "+1 layer per 1-2 bars", "HPF sweep up, snare roll 1/4->1/8->1/16->1/32, noise riser", "T up, E up"),
           ("drop / chorus", "16-32 (EDM) / 8-16 (pop)", 1.0, "6-10", "full range, kick returns after 1 beat-1 bar drop-out", "E max, T release; LUFS +2..5 LU"),
           ("breakdown", "8-16", .30, "1-3", "kick out, pads, filter closed", "E down, V steady"), ("bridge", "8", .40, "2-4", "key/instrument change, dip", "E 0.3-0.5"), ("outro", "4-16", .40, "3 -> 1", "layers removed, reverb tail", "E down")]
    for nm, bars, en, lay, dev, mood in SEC:
        for metric, val in (("bars", bars), ("energy E (0-1)", en), ("layers", lay), ("device", dev), ("mood motion", mood), ("loudness step vs previous", "2-5 LU at boundary; centroid +-500..1500 Hz")):
            rows.append(["%s: %s" % (nm, metric), "section arc", nm, str(val), "", 0, r2(en * 2 - 1), 0, "", "arrangement/automation rule for %s" % nm, "all", "L/B (B 5.1, 5.2: pop and EDM section statistics)"])
    save("4_Mix", header, rows)


# =====================================================================================================================
# 5. NODES / KNOBS
# =====================================================================================================================
def table_nodes():
    D = json.load(open(os.path.join(ROOT, "repo/tools/universe/describe_all.json")))["types"]
    NOTE_AX = {"Arpeggiator": "density, movement", "Chord": "fullness, harmonic_rhythm, tension", "Scale": "pitch set (mode)", "Transposer": "pitch", "Step Sequencer": "density, movement"}
    header = ["Node", "category", "Knob", "kind", "min", "max", "modulatable", "measured", "22 Characteristics Mapped (signed effect of raising the knob)", "V", "E", "T", "Axis/Feelings Mapped (what raising it feels like)",
              "Best Model to be used", "activity (mean |slope|)", "Evidence"]
    rows = []; AUDIO_CATS = {"Synths", "AudioEffects", "Utility", "Notes", "Prediction", "Modulators", "Macros", "Source"}
    effm = MEAS["effects"]; hit_syn = hit_eff = 0
    for t in D:
        node, cat = t["type"], t["category"]
        if cat in ("3D", "Compositing", "Effects"): model = "visual / not in the sound model"
        elif cat == "Synths": model = "sound model (recipe)"
        elif cat == "AudioEffects": model = "effects/chain model"
        elif cat == "Notes": model = "note model"
        elif cat == "Prediction": model = "learned modulator (prediction)"
        elif cat in ("Modulators", "Macros"): model = "modulation layer (feel -> knob)"
        elif cat == "Source": model = "visual or sample source"
        else: model = "utility / mix model"
        mod = {m.get("key") or m.get("label") for m in t.get("modulatable", [])}
        for r in t["rows"]:
            k = r["key"]; kind = r.get("kind")
            meas = None; slope = None; act = None; ev = "R"
            eng = node if node in MEAS["synth"] else None
            if eng:
                for x in MEAS["synth"][eng].get("knobs", []):
                    if x["knob"] == k and x.get("slope"): slope = x["slope"]; act = x["activity"]; ev = "M (one-knob pairs, own sweep)"; meas = "yes"; hit_syn += 1
            else:
                for key in ("%s|%s" % (node, k), "%s|p.%s" % (node, k)):
                    if key in effm:
                        d = effm[key]; meas = "yes"; hit_eff += 1; ev = "M (single-knob response, 3 settings)"
                        slope = {"brightness": d["brightness_oct"], "loudness": d["loudness_db"], "noisiness": d["noisiness_d"], "width": d["width_d"], "weight": d["weight_d"]}
                        slope = {a: (None if (isinstance(x, float) and math.isnan(x)) else x) for a, x in slope.items()}; break
            if slope:
                items = sorted([(a, x) for a, x in slope.items() if x is not None], key=lambda z: -abs(z[1]) if eng is not None else -abs(z[1]) / (4 if z[0] == "loudness" else 1))
                axs = "; ".join("%s %+.2f" % (a, x) for a, x in items[:6]); unit = "" if eng else " (brightness in octaves, loudness in dB, others raw delta)"
                vet = L.vet_from_axes({a: .5 + (x / (6 if eng is None and a in ("loudness",) else 2.5 if eng is None and a == "brightness" else 1)) / 2 for a, x in slope.items() if x is not None and a in L.AX_VET})
                axs += unit
            else:
                lk = k.lower(); eff = {}
                if cat in AUDIO_CATS and kind in ("float", "int", "pitch", "lin", "log", "Hz", None):
                    for words, e in L.KNOB_RULES:
                        if any(w == lk or lk.startswith(w) or lk.endswith(w) or w in lk for w in words if len(w) > 2) or lk in words:
                            for a, x in e.items(): eff[a] = eff.get(a, 0) + x
                axs = "; ".join("%s %+.1f" % (a, x) for a, x in sorted(eff.items(), key=lambda z: -abs(z[1]))[:5]) if eff else ("no rule match" if cat in AUDIO_CATS else "n/a (not audio)")
                vet = L.vet_from_axes({a: .5 + x * .25 for a, x in eff.items() if a in L.AX_VET}) if eff else (0.0, 0.0, 0.0)
                if eff: ev = "R (keyword rule; unmeasured)"
                else: ev = "none"
            v, e, tt = vet
            rows.append([node, cat, k, kind, r.get("min"), r.get("max"), "yes" if k in mod else "no", meas or "no", axs, v, e, tt, ", ".join(L.nearest_moods(v, e, tt)) if (v or e or tt) else "", model,
                         None if act is None else r2(act), ev])
    print("knob rows measured: synth %d, effect %d" % (hit_syn, hit_eff))
    save("5_NodesKnobs", header, rows)


def clean(x):
    if x is None: return ""
    if isinstance(x, (np.floating, float)): return "" if not math.isfinite(float(x)) else float(x)
    if isinstance(x, (np.integer,)): return int(x)
    if isinstance(x, str): return re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", "", x)[:32000]
    return x if isinstance(x, (int, float, bool)) else str(x)


def write_xlsx():
    from openpyxl import Workbook
    wb = Workbook(write_only=True)
    for name, (header, rows) in TABLES.items():
        ws = wb.create_sheet(name[:31]); ws.append(header)
        for r in rows: ws.append([clean(x) for x in r])
    wb.save(os.path.join(OUT, "feel_dataset.xlsx"))


if __name__ == "__main__":
    which = sys.argv[1:] or ["i", "s", "n", "m", "k"]
    if "i" in which: table_instruments()
    if "s" in which: table_sounds()
    if "n" in which: table_notes()
    if "m" in which: table_mix()
    if "k" in which: table_nodes()
    if len(which) == 5: write_xlsx()
