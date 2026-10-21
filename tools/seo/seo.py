#!/usr/bin/env python3
"""One entry point for the SEO system. Run `python3 tools/seo/seo.py` for the weekly report.

  report            lint, build, audit, ledger state, outreach queue and what to do next (default)
  build             rebuild the blog, then run gen_seo.py so sitemap and llms.txt include it
  new <slug> A|B    create a draft post from the template (A = product, B = creative)
  ship <slug>       gate check + dev.to draft + Hashnode draft + pack files (never goes live by itself)
Other tools: lint_post.py, build_blog.py, publish.py, check.py, radar_queue.py (each has --help in its docstring).
"""
from __future__ import annotations
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from lib import ledger, load_posts, today


def run(*a):
    return subprocess.run([sys.executable, *a], cwd=ROOT).returncode


def build():
    run("tools/seo/build_blog.py"); run("tools/website/gen_seo.py")


def new(slug, track):
    cat = "product" if track.upper() == "A" else "creative"
    p = os.path.join(HERE, "posts", slug + ".md")
    if os.path.exists(p): sys.exit("exists: " + p)
    open(p, "w").write(f"---\ntitle: \ndescription: \ndate: {today()}\ncategory: {cat}\ntags: [infinite, ]\nimage: assets/og-image.jpg\nimage_alt: \nkeywords: \nstatus: draft\nverify:\n  - every command, node name and value was run or read from the source\n---\n\n")
    print("created", os.path.relpath(p, ROOT), "- read RULES.md and FACTS.md first")


def ship(slug):
    for t in ("devto", "hashnode", "pack"):
        if run("tools/seo/publish.py", t, slug): sys.exit("stopped at " + t)


def report():
    print("== lint =="); run("tools/seo/lint_post.py")
    print("\n== blog audit =="); run("tools/seo/check.py", "audit")
    d = ledger(); posts = load_posts(include_drafts=True)
    print("\n== posts =="); run("tools/seo/publish.py", "status")
    q = d.get("outreach", {}).get("queue", {})
    open_ = [v for v in q.values() if v["status"] == "open"]
    print("\n== outreach ==\n%d open, %d posted, %d skipped" % (len(open_), sum(v["status"] == "posted" for v in q.values()), sum(v["status"] == "skipped" for v in q.values())))
    print("backlinks logged:", len(d.get("backlinks", {})))
    pub = [p for p in posts if p["status"] == "published"]
    print("\n== next ==")
    if any(not d["posts"].get(p["slug"], {}).get("live_since") for p in pub): print("- deploy the site, then: check.py live")
    print("- ship ready posts: seo.py ship <slug> (48 h after live)")
    print("- scan for questions: radar_queue.py scan; answer at most 3 a day, by hand")
    print("- cadence: one new post a week, alternating track A and B (PLAN.md)")


if __name__ == "__main__":
    a = sys.argv[1:] or ["report"]
    {"report": lambda: report(), "build": lambda: build(), "new": lambda: new(a[1], a[2]), "ship": lambda: ship(a[1])}.get(a[0], lambda: sys.exit(__doc__))()
