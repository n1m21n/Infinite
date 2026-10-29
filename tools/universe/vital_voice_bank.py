#!/usr/bin/env python3
"""Vital presets -> Wavetable voices for Generate Samples (owner, 2026-10-30: "data is gold and a slight variation
can give many results"; variety was 4-7 designed voices per part). Each of the 794 presets in vital_dsp_dataset.json
becomes one row: its two oscillators (the Vital wave matched to the closest of Infinite's 64 tables x 8 frames by
harmonic spectrum), unison count and spread, transpose and level, filter 1 (type, cutoff, resonance, the env_2/env_3
sweep onto its cutoff in octaves) and the amp envelope. Rows keep Vital's own style label (Bass, Keys, Lead, Pad,
Sequence, Percussion). Generate Samples draws a row of the part's style and jitters it, so a take is a real preset's
shape played by Infinite's synth.
Diva (--diva): u-he Diva's five oscillator models do not share knobs, so its rows are fitted to the presets' own
renders instead (training-data/audio/preset_diva*.jsonl: the real plugin playing C3 for 3 s, 32 log bands): the wave,
octave and low-pass that give the closest band shape, the amp envelope from the measured attack, decay and held level,
the second oscillator's interval from the preset's tunes. Roles come from the preset names (BS, LD, KY, PD, PL, ...).
    vital_voice_bank.py --spectra wt_spectra.txt [--vital FILE] [--diva] [--out FILE] [--stats]
--diva needs numpy (~/Infinite/ml-venv/bin/python3).
wt_spectra.txt: one line per Infinite table x frame, `table frame "name" h1..h32` (harmonic magnitudes, mip 0); made by
a 20-line program over src/audio/Wavetable.cpp (Frame()). The Vital file is 646 MB: it is read one preset at a time."""
import argparse, base64, json, math, os, re, struct

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_VITAL = os.path.expanduser("~/Infinite/musical_datasets/3_dsp_synthesis_schemas/vital_dsp_dataset.json")
DEFAULT_OUT = os.path.join(HERE, "..", "..", "src", "app", "graph", "GenerateSamplesVoices.inc")
STYLES = ["Bass", "Keys", "Lead", "Pad", "Sequence", "Percussion"]
H = 32


def presets(path):
    """Yield one preset dict at a time from the top-level JSON array, without loading the whole file."""
    dec, buf, pos = json.JSONDecoder(), "", 0
    with open(path) as f:
        while True:
            chunk = f.read(1 << 22)
            buf = buf[pos:] + chunk; pos = 0
            while True:
                m = re.compile(r"[\s,\[]*").match(buf, pos); pos = m.end()
                if pos >= len(buf) or buf[pos] == "]": break
                try:
                    obj, end = dec.raw_decode(buf, pos)
                except json.JSONDecodeError:
                    break # the object runs past the chunk: read more
                yield obj; pos = end
            if not chunk: return


def harmonics(wave):
    n = len(wave); out = []
    for k in range(1, H + 1):
        re_ = sum(wave[i] * math.cos(2 * math.pi * k * i / n) for i in range(0, n, 2))
        im = sum(wave[i] * math.sin(2 * math.pi * k * i / n) for i in range(0, n, 2))
        out.append(math.hypot(re_, im))
    mx = max(out) or 1e-12
    return [h / mx for h in out]


def logspec(h):
    v = [math.log10(max(x, 1e-4)) for x in h]   # -4..0: the shape of the spectrum, quiet harmonics count
    m = sum(v) / len(v); return [x - m for x in v]


