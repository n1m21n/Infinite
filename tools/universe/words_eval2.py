#!/usr/bin/env python3
"""Claim C4, words, second measurement (IMPROVEMENT-PLAN.md section 5 step 1): richer word labels from preset names and tags.
    words_eval2.py [--out docs/plans/data-universe/feel/words_eval2.json]
Sources with measured audio and words:
  diva     rendered u-he Diva presets (preset_diva*.jsonl, diva_m1.jsonl), joined by name to their metadata (presets.jsonl, air_presets.jsonl):
           words from the name, description, usage, type tags and folder. Group (held out together) = the preset's bank folder.
  ableton  Ableton device previews (air_features.jsonl): words from the file name and folders. Group = the device folder.
  samples  sample files (features.jsonl, the first measurement's source): words from the file name. Group = the pack.
Features: 32 log bands, centroid, rolloff, flatness, crest, rms, low/mid/high, attack and decay, stereo width (1 - L/R correlation), onset count, length, and the 40-point envelope (time shape: what
'punchy', 'soft', 'tight' are about). Model: logistic regression per word. Test: 5 folds by group (crc32(group) % 5), AUC mean and sd over the folds
that hold >= 5 positives. A word is testable with >= 40 positives in total and >= 5 in at least 3 folds. Bar: AUC >= 0.75 per word."""
import argparse, json, math, os, re, zlib
import numpy as np
D = os.path.expanduser("~/Infinite/training-data")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../docs/plans/data-universe/feel/words_eval2.json")
WORDS = ("warm bright dark soft hard punchy fat thin airy wet dry smooth harsh deep dirty clean lofi distorted crisp mellow sub heavy light "
         "metallic glassy gritty round tight huge wide dusty analog digital").split()
ALIAS = {"lo": "lofi", "fi": "lofi", "distortion": "distorted", "dist": "distorted", "glass": "glassy", "metal": "metallic", "grit": "gritty",
         "air": "airy", "fatty": "fat", "warmth": "warm", "brightness": "bright", "darker": "dark", "softer": "soft", "harder": "hard", "punch": "punchy",
         "heavier": "heavy", "smoother": "smooth", "dusty": "dusty", "dust": "dusty", "wider": "wide", "deeper": "deep", "huger": "huge"}

def tokens(text):
    out = set()
    for t in re.findall(r"[a-z]+", text.lower()):
        t = ALIAS.get(t, t); out.add(t)
        if t.endswith("s") and t[:-1] in WORDS: out.add(t[:-1])
    return out

def vec(r):
    if not r.get("bands") or len(r["bands"]) != 32: return None
    sc = [math.log(max(r.get(k) or 1, 1)) for k in ("centroid", "rolloff")] + [r.get(k) or 0 for k in ("flatness", "crest", "rms", "low", "mid", "high")]
    tm = [math.log1p(r.get("attack_ms") or 0), math.log1p(r.get("decay_ms") or 3000), float(r.get("decay_ms") is None),
          1 - (r["corr"] if r.get("corr") is not None else 1), float(r.get("corr") is None), math.log1p(r.get("onsets") or 0), math.log1p(r.get("seconds") or 0)]
    env = r.get("env") or [0] * 40
    env = np.log10(np.maximum(np.array(env, float), 1e-4)); env = env - env.max() if len(env) == 40 else np.zeros(40)
    return np.r_[np.log10(np.maximum(r["bands"], 1e-9)), sc, tm, env]

def load():
    meta = {}
    for f in ("presets/presets.jsonl", "presets/air_presets.jsonl"):
        for l in open(os.path.join(D, f)):
            r = json.loads(l)
            if r.get("engine") == "u-he diva": meta[r["name"].lower().strip()] = r
    X, G, T, S = [], [], [], []
    seen = set()
    for f in ("preset_diva.jsonl", "diva_m1.jsonl", "preset_diva_v2.jsonl", "preset_diva_v2_air.jsonl"):
        for l in open(os.path.join(D, "audio", f)):
            r = json.loads(l); name = re.sub(r"\.h2p$", "", r["label"]).lower().strip()
            if "__" in name or name in seen or name not in meta: continue   # '__' = a knob variant of a preset, not a named preset
            v = vec(r)
            if v is None or (r.get("rms") or 0) < 1e-4: continue
            m = meta[name]; tg = m.get("tags") or {}; parts = m["src"].split("/")
            text = " ".join([m["name"], tg.get("description") or "", tg.get("usage") or "", " ".join(tg.get("type") or []), " ".join(parts[-3:-1])])
            seen.add(name); X.append(v); G.append("diva:" + "/".join(parts[-3:-1])); T.append(tokens(text)); S.append("diva")
    for l in open(os.path.join(D, "audio/air_features.jsonl")):
        r = json.loads(l); v = vec(r)
        if v is None: continue
        parts = r["src"].split("/"); X.append(v); G.append("ableton:" + "/".join(parts[-4:-2])); T.append(tokens(" ".join(parts[-3:]))); S.append("ableton")
    for l in open(os.path.join(D, "audio/features.jsonl")):
        r = json.loads(l); v = vec(r)
        if v is None: continue
        X.append(v); G.append("pack:" + r["pack"].split("/")[0]); T.append(tokens(os.path.basename(r["src"]))); S.append("samples")
    return np.nan_to_num(np.array(X)), np.array(G), T, np.array(S)

