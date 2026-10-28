#!/usr/bin/env python3
"""Drum feel in the corpus: velocity accents, micro-timing, swing and snare lay-back, overall and per groove style.
    feel_analysis.py [--store FILE] [--models DIR] [--cap N]
Reads the ok drum_groove rows that midi_ingest.py wrote with per-hit velocity ('0'-'9') and grid offset
('a'-'q' = -8..+8 sixteenths of a step, 'i' on grid). A bar is "played" when any hit sits off the grid and
"varied" when its hits use three or more velocities; quantized, flat bars are drum-machine programming, and
they are plausible music too, so the bands below cover both. Styles are the drum_mix clusters (argmax).
Bands come from the training files only (learn.split's hash); the held-out fifth measures the gate's false rejects.
Writes feel.json (per-feature p2/p5/p50/p95/p98 bands, overall and per cluster, plus the gate) next to current.json.
The bands are a guardrail for the critic: they say what is implausible, not what to play."""
import argparse, collections, json, math, os, statistics, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import learn

LANE = ["kick", "snare", "hat", "open_hat", "clap", "tom_lo", "tom_hi", "bell"]
FEATURES = ["vel_sd", "accent", "hat_alt", "backbeat", "swing", "layback", "jitter", "drag"]


def hits(lanes, vel, off):
    """(lane, step, velocity 0..1, offset in steps) per hit; None when the row has no feel data."""
    if not vel or not off:
        return None
    out = []
    for li, l in enumerate(lanes):
        if not l:
            continue
        for s, c in enumerate(l):
            if c != "." and s < len(vel[li]) and s < len(off[li]) and vel[li][s].isdigit():
                out.append((li, s, int(vel[li][s]) / 9.0, (ord(off[li][s]) - ord("i")) / 16.0))
    return out


def mean(xs):
    return sum(xs) / len(xs) if xs else None


def feel_features(lanes, vel, off):
    """Per-bar feel, every feature in steps (timing) or 0..1 velocity units; None where the bar cannot show it.
       vel_sd   spread of all hit velocities
       accent   hits on the quarter minus every other hit (metric accent depth)
       hat_alt  closed hat on 8ths minus closed hat on off-16ths (the hat's loud-soft alternation)
       backbeat snare on 2 and 4 minus the snare's other hits (ghost depth)
       swing    hat offset on off-16ths minus on 8ths (+ = late off-16ths)
       layback  snare offset on 2 and 4 minus kick offset on 1 and 3 (+ = snare behind the kick)
       jitter   sd of offsets around each lane's own mean (human looseness)
       drag     mean offset of every hit (whole-bar push - or drag +)"""
    h = hits(lanes, vel, off)
    if not h:
        return None
    f = dict.fromkeys(FEATURES)
    vs = [v for _, _, v, _ in h]
    f["vel_sd"] = statistics.pstdev(vs)
    on4 = [v for _, s, v, _ in h if s % 4 == 0]
    offq = [v for _, s, v, _ in h if s % 4 != 0]
    if on4 and offq:
        f["accent"] = mean(on4) - mean(offq)
    h8 = [v for li, s, v, _ in h if li == 2 and s % 2 == 0]
    h16 = [v for li, s, v, _ in h if li == 2 and s % 2 == 1]
    if len(h8) >= 2 and len(h16) >= 2:
        f["hat_alt"] = mean(h8) - mean(h16)
    bb = [v for li, s, v, _ in h if li == 1 and s in (4, 12)]
    gh = [v for li, s, v, _ in h if li == 1 and s not in (4, 12)]
    if bb and gh:
        f["backbeat"] = mean(bb) - mean(gh)
    o8 = [o for li, s, _, o in h if li in (2, 3) and s % 2 == 0]
    o16 = [o for li, s, _, o in h if li in (2, 3) and s % 2 == 1]
    if len(o8) >= 2 and len(o16) >= 2:
        f["swing"] = mean(o16) - mean(o8)
    sn = [o for li, s, _, o in h if li == 1 and s in (4, 12)]
    kk = [o for li, s, _, o in h if li == 0 and s in (0, 8)]
    if sn and kk:
        f["layback"] = mean(sn) - mean(kk)
    by = collections.defaultdict(list)
    for li, _, _, o in h:
        by[li].append(o)
    dev = [o - mean(os_) for os_ in by.values() if len(os_) >= 2 for o in os_]
    if len(dev) >= 4:
        f["jitter"] = statistics.pstdev(dev)
    f["drag"] = mean([o for _, _, _, o in h])
    return f


