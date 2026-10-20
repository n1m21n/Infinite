#!/usr/bin/env python3
"""Lints blog posts against the brand voice and on-page SEO rules.

What: checks front matter, length, dashes, exclamation marks, hype words, American spellings, Title Case headings,
      links (internal count, no root-relative links), image alt text and the `verify:` guard.
Why:  the brand book bans em dashes, exclamation marks and hype; search engines want a clear title, description and internal links.
      Syndication refuses any post that fails.
Usage: python3 tools/seo/lint_post.py [slug ...]     (no slug = every post, drafts included)
Exit codes: 0 clean (warnings allowed), 1 at least one error.
"""
from __future__ import annotations
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lib import load_posts
from build_blog import page_title

HYPE = ["revolutionary", "world's first", "world-first", "game-changing", "game changer", "game-changer", "cutting-edge", "unleash", "supercharge",
        "blazing", "next-level", "next level", "groundbreaking", "ultimate", "the best ", "killer feature", "mind-blowing", "effortless"]
US = {"color": "colour", "colors": "colours", "visualizer": "visualiser", "optimize": "optimise", "behavior": "behaviour", "customize": "customise",
      "center": "centre", "gray": "grey", "favorite": "favourite"}
CATS = {"product", "creative"}


def prose(body):
    """Body without code fences, inline code and link targets (so node names and URLs are not linted as prose)."""
    t = re.sub(r"```.*?```", "", body, flags=re.S)
    t = re.sub(r"`[^`]*`", "", t)
    t = re.sub(r"\]\([^)]*\)", "]", t)
    return t


def lint(p):
    err, warn, b = [], [], p["body"]
    t = prose(b)
    if len(p.get("title", "")) > 70: warn.append(f"title is {len(p['title'])} chars; aim for 70 or fewer")
    st = page_title(p) if p.get("title") else ""
    if not 30 <= len(st) <= 60: warn.append(f"page title is {len(st)} chars; set seo_title to 30-60 characters")
    d = p.get("description", "")
    if not 70 <= len(d) <= 175: err.append(f"description is {len(d)} chars; use 70 to 175")
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", p.get("date", "")): err.append("date must be YYYY-MM-DD")
    if p["category"] not in CATS: err.append(f"category must be one of {sorted(CATS)}")
    if not 2 <= len(p["tags"]) <= 5: warn.append("use 2 to 5 tags")
    if not p.get("keywords"): warn.append("add `keywords:` (the first one is the primary search phrase)")
    if p["status"] == "published" and p.get("verify"): err.append("`verify:` is set; resolve every claim, then remove it before publishing")
    if p["status"] == "published" and p["words"] < 350: warn.append(f"only {p['words']} words; most ranking posts are 600+")
    if re.search(r"[—–]", p.get("title", "") + d + t): err.append("em or en dash found; use a colon, comma or full stop")
    if re.search(r"!(?!\[)", t): err.append("exclamation mark found")
    for w in HYPE:
        if w in t.lower() or w in (p["title"] + d).lower(): err.append(f"hype word: {w!r}")
    for us, uk in US.items():
        if re.search(r"\b" + us.strip() + r"\b", t, re.I) and us.strip() not in ("center",): warn.append(f"American spelling {us.strip()!r}; prose uses British ({uk.strip()})")
    for m in re.finditer(r"^#{2,4} (.+)$", b, re.M):
        words = [w for w in re.findall(r"[A-Za-z][A-Za-z']+", m.group(1))][1:]
        if len(words) >= 3 and sum(w[0].isupper() for w in words) / len(words) > 0.6: warn.append(f"heading looks Title Case, use sentence case: {m.group(1)!r}")
    if re.search(r"\]\(/(?!/)", b): err.append("root-relative link found; use full https://infinitedevices.org/... URLs so syndicated copies keep working")
    internal = len(re.findall(r"\(https://infinitedevices\.org[^)]*\)", b))
    if internal < 2: warn.append(f"{internal} internal link(s); add at least 2 (library, another post, a manual)")
    for m in re.finditer(r"!\[([^\]]*)\]\(", b):
        if not m.group(1).strip(): err.append("image without alt text")
    if p.get("image") and not p.get("image_alt"): warn.append("cover image has no `image_alt:`")
    if p.get("keywords"):
        kw = p["keywords"].split(",")[0].strip().lower()
        if kw and kw not in (p["title"] + " " + d + " " + " ".join(b.split()[:120])).lower(): warn.append(f"primary keyword {kw!r} not in title, description or first 120 words")
    return err, warn


def main():
    want = sys.argv[1:]
    posts = [p for p in load_posts(include_drafts=True) if not want or p["slug"] in want]
    bad = 0
    for p in posts:
        err, warn = lint(p)
        mark = "FAIL" if err else ("warn" if warn else "ok  ")
        print(f"[{mark}] {p['slug']} ({p['status']}, {p['words']} words)")
        for e in err: print("   error:", e)
        for w in warn: print("   warn: ", w)
        bad += bool(err)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
