#!/usr/bin/env python3
"""Calibrate the per-part spectral gate on real renders, and measure it.
    spectral_gates.py [--spectrum FILE] [--models DIR]
FILE comes from the app: INFINITE_GENSAMPLESPECTRUM=FILE INFINITE_GENSAMPLETAKES=40 Infinite
(one JSON line per take: the clean render before the spectral gate, plus six damaged copies).
Takes with take % 5 == 0 are held out: each part's gate is fitted on the other takes (clean and
damaged), then reports the false-reject rate on held-out clean takes and the catch rate on their
damaged copies. One check per mishap; each threshold sits halfway between the part's clean takes
and that mishap's copies. The checks were chosen by 4-fold cross-validation inside the fitted takes.
Writes spectral_gates.json next to current.json (local and shippable); export_critic_inc.py reads it."""
import argparse, collections, json, math, os, re

PART_NAMES = [
    "full", "synths", "pads", "plucks", "percussion", "drums", "bass", "fx",
    "foley", "keys", "leads", "textures", "kicks", "snares", "hats", "toms",
]
# Fixed before looking at held-out results (chosen by the inner cross-validation).
CLEAN_EDGE = 1       # the clean edge is the 2nd most extreme fitted clean take: one odd take may not open a check
WAY = 0.5            # threshold = clean edge + WAY * (gap to the 10th percentile of the mishap's copies)
OVERLAP = 0.02       # when they overlap, the threshold sits just past the clean edge
OFF = 1e9            # a check that does not apply to the part
AUDIBLE_RMS = 0.003  # quieter takes are under Master's -50 LUFS floor: dropped as silent before this gate


def features(r):
    b = [max(1e-12, x) for x in r["bands"]]
    tot = sum(b)
    L = lambda v: math.log(max(v, 1e-9))
    med = lambda v: sorted(v)[len(v) // 2]
    corr = r["corr"]
    return {
        "silence": r["silence"], "peak": L(r["peak"]), "rms": L(r["rms"]),
        "whistle": L(max(b[12:]) / tot), "high": L(sum(b[22:]) / tot), "bot4": L(sum(b[:4]) / tot),
        # strongest band over the median of its 7 neighbours: a lone tone, not a bright mix
        "peaky": L(max(b[i] / med(b[max(0, i - 3):i + 4]) for i in range(32))),
        "corr": corr if corr == corr else -1.0, "tot": sum(max(0.0, x) for x in r["bands"]), "raw_rms": r["rms"],
    }


# Gate tuple order; sign +1 rejects above the threshold, -1 below. Mishap rule:
#   dropout: silence | brickwall: peak AND rms | whistle: whistle AND (peaky OR high_w)
#   muffled: high_m | thin: bot4 AND high_t | phase: corr
CHECKS = [("silence", "dropout", "silence", 1), ("peak", "brickwall", "peak", 1), ("rms", "brickwall", "rms", 1),
          ("whistle", "whistle", "whistle", 1), ("peaky", "whistle", "peaky", 1), ("high_w", "whistle", "high", 1),
          ("high_m", "muffled", "high", -1), ("bot4", "thin", "bot4", -1), ("high_t", "thin", "high", 1),
          ("corr", "phase", "corr", -1)]


def threshold(clean, damaged, sign):
    c = sorted(sign * v for v in clean)
    edge = c[-1 - min(CLEAN_EDGE, len(c) - 1)]
    d = sorted(sign * v for v in damaged)
    q = d[int(0.1 * (len(d) - 1))] if d else edge
    t = edge + WAY * (q - edge) if q > edge else edge + OVERLAP * abs(edge) + 1e-3
    return round(sign * t, 4)


def fit(clean, damaged):
    """damaged: {kind: [rows]} of the same fitted takes. Returns the gate tuple in CHECKS order."""
    fc = [features(r) for r in clean]
    g = []
    for _, kind, f, sign in CHECKS:
        if not applies(kind, clean):
            g.append(sign * OFF)
            continue
        fd = [features(r)[f] for r in damaged.get(kind, [])]
        g.append(threshold([x[f] for x in fc], fd, sign))
    return tuple(g)


def verify(g, x):
    """Same checks, same order as SampleCriticData::VerifyAudioSpectrum. Returns the failed checks."""
    if x["tot"] <= 1e-12 or x["raw_rms"] <= 1e-7:
        return ["silent"]
    t = dict(zip((c[0] for c in CHECKS), g))
    fails = []
    if x["silence"] > t["silence"]: fails.append("dropout_silence")
    if x["peak"] > t["peak"] and x["rms"] > t["rms"]: fails.append("clipped_flat")
    if x["whistle"] > t["whistle"] and (x["peaky"] > t["peaky"] or x["high"] > t["high_w"]): fails.append("resonant_whistle")
    if x["high"] < t["high_m"]: fails.append("dull_highs")
    if x["bot4"] < t["bot4"] and x["high"] > t["high_t"]: fails.append("thin_low_end")
    if x["corr"] < t["corr"]: fails.append("phase_cancellation")  # NaN arrives as -1 and fails, as in C++
    return fails


def applies(kind, part_clean):
    """Which damage counts as a mishap for a part, from the part's fitted clean takes (not from results):
       muffling only where the part has highs to lose, thinning only where it has lows to lose."""
    raw = lambda r: [max(0.0, x) for x in r["bands"]]
    share = lambda r, a, b: sum(raw(r)[a:b]) / (sum(raw(r)) or 1e-30)
    med = lambda v: sorted(v)[len(v) // 2]
    if kind == "muffled":
        return med([share(r, 22, 32) for r in part_clean]) > 0.05
    if kind == "thin":
        return med([share(r, 0, 11) for r in part_clean]) > 0.15
    return True


def load(path):
    """An all-silent copy has no defined correlation; the app writes it as nan. Takes whose clean
       render Master would drop as silent are left out with all their copies."""
    rows = [json.loads(re.sub(r"\bnan\b", "NaN", ln)) for ln in open(os.path.expanduser(path))]
    quiet = {(r["part"], r["take"]) for r in rows if r["kind"] == "clean" and r["rms"] < AUDIBLE_RMS}
    return [r for r in rows if (r["part"], r["take"]) not in quiet]


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
        damaged = {k: [r for r in rs if r["take"] % 5 != 0] for k, rs in by[part].items() if k != "clean"}
        g = fit(train, damaged)
        gates[part] = g
        rej = sum(bool(verify(g, features(r))) for r in test)
        fr += rej
        ft += len(test)
        caught = tot = 0
        for kind, rs in by[part].items():
            if kind == "clean" or not applies(kind, train):
                continue
            rs = [r for r in rs if r["take"] % 5 == 0]  # the fit saw the other takes' copies
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
    out = {"gates": gates, "checks": [c[0] for c in CHECKS], "frr": round(rep["frr"], 3), "catch": round(rep["catch"], 3),
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
