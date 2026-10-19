#!/usr/bin/env python3
"""Checks the blog from the outside: is it live, who links to it, is it indexed, is the HTML sound.

  live       fetch every published post, record first-seen-live time in the ledger (starts the 48 h syndication clock)
  audit      static checks on website/blog/*.html: title, description, canonical, JSON-LD, one h1, image alt, internal links
  indexnow   ping IndexNow (Bing, Yandex and others) with the post URLs; needs INDEXNOW_KEY in the env file and the key file on the site
  backlinks  search for pages that link to the blog (DuckDuckGo html, polite, one request per post) and log new ones
Usage: python3 tools/seo/check.py live
"""
from __future__ import annotations
import datetime, json, os, re, sys, time, urllib.parse, urllib.request, urllib.error
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lib import BASE, BLOG, UA, ledger, load_env, load_posts, save_ledger


def fetch(url):
    try:
        r = urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=20)
        return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as ex:
        return ex.code, ""
    except Exception as ex:
        return 0, str(ex)


def live():
    d = ledger(); now = datetime.datetime.utcnow().isoformat(timespec="seconds")
    for p in load_posts():
        code, _ = fetch(p["url"])
        L = d["posts"].setdefault(p["slug"], {})
        if code == 200 and not L.get("live_since"):
            L["live_since"] = now
        print(code, p["url"], "live since", L.get("live_since", "-"))
    save_ledger(d)


def audit():
    bad = 0
    for f in sorted(os.listdir(BLOG)):
        if not f.endswith(".html"):
            continue
        h = open(os.path.join(BLOG, f)).read(); issues = []
        t = re.search(r"<title>(.*?)</title>", h, re.S)
        if not t or not 10 <= len(t.group(1)) <= 80: issues.append("title length")
        if not re.search(r'<meta name="description" content="[^"]{70,160}"', h): issues.append("description 70-160")
        if not re.search(r'<link rel="canonical" href="https://infinitedevices.org/', h): issues.append("canonical")
        if f != "index.html" and "BlogPosting" not in h: issues.append("BlogPosting JSON-LD")
        if "BreadcrumbList" not in h: issues.append("BreadcrumbList")
        if h.count("<h1") != 1: issues.append("h1 count %d" % h.count("<h1"))
        if re.search(r"<img(?![^>]*\balt=)", h): issues.append("img without alt")
        for m in re.findall(r'<script type="application/ld\+json">(.*?)</script>', h, re.S):
            try: json.loads(m)
            except ValueError: issues.append("invalid JSON-LD")
        print(("FAIL " if issues else "ok   ") + f, "; ".join(issues)); bad += bool(issues)
    sys.exit(1 if bad else 0)


def indexnow():
    load_env(); key = os.environ.get("INDEXNOW_KEY")
    if not key:
        sys.exit("set INDEXNOW_KEY (any 8-64 char hex) in ~/.config/infinite-seo.env, and publish website/<key>.txt containing it")
    sitemap_path = os.path.join(os.path.dirname(BLOG), "sitemap.xml")
    if os.path.exists(sitemap_path):
        raw = re.findall(r'<loc>(https://infinitedevices\.org/[^<]+)</loc>', open(sitemap_path).read())
        urls = sorted(list(set([u for u in raw if not u.endswith(('.json', '.xml', '.txt', '.pdf'))])))
    else:
        urls = [BASE + "blog/"] + [p["url"] for p in load_posts()]
    body = json.dumps({"host": "infinitedevices.org", "key": key, "keyLocation": BASE + key + ".txt", "urlList": urls}).encode()
    req = urllib.request.Request("https://api.indexnow.org/indexnow", data=body, headers={"Content-Type": "application/json; charset=utf-8", "User-Agent": UA})
    try: print("IndexNow", urllib.request.urlopen(req, timeout=20).status, len(urls), "urls")
    except urllib.error.HTTPError as ex: print("IndexNow error", ex.code, ex.read().decode()[:200])


def backlinks():
    d = ledger(); known = d.setdefault("backlinks", {}); new = 0
    for p in load_posts():
        q = urllib.parse.quote_plus('"%s"' % p["url"].replace("https://", ""))
        code, h = fetch("https://html.duckduckgo.com/html/?q=" + q)
        for u in set(re.findall(r'uddg=([^&"]+)', h)):
            u = urllib.parse.unquote(u)
            if "infinitedevices.org" in u or u in known: continue
            known[u] = {"to": p["slug"], "seen": datetime.date.today().isoformat()}; new += 1; print("new:", u)
        time.sleep(3)
    save_ledger(d); print(new, "new,", len(known), "total backlinks logged")


if __name__ == "__main__":
    cmds = {"live": live, "audit": audit, "indexnow": indexnow, "backlinks": backlinks}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds: sys.exit(__doc__)
    cmds[sys.argv[1]]()
