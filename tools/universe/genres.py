#!/usr/bin/env python3
"""Genres / subgenres (data-universe step 5): which sound clusters, drum grooves, tempi and arcs each genre uses, COUNTED.

Genres come from three places and are never invented without a count:
  catalog   the 34 styles of catalog/styles_music.json (the genre graph) and their 12 families
  inferred  genres named in sample-pack / folder / file names and MIDI folders (Afro, Trap, Hip Hop, Soul, Jazz ...),
            matched with the word list LEXICON below (word-boundary regexes on the lower-cased path)
Every genre entry lists n rows per source, so a reader can see what the numbers rest on:
  audio        rows of audio/features.jsonl + air_features.jsonl whose path names the genre. Each row is projected onto
               the nearest sound cluster of its family (same features and z-scoring as sound_clusters.py; missing
               attack/decay dims are ignored), and its loop groove (swing, accent, tightness) and bpm are collected.
  instruments  atlas instruments whose name / category / moods / types name the genre, counted in their own cluster
               (via = "audio" measured, or "tags" assigned by words)
  midi         drum_groove / bass_seq / chord_prog rows of store/inbox.jsonl and public_drums.jsonl that carry a family
               or whose source path names the genre: tempo, swing, kick / snare patterns, mode, mood words
  arcs         song_arc rows (bar unit, >= 20 bars) of arcs/detail.jsonl.gz: energy curve over song tenths, shape, major share
The style-card drums and the catalog pillars are hand-written declarations; they are kept in their own fields
("declared") and never mixed with the counts. store/synthetic.jsonl (our own generator output) is not evidence.
A genre with no cluster rows is "midi_only", "audio_only" or "declared_only": the file says which, per genre.

Data is a guardrail, not a template: this file describes the data; the generator does not copy it.

    python3 tools/universe/genres.py [--data DIR] [--out FILE] [--no-arcs] [--no-midi]
Re-run after the audio pass (more audio rows) and after sound_clusters.py (new clusters). Numpy only, streams the big
files, memory well under 1 GB. Writes atlas/genres.json and prints the top genres."""
import argparse, collections, gzip, json, math, os, re, sys, time
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sound_clusters as SC  # feat() = the clustering features

DATA = os.path.expanduser("~/Infinite/training-data")
CATALOG = os.path.join(HERE, "..", "catalog", "styles_music.json")

