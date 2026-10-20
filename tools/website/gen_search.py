#!/usr/bin/env python3
"""Builds website/search-index.json: the text behind the site search (search.js).

What: reads every page in website/ (home, audience pages, every blog post, every Library entry, the embeds list, the
      docs), plus each page of the PDFs in website/assets, and writes one compact JSON file.
Why:  the site is static, so the search runs in the browser. The file loads the first time someone opens search.
Use:  python3 tools/website/gen_search.py          write website/search-index.json
Entry: [title, url, kind, description, text, anchor-sections]. kind: Page, Blog, Library, Embed, Doc, Code.
"""
import html, json, os, re, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W = os.path.join(ROOT, "website")
OUT = os.path.join(W, "search-index.json")
SKIP_DIRS = {"vendor", "assets", "node_modules"}
SKIP_FILES = {"404.html", "slides.html", "artworks.html", "system.html"}
TEXT_CAP = {"Blog": 4000, "Library": 1800, "Page": 3000, "Embed": 600, "Doc": 1000, "Code": 1200}


def strip(s):
    s = re.sub(r"<(script|style|svg|noscript|template)\b.*?</\1>", " ", s, flags=re.S | re.I)
    s = re.sub(r"<!--.*?-->", " ", s, flags=re.S)
    s = re.sub(r"<[^>]+>", " ", s)
    return re.sub(r"\s+", " ", html.unescape(s)).strip()


def meta(h, name):
    m = re.search(r'<meta[^>]+(?:name|property)="%s"[^>]+content="([^"]*)"' % re.escape(name), h)
    return html.unescape(m.group(1)).strip() if m else ""


def kind_of(rel):
    if rel.startswith("blog/") and rel != "blog/index.html": return "Blog"
    if rel.startswith("library/"): return "Library"
    if rel.startswith("embeds/"): return "Embed"
    return "Page"


def clean_title(t):
    t = re.sub(r"\s*[|:–—-]\s*(Infinite|free .* for Infinite)\s*$", "", t.strip(), flags=re.I)
    return re.sub(r"\s*:\s*free (.+?) for Infinite$", r" (\1)", t, flags=re.I)


def page_entry(rel):
    h = open(os.path.join(W, rel), encoding="utf-8", errors="ignore").read()
    t = re.search(r"<title>(.*?)</title>", h, re.S)
    title = clean_title(html.unescape(t.group(1))) if t else rel
    desc = meta(h, "description") or meta(h, "og:description")
    body = re.search(r"<body.*", h, re.S)
    b = body.group(0) if body else h
    b = re.sub(r"<(header|footer|nav)\b.*?</\1>", " ", b, flags=re.S | re.I)
    code = " ".join(strip(c) for c in re.findall(r"<pre\b.*?</pre>", b, flags=re.S | re.I))
    heads = [strip(x) for x in re.findall(r"<h[1-3]\b[^>]*>(.*?)</h[1-3]>", b, flags=re.S | re.I)]
    sections = []
    for m in re.finditer(r'<h([23])\b[^>]*\bid="([^"]+)"[^>]*>(.*?)</h\1>', b, flags=re.S | re.I):
        st = strip(m.group(3))
        if st and len(st) < 90: sections.append([st, m.group(2)])
    text = strip(b)
    k = kind_of(rel)
    cap = TEXT_CAP[k]
    parts = " . ".join(dict.fromkeys(x for x in heads if x))
    body_txt = (parts + " . " + text)[:cap]
    entries = [[title, rel, k, desc[:240], body_txt, sections[:12]]]
    if code and k != "Embed":
        entries.append([title + ": code", rel, "Code", "", code[:TEXT_CAP["Code"]], []])
    return entries


def embeds():
    f = os.path.join(W, "embeds", "embeds.json")
    out = []
    if not os.path.exists(f): return out
    for it in json.load(open(f, encoding="utf-8")).get("items", []):
        slug = it["slug"]
        src = os.path.join(W, "embeds", slug + ".html")
        code = ""
        if os.path.exists(src):
            code = strip(re.sub(r"^.*?<script\b", "<script", open(src, encoding="utf-8", errors="ignore").read(), count=1, flags=re.S))[:300]
        out.append([it["title"] + " (embed)", "embeds/" + slug + ".html", "Embed", html.unescape(it.get("description", ""))[:240], "audio-reactive widget iframe embed " + slug.replace("-", " "), []])
    return out


def pdfs():
    out, seen = [], set()
    d = os.path.join(W, "assets")
    for f in sorted(os.listdir(d)):
        if not f.lower().endswith(".pdf"): continue
        name = f[:-4].replace("_", " ")
        try:
            txt = subprocess.run(["pdftotext", "-layout", os.path.join(d, f), "-"], capture_output=True, text=True, check=True).stdout
        except Exception as e:
            print("pdftotext failed for", f, e); continue
        for n, pg in enumerate(txt.split("\f"), 1):
            t = re.sub(r"\s+", " ", pg).strip()
            if len(t) < 80: continue
            if t in seen: continue   # a PDF that repeats another's page
            seen.add(t)
            first = t[:70].rsplit(" ", 1)[0]
            out.append([f"{name}, page {n}", f"assets/{f}#page={n}", "Doc", first, t[:TEXT_CAP["Doc"]], []])
    return out


def main():
    items = []
    for dp, dn, fn in os.walk(W):
        dn[:] = [d for d in sorted(dn) if d not in SKIP_DIRS and d != "embeds"]
        for f in sorted(fn):
            if not f.endswith(".html"): continue
            rel = os.path.relpath(os.path.join(dp, f), W).replace(os.sep, "/")
            if f in SKIP_FILES: continue
            items += page_entry(rel)
    embed_index = os.path.join(W, "embeds", "index.html")
    if os.path.exists(embed_index): items += page_entry("embeds/index.html")
    items += embeds()
    items += pdfs()
    data = {"n": len(items), "i": items}
    s = json.dumps(data, ensure_ascii=False, separators=(",", ":"))
    open(OUT, "w", encoding="utf-8").write(s)
    kinds = {}
    for i in items: kinds[i[2]] = kinds.get(i[2], 0) + 1
    print(f"{len(items)} entries, {len(s)/1024:.0f} KB", kinds)


main()
