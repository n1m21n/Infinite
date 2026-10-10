#!/usr/bin/env python3
"""Regenerates the search and AI-agent surfaces of the website from the library indexes.

What: writes website/assets/library/catalog.json (every library item in one file), the structured data
      (JSON-LD ItemList) and a plain <noscript> list inside library.html between the seo markers,
      website/sitemap.xml, website/robots.txt and website/llms.txt.
Why:  the library grid is drawn by JavaScript, so a crawler or an AI agent that does not run scripts
      sees an empty page. These files give them the same list as plain HTML, JSON and text.
Usage: python3 tools/website/gen_seo.py     (rerun after the library, templates or community indexes change)
Exit codes: 0 written.
"""
import datetime, html, json, os, re
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W = os.path.join(ROOT, "website")
BASE = "https://n1m21n.github.io/Infinite/"
SRC = [("Templates", "assets/templates/index.json"), ("Community", "assets/community/index.json"), ("", "assets/library/index.json")]

def items():
    out = []
    for _, p in SRC:
        for i in json.load(open(os.path.join(W, p)))["items"]:
            out.append({"id": i["id"], "name": i["name"], "category": i.get("category", ""), "type": i.get("kind", ""),
                        "description": i.get("description", ""), "author": i.get("author", ""), "price": i.get("price", 0),
                        "file": BASE + i["file"], "preview": BASE + i["preview"]})
    return out

def library_block(its):
    ld = {"@context": "https://schema.org", "@type": "CollectionPage", "name": "Infinite Library",
          "description": "Free starter templates, community patches and Field devices for Infinite, the real-time audiovisual workstation.",
          "url": BASE + "library.html", "isPartOf": {"@type": "WebSite", "url": BASE},
          "mainEntity": {"@type": "ItemList", "numberOfItems": len(its), "itemListElement": [
              {"@type": "ListItem", "position": n + 1, "item": {"@type": "CreativeWork", "name": i["name"], "description": i["description"],
                                                                 "genre": i["category"], "image": i["preview"], "isAccessibleForFree": True,
                                                                 "encodingFormat": os.path.splitext(i["file"])[1].lstrip("."), "contentUrl": i["file"],
                                                                 **({"author": {"@type": "Person", "name": i["author"]}} if i["author"] else {})}}
              for n, i in enumerate(its)]}}
    lis = "\n".join(f'      <li><a href="{html.escape(i["file"])}" download>{html.escape(i["name"])}</a> ({html.escape(i["category"])}): {html.escape(i["description"])}</li>' for i in its)
    return ('<!-- seo:start -->\n  <script type="application/ld+json">\n' + json.dumps(ld, indent=1, ensure_ascii=False) +
            '\n  </script>\n  <noscript>\n    <h2>All library items</h2>\n    <ul>\n' + lis + '\n    </ul>\n  </noscript>\n  <!-- seo:end -->')

def main():
    its = items()
    json.dump({"name": "Infinite Library", "url": BASE + "library.html", "count": len(its), "items": its},
              open(os.path.join(W, "assets/library/catalog.json"), "w"), indent=1, ensure_ascii=False)
    p = os.path.join(W, "library.html"); s = open(p).read()
    blk = library_block(its)
    if "<!-- seo:start -->" in s:
        s = re.sub(r"<!-- seo:start -->.*?<!-- seo:end -->", lambda m: blk, s, flags=re.S)
    else:
        s = s.replace("  </main>", "  </main>\n\n  " + blk, 1)
    open(p, "w").write(s)
    today = datetime.date.today().isoformat()
    pages = [("", 1.0), ("library.html", 0.8), ("artworks.html", 0.6), ("assets/library/catalog.json", 0.3)]
    open(os.path.join(W, "sitemap.xml"), "w").write('<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n' +
        "".join(f"  <url><loc>{BASE}{u}</loc><lastmod>{today}</lastmod><priority>{pr}</priority></url>\n" for u, pr in pages) + "</urlset>\n")
    open(os.path.join(W, "robots.txt"), "w").write(
        "# Everyone is welcome, search engines and AI agents included.\nUser-agent: *\nAllow: /\n\n" +
        "".join(f"User-agent: {b}\nAllow: /\n\n" for b in ("GPTBot", "ChatGPT-User", "OAI-SearchBot", "ClaudeBot", "Claude-User", "Claude-SearchBot", "PerplexityBot", "Google-Extended", "Applebot-Extended")) +
        f"Sitemap: {BASE}sitemap.xml\n# Plain-text summary for language models: {BASE}llms.txt\n")
    byc = {}
    for i in its: byc.setdefault(i["category"] or "Other", []).append(i)
    L = ["# Infinite", "",
         "> Infinite is a free, open-source DAW for visuals, music and code on one infinite canvas. Wire shaders, 3D geometry, synths and modulation together in real time. It runs on macOS, Windows and Linux.", "",
         "## Start here", "",
         f"- [Home]({BASE}): what Infinite is, the node canvas, how to set it up",
         f"- [Library]({BASE}library.html): free templates, community patches and Field devices to download",
         f"- [Library catalog (JSON)]({BASE}assets/library/catalog.json): every library item with description, category and file URL",
         f"- [Art Gallery]({BASE}artworks.html): visuals and motion made with Infinite",
         "- [Download](https://github.com/n1m21n/Infinite/releases/latest): installers for macOS, Windows and Linux",
         "- [Source code](https://github.com/n1m21n/Infinite): MIT licensed",
         "- [Discord](https://discord.gg/7cpQfCxnx): community and support",
         "- [YouTube](https://www.youtube.com/channel/UCpuCUMODr4utnpa162CkIug): tutorials and films", "",
         "## File formats", "",
         "- `.inf` and `.infinite`: a patch. A line-based text file listing nodes and cables. Open it with File, Open in Infinite.",
         "- `.field`: a Field device (a small program for a Field node). Import it into a Field Pixel node, or copy its code.", ""]
    for c, v in byc.items():
        L += [f"## Library: {c}", ""] + [f"- [{i['name']}]({i['file']}): {i['description']}" for i in v] + [""]
    open(os.path.join(W, "llms.txt"), "w").write("\n".join(L))
    print(len(its), "items; sitemap, robots, llms.txt, catalog.json, library.html seo block written")
main()