# sub-genre aliases for catalog ids (regexes on the normalised path: lower case, _ . - ( ) [ ] + become spaces, & becomes "and")
ALIASES = {
    "deep_house": r"deep house", "acid_house": r"acid house|\bacid\b", "tech_house": r"tech house",
    "progressive_house": r"prog(ressive)? house", "french_filter_house": r"french (house|touch)|filter house",
    "detroit_techno": r"detroit", "minimal_techno": r"\bminimal\b", "dub_techno": r"dub techno",
    "industrial_techno": r"industrial", "classic_trance": r"classic trance|uplifting trance", "psytrance": r"psy ?trance|\bpsy\b|\bgoa\b",
    "liquid_dnb": r"liquid (drum|dnb|funk|d ?n ?b)", "neurofunk": r"neuro ?funk|\bneuro\b", "jungle": r"\bjungle\b",
    "uk_garage": r"uk garage|2 ?step|\bukg\b|\bgarage\b", "dubstep": r"dub ?step|brostep", "breakbeat": r"break ?beats?|\bbreaks\b|big beat",
    "electro": r"\belectro\b(?! ?house)", "ambient": r"ambient|ambience", "dark_ambient": r"dark ambient", "idm": r"\bidm\b",
    "glitch": r"glitch", "downtempo": r"down ?tempo|chill ?out|\bchill\b", "lofi_beats": r"lo ?fi", "synthwave": r"synth ?wave|retro ?wave|outrun",
    "vaporwave": r"vapor ?wave", "footwork": r"footwork|\bjuke\b", "gabber": r"gabber|hard ?core", "solo_piano": r"solo piano",
    "neo_classical": r"neo ?classical", "ambient_piano": r"ambient piano", "amapiano": r"amapiano|log ?drum",
    "future_garage": r"future garage", "drone_kosmische": r"drone|kosmische",
}
# generic family words (catalog families) and genres the catalog does not have: id -> (parent, regex)
FAMILY_WORDS = {
    "house": r"\bhouse\b", "techno": r"techno", "trance": r"trance", "drum_and_bass": r"drum ?(and|n|\'n\') ?bass|\bdnb\b|\bd ?n ?b\b",
    "bass_music": r"bass music", "breaks": r"\bbreaks\b", "ambient": r"ambient|ambience", "experimental": r"experimental",
    "downtempo": r"down ?tempo|chill ?out", "retro": r"\bretro\b", "hard": r"hard ?style|hard ?dance|hard ?core", "piano": r"\bpiano solo\b",
}
INFERRED = {  # id: (parent group, regex)
    "hiphop": (None, r"hip ?hop|boom ?bap|\brap\b|\bgrime\b"), "trap": ("hiphop", r"\btrap\b"),
    "soul_funk": (None, r"^$"), "rnb": ("soul_funk", r"\br ?(and|n) ?b\b|\brnb\b|neo ?soul"), "soul": ("soul_funk", r"\bsoul\b|motown"),
    "funk": ("soul_funk", r"funk(?!y town)"), "disco": ("soul_funk", r"disco"),
    "world": (None, r"\bworld\b|ethnic|\bwhirl"), "afro": ("world", r"afro|african|south africa"),
    "latin": ("world", r"latin|salsa|bossa|samba|reggaeton|cumbia|rumba|mambo|bachata|brazil|baiao|songo|cuban"),
    "reggae": ("world", r"reggae|dancehall|\bska\b|\bdub\b"),
    "pop_rock": (None, r"^$"), "pop": ("pop_rock", r"\bpop\b|synth ?pop"), "rock": ("pop_rock", r"\brock\b|metal|punk|grunge|indie"),
    "folk": ("pop_rock", r"\bfolk\b|country|bluegrass|celtic"),
    "jazz": (None, r"jazz|bebop|big band"), "blues": ("jazz", r"\bblues\b"),
    "classical": (None, r"classical|orchestral|baroque|symphon|chamber|chopin|bach|beethoven"),
    "cinematic": (None, r"cinematic|soundtrack|trailer|\bepic\b"),
    "edm": (None, r"\bedm\b|festival"), "electro_house": ("house", r"electro ?house|complextro|big room"),
    "future_bass": ("edm", r"future bass|melodic dubstep"), "big_room": ("edm", r"big room"),
    "chiptune": ("retro", r"chip ?tune|\bnes\b|8 ?bit|chip"), "rave": ("breaks", r"\brave\b|old ?skool|oldskool|hardcore rave"),
}


# cheap pre-filter: a path with none of these substrings cannot match any LEXICON pattern (the 60 regexes only run on the rest)
STEMS = ("hous", "techno", "tranc", "acid", "detroit", "minimal", "industrial", "psy", "goa", "liquid", "neuro", "jungle", "garage", "step",
         "ukg", "dub", "break", "electro", "ambien", "idm", "glitch", "tempo", "chill", "lo fi", "lofi", "wave", "footwork", "juke", "gabber",
         "hard", "piano", "drone", "kosmische", "amapiano", "log drum", "dnb", "d n b", "bass", "retro", "hip", "rap", "grime", "trap", "soul",
         "rnb", "r and b", "funk", "disco", "world", "ethnic", "afro", "african", "latin", "salsa", "bossa", "samba", "brazil", "cuban", "reggae",
         "dancehall", "ska", "pop", "rock", "metal", "punk", "grunge", "indie", "folk", "country", "bluegrass", "celtic", "jazz", "bebop", "big band",
         "blues", "classic", "orchestr", "baroque", "symphon", "chamber", "chopin", "bach", "beethoven", "cinematic", "soundtrack", "trailer", "epic",
         "edm", "festival", "big room", "future", "chip", "nes", "8 bit", "rave", "skool", "experimental", "motown", "complextro", "baiao", "songo",
         "rumba", "mambo", "bachata", "cumbia", "reggaeton", "vapor", "gabber", "neo", "french", "filter", "uplifting", "melodic", "n bass", "drum",
         "solo", "kosmische", "rave", "mood", "synth")