def auc(s, y):
    o = np.argsort(s, kind="stable"); r = np.empty(len(s)); r[o] = np.arange(1, len(s) + 1); n1 = y.sum(); n0 = len(y) - n1
    return float((r[y == 1].sum() - n1 * (n1 + 1) / 2) / (n1 * n0)) if n1 and n0 else float("nan")

def fit(Xt, yt, it=300, l2=0.01):
    w = np.zeros(Xt.shape[1] + 1); Xb = np.c_[Xt, np.ones(len(Xt))]; wt = np.where(yt == 1, 0.5 / max(yt.mean(), 1e-9), 0.5 / max(1 - yt.mean(), 1e-9))
    for _ in range(it):
        p = 1 / (1 + np.exp(-np.clip(Xb @ w, -30, 30))); w -= 0.3 * (Xb.T @ (wt * (p - yt)) / len(yt) + l2 * w)
    return w

def evaluate(X, G, T, S, src, folds=5, neg=20000, seed=0):
    keep = np.isin(S, src); X, G = X[keep], G[keep]; T = [t for t, k in zip(T, keep) if k]
    fold = np.array([zlib.crc32(g.encode()) % folds for g in G]); rng = np.random.default_rng(seed); res = {}
    for w in WORDS:
        y = np.array([w in t for t in T], float)
        per = [int(y[fold == f].sum()) for f in range(folds)]
        if y.sum() < 40 or sum(p >= 5 for p in per) < 3: continue
        fa = []
        for f in range(folds):
            te = fold == f; tr = ~te
            if y[te].sum() < 5: continue
            ti = np.flatnonzero(tr); pos, ng = ti[y[ti] == 1], ti[y[ti] == 0]; ng = rng.choice(ng, min(len(ng), neg), replace=False); ti = np.r_[pos, ng]
            mu, sd = X[ti].mean(0), X[ti].std(0) + 1e-9; wv = fit((X[ti] - mu) / sd, y[ti])
            fa.append(auc(np.c_[(X[te] - mu) / sd, np.ones(te.sum())] @ wv, y[te]))
        res[w] = {"auc": round(float(np.mean(fa)), 3), "fold_sd": round(float(np.std(fa)), 3), "positives": int(y.sum()), "folds_scored": len(fa)}
    return res

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--out", default=OUT); a = ap.parse_args()
    X, G, T, S = load()
    print({s: int((S == s).sum()) for s in np.unique(S)}, "rows;", len(set(G)), "groups")
    out = {"bar": "AUC >= 0.75 per word, groups held out", "features": "32 log bands + 8 scalars + attack/decay + width, onsets, length + 40-point envelope", "runs": {}}
    for name, src in (("diva", ["diva"]), ("ableton", ["ableton"]), ("samples", ["samples"]), ("all", ["diva", "ableton", "samples"])):
        r = evaluate(X, G, T, S, src); out["runs"][name] = r
        met = [w for w, v in r.items() if v["auc"] >= 0.75]
        print("%-8s %2d testable, %2d >= 0.75: %s" % (name, len(r), len(met), ", ".join("%s %.3f" % (w, v["auc"]) for w, v in sorted(r.items(), key=lambda kv: -kv[1]["auc"]))), flush=True)
    r = out["runs"]["all"]   # the verdict is read on all sources together, not on the best source per word
    out["testable_words"] = len(r); out["met_words"] = sorted(w for w, v in r.items() if v["auc"] >= 0.75)
    out["verdict"] = "MET" if r and len(out["met_words"]) == len(r) else "MISS"
    print("all sources: testable %d of %d words; >= 0.75: %d %s" % (len(r), len(WORDS), len(out["met_words"]), out["met_words"]))
    json.dump(out, open(a.out, "w"), indent=1)
