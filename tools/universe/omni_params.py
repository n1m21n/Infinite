#!/usr/bin/env python3
"""Recipe side of the Omnisphere join: every knob of every patch, read straight from the patch XML (no rendering, no plug-in).

    omni_params.py --lib "<.../Omnisphere/Settings Library/Patches>" [--out ~/Infinite/training-data/audio/omni_params.jsonl.gz] [--limit N]

One row per patch, keyed by the same `src` as omni_render.py rows. `params` is a flat {path: value}: path = ELEMENT[index].attr down
the tree (index = nth sibling of that tag), values decoded - 8-hex-digit strings are IEEE floats (3f800000 -> 1.0), plain numbers are
numbers, the rest stay strings. Attributes that are the patch's name/notes/tags are skipped (they are in `tags`). Prune to the knobs
that actually vary across patches at fit time. Statistics only."""
import argparse, collections, gzip, json, os, re, struct, sys
import xml.etree.ElementTree as ET
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from omni_render import patches, tags  # noqa: E402

HEX = re.compile(r"^[0-9a-f]{8}$")
SKIP = {"name", "ATTRIB_VALUE_DATA", "library", "origPatchName", "origLibName", "tunpath", "Notes", "notes"}


def val(v):
    if HEX.match(v):
        f = struct.unpack(">f", bytes.fromhex(v))[0]
        if f == f and abs(f) < 1e12:
            return round(f, 5)
    try:
        return int(v)
    except ValueError:
        try:
            return float(v)
        except ValueError:
            return v


def flatten(el, path, out):
    seen = collections.Counter()
    for k, v in el.attrib.items():
        if k not in SKIP:
            out["%s.%s" % (path, k)] = val(v)
    for ch in el:
        i = seen[ch.tag]
        seen[ch.tag] += 1
        flatten(ch, "%s/%s[%d]" % (path, ch.tag, i) if i or sum(1 for c in el if c.tag == ch.tag) > 1 else "%s/%s" % (path, ch.tag), out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lib", required=True)
    ap.add_argument("--out", default="~/Infinite/training-data/audio/omni_params.jsonl.gz")
    ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()
    n = bad = 0
    with gzip.open(os.path.expanduser(a.out), "wt") as fo:
        for src, patch in patches(a.lib):
            if a.limit and n >= a.limit:
                break
            try:
                root = ET.fromstring(patch.decode("utf8", "replace").replace("\x00", ""))
            except ET.ParseError:
                bad += 1
                continue
            out = {}
            flatten(root, root.tag, out)
            fo.write(json.dumps({"src": "state:" + src, "tags": tags(patch), "n": len(out), "params": out}, separators=(",", ":")) + "\n")
            n += 1
    print("patches %d, unparsable %d -> %s" % (n, bad, a.out))


if __name__ == "__main__":
    main()