def norm(p):
    t = re.sub(r"^/Volumes/[^/]+/", "/", p or "")
    t = re.sub(r"\.[A-Za-z0-9]{2,4}(#.*)?$", "", t)
    t = t.lower().replace("&", " and ")
    return re.sub(r"[\s_.\-+()\[\]#]+", " ", t)


class Lexicon:
    def __init__(self, styles):
        self.fam = {}      # catalog family -> kind
        self.parent = {}   # id -> parent id
        self.kind = {}     # id -> catalog | inferred | family
        pats = {}
        for s in styles:
            sid, fam = s["id"], s["family"]
            self.parent[sid] = fam if fam != sid else None  # style named like its family (ambient, downtempo): no self-parent
            self.kind[sid] = "catalog"
            pats[sid] = ALIASES.get(sid, re.escape(sid.replace("_", " ")))
            if fam != sid:
                self.kind.setdefault(fam, "family")
                self.parent.setdefault(fam, None)
        for fam, rx in FAMILY_WORDS.items():
            self.kind.setdefault(fam, "family")
            self.parent.setdefault(fam, None)
            pats[fam] = rx if fam not in pats else pats[fam] + "|" + rx
        for gid, (par, rx) in INFERRED.items():
            self.kind[gid] = "family" if par is None else "inferred"
            self.parent[gid] = par
            if par:
                self.kind.setdefault(par, "family")
                self.parent.setdefault(par, None)
            pats[gid] = rx
        self.rx = {g: re.compile(p) for g, p in pats.items() if p != r"^$"}
        self.stems = tuple(STEMS)
        self.cache = {}

    def chain(self, g):
        out = []
        while g and g not in out:
            out.append(g)
            g = self.parent.get(g)
        return out

    def labels(self, text, family=None):
        """genre ids named in the path text (plus every parent), and the row's own family field"""
        got = self.cache.get(text)
        if got is None:
            t = norm(text)
            got = set()
            if any(x in t for x in self.stems):
                for g, rx in self.rx.items():
                    if rx.search(t):
                        got.update(self.chain(g))
            if len(self.cache) < 400000:
                self.cache[text] = got
        out = set(got)
        if family and family not in ("unlabeled", "unknown") and family in self.kind:
            out.update(self.chain(family))
        return out


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else None


def sound_families(rows):
    """per family: cluster ids, centroids (z space), mu / sd of the audio-clustered members, and the names"""
    byfam = collections.defaultdict(list)
    for r in rows:
        if r.get("assigned") == "audio" and r.get("audio") and r["audio"].get("bands"):
            byfam[r["family"]].append(SC.feat(r["audio"]))
    return byfam


