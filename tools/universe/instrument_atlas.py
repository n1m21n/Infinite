#!/usr/bin/env python3
"""Instrument atlas (data-universe step 3): one row per instrument, joined from every reader's rows.

Sources: EXS maps (+ the audio features of their zone files), Kontakt .nkc maps, Omnisphere
soundsources (moods, timbres, types), LABS packs (audio features per pack). Reads
~/Infinite/training-data, writes atlas/instruments.jsonl and atlas/summary.json. Rows only, no audio.

    python3 -I tools/universe/instrument_atlas.py [--data DIR]
"""
import argparse, collections, json, os, re

FAMILIES = [  # first match wins; text = category + name (+ library)
    ("drums", r"kick|snare|hi-?hat|\bhat\b|tom\b|toms|cymbal|drum|clap|rim|conga|bongo|percussion|shaker|tambourine|break"),
    ("bass", r"\bbass|sub\b|808"),
    ("piano", r"piano|grand|upright"),
    ("keys", r"keyboard|organ|clav|rhodes|wurli|harpsichord|electric piano|\bep\b|accordion|harmonium"),
    ("strings", r"string|violin|viola|cello|harp\b|orchestral ensemble|pizzicato|spiccato"),
    ("brass", r"brass|trumpet|trombone|horn|tuba|sax"),
    ("woodwind", r"wood ?wind|flute|clarinet|oboe|bassoon|piccolo|recorder|pan ?pipe|ocarina"),
    ("guitar", r"guitar|banjo|mandolin|ukulele|sitar|oud|lute|bouzouki|charango"),
    ("mallets", r"mallet|marimba|vibraphone|xylophone|glockenspiel|celesta|bell|chime|kalimba|steel ?drum|gamelan"),
    ("choir", r"choir|vocal|vox|voice|choral|chant|\boo+h?\b|\bah+\b"),
    ("synth", r"synth|lead|pad\b|pads\b|waveform|motion|sequence|arp|analog|digital|saw|square|wavetable"),
    ("texture", r"textur|drone|ambient|cinematic|atmos|noise|fx\b|effect|riser|impact|soundscape|foley"),
    ("world", r"world|ethnic|asia|africa|india|tabla|koto|shamisen|duduk|erhu|didgeridoo|cuba|latin|celtic"),
]
FAM_RE = [(f, re.compile(p, re.I)) for f, p in FAMILIES]
VOL = re.compile(r"^/Volumes/[^/]+/", re.I)


def family(*texts):
    t = " ".join(x for x in texts if x)
    for f, rx in FAM_RE:
        if rx.search(t):
            return f
    return "other"


def norm(p):
    return VOL.sub("/", p or "").lower()


def rows(path):
    if not os.path.exists(path):
        return
    with open(path) as f:
        for l in f:
            yield json.loads(l)


def feat_vec(r):
    return [r["bands"], r.get("centroid"), r.get("flatness"), r.get("attack_ms"), r.get("decay_ms"), r.get("seconds"), r.get("rms")]


class Acc:
    """Running mean of 32-band spectrum and scalar features over a set of audio rows."""
    def __init__(self):
        self.n = 0
        self.bands = [0.0] * 32
        self.s = collections.defaultdict(lambda: [0.0, 0])

    def add(self, r):
        b = r.get("bands")
        if not b or len(b) != 32:
            return
        self.n += 1
        for i, v in enumerate(b):
            self.bands[i] += v
        for k in ("centroid", "flatness", "attack_ms", "decay_ms", "seconds", "rms", "low", "mid", "high"):
            v = r.get(k)
            if v is not None:
                self.s[k][0] += v
                self.s[k][1] += 1

    def out(self):
        if not self.n:
            return None
        o = {"n": self.n, "bands": [round(x / self.n, 6) for x in self.bands]}
        for k, (t, c) in self.s.items():
            o[k] = round(t / c, 4)
        return o


