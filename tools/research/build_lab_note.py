#!/usr/bin/env python3
"""Builds a research web lab note: website/research/<slug>.html from docs/research/<slug>/.

What: renders docs/research/<slug>/web/note.md (blog Markdown) inside the blog page shell, puts the figures from
      docs/research/<slug>/web/figures.py where the note has <div data-fig="name"></div>, copies data/ unchanged
      to website/research/<slug>/data/, and adds TechArticle JSON-LD, a provenance box pinned to the README's
      commit_sha, and <link rel="alternate"> to data/provenance.json. Reading pages default to Paper (BRAND.md 2.2).
Why:  docs/RESEARCH_STANDARD.md section 5 wants the web note generated from data/, with provenance, on the site shell.
Usage: python3 tools/research/build_lab_note.py <slug>
Exit codes: 0 written, 1 missing input.
"""
import html, importlib.util, json, os, re, shutil, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "seo"))
from lib import BASE, render_md  # noqa: E402
import build_blog  # noqa: E402

e = html.escape
REPO = "https://github.com/n1m21n/Infinite"


def front(text):
    m = re.match(r"^---\n(.*?)\n---\n", text, re.S)
    meta = {}
    for ln in (m.group(1).splitlines() if m else []):
        k, _, v = ln.partition(":")
        meta[k.strip()] = v.strip().strip('"')
    return meta, text[m.end():] if m else text


CSS = """
.rn-verdict { border: 1px solid var(--line); border-left: 3px solid var(--accent); border-radius: var(--radius-card); padding: 14px 18px; margin: 0 0 2em; }
.rn-verdict p { margin: 0.4em 0; }
.rn-verdict-h { font-size: var(--size-caption); text-transform: uppercase; letter-spacing: 0.08em; color: var(--ink-3); }
.rn-fig .chart { width: 100%; height: auto; display: block; font: 500 11px 'Geist Mono', ui-monospace, monospace; fill: var(--ink-2); }
.rn-fig .chart .ax { stroke: var(--ink-3); }
.rn-fig .chart .grid { stroke: var(--line); }
.rn-fig .chart .axl { fill: var(--ink-3); }
.rn-fig .chart .ln { fill: none; stroke-width: 2; }
.rn-fig .chart .dash { stroke-dasharray: 5 4; }
.rn-fig .chart .target { stroke: var(--ink); stroke-dasharray: 3 3; }
.rn-fig .chart .floor { stroke: var(--ink-3); stroke-dasharray: 1 3; }
.rn-fig .chart .agg { stroke: var(--accent); stroke-width: 2; }
.rn-fig .chart .tl { fill: var(--ink); } .rn-fig .chart .al { fill: var(--accent); }
.rn-fig .chart .bar { fill: var(--signal); } .rn-fig .chart .bv { fill: var(--ink-2); }
.rn-fig .chart .sl { font-weight: 600; }
.rn-pick { display: flex; gap: 10px; align-items: center; justify-content: center; font-size: var(--size-caption); margin-bottom: 8px; }
.rn-pick select { font: inherit; padding: 4px 8px; border-radius: var(--radius-control); border: 1px solid var(--line); background: var(--ground); color: var(--ink); max-width: 70vw; }
.rn-readout { font-size: var(--size-caption); color: var(--ink-2); margin: 6px 0 0; min-height: 1.4em; }
.rn-drawer { border: 1px solid var(--line); border-radius: var(--radius-card); padding: 10px 16px; margin: 1.5em 0; }
.rn-drawer summary { cursor: pointer; font-weight: 600; }
.rn-prov { font-size: var(--size-caption); border-top: 1px solid var(--line); margin-top: 2.5em; padding-top: 1em; color: var(--ink-2); }
.rn-prov dl { display: grid; grid-template-columns: max-content 1fr; gap: 4px 14px; margin: 0.6em 0; }
.rn-prov dt { color: var(--ink-3); } .rn-prov dd { margin: 0; overflow-wrap: anywhere; }
"""


