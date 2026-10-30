#!/usr/bin/env python3
"""A/B of String node settings on the acoustic families (PLAN-2026-10-30 items 7 and 8).
    INF_STIM=C3 string_body_test.py [--n 600] [--types box,guitar] [--pluck] [--tag NAME]
    INF_STIM=C3 string_body_test.py --variants "box:bodyType=0;bow_heavy:exciter=1,pressure=0.8,noise=0.3" --tag bow
The same String presets (base designs from own_string.jsonl, every knob as stored) rendered once per body type (or per
variant: name:knob=value,...; a variant's knobs override the stored ones), then
string_families.py's measure per family: median distance of each library sample to the nearest render on the 11 axes, over the
family's own inner distance (lower = the renders land closer to the family). --pluck forces the pluck exciter (the guitar case).
Writes ~/Infinite/training-data/eval/string_body_test[_TAG].json."""
import argparse, json, os, re, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as osw  # noqa: E402
import sound_space as ss  # noqa: E402
import string_families as sfam  # noqa: E402

TYPES = ["box", "guitar", "harp", "violin", "cello"]


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--n", type=int, default=600); ap.add_argument("--types", default="box,guitar")
    ap.add_argument("--pluck", action="store_true"); ap.add_argument("--chunk", type=int, default=50)
    ap.add_argument("--variants", default=""); ap.add_argument("--tag", default=""); a = ap.parse_args()
    if a.variants:
        var = {nm: {k: float(v) for k, v in (kv.split("=") for kv in body.split(",") if kv)}
               for nm, body in (x.split(":", 1) for x in a.variants.split(";") if x)}
    else:
        var = {t: {"bodyType": TYPES.index(t)} for t in a.types.split(",")}
    if a.pluck:
        for v in var.values():
            v["exciter"] = 0
    recs = []
    for ln in open(osw.out_path("String")):
        if '"recipe"' not in ln or '"design": "base"' not in ln and '"design":"base"' not in ln:
            continue
        r = json.loads(ln)
        if r.get("tags", {}).get("design") == "base" and "recipe" in r:
            recs.append(r["recipe"])
    recs = [recs[i] for i in np.random.default_rng(0).choice(len(recs), min(a.n, len(recs)), replace=False)]
    X, N = ss.sample_matrix(None, 200000)
    res = {"presets": len(recs), "pluck": a.pluck, "variants": var, "families": {}}
    S = {}
    for t, over in var.items():
        rows = []
        for k in range(0, len(recs), a.chunk):
            batch = [dict(rc, **over) for rc in recs[k:k + a.chunk]]
            rows += osw.rerender("String", batch)
            print("  %s: %d / %d" % (t, min(k + a.chunk, len(recs)), len(recs)), flush=True)
        V = [ss.vec_from_row(rw[0]) for rw in rows if rw and not rw[0].get("silent")]
        S[t] = np.array([v for v in V if v is not None and np.isfinite(v).all()])
        print("%s: %d usable renders" % (t, len(S[t])), flush=True)
    z = ss.prep(X, np.vstack(list(S.values()))); Xz = z(X)
    print("| family | samples | " + " | ".join("%s ratio" % t for t in S) + " |\n|---|---|" + "---|" * len(S))
    for fam, pat in sfam.FAM.items():
        idx = [i for i, nm in enumerate(N) if re.search(pat, nm.lower())]
        if len(idx) < 20:
            continue
        F = Xz[idx]
        inner = np.median([np.sort(((F[i] - F) ** 2).sum(1) ** .5)[1] for i in range(len(F))])
        ratios = {t: float(np.median(ss.nn_dist(F, z(S[t]))) / inner) for t in S}
        res["families"][fam] = {"samples": len(idx), **{t: round(v, 3) for t, v in ratios.items()}}
        print("| %s | %d | %s |" % (fam, len(idx), " | ".join("%.2f" % ratios[t] for t in S)), flush=True)
    out = os.path.expanduser("~/Infinite/training-data/eval/string_body_test%s.json" % ("_" + (a.tag or "pluck") if a.tag or a.pluck else ""))
    json.dump(res, open(out, "w"), indent=1); print("->", out)


if __name__ == "__main__":
    sys.exit(main())
