# tools/seo

Python 3.9, standard library only. Posts in `posts/*.md` are the single source for the website blog, RSS, sitemap, llms.txt and every syndication copy.

| Tool | Purpose |
|---|---|
| `seo.py` | report, build, new, ship |
| `build_blog.py [--drafts]` | render `website/blog/`, `feed.xml`, `posts.json` |
| `lint_post.py [slug]` | voice, SEO and truth gates |
| `publish.py devto/hashnode/pack/status <slug>` | syndicate with canonical links; drafts by default |
| `check.py live/audit/indexnow/backlinks` | outside checks, 48 h clock |
| `radar_queue.py scan/list/brief/mark/caps` | find questions, human-written replies |

Setup once: create `~/.config/infinite-seo.env` with `DEVTO_API_KEY=`, `HASHNODE_TOKEN=`, `HASHNODE_PUBLICATION_ID=`, `INDEXNOW_KEY=` (and put `website/<key>.txt` containing the key). Rules: RULES.md. Plan: PLAN.md. Facts: FACTS.md.
Weekly flow: `python3 tools/seo/seo.py new my-slug A`, write, `lint_post.py`, set `status: published`, `seo.py build`, deploy, `check.py live`, wait 48 h, `seo.py ship my-slug`.
Not yet tested live: the dev.to and Hashnode API calls (dry-run only), IndexNow, backlink search.
