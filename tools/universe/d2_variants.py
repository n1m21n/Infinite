#!/usr/bin/env python3
"""D2 of SOUND-MODEL.md 4b: one-knob variants of real Diva presets, so the model sees each knob's own effect unconfounded.

    d2_variants.py ROWS.jsonl --presets ~/Infinite/scratch_omni/diva_air --out ~/Infinite/scratch_omni/diva_d2 [--bases 50] [--per 2]

Bases: presets of ROWS that are in training on both frozen splits (sound_data.split), picked by crc of their id (stable). For each base
and each knob in KNOBS, --per new values are drawn from that knob's spread over the whole library (deciles 1-9 of every .h2p under
--presets), skipping values within 10% of the library range of the base's own value. One edited copy of the .h2p per variant, at
<out>/<base's relative folder>/<base name>__<section>.<key>=<value>.h2p, so a variant shares its base's bank. Render the folder with
preset_state.py --presets <out> --out audio/preset_diva_d2.jsonl; p3_characteristics.py --extra uses those rows for fitting only."""
import argparse, collections, glob, os, re, sys, zlib
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sound_data as sd  # noqa: E402

# the knobs most tied to the characteristic axes: filter (brightness, sharpness), both envelopes (attack, decay, loudness curve),
# oscillator mix and noise (fullness, noisiness), high-pass (weight, warmth)
KNOBS = [("VCF1", "Freq"), ("VCF1", "Res"), ("VCF1", "FMDpt"), ("ENV1", "Atk"), ("ENV1", "Dec"), ("ENV2", "Atk"), ("ENV2", "Dec"),
         ("ENV2", "Sus"), ("OSC", "Vol2"), ("HPF", "Freq")]
LINE = re.compile(r"^([A-Za-z0-9]+)=(-?[0-9.]+)$")


def where(text):
    """(section, key) -> (line index, value) for the numeric lines of an .h2p text."""
    sec, out = None, {}
    for i, ln in enumerate(text.split("\n")):
        if ln.startswith("#cm="):
            sec = ln[4:].strip()
        else:
            m = LINE.match(ln.strip())
            if m and sec and (sec, m.group(1)) not in out:
                out[(sec, m.group(1))] = (i, float(m.group(2)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rows"); ap.add_argument("--presets", required=True); ap.add_argument("--out", required=True)
    ap.add_argument("--bases", type=int, default=50); ap.add_argument("--per", type=int, default=2)
    a = ap.parse_args()
    root = os.path.expanduser(a.presets); out = os.path.expanduser(a.out)
    lib = collections.defaultdict(list)
    for p in glob.glob(os.path.join(root, "**", "*.h2p"), recursive=True):
        w = where(open(p, encoding="latin-1").read())
        for k in KNOBS:
            if k in w:
                lib[k].append(w[k][1])
    dec = {k: np.percentile(v, np.arange(10, 100, 10)) for k, v in lib.items()}
    rng_ = {k: max(np.ptp(v), 1e-6) for k, v in lib.items()}
    for k in KNOBS:
        print("%-12s %5d presets, deciles %s" % ("%s.%s" % k, len(lib[k]), np.round(dec.get(k, []), 1)))
    d = sd.load(a.rows); s = sd.split(d); train = ~s["group"] & ~s["random"]
    ids = sorted(np.flatnonzero(train), key=lambda i: zlib.crc32(str(d["preset_id"][i]).encode()))[:a.bases]
    n = 0
    for i in ids:
        rel = str(d["src"][i]).split(":", 2)[2]; text = open(os.path.join(root, rel), encoding="latin-1").read()
        lines, w = text.split("\n"), where(text)
        for k in KNOBS:
            if k not in w or k not in dec:
                continue
            li, v0 = w[k]
            cand = [x for x in dec[k] if abs(x - v0) > 0.1 * rng_[k]]
            if not cand:
                continue
            pick = sorted(cand, key=lambda x: zlib.crc32(("%s%s%s" % (rel, k, x)).encode()))[:a.per]
            for x in pick:
                new = list(lines); new[li] = re.sub(r"=.*$", "=%.2f" % x, new[li])
                dst = os.path.join(out, os.path.dirname(rel), "%s__%s.%s=%.2f.h2p" % (os.path.basename(rel)[:-4], k[0], k[1], x))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                open(dst, "w", encoding="latin-1").write("\n".join(new)); n += 1
    print("%d bases, %d variants -> %s" % (len(ids), n, out))


if __name__ == "__main__":
    main()