class Cl:
    """nearest-cluster projection of one audio row onto its family's clusters"""
    PART = {"kick": "drums", "snare": "drums", "hat": "drums", "cymbal": "drums", "tom": "drums", "clap": "drums", "perc": "drums",
            "drums": "drums", "bass": "bass", "keys": "keys", "chord": "keys", "strings": "strings", "brass": "brass", "guitar": "guitar",
            "vocal": "choir", "pad": "synth", "synth": "synth", "lead": "synth", "pluck": "synth", "fx": "texture"}

    def __init__(self, clusters, inst_rows):
        feats = sound_families(inst_rows)
        self.fam = {}
        for fam, e in clusters["families"].items():
            if not e["clusters"] or fam not in feats:
                continue
            F = np.array(feats[fam])
            cm = np.nanmean(F, 0)
            cm = np.where(np.isnan(cm), 0, cm)
            F = np.where(np.isnan(F), cm, F)
            self.fam[fam] = (F.mean(0), F.std(0) + 1e-9, np.array([c["centroid"] for c in e["clusters"]]),
                             [c["id"] for c in e["clusters"]])
        self.names = {c["id"]: c["name"] for e in clusters["families"].values() for c in e["clusters"]}

    def assign(self, r):
        fam = self.PART.get(r.get("part"))
        if fam not in self.fam or not r.get("bands"):
            return None
        mu, sd, C, ids = self.fam[fam]
        v = (SC.feat(r) - mu) / sd
        ok = ~np.isnan(v)
        d = ((C[:, ok] - v[ok]) ** 2).mean(1)
        return ids[int(d.argmin())]


class Gen:
    def __init__(self, gid):
        self.id = gid
        self.n = collections.Counter()               # rows per source
        self.clu = collections.defaultdict(collections.Counter)  # cluster id -> {audio, inst_audio, inst_tags}
        self.packs = collections.Counter()
        self.parts = collections.Counter()
        self.bpm = {"audio": [], "midi": []}
        self.audio_groove = {"n": 0, "swing": [], "tight": [], "accent": np.zeros(16), "onsets_s": []}
        self.audio_energy = {"rms": [], "low": [], "mid": [], "high": []}
        self.dr = {"n": 0, "swing": [], "lane_hits": np.zeros(8), "kick": collections.Counter(), "snare": collections.Counter(),
                   "full": collections.Counter(), "off_kick": [0, 0], "backbeat": [0, 0], "datasets": collections.Counter()}
        self.bass = {"n": 0, "minor": 0, "major": 0}
        self.chord = {"n": 0, "major": 0, "minor": 0, "mood": collections.Counter()}
        self.arc = {"n": 0, "act": np.zeros(10), "vel": np.zeros(10), "parts": np.zeros(10), "major": 0, "minor": 0}
        self.skipped = collections.Counter()


