#!/usr/bin/env python3
"""The installers' Licence page, from website/terms.html (one source for the terms).

usage: terms_page.py <terms.html> <out.html>         macOS Installer page (links made absolute)
       terms_page.py <terms.html> <out.txt> --text   plain text for Inno Setup and install.sh
"""
import html as htmllib
import re
import sys

src, out = sys.argv[1], sys.argv[2]
text_mode = "--text" in sys.argv[3:]
page = open(src, encoding="utf-8").read()
m = re.search(r"<article[^>]*>(.*?)</article>", page, re.S)
if not m:
    sys.exit("terms_page: no <article> in " + src)
body = m.group(1)
body = re.sub(r'\sclass="[^"]*"', "", body)
body = re.sub(r'href="(?!https?:|mailto:|#)([^"]+)"', r'href="https://infinitedevices.org/\1"', body)

if text_mode:
    t = body
    t = re.sub(r"<h1>(.*?)</h1>", lambda g: g.group(1).upper() + "\n", t, flags=re.S)
    t = re.sub(r"<h2>(.*?)</h2>", r"\n\1\n", t, flags=re.S)
    t = re.sub(r"<li>", "  - ", t)
    t = re.sub(r'<a href="([^"]+)">(.*?)</a>', lambda g: g.group(2) if g.group(1).startswith("mailto:") else f"{g.group(2)} ({g.group(1)})", t, flags=re.S)
    t = re.sub(r"</(p|li)>", "\n", t)
    t = re.sub(r"<[^>]+>", "", t)
    t = htmllib.unescape(t)
    t = re.sub(r"[ \t]+\n", "\n", t)
    t = re.sub(r"^[ \t]+(?!- )", "", t, flags=re.M)
    t = re.sub(r"\n\n(?=  - )", "\n", t)
    t = re.sub(r"\n{3,}", "\n\n", t).strip() + "\n"
    # Inno Setup reads a .txt licence as the system code page unless it carries a BOM.
    open(out, "w", encoding="utf-8-sig", newline="\r\n").write(t)
else:
    open(out, "w", encoding="utf-8").write(
        '<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="style.css"></head><body>'
        + body + "</body></html>\n")
