#!/usr/bin/env python3
"""SEO and link audit for website/: metadata per page, internal links, sitemap, llms.txt, robots, orphans.
Usage: python3 tools/website/seo_audit.py   (exit 1 when any error is found)"""
import re, sys, json, urllib.parse
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / "website"
BASE = "https://infinitedevices.org/"

class P(HTMLParser):
    def __init__(s):
        super().__init__(); s.m = {}; s.links = []; s.h1 = 0; s.imgs = []; s.ids = set(); s.title = ""; s._t = False; s.ld = []; s._ld = False; s.canon = None; s.lang = None; s.noindex = False
    def handle_starttag(s, t, a):
        a = dict(a)
        if "id" in a: s.ids.add(a["id"])
        if t == "html": s.lang = a.get("lang")
        if t == "title": s._t = True
        if t == "meta":
            k = a.get("name") or a.get("property")
            if k: s.m[k] = a.get("content", "")
            if k == "robots" and "noindex" in a.get("content", ""): s.noindex = True
        if t == "link" and a.get("rel") == "canonical": s.canon = a.get("href")
        if t == "a" and a.get("href"): s.links.append(a["href"])
        if t == "h1": s.h1 += 1
        if t == "img": s.imgs.append(a)
        if t == "script" and a.get("type") == "application/ld+json": s._ld = True; s.ld.append("")
    def handle_endtag(s, t):
        if t == "title": s._t = False
        if t == "script": s._ld = False
    def handle_data(s, d):
        if s._t: s.title += d
        if s._ld: s.ld[-1] += d

err, warn = [], []
pages = {}
for f in sorted(ROOT.rglob("*.html")):
    rel = f.relative_to(ROOT).as_posix()
    if rel.startswith("assets/") or "/_" in rel: continue
    if re.fullmatch(r"embeds/[^/]+\.html", rel): continue   # raw iframe visuals, not pages
    p = P(); p.feed(f.read_text(errors="ignore")); pages[rel] = p

def url_of(rel):
    return BASE + (rel[:-10] if rel.endswith("index.html") else rel)

inbound = {r: 0 for r in pages}
for rel, p in pages.items():
    e = lambda m: err.append(f"{rel}: {m}")
    w = lambda m: warn.append(f"{rel}: {m}")
    t = p.title.strip()
    if not t: e("no <title>")
    elif not 15 <= len(t) <= 70: w(f"title length {len(t)}")
    d = p.m.get("description", "")
    if not d: e("no meta description")
    elif not 70 <= len(d) <= 165: w(f"description length {len(d)}")
    if not p.canon: e("no canonical")
    elif p.canon.rstrip("/") != url_of(rel).rstrip("/") and not p.noindex: w(f"canonical {p.canon} != {url_of(rel)}")
    for k in ("og:title", "og:description", "og:image", "og:url", "twitter:card"):
        if k not in p.m and not p.noindex: w(f"missing {k}")
    if p.h1 != 1 and not rel.startswith("embeds/"): w(f"{p.h1} h1")
    if not p.lang: e("no lang")
    if "viewport" not in p.m: e("no viewport")
    for i in p.imgs:
        if "alt" not in i: w(f"img without alt: {i.get('src','')[:50]}")
    for j in p.ld:
        try: json.loads(j)
        except Exception as x: e(f"bad JSON-LD: {x}")
    for h in p.links:
        if re.match(r"(mailto:|tel:|javascript:|data:|#$)", h) or h.startswith("#"): continue
        u = urllib.parse.urlparse(h)
        if u.scheme in ("http", "https"):
            if not h.startswith(BASE): continue
            path = u.path[1:]
        elif u.scheme: continue
        else:
            path = urllib.parse.urljoin("/" + rel, u.path)[1:] if u.path else rel
        tgt = ROOT / path if path else ROOT / "index.html"
        if tgt.is_dir(): tgt = tgt / "index.html"
        if not tgt.exists(): e(f"broken link {h}"); continue
        r2 = tgt.relative_to(ROOT).as_posix()
        if r2 in inbound and r2 != rel: inbound[r2] += 1
        if u.fragment and r2 in pages and u.fragment not in pages[r2].ids: w(f"missing anchor {h}")

for r, n in inbound.items():
    if n == 0 and not pages[r].noindex and not r.startswith("embeds/") : warn.append(f"{r}: orphan (no inbound links)")

sm = (ROOT / "sitemap.xml").read_text()
locs = re.findall(r"<loc>([^<]+)</loc>", sm)
for l in locs:
    path = l.replace(BASE, "")
    tgt = ROOT / path if path else ROOT / "index.html"
    if tgt.is_dir(): tgt = tgt / "index.html"
    if not tgt.exists(): err.append(f"sitemap: {l} not found")
ls = set(locs)
for rel, p in pages.items():
    if not p.noindex and url_of(rel) not in ls and url_of(rel).rstrip("/") + "/" not in ls and not rel.startswith("embeds/") : warn.append(f"{rel}: not in sitemap")

llms = (ROOT / "llms.txt").read_text()
for l in set(re.findall(r"\((https://infinitedevices\.org/[^)\s]*)\)", llms)):
    path = l.replace(BASE, "").split("#")[0]
    tgt = ROOT / path if path else ROOT / "index.html"
    if tgt.is_dir(): tgt = tgt / "index.html"
    if not tgt.exists(): err.append(f"llms.txt: {l} not found")
rb = (ROOT / "robots.txt").read_text()
if "Sitemap:" not in rb: err.append("robots.txt: no Sitemap line")
print(f"{len(pages)} pages, {len(locs)} sitemap urls")
for m in warn: print("warn", m)
for m in err: print("ERR ", m)
print(f"{len(err)} errors, {len(warn)} warnings")
sys.exit(1 if err else 0)
