#!/usr/bin/env python3
"""Goal item 2, second half: are the live generator's notes inside the corpus spread?
    note_spread.py [--n 20000] [--out ../../docs/plans/data-universe/feel/note_spread.json]
Generated side: every Note Sequencer in the 34 style-card patches (render-runs/styled/*_1.inf).
Corpus side: midi_axes.jsonl + midi_full.jsonl (tempo, notes/s, register, silence, swing).
Test (fixed before looking): per axis, the share of generated patches inside the corpus 5th-95th percentile. Pass >= 90% on every axis."""
import argparse, glob, json, os, re, random
import numpy as np
S = os.path.expanduser("~/Infinite/training-data/store/")

def gen(path):
    t = open(path).read()
    bpm = float(re.search(r"transport (\S+)", t).group(1))
    nodes = re.split(r"\nnode ", t)
    en_all, regs, dens, sw = [], [], [], 0.0
    for n in nodes:
        for m in re.finditer(r"  f swing (\S+)", n): sw = max(sw, float(m.group(1)))
        if "Notes Note Sequencer" not in n.split("\n")[0]: continue
        steps = int(re.search(r"i steps (\d+)", n).group(1)); rate = float(re.search(r"f rateBeats (\S+)", n).group(1))
        en = [int(re.search(r"b en%d (\d)" % i, n).group(1)) if re.search(r"b en%d (\d)" % i, n) else 0 for i in range(steps)]
        pit = [int(re.search(r"i note%d (\d+)" % i, n).group(1)) if re.search(r"i note%d (\d+)" % i, n) else None for i in range(steps)]
        en_all.append(en)
        on = [p for e, p in zip(en, pit) if e and p is not None]
        if on: regs.append((min(on) + max(on)) / 2)
        dens.append(sum(en) / steps / (rate * 60.0 / bpm))
    if not en_all: return None
    L = max(map(len, en_all)); union = np.zeros(L)
    for e in en_all: union += np.resize(np.array(e), L)
    return {"bpm": bpm, "register": float(np.mean(regs)) if regs else None, "density": float(sum(dens)), "rest": float((union == 0).mean()), "swing": sw}

def corpus(n):
    ax = {}
    for l in open(S + "midi_axes.jsonl"):
        r = json.loads(l)
        if "a24" not in r or "a25" not in r or "a27" not in r: continue
        sw = (r["a25"].get("swing_ratio") or 1.0)
        ax[r["src"]] = {"bpm": r["a24"]["bpm"], "rest": r["a27"]["silence_ratio"], "swing": max(0.0, (sw - 1) / (sw + 1))}
    keys = list(ax); random.Random(0).shuffle(keys); keep = set(keys[:n]); out = []
    for l in open(S + "midi_full.jsonl"):
        r = json.loads(l)
        if r["src"] not in keep or r["src"] not in ax or not r.get("seconds"): continue
        ch = [c for c in r["channels"].values() if c.get("family") != "drums"]
        if not ch: continue
        d = dict(ax[r["src"]]); d["density"] = r["notes"] / r["seconds"]; d["register"] = float(np.mean([(c["lo"] + c["hi"]) / 2 for c in ch])); out.append(d)
    return out

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--n", type=int, default=20000); ap.add_argument("--out", default="../../docs/plans/data-universe/feel/note_spread.json")
    a = ap.parse_args()
    G = [g for g in (gen(p) for p in sorted(glob.glob(os.path.expanduser("~/Infinite/render-runs/styled/*_1.inf")))) if g]
    C = corpus(a.n); print(len(G), "generated patches;", len(C), "corpus files")
    res = {}
    for k in ("bpm", "register", "density", "rest", "swing"):
        c = np.array([x[k] for x in C if x.get(k) is not None]); g = np.array([x[k] for x in G if x.get(k) is not None])
        lo, hi = np.percentile(c, [5, 95]); inside = float(((g >= lo) & (g <= hi)).mean())
        res[k] = {"corpus_p5": round(float(lo), 3), "corpus_p50": round(float(np.median(c)), 3), "corpus_p95": round(float(hi), 3), "gen_p5": round(float(np.percentile(g, 5)), 3), "gen_p50": round(float(np.median(g)), 3), "gen_p95": round(float(np.percentile(g, 95)), 3), "inside": round(inside, 3), "n_gen": len(g), "n_corpus": len(c)}
        print("%-9s corpus p5/50/95 %7.3f %7.3f %7.3f | gen %7.3f %7.3f %7.3f | inside %.0f%%" % (k, lo, np.median(c), hi, np.percentile(g, 5), np.median(g), np.percentile(g, 95), inside * 100))
    res["verdict"] = "MET" if all(res[k]["inside"] >= 0.9 for k in ("bpm", "register", "density", "rest", "swing")) else "MISS"
    print(res["verdict"]); json.dump(res, open(a.out, "w"), indent=1)
main()
