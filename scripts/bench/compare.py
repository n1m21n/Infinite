#!/usr/bin/env python3
"""Compare two BENCH_JSON .jsonl files (docs/plans/perf/benchmark-suite.md).

Usage: scripts/bench/compare.py <baseline.jsonl> <new.jsonl>

Prints a table keyed by (bench, variant) and flags:
  - any frame_ms/audio p99 regression over 10%
  - any xrun increase
  - any output_hash change (the quality guard - Full quality must render
    bit-identical, or the run has to explain why not, e.g. float-summation
    order per benchmark-suite.md §3)

Exits non-zero if any regression/hash-change was flagged, so it can gate CI.
"""
import json
import sys
from collections import OrderedDict

REGRESSION_THRESHOLD = 0.10


def load(path):
    rows = OrderedDict()
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            j = json.loads(line)
            key = (j.get("bench"), j.get("variant"))
            rows[key] = j
    return rows


def p99_frame(j):
    return (j.get("frame_ms") or {}).get("p99")


def p99_audio(j):
    return (j.get("audio") or {}).get("cb_load_p99")


def xruns(j):
    return (j.get("audio") or {}).get("xruns")


def fmt(v):
    return "n/a" if v is None else f"{v:.3f}"


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    baseline = load(sys.argv[1])
    new = load(sys.argv[2])

    flagged = False
    print(f"{'bench':<32} {'variant':<8} {'frame p99 (base->new)':<26} {'audio p99':<20} {'xruns':<12} {'hash':<8}")
    print("-" * 110)

    all_keys = list(baseline.keys())
    for k in new.keys():
        if k not in baseline:
            all_keys.append(k)

    for key in all_keys:
        b = baseline.get(key)
        n = new.get(key)
        bench, variant = key
        if b is None:
            print(f"{bench:<32} {str(variant):<8} (new benchmark, no baseline)")
            continue
        if n is None:
            print(f"{bench:<32} {str(variant):<8} (missing from new run)")
            continue

        row_flag = ""

        bp99, np99 = p99_frame(b), p99_frame(n)
        frame_str = f"{fmt(bp99)} -> {fmt(np99)}"
        if bp99 is not None and np99 is not None and bp99 > 0 and np99 > bp99 * (1 + REGRESSION_THRESHOLD):
            frame_str += " REGRESSION"
            row_flag = "FLAG"
            flagged = True

        bap99, nap99 = p99_audio(b), p99_audio(n)
        audio_str = f"{fmt(bap99)} -> {fmt(nap99)}"
        if bap99 is not None and nap99 is not None and bap99 > 0 and nap99 > bap99 * (1 + REGRESSION_THRESHOLD):
            audio_str += " REGRESSION"
            row_flag = "FLAG"
            flagged = True

        bx, nx = xruns(b), xruns(n)
        xrun_str = f"{bx}->{nx}" if bx is not None and nx is not None else "n/a"
        if bx is not None and nx is not None and nx > bx:
            xrun_str += " UP"
            row_flag = "FLAG"
            flagged = True

        bh, nh = b.get("output_hash"), n.get("output_hash")
        hash_str = "same"
        if bh and nh and bh != "n/a" and nh != "n/a" and bh != nh:
            hash_str = "CHANGED"
            row_flag = "FLAG"
            flagged = True

        marker = f" [{row_flag}]" if row_flag else ""
        print(f"{bench:<32} {str(variant):<8} {frame_str:<26} {audio_str:<20} {xrun_str:<12} {hash_str:<8}{marker}")

    print()
    if flagged:
        print("REGRESSIONS FOUND - see [FLAG] rows above.")
        return 1
    print("No regressions over the 10% p99 threshold, no xrun increase, no output_hash change.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
