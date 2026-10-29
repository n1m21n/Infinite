#!/usr/bin/env python3
"""Goal item 2, per-role version (signed off 2026-10-29): is each live Note Sequencer layer inside its own family's corpus spread?
    role_spread.py <patches.jsonl> [--n 150000] [--out FILE]
patches.jsonl comes from wasm_patches.js (the live SampleStyles engine, not styled.py). A layer whose enabled-note range midpoint is < 48 is
a bass layer and is compared with corpus bass channels; the rest with melodic channels (every non-drum, non-bass family).
Per role and axis: register = range midpoint, density = enabled steps per second. Pass >= 90% inside the corpus 5th-95th range."""
import argparse, json, os, re, sys
import numpy as np
S = os.path.expanduser("~/Infinite/training-data/store/")

def layers(row):
    out = []
    for n in re.split(r"\nnode ", row["patch"]):
        if "Notes Note Sequencer" not in n.split("\n")[0]: continue
        steps = int(re.search(r"i steps (\d+)", n).group(1)); rate = float(re.search(r"f rateBeats (\S+)", n).group(1))
        on = [int(re.search(r"i note%d (\d+)" % i, n).group(1)) for i in range(steps) if re.search(r"b en%d 1\b" % i, n)]
        if not on: continue
        out.append({"id": row["id"], "seed": row["seed"], "mid": (min(on) + max(on)) / 2, "dens": len(on) / steps / (rate * 60.0 / row["bpm"])})
    return out

def corpus(n):
    reg, den = {"bass": [], "melodic": []}, {"bass": [], "melodic": []}
    for i, l in enumerate(open(S + "midi_full.jsonl")):
        if i >= n: break
        r = json.loads(l); sec = r.get("seconds") or 0
        if sec < 5: continue
        for c in r["channels"].values():
            f = c.get("family")
            if f == "drums" or not c.get("notes") or c.get("lo") is None: continue
            k = "bass" if f == "bass" else "melodic"
            reg[k].append((c["lo"] + c["hi"]) / 2); den[k].append(c["notes"] / sec)
    return reg, den

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("patches"); ap.add_argument("--n", type=int, default=150000); ap.add_argument("--out")
    a = ap.parse_args()
    L = [x for l in open(a.patches) for x in layers(json.loads(l))]
    reg, den = corpus(a.n); res = {"patches": a.patches, "layers": len(L)}
    for role in ("bass", "melodic"):
        G = [x for x in L if (x["mid"] < 48) == (role == "bass")]
        res[role] = {"n": len(G)}
        for ax, C, key in (("register", reg, "mid"), ("density", den, "dens")):
            p5, p50, p95 = np.percentile(C[role], [5, 50, 95]); g = np.array([x[key] for x in G])
            ins = float(((g >= p5) & (g <= p95)).mean()) if len(g) else None
            res[role][ax] = {"corpus_p5": round(p5, 2), "corpus_p50": round(p50, 2), "corpus_p95": round(p95, 2), "n_corpus": len(C[role]),
                             "gen_p5": round(float(np.percentile(g, 5)), 2) if len(g) else None, "gen_p50": round(float(np.median(g)), 2) if len(g) else None,
                             "gen_p95": round(float(np.percentile(g, 95)), 2) if len(g) else None, "inside": round(ins, 3) if ins is not None else None,
                             "below": int((g < p5).sum()), "above": int((g > p95).sum())}
    ins = [res[r][ax]["inside"] for r in ("bass", "melodic") for ax in ("register", "density") if res[r][ax]["inside"] is not None]
    res["verdict"] = "MET" if ins and min(ins) >= 0.9 else "MISS"
    js = json.dumps(res, indent=1); print(js)
    if a.out: open(a.out, "w").write(js + "\n")

if __name__ == "__main__":
    main()