MARGIN = 0.25  # gate = the training bars' p1..p99 band, widened by a quarter of its width each side; fixed before testing
MARGINS = {"jitter": 0.0}  # 2026-10-28: with the quarter margin the live limit was 0.229 and 5% of random-offset bars passed; plain p99 (0.18) is the limit
# Gated: what a listener hears. drag (a whole-bar offset) is inaudible against itself, and layback's band is
# squeezed to the grid by quantized files, so both are reported but never gated.
GATED = ["vel_sd", "accent", "hat_alt", "backbeat", "swing", "jitter"]


def verify(gates, f):
    """Feel features outside the plausible band; gates = feel.json's "gate" dict. Empty list = plausible."""
    out = []
    for ft, g in gates.items():
        x = f.get(ft)
        if x is None or g is None:
            continue
        m = MARGINS.get(ft, MARGIN) * (g["p99"] - g["p1"])
        if x < g["p1"] - m or x > g["p99"] + m:
            out.append(ft)
    return out


def kind(lanes, vel, off):
    h = hits(lanes, vel, off) or []
    played = any(o != 0.0 for _, _, _, o in h)
    varied = len({v for _, _, v, _ in h}) >= 3
    return ("played" if played else "quantized") + "/" + ("varied" if varied else "flat")


def bands(xs):
    xs = sorted(x for x in xs if x is not None)
    if len(xs) < 30:
        return None
    q = lambda p: round(float(xs[min(len(xs) - 1, int(p * len(xs)))]), 4)
    return {"n": len(xs), "p1": q(0.01), "p2": q(0.02), "p5": q(0.05), "p50": q(0.5), "p95": q(0.95), "p98": q(0.98), "p99": q(0.99), "mean": round(statistics.fmean(xs), 4)}


