#!/usr/bin/env python3
"""Pitch from audio, per instrument family (PLAN-2026-10-30 item 11): YIN vs harmonic sum vs the combined estimate, against
the note in the file name.
    pitch_check.py build [--per 160]     drive files named with ONE note+octave (no loop/bpm/chord/arp), grouped by family from
                                         the path, a fixed sample per family -> eval/pitch_set.json
    pitch_check.py run [--jobs 2]        measure every file -> eval/pitch_check.jsonl (resumes)
    pitch_check.py score                 pitch class right per family and estimator, split A (rule choice) / B (report)
Pitch class only: libraries disagree on which octave is C3, so the octave in a name is not ground truth. Bar: 90% per family."""
import argparse, json, math, os, random, re, sys
import numpy as np
from concurrent.futures import ProcessPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import audio_features as af, detail as det  # noqa: E402

EVAL = os.path.expanduser("~/Infinite/training-data/eval")
SET, OUT = os.path.join(EVAL, "pitch_set.json"), os.path.join(EVAL, "pitch_check.jsonl")
NOTE = re.compile(r"(?<![A-Za-z])([A-G])([#b]?)(-?[0-8])(?![A-Za-z#0-9])"); PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
LOOPY = re.compile(r"bpm|loop|chord|arp", re.I)
FAM = [("piano", r"piano|rhodes|wurli|epiano|e-piano|grand"), ("guitar", r"guitar|gtr"), ("bass", r"bass(?!oon)|808"),
       ("strings", r"violin|viola|cello|string|contra"), ("brass", r"horn|trumpet|trombone|tuba|brass"),
       ("wind", r"flute|clarinet|sax|oboe|bassoon|woodwind|reed"), ("mallet", r"marimba|vibra|xylo|glock|mallet|bell|kalimba"),
       ("organ", r"organ"), ("vocal", r"vox|vocal|choir|voice"), ("synth", r"synth|lead|pad|pluck|saw|square")]


def name_pc(path):
    m = list(NOTE.finditer(os.path.splitext(os.path.basename(path))[0]))
    if len(m) != 1: return None
    return (PC[m[0].group(1)] + (1 if m[0].group(2) == "#" else -1 if m[0].group(2) == "b" else 0)) % 12


def family(path):
    low = path.lower()
    for f, rx in FAM:
        if re.search(rx, low): return f
    return None


def cmd_build(a):
    fam = {}
    for p in open(a.list).read().splitlines():
        b = os.path.basename(p)
        if b.startswith("._") or LOOPY.search(b) or name_pc(p) is None: continue
        f = family(p)
        if f: fam.setdefault(f, []).append(p)
    rng = random.Random(7); pick = {}
    for f, v in sorted(fam.items()):
        v = sorted(v); rng.shuffle(v); pick[f] = v[: a.per]
        print("%-8s %5d files, %d picked" % (f, len(v), len(pick[f])))
    json.dump(pick, open(SET, "w"), indent=0)


def measure(job):
    f, p = job; r = {"src": p, "family": f, "name_pc": name_pc(p)}
    try:
        st = af.decode(p, 8)
        if st is None: return dict(r, error="decode")
        x = st.mean(0); rate = af.FS
        if len(x) < 0.2 * rate or np.abs(x).max() < 1e-5: return dict(r, error="short/silent")
        hold = min(1.5, 0.6 * len(x) / rate)
        tt, f0s = det.yin(x, rate, 0.02, max(hold, 0.25))
        v = np.isfinite(f0s)
        r["yin"] = float(np.median(f0s[v])) if v.sum() >= 5 else None
        fh, sal = det.hsum(x, rate, 0.02, max(hold, 0.25))
        r["hs"] = fh if np.isfinite(fh) else None; r["hs_sal"] = round(sal, 4)
    except Exception as e:
        r["error"] = "%s: %s" % (type(e).__name__, str(e)[:80])
    return r


