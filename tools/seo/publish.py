#!/usr/bin/env python3
"""Syndicates blog posts to dev.to, Hashnode and copy-paste packs for other sites, always with a canonical link back.

What: reads tools/seo/posts/<slug>.md (the one source), checks the publish gate, then
        devto    <slug> [--live]   create a dev.to article (draft unless --live), canonical_url = the blog post
        hashnode <slug> [--live]   create a Hashnode draft (or publish with --live), originalArticleURL = the blog post
        pack     <slug>            write ready-to-paste files for Medium (import by URL), Substack, LinkedIn, Hackaday.io, itch.io, Reddit
        status                      show where every post has been syndicated
Why:  the same article on other sites earns reach and links, and the canonical tag makes search engines credit the original on infinitedevices.org.
Gate (all must hold, or the script stops):
        1. the post is status: published and passes lint_post.py with no errors
        2. the post URL answers HTTP 200 on infinitedevices.org (so the original is live)
        3. at least GATE_HOURS (default 48) passed since `check.py live` first saw it, so the original gets indexed first. --force-early overrides.
Secrets: ~/.config/infinite-seo.env  (DEVTO_API_KEY, HASHNODE_TOKEN, HASHNODE_PUBLICATION_ID). Nothing is stored in the repo.
Usage: python3 tools/seo/publish.py devto render-video-and-audio-from-a-text-file --dry-run
Exit codes: 0 done, 1 gate or API failure.
"""
from __future__ import annotations
import argparse, datetime, json, os, re, sys, urllib.request, urllib.error
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lib import BASE, UA, SEO, ledger, load_env, load_posts, save_ledger, today
import lint_post

GATE_HOURS = int(os.environ.get("GATE_HOURS", "48"))
OUT = os.path.join(SEO, "out")


def find(slug):
    for p in load_posts(include_drafts=True):
        if p["slug"] == slug:
            return p
    sys.exit(f"no post {slug!r}")


def gate(p, force_early=False, dry=False):
    if p["status"] != "published":
        sys.exit(f"gate: {p['slug']} is {p['status']}, not published")
    err, _ = lint_post.lint(p)
    if err:
        sys.exit("gate: lint errors:\n  " + "\n  ".join(err))
    try:
        req = urllib.request.Request(p["url"], headers={"User-Agent": UA})
        code = urllib.request.urlopen(req, timeout=15).status
    except urllib.error.HTTPError as ex:
        code = ex.code
    except Exception as ex:
        code = str(ex)
    if code != 200:
        if dry:
            print(f"gate (dry run): {p['url']} -> {code}; would stop here until the site is deployed"); return
        sys.exit(f"gate: {p['url']} -> {code}. Deploy the site first (original must be live).")
    L = ledger()["posts"].get(p["slug"], {})
    seen = L.get("live_since")
    if not seen:
        sys.exit("gate: run `python3 tools/seo/check.py live` first so the live time is recorded")
    age = (datetime.datetime.utcnow() - datetime.datetime.fromisoformat(seen)).total_seconds() / 3600
    if age < GATE_HOURS and not force_early:
        if dry:
            print(f"gate (dry run): live for {age:.0f} h, needs {GATE_HOURS} h"); return
        sys.exit(f"gate: live for {age:.0f} h, wait until {GATE_HOURS} h (or --force-early)")


def body_for_syndication(p):
    img = p.get("image", "")
    cover = img if img.startswith("http") else (BASE + img if img else BASE + "assets/og-image.jpg")
    foot = (f"\n\n---\n\n*Originally published at [{p['url']}]({p['url']}). Infinite is a free, open-source DAW for visuals, music and code: "
            f"[infinitedevices.org](https://infinitedevices.org).*\n")
    return p["body"] + foot, cover


