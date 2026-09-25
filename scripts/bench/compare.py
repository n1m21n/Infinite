#!/usr/bin/env python3
"""Compare two BENCH_JSON .jsonl files (docs/plans/perf/benchmark-suite.md).

Usage: scripts/bench/compare.py <baseline.jsonl> <new.jsonl>

Rows are keyed by (bench, variant). The variant is normalized first: the
fixture's own ",unfocused=1" / ",unpaced=1" / ",overlap=1" suffixes are
stripped and the row is marked UNTRUSTED (the window lost focus, was not
paced by the display, or projector windows overlapped - its numbers are not a
baseline). Untrusted rows are printed but never gate, and their targets never
count as passing. When a key has both trusted and untrusted lines, only the
trusted ones are used.

A file may hold several lines per key (repeated runs). Each metric is then
the median over those lines and the table shows n (baseline/new). xruns use
the max, so one bad run is never averaged away.

Change gates (baseline -> new, medians):
  - frame_ms.p99 up more than 10%
  - audio.cb_load_p99 up more than 10%
  - audio.xruns up
  - mem.footprint_peak_mb up more than 20% (the memory gate; RSS undercounts
    GPU-backed memory on Apple silicon, so mem.rss_* is printed as info only)
  - mem.gpu_est_breakdown.render_targets_mb (B9) up more than 20%
  - media_io.windows[i].interval_ms.p99 (B8 projectors) up more than 10%
  - B6 frame_ms p50/p95 up more than 10%
  - output_hash changed on a deterministic variant (see below)

Target gates (new run only, benchmark-suite.md section 6 / README):
  - any targets_pass verdict that is false in the new run (and "FLIP" when it
    was true in the baseline). null verdicts are "unproven", not failures.
  - B8 projectors: media_io.windows[i].missed_vsync_frac >= 0.5%
  - B6 canvas: frame_ms p50 > 17.2 ms (60 fps) or p95 > 22.8 ms (45 fps)
  - fbo_allocs_steady > 0 (B2/B4: render targets reallocated in steady state)
  - B7 soak: soak.rss_growth_pct >= 2 or soak.xruns_total > 0 (worst run);
    soak.thermal_fps_drop_pct is printed as info, never gated

output_hash (the quality guard):
  - Deterministic, compared: every variant with anim=0 (B2 static, B4 static).
    Nothing moves, so the Output frame must be bit-identical run to run
    (B2 l-static 29dcc23d9a902c68 on two runs of e04b7a6; B4 l-static
    8bd85fec4eeb5123 on two runs of 1809dbc). Several new lines
    for one deterministic key that disagree among themselves are flagged too.
  - Nondeterministic, printed "n/a (nondeterministic)": every other hashed
    variant - B2/B4 anim=1, B2 gpunodes=1, B3, B9. Their LFOs and animation
    run on wall-clock time, so the frame captured at the sample boundary
    differs between two runs of the same commit.
  - Fixtures that do not hash (B1, B5, B6, B8) report "n/a".

Exits 1 if any trusted row was flagged, 0 otherwise.
"""
import json
import statistics
import sys
from collections import OrderedDict

REGRESSION_THRESHOLD = 0.10
MEM_THRESHOLD = 0.20
UNTRUSTED_MARKS = ("unfocused=1", "unpaced=1", "overlap=1")
MISSED_VSYNC_TARGET = 0.005
CANVAS_P50_MS = 17.2
CANVAS_P95_MS = 22.8
SOAK_RSS_GROWTH_PCT = 2.0


def normalize_variant(variant):
    parts = [p for p in str(variant or "").split(",") if p]
    kept = [p for p in parts if p not in UNTRUSTED_MARKS]
    return ",".join(kept), len(kept) != len(parts)


def load(path):
    """key -> {"trusted": [rows], "untrusted": [rows]}"""
    groups = OrderedDict()
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("BENCH_JSON "):
                line = line[len("BENCH_JSON "):]
            j = json.loads(line)
            variant, untrusted = normalize_variant(j.get("variant"))
            key = (j.get("bench"), variant)
            g = groups.setdefault(key, {"trusted": [], "untrusted": []})
            g["untrusted" if untrusted else "trusted"].append(j)
    return groups


def pick(group):
    """(rows, untrusted) - trusted lines win whenever there are any."""
    if group is None:
        return [], False
    if group["trusted"]:
        return group["trusted"], False
    return group["untrusted"], True