def osc_wave(s, i):
    """The Vital oscillator's wave at its frame: the wavetable's first Wave Source keyframe nearest wave_frame."""
    try:
        comps = s["wavetables"][i]["groups"][0]["components"]
    except (KeyError, IndexError):
        return None
    frame = s.get("osc_%d_wave_frame" % (i + 1), 0.0)
    for c in comps:
        kf = [k for k in c.get("keyframes", []) if "wave_data" in k]
        if kf:
            k = min(kf, key=lambda k: abs(k.get("position", 0) - frame))
            raw = base64.b64decode(k["wave_data"])
            return list(struct.unpack("<%df" % (len(raw) // 4), raw))
    return None

DIVA = os.path.expanduser("~/Infinite/musical_datasets/3_dsp_synthesis_schemas/diva_analog_presets/diva_dsp_full_dataset.json")
DIVA_RENDERS = os.path.expanduser("~/Infinite/training-data/audio")
# Name codes the Diva banks use, to a style. Names carry them first ("BS Deep Space", "02867_PD - Discovery").
DIVA_CODES = {"Bass": ("BS", "BASS", "BAS", "BA", "SUB", "808"), "Lead": ("LD", "LEAD", "LED", "LDS"),
              "Keys": ("KY", "KEYS", "KEY", "EP", "PIANO", "ORGAN", "ORG", "CHD", "CHORD", "CH"),
              "Pad": ("PD", "PAD", "PADS"), "Sequence": ("PL", "PLUCK", "PLK", "SEQ", "SQ", "ARP", "AR")}
DIVA_WORDS = {"Bass": "bass", "Lead": "lead", "Keys": "keys|piano|organ|chord", "Pad": "pad", "Sequence": "pluck|arp|seq"}


def diva_style(name):
    base = re.sub(r"^\d+_", "", name.split("__")[0])
    code = re.split(r"[ _-]", base)[0].upper()
    for st, codes in DIVA_CODES.items():
        if code in codes: return st
    for st, w in DIVA_WORDS.items():
        if re.search(r"\b(%s)" % w, base.lower()): return st
    return None


def diva_rows():
    """Fit each rendered Diva preset: wave x octave x low-pass whose 32-band shape is nearest the render's at C3."""
    import glob
    import numpy as np
    lo, hi, nb = 20.0, 20000.0, 32
    edges = lo * (hi / lo) ** (np.arange(nb + 1) / nb)
    waves = {0.0: lambda k: (k == 1) * 1.0, 0.333: lambda k: (k % 2) / k ** 2, 0.667: lambda k: 1.0 / k, 1.0: lambda k: (k % 2) / k}
    cuts = 80.0 * 2 ** (np.arange(0, 36) / 4.5)   # 80 Hz - 20 kHz
    cands, shapes = [], []
    for octv in (-2, -1, 0, 1):
        f0 = 130.81 * 2 ** octv; k = np.arange(1, int(20000 / f0) + 1); fk = f0 * k
        for pos, w in waves.items():
            amp = w(k)
            for poles, ftype in ((2, 1), (4, 2)):
                for c in cuts:
                    pw = (amp ** 2) / (1 + (fk / c) ** (2 * poles))
                    b = np.histogram(fk, edges, weights=pw)[0]
                    cands.append((octv, pos, ftype, float(c))); shapes.append(b)
    # Bands under 90 Hz are left out of the fit: the renders carry rumble there that no oscillator setting explains,
    # and the octave comes from the preset's own tune knob, not the fit (the fit alone put most voices 2 octaves down).
    use = edges[:-1] >= 90.0
    shapes = np.array(shapes)[:, use]
    S = np.log10(np.maximum(shapes / np.maximum(shapes.max(1, keepdims=True), 1e-30), 1e-5))
    octs = np.array([c[0] for c in cands])
    params = {}
    for p in presets(DIVA):
        q = p.get("parameters", {})
        params[p["name"]] = {k: q.get(k) for k in ("OSC.Tune1", "OSC.Tune2", "OSC.Tune3", "OSC.Vol1", "OSC.Vol2", "OSC.Vol3", "ENV1.Rel", "VCF1.Res")}
    num = lambda v, d=0.0: float(v) if v not in (None, "") else d
    rows, seen = [], set()
    for fpath in sorted(glob.glob(os.path.join(DIVA_RENDERS, "preset_diva*.jsonl"))):
        for line in open(fpath):
            r = json.loads(line)
            if r.get("note") != "C3" or r.get("rms", 0) < 1e-3 or r.get("flatness", 1) > 0.3: continue
            name = r["label"].rsplit(".h2p", 1)[0]
            st = diva_style(name)
            if st is None or name in seen: continue
            seen.add(name)
            b = np.array(r["bands"], float)
            if b.max() <= 0: continue
            b = b[use]
            if b.max() <= 0: continue
            t = np.log10(np.maximum(b / b.max(), 1e-5))
            q = params.get(name, {})
            vols = sorted(((num(q.get("OSC.Vol%d" % i)), num(q.get("OSC.Tune%d" % i))) for i in (1, 2, 3)), reverse=True)
            want = max(-2, min(1, int(round(vols[0][1] / 12.0))))
            err = ((S - t) ** 2).sum(1) + 1e9 * (octs != want)
            octv, pos, ftype, cut = cands[int(np.argmin(err))]
            semi = int(round(vols[1][1] - vols[0][1])) if vols[1][0] > 30 else 0
            env = np.array(r.get("env") or [1.0], float); env = env / (env.max() or 1.0)
            held = float(env[int(len(env) * 0.5):int(len(env) * 0.8)].mean()) if len(env) > 4 else 1.0
            uni, det = (1, 0.0) if st == "Bass" else (2, 8.0)   # an analog voice: one oscillator, a little drift
            oa = (1, 0, pos, uni, det, 12 * octv, 1.0)
            ob = (1, 0, pos, 1, 0.0, 12 * octv + max(-12, min(24, semi)), min(1.0, vols[1][0] / max(1.0, vols[0][0]))) if semi else (0, 0, 0.667, 1, 0.0, 0, 0.0)
            pluck = held < 0.3
            famt = 1.5 if pluck else 0.0   # the render's bands are an average: a pluck opens above it and closes under
            rows.append((STYLES.index(st), [oa, ob], ftype, min(18000.0, cut / (2 ** 0.5 if pluck else 1.0)), min(0.9, num(q.get("VCF1.Res")) / 100.0),
                         famt, 0.0, float(r.get("decay_ms") or 400) * 0.5, 0.2, 200.0,
                         float(min(r.get("attack_ms") or 5, 2000)), float(r.get("decay_ms") or 400), held, 10.0 + 6.0 * num(q.get("ENV1.Rel"), 20.0), name))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spectra", required=True); ap.add_argument("--vital", default=DEFAULT_VITAL)
    ap.add_argument("--out", default=DEFAULT_OUT); ap.add_argument("--stats", action="store_true")
    ap.add_argument("--diva", action="store_true")
    a = ap.parse_args()
    tables = []
    for line in open(a.spectra):
        t, f, rest = line.split(" ", 2); name, hs = rest.rsplit('"', 1)
        tables.append((int(t), int(f), logspec([float(x) for x in hs.split()])))
    # Basic Shapes' named waves go first, so a pure saw lands on its saw (0.667), not on a later table that ends in one.
    ideal = {0.0: [1.0] + [0.0] * (H - 1), 0.333: [1.0 / k ** 2 if k % 2 else 0.0 for k in range(1, H + 1)],
             0.667: [1.0 / k for k in range(1, H + 1)], 1.0: [1.0 / k if k % 2 else 0.0 for k in range(1, H + 1)]}
    tables[:0] = [(0, pos * 7.0, logspec(h)) for pos, h in ideal.items()]

    def match(wave):
        if not wave: return 0, 0.667 # Basic Shapes' saw
        ls = logspec(harmonics(wave))
        t, f, _ = min(tables, key=lambda r: sum((x - y) ** 2 for x, y in zip(ls, r[2])))
        return t, f / 7.0

    rows, seen = [], {}
    for p in presets(a.vital):
        style = p.get("style")
        if style not in STYLES: continue
        s = p["settings"]
        amt = {}   # modulation index -> amount, for env_2 / env_3 onto filter 1's cutoff
        for i, m in enumerate(s.get("modulations", [])):
            if m.get("destination") == "filter_1_cutoff" and m.get("source") in ("env_2", "env_3"):
                amt[m["source"]] = s.get("modulation_%d_amount" % (i + 1), 0.0)
        src = max(amt, key=lambda k: abs(amt[k])) if amt else None
        famt = (amt[src] * 128.0 / 12.0) if src else 0.0   # Vital's amount is a share of 128 semitones of cutoff
        fenv = src or "env_2"
        sec = lambda v: 1000.0 * max(0.0, v) ** 4            # Vital's envelope times: seconds = value^4
        oscs = []
        for i in (0, 1):
            on = s.get("osc_%d_on" % (i + 1), 0.0) > 0.5 and s.get("osc_%d_level" % (i + 1), 0.7) > 0.05
            t, pos = match(osc_wave(s, i)) if on else (0, 0.667)
            d = s.get("osc_%d_unison_detune" % (i + 1), 0.0); rng = s.get("osc_%d_detune_range" % (i + 1), 2.0)
            uni = int(s.get("osc_%d_unison_voices" % (i + 1), 1))
            oscs.append((int(on), t, pos, min(8, max(1, uni)), min(60.0, rng * d * d) if uni > 1 else 0.0,
                         int(round(s.get("osc_%d_transpose" % (i + 1), 0.0))), s.get("osc_%d_level" % (i + 1), 0.7)))
        if not oscs[0][0] and not oscs[1][0]: continue
        blend, style_f = s.get("filter_1_blend", 0.0), int(s.get("filter_1_style", 0))
        ftype = 0
        if s.get("filter_1_on", 0.0) > 0.5:
            ftype = 4 if blend > 1.5 else 7 if blend > 0.5 else (2 if style_f == 1 else 1)  # HP12, BP12, LP24, LP12
        cut = 440.0 * 2 ** ((s.get("filter_1_cutoff", 60.0) - 69.0) / 12.0)
        row = (STYLES.index(style), oscs, ftype, min(18000.0, max(40.0, cut)), min(0.9, s.get("filter_1_resonance", 0.0)),
               max(-6.0, min(6.0, famt)), sec(s.get(fenv + "_attack", 0)), sec(s.get(fenv + "_decay", 1)),
               s.get(fenv + "_sustain", 1.0), sec(s.get(fenv + "_release", 0.5)),
               sec(s.get("env_1_attack", 0)), sec(s.get("env_1_decay", 1)), s.get("env_1_sustain", 1.0), sec(s.get("env_1_release", 0.5)),
               p.get("name", "?"))
        key = json.dumps(row[:-1], sort_keys=True)
        if key in seen: continue # the same patch saved twice
        seen[key] = 1; rows.append(row)
    nv = len(rows)
    if a.diva:
        rows += diva_rows()
    lines = ["// Generated by tools/universe/vital_voice_bank.py: %d Vital presets (vital_dsp_dataset.json) and %d Diva" % (nv, len(rows) - nv),
             "// presets (fitted to their renders), each one preset's shape on Infinite's Wavetable. Times in ms; filter",
             "// amount in octaves (negative closes); src 0 Vital, 1 Diva. Do not edit by hand: rerun the tool.",
             "struct BankOsc { int on, table; float pos; int unison; float detune; int semi; float level; };",
             "struct BankRow { int style; BankOsc a, b; int filter; float cutoff, res, famt, fa, fd, fs, fr, aa, ad, as, ar; int src; };",
             "static const char* const kBankStyles[] = { %s };" % ", ".join('"%s"' % s for s in STYLES),
             "static const BankRow kBankVoices[] = {"]
    f = lambda x: ("%.4g" % x) + ("" if "." in "%.4g" % x or "e" in "%.4g" % x else ".0") + "f"
    for i, (st, (oa, ob), ft, cut, res, famt, fa, fd, fs, fr, aa, ad, as_, ar, name) in enumerate(rows):
        osc = lambda o: "{ %d, %d, %s, %d, %s, %d, %s }" % (o[0], o[1], f(o[2]), o[3], f(o[4]), o[5], f(o[6]))
        lines.append("   { %d, %s, %s, %d, %s, %d }, // %s" % (st, osc(oa), osc(ob), ft, ", ".join(f(x) for x in (cut, res, famt, fa, fd, fs, fr, aa, ad, as_, ar)),
                                                             int(i >= nv), re.sub(r"[^\w .&+-]", "", name)[:40]))
    lines.append("};")
    open(a.out, "w").write("\n".join(lines) + "\n")
    if a.stats:
        import collections
        print("vital", collections.Counter(STYLES[r[0]] for r in rows[:nv]))
        print("diva", collections.Counter(STYLES[r[0]] for r in rows[nv:]))
        print("tables used:", collections.Counter(r[1][0][1] for r in rows).most_common(12))
    print("%d voices -> %s" % (len(rows), os.path.relpath(a.out)))


if __name__ == "__main__":
    main()
