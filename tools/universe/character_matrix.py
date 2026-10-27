#!/usr/bin/env python3
"""Character x node matrix (docs/plans/data-universe/dials.md, W5 input): which Infinite audio effects and synths people
reach for when a sound is meant to be spacious, glitchy, dirty, warm... Learned from the preset rows we gathered.
    character_matrix.py [--presets ~/Infinite/training-data/presets] [--out ~/Infinite/training-data/atlas/character_matrix.json]
Evidence per preset: a text bag (tags, folder, name) gives its characters by lexicon; the effect chain (Logic strips: plugin list;
Guitar Rig: FX tags) gives the Infinite node families it used. Cell = log2 lift P(node | character) / P(node), kept only with
support >= MIN_N. DECLARED holds the owner's examples as a prior; the report says where data agrees and where plugins have no
Infinite node (candidates to write in Field). Statistics only."""
import argparse, collections, json, math, os, re

TD = os.path.expanduser("~/Infinite/training-data")
MIN_N = 30

CHARS = {  # character -> lexicon over the preset's text bag
    "spacious": r"spacious|space|ambien|hall|cathedral|wide|atmosph|airy|ethereal|shimmer|dream|soundscape",
    "dirty": r"dirty|dist|fuzz|crunch|grit|overdrive|saturat|hi gain|rammfire|filthy|destroy",
    "aggressive": r"aggressive|metal|hard|brutal|heavy|angry|slam",
    "lofi": r"lo-?fi|bit|crush|tape|vintage|vinyl|dusty|broken|cassette|8.?bit|retro",
    "glitchy": r"glitch|stutter|experiment|granular|chop|random|dissonant|mash|re-?sample|special fx|weird|noise",
    "warm": r"warm|analog|tube|soft|smooth|vintage|round|mellow",
    "bright": r"bright|crisp|shiny|sparkl|airy|glass|clear|sizzl",
    "dark": r"dark|deep|sub|dungeon|ominous|murky|doom",
    "clean": r"clean|pure|clear|natural|transparent",
    "moving": r"moving|modulat|wobble|sweep|pulse|rhythmic|motion|tremolo|swirl|animate",
    "phat": r"phat|fat|thick|huge|big|massive|wall|stack",
}

NODE = [  # (regex over plugin name or FX tag, Infinite node)
    (r"bitcrush|bit crush|lo-?fi|8.?bit", "Bitcrush"),
    (r"space d|convolution|impulse", "Convolution Reverb"),
    (r"verb|plate|hall|room|spring", "Reverb"),
    (r"delay|dly|echo|tape delay", "Delay"),
    (r"ringshift|ring ?mod", "Ring Mod"),
    (r"freq(uency)? ?shift", "Frequency Shifter"),
    (r"pitch( ?shift)?|harmoniz|whammy|octav", "Pitch Shifter"),
    (r"overdrive|dist|fuzz|clipdist|drive|crunch|hi gain|amp(?!l)", "Drive"),
    (r"satur|exciter|chromaglow|tube|warm|tape(?! delay)", "Saturator"),
    (r"chorus|ensemble", "Chorus"),
    (r"flang", "Flanger"),
    (r"phase", "Phaser"),
    (r"tremolo", "Tremolo"),
    (r"autofilter|filter|wah|formant", "Audio Filter"),
    (r"channel eq|single eq|tube eq|\beq\b|equali", "EQ"),
    (r"limiter|maximi", "Limiter"),
    (r"compress|squash|noise gate|gate|multipr|dynamics|adaptive", "Dynamics"),
    (r"enveloper|transient|attack", "Transient Shaper"),
    (r"spreader|stereo|dirmix|width|imager", "Stereo"),
    (r"step fx|stutter|gater", "Stutter"),
]
SYNTH = [
    (r"retro synth|es1|es2|es e|es m|analog|minimoog|subtractive|hive|zebra|diva|sylenth", "Analog"),
    (r"efm1|\bfm\b|operator|dx7|fm8", "FM"),
    (r"sculpture|string|karplus|pluck", "String"),
    (r"alchemy|wavetable|serum|vital|massive|pigments", "Wavetable"),
    (r"granular|grain", "Granular"),
    (r"q-sampler|exs24|sampler|kontakt|simpler|ultrabeat|drum kit|drum machine|drum synth", "Sampler"),
]
# plugin names in the data that no Infinite node covers: reported as Field candidates when frequent
DECLARED = {  # the owner's examples (2026-10-27), the prior the data is checked against
    "spacious": ["Reverb", "Delay", "Convolution Reverb"],
    "glitchy": ["Bitcrush", "Stutter", "Frequency Shifter", "Pitch Shifter", "Ring Mod"],
    "dirty": ["Drive", "Saturator", "Bitcrush"],
    "moving": ["Chorus", "Phaser", "Flanger", "Tremolo"],
}


