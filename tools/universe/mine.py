#!/usr/bin/env python3
"""Mine a set of .inf patches for how their author actually plays: node shapes, chains, which params get modulated.
    mine.py patches.txt --out profile.json      (patches.txt = one .inf path per line)
The profile feeds the generators: param weights (what to modulate), modulator-source weights, size
distribution, and the common 2/3-node chains per domain."""
import argparse, collections, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u


def read(path):
    types, cat, edges, mods = {}, {}, [], []
    for l in open(path, errors="replace"):
        p = l.split()
        if not p:
            continue
        if p[0] == "node" and len(p) >= 4:
            types[int(p[1])] = " ".join(p[3:]); cat[int(p[1])] = p[2]
        elif p[0] in ("cable", "geo", "aud", "note") and len(p) >= 4:
            try: edges.append((p[0], int(p[3]), int(p[1])))
            except ValueError: pass
        elif p[0] == "mod" and len(p) >= 4:
            try: mods.append((int(p[1]), p[2], int(p[3])))
            except ValueError: pass
    return types, cat, edges, mods


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("list"); ap.add_argument("--out", default="profile.json"); ap.add_argument("--min-nodes", type=int, default=4)
    a = ap.parse_args()
    facts = u.load_facts()
    paths = [l.strip() for l in open(a.list) if l.strip()]
    pk, tk, src, pair, tri, sizes, mrate, ncount = [collections.Counter() for _ in range(8)]
    chains = collections.defaultdict(collections.Counter)
    used = 0
    for path in paths:
        try: types, cat, edges, mods = read(path)
        except OSError: continue
        types = {k: v for k, v in types.items() if v not in ("Comment", "Group")}
        if len(types) < a.min_nodes: continue
        ncount.update(set(types.values()))
        used += 1; sizes[min(len(types) // 4 * 4, 40)] += 1; mrate[round(len(mods) / max(1, len(types)), 1)] += 1
        for dst, key, s in mods:
            if dst in types and s in types:
                if key.isdigit():
                    ms = {m["index"]: m.get("key") or m.get("label") for m in facts.get(types[dst], {}).get("modulatable", [])}
                    key = ms.get(int(key), "?" + key)
                pk[key] += 1; tk[(types[dst], key)] += 1; src[types[s]] += 1
        out = collections.defaultdict(list)
        for kind, s, d in edges:
            if s in types and d in types:
                pair[(kind, types[s], types[d])] += 1; out[d].append(s)
        for kind, s, d in edges:
            for c in out.get(s, []):
                if s in types and d in types and c in types:
                    tri[(types[c], types[s], types[d])] += 1
    prof = {"patches": used, "node_count": ncount.most_common(),
            "param_key": pk.most_common(80), "type_param": [[a_, b_, n] for (a_, b_), n in tk.most_common(150)],
            "mod_source": src.most_common(40), "size_hist": sorted(sizes.items()), "mods_per_node": sorted(mrate.items()),
            "pairs": [[*k, n] for k, n in pair.most_common(150)], "triples": [[*k, n] for k, n in tri.most_common(80)]}
    json.dump(prof, open(a.out, "w"), indent=1)
    print("patches used", used)
    print("top modulated param keys:", pk.most_common(25))
    print("top (node,param):", [(a_ + "." + b_, n) for (a_, b_), n in tk.most_common(25)])
    print("modulator sources:", src.most_common(15))
    print("sizes:", sorted(sizes.items())); print("mods/node:", sorted(mrate.items()))


if __name__ == "__main__":
    main()
