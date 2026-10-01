#!/usr/bin/env python3
"""Render addendum.html with headless Chrome and append it to Field_Language_Manual.pdf.

    python3 tools/manual/field/build_addendum.py        # base = the shipped v1 PDF in git (a1ccd8f)

The v1 manual's HTML source was never committed, so the base is the v0.3.0 PDF taken from git and
the post-v1 language changes are an addendum. Output is written to the three shipped copies.
"""
import os, subprocess, shutil, tempfile
from pypdf import PdfWriter, PdfReader
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
HERE = os.path.dirname(os.path.abspath(__file__))
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
tmp = tempfile.mkdtemp()
base = os.path.join(tmp, "base.pdf")
open(base, "wb").write(subprocess.check_output(["git", "-C", ROOT, "show", "a1ccd8f:Field_Language_Manual.pdf"]))
add = os.path.join(tmp, "add.pdf")
subprocess.check_call([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", f"--print-to-pdf={add}",
                       "file://" + os.path.join(HERE, "addendum.html")], stderr=subprocess.DEVNULL)
w = PdfWriter()
for f in (base, add):
    for p in PdfReader(f).pages: w.add_page(p)
w.add_metadata({"/Title": "The Field Language Manual"})
out = os.path.join(tmp, "out.pdf"); w.write(out)
for d in ("", "website/assets/", "press_kit/manuals/"):
    shutil.copy(out, os.path.join(ROOT, d, "Field_Language_Manual.pdf"))
print("pages:", len(w.pages))