def cmd_run(a):
    pick = json.load(open(SET)); have = set()
    if os.path.exists(OUT):
        have = {json.loads(l)["src"] for l in open(OUT)}
    jobs = [(f, p) for f, v in pick.items() for p in v if p not in have]
    print("%d to do" % len(jobs), flush=True)
    with open(OUT, "a") as o, ProcessPoolExecutor(a.jobs) as ex:
        for i, r in enumerate(ex.map(measure, jobs, chunksize=4)):
            o.write(json.dumps(r) + "\n")
            if i % 100 == 0: o.flush(); print(i, flush=True)


def pc(f):
    return int(round(12 * math.log2(f / 440.0) + 69)) % 12 if f else None


def combined(r, rule):
    """rule 'yin', 'hs', or ('hs_if', s): harmonic sum where YIN is missing or the two disagree in pitch class and the harmonic
    sum is salient (>= s); YIN otherwise."""
    y, h = r.get("yin"), r.get("hs")
    if rule == "yin": return y
    if rule == "hs": return h
    if y is None: return h
    if h is None: return y
    return h if pc(y) != pc(h) and r.get("hs_sal", 0) >= rule[1] else y


def cmd_score(a):
    rows = [json.loads(l) for l in open(OUT)]
    rows = [r for r in rows if "error" not in r and (r.get("yin") or r.get("hs"))]
    for r in rows: r["split"] = "A" if sum(map(ord, r["src"])) % 2 == 0 else "B"
    rules = ["yin", "hs"] + [("hs_if", s) for s in (0.0, 0.1, 0.15, 0.2, 0.25, 0.3, 0.4)]
    acc = lambda rs, rule: 100 * sum(pc(combined(r, rule)) == r["name_pc"] for r in rs) / max(1, len(rs))
    A = [r for r in rows if r["split"] == "A"]
    fams = sorted({r["family"] for r in rows})
    meanA = {str(rule): np.mean([acc([r for r in A if r["family"] == f], rule) for f in fams]) for rule in rules}
    best = max(rules, key=lambda rule: meanA[str(rule)])
    print("split A, mean over families:", {k: round(v, 1) for k, v in meanA.items()}, "-> rule", best)
    B = [r for r in rows if r["split"] == "B"]; res = {"rule": str(best), "families": {}}
    print("split B (report)   n     yin     hs  combined  (bar 90)")
    for f in fams:
        rs = [r for r in B if r["family"] == f]
        y, h, c = acc(rs, "yin"), acc(rs, "hs"), acc(rs, best)
        res["families"][f] = {"n": len(rs), "yin": round(y, 1), "hs": round(h, 1), "combined": round(c, 1)}
        print("  %-8s       %4d  %5.1f  %5.1f   %5.1f  %s" % (f, len(rs), y, h, c, "PASS" if c >= 90 else "MISS"))
    rs = [r for r in B if r["family"] == "mallet" and not re.search(r"bell|slap", r["src"], re.I)]   # bells are inharmonic: no one pitch
    res["mallet_no_bells"] = {"n": len(rs), "yin": round(acc(rs, "yin"), 1), "hs": round(acc(rs, "hs"), 1), "combined": round(acc(rs, best), 1)}
    print("  mallet without bells %4d  %5.1f  %5.1f   %5.1f" % (len(rs), acc(rs, "yin"), acc(rs, "hs"), acc(rs, best)))
    json.dump(res, open(os.path.join(EVAL, "pitch_check.json"), "w"), indent=1)


def main():
    ap = argparse.ArgumentParser(); sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build"); b.add_argument("--per", type=int, default=160); b.add_argument("--list", required=True)
    r = sp.add_parser("run"); r.add_argument("--jobs", type=int, default=2)
    sp.add_parser("score")
    a = ap.parse_args(); {"build": cmd_build, "run": cmd_run, "score": cmd_score}[a.cmd](a)


if __name__ == "__main__":
    main()
