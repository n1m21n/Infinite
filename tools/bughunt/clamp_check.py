#!/usr/bin/env python3
"""Before a clamp ships: which saved patches would it change?

A clamp fix for a param range (B-K-031..034) holds a loaded value to a range.
If a real patch already holds a value outside that range, the fix silently
changes how that patch looks or sounds. This scans every tracked .inf for
such values in about a second.

  clamp_check.py "Type.key=LO..HI" ...           the clamps you are about to add
  clamp_check.py --knobs DESCRIBE.json [Type ...]
                                                 every knob range of those types (all types if none)
  clamp_check.py ... --in DIR                    scan DIR instead of the tracked patches
  clamp_check.py selftest

DESCRIBE.json comes from `Infinite --describe --out FILE` (about 16 s).
Exit status is 1 when any value would change.

A knob range is what that knob draws in the probe's default state. It is not
the param's domain. A central clamp from describe ranges was tried and dropped
for that reason: Select draws selectC as 1..32, -1..1 or -3..3 by mode,
Frequency Shifter's shift knob shows ±100 or ±10000 by its range toggle, the
viewport camera writes camDistance past the knob, and reseed writes seed past
it. Any hit here is a reason to look before clamping.
"""
import json
import math
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def knob_ranges(describe, types=None):
    table = {}
    for t in describe["types"]:
        if types and t["type"] not in types:
            continue
        for r in t.get("rows", []):
            if r.get("tag") != "f" or not r.get("ui"):
                continue
            lo, hi = r.get("min"), r.get("max")
            if lo is None or hi is None or not (math.isfinite(lo) and math.isfinite(hi)) or not lo < hi:
                continue
            table[(t["type"], r["key"])] = (float(lo), float(hi))
    return table


SPEC = re.compile(r"^(.+)\.([^.=]+)=(-?[\d.eE+-]+)\.\.(-?[\d.eE+-]+)$")


def parse_spec(s):
    """'Type Name.key=LO..HI' -> ((type, key), (lo, hi))."""
    m = SPEC.match(s)
    if not m:
        raise ValueError("expected Type.key=LO..HI, got %r" % s)
    return (m.group(1), m.group(2)), (float(m.group(3)), float(m.group(4)))


def parse_inf(text):
    """Yields (type, key, value) for every `f` param in a patch."""
    ty = None
    for line in text.splitlines():
        s = line.strip()
        if line.startswith("node "):
            parts = s.split(" ", 3)  # node <index> <category> <type name with spaces>
            ty = parts[3] if len(parts) == 4 else None
        elif not line.startswith(" "):
            ty = None
        elif ty and s.startswith("f "):
            parts = s.split(" ", 2)
            if len(parts) == 3:
                try:
                    yield ty, parts[1], float(parts[2])
                except ValueError:
                    pass


def scan(table, files):
    hits = []
    for path in files:
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for ty, key, v in parse_inf(text):
            r = table.get((ty, key))
            if r and math.isfinite(v) and not (r[0] <= v <= r[1]):
                hits.append((path, ty, key, v, r))
    return hits


def tracked_inf():
    out = subprocess.run(["git", "ls-files", "*.inf"], cwd=ROOT, capture_output=True, text=True).stdout
    return [os.path.join(ROOT, p) for p in out.split()]


def selftest():
    d = {"types": [{"type": "Gain", "rows": [
        {"key": "gainDb", "tag": "f", "ui": True, "min": -60, "max": 12},
        {"key": "hidden", "tag": "f", "ui": False, "min": 0, "max": 1},
        {"key": "free", "tag": "f", "ui": True, "min": 0, "max": 0},
        {"key": "n", "tag": "i", "ui": True, "min": 0, "max": 4}]}]}
    t = knob_ranges(d)
    assert t == {("Gain", "gainDb"): (-60.0, 12.0)}, t
    assert parse_spec("Frequency Shifter.shift=-1e4..10000") == (("Frequency Shifter", "shift"), (-1e4, 1e4))
    inf = ("infinite-patch 1\nnode 0 Audio Gain\n  pos 0 0\n  f gainDb 900\n  f free 5\nend\n"
           "node 1 3D Torus Knot\n  f gainDb 900\nend\nnode 2 Audio Gain\n  f gainDb 3\nend\n")
    got = list(parse_inf(inf))
    assert got == [("Gain", "gainDb", 900.0), ("Gain", "free", 5.0), ("Torus Knot", "gainDb", 900.0),
                   ("Gain", "gainDb", 3.0)], got
    with tempfile.NamedTemporaryFile("w", suffix=".inf", delete=False) as f:
        f.write(inf)
    try:
        h = scan(t, [f.name])
        assert len(h) == 1 and h[0][1:4] == ("Gain", "gainDb", 900.0), h
    finally:
        os.unlink(f.name)
    print("selftest ok")


def main(argv):
    if argv[:1] == ["selftest"]:
        selftest()
        return 0
    if not argv:
        print(__doc__)
        return 2
    files = None
    if "--in" in argv:
        i = argv.index("--in")
        d = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
        files = [os.path.join(dp, f) for dp, _, fs in os.walk(d) for f in fs if f.endswith(".inf")]
    if argv[0] == "--knobs":
        table = knob_ranges(json.load(open(argv[1])), set(argv[2:]) or None)
    else:
        table = dict(parse_spec(s) for s in argv)
    files = tracked_inf() if files is None else files
    hits = scan(table, files)
    for path, ty, key, v, (lo, hi) in hits:
        print("%s: %s.%s = %g outside %g..%g" % (os.path.relpath(path, ROOT), ty, key, v, lo, hi))
    print("%d clamps, %d patches, %d values the clamp would change" % (len(table), len(files), len(hits)))
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
