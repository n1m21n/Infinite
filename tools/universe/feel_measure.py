#!/usr/bin/env python3
"""Measured knob -> axis responses for the feel dataset (ml-venv python).
  synth knobs:  signed slope of each of the 11 axis means (in spreads per full knob range) from stored one-knob pairs (own_<engine>.jsonl)
  effect knobs: delta at frac 1 vs frac 0 per node/param from response_native.jsonl (centroid octaves, rms dB, flatness, crest, width, decay, low share)
Writes docs/plans/data-universe/feel/dataset/_measured.json"""
import json, os, sys, collections
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, p3_characteristics as p3, sound_data as sd
OUT = os.path.join(HERE, "../../docs/plans/data-universe/feel/dataset/_measured.json")
ENGINES = ["Analog", "Oscillator", "FM", "Metallic", "String", "Wavetable", "Equation Synth", "Partials", "Sampler", "Slicer", "Grain Molder", "Field Synth"]


def synth(node):
    table, sw, fixed = o.param_table(node); d = sd.load(o.out_path(node)); keys = [p["key"] for p in sw]
    keep = np.flatnonzero(d["ok"][:, 0]); V = {i: p3.vec(d["mel"][i, 0].astype(float) / 2 - 120) for i in keep}
    means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]; names = [p3.COLS[j].split(".")[0] for j in means]
    sd_ = np.nanstd(np.array([V[i][means] for i in keep]), 0) + 1e-9
    by = {}
    for i in keep: by.setdefault(d["group"][i], []).append(i)
    num = {j: np.zeros(len(means)) for j in range(len(keys))}; den = {j: 0.0 for j in range(len(keys))}; cnt = collections.Counter()
    for g, ix in by.items():
        K = d["knobs"][ix]
        for x in range(len(ix)):
            diff = np.abs(K - K[x]) > 0.005
            for y in np.flatnonzero(diff.sum(1) == 1):
                if y > x:
                    j = int(np.flatnonzero(diff[y])[0]); dk = float(K[y, j] - K[x, j])
                    mv = np.nan_to_num((V[ix[y]][means] - V[ix[x]][means]) / sd_)
                    num[j] += mv * dk; den[j] += dk * dk; cnt[j] += 1
    rows = []
    for j, k in enumerate(keys):
        if cnt[j] >= 3 and den[j] > 0:
            s = num[j] / den[j]
            rows.append({"knob": k, "pairs": int(cnt[j]), "activity": float(np.mean(np.abs(s))), "slope": {names[a]: round(float(s[a]), 3) for a in range(len(names))},
                         "range": [sw[j].get("lo"), sw[j].get("hi")]})
        else:
            rows.append({"knob": k, "pairs": int(cnt[j]), "activity": None, "slope": None, "range": None})
    return {"n_swept": len(keys), "n_fixed": len(fixed) if hasattr(fixed, "__len__") else None, "knobs": rows}


def effects():
    R = [json.loads(l) for l in open(os.path.expanduser("~/Infinite/training-data/audio/response_native.jsonl"))]
    g = collections.defaultdict(lambda: collections.defaultdict(list))
    for r in R:
        if r.get("param"): g[(r["node"], r["param"])][r["frac"]].append(r)
    base = collections.defaultdict(list)
    for r in R:
        if r.get("setting") == "default": base[(r["node"], r["stimulus"])].append(r)
    out = {}
    def m(rs, k):
        v = [x[k] for x in rs if isinstance(x.get(k), (int, float))]
        return float(np.nanmean(v)) if v else float("nan")
    for (node, param), fr in g.items():
        fs = sorted(fr)
        if len(fs) < 2: continue
        lo, hi = fr[fs[0]], fr[fs[-1]]
        d = {"fracs": fs, "n": sum(len(v) for v in fr.values()),
             "brightness_oct": round(float(np.log2((m(hi, "centroid") + 1) / (m(lo, "centroid") + 1))), 3),
             "loudness_db": round(20 * float(np.log10((m(hi, "rms") + 1e-6) / (m(lo, "rms") + 1e-6))), 2),
             "noisiness_d": round(m(hi, "flatness") - m(lo, "flatness"), 3),
             "crest_d": round(m(hi, "crest") - m(lo, "crest"), 2),
             "width_d": round(-(m(hi, "corr") - m(lo, "corr")), 3),
             "decay_ms_d": round(m(hi, "decay_ms") - m(lo, "decay_ms"), 0),
             "weight_d": round(m(hi, "low") - m(lo, "low"), 3),
             "silence_d": round(m(hi, "silence") - m(lo, "silence"), 3)}
        out["%s|%s" % (node, param)] = d
    return out


if __name__ == "__main__":
    res = {"synth": {}, "effects": effects()}
    for e in ENGINES:
        try: res["synth"][e] = synth(e); print(e, len(res["synth"][e]["knobs"]), "knobs")
        except Exception as ex: print(e, "FAILED", repr(ex)[:200]); res["synth"][e] = {"error": repr(ex)[:200]}
    os.makedirs(os.path.dirname(OUT), exist_ok=True); json.dump(res, open(OUT, "w"), indent=0)
    print("effects knobs", len(res["effects"]), "->", OUT)
