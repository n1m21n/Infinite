#!/usr/bin/env python3
"""Check built PDFs against docs/brand/PDF_GUIDELINES.md: fonts, page size, em dashes. Exit 1 on a failure; large blank areas only warn."""
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


def blank_pages(path, limit=0.45):
    """Warn-only: body pages whose lowest content leaves more than `limit` of the text area empty."""
    import glob
    import tempfile
    try:
        from PIL import Image
    except ImportError:
        return []
    out = []
    with tempfile.TemporaryDirectory() as d:
        subprocess.run(["pdftoppm", "-r", "24", "-gray", "-png", path, d + "/p"], capture_output=True)
        files = sorted(glob.glob(d + "/p*.png"))
        for i, f in enumerate(files, 1):
            im = Image.open(f).convert("L")
            w, h = im.size
            top, bot = int(h * 0.07), int(h * 0.90)  # text area between margins and the folio
            px = im.load()
            last = top
            for y in range(top, bot):
                if any(abs(px[x, y] - px[2, 2]) > 12 for x in range(int(w * .08), int(w * .92))):
                    last = y
            blank = (bot - last) / (bot - top)
            if blank > limit and i < len(files):  # the last page of a document may end short
                out.append(f"p{i} {int(blank * 100)}%")
    return out


def layout(path):
    """Hard layout rules (a failure, not a warning). The 2.0 manual shipped a page whose text was one word per line:
    1. squeezed text: many short one-word lines in one column (text squeezed beside a picture);
    2. (same rule) 14+ one-word lines stacked at one left edge;
    3. big picture: an image taller than 45% or wider than 70% of the page;
    4. duplicate pictures: the same image drawn twice on one page."""
    import collections
    import hashlib
    import tempfile
    from xml.etree import ElementTree as ET
    bad = []
    xml = subprocess.run(["pdftotext", "-bbox-layout", path, "-"], capture_output=True, text=True).stdout
    xml = re.sub(r"<!DOCTYPE[^>]*>", "", xml)
    ns = "{http://www.w3.org/1999/xhtml}"
    try:
        root = ET.fromstring(xml)
    except ET.ParseError:
        return ["layout: cannot parse pdftotext -bbox-layout"]
    narrow = []
    for pn, page in enumerate(root.iter(ns + "page"), 1):
        # text squeezed into a sliver comes out as many lines each under 60 pt wide (a block is split per line pair, so count lines)
        alllines = list(page.iter(ns + "line"))
        thin = [l for l in alllines if float(l.get("xMax")) - float(l.get("xMin")) < 60 and len(list(l.iter(ns + "word"))) == 1
                and "".join(w.text or "" for w in l.iter(ns + "word")).isalpha()]
        if len(thin) < 0.5 * len(alllines):
            continue  # a table's first column is thin too; squeezed text is most of the page
        xs = collections.Counter(round(float(l.get("xMin"))) for l in thin)
        if xs and xs.most_common(1)[0][1] >= 14:
            narrow.append(pn)
    if narrow:
        bad.append("text squeezed to one word per line on p" + ",p".join(map(str, narrow[:8])))
    lst = subprocess.run(["pdfimages", "-list", path], capture_output=True, text=True).stdout.splitlines()[2:]
    big, dup = [], []
    seen = collections.defaultdict(list)
    for ln in lst:
        f = ln.split()
        if len(f) < 15 or f[2] != "image":
            continue
        pg, w, h, xppi, yppi, size = int(f[0]), int(f[3]), int(f[4]), float(f[12]), float(f[13]), f[14]
        if pg > 1 and xppi and (w / xppi * 72 > 612 * 0.70 or h / yppi * 72 > 792 * 0.45) and w < 2400:
            big.append(pg)
        seen[(pg, f[10])].append((w, h))   # the same object id drawn twice on one page
    for (pg, oid), v in seen.items():
        if len(v) > 1 and v[0][0] > 60 and v[0][1] > 60:
            dup.append(pg)
    if big:
        bad.append("oversized picture on p" + ",p".join(map(str, sorted(set(big))[:8])))
    if dup:
        bad.append("duplicate picture on p" + ",p".join(map(str, sorted(set(dup))[:8])))
    return bad


if __name__ == "__main__":
    rc = 0
    for p in sys.argv[1:]:
        b = check(p) + layout(p)
        bl = blank_pages(p)
        if bl:
            print("warn " + p + "  large blank area: " + ", ".join(bl))
        print(("FAIL " if b else "ok   ") + p + ("  " + "; ".join(b) if b else ""))
        rc |= bool(b)
    sys.exit(rc)
