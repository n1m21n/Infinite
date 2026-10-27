#!/usr/bin/env python3
"""Instrument-family coverage: how many measured rows we hold per family, per source, so collection targets the thin families.

    coverage_audit.py [--audio ~/Infinite/training-data/audio] [--out coverage_audit.md]

Family = first keyword hit in (part, path) from FAMILIES (order matters: specific before generic). Sources: `library` = features.jsonl
(loops / one-shots from the drive), `plugin` = render_features*.jsonl (plug-in default + sweep), `omni` = omni_s*.jsonl (patch renders).
Keyword matching on file and folder names is a guess at the family, not a label: the table says where to look, not what is true."""
import argparse, collections, glob, json, os, re

FAMILIES = [
    ("piano", r"piano|rhodes|wurli|clav|keys\b|epiano|e-piano|harpsichord|grand"),
    ("strings", r"string|violin|viola|cello|orchestra|ensemble|pizz|legato|spiccato"),
    ("guitar", r"guitar|gtr|strum|pluck|acoustic|banjo|ukulele|sitar|oud"),
    ("bass", r"\bbass|808|sub\b|reese"),
    ("brass_wind", r"brass|trumpet|trombone|horn|sax|flute|clarinet|oboe|bassoon|woodwind|whistle"),
    ("drums_kit", r"kick|snare|clap|hat|tom|cymbal|crash|ride|rim|drum|perc|conga|bongo|shaker|tabla|cowbell"),
    ("vocal", r"vocal|vox|voice|choir|acapella|adlib|chant|sing"),
    ("pad_texture", r"pad|atmos|ambient|texture|drone|soundscape|cinematic|evolv"),
    ("lead_synth", r"lead|synth|arp|pluck|saw|square|analog|digital|supersaw"),
    ("bell_mallet", r"bell|chime|mallet|marimba|vibraphone|glock|xylophone|kalimba|harp|music box"),
    ("fx_noise", r"\bfx\b|effect|riser|sweep|impact|noise|foley|whoosh|downlifter|transition"),
    ("organ_accordion", r"organ|accordion|harmonium|melodica"),
]
FAM = [(n, re.compile(p, re.I)) for n, p in FAMILIES]


def family(*text):
    t = " ".join(x for x in text if x)
    for n, rx in FAM:
        if rx.search(t):
            return n
    return "other"


def scan(files, key):
    c = collections.Counter()
    for f in files:
        for ln in open(f):
            r = json.loads(ln)
            if key == "omni":
                if r.get("note") != "C3":
                    continue
                tg = r.get("tags") or {}
                c[family(" ".join(tg.get("Type", [])), r.get("label"))] += 1
            elif key == "plugin":
                c[family(r.get("plugin"), r.get("name"), r.get("category"), r.get("vendor"))] += 1
            else:
                c[family(r.get("part"), r.get("pack"), r.get("src"))] += 1
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--audio", default="~/Infinite/training-data/audio")
    ap.add_argument("--out", default="")
    a = ap.parse_args()
    d = os.path.expanduser(a.audio)
    src = {"library": scan([os.path.join(d, "features.jsonl")], "library"),
           "plugin": scan(glob.glob(os.path.join(d, "render_features*.jsonl")), "plugin"),
           "omni": scan(glob.glob(os.path.join(d, "omni_s*.jsonl")), "omni")}
    names = [n for n, _ in FAMILIES] + ["other"]
    lines = ["| family | library rows | plugin rows | omni patches (C3) |", "|---|---:|---:|---:|"]
    for n in names:
        lines.append("| %s | %d | %d | %d |" % (n, src["library"][n], src["plugin"][n], src["omni"][n]))
    lines.append("| **total** | %d | %d | %d |" % tuple(sum(src[k].values()) for k in ("library", "plugin", "omni")))
    out = "\n".join(lines)
    print(out)
    if a.out:
        open(a.out, "w").write("# Instrument-family coverage (keyword guess)\n\n" + out + "\n")


if __name__ == "__main__":
    main()
