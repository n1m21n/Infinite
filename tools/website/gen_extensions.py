#!/usr/bin/env python3
"""Extensions page: website/extensions.html, built from the app's own extension catalog.

What: one card per pack in the release catalog (the same extensions.json the app reads in
Settings > Extensions). Each card has an Install button that opens the live-install steps (the app
downloads, verifies and installs the pack while it runs, no restart) and offline links to the
release assets. Also writes website/assets/extensions/extensions.json (the tracked list), index.json + a preview per pack
(the packs as Library items, category Extensions) and adds the
nav link to index.html and library.html.
Why: packs never ship inside the app and never live on the website; the release is the one host.
The page only tracks them, so a new pack in the catalog is a new card with no page edit.

Usage:
    python3 tools/website/gen_extensions.py                  # fetch the latest release catalog
    python3 tools/website/gen_extensions.py --catalog FILE   # use a local extensions.json
Then python3 tools/website/gen_seo.py (sitemap, llms.txt) and python3 tools/brand/brand_lint.py.
"""
import argparse, html, json, os, re, urllib.request
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
W = os.path.join(ROOT, "website")
CATALOG_URL = "https://github.com/n1m21n/Infinite/releases/download/extensions/extensions.json"

# Hand-kept per pack: what it adds and the first app version that can install it.
# A pack missing here still gets a card from the catalog alone.
PACKS = {
    "tracking": {
        "nodes": ["Hand Track", "Face Track", "Pose Track"],
        "blurb": "Turn a camera or a picture into controls: hands, face and body landmarks become modulators.",
        "requires": "0.5.0",
    },
}
PLATFORMS = [("macos", "macOS"), ("windows-x64", "Windows x64"), ("windows-arm64", "Windows ARM64"), ("linux-x64", "Linux x86_64")]

e = html.escape


def card(p):
    meta = PACKS.get(p["id"], {})
    nodes = meta.get("nodes", [])
    mb = f'{p["size"] / 1e6:.0f} MB' if p.get("size") else ""
    links = "".join(f'<a class="ext-file" href="{e(p["files"][k]["url"])}">{label}</a>'
                    for k, label in PLATFORMS if k in p.get("files", {}))
    specs = [("version", p["version"])] + ([("size", mb)] if mb else []) + \
            ([("requires", "Infinite " + meta["requires"] + "+")] if meta.get("requires") else []) + [("price", "free")]
    return f'''        <article class="ext-card" id="{e(p["id"])}" aria-labelledby="{e(p["id"])}-title">
          <div class="ext-head">
            <h2 id="{e(p["id"])}-title">{e(p["name"])}</h2>
            <details class="ext-install">
              <summary class="btn btn-primary btn-sm">Install</summary>
              <div class="ext-steps">
                <p>In Infinite, open <b>Settings &rarr; Extensions</b> and press <b>Install</b> next to {e(p["name"])}. It downloads, checks and installs while the app runs, the same way live updates do. No restart. When a newer version is published, the same row shows <b>Update</b> the next time you open the tab.</p>
                <p class="ext-offline">Offline? Download your platform's pack and use <b>Install from file</b>: {links}</p>
              </div>
            </details>
          </div>
          <p class="ext-purpose">{e(meta.get("blurb", p.get("purpose", "")))}</p>
          {('<p class="ext-nodes mono">adds ' + ", ".join(e(n) for n in nodes) + '</p>') if nodes else ''}
          <dl class="specs mono">{"".join(f"<div><dt>{k}</dt><dd>{e(v)}</dd></div>" for k, v in specs)}</dl>
        </article>
'''


