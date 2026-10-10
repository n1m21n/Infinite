#!/usr/bin/env python3
"""Publishes the app's starter templates to the website library.

What: reads assets/templates/index.txt, the .inf patches and copies the patches, a zip of all of them and
      brand-styled patch diagrams (template_preview.py) into website/assets/templates/ and writes website/assets/templates/index.json, which library.js lists
      under the Templates chip (preview, name, line, Download).
Why:  the 15 templates under File > New from template should be downloadable from the library with the same UI.
Usage: python3 tools/website/gen_library_templates.py        (rerun after tools/templates/gen_templates.py)
Exit codes: 0 written.
"""
import json, os, re, shutil, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from template_preview import render
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "assets", "templates")
OUT = os.path.join(ROOT, "website", "assets", "templates")
SCAFFOLD = {"Audio Analyze", "Shape", "Output", "Comment"}

def chain(path):
    out = []
    for l in open(path):
        m = re.match(r"node \d+ (\S+) (.+)", l.strip())
        if m and m[2] not in SCAFFOLD and (m[1], m[2]) not in out:
            out.append((m[1], m[2]))
    return out[:4]

def main():
    rows = [l.split("|") for l in open(os.path.join(SRC, "index.txt")) if l.strip() and not l.startswith("#")]
    os.makedirs(OUT, exist_ok=True)
    items = []
    with zipfile.ZipFile(os.path.join(OUT, "infinite-templates.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for slug, group, title, line in rows:
            shutil.copy(os.path.join(SRC, slug + ".inf"), OUT)
            z.write(os.path.join(SRC, slug + ".inf"), f"infinite-templates/{slug}.inf")
            render(os.path.join(SRC, slug + ".inf"), os.path.join(OUT, slug + ".webp"))
            items.append({"id": slug, "name": title, "category": "Templates", "kind": "Template", "group": group,
                          "description": line.strip()[:1].upper() + line.strip()[1:] + ".", "price": 0,
                          "preview": f"assets/templates/{slug}.webp", "file": f"assets/templates/{slug}.inf",
                          "nodes": [t for _, t in chain(os.path.join(SRC, slug + ".inf"))]})
    json.dump({"version": 1, "items": items}, open(os.path.join(OUT, "index.json"), "w"), indent=1)
    print(f"{len(rows)} templates written")
main()
