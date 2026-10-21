#!/usr/bin/env python3
"""Outreach queue: finds real questions Infinite can answer, ranks them, and prepares a brief for a human-approved reply.

Nothing here posts anything. A person (Naman, or Claude through the browser with Naman's go) writes each reply by hand, one thread at a time.

  scan [--track A|B]   search Hacker News, Stack Exchange and GitHub issues; add new matches to the queue
  list [--top N]       show open items, best first
  brief <id>           print the thread text, the fit check and the rules for the reply
  mark <id> posted|skipped|declined [--url URL] [--note TEXT]
  caps                 show today's posted count against the daily cap
Scoring: +3 asks for an alternative or recommendation, +2 fresh (under 14 days), +2 question mark in title, +1 per matching keyword, -5 already well answered.
Caps: MAX_PER_DAY posted replies (default 3). Read RULES.md before posting.
"""
from __future__ import annotations
import argparse, datetime, html, json, os, re, sys, time, urllib.parse, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lib import SEO, UA, ledger, save_ledger, today

MAX_PER_DAY = int(os.environ.get("MAX_PER_DAY", "3"))
TOPICS = json.load(open(os.path.join(SEO, "topics.json")))


def get(url):
    try:
        return json.loads(urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=20).read().decode())
    except Exception as ex:
        print("  skip source (%s): %s" % (type(ex).__name__, url[:70])); return None


def sources(q):
    e = urllib.parse.quote(q)
    d = get("https://hn.algolia.com/api/v1/search_by_date?query=%s&tags=story&hitsPerPage=8" % e)
    for h in (d or {}).get("hits", []):
        yield "hn", "https://news.ycombinator.com/item?id=%s" % h["objectID"], h.get("title") or "", h.get("created_at", ""), h.get("num_comments", 0)
    d = get("https://api.stackexchange.com/2.3/search/advanced?order=desc&sort=creation&q=%s&site=sound&pagesize=6" % e)
    for h in (d or {}).get("items", []):
        yield "stackexchange", h["link"], html.unescape(h["title"]), datetime.datetime.utcfromtimestamp(h["creation_date"]).isoformat(), h.get("answer_count", 0)
    d = get("https://api.github.com/search/issues?q=%s+type:issue+state:open&sort=created&order=desc&per_page=5" % e)
    for h in (d or {}).get("items", []):
        yield "github", h["html_url"], h["title"], h["created_at"], h.get("comments", 0)


def score(title, created, answers, hits):
    s = hits
    t = title.lower()
    if re.search(r"alternative|instead of|similar to|replacement|recommend|looking for|which|best|how (do|can|to)", t): s += 3
    if "?" in title: s += 2
    try:
        if (datetime.datetime.utcnow() - datetime.datetime.fromisoformat(created.rstrip("Z")[:19])).days < 14: s += 2
    except ValueError: pass
    if answers >= 5: s -= 5
    return s


def scan(track):
    d = ledger(); q = d.setdefault("outreach", {}).setdefault("queue", {}); added = 0
    for t in TOPICS["queries"]:
        if track and t["track"] != track: continue
        for src, url, title, created, ans in sources(t["q"]):
            if url in q or not title: continue
            hits = sum(1 for k in t["match"] if k in title.lower())
            if not hits: continue
            q[url] = {"id": str(len(q) + 1), "src": src, "title": title, "created": created, "score": score(title, created, ans, hits),
                      "query": t["q"], "track": t["track"], "status": "open", "found": today()}
            added += 1
        time.sleep(1.5)
    save_ledger(d); print("%d new, %d in queue" % (added, len(q)))


def items(d): return d.get("outreach", {}).get("queue", {})


def find(d, i):
    for url, v in items(d).items():
        if v["id"] == i: return url, v
    sys.exit("no item " + i)


def lst(top):
    rows = sorted(((v["score"], u, v) for u, v in items(ledger()).items() if v["status"] == "open"), key=lambda r: -r[0])[:top]
    for sc, u, v in rows: print("#%-3s %3d %-13s %s\n       %s" % (v["id"], sc, v["src"], v["title"][:90], u))


def brief(i):
    d = ledger(); url, v = find(d, i)
    print("ITEM", i, v["src"], url, "\nTITLE:", v["title"])
    if v["src"] == "hn":
        j = get("https://hn.algolia.com/api/v1/items/" + url.split("=")[-1])
        if j:
            print("TEXT:", re.sub(r"<[^>]+>", " ", html.unescape(j.get("text") or j.get("title") or ""))[:1500])
            for c in (j.get("children") or [])[:6]:
                print("  - %s: %s" % (c.get("author"), re.sub(r"<[^>]+>", " ", html.unescape(c.get("text") or ""))[:300]))
    print("\nFIT CHECK (all must be yes, else mark skipped):\n  1. Do they actually ask for something Infinite does (see FACTS.md)?\n  2. Can I answer usefully with Infinite not mentioned at all?\n"
          "  3. Is the thread open to self-made tools under the site's rules?\n  4. Has anyone already given a better answer?\n"
          "\nREPLY RULES: answer first, plain and specific; say 'I make Infinite' once; mention one limit honestly (young project, no AAX);\n"
          "one link at most; British spelling, no exclamation marks, no hype; no pasted template; then `mark %s posted --url <reply url>`." % i)


def mark(i, status, url, note):
    d = ledger(); u, v = find(d, i)
    v["status"] = status; v["note"] = note or ""; v["done"] = today()
    if status == "posted": v["reply_url"] = url or ""
    save_ledger(d); print("marked", i, status)


def caps():
    posted = [v for v in items(ledger()).values() if v["status"] == "posted" and v.get("done") == today()]
    print("today: %d/%d posted" % (len(posted), MAX_PER_DAY), [v["src"] for v in posted])
    return posted


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0]); sp = ap.add_subparsers(dest="c", required=True)
    s = sp.add_parser("scan"); s.add_argument("--track", choices=["A", "B"])
    l = sp.add_parser("list"); l.add_argument("--top", type=int, default=15)
    b = sp.add_parser("brief"); b.add_argument("id")
    m = sp.add_parser("mark"); m.add_argument("id"); m.add_argument("status", choices=["posted", "skipped", "declined"]); m.add_argument("--url"); m.add_argument("--note")
    sp.add_parser("caps")
    a = ap.parse_args()
    if a.c == "scan": scan(a.track)
    elif a.c == "list": lst(a.top)
    elif a.c == "brief": brief(a.id)
    elif a.c == "mark":
        if a.status == "posted" and len(caps()) >= MAX_PER_DAY: sys.exit("daily cap reached")
        mark(a.id, a.status, a.url, a.note)
    else: caps()