def hits(table, name):
    n = name.lower()
    return {v for rx, v in table if re.search(rx, n)}


def evidence(presets):
    for fn in sorted(os.listdir(presets)):
        if fn not in ("air_logic_strips.jsonl", "air_ni_guitarrig_nkp.jsonl"):
            continue
        for ln in open(os.path.join(presets, fn)):
            r = json.loads(ln)
            t = r.get("tags")
            if fn.startswith("air_logic"):
                chain = t.get("chain") or []
                bag = " ".join([r["name"], t.get("folder", "").split("Settings/")[-1]])
                eff = set().union(*[hits(NODE, c) for c in chain]) if chain else set()
                syn = set().union(*[hits(SYNTH, c) for c in chain]) if chain else set()
                un = [c for c in chain if not hits(NODE, c) and not hits(SYNTH, c)]
            else:
                tags = t or []
                bag = " ".join([r["name"]] + [x for x in tags if x not in ("Effect", "FX Types", "Characters", "Genres")])
                fx = [x for x in tags if x in ("Delay", "Reverb", "Distortion", "Flanger", "Phaser", "Pitch Shift", "Filter",
                                              "Compressor", "EQ", "Tape", "Lofi", "Granular", "Modulation", "Twang Reverb", "Chorus")]
                eff = set().union(*[hits(NODE, x) for x in fx]) if fx else set()
                syn, un = set(), []
            yield bag.lower(), eff, syn, un


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--presets", default=os.path.join(TD, "presets"))
    ap.add_argument("--out", default=os.path.join(TD, "atlas", "character_matrix.json"))
    a = ap.parse_args()
    N = 0
    nc, ne, nce = collections.Counter(), collections.Counter(), collections.Counter()
    ns, ncs = collections.Counter(), collections.Counter()
    unmapped = collections.Counter()
    for bag, eff, syn, un in evidence(os.path.expanduser(a.presets)):
        if not eff and not syn:
            continue
        N += 1
        ch = {c for c, rx in CHARS.items() if re.search(rx, bag)}
        ne.update(eff); ns.update(syn); unmapped.update(un)
        for c in ch:
            nc[c] += 1
            for e in eff: nce[(c, e)] += 1
            for s in syn: ncs[(c, s)] += 1

    def mat(cnt_ce, cnt_e):
        out = {}
        for c in CHARS:
            row = {}
            for e in cnt_e:
                n = cnt_ce.get((c, e), 0)
                if nc[c] >= MIN_N and cnt_e[e] >= MIN_N:
                    lift = math.log2(((n + 0.5) / (nc[c] + 1)) / ((cnt_e[e] + 0.5) / (N + 1)))
                    row[e] = {"lift": round(lift, 2), "p": round(n / nc[c], 3), "n": n}
            out[c] = row
        return out
    res = {"presets": N, "characters": {c: nc[c] for c in CHARS}, "effects": mat(nce, ne), "synths": mat(ncs, ns),
           "declared": DECLARED, "unmapped_plugins": unmapped.most_common(25)}
    os.makedirs(os.path.dirname(os.path.expanduser(a.out)), exist_ok=True)
    json.dump(res, open(os.path.expanduser(a.out), "w"), indent=1)
    print("%d presets with an effect or synth chain; character support %s" % (N, dict(nc)))
    for kind in ("effects", "synths"):
        print("\n== %s: top 4 by lift per character ==" % kind)
        for c in CHARS:
            r = sorted(res[kind][c].items(), key=lambda kv: -kv[1]["lift"])[:4]
            print("%-11s %s" % (c, "  ".join("%s %+.2f" % (k, v["lift"]) for k, v in r)))
    print("\n== data vs declared (declared node in the character's top 5 by lift) ==")
    for c, nodes in DECLARED.items():
        top = [k for k, _ in sorted(res["effects"][c].items(), key=lambda kv: -kv[1]["lift"])[:5]]
        print("%-9s declared %s -> in top5: %s | top5 %s" % (c, nodes, [n for n in nodes if n in top], top))
    print("\nunmapped plugins (no Infinite node):", unmapped.most_common(12))


if __name__ == "__main__":
    main()