def dig(j, *path):
    for p in path:
        if isinstance(j, dict):
            j = j.get(p)
        elif isinstance(j, list) and isinstance(p, int) and p < len(j):
            j = j[p]
        else:
            return None
    return j


def median(rows, *path):
    vals = [v for v in (dig(r, *path) for r in rows) if isinstance(v, (int, float)) and not isinstance(v, bool)]
    return statistics.median(vals) if vals else None


def maximum(rows, *path):
    vals = [v for v in (dig(r, *path) for r in rows) if isinstance(v, (int, float)) and not isinstance(v, bool)]
    return max(vals) if vals else None


def windows(rows):
    return max((len(dig(r, "media_io", "windows") or []) for r in rows), default=0)


def targets(rows):
    """Combine targets_pass over repeated runs: false if any run failed,
    true if every run that judged it passed, else null."""
    out = OrderedDict()
    for r in rows:
        for k, v in (r.get("targets_pass") or {}).items():
            prev = out.get(k, None)
            if v is False or prev is False:
                out[k] = False
            elif v is True:
                out[k] = True if prev in (None, True) else prev
            else:
                out.setdefault(k, None)
    return out


def deterministic(variant):
    return "anim=0" in variant.split(",")


def fmt(v, digits=2):
    return "n/a" if v is None else f"{v:.{digits}f}"


def pair(b, n, digits=2):
    return f"{fmt(b, digits)}->{fmt(n, digits)}"


def up(b, n, threshold):
    return b is not None and n is not None and b > 0 and n > b * (1 + threshold)