def page(packs):
    t = open(os.path.join(W, "library.html")).read()
    head = t[:t.index("<body>")]
    head = re.sub(r"<title>.*?</title>", "<title>Infinite Extensions: optional packs, installed live</title>", head)
    desc = "Optional packs for Infinite, the open-source audiovisual workstation. Install from Settings, Extensions while the app runs; nothing extra ships in the app."
    head = re.sub(r'(<meta name="description" content=")[^"]*', r"\g<1>" + desc, head)
    head = re.sub(r'(<meta (?:property|name)="(?:og|twitter):description" content=")[^"]*', r"\g<1>" + desc, head)
    head = re.sub(r'(<meta (?:property|name)="(?:og|twitter):title" content=")[^"]*', r"\g<1>Infinite Extensions", head)
    head = head.replace("library.html", "extensions.html")
    head = "\n".join(l for l in head.splitlines() if "assets/library/catalog.json" not in l)
    head = head.replace("</head>", '  <link rel="alternate" type="application/json" href="assets/extensions/extensions.json" title="Extensions">\n'
                                   '  <link rel="stylesheet" href="extensions.css?v=cb2bbbb0">\n</head>')
    header = t[t.index("  <header"):t.index("  <main>")].replace(' aria-current="page"', "")
    header = header.replace('<a href="extensions.html">', '<a href="extensions.html" aria-current="page">')
    return head + '<body class="extensions-page">\n' + header + f'''  <main>
    <section class="section lib-hero">
      <div class="container">
        <div class="section-head">
          <p class="lib-hand hand">more</p>
          <h1 class="slab">Extensions</h1>
          <p class="section-sub">Optional packs for the heavy things. Nothing extra ships in the app: install a pack from Settings while Infinite runs, only if you want it.</p>
        </div>
        <div class="ext-list">
{"".join(card(p) for p in packs)}        </div>
      </div>
    </section>
  </main>
  <script src="site.js?v=4" defer></script>
</body>
</html>
'''


def add_nav(name):
    p = os.path.join(W, name)
    s = open(p).read()
    if 'href="extensions.html"' in s:
        return
    m = re.search(r'\n(\s*)<a href="library.html"[^>]*>Library</a>', s)
    s = s[:m.end()] + "\n" + m.group(1) + '<a href="extensions.html">Extensions</a>' + s[m.end():]
    open(p, "w").write(s)


def preview(p, path):
    """The Library card picture: same look as the AI Skills previews (tools/website/sync_skills.py)."""
    def font(sz):
        try:
            return ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", sz)
        except Exception:
            return ImageFont.load_default()
    meta = PACKS.get(p["id"], {})
    im = Image.new("RGB", (900, 600), "#151930")
    g = ImageDraw.Draw(im)
    g.rounded_rectangle((70, 110, 830, 490), 24, fill="#20263F")
    for k, c in enumerate(["#F57F66", "#343B5E", "#343B5E"]):
        g.ellipse((100 + k * 26, 140, 118 + k * 26, 158), fill=c)
    lines = [("extension pack", "#8890B1"), (p["name"], "#EEF1FA"), ("v" + p["version"], "#A3AACB"), ("", "#fff")]
    lines += [("+ " + n, "#FF6B35") for n in meta.get("nodes", [])[:3]]
    lines += [("", "#fff"), ("Install from Settings > Extensions", "#A7B1FF")]
    for n, (t, c) in enumerate(lines):
        g.text((105, 190 + n * 32), t, font=font(24), fill=c)
    im.save(path, quality=85)


def library_index(packs):
    """assets/extensions/index.json: the packs as Library items (category Extensions), read by library.js and gen_seo.py."""
    items = []
    for p in packs:
        meta = PACKS.get(p["id"], {})
        rel = f'assets/extensions/{p["id"]}.webp'
        preview(p, os.path.join(W, rel))
        items.append({"id": "ext-" + p["id"], "name": p["name"] + " pack", "category": "Extensions", "kind": "Extension",
                      "author": "n1m21n", "description": meta.get("blurb", p.get("purpose", "")), "price": 0,
                      "preview": rel, "file": "extensions.html#" + p["id"]})
    json.dump({"version": 1, "items": items}, open(os.path.join(W, "assets/extensions/index.json"), "w"), indent=1, ensure_ascii=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--catalog", help="local extensions.json (default: the latest release)")
    a = ap.parse_args()
    raw = open(a.catalog).read() if a.catalog else urllib.request.urlopen(CATALOG_URL, timeout=30).read().decode()
    cat = json.loads(raw)
    assert cat.get("schema") == 1, "catalog schema"
    packs = cat["packs"]
    os.makedirs(os.path.join(W, "assets/extensions"), exist_ok=True)
    tracked = {"name": "Infinite Extensions", "source": CATALOG_URL,
               "packs": [dict(p, **{k: v for k, v in PACKS.get(p["id"], {}).items()}) for p in packs]}
    json.dump(tracked, open(os.path.join(W, "assets/extensions/extensions.json"), "w"), indent=1, ensure_ascii=False)
    library_index(packs)
    for n in ("index.html", "library.html"):
        add_nav(n)
    open(os.path.join(W, "extensions.html"), "w").write(page(packs))
    print(len(packs), "packs; extensions.html, assets/extensions/{extensions,index}.json, Library previews, nav links written")


if __name__ == "__main__":
    main()
