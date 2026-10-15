#!/usr/bin/env python3
"""Check built PDFs against docs/brand/PDF_GUIDELINES.md: fonts, page size, em dashes. Exit 1 on a failure."""
import re
import subprocess
import sys

OK_FONTS = ("Geist", "Caveat")
SIZES = {(612, 792): "Letter", (595, 842): "A4"}


def check(path):
    bad = []
    fonts = subprocess.run(["pdffonts", path], capture_output=True, text=True).stdout.splitlines()[2:]
    for ln in fonts:
        name = ln.split()[0].split("+")[-1] if ln.split() else ""
        if name and not name.startswith(OK_FONTS):
            bad.append(f"font {name}")
    info = subprocess.run(["pdfinfo", path], capture_output=True, text=True).stdout
    m = re.search(r"Page size:\s+([\d.]+) x ([\d.]+)", info)
    if m:
        sz = (round(float(m.group(1))), round(float(m.group(2))))
        if not any(abs(sz[0] - w) <= 2 and abs(sz[1] - h) <= 2 for (w, h) in SIZES):
            bad.append(f"page size {sz}")
    txt = subprocess.run(["pdftotext", path, "-"], capture_output=True, text=True).stdout
    if "—" in txt:
        bad.append(f"{txt.count(chr(0x2014))} em dash(es)")
    return sorted(set(bad))


if __name__ == "__main__":
    rc = 0
    for p in sys.argv[1:]:
        b = check(p)
        print(("FAIL " if b else "ok   ") + p + ("  " + "; ".join(b) if b else ""))
        rc |= bool(b)
    sys.exit(rc)
