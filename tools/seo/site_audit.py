#!/usr/bin/env python3
"""Whole-site SEO and interlinking audit for website/ (static HTML, no browser).

  site_audit.py                 report; exit 1 on errors
  site_audit.py --links PAGE    inlinks and outlinks of one page
  site_audit.py --json FILE     also write every finding

Per indexable page (no noindex): title 30-60 characters, meta description 70-160, canonical pointing at the page's own
URL, og:title / og:description / og:image, lang, exactly one h1, no skipped heading levels, alt on every img, valid
JSON-LD. Site-wide: broken internal links and anchors, orphan pages (no inlinks from another indexable page), pages with
few inlinks, sitemap.xml coverage both ways (every indexable page listed; nothing listed that is missing or noindex).
"""
from __future__ import annotations
import argparse, glob, json, os, re, sys
from collections import defaultdict
from html.parser import HTMLParser
from urllib.parse import urljoin, urlparse, unquote

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WEB = os.environ.get("SITE_DIR") or os.path.join(ROOT, "website")
HOST = open(os.path.join(WEB, "CNAME")).read().strip() if os.path.exists(os.path.join(WEB, "CNAME")) else "infinitedevices.org"
SITE = "https://%s/" % HOST
MIN_INLINKS = 3   # a page reachable only from the menu is thinly linked


