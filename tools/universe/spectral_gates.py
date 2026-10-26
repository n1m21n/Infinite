#!/usr/bin/env python3
"""Calibrate the per-part spectral gate on real renders, and measure it.
    spectral_gates.py [--spectrum FILE] [--models DIR]
FILE comes from the app: INFINITE_GENSAMPLESPECTRUM=FILE INFINITE_GENSAMPLETAKES=40 Infinite
(one JSON line per take: the clean render before the spectral gate, plus six damaged copies).
Takes with take % 5 == 0 are held out: the gate is fitted on the other clean takes only, then
reports the false-reject rate on held-out clean takes and the catch rate on every damaged copy.
Writes spectral_gates.json next to current.json (local and shippable); export_critic_inc.py reads it."""
import argparse, collections, json, math, os, re

PART_NAMES = [
    "full", "synths", "pads", "plucks", "percussion", "drums", "bass", "fx",
    "foley", "keys", "leads", "textures", "kicks", "snares", "hats", "toms",
]
# Margins outside the fitted takes' range, fixed before looking at held-out results.
SHARE_MIN_SCALE = 0.5   # min share = half the lowest fitted share
OPEN_MAX = 0.35         # max share/silence/whistle = fitted max + 35% of the room left to 1
CREST_SCALE = 0.85      # min crest = 85% of the lowest fitted crest
MIN_CORR = -0.40        # no rendered part is out of phase with itself


def features(r):
    b = [max(0.0, x) for x in r["bands"]]
    tot = sum(b) or 1e-30
    return {
        "low": sum(b[:11]) / tot, "mid": sum(b[11:22]) / tot, "high": sum(b[22:]) / tot,
        "whistle": max(b[12:]) / tot, "crest": r["peak"] / max(1e-12, r["rms"]),
        "silence": r["silence"], "corr": r["corr"], "tot": sum(b), "rms": r["rms"],
    }


def fit(clean):
    """Gate tuple (min_low, max_low, min_mid, min_high, max_high, min_crest, max_silence, max_whistle)."""
    f = [features(r) for r in clean]
    lo = lambda k: min(x[k] for x in f)
    hi = lambda k: max(x[k] for x in f)
    opened = lambda v: min(1.0, v + OPEN_MAX * (1.0 - v))
    return (round(SHARE_MIN_SCALE * lo("low"), 4), round(opened(hi("low")), 4), round(SHARE_MIN_SCALE * lo("mid"), 4),
            round(SHARE_MIN_SCALE * lo("high"), 4), round(opened(hi("high")), 4), round(CREST_SCALE * lo("crest"), 4),
            round(opened(hi("silence")), 4), round(opened(hi("whistle")), 4))


def verify(g, x):
    """Same checks, same order as SampleCriticData::VerifyAudioSpectrum. Returns the failed checks."""
    min_low, max_low, min_mid, min_high, max_high, min_crest, max_sil, max_whistle = g
    if x["tot"] <= 1e-12 or x["rms"] <= 1e-7:
        return ["silent"]
    fails = []
    if x["low"] < min_low: fails.append("thin_low_end")
    if x["low"] > max_low: fails.append("muddy_low_end")
    if x["mid"] < min_mid: fails.append("scooped_mid")
    if x["high"] < min_high: fails.append("dull_highs")
    if x["high"] > max_high: fails.append("harsh_highs")
    if x["crest"] < min_crest: fails.append("flat_dynamics")
    if x["silence"] > max_sil: fails.append("dropout_silence")
    if x["whistle"] > max_whistle: fails.append("resonant_whistle")
    if not x["corr"] >= MIN_CORR: fails.append("phase_cancellation")  # NaN fails, as in C++
    return fails


def applies(kind, part_clean):
    """Which damage counts as a mishap for a part, from the part's fitted clean takes (not from results):
       muffling only where the part has highs to lose, thinning only where it has lows to lose."""
    med = lambda k: sorted(features(r)[k] for r in part_clean)[len(part_clean) // 2]
    if kind == "muffled":
        return med("high") > 0.05
    if kind == "thin":
        return med("low") > 0.15
    return True


def load(path):
    """An all-silent copy has no defined correlation; the app writes it as nan."""
    return [json.loads(re.sub(r"\bnan\b", "NaN", ln)) for ln in open(os.path.expanduser(path))]


def evaluate(rows):
    by = collections.defaultdict(lambda: collections.defaultdict(list))
    for r in rows:
        part = "full" if r["part"] == "all" else r["part"]
        by[part][r["kind"]].append(r)
    gates, report = {}, {"parts": {}, "kinds": collections.Counter(), "kinds_tot": collections.Counter()}
    fr = ft = 0
    for part in PART_NAMES:
        clean = by[part]["clean"]
        train = [r for r in clean if r["take"] % 5 != 0]
        test = [r for r in clean if r["take"] % 5 == 0]
        if len(train) < 8:
            continue
        g = fit(train)
        gates[part] = g
        rej = sum(bool(verify(g, features(r))) for r in test)
        fr += rej
        ft += len(test)
        caught = tot = 0
        for kind, rs in by[part].items():
            if kind == "clean" or not applies(kind, train):
                continue
            c = sum(bool(verify(g, features(r))) for r in rs)
            caught += c
            tot += len(rs)
            report["kinds"][kind] += c
            report["kinds_tot"][kind] += len(rs)
        report["parts"][part] = {"fit": len(train), "held_out": len(test), "false_rejects": rej, "caught": caught, "damaged": tot}
    report["frr"] = 100.0 * fr / max(1, ft)
    c = sum(report["kinds"].values())
    t = sum(report["kinds_tot"].values())
    report["catch"] = 100.0 * c / max(1, t)
    report["n_clean"] = sum(len(by[p]["clean"]) for p in by)
    return gates, report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spectrum", default="~/Infinite/training-data/renders/spectrum.jsonl")
    ap.add_argument("--models", default="~/Infinite/training-data/models")
    a = ap.parse_args()
    rows = load(a.spectrum)
    gates, rep = evaluate(rows)
    missing = [p for p in PART_NAMES if p not in gates]
    if missing:
        raise SystemExit("too few takes for: %s" % ", ".join(missing))
    out = {"gates": gates, "min_corr": MIN_CORR, "frr": round(rep["frr"], 3), "catch": round(rep["catch"], 3),
           "n_clean": rep["n_clean"], "parts": rep["parts"]}
    for scope in ("local", "shippable"):
        d = os.path.join(os.path.expanduser(a.models), scope)
        os.makedirs(d, exist_ok=True)
        json.dump(out, open(os.path.join(d, "spectral_gates.json"), "w"), indent=1)
    print("spectral gates from %d clean renders: held-out FRR %.2f%%, damaged-copy catch %.1f%%" % (rep["n_clean"], rep["frr"], rep["catch"]))
    for k in sorted(rep["kinds_tot"]):
        print("  %-10s caught %3d / %3d" % (k, rep["kinds"][k], rep["kinds_tot"][k]))
    for p, v in rep["parts"].items():
        print("  %-10s fit %2d  held-out %2d  false-rejects %d  caught %3d/%3d  gate %s" % (
            p, v["fit"], v["held_out"], v["false_rejects"], v["caught"], v["damaged"], gates[p]))


if __name__ == "__main__":
    main()
