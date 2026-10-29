#!/usr/bin/env python3
"""Goal item 2, per-role version (signed off 2026-10-29): is each live Note Sequencer layer inside its own family's corpus spread?
    role_spread.py <patches.jsonl> [--n 150000] [--out FILE]
patches.jsonl comes from wasm_patches.js (the live SampleStyles engine, not styled.py). A layer whose enabled-note range midpoint is < 48 is
a bass layer and is compared with corpus bass channels; the rest with melodic channels (every non-drum, non-bass family).
Per role and axis: register = range midpoint, density = enabled steps per second. Pass >= 90% inside the corpus 5th-95th range."""
import argparse, json, os, re, sys
import numpy as np
S = os.path.expanduser("~/Infinite/training-data/store/")

def drum_voice(n):
    """A synth drum (SampleStyles drumSeq): a Wavetable with a pitch drop and no sustain (kick, tom), or a noise-only Analog (snare, hat)."""
    f = lambda k: float(m.group(1)) if (m := re.search(r"\n  [fi] %s (\S+)" % re.escape(k), n)) else None
    head = n.split("\n")[0]
    if head.endswith("Wavetable"): return (f("a.pitchAmount") or 0) >= 4 and (f("a.ampSustain") or 0) == 0
    if head.endswith("Analog"): return (f("noise") or 0) > 0 and (f("osc1Vol") or 0) == 0 and (f("osc2Vol") or 0) == 0
    return False

def layers(row, legacy=False):
    """Note Sequencer layers. Since 2026-10-29: a step counts only if enabled and its velocity is above 0 (silent steps are not notes), and
    sequencers that drive synth drums are left out (all targets drum voices, or one pitch on every step and a drum voice among the targets) (kicks and toms were counted as bass). legacy=True gives the earlier count."""
    out = []; nodes = {}
    for n in re.split(r"\nnode ", row["patch"]):
        m = re.match(r"(?:node )?(\d+) ", n)
        if m: nodes[m.group(1)] = n
    feeds = {}
    for dst, src in re.findall(r"^note (\d+) \d+ (\d+)$", row["patch"], re.M): feeds.setdefault(src, []).append(dst)
    for nid, n in nodes.items():
        if "Notes Note Sequencer" not in n.split("\n")[0]: continue
        steps = int(re.search(r"i steps (\d+)", n).group(1)); rate = float(re.search(r"f rateBeats (\S+)", n).group(1))
        vel = lambda i: float(v.group(1)) if (v := re.search(r"f vel%d (\S+)" % i, n)) else 1.0
        on = [int(re.search(r"i note%d (\d+)" % i, n).group(1)) for i in range(steps) if re.search(r"b en%d 1\b" % i, n) and (legacy or vel(i) > 0)]
        if not on: continue
        dst = [nodes[d] for d in feeds.get(nid, []) if d in nodes]
        same = len({re.search(r"i note%d (\d+)" % i, n).group(1) for i in range(steps)}) == 1   # drumSeq writes one pitch to every step
        if not legacy and dst and (all(drum_voice(d) for d in dst) or (same and any(drum_voice(d) for d in dst))): continue
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
    ap = argparse.ArgumentParser(); ap.add_argument("patches"); ap.add_argument("--n", type=int, default=150000); ap.add_argument("--out"); ap.add_argument("--legacy", action="store_true", help="the count before 2026-10-29 (silent steps and synth drums included)")
    a = ap.parse_args()
    L = [x for l in open(a.patches) for x in layers(json.loads(l), a.legacy)]
    reg, den = corpus(a.n); res = {"patches": a.patches, "layers": len(L), "count": "legacy" if a.legacy else "audible steps, synth drums excluded"}
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