def main():
    if len(sys.argv) != 2:
        print(__doc__); sys.exit(1)
    slug = sys.argv[1]
    src = os.path.join(ROOT, "docs", "research", slug)
    note_path, fig_path = os.path.join(src, "web", "note.md"), os.path.join(src, "web", "figures.py")
    for p in (os.path.join(src, "README.md"), note_path, os.path.join(src, "data", "provenance.json")):
        if not os.path.exists(p):
            print("missing", os.path.relpath(p, ROOT)); sys.exit(1)
    readme, _ = front(open(os.path.join(src, "README.md"), encoding="utf-8").read())
    note, body = front(open(note_path, encoding="utf-8").read())
    prov = json.load(open(os.path.join(src, "data", "provenance.json"), encoding="utf-8"))
    sha = readme.get("commit_sha") or prov.get("commit_sha", "")

    out_dir = os.path.join(ROOT, "website", "research")
    data_out = os.path.join(out_dir, slug, "data")
    if os.path.isdir(data_out):
        shutil.rmtree(data_out)
    shutil.copytree(os.path.join(src, "data"), data_out)

    body_html, heads = render_md(body)
    script = ""
    if os.path.exists(fig_path):
        spec = importlib.util.spec_from_file_location("figs", fig_path)
        figs = importlib.util.module_from_spec(spec); spec.loader.exec_module(figs)
        for name, frag in figs.figures(os.path.join(src, "data")).items():
            body_html = body_html.replace(f'<div data-fig="{name}"></div>', frag)
        script = getattr(figs, "SCRIPT", "")
    left = re.findall(r'<div data-fig="([^"]+)"></div>', body_html)
    if left:
        print("no figure for", left); sys.exit(1)

    h2 = [h for h in heads if h[0] == 2]
    toc = '<details class="toc"><summary class="mono">on this page</summary><ol>' + "".join(
        f'<li><a href="#{i}">{e(t)}</a></li>' for _, i, t in h2) + "</ol></details>"
    url = f"{BASE}research/{slug}.html"
    title, desc = readme["title"], note["description"]
    words = len(re.findall(r"\w+", re.sub(r"<[^>]+>", " ", body)))
    art = {"@context": "https://schema.org", "@type": "TechArticle", "headline": title, "description": desc,
           "datePublished": readme["date"], "dateModified": readme["date"], "url": url, "mainEntityOfPage": url,
           "author": {"@type": "Person", "name": "Naman Soni", "alternateName": "n1m21n", "url": "https://github.com/n1m21n"},
           "publisher": {"@type": "Organization", "name": "Infinite", "url": BASE, "logo": {"@type": "ImageObject", "url": BASE + "assets/icon.png"}},
           "keywords": note.get("keywords", ""), "wordCount": words, "inLanguage": "en-GB",
           "proficiencyLevel": "Expert", "isBasedOn": f"{REPO}/tree/{sha}/docs/research/{slug}",
           "image": [BASE + "assets/og-image.jpg"]}
    extra = build_blog.ld(art) + (f'  <link rel="alternate" type="application/json" href="{slug}/data/provenance.json" title="Provenance of the raw data">\n'
                                  f"  <style>{CSS}</style>\n")
    s = build_blog.head(note.get("seo_title", title), desc, url, "assets/og-image.jpg", extra, kind="article")
    s = s.replace('data-mode="midnight"', 'data-mode="paper"', 1).replace("INFINITE_DEFAULT_MODE = 'midnight'", "INFINITE_DEFAULT_MODE = 'paper'", 1)
    header, footer = build_blog.chrome()

    files = sorted(f for f in os.listdir(data_out) if not f.startswith("."))
    data_links = " · ".join(f'<a href="{slug}/data/{e(f)}">{e(f)}</a>' for f in files)
    short = sha[:8]
    prov_html = f"""<section class="rn-prov" aria-label="Provenance">
<p class="mono">provenance</p>
<dl>
<dt>status</dt><dd>{e(readme.get('status', ''))} ({e(readme['date'])})</dd>
<dt>commit</dt><dd><a href="{REPO}/commit/{e(sha)}" rel="noopener"><code>{e(short)}</code></a> reproduces every number on this page</dd>
<dt>machine</dt><dd>{e(prov.get('cpu') or prov.get('hardware', ''))}, {e(prov.get('os', ''))}</dd>
<dt>toolchain</dt><dd>{e(prov.get('compiler', ''))}</dd>
<dt>raw data</dt><dd>{data_links}</dd>
<dt>source</dt><dd><a href="{REPO}/tree/{e(sha)}/docs/research/{slug}" rel="noopener">docs/research/{slug}</a> (README, ARTIFACT.md, scripts)</dd>
</dl>
</section>"""
    s += f"""<body class="blog-page blog-post research-note">
  {header}

  <main>
    <article class="post">
      <div class="container post-wrap">
        <nav class="crumbs mono" aria-label="Breadcrumb"><a href="../index.html">Infinite</a> / <a href="../blog/">Blog</a> / Research</nav>
        <header class="post-head">
          <p class="post-kicker mono">{e(note.get('kicker', 'Lab note'))}</p>
          <h1>{e(title)}</h1>
          <p class="post-lede">{e(desc)}</p>
          <p class="post-by"><img class="by-av" src="../assets/icon-96.png" alt="" width="40" height="40" decoding="async"><span class="by-text"><a href="https://github.com/n1m21n" rel="noopener">Naman Soni</a><span class="by-meta">{e(build_blog.fmt_date(readme['date']))} · {max(1, round(words / 220))} min read</span></span></p>
        </header>
        {toc}
        <div class="prose">
{body_html}
        </div>
        {prov_html}
      </div>
    </article>
  </main>

  {footer}
  <script>{script}</script>
  <script src="../site.js?v=9" defer></script>
</body>
</html>
"""
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, slug + ".html")
    # the site menu, footer, dock and shared scripts, for this page only (sync_chrome.py owns them)
    sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
    from sync_chrome import sync
    open(out, "w", encoding="utf-8").write(sync(f"research/{slug}.html", s))
    print("wrote", os.path.relpath(out, ROOT), "and", len(files), "data file(s)")


if __name__ == "__main__":
    main()
