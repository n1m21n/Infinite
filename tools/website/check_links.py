#!/usr/bin/env python3
"""Check all internal links, anchors, image paths, and external links across website/.

Usage:
  python3 tools/website/check_links.py                 # internal + cached/budgeted external checks
  python3 tools/website/check_links.py --internal-only # fast build-gate check (no network)
  python3 tools/website/check_links.py --no-cache      # force fresh external checks

Exit code: non-zero if any internal link, #anchor, or <img> src is broken.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import sys
import time
import urllib.error
import urllib.request
from html.parser import HTMLParser
from urllib.parse import unquote, urlparse

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WEB = os.path.join(ROOT, "website")
SKIP_DIRS = {"vendor", "node_modules"}
DEFAULT_CACHE = os.path.join(ROOT, ".proof", "link_cache.json")


class LinkParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids: set[str] = set()
        self.hrefs: list[str] = []
        self.imgs: list[str] = []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if "id" in a and a["id"]:
            self.ids.add(a["id"])
        if tag == "a" and "name" in a and a["name"]:
            self.ids.add(a["name"])
        if tag in ("a", "link") and "href" in a and a["href"]:
            if tag == "link" and a.get("rel") in ("preconnect", "dns-prefetch", "canonical", "alternate"):
                return
            self.hrefs.append(a["href"])
        if tag == "img" and "src" in a and a["src"]:
            self.imgs.append(a["src"])


def collect_pages() -> dict[str, LinkParser]:
    pages: dict[str, LinkParser] = {}
    for dp, dn, fn in os.walk(WEB):
        dn[:] = [d for d in sorted(dn) if d not in SKIP_DIRS]
        for f in sorted(fn):
            if not f.endswith(".html"):
                continue
            abs_p = os.path.join(dp, f)
            rel_p = os.path.relpath(abs_p, WEB).replace(os.sep, "/")
            p = LinkParser()
            p.feed(open(abs_p, encoding="utf-8", errors="ignore").read())
            pages[rel_p] = p
    return pages


def resolve_target(src_rel: str, href: str) -> tuple[str | None, str]:
    """Resolve an internal href from src_rel -> (target_rel_path_or_None, fragment)."""
    raw = href.split("?", 1)[0]
    frag = ""
    if "#" in raw:
        raw, frag = raw.split("#", 1)
    frag = unquote(frag)
    if not raw:
        return src_rel, frag
    if raw.startswith("/"):
        rel = raw.lstrip("/") or "index.html"
    else:
        base_dir = os.path.dirname(src_rel)
        rel = os.path.normpath(os.path.join(base_dir, raw)).replace(os.sep, "/")
    if rel == ".":
        rel = "index.html"
    elif rel.endswith("/"):
        rel += "index.html"
    elif os.path.isdir(os.path.join(WEB, rel)):
        rel = rel.rstrip("/") + "/index.html"
    return rel, frag


def check_external_url(url: str, timeout: float = 5.0) -> tuple[str, int, str]:
    req = urllib.request.Request(
        url,
        method="HEAD",
        headers={"User-Agent": "InfiniteLinkChecker/1.0 (+https://infinitedevices.org)"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return url, resp.status, ""
    except urllib.error.HTTPError as e:
        if e.code in (405, 403):
            req2 = urllib.request.Request(
                url,
                method="GET",
                headers={"User-Agent": "InfiniteLinkChecker/1.0 (+https://infinitedevices.org)"},
            )
            try:
                with urllib.request.urlopen(req2, timeout=timeout) as resp2:
                    return url, resp2.status, ""
            except urllib.error.HTTPError as e2:
                return url, e2.code, str(e2)
            except Exception as ex:
                return url, 0, str(ex)
        return url, e.code, str(e)
    except Exception as ex:
        return url, 0, str(ex)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--internal-only", action="store_true")
    ap.add_argument("--no-cache", action="store_true")
    ap.add_argument("--budget", type=float, default=25.0, help="Seconds budget for external checks")
    ap.add_argument("--cache", default=DEFAULT_CACHE)
    args = ap.parse_args()

    pages = collect_pages()
    broken_links: list[tuple[str, str, str]] = []
    broken_imgs: list[tuple[str, str]] = []
    external_urls: set[str] = set()

    for rel, p in sorted(pages.items()):
        base_dir = os.path.dirname(rel)
        for src in p.imgs:
            if src.startswith(("http://", "https://", "data:")):
                continue
            clean = src.split("?", 1)[0].split("#", 1)[0]
            target = clean.lstrip("/") if clean.startswith("/") else os.path.normpath(os.path.join(base_dir, clean))
            if not os.path.exists(os.path.join(WEB, target)):
                broken_imgs.append((rel, src))

        for href in p.hrefs:
            if href.startswith(("mailto:", "tel:", "javascript:", "data:")):
                continue
            if href.startswith(("http://", "https://")):
                u = urlparse(href)
                if u.netloc == "infinitedevices.org":
                    local_path = u.path.lstrip("/") or "index.html"
                    if local_path.endswith("/"):
                        local_path += "index.html"
                    elif os.path.isdir(os.path.join(WEB, local_path)):
                        local_path += "/index.html"
                    if not os.path.exists(os.path.join(WEB, local_path)):
                        broken_links.append((rel, href, "missing local target for canonical domain"))
                    elif u.fragment and local_path in pages and u.fragment not in pages[local_path].ids:
                        broken_links.append((rel, href, f"missing anchor #{u.fragment}"))
                else:
                    external_urls.add(href.split("#", 1)[0])
                continue

            target_rel, frag = resolve_target(rel, href)
            if not target_rel:
                continue
            abs_target = os.path.join(WEB, target_rel)
            if not os.path.exists(abs_target):
                broken_links.append((rel, href, f"missing file {target_rel}"))
                continue
            if frag and target_rel in pages and frag not in pages[target_rel].ids:
                broken_links.append((rel, href, f"missing anchor #{frag} in {target_rel}"))

    print(f"checked {len(pages)} HTML pages: {len(broken_links)} broken internal links, {len(broken_imgs)} missing images")
    for rel, href, reason in broken_links[:30]:
        print(f"  BROKEN LINK [{rel}] {href} -> {reason}")
    for rel, src in broken_imgs[:30]:
        print(f"  MISSING IMG [{rel}] {src}")

    if not args.internal_only and external_urls:
        cache: dict[str, dict] = {}
        if not args.no_cache and os.path.exists(args.cache):
            try:
                cache = json.load(open(args.cache, encoding="utf-8"))
            except Exception:
                cache = {}
        now = time.time()
        to_check = [
            u for u in sorted(external_urls)
            if u not in cache or (now - cache[u].get("ts", 0)) > 86400
        ]
        if to_check:
            t0 = time.time()
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
                futs = {ex.submit(check_external_url, u, 5.0): u for u in to_check}
                for fut in concurrent.futures.as_completed(futs):
                    u, status, err = fut.result()
                    cache[u] = {"status": status, "err": err, "ts": time.time()}
                    if time.time() - t0 > args.budget:
                        break
            os.makedirs(os.path.dirname(args.cache), exist_ok=True)
            with open(args.cache, "w", encoding="utf-8") as f:
                json.dump(cache, f, indent=2, sort_keys=True)

        ext_ok = sum(1 for u in external_urls if 0 < cache.get(u, {}).get("status", 0) < 400)
        ext_bad = [(u, cache.get(u, {}).get("status", 0), cache.get(u, {}).get("err", "")) for u in sorted(external_urls) if not (0 < cache.get(u, {}).get("status", 0) < 400)]
        print(f"external links: {len(external_urls)} total, {ext_ok} < 400, {len(ext_bad)} >= 400 or unchecked")
        for u, st, err in ext_bad[:20]:
            print(f"  EXT [{st}] {u} {err}")

    if broken_links or broken_imgs:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
