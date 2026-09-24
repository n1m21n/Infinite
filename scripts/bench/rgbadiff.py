#!/usr/bin/env python3
"""Compare two raw RGBA8 dumps written by INFINITE_BENCH_DUMPRGBA.

Explains an output_hash change: prints how many bytes differ and the largest
per-channel difference (in 8-bit steps).

   scripts/bench/rgbadiff.py before.rgba after.rgba
"""
import sys

a = open(sys.argv[1], "rb").read()
b = open(sys.argv[2], "rb").read()
if len(a) != len(b):
    sys.exit(f"size mismatch: {len(a)} vs {len(b)} bytes")
diffs = [abs(x - y) for x, y in zip(a, b) if x != y]
print(f"bytes: {len(a)}  differing: {len(diffs)} ({100.0 * len(diffs) / len(a):.4f}%)  "
      f"max diff: {max(diffs) if diffs else 0}")
