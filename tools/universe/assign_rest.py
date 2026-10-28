#!/usr/bin/env python3
"""Block 3 (NEXT-SESSION), no-render route: the instruments sound_clusters.py could not place (assigned "none") are not instruments but phrases, one-shots,
psychoacoustic FX, retro textures and Spitfire loop libraries; the library path says which. They get a content-kind cluster from that path
("phrase:<group>", "oneshot", "fx:psychoacoustic", "texture:retro", "loop:<library>"), assigned = "path". A kind is not a timbre cluster: rows carry
`kind_cluster: true`, so a timbre model can leave them out. Reads atlas/instruments_clustered.jsonl, writes atlas/instruments_assigned.jsonl and prints the share
still unassigned. Rendering the rest (Omnisphere sound sources need a patch that uses them) stays open if timbre clusters are wanted.
    assign_rest.py [--atlas ~/Infinite/training-data/atlas]"""
import argparse, collections, json, os


def kind(r):
    cat = r.get("category") or ""; parts = [p.strip() for p in cat.split("/")]; low = cat.lower()
    if r["source"] == "nkc": return "loop:" + (cat or "unknown")[:48]
    if "/phrases" in low or "phrases" in (r.get("types") or "").lower() or r["name"].startswith("PHR"):
        g = parts[3] if len(parts) > 3 else (parts[2] if len(parts) > 2 else "other"); return "phrase:" + g.lower()
    if "oneshot" in low: return "oneshot"
    if "psychoacoustic" in low: return "fx:psychoacoustic"
    if "retro" in low: return "texture:retro"
    if len(parts) > 1 and parts[0]: return "other:" + "/".join(parts[:2]).lower()[:48]
    return None


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--atlas", default=os.path.expanduser("~/Infinite/training-data/atlas")); a = ap.parse_args()
    rows = [json.loads(l) for l in open(os.path.join(a.atlas, "instruments_clustered.jsonl"))]; c = collections.Counter(); left = 0
    for r in rows:
        if r.get("assigned") == "none":
            k = kind(r)
            if k: r["cluster"] = k; r["assigned"] = "path"; r["kind_cluster"] = True; c[k.split(":")[0]] += 1
            else: left += 1
    with open(os.path.join(a.atlas, "instruments_assigned.jsonl"), "w") as f:
        for r in rows: f.write(json.dumps(r) + "\n")
    n = len(rows); print("rows %d; assigned by path %s; still unassigned %d (%.1f%%)" % (n, dict(c), left, 100.0 * left / n))
    print("assigned total: ", collections.Counter(r.get("assigned") for r in rows))


if __name__ == "__main__":
    main()
