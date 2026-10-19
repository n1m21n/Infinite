#!/usr/bin/env python3
"""Builds the website blog from tools/seo/posts/*.md.

What: writes website/blog/index.html, website/blog/<slug>.html, website/blog/feed.xml and website/blog/posts.json,
      and (idempotently) adds a "Blog" link to the top bar and footer of index.html, library.html and extensions.html.
      The header and footer on blog pages are copied from index.html at build time, so the blog never drifts from the site.
Why:  one source (the Markdown posts) feeds the site, the RSS feed, the sitemap and llms.txt (via gen_seo.py) and every syndication target.
Usage: python3 tools/seo/build_blog.py            (then python3 tools/website/gen_seo.py for sitemap and llms.txt)
       python3 tools/seo/build_blog.py --drafts   (also render drafts, for local preview)
Exit codes: 0 written, 1 a post is missing required front matter.
"""
from __future__ import annotations
import argparse, html, json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lib import BASE, BLOG, WEB, load_posts, render_md

e = html.escape
CATS = {"product": "Product", "creative": "Creative"}
ICON_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "art", "icons", "src")
CHIP_ICON = {"all": "grid-dots", "product": "cube", "creative": "pencil"}


def chip_icon(key):
    """Brand glyph from art/icons/src, inlined so the chip needs no request."""
    svg = open(os.path.join(ICON_DIR, CHIP_ICON[key] + "-20.svg"), encoding="utf-8").read().strip()
    return svg.replace("<svg ", '<svg class="chip-ic" aria-hidden="true" ', 1).replace("\n", "")

REQUIRED = ("title", "description", "date")


def read(name):
    return open(os.path.join(WEB, name), encoding="utf-8").read()


def rebase(fragment):
    """Make a root-relative fragment from index.html work one folder down (blog/)."""
    def fix(m):
        attr, val = m.group(1), m.group(2)
        if val.startswith(("http://", "https://", "mailto:", "../", "data:")):
            return m.group(0)
        if val.startswith("#"):
            val = "index.html" + val
        return f'{attr}="../{val}"'
    return re.sub(r'\b(href|src)="([^"]*)"', fix, fragment)


def chrome():
    idx = read("index.html")
    header = re.search(r'<header class="site-header".*?</header>', idx, re.S).group(0)
    footer = re.search(r'<footer class="site-footer">.*?</footer>', idx, re.S).group(0)
    header = rebase(header)
    header = re.sub(r'<a href="../blog/">Blog</a>', '<a href="../blog/" aria-current="page">Blog</a>', header)
    return header, rebase(footer)


def ensure_nav():
    """Add the Blog link to the top bar and footer of the hand-written pages (safe to run twice)."""
    for name in ("index.html", "library.html", "extensions.html"):
        p = os.path.join(WEB, name)
        s = open(p, encoding="utf-8").read()
        if 'href="blog/"' in s:
            continue
        s2 = re.sub(r'(\n\s*)(<a href="extensions\.html"[^>]*>Extensions</a>)', r'\1\2\1<a href="blog/">Blog</a>', s, count=1)
        if s2 == s:
            s2 = re.sub(r'(\n\s*)(<a href="library\.html"[^>]*>Library</a>)', r'\1\2\1<a href="blog/">Blog</a>', s, count=1)
        if name == "index.html":
            s2 = re.sub(r'(\n\s*)(<a href="artworks\.html">Art Gallery</a>\n\s*<a href="#listen">)', r'\1<a href="blog/">Blog</a>\1\2', s2, count=1)
        if s2 != s:
            open(p, "w", encoding="utf-8").write(s2)
            print("nav: added Blog to", name)


