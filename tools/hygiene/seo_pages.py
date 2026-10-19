#!/usr/bin/env python3
"""Website hygiene: SEO of every public page (tools/seo/check.py audit covers the blog only).
Static, no browser: title and description length, canonical, one h1, Open Graph, alt text, valid JSON-LD,
sitemap coverage, internal links that resolve. Exit 1 on any failure."""
import json, os, re, sys
from common import WEB, pages

sm = open(os.path.join(WEB, "sitemap.xml"), encoding="utf-8").read() if os.path.exists(os.path.join(WEB, "sitemap.xml")) else ""
bad = 0
for rel in pages(True if "--all" in sys.argv else False):
    if rel.startswith("embeds/") and rel != "embeds/index.html":
        continue   # the widget pages are iframe content, not landing pages
    h = open(os.path.join(WEB, rel), encoding="utf-8").read()
    issues = []
    t = re.search(r"<title>(.*?)</title>", h, re.S)
    if not t or not 10 <= len(t.group(1).strip()) <= 80: issues.append("title length %d" % (len(t.group(1)) if t else 0))
    d = re.search(r'<meta name="description" content="([^"]*)"', h)
    if not d or not 70 <= len(d.group(1)) <= 160: issues.append("description %d (want 70-160)" % (len(d.group(1)) if d else 0))
    if not re.search(r'<link rel="canonical" href="https://infinitedevices.org/', h): issues.append("canonical")
    if h.count("<h1") != 1: issues.append("h1 count %d" % h.count("<h1"))
    if not re.search(r'property="og:(title|image)"', h): issues.append("open graph")
    if re.search(r"<img(?![^>]*\balt=)", h): issues.append("img without alt")
    for m in re.findall(r'<script type="application/ld\+json">(.*?)</script>', h, re.S):
        try: json.loads(m)
        except ValueError: issues.append("invalid JSON-LD")
    path = "" if rel == "index.html" else rel.replace("index.html", "")
    if rel not in ("404.html",) and ("/" + path) not in sm and path not in sm and "noindex" not in h: issues.append("not in sitemap")
    for href in re.findall(r'(?:href|src)="([^"#?:]+?)(?:[?#][^"]*)?"', h):
        if href.startswith(("http", "//", "mailto")) or not href: continue
        tgt = os.path.normpath(os.path.join(WEB, os.path.dirname(rel), href))
        if os.path.isdir(tgt): tgt = os.path.join(tgt, "index.html")
        if not os.path.exists(tgt): issues.append("broken link " + href); break
    print(("FAIL " if issues else "ok   ") + rel, "; ".join(issues)); bad += bool(issues)
sys.exit(1 if bad else 0)
