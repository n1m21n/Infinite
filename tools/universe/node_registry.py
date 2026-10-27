#!/usr/bin/env python3
"""Universal node + param registry: every Infinite node type and every parameter row as one flat table, each tied to the rest of
the universe (docs/plans/data-universe/recipes.md, axes-strategy.md).
    node_registry.py build [--describe describe_all.json] [--presets ~/Infinite/training-data/presets] [--out atlas/node_registry.json]
    node_registry.py report
Sources: `Infinite --describe --json` (describe_all.json: 342 types, every row with key, kind, min, max, options, modulatable, pins).
Per param it records scale (linear / log by range), a semantic role by name (prior, replaced by measured responses once the render
harness runs), how often that role appears in the preset corpora (presence, by engine), and for each node the character words
(character_matrix.json lift) and the pin kinds it can take or give (the compatibility graph). Statistics only."""
import argparse, collections, json, math, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
TD = os.path.expanduser("~/Infinite/training-data")

# role prior by param key, first match wins; each role names the percept family it should move (axes-strategy L1/L2)
ROLES = [
    ("pitch", r"pitch|tune|semi|cents|transpose|octave|coarse|fine|detune|interval|note|key"),
    ("resonance", r"reson|^q$|peak|emphasis|feedbackq"),
    ("tone", r"cutoff|freq|tone|bright|hpf|lpf|filter|hp$|lp$|slope|tilt|formant|centroid|harmonic|partial|spectr"),
    ("attack_shape", r"attack|atk|decay|dec$|sustain|sus$|release|rel$|hold|envelope|env|trans"),
    ("space", r"reverb|room|size|predelay|damp|pre.?delay|tail|diffus|early|spread"),
    ("echo", r"delay|feedback|fdbk|echo|repeat|tap|time|length|interval"),
    ("motion", r"rate|speed|lfo|depth|sweep|wobble|phase|mod|rotate|drift|wander|jitter"),
    ("grit", r"drive|dist|bits|bit|crush|sat|fold|clip|noise|gritty|hard|shape|warp|wavefold|downsample|sample.?rate"),
    ("width", r"width|stereo|pan|balance|spatial|azimuth|elev"),
    ("density", r"density|voices|count|grain|unison|number|steps|polyphon|amount"),
    ("loudness", r"gain|level|volume|vol$|amp$|output|trim|velocity|thresh|ratio|makeup|compress|limit"),
    ("blend", r"mix|wet|dry|blend|amount|strength|intensity|opacity"),
    ("rhythm", r"swing|groove|tempo|bpm|beat|bar|clock|gate|division|probab|chance|humaniz"),
]


def role_of(key, label):
    s = (key or label or "").lower()
    for role, rx in ROLES:
        if re.search(rx, s):
            return role
    return None


def scale_of(lo, hi, kind):
    if kind in ("bool", "text", "color"):
        return kind
    if lo is None or hi is None:
        return "unbounded"
    if lo > 0 and hi / lo >= 20:
        return "log"
    return "linear"


def corpora_presence(presets):
    """presence of each role in the preset corpora: role -> {presets, engines}"""
    pres, eng = collections.Counter(), collections.defaultdict(set)
    n = 0
    for fn in sorted(os.listdir(presets)):
        if not fn.endswith(".jsonl"):
            continue
        for ln in open(os.path.join(presets, fn)):
            try:
                r = json.loads(ln)
            except ValueError:
                continue
            seen = set()
            mods = r.get("modules") or {}
            for m in mods.values():
                if isinstance(m, dict):
                    for k in m:
                        ro = role_of(str(k), "")
                        if ro:
                            seen.add(ro)
            for ro in seen:
                pres[ro] += 1
                eng[ro].add(r.get("engine") or fn)
            n += 1
    return n, {k: {"presets": pres[k], "engines": len(eng[k])} for k in pres}