def head(title, desc, url, image, extra="", kind="website", depth="../"):
    img = image if image.startswith("http") else BASE + image.lstrip("/")
    return f"""<!DOCTYPE html>
<html lang="en" data-mode="paper">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#FBFAF6">
  <script>window.INFINITE_DEFAULT_MODE = 'paper';</script>
  <script src="{depth}theme.js?v=2"></script>
  <title>{e(title)}</title>
  <meta name="description" content="{e(desc)}">
  <link rel="canonical" href="{url}">
  <meta property="og:title" content="{e(title)}">
  <meta property="og:description" content="{e(desc)}">
  <meta property="og:image" content="{e(img)}">
  <meta property="og:url" content="{url}">
  <meta property="og:type" content="{kind}">
  <meta property="og:site_name" content="Infinite">
  <meta name="twitter:card" content="summary_large_image">
  <meta name="twitter:title" content="{e(title)}">
  <meta name="twitter:description" content="{e(desc)}">
  <meta name="twitter:image" content="{e(img)}">
  <link rel="alternate" type="application/rss+xml" href="{BASE}blog/feed.xml" title="Infinite blog">
  <link rel="alternate" type="text/plain" href="{depth}llms.txt" title="Plain-text summary for language models">
  <link rel="icon" href="{depth}assets/favicon.svg" type="image/svg+xml">
  <link rel="icon" href="{depth}assets/favicon-32.png" sizes="32x32">
  <link rel="apple-touch-icon" href="{depth}assets/apple-touch-icon.png">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Caveat:wght@600;700&family=Geist:wght@400;500;600;700;800&family=Geist+Mono:wght@400;500;600&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="{depth}brand.css?v=3">
  <link rel="stylesheet" href="{depth}style.css?v=64">
  <link rel="stylesheet" href="{depth}glass.css?v=4">
  <link rel="stylesheet" href="{depth}blog.css?v=3">
  <script async src="https://www.googletagmanager.com/gtag/js?id=G-ZC4TYX1WCL"></script>
  <script>
    window.dataLayer = window.dataLayer || [];
    function gtag(){{dataLayer.push(arguments);}}
    gtag('js', new Date());
    gtag('config', 'G-ZC4TYX1WCL');
  </script>
{extra}</head>
"""


def ld(obj):
    return '  <script type="application/ld+json">' + json.dumps(obj, ensure_ascii=False) + "</script>\n"


def fmt_date(d):
    import datetime
    try:
        return datetime.date.fromisoformat(d).strftime("%-d %B %Y")
    except Exception:
        return d


def card(p, i):
    img = p.get("image", "")
    media = (f'<div class="post-media"><img src="../{e(img)}" alt="" loading="lazy" width="1200" height="675"></div>' if img and not img.startswith("http")
             else f'<div class="post-media"><img src="{e(img)}" alt="" loading="lazy" width="1200" height="675"></div>' if img
             else '<div class="post-media post-media-empty" aria-hidden="true"><span class="mono">infinite</span></div>')
    return f"""        <article class="post-card" data-cat="{e(p['category'])}" style="--i:{i}">
          <a class="post-link" href="{p['slug']}.html">
            {media}
            <div class="post-body">
              <p class="post-kicker mono">{e(CATS.get(p['category'], p['category']))} · {e(fmt_date(p['date']))} · {p['minutes']} min</p>
              <h2>{e(p['title'])}</h2>
              <p class="post-desc">{e(p['description'])}</p>
            </div>
          </a>
        </article>"""