def compare_key(key, bg, ng):
    """Returns (summary columns, reasons, untrusted)."""
    bench, variant = key
    b, _ = pick(bg)
    n, untrusted = pick(ng)
    reasons = []
    info = []

    def chg(label, bv, nv, threshold, digits=2):
        if up(bv, nv, threshold):
            reasons.append(f"REG {label} {pair(bv, nv, digits)} (+{(nv / bv - 1) * 100:.0f}%)")

    bf, nf = median(b, "frame_ms", "p99"), median(n, "frame_ms", "p99")
    chg("frame_ms.p99", bf, nf, REGRESSION_THRESHOLD)
    ba, na = median(b, "audio", "cb_load_p99"), median(n, "audio", "cb_load_p99")
    chg("audio.cb_load_p99", ba, na, REGRESSION_THRESHOLD, 3)
    bx, nx = maximum(b, "audio", "xruns"), maximum(n, "audio", "xruns")
    if bx is not None and nx is not None and nx > bx:
        reasons.append(f"REG xruns {bx}->{nx}")
    bm, nm = median(b, "mem", "footprint_peak_mb"), median(n, "mem", "footprint_peak_mb")
    chg("mem.footprint_peak_mb", bm, nm, MEM_THRESHOLD, 0)
    brt = median(b, "mem", "gpu_est_breakdown", "render_targets_mb")
    nrt = median(n, "mem", "gpu_est_breakdown", "render_targets_mb")
    chg("gpu_est render_targets_mb", brt, nrt, MEM_THRESHOLD, 1)

    for i in range(windows(n)):
        chg(f"window{i} interval_ms.p99",
            median(b, "media_io", "windows", i, "interval_ms", "p99"),
            median(n, "media_io", "windows", i, "interval_ms", "p99"), REGRESSION_THRESHOLD)
        mv = median(n, "media_io", "windows", i, "missed_vsync_frac")
        if mv is not None and mv >= MISSED_VSYNC_TARGET:
            reasons.append(f"TARGET window{i} missed_vsync_frac {mv * 100:.2f}% (>= 0.5%)")

    if bench == "B6_canvas_nav":
        for pct, limit in (("p50", CANVAS_P50_MS), ("p95", CANVAS_P95_MS)):
            bv, nv = median(b, "frame_ms", pct), median(n, "frame_ms", pct)
            chg(f"frame_ms.{pct}", bv, nv, REGRESSION_THRESHOLD)
            if nv is not None and nv > limit:
                reasons.append(f"TARGET canvas frame_ms.{pct} {nv:.2f} ms (> {limit} ms)")

    fbo = maximum(n, "fbo_allocs_steady")
    if fbo is not None and fbo > 0:
        reasons.append(f"TARGET fbo_allocs_steady {fbo:g} (> 0)")

    if bench == "B7_soak":
        growth = maximum(n, "soak", "rss_growth_pct")
        if growth is not None and growth >= SOAK_RSS_GROWTH_PCT:
            reasons.append(f"TARGET soak rss_growth_pct {growth:.2f}% (>= {SOAK_RSS_GROWTH_PCT}%)")
        sx = maximum(n, "soak", "xruns_total")
        if sx is not None and sx > 0:
            reasons.append(f"TARGET soak xruns_total {sx:g} (> 0)")
        drop = median(n, "soak", "thermal_fps_drop_pct")
        info.append(f"soak: rss growth {fmt(median(b, 'soak', 'rss_growth_pct'))}%->{fmt(growth)}%, "
                    f"thermal fps drop {fmt(median(b, 'soak', 'thermal_fps_drop_pct'))}%->{fmt(drop)}% (info)")

    bt, nt = targets(b), targets(n)
    for k, v in nt.items():
        if v is False:
            flip = " FLIP (was true)" if bt.get(k) is True else ""
            reasons.append(f"TARGET {k} false{flip}")
    tp_pass = sum(1 for v in nt.values() if v is True)
    tp_str = f"{tp_pass}/{len(nt)}" if nt else "-"

    bh = {r.get("output_hash") for r in b} - {None, "n/a"}
    nh = {r.get("output_hash") for r in n} - {None, "n/a"}
    if not bh and not nh:
        hash_str = "n/a"
    elif not deterministic(variant):
        hash_str = "n/a (nondeterministic)"
    elif len(nh) > 1:
        hash_str = "UNSTABLE"
        reasons.append(f"HASH new runs disagree: {sorted(nh)}")
    elif bh and nh and bh != nh:
        hash_str = "DIFF"
        reasons.append(f"HASH {'/'.join(sorted(bh))} -> {'/'.join(sorted(nh))}")
    elif bh and nh:
        hash_str = "same"
    else:
        hash_str = "(one side only)"

    rss = pair(median(b, "mem", "rss_mb"), median(n, "mem", "rss_mb"), 0)
    cols = [
        f"{len(b)}/{len(n)}",
        pair(bf, nf),
        pair(ba, na, 3),
        f"{bx}->{nx}" if bx is not None or nx is not None else "n/a",
        pair(bm, nm, 0),
        rss,
        tp_str,
        hash_str,
    ]
    return cols, reasons, untrusted, info


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    baseline = load(sys.argv[1])
    new = load(sys.argv[2])

    keys = list(baseline.keys()) + [k for k in new.keys() if k not in baseline]
    head = ["bench", "variant", "n b/n", "frame p99", "audio p99", "xruns", "footprint pk", "rss (info)", "targets", "hash"]
    widths = [28, 34, 6, 16, 14, 8, 12, 12, 8, 22]
    print(" ".join(h.ljust(w) for h, w in zip(head, widths)))
    print("-" * (sum(widths) + len(widths)))

    flagged = untrusted_rows = 0
    for key in keys:
        bench, variant = key
        name = [str(bench or "").ljust(widths[0]), (variant or "-").ljust(widths[1])]
        if key not in new:
            print(" ".join(name), "(missing from new run)")
            continue
        cols, reasons, untrusted, info = compare_key(key, baseline.get(key), new[key])
        if key not in baseline:
            cols[0] = f"-/{cols[0].split('/')[1]}"
        mark = ""
        if untrusted:
            cols[6] = "untrusted"
            mark = " [UNTRUSTED]"
            untrusted_rows += 1
        elif reasons:
            mark = " [FLAG]"
            flagged += 1
        if key not in baseline:
            mark += " (no baseline)"
        print(" ".join(name + [c.ljust(w) for c, w in zip(cols, widths[2:])]) + mark)
        for r in reasons:
            print(f"      {'(ignored) ' if untrusted else ''}{r}")
        for line in info:
            print(f"      {line}")

    print()
    if untrusted_rows:
        print(f"{untrusted_rows} UNTRUSTED row(s): unfocused/unpaced/overlap - printed, never gated, never passing.")
    if flagged:
        print(f"{flagged} row(s) FLAGGED - see REG/TARGET/HASH lines above.")
        return 1
    print("No regressions, no failing targets, no deterministic output_hash change.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
