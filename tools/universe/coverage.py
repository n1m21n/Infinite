#!/usr/bin/env python3
"""Coverage of the generator against the engine's own node facts: which node types, cable kinds and categories the pools can ever reach.
    coverage.py [atlas.jsonl ...]   (atlases add 'actually used' counts)"""
import collections, json, sys
import universe as u, scene, visual

facts = u.load_facts(); P = scene.pools_v2(facts); VP = visual.vpools(facts)
pooled = set()
for k, v in P.items():
    if isinstance(v, list): pooled |= set(v)
for v in P["_V"].values(): pooled |= set(v)
pooled |= set(VP["I"]) | set(VP["F"]) | set(VP["B"])
base = lambda n: n.split(":")[0]
pooled_base = {base(n) for n in pooled}

used = collections.Counter()
for p in sys.argv[1:]:
    for l in open(p):
        r = json.loads(l)
        for n in r.get("names", []) + r.get("mods", []): used[base(n)] += 1

allt = {t: x for t, x in facts.items() if ":" not in t}
print("node types in engine: %d   reachable by some pool: %d" % (len(allt), len(pooled_base & set(allt))))
bycat = collections.defaultdict(lambda: [0, 0, []])
for t, x in allt.items():
    c = bycat[x["category"]]; c[0] += 1
    if t in pooled_base: c[1] += 1
    else: c[2].append((t, "hardware" if x["hardware_driven"] else "no pool"))
print("\n%-16s %5s %6s" % ("category", "types", "pooled"))
for k, (n, p, miss) in sorted(bycat.items()):
    print("%-16s %5d %6d   unreachable: %s" % (k, n, p, ", ".join("%s(%s)" % m for m in miss[:6]) + (" ..." if len(miss) > 6 else "")))

edges = collections.Counter()
for t, x in allt.items():
    for i in x["inputs"]:
        for o in {o["kind"] for o in x["outputs"]}: edges[(o, i["kind"])] += 0
kinds = sorted({i["kind"] for x in allt.values() for i in x["inputs"]} | {o["kind"] for x in allt.values() for o in x["outputs"]})
print("\ncable kinds in engine:", kinds)
print("kinds the generator wires: audio, note, image, geometry (+ modulation mods); others unwired:", [k for k in kinds if k not in ("audio", "note", "image", "geometry")])
if used:
    never = sorted(t for t in pooled_base & set(allt) if not used[t])
    print("\npooled but never drawn in the given atlases (%d): %s" % (len(never), ", ".join(never[:40])))
