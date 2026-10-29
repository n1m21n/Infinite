#!/usr/bin/env python3
"""Vital presets -> Wavetable voices for Generate Samples (owner, 2026-10-30: "data is gold and a slight variation
can give many results"; variety was 4-7 designed voices per part). Each of the 794 presets in vital_dsp_dataset.json
becomes one row: its two oscillators (the Vital wave matched to the closest of Infinite's 64 tables x 8 frames by
harmonic spectrum), unison count and spread, transpose and level, filter 1 (type, cutoff, resonance, the env_2/env_3
sweep onto its cutoff in octaves) and the amp envelope. Rows keep Vital's own style label (Bass, Keys, Lead, Pad,
Sequence, Percussion). Generate Samples draws a row of the part's style and jitters it, so a take is a real preset's
shape played by Infinite's synth.
    vital_voice_bank.py --spectra wt_spectra.txt [--vital FILE] [--out FILE] [--stats]
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spectra", required=True); ap.add_argument("--vital", default=DEFAULT_VITAL)
    ap.add_argument("--out", default=DEFAULT_OUT); ap.add_argument("--stats", action="store_true")
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
    lines = ["// Generated by tools/universe/vital_voice_bank.py from %d Vital presets (vital_dsp_dataset.json): each row" % len(rows),
             "// is one preset's shape on Infinite's Wavetable. Oscillator waves matched to Infinite's tables by harmonic",
             "// spectrum; times in ms; filter amount in octaves (negative closes). Do not edit by hand: rerun the tool.",
             "struct VitalOsc { int on, table; float pos; int unison; float detune; int semi; float level; };",
             "struct VitalVoice { int style; VitalOsc a, b; int filter; float cutoff, res, famt, fa, fd, fs, fr, aa, ad, as, ar; };",
             "static const char* const kVitalStyles[] = { %s };" % ", ".join('"%s"' % s for s in STYLES),
             "static const VitalVoice kVitalVoices[] = {"]
    f = lambda x: ("%.4g" % x) + ("" if "." in "%.4g" % x or "e" in "%.4g" % x else ".0") + "f"
    for st, (oa, ob), ft, cut, res, famt, fa, fd, fs, fr, aa, ad, as_, ar, name in rows:
        osc = lambda o: "{ %d, %d, %s, %d, %s, %d, %s }" % (o[0], o[1], f(o[2]), o[3], f(o[4]), o[5], f(o[6]))
        lines.append("   { %d, %s, %s, %d, %s }, // %s" % (st, osc(oa), osc(ob), ft, ", ".join(f(x) for x in (cut, res, famt, fa, fd, fs, fr, aa, ad, as_, ar)),
                                                         re.sub(r"[^\w .&+-]", "", name)[:40]))
    lines.append("};")
    open(a.out, "w").write("\n".join(lines) + "\n")
    if a.stats:
        import collections
        print(collections.Counter(STYLES[r[0]] for r in rows))
        print("tables used:", collections.Counter(r[1][0][1] for r in rows).most_common(12))
    print("%d voices -> %s" % (len(rows), os.path.relpath(a.out)))


if __name__ == "__main__":
    main()