def build(a):
    types = json.load(open(os.path.join(HERE, a.describe)))["types"]
    cm_path = os.path.join(TD, "atlas", "character_matrix.json")
    cm = json.load(open(cm_path)) if os.path.exists(cm_path) else {"effects": {}, "synths": {}}
    words = collections.defaultdict(dict)  # node -> word -> lift
    for kind in ("effects", "synths"):
        for w, row in cm[kind].items():
            for node, v in row.items():
                words[node][w] = v["lift"]
    n_pre, presence = corpora_presence(os.path.expanduser(a.presets))
    nodes, params = [], []
    edge = collections.Counter()
    for t in types:
        mod = {m.get("key") or m["label"]: m for m in t["modulatable"]}
        ins = sorted({p["kind"] for p in t["inputs"]})
        outs = sorted({p["kind"] for p in t["outputs"]})
        for i in ins:
            for o in outs:
                edge[(i, o)] += 1
        nodes.append({"type": t["type"], "category": t["category"], "inputs": ins, "outputs": outs,
                      "n_params": len(t["rows"]), "n_modulatable": len(t["modulatable"]),
                      "hardware": t.get("hardware_driven", False), "words": words.get(t["type"], {})})
        for r in t["rows"]:
            m = mod.get(r["key"])
            ro = role_of(r["key"], "")
            params.append({"node": t["type"], "category": t["category"], "key": r["key"], "kind": r["kind"], "min": r["min"],
                           "max": r["max"], "options": r["options"], "ui": r["ui"], "modulatable": m is not None,
                           "scale": scale_of(r["min"], r["max"], r["kind"]), "role": ro,
                           "corpora": presence.get(ro) if ro else None})
    out = {"types": len(nodes), "params": len(params), "presets_scanned": n_pre, "presence": presence,
           "compat": {"%s>%s" % k: v for k, v in sorted(edge.items(), key=lambda kv: -kv[1])}, "nodes": nodes, "param_rows": params}
    p = os.path.expanduser(a.out)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    json.dump(out, open(p, "w"), separators=(",", ":"))
    print("wrote %d types, %d params to %s (%.1f MB)" % (len(nodes), len(params), p, os.path.getsize(p) / 1e6))


def report(a):
    d = json.load(open(os.path.expanduser(a.out)))
    P = d["param_rows"]
    print("%d types, %d param rows, %d modulatable, %d presets scanned" % (d["types"], d["params"], sum(p["modulatable"] for p in P), d["presets_scanned"]))
    print("\nkind:", dict(collections.Counter(p["kind"] for p in P)))
    print("scale:", dict(collections.Counter(p["scale"] for p in P)))
    tot = len(P)
    unrole = sum(1 for p in P if p["role"] is None)
    print("role prior coverage: %d / %d (%.0f%%) params have a role by name" % (tot - unrole, tot, 100 * (tot - unrole) / tot))
    print("\nrole x category (param counts):")
    cats = sorted({p["category"] for p in P})
    roles = [r for r, _ in ROLES]
    print("%-11s" % "" + "".join("%7s" % c[:6] for c in cats))
    for ro in roles + [None]:
        print("%-11s" % (ro or "(none)") + "".join("%7d" % sum(1 for p in P if p["role"] == ro and p["category"] == c) for c in cats))
    print("\nrole presence in preset corpora (presets / engines):")
    for ro in roles:
        v = d["presence"].get(ro, {})
        print("  %-13s %7s / %s" % (ro, v.get("presets", 0), v.get("engines", 0)))
    print("\npin compatibility (in>out, node count):", list(d["compat"].items())[:10])
    withw = sum(1 for n in d["nodes"] if n["words"])
    print("nodes with character words: %d; audio nodes without a word row:" % withw,
          [n["type"] for n in d["nodes"] if n["category"] in ("AudioEffects", "Synths") and not n["words"]][:25])


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("build", "report"))
    ap.add_argument("--describe", default="describe_all.json")
    ap.add_argument("--presets", default=os.path.join(TD, "presets"))
    ap.add_argument("--out", default=os.path.join(TD, "atlas", "node_registry.json"))
    a = ap.parse_args()
    build(a) if a.cmd == "build" else report(a)