def main():
    import numpy as np  # only the cluster table needs it; the critic imports this file under python -I
    ap = argparse.ArgumentParser()
    ap.add_argument("--store", default="~/Infinite/training-data/store/inbox.jsonl")
    ap.add_argument("--models", default="~/Infinite/training-data/models")
    ap.add_argument("--cap", type=int, default=0, help="stop after N feel bars (0 = all)")
    a = ap.parse_args()
    mix = json.load(open(os.path.join(os.path.expanduser(a.models), "shippable", "drum_mix.json")))
    th = np.clip(np.array(mix["theta"], dtype=np.float64), 1e-4, 1 - 1e-4)
    lt, l1, lpi = np.log(th), np.log1p(-th), np.log(np.array(mix["pi"]))
    named = mix.get("names") or {}
    names = ["%s_%d" % (named.get(str(i), "cluster"), i) for i in range(len(th))]

    rows, held, kinds = [], [], collections.Counter()
    step_vel = collections.defaultdict(list)  # (lane, step) -> velocities, varied bars only
    step_off = collections.defaultdict(list)  # (lane, step) -> offsets, played bars only
    ms_jitter = []
    for ln in open(os.path.expanduser(a.store)):
        if '"drum_groove"' not in ln or '"vel"' not in ln:
            continue
        r = json.loads(ln)
        if not r.get("ok") or not r.get("vel"):
            continue
        f = feel_features(r["lanes"], r["vel"], r["off"])
        if f is None:
            continue
        if learn.unit_hash(r["src"]) % 5 == 0:  # held out, as learn.split: only scores the gate
            held.append(f)
            continue
        k = kind(r["lanes"], r["vel"], r["off"])
        kinds[k] += 1
        x = np.array(learn.cells(r), dtype=np.float64).reshape(-1)
        c = int(np.argmax(x @ lt.T + (1 - x) @ l1.T + lpi))
        rows.append((k, c, f))
        for li, s, v, o in hits(r["lanes"], r["vel"], r["off"]):
            if k.endswith("varied"):
                step_vel[(li, s)].append(v)
            if k.startswith("played"):
                step_off[(li, s)].append(o)
        if k.startswith("played") and f["jitter"] is not None and r.get("bpm"):
            ms_jitter.append(f["jitter"] * 60000.0 / r["bpm"] / 4.0)
        if a.cap and len(rows) >= a.cap:
            break

    n = len(rows)
    print("feel bars: %d" % n)
    for k, v in kinds.most_common():
        print("  %-18s %6d  %4.1f%%" % (k, v, 100.0 * v / max(1, n)))

    out = {"n": n, "kinds": dict(kinds), "all": {}, "played": {}, "clusters": {}}
    print("\n%-9s %-28s %-28s" % ("feature", "all bars p5 / p50 / p95", "played bars p5 / p50 / p95"))
    for ft in FEATURES:
        b_all = bands([f[ft] for _, _, f in rows])
        b_pl = bands([f[ft] for k, _, f in rows if k.startswith("played")])
        out["all"][ft], out["played"][ft] = b_all, b_pl
        fmt = lambda b: ("%+.3f / %+.3f / %+.3f  n=%d" % (b["p5"], b["p50"], b["p95"], b["n"])) if b else "-"
        print("%-9s %-28s %-28s" % (ft, fmt(b_all), fmt(b_pl)))
    if ms_jitter:
        js = sorted(ms_jitter)
        print("\njitter in ms (played bars with a tempo): p50 %.1f  p95 %.1f" % (js[len(js) // 2], js[int(0.95 * len(js))]))
        out["jitter_ms"] = {"p50": round(js[len(js) // 2], 2), "p95": round(js[int(0.95 * len(js))], 2)}

    print("\nvelocity by step (varied bars), 0..1:")
    for li in (0, 1, 2):
        print("  %-6s " % LANE[li] + " ".join("%3d" % round(100 * mean(step_vel[(li, s)])) if len(step_vel[(li, s)]) >= 30 else "  ." for s in range(16)))
    print("offset by step (played bars), hundredths of a step:")
    for li in (0, 1, 2):
        print("  %-6s " % LANE[li] + " ".join("%+3d" % round(100 * mean(step_off[(li, s)])) if len(step_off[(li, s)]) >= 30 else "  ." for s in range(16)))
    out["step_vel"] = {LANE[li]: [round(mean(step_vel[(li, s)]), 3) if step_vel[(li, s)] else None for s in range(16)] for li in range(8)}
    out["step_off"] = {LANE[li]: [round(mean(step_off[(li, s)]), 4) if step_off[(li, s)] else None for s in range(16)] for li in range(8)}

    by_c = collections.defaultdict(list)
    for k, c, f in rows:
        by_c[c].append((k, f))
    print("\nstyle groove (drum_mix clusters with >= 300 feel bars):")
    print("  %-22s %6s %7s %7s %7s %7s %7s %7s" % ("cluster", "bars", "played", "accent", "hat_alt", "swing", "layback", "jitter"))
    for c, xs in sorted(by_c.items(), key=lambda kv: -len(kv[1])):
        if len(xs) < 300:
            continue
        pl = [f for k, f in xs if k.startswith("played")]
        med = lambda ft, src: (lambda v: sorted(v)[len(v) // 2] if v else float("nan"))([f[ft] for f in src if f[ft] is not None])
        out["clusters"][names[c]] = {"bars": len(xs), "played_share": round(len(pl) / len(xs), 3),
                                     **{ft: bands([f[ft] for f in (pl if ft in ("swing", "layback", "jitter", "drag") else [f for _, f in xs])]) for ft in FEATURES}}
        print("  %-22s %6d %6.0f%% %+7.3f %+7.3f %+7.3f %+7.3f %7.3f" % (
            names[c][:22], len(xs), 100.0 * len(pl) / len(xs), med("accent", [f for _, f in xs]), med("hat_alt", [f for _, f in xs]),
            med("swing", pl), med("layback", pl), med("jitter", pl)))

    out["gate"] = {ft: out["all"][ft] for ft in GATED}
    out["margin"] = MARGIN
    rej = collections.Counter(ft for f in held for ft in verify(out["gate"], f))
    n_rej = sum(bool(verify(out["gate"], f)) for f in held)
    out["held_out"] = {"n": len(held), "frr": round(100.0 * n_rej / max(1, len(held)), 3), "by_feature": dict(rej)}
    print("\nfeel gate on %d held-out real bars: %.2f%% rejected  (%s)" % (
        len(held), out["held_out"]["frr"], ", ".join("%s %d" % kv for kv in rej.most_common())))

    for scope in ("local", "shippable"):
        d = os.path.join(os.path.expanduser(a.models), scope)
        os.makedirs(d, exist_ok=True)
        json.dump(out, open(os.path.join(d, "feel.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
