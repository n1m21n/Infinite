#!/usr/bin/env python3
"""Library names for website/samples/data.json: "<part>, <scale>" and a bpm (read from the patch header), no genre, no binary names.
usage: python3 tools/web_library_names.py"""
import json, os, re
site = os.path.join(os.path.dirname(__file__), "..", "website", "samples")
path = os.path.join(site, "data.json")
d = json.load(open(path))
for x in d["samples"]:
    if x.get("kind") != "A":
        continue
    part = x.get("part") or "mix"
    key = x.get("key") or ""
    x["name"] = part + (", " + key if key else "")
    if not x.get("bpm") and x.get("patch"):
        m = re.search(r"bpm (\d+(?:\.\d+)?)", open(os.path.join(site, x["patch"])).readline() or "")
        if not m:
            head = "".join(open(os.path.join(site, x["patch"])).readlines()[:5])
            m = re.search(r"bpm (\d+(?:\.\d+)?)", head)
        if m:
            x["bpm"] = round(float(m.group(1)))
json.dump(d, open(path, "w"), separators=(",", ":"))
print(sum(1 for x in d["samples"] if x.get("bpm")), "of", len(d["samples"]), "have a bpm")
