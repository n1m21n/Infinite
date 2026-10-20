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
CATS = {"product": "Product", "creative": "Creative", "engineering": "Engineering"}
ICON_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "art", "icons", "src")
CHIP_ICON = {"all": "grid-dots", "product": "cube", "creative": "pencil", "engineering": "mod-matrix"}


def brand_icon(name, cls="chip-ic"):
    """Brand glyph from art/icons/src, inlined so controls need no extra network request."""
    svg = open(os.path.join(ICON_DIR, name + "-20.svg"), encoding="utf-8").read().strip()
    return svg.replace("<svg ", f'<svg class="{cls}" aria-hidden="true" ', 1).replace("\n", "")


def chip_icon(key):
    """Brand glyph from art/icons/src, inlined so the chip needs no request."""
    return brand_icon(CHIP_ICON[key], "chip-ic")

REQUIRED = ("title", "description", "date")


def page_title(p):
    """The <title> and social title: `seo_title` when the post sets one, else the title with " | Infinite" when that
    fits in 60 characters (the site audit's limit), else the bare title."""
    if p.get("seo_title"):
        return p["seo_title"]
    t = f"{p['title']} | Infinite"
    return t if len(t) <= 60 else p["title"]


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


def thumb(img, width):
    """A WebP copy of a local blog image, `width` px wide, written to website/assets/thumbs/. Returns the site path,
    or the original when it is remote, already small, or Pillow is missing. Social cards keep using the original."""
    if not img or img.startswith("http"):
        return img
    src = os.path.join(WEB, img.lstrip("/"))
    name = os.path.splitext(os.path.basename(img))[0]
    rel = "assets/thumbs/%s-%d.webp" % (name, width)
    dst = os.path.join(WEB, rel)
    try:
        from PIL import Image
        if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with Image.open(src) as im:
                if im.width <= width:
                    return img
                im.convert("RGB").resize((width, round(im.height * width / im.width)), Image.LANCZOS).save(dst, "WEBP", quality=82, method=6)
        return rel
    except (ImportError, OSError):
        return img


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
  <link rel="stylesheet" href="{depth}brand.css?v=7eb5c13f">
  <link rel="stylesheet" href="{depth}style.css?v=da19e273">
  <link rel="stylesheet" href="{depth}glass.css?v=c25dae44">
  <link rel="stylesheet" href="{depth}blog.css?v=823c9183">
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


def fmt_short_date(d):
    import datetime
    try:
        return datetime.date.fromisoformat(d).strftime("%-d %b %Y")
    except Exception:
        return d


def card(p, i):
    img = p.get("image", "")
    media_img = (f'<img src="../{e(thumb(img, 720))}" alt="" loading="lazy" decoding="async" width="720" height="450">' if img and not img.startswith("http")
                 else f'<img src="{e(img)}" alt="" loading="lazy" width="1200" height="675">' if img
                 else '<div class="post-media-empty" aria-hidden="true"><span>infinite</span></div>')
    cat_label = e(CATS.get(p['category'], p['category'].capitalize()))
    date_short = e(fmt_short_date(p['date']))
    read_label = f"{p['minutes']} min read"
    ic_time = brand_icon("history", "chip-ic")
    open_ic = '<svg class="expand-ic" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M6.5 13.5 13.5 6.5M8 6.5h5.5V12"/></svg>'
    cat_ic = chip_icon(p['category']) if p['category'] in CHIP_ICON else chip_icon("all")
    return f"""        <article class="post-card deck-card" data-cat="{e(p['category'])}" data-title="{e(p['title'])}" data-cat-label="{cat_label}" data-read="{read_label}" style="--i:{i}">
          <a class="post-link" href="{p['slug']}.html">
            <div class="post-media">
              {media_img}
              <span class="post-chip post-chip-cat" title="{cat_label}">{cat_ic}</span>
              <span class="post-chip post-chip-read"><b>{p['minutes']}</b>{ic_time}</span>
              <h2 class="post-title">{e(p['title'])}</h2>
            </div>
          </a>
        </article>"""


