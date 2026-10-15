#!/usr/bin/env python3
"""Render addendum.html with headless Chrome and append it to Field_Language_Manual.pdf.

    python3 tools/manual/field/build_addendum.py        # base = the shipped v1 PDF in git (a1ccd8f)

The v1 manual's HTML source was never committed, so the base is the v0.3.0 PDF taken from git and
the post-v1 language changes are an addendum. Output is written to the three shipped copies.
"""
import os, subprocess, shutil, tempfile
import argparse, sys
from pypdf import PdfWriter, PdfReader
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
HERE = os.path.dirname(os.path.abspath(__file__))
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "docs-shots"))
import common as shots  # screenshot manifest + Brand v2 Paper styling
ap = argparse.ArgumentParser()
ap.add_argument("--out", help="write only this file (no copy to the shipped locations)")
ap.add_argument("--no-base", action="store_true", help="skip the v1 pages (smoke builds)")
args = ap.parse_args()
FIELD_NODES = ["FieldPixel", "Field Graph", "Field Modifier", "Field Primitive", "Field Synth", "Field Notes", "Field Effect"]


def screens_html():
    """'Field nodes on screen': one card per Field node from the screenshot manifest, in Brand v2 Paper styling."""
    m = shots.load_manifest()
    cells = ""
    for name in FIELD_NODES:
        sid = shots.find_by_name(name, "o", m)
        if sid:
            s = m["shots"][sid]
            cells += (f'<div class="cell"><h4>{name}<span>{s["category"]}</span></h4>'
                      f'<img src="file://{os.path.join(shots.ROOT, s["pdf"])}"></div>')
    if not cells:
        return None
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>@page{{size:8.5in 11in;margin:.6in .63in}}'
            f'{shots.brand_css()}h1{{font-size:27px;margin:0 0 6px}}.kick{{font-size:9.5px;letter-spacing:2.6px;color:var(--accent);font-weight:bold;text-transform:uppercase}}'
            f'p{{font-size:12px;color:var(--mute);margin:0 0 16px;line-height:1.6}}</style></head><body>'
            f'<div class="kick">Field language</div><h1>The Field nodes on screen</h1>'
            f'<p>Every node that runs Field code, as it looks in the app with its parameters open. Each one has an editor where the code lives.</p>'
            f'<div class="atlas">{cells}</div></body></html>')


tmp = tempfile.mkdtemp()
base = os.path.join(tmp, "base.pdf")
open(base, "wb").write(subprocess.check_output(["git", "-C", ROOT, "show", "a1ccd8f:Field_Language_Manual.pdf"]))
add = os.path.join(tmp, "add.pdf")
src = open(os.path.join(HERE, "addendum.html"), encoding="utf-8").read()   # brand layout laid over the hand-written styles
src = shots.house_text(src.replace("</head>", f"<style>{shots.brand_css()}</style></head>", 1))
open(os.path.join(tmp, "addendum.html"), "w", encoding="utf-8").write(src)
subprocess.check_call([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", "--allow-file-access-from-files",
                       f"--print-to-pdf={add}", "file://" + os.path.join(tmp, "addendum.html")], stderr=subprocess.DEVNULL)
parts = [add] if args.no_base else [base, add]
sh = screens_html()
if sh:
    open(os.path.join(tmp, "screens.html"), "w").write(shots.house_text(sh))
    scr = os.path.join(tmp, "screens.pdf")
    subprocess.check_call([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", "--allow-file-access-from-files",
                           f"--print-to-pdf={scr}", "file://" + os.path.join(tmp, "screens.html")], stderr=subprocess.DEVNULL)
    parts.append(scr)
w = PdfWriter()
for f in parts:
    for p in PdfReader(f).pages: w.add_page(p)
w.add_metadata({"/Title": "The Field Language Manual"})
out = os.path.join(tmp, "out.pdf"); w.write(out)
if args.out:
    shutil.copy(out, args.out); print("wrote", args.out); sys.exit(0)
for d in ("", "website/assets/", "press_kit/manuals/"):
    shutil.copy(out, os.path.join(ROOT, d, "Field_Language_Manual.pdf"))
print("pages:", len(w.pages))