def build_index(posts, header, footer):
    items = "\n".join(card(p, i) for i, p in enumerate(posts))
    chips = '<button type="button" class="chip is-on" data-filter="all">' + chip_icon("all") + 'All</button>' + "".join(
        f'<button type="button" class="chip" data-filter="{k}">{chip_icon(k)}{v}</button>' for k, v in CATS.items() if any(p["category"] == k for p in posts))
    blog_ld = {"@context": "https://schema.org", "@type": "Blog", "name": "Infinite blog", "url": BASE + "blog/",
               "description": "Notes on building and using Infinite, the free open-source DAW for visuals, music and code.",
               "publisher": {"@type": "Person", "name": "Naman Soni", "url": "https://github.com/n1m21n"},
               "blogPost": [{"@type": "BlogPosting", "headline": p["title"], "url": p["url"], "datePublished": p["date"]} for p in posts]}
    crumbs = {"@context": "https://schema.org", "@type": "BreadcrumbList", "itemListElement": [
        {"@type": "ListItem", "position": 1, "name": "Infinite", "item": BASE}, {"@type": "ListItem", "position": 2, "name": "Blog", "item": BASE + "blog/"}]}
    s = head("Infinite blog: audio-reactive visuals, node-based music and code",
             "Notes on building and using Infinite, the free open-source DAW for visuals, music and code on one canvas. Tutorials, engineering write-ups and ideas.",
             BASE + "blog/", "assets/og-image.jpg", ld(blog_ld) + ld(crumbs))
    s += f"""<body class="blog-page">
  {header}

  <main>
    <section class="section blog-hero">
      <div class="container">
        <div class="section-head">
          <p class="blog-hand hand">notes</p>
          <h1 class="slab">Blog</h1>
          <p class="section-sub">How Infinite is built, and what you can make with it. Tutorials for musicians, VJs and 3D artists, and engineering write-ups for the curious.</p>
        </div>
        <div class="blog-chips" role="group" aria-label="Filter posts">{chips}</div>
        <div class="post-grid" id="post-grid">
{items}
        </div>
        <p class="blog-empty" id="blog-empty" hidden>No posts in this group yet.</p>
        <p class="blog-feed mono"><a href="feed.xml">RSS feed</a></p>
      </div>
    </section>
  </main>

  {footer}
  <script src="../site.js?v=9" defer></script>
  <script>
    (function () {{
      var chips = document.querySelectorAll('[data-filter]'), cards = document.querySelectorAll('.post-card'), empty = document.getElementById('blog-empty');
      chips.forEach(function (c) {{ c.addEventListener('click', function () {{
        var f = c.getAttribute('data-filter'), n = 0;
        chips.forEach(function (x) {{ x.classList.toggle('is-on', x === c); }});
        cards.forEach(function (k) {{ var show = f === 'all' || k.getAttribute('data-cat') === f; k.hidden = !show; if (show) n++; }});
        empty.hidden = n > 0;
      }}); }});
    }})();
  </script>
</body>
</html>
"""
    open(os.path.join(BLOG, "index.html"), "w", encoding="utf-8").write(s)


def build_post(p, posts, header, footer):
    body_html, heads = render_md(p["body"])
    h2 = [h for h in heads if h[0] == 2]
    toc = ""
    if len(h2) >= 3:
        toc = '<details class="toc" open><summary class="mono">on this page</summary><ol>' + "".join(f'<li><a href="#{i}">{e(t)}</a></li>' for _, i, t in h2) + "</ol></details>"
    img = p.get("image", "")
    hero = (f'<figure class="post-hero"><img src="../{e(img)}" alt="{e(p.get("image_alt", p["title"]))}" width="1200" height="675"></figure>' if img and not img.startswith("http")
            else f'<figure class="post-hero"><img src="{e(img)}" alt="{e(p.get("image_alt", p["title"]))}" width="1200" height="675"></figure>' if img else "")
    related = [r for r in posts if r["slug"] != p["slug"]]
    related.sort(key=lambda r: (-(len(set(r["tags"]) & set(p["tags"]))), r["category"] != p["category"]))
    rel = "".join(f'<a class="rel-card" href="{r["slug"]}.html"><span class="mono">{e(CATS.get(r["category"], r["category"]))}</span><b>{e(r["title"])}</b></a>' for r in related[:3])
    rel_html = f'<section class="post-related" aria-label="More posts"><h2>Keep reading</h2><div class="rel-grid">{rel}</div></section>' if rel else ""
    art = {"@context": "https://schema.org", "@type": "BlogPosting", "headline": p["title"], "description": p["description"],
           "datePublished": p["date"], "dateModified": p.get("updated", p["date"]), "mainEntityOfPage": p["url"], "url": p["url"],
           "image": [(img if img.startswith("http") else BASE + img) if img else BASE + "assets/og-image.jpg"],
           "author": {"@type": "Person", "name": "Naman Soni", "alternateName": "n1m21n", "url": "https://github.com/n1m21n"},
           "publisher": {"@type": "Organization", "name": "Infinite", "url": BASE, "logo": {"@type": "ImageObject", "url": BASE + "assets/icon.png"}},
           "keywords": ", ".join(p["tags"]), "wordCount": p["words"], "inLanguage": "en-GB",
           "isPartOf": {"@type": "Blog", "name": "Infinite blog", "url": BASE + "blog/"}}
    crumbs = {"@context": "https://schema.org", "@type": "BreadcrumbList", "itemListElement": [
        {"@type": "ListItem", "position": 1, "name": "Infinite", "item": BASE}, {"@type": "ListItem", "position": 2, "name": "Blog", "item": BASE + "blog/"},
        {"@type": "ListItem", "position": 3, "name": p["title"], "item": p["url"]}]}
    s = head(f"{p['title']} | Infinite", p["description"], p["url"], img or "assets/og-image.jpg", ld(art) + ld(crumbs), kind="article")
    s += f"""<body class="blog-page blog-post">
  {header}

  <main>
    <article class="post">
      <div class="container post-wrap">
        <nav class="crumbs mono" aria-label="Breadcrumb"><a href="../index.html">Infinite</a> / <a href="./">Blog</a></nav>
        <header class="post-head">
          <p class="post-kicker mono">{e(CATS.get(p['category'], p['category']))} · {e(fmt_date(p['date']))} · {p['minutes']} min read</p>
          <h1>{e(p['title'])}</h1>
          <p class="post-lede">{e(p['description'])}</p>
          <p class="post-by">By <a href="https://github.com/n1m21n" rel="noopener">Naman Soni</a>{(' · updated ' + e(fmt_date(p['updated']))) if p.get('updated') else ''}</p>
        </header>
        {hero}
        {toc}
        <div class="prose">
{body_html}
        </div>
        <aside class="post-cta" aria-label="Try Infinite">
          <p class="hand">your turn</p>
          <h2>Make something with it</h2>
          <p>Infinite is free and open source for macOS, Windows and Linux. Start from a template in the Library, or read how the nodes fit together.</p>
          <p class="cta-row"><a class="btn btn-primary" href="../index.html#download">Download Infinite</a><a class="btn btn-ghost" href="../library.html">Browse the Library</a></p>
        </aside>
        {rel_html}
      </div>
    </article>
  </main>

  {footer}
  <script src="../site.js?v=9" defer></script>
</body>
</html>
"""
    open(os.path.join(BLOG, p["slug"] + ".html"), "w", encoding="utf-8").write(s)