def build_index(posts, header, footer):
    items = "\n".join(card(p, i) for i, p in enumerate(posts))
    ic_chev = brand_icon("chevron-down", "select-chev")
    filter_opts = (
        f'<button type="button" class="blog-opt" role="option" aria-selected="true" data-filter="all" data-label="All">{chip_icon("all")}<span>All</span></button>'
        + "".join(
            f'<button type="button" class="blog-opt" role="option" aria-selected="false" data-filter="{k}" data-label="{e(v)}">{chip_icon(k)}<span>{e(v)}</span></button>'
            for k, v in CATS.items() if any(p["category"] == k for p in posts)
        )
    )
    ic_shuffle = brand_icon("loop", "chip-ic")
    ic_fan = brand_icon("library", "chip-ic")
    ic_stack = brand_icon("arrange", "chip-ic")
    ic_grid = brand_icon("grid-dots", "chip-ic")
    ic_prev = brand_icon("chevron-right", "deck-arrow-ic is-prev")
    ic_next = brand_icon("chevron-right", "deck-arrow-ic")
    blog_ld = {"@context": "https://schema.org", "@type": "Blog", "name": "Infinite blog", "url": BASE + "blog/",
               "description": "Notes on building and using Infinite, the free open-source DAW for visuals, music and code.",
               "publisher": {"@type": "Person", "name": "Naman Soni", "url": "https://github.com/n1m21n"},
               "blogPost": [{"@type": "BlogPosting", "headline": p["title"], "url": p["url"], "datePublished": p["date"]} for p in posts]}
    crumbs = {"@context": "https://schema.org", "@type": "BreadcrumbList", "itemListElement": [
        {"@type": "ListItem", "position": 1, "name": "Infinite", "item": BASE}, {"@type": "ListItem", "position": 2, "name": "Blog", "item": BASE + "blog/"}]}
    s = head("Infinite blog: audio-reactive visuals, music and code",
             "Notes on building and using Infinite, the free open-source DAW for visuals, music and code on one canvas. Tutorials, engineering write-ups and ideas.",
             BASE + "blog/", "assets/og-image.jpg", ld(blog_ld) + ld(crumbs))
    s = s.replace('blog.css?v=', 'blog.css?v=', 1)
    s = re.sub(r'(<link rel="stylesheet" href="\.\./blog\.css\?v=[A-Za-z0-9]+">)', r'\1\n  <link rel="stylesheet" href="../deck.css?v=2b385582">', s, count=1)
    s += f"""<body class="blog-page blog-index">
  {header}

  <main>
    <section class="section blog-hero">
      <div class="container">
        <div class="blog-head-bar">
          <h1 class="deck-title">Blog</h1>

          <div class="blog-toolbar">
            <div class="blog-filter" id="blog-filter">
              <button type="button" class="blog-select" id="blog-select" aria-haspopup="listbox" aria-expanded="false" aria-label="Filter by category">
                <span class="blog-select-ic" id="blog-select-ic">{chip_icon("all")}</span>
                <span class="blog-select-val" id="blog-select-val">All</span>
                {ic_chev}
              </button>
              <div class="blog-menu" id="blog-menu" role="listbox" aria-label="Category" hidden>
                {filter_opts}
              </div>
            </div>

            <div class="blog-deck-tools" role="group" aria-label="Deck layout and shuffle">
              <button type="button" class="chip chip-icon-only chip-action" data-deck-shuffle aria-label="Shuffle deck" title="Shuffle">{ic_shuffle}</button>
              <div class="deck-view-switch" role="group" aria-label="Card arrangement">
                <button type="button" class="view-pill is-on" data-deck-view="fan" aria-pressed="true" aria-label="Book view" title="Book">{ic_fan}</button>
                <button type="button" class="view-pill" data-deck-view="stack" aria-pressed="false" aria-label="Stack view" title="Stack">{ic_stack}</button>
                <button type="button" class="view-pill" data-deck-view="grid" aria-pressed="false" aria-label="Grid view" title="Grid">{ic_grid}</button>
              </div>
            </div>
          </div>
        </div>

        <div class="blog-book-stage" id="blog-book-stage" data-deck-mode="fan" aria-roledescription="carousel" aria-label="Infinite blog card book">
          <div class="post-grid" id="post-grid">
{items}
          </div>

          <div class="deck-hud-wrap" id="deck-hud-wrap">
            <div class="deck-hud-pill">
              <button type="button" class="deck-nav-btn" data-deck-prev aria-label="Previous card" title="Previous (←)">
                {ic_prev}
              </button>
              <div class="deck-hud-info" aria-live="polite" aria-atomic="true">
                <img src="../assets/icon-96.png" alt="" class="deck-hud-avatar" width="36" height="36" decoding="async">
                <div class="deck-hud-text">
                  <span class="deck-hud-title" id="deck-hud-title">{e(posts[0]['title']) if posts else 'Infinite Blog'}</span>
                </div>
              </div>
              <div class="deck-hud-right">
                <button type="button" class="deck-icon-btn" data-deck-shuffle aria-label="Reshuffle deck" title="Shuffle (R)">
                  {ic_shuffle}
                </button>
                <button type="button" class="deck-nav-btn" data-deck-next aria-label="Next card" title="Next (→)">
                  {ic_next}
                </button>
              </div>
            </div>
            <div class="deck-progress-row">
              <div class="deck-progress-dots" id="deck-progress-dots" role="group" aria-label="Jump to note"></div>
            </div>
          </div>
        </div>

        <p class="blog-empty" id="blog-empty" hidden>No posts in this group yet.</p>
        <p class="blog-feed"><a href="feed.xml">RSS feed</a></p>
      </div>
    </section>
  </main>

  {footer}
  <script src="../site.js?v=9" defer></script>
  <script src="../deck.js?v=4" defer></script>
  <script src="../blog.js?v=3" defer></script>
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
    hero = (f'<figure class="post-hero"><img src="../{e(thumb(img, 1200))}" alt="{e(p.get("image_alt", p["title"]))}" width="1200" height="675" decoding="async"></figure>' if img and not img.startswith("http")
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
    s = head(page_title(p), p["description"], p["url"], img or "assets/og-image.jpg", ld(art) + ld(crumbs), kind="article")
    s += f"""<body class="blog-page blog-post">
  {header}

  <main>
    <article class="post">
      <div class="container post-wrap">
        <nav class="crumbs mono" aria-label="Breadcrumb"><a href="../index.html">Infinite</a> / <a href="./">Blog</a></nav>
        <header class="post-head">
          <p class="post-kicker mono">{e(CATS.get(p['category'], p['category']))}</p>
          <h1>{e(p['title'])}</h1>
          <p class="post-lede">{e(p['description'])}</p>
          <p class="post-by"><img class="by-av" src="../assets/icon-96.png" alt="" width="40" height="40" decoding="async"><span class="by-text"><a href="https://github.com/n1m21n" rel="noopener">Naman Soni</a><span class="by-meta">{e(fmt_date(p['date']))} · {p['minutes']} min read{(' · updated ' + e(fmt_date(p['updated']))) if p.get('updated') else ''}</span></span></p>
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