def post_json(url, payload, headers):
    req = urllib.request.Request(url, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json", "User-Agent": UA, **headers})
    try:
        return json.loads(urllib.request.urlopen(req, timeout=30).read().decode())
    except urllib.error.HTTPError as ex:
        sys.exit(f"API error {ex.code}: {ex.read().decode()[:600]}")


def record(slug, platform, **kw):
    d = ledger(); d["posts"].setdefault(slug, {})[platform] = {"at": today(), **kw}; save_ledger(d)


def tags_devto(p):
    return [re.sub(r"[^a-z0-9]", "", t.lower()) for t in p["tags"]][:4]


def devto(p, live, dry):
    body, cover = body_for_syndication(p)
    payload = {"article": {"title": p["title"][:128], "published": live, "body_markdown": body, "tags": tags_devto(p),
                           "canonical_url": p["url"], "description": p["description"][:150], "main_image": cover}}
    if dry:
        print(json.dumps(payload, indent=1)[:1800]); return
    load_env(); key = os.environ.get("DEVTO_API_KEY") or sys.exit("set DEVTO_API_KEY in ~/.config/infinite-seo.env")
    r = post_json("https://dev.to/api/articles", payload, {"api-key": key})
    print("dev.to:", r.get("url"), "(live)" if live else "(draft: review and publish from the dev.to dashboard)")
    record(p["slug"], "devto", url=r.get("url"), id=r.get("id"), live=live)


def hashnode(p, live, dry):
    body, cover = body_for_syndication(p)
    tags = [{"slug": re.sub(r"[^a-z0-9]+", "-", t.lower()).strip("-"), "name": t} for t in p["tags"]][:5]
    inp = {"title": p["title"], "subtitle": p["description"][:250], "contentMarkdown": body, "tags": tags,
           "originalArticleURL": p["url"], "coverImageOptions": {"coverImageURL": cover}}
    op, mut = ("publishPost", "mutation($input: PublishPostInput!){ publishPost(input:$input){ post{ id url slug } } }") if live else \
              ("createDraft", "mutation($input: CreateDraftInput!){ createDraft(input:$input){ draft{ id slug } } }")
    if dry:
        print(op, json.dumps(inp, indent=1)[:1800]); return
    load_env(); tok = os.environ.get("HASHNODE_TOKEN") or sys.exit("set HASHNODE_TOKEN in ~/.config/infinite-seo.env")
    inp["publicationId"] = os.environ.get("HASHNODE_PUBLICATION_ID") or sys.exit("set HASHNODE_PUBLICATION_ID in ~/.config/infinite-seo.env")
    r = post_json("https://gql.hashnode.com", {"query": mut, "variables": {"input": inp}}, {"Authorization": tok})
    if r.get("errors"):
        sys.exit("Hashnode: " + json.dumps(r["errors"])[:600])
    node = r["data"][op].get("post") or r["data"][op].get("draft")
    print("hashnode:", node.get("url") or f"draft {node.get('id')}", "(live)" if live else "(draft: review and publish from the Hashnode dashboard)")
    record(p["slug"], "hashnode", url=node.get("url"), id=node.get("id"), live=live)


def pack(p):
    body, cover = body_for_syndication(p)
    d = os.path.join(OUT, p["slug"]); os.makedirs(d, exist_ok=True)
    w = lambda n, t: open(os.path.join(d, n), "w").write(t)
    w("medium.txt", f"Medium: easiest route keeps the canonical link automatically.\n1. Sign in, open https://medium.com/p/import\n2. Paste: {p['url']}\n3. Review, add 5 tags ({', '.join(p['tags'])}), publish (or submit to a fitting publication).\n")
    w("substack.md", f"# {p['title']}\n\n*{p['description']}*\n\n{body}")
    w("linkedin.txt", f"{p['title']}\n\n{p['description']}\n\nRead it here: {p['url']}\n\n#{' #'.join(re.sub(r'[^A-Za-z0-9]', '', t.title()) for t in p['tags'][:4])}\n")
    w("hackaday-io-log.md", f"### {p['title']}\n\n{p['description']}\n\n{body}")
    w("itch-devlog.md", f"# {p['title']}\n\n{body}")
    w("reddit-self-post.md", f"TITLE: {p['title']}\n\nREAD FIRST: check the subreddit's self-promotion rules and use its flair. Disclose that you made the tool. "
      f"Post text, not just a link, and answer comments for the first hours.\n\nBODY:\n\n{p['description']}\n\n(Write 3 to 5 sentences of real substance from the post here, then:)\n\nFull write-up with details: {p['url']}\n\nI make Infinite, so ask me anything about how it works.\n")
    print("pack written to", os.path.relpath(d))


def status():
    d = ledger()["posts"]
    for p in load_posts(include_drafts=True):
        L = d.get(p["slug"], {})
        pl = [k + ("*" if L[k].get("live") else "(draft)") for k in ("devto", "hashnode") if k in L]
        seen = L.get("live_since", "-")[:16]
        print("%-9s live_since=%-16s syndicated=%-22s %s" % (p["status"], seen, ",".join(pl) or "-", p["slug"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", choices=["devto", "hashnode", "pack", "status"])
    ap.add_argument("slug", nargs="?")
    ap.add_argument("--live", action="store_true", help="publish instead of creating a draft")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--force-early", action="store_true")
    a = ap.parse_args()
    if a.cmd == "status":
        return status()
    if not a.slug:
        sys.exit("slug required")
    p = find(a.slug)
    gate(p, a.force_early, a.dry_run)
    {"devto": lambda: devto(p, a.live, a.dry_run), "hashnode": lambda: hashnode(p, a.live, a.dry_run), "pack": lambda: pack(p)}[a.cmd]()


if __name__ == "__main__":
    main()