def build_feed(posts):
    from email.utils import format_datetime
    import datetime
    def rfc(d):
        return format_datetime(datetime.datetime.fromisoformat(d).replace(hour=9, tzinfo=datetime.timezone.utc))
    items = "".join(f"""  <item><title>{e(p['title'])}</title><link>{p['url']}</link><guid isPermaLink="true">{p['url']}</guid>
    <pubDate>{rfc(p['date'])}</pubDate><category>{e(p['category'])}</category><description>{e(p['description'])}</description></item>
""" for p in posts)
    last = rfc(posts[0]["date"]) if posts else ""
    open(os.path.join(BLOG, "feed.xml"), "w", encoding="utf-8").write(f"""<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:atom="http://www.w3.org/2005/Atom"><channel>
  <title>Infinite blog</title><link>{BASE}blog/</link><description>Notes on building and using Infinite, the free open-source DAW for visuals, music and code.</description>
  <language>en-gb</language><lastBuildDate>{last}</lastBuildDate><atom:link href="{BASE}blog/feed.xml" rel="self" type="application/rss+xml"/>
{items}</channel></rss>
""")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--drafts", action="store_true", help="also render drafts (local preview only; do not deploy)")
    a = ap.parse_args()
    posts = load_posts(include_drafts=a.drafts)
    for p in posts:
        miss = [k for k in REQUIRED if not p.get(k)]
        if miss:
            print(f"{p['slug']}: missing front matter {miss}"); sys.exit(1)
    os.makedirs(BLOG, exist_ok=True)
    for f in os.listdir(BLOG):
        if f.endswith(".html") or f in ("feed.xml", "posts.json"):
            os.remove(os.path.join(BLOG, f))
    ensure_nav()
    header, footer = chrome()
    build_index(posts, header, footer)
    for p in posts:
        build_post(p, posts, header, footer)
    build_feed(posts)
    json.dump([{"slug": p["slug"], "title": p["title"], "description": p["description"], "date": p["date"], "updated": p.get("updated", p["date"]),
                "category": p["category"], "url": p["url"]} for p in posts], open(os.path.join(BLOG, "posts.json"), "w"), indent=1)
    # the menu and footer come from one definition; this marks the current page in them
    import subprocess
    subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "website", "sync_chrome.py")], check=True)
    print(f"blog: {len(posts)} post(s) -> website/blog/")


if __name__ == "__main__":
    main()