def tenths(xs):
    n = len(xs)
    return np.array([np.mean(xs[i * n // 10:max(i * n // 10 + 1, (i + 1) * n // 10)]) for i in range(10)], float)


def arc_shape(c):
    """shape word for a 10-point energy curve (mean of per-song curves, each divided by its own mean)"""
    c = np.asarray(c)
    if c.max() <= 0:
        return "empty"
    m = c / c.max()
    head, mid, tail = m[:3].mean(), m[3:7].mean(), m[7:].mean()
    if m.min() > 0.82:
        return "plateau"
    if tail > 1.2 * head and tail >= mid * 0.95:
        return "rising"
    if head > 1.2 * tail and head >= mid * 0.95:
        return "falling"
    if mid > 1.1 * max(head, tail):
        return "arch"
    if min(head, tail) > 1.1 * mid:
        return "valley"
    return "plateau"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default=DATA)
    ap.add_argument("--out")
    ap.add_argument("--no-arcs", action="store_true")
    ap.add_argument("--no-midi", action="store_true")
    a = ap.parse_args()
    t0 = time.time()
    data = os.path.expanduser(a.data)
    out_path = a.out or os.path.join(data, "atlas", "genres.json")
    cat = json.load(open(CATALOG))["styles"]
    lex = Lexicon(cat)
    G = {}
    gen = lambda g: G.setdefault(g, Gen(g))
    clusters = json.load(open(os.path.join(data, "atlas", "clusters.json")))
    inst = [json.loads(l) for l in open(os.path.join(data, "atlas", "instruments_clustered.jsonl"))]
    cl = Cl(clusters, inst)
    src_stats = {}

    # 1. instruments named after a genre, counted in their own cluster
    n_inst = 0
    for r in inst:
        txt = " ".join(str(x) for x in [r.get("name"), r.get("category"), " ".join(r.get("moods") or []), " ".join(r.get("timbre") or []),
                                       r.get("types"), r["id"] if r.get("source") in ("exs", "nkc") else ""] if x)
        gs = lex.labels(txt)
        if not gs:
            continue
        n_inst += 1
        for g in gs:
            e = gen(g)
            e.n["instruments"] += 1
            if r.get("cluster"):
                e.clu[r["cluster"]]["inst_" + r["assigned"]] += 1
    src_stats["instruments"] = {"rows": len(inst), "named_after_a_genre": n_inst}

    # 2. audio rows
    seen, n_aud, n_lab, n_proj = set(), 0, 0, 0
    for fn in ("features.jsonl", "air_features.jsonl"):
        p = os.path.join(data, "audio", fn)
        if not os.path.exists(p):
            continue
        for ln in open(p):
            try:
                r = json.loads(ln)
            except ValueError:
                continue
            if not r.get("bands") or r["src"] in seen:
                continue
            seen.add(r["src"])
            n_aud += 1
            gs = lex.labels(r["src"])
            if not gs:
                continue
            n_lab += 1
            c = cl.assign(r)
            n_proj += c is not None
            pack = (r.get("pack") or "").split("/")[0] or os.path.basename(os.path.dirname(r["src"]))
            for g in gs:
                e = gen(g)
                e.n["audio"] += 1
                e.packs[pack] += 1
                e.parts[r.get("part") or "unknown"] += 1
                if c:
                    e.clu[c]["audio"] += 1
                if r.get("loop") and r.get("bpm") and 40 <= r["bpm"] <= 260:
                    e.bpm["audio"].append(float(r["bpm"]))
                for k in ("rms", "low", "mid", "high"):
                    if r.get(k) is not None:
                        e.audio_energy[k].append(r[k])
                gr = r.get("groove")
                if gr:
                    ag = e.audio_groove
                    ag["n"] += 1
                    if gr.get("swing") is not None:
                        ag["swing"].append(gr["swing"])
                    if gr.get("tight") is not None:
                        ag["tight"].append(gr["tight"])
                    if gr.get("accent") and len(gr["accent"]) == 16:
                        ag["accent"] += np.array(gr["accent"])
                    if r.get("seconds"):
                        ag["onsets_s"].append(r["onsets"] / r["seconds"])
    src_stats["audio"] = {"rows": n_aud, "named_after_a_genre": n_lab, "projected_on_a_cluster": n_proj}

    # 3. MIDI rows
    T = re.compile(r'^\{"type": ?"(\w+)"')
    S = re.compile(r'"src": ?"([^"]*)"')
    F = re.compile(r'"family": ?"([^"]*)"')
    n_mid = collections.Counter()
    n_seen = 0
    mid_files = [("store/inbox.jsonl", True), ("store/public_drums.jsonl", False), ("store/chords.jsonl", False)]
    for fn, _ in ([] if a.no_midi else mid_files):
        p = os.path.join(data, fn)
        if not os.path.exists(p):
            continue
        for ln in open(p):
            t = T.match(ln)
            s = S.search(ln)
            if not t or not s:
                continue
            t, s = t[1], s[1]
            if s.startswith("card:") or s.startswith("synth"):
                continue  # style-card drums are declarations; synthetic rows are our own output
            n_seen += 1
            if n_seen % 500000 == 0:
                print("  midi rows", n_seen, flush=True)
            f = F.search(ln)
            gs = lex.labels(s.split("#")[0], f[1] if f else None)
            n_mid[t] += 1
            if not gs:
                continue
            if t not in ("drum_groove", "bass_seq", "chord_prog"):
                for g in gs:
                    gen(g).n["midi_" + t] += 1
                continue
            r = json.loads(ln)
            if not r.get("ok", True):
                for g in gs:
                    gen(g).skipped[t] += 1
                continue
            ds = s.split("/")[0].replace("inbox:", "").replace("groove:", "groove ")
            for g in gs:
                e = gen(g)
                e.n["midi_" + t] += 1
                bpm = r.get("bpm")
                if bpm and 40 <= bpm <= 260:
                    e.bpm["midi"].append(float(bpm))
                if t == "drum_groove":
                    d = e.dr
                    d["n"] += 1
                    d["datasets"][ds] += 1
                    if r.get("swing") is not None:
                        d["swing"].append(r["swing"])
                    L = (r["lanes"] + [""] * 8)[:8]
                    for i, l in enumerate(L):
                        d["lane_hits"][i] += sum(ch != "." for ch in l)
                    if L[0]:
                        d["kick"][L[0]] += 1
                        for i, ch in enumerate(L[0][:16]):
                            if ch != ".":
                                d["off_kick"][0] += 1
                                d["off_kick"][1] += i % 4 != 0
                    if L[1]:
                        d["snare"][L[1]] += 1
                        for i in (4, 12):
                            d["backbeat"][1] += 1
                            d["backbeat"][0] += len(L[1]) > i and L[1][i] != "."
                    d["full"]["|".join(L)] += 1
                elif t == "bass_seq":
                    e.bass["n"] += 1
                    e.bass["minor" if r.get("mode") == "Minor" else "major"] += 1
                else:
                    e.chord["n"] += 1
                    if r.get("mode") in ("Major", "Minor"):
                        e.chord[r["mode"].lower()] += 1
                    for w in r.get("mood") or []:
                        e.chord["mood"][w] += 1
    src_stats["midi"] = {"rows_by_type": dict(n_mid), "note": "labelled rows are those with a family or a genre word in the source path"}

    # 4. arcs
    n_arc = n_arc_lab = 0
    if not a.no_arcs:
        p = os.path.join(data, "arcs", "detail.jsonl.gz")
        try:
            for ln in gzip.open(p, "rt"):
                if not ln.endswith("\n"):
                    break
                if '"song_arc"' not in ln or not ln.startswith('{"unit": "bar"') and not ln.startswith('{"unit":"bar"'):
                    continue
                s = S.search(ln)
                f = F.search(ln)
                if not s:
                    continue
                n_arc += 1
                gs = lex.labels(s[1], f[1] if f else None)
                if not gs:
                    continue
                r = json.loads(ln)
                if not r.get("ok", True) or r["n"] < 20:
                    continue
                dens = np.array(r["dens"], float)
                drum = np.array(r.get("drum") or [0] * r["n"], float)
                act = dens + drum
                if act.mean() <= 0:
                    continue
                vel = np.array(r["vel"], float)
                parts = np.array([bin(x).count("1") for x in r["parts"]], float)
                a_t, v_t, p_t = tenths(act / act.mean()), tenths(vel), tenths(parts)
                n_arc_lab += 1
                mode = (r.get("key") or [None, None])[1]
                for g in gs:
                    e = gen(g)
                    e.n["arcs"] += 1
                    e.arc["n"] += 1
                    e.arc["act"] += a_t
                    e.arc["vel"] += v_t
                    e.arc["parts"] += p_t
                    if mode in ("Major", "Minor"):
                        e.arc[mode.lower()] += 1
        except EOFError:
            pass
    src_stats["arcs"] = {"bar song_arc rows": n_arc, "labelled with n >= 20": n_arc_lab}

    # 5. declared (catalog) side
    styles = {s["id"]: s for s in cat}
    try:
        graph = {s["id"]: s for s in json.load(open(os.path.join(data, "models", "shippable", "genre_graph.json")))["styles"]}
    except (OSError, ValueError):
        graph = {}

    def finish(g, e):
        out = {"id": g, "kind": lex.kind.get(g, "inferred"), "parent": lex.parent.get(g)}
        if lex.kind.get(g) == "family":
            out["children"] = sorted(c for c, par in lex.parent.items() if par == g)
        st = styles.get(g)
        if st:
            out["declared"] = {"bpm": st["bpm"], "swing": st.get("swing"), "groove": st.get("groove"), "drums": st.get("drums"),
                               "pillars": (graph.get(g) or {}).get("pillars"), "note": "hand-written catalog card, not measured"}
        # clusters
        tot = sum(sum(c.values()) for c in e.clu.values())
        allc = collections.Counter()
        for gg in G.values():
            for c, v in gg.clu.items():
                allc[c] += sum(v.values())
        gtot = sum(allc.values()) or 1
        rows = []
        for c, v in e.clu.items():
            n = sum(v.values())
            rows.append({"cluster": c, "name": cl.names.get(c), "n": n, "share": round(n / tot, 4), "lift": round((n / tot) / (allc[c] / gtot), 2),
                         "from": {k: x for k, x in v.items()}})
        rows.sort(key=lambda r: -r["n"])
        out["clusters"] = {"n": tot, "top": rows[:10]}
        # tempo
        tm = {}
        allb = e.bpm["audio"] + e.bpm["midi"]
        if allb:
            tm = {"n": len(allb), "p10": pct(allb, .1), "p50": pct(allb, .5), "p90": pct(allb, .9), "min": min(allb), "max": max(allb),
                  "by_source": {k: {"n": len(v), "p10": pct(v, .1), "p50": pct(v, .5), "p90": pct(v, .9)} for k, v in e.bpm.items() if v}}
        out["tempo"] = tm
        # grooves
        gr = {}
        ag = e.audio_groove
        if ag["n"]:
            gr["audio_loops"] = {"n": ag["n"], "swing_median": pct(ag["swing"], .5) if ag["swing"] else None, "swing_n": len(ag["swing"]),
                                 "tight_median": pct(ag["tight"], .5) if ag["tight"] else None,
                                 "accent16": [round(float(x), 3) for x in ag["accent"] / ag["n"]], "onsets_per_s_median": pct(ag["onsets_s"], .5)}
        d = e.dr
        if d["n"]:
            top = lambda c: [{"pattern": k, "n": v, "share": round(v / d["n"], 4)} for k, v in c.most_common(5)]
            gr["midi_drums"] = {"n": d["n"], "swing_mean": round(float(np.mean(d["swing"])), 4) if d["swing"] else None,
                                "hits_per_bar_by_lane": {k: round(float(x / d["n"]), 2) for k, x in zip(
                                    ["kick", "snare", "chat", "ohat", "clap", "tom", "htom", "bell"], d["lane_hits"])},
                                "kick_offbeat_share": round(d["off_kick"][1] / d["off_kick"][0], 3) if d["off_kick"][0] else None,
                                "snare_on_2_4_share": round(d["backbeat"][0] / d["backbeat"][1], 3) if d["backbeat"][1] else None,
                                "distinct_grooves": len(d["full"]), "top_kick": top(d["kick"]), "top_snare": top(d["snare"]),
                                "top_full": top(d["full"])[:3], "datasets": dict(d["datasets"].most_common(5))}
        out["grooves"] = gr
        # arcs
        ar = e.arc
        if ar["n"]:
            act = ar["act"] / ar["n"]
            out["arc"] = {"n": ar["n"], "shape": arc_shape(act), "activity_by_tenth": [round(float(x), 3) for x in act],
                          "peak_tenth": int(act.argmax()) + 1, "vel_by_tenth": [round(float(x), 1) for x in ar["vel"] / ar["n"]],
                          "parts_by_tenth": [round(float(x), 2) for x in ar["parts"] / ar["n"]]}
        # mood
        mo = {}
        maj = ar["major"] + e.chord["major"] + e.bass["major"]
        mnr = ar["minor"] + e.chord["minor"] + e.bass["minor"]
        if maj + mnr:
            mo["major_share"] = round(maj / (maj + mnr), 3)
            mo["mode_n"] = maj + mnr
        if e.chord["mood"]:
            from genre_graph import MOOD_VA
            w = [(MOOD_VA[k], v) for k, v in e.chord["mood"].items() if k in MOOD_VA]
            if w:
                nn = sum(v for _, v in w)
                mo["mood_words"] = dict(e.chord["mood"].most_common(6))
                mo["valence_from_mood_words"] = round(sum(va[0] * v for va, v in w) / nn, 3)
                mo["arousal_from_mood_words"] = round(sum(va[1] * v for va, v in w) / nn, 3)
        if e.audio_energy["rms"]:
            mo["audio_rms_median"] = round(float(np.median(e.audio_energy["rms"])), 4)
            mo["audio_spectrum_low_mid_high_median"] = [round(float(np.median(e.audio_energy[k])), 3) for k in ("low", "mid", "high")]
        if st and graph.get(g, {}).get("pillars"):
            mo["declared"] = {k: graph[g]["pillars"][k] for k in ("valence", "energy", "danceability") if k in graph[g]["pillars"]}
        out["mood"] = mo
        out["evidence"] = {"n_rows": {k: v for k, v in sorted(e.n.items())}, "sources": sorted(
            {"audio" if k == "audio" else "instruments" if k == "instruments" else "arcs" if k == "arcs" else "midi" for k in e.n}),
            "audio_packs": dict(e.packs.most_common(6)), "audio_parts": dict(e.parts.most_common(6)), "midi_rows_skipped_not_ok": dict(e.skipped)}
        has_c = tot >= 5
        has_m = any(k.startswith("midi") for k in e.n)
        has_a = e.n["audio"] > 0
        out["evidence"]["tier"] = ("cluster" if has_c else "midi_only" if has_m else "audio_only" if has_a else
                                    "arcs_only" if e.n["arcs"] else "declared_only")
        return out

    # every catalog id and family is listed even with zero rows
    for sid, s in styles.items():
        gen(sid)
        gen(s["family"])
    genres = {g: finish(g, e) for g, e in sorted(G.items())}
    cover = collections.defaultdict(collections.Counter)
    for g in genres.values():
        cover[g["kind"]][g["evidence"]["tier"]] += 1
    doc = {"generated": time.strftime("%Y-%m-%d %H:%M"), "seconds": round(time.time() - t0), "sources": src_stats,
           "coverage_by_kind": {k: dict(v) for k, v in cover.items()},
           "note": "counts, not guesses: see evidence.n_rows per genre. Clusters of audio rows are nearest-centroid projections of single files onto "
                   "instrument clusters; 'instruments' rows use the instrument's own cluster. Data is a guardrail, not a template.",
           "genres": genres}
    json.dump(doc, open(out_path, "w"), indent=1)

    print("%d genres -> %s (%.0f s)" % (len(genres), out_path, time.time() - t0))
    print("sources:", json.dumps(src_stats))
    print("\n%-20s %-9s %-10s %6s %6s %6s %6s  %-14s %s" % ("genre", "kind", "tier", "audio", "inst", "midiDr", "arcs", "tempo p10-p90 (n)", "top 3 clusters (n)"))
    order = sorted(genres.values(), key=lambda g: -(sum(g["evidence"]["n_rows"].values())))
    for g in order[:40]:
        n = g["evidence"]["n_rows"]
        tm = g["tempo"]
        tp = "%d-%d (%d)" % (tm["p10"], tm["p90"], tm["n"]) if tm else "-"
        tc = "; ".join("%s (%d, x%.1f)" % (c["name"] or c["cluster"], c["n"], c["lift"]) for c in g["clusters"]["top"][:3]) or "-"
        print("%-20s %-9s %-10s %6d %6d %6d %6d  %-14s %s" % (g["id"], g["kind"], g["evidence"]["tier"], n.get("audio", 0), n.get("instruments", 0),
                                                              n.get("midi_drum_groove", 0), n.get("arcs", 0), tp, tc))
    print("\ncoverage (genres by evidence tier):")
    for k, v in doc["coverage_by_kind"].items():
        print("  %-9s %s" % (k, dict(v)))


if __name__ == "__main__":
    main()