class Page(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.meta, self.links, self.ids, self.heads, self.imgs, self.ld = {}, [], set(), [], [], []
        self.title, self.lang, self.canon, self._in, self._buf, self.h1 = "", "", "", None, "", []

    def handle_starttag(self, tag, a):
        a = dict(a)
        if "id" in a:
            self.ids.add(a["id"])
        if tag == "a" and "name" in a:
            self.ids.add(a["name"])
        if tag == "html":
            self.lang = a.get("lang", "")
        elif tag == "meta":
            k = a.get("name") or a.get("property")
            if k:
                self.meta[k.lower()] = a.get("content", "")
        elif tag == "link" and "canonical" in (a.get("rel") or "").split():
            self.canon = a.get("href", "")
        elif tag == "a" and a.get("href"):
            self.links.append(a["href"])
        elif tag == "img":
            self.imgs.append(a)
        elif tag == "title":
            self._in, self._buf = "title", ""
        elif tag == "script" and a.get("type") == "application/ld+json":
            self._in, self._buf = "ld", ""
        elif re.fullmatch(r"h[1-6]", tag):
            self.heads.append(int(tag[1]))
            if tag == "h1":
                self._in, self._buf = "h1", ""

    def handle_endtag(self, tag):
        if self._in == "title" and tag == "title":
            self.title, self._in = self._buf.strip(), None
        elif self._in == "ld" and tag == "script":
            self.ld.append(self._buf); self._in = None
        elif self._in == "h1" and tag == "h1":
            self.h1.append(re.sub(r"\s+", " ", self._buf).strip()); self._in = None

    def handle_data(self, d):
        if self._in:
            self._buf += d


def url_of(rel):
    if rel == "index.html":
        return SITE
    if rel.endswith("/index.html"):
        return SITE + rel[:-10]
    return SITE + rel


def resolve(rel, href):
    """Internal href -> (target rel, fragment) or None for external links."""
    href = href.strip()
    if href.startswith(("mailto:", "tel:", "javascript:", "data:")):
        return None
    u = urlparse(urljoin(url_of(rel), href))
    if u.scheme not in ("http", "https") or u.netloc not in (HOST, "www." + HOST):
        return None
    path = unquote(u.path.lstrip("/"))
    if path == "" or path.endswith("/"):
        path += "index.html"
    elif not os.path.splitext(path)[1]:
        path += ".html" if os.path.exists(os.path.join(WEB, path + ".html")) else "/index.html"
    return path, u.fragment


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--links"); ap.add_argument("--json"); ap.add_argument("--limit", type=int, default=80)
    a = ap.parse_args()
    pages = {}
    for f in sorted(glob.glob(os.path.join(WEB, "**", "*.html"), recursive=True)):
        rel = os.path.relpath(f, WEB).replace(os.sep, "/")
        if rel.startswith(("assets/", "node_modules/", "vendor/")):
            continue
        p = Page()
        p.feed(open(f, encoding="utf-8", errors="ignore").read())
        p.noindex = "noindex" in p.meta.get("robots", "")
        pages[rel] = p
    idx = {r for r, p in pages.items() if not p.noindex}
    F = []
    add = lambda sev, rule, where, detail: F.append({"sev": sev, "rule": rule, "where": where, "detail": detail})

    inl, outl = defaultdict(set), defaultdict(set)
    for rel, p in pages.items():
        for href in p.links:
            t = resolve(rel, href)
            if not t:
                continue
            tgt, frag = t
            if tgt not in pages and not os.path.exists(os.path.join(WEB, tgt)):
                add("error", "broken-link", rel, href)
                continue
            if frag and tgt in pages and frag not in pages[tgt].ids and not frag.startswith((":~:", "t=")):
                add("warn", "broken-anchor", rel, href)
            if tgt != rel and tgt in pages:
                outl[rel].add(tgt)
                if rel in idx:
                    inl[tgt].add(rel)

    for rel in sorted(idx):
        p = pages[rel]
        t = p.title
        if not t:
            add("error", "title-missing", rel, "")
        elif not 30 <= len(t) <= 60:
            add("warn", "title-length", rel, "%d chars: %s" % (len(t), t[:70]))
        d = p.meta.get("description", "")
        if not d:
            add("error", "description-missing", rel, "")
        elif not 70 <= len(d) <= 160:
            add("warn", "description-length", rel, "%d chars" % len(d))
        if not p.canon:
            add("error", "canonical-missing", rel, "")
        elif p.canon.rstrip("/") != url_of(rel).rstrip("/") and p.canon.replace(".html", "").rstrip("/") != url_of(rel).replace(".html", "").rstrip("/"):
            add("warn", "canonical-elsewhere", rel, p.canon)
        for k in ("og:title", "og:description", "og:image"):
            if not p.meta.get(k):
                add("warn", "og-missing", rel, k)
        if not p.lang:
            add("error", "lang-missing", rel, "")
        if len(p.h1) != 1:
            add("error", "h1-count", rel, "%d h1" % len(p.h1))
        prev = 0
        for h in p.heads:
            if prev and h > prev + 1:
                add("warn", "heading-skip", rel, "h%d after h%d" % (h, prev)); break
            prev = h
        missing_alt = [i.get("src", "")[:50] for i in p.imgs if "alt" not in i]
        if missing_alt:
            add("error", "img-alt", rel, "%d img without alt, e.g. %s" % (len(missing_alt), missing_alt[0]))
        for blob in p.ld:
            try:
                json.loads(blob)
            except Exception as ex:
                add("error", "jsonld-invalid", rel, str(ex)[:60])
        if not p.ld and rel != "404.html":
            add("warn", "jsonld-missing", rel, "")
        n = len(inl[rel])
        if rel != "index.html" and n == 0:
            add("error", "orphan", rel, "no inlinks from an indexable page")
        elif rel != "index.html" and n < MIN_INLINKS:
            add("warn", "few-inlinks", rel, "%d inlinks: %s" % (n, ", ".join(sorted(inl[rel])[:3])))

    sm = os.path.join(WEB, "sitemap.xml")
    listed = set()
    if os.path.exists(sm):
        for loc in re.findall(r"<loc>([^<]+)</loc>", open(sm).read()):
            t = resolve("index.html", loc)
            if not t:
                add("error", "sitemap-foreign", "sitemap.xml", loc); continue
            listed.add(t[0])
            if t[0] not in pages and not os.path.exists(os.path.join(WEB, t[0])):
                add("error", "sitemap-missing-page", "sitemap.xml", loc)
            elif t[0] in pages and pages[t[0]].noindex:
                add("error", "sitemap-noindex", "sitemap.xml", loc)
        for rel in sorted(idx - listed):
            if rel not in ("404.html",):
                add("warn", "not-in-sitemap", rel, "")
    else:
        add("error", "sitemap-missing", "sitemap.xml", "")

    if a.links:
        print("inlinks (%d):" % len(inl[a.links]), *sorted(inl[a.links]), sep="\n  ")
        print("outlinks (%d):" % len(outl[a.links]), *sorted(outl[a.links]), sep="\n  ")
        return 0
    by = defaultdict(list)
    for f in F:
        by[(f["sev"], f["rule"])].append(f)
    for (sev, rule), fs in sorted(by.items(), key=lambda kv: (kv[0][0] != "error", kv[0][1])):
        print("%-5s %-22s %d" % (sev.upper(), rule, len(fs)))
        for f in fs[: (a.limit or 10 ** 9) // max(1, len(by)) + 2]:
            print("        %s  %s" % (f["where"], f["detail"]))
    ins = sorted(((len(inl[r]), r) for r in idx if r != "index.html"))
    print("\n%d indexable pages, %d in sitemap; inlinks median %d, fewest: %s" % (
        len(idx), len(listed), ins[len(ins) // 2][0] if ins else 0, ", ".join("%s (%d)" % (r, n) for n, r in ins[:5])))
    if a.json:
        json.dump(F, open(a.json, "w"), indent=1)
    errs = sum(1 for f in F if f["sev"] == "error")
    print("%d error(s), %d warning(s)" % (errs, len(F) - errs))
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