def audio_index(data):
    """normalised file path -> audio row (all machines), and LABS pack -> Acc."""
    by_path, by_pack, dups = {}, collections.defaultdict(Acc), []
    for p in ("audio/features.jsonl", "audio/air_features.jsonl", "spitfire/labs_audio.jsonl", "steam/soundsource_audio.jsonl"):
        for r in rows(os.path.join(data, p)):
            if r.get("dup_of"):
                dups.append(r)
            elif r.get("bands"):
                by_path[norm(r["src"])] = r
                if r.get("pack"):
                    by_pack[r["pack"].split("/")[0].lower()].add(r)
    for r in dups:  # identical copies share the original's features
        if norm(r["dup_of"]) in by_path:
            by_path[norm(r["src"])] = by_path[norm(r["dup_of"])]
    return by_path, by_pack


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=os.path.expanduser("~/Infinite/training-data"))
    a = ap.parse_args()
    out_dir = os.path.join(a.data, "atlas")
    os.makedirs(out_dir, exist_ok=True)
    by_path, by_pack = audio_index(a.data)
    out, n = open(os.path.join(out_dir, "instruments.jsonl"), "w"), collections.Counter()

    def emit(row):
        row["family"] = row.get("family") or "other"
        out.write(json.dumps(row, separators=(",", ":")) + "\n")
        n[(row["source"], row["family"])] += 1

    # EXS: every zone's audio file joined by path
    for r in rows(os.path.join(a.data, "samplers/exs.jsonl")):
        acc, files = Acc(), set()
        for z in r["zones"]:
            f = z.get("file")
            if f and f not in files:
                files.add(f)
                ar = by_path.get(norm(f))
                if ar:
                    acc.add(ar)
        emit({"id": "exs:" + norm(r["src"]), "source": "exs", "name": r["name"], "category": r["category"],
              "family": family(r["category"], r["name"]), "keys": r["keys"], "vel_layers": r["vel_layers"],
              "zones": len(r["zones"]), "files": len(files), "playable": r["resolved"] == 1.0,
              "audio": acc.out(), "audio_cover": round(acc.n / len(files), 3) if files else 0.0,
              "license": r["license"]})
    # Kontakt maps
    for r in rows(os.path.join(a.data, "spitfire/nkc_catalog.jsonl")):
        pk = by_pack.get(r["library"].lower())
        emit({"id": "nkc:" + norm(r["src"]), "source": "nkc", "name": r["instrument"], "category": r["library"],
              "family": family(r["library"], r["instrument"], " ".join(r.get("sample_names", [])[:8])),
              "keys": [r["lo"], r["hi"]], "vel_layers": r["layers"], "zones": r["files"], "files": r["files"],
              "playable": None, "audio": pk.out() if pk else None, "license": r["license"]})
    # Omnisphere soundsources: moods / timbre tags, family from type
    for r in rows(os.path.join(a.data, "steam/soundsources.jsonl")):
        t = r["tags"]
        typ = t.get("type")
        typ = " ".join(typ) if isinstance(typ, list) else (typ or "")
        emit({"id": "steam:%s/%s/%s" % (r["library"], r["path"], r["name"]), "source": "steam", "name": r["name"],
              "category": "%s / %s" % (r["library"], r.get("path", "")),
              "family": family(typ, t.get("category"), t.get("subcategory"), r["name"]),
              "moods": t.get("mood") or [], "timbre": t.get("timbre") or [], "types": typ, "tonality": t.get("tonality"),
              "audio": None, "license": r["license"]})
    out.close()
    sm = collections.defaultdict(dict)
    for (s, f), c in sorted(n.items()):
        sm[s][f] = c
    json.dump({"rows": sum(n.values()), "by_source_family": sm, "audio_paths": len(by_path)},
              open(os.path.join(out_dir, "summary.json"), "w"), indent=1)
    print("atlas rows", sum(n.values()), "audio paths indexed", len(by_path))
    for s, d in sm.items():
        print(" ", s, dict(sorted(d.items(), key=lambda x: -x[1])))


if __name__ == "__main__":
    main()
