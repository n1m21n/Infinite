#!/usr/bin/env python3
"""Builds a research report: website/research/<slug>.html and <slug>.pdf from docs/research/<slug>/.

What: renders docs/research/<slug>/web/note.md as a numbered technical report on the site shell, and the same text as
      a print layout turned into a PDF by headless Chrome, so the web page and the paper cannot drift.
      In note.md:  ## / ### headings are numbered 1, 1.1 ...;  <div data-fig="name"></div> places a figure or table
      from web/figures.py (numbered in order, "Figure n" / "Table n");  [[fig:name]] refers to one;
      [@key] or [@a; @b] cites web/references.json (numbered in order of first citation, listed at the end);
      {{name}} is a number from figures.numbers(data_dir), filled in the body, abstract, description and outcome. Frontmatter: description, abstract, keywords, series,
      version, seo_title, prereg_commit (the commit that locked sections 1-7), outcome (the status line). data/ is copied unchanged to website/research/<slug>/data/.
      The page carries TechArticle JSON-LD, <link rel="alternate"> to data/provenance.json, BibTeX, and a provenance
      block pinned to the README's commit_sha. Reading pages default to Paper (BRAND.md 2.2).
Why:  docs/RESEARCH_STANDARD.md section 5: the web note and paper are generated from data/, with provenance.
Usage: python3 tools/research/build_lab_note.py <slug> [--no-pdf]
Exit codes: 0 written, 1 missing input or unresolved marker, 2 PDF failed.
"""
import html, importlib.util, json, os, re, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "seo"))
from lib import BASE, render_md  # noqa: E402
import build_blog  # noqa: E402

e = html.escape
REPO = "https://github.com/n1m21n/Infinite"
AUTHOR = "Naman Soni"
TABLES = {"costs"}
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"


def front(text):
    m = re.match(r"^---\n(.*?)\n---\n", text, re.S)
    meta = {}
    for ln in (m.group(1).splitlines() if m else []):
        k, _, v = ln.partition(":")
        meta[k.strip()] = v.strip().strip('"')
    return meta, text[m.end():] if m else text


def git_date(rev):
    r = subprocess.run(["git", "-C", ROOT, "log", "-1", "--format=%cs", rev], capture_output=True, text=True)
    return r.stdout.strip()


def number_headings(body):
    """1, 1.1 ... on h2/h3; returns the new html and [(level, id, number, text)]."""
    toc, n2, n3 = [], 0, 0

    def sub(m):
        nonlocal n2, n3
        lvl, hid, text = int(m.group(1)), m.group(2), m.group(3)
        if lvl == 2:
            n2, n3 = n2 + 1, 0; num = str(n2)
        else:
            n3 += 1; num = f"{n2}.{n3}"
        toc.append((lvl, hid, num, re.sub(r"<[^>]+>", "", text)))
        return f'<h{lvl} id="{hid}"><span class="rn-sn">{num}</span>{text}</h{lvl}>'
    return re.sub(r'<h([23]) id="([^"]+)">(.*?)</h\1>', sub, body), toc


def place_figures(body, figs, mode):
    """Wraps each placeholder as Figure n / Table n; resolves [[fig:name]]."""
    num, counts = {}, {"Figure": 0, "Table": 0}
    for name in re.findall(r'<div data-fig="([^"]+)"></div>', body):
        if name not in figs:
            sys.exit(f"no figure named {name}")
        kind = "Table" if name in TABLES else "Figure"
        counts[kind] += 1
        num[name] = (kind, counts[kind])
        inner, cap = figs[name]
        tag = f"<b>{kind} {counts[kind]}.</b> {cap}"
        block = (f'<figure class="rn-fig rn-tab" id="fig-{name}"><figcaption>{tag}</figcaption>{inner}</figure>' if kind == "Table"
                 else f'<figure class="rn-fig" id="fig-{name}">{inner}<figcaption>{tag}</figcaption></figure>')
        body = body.replace(f'<div data-fig="{name}"></div>', block, 1)

    def ref(m):
        if m.group(1) not in num:
            sys.exit(f"[[fig:{m.group(1)}]] refers to no placed figure")
        kind, n = num[m.group(1)]
        return f'<a href="#fig-{m.group(1)}">{kind} {n}</a>'
    return re.sub(r"\[\[fig:([\w-]+)\]\]", ref, body)


def cite(body, refs):
    order = []

    def sub(m):
        keys = [k.strip().lstrip("@") for k in m.group(1).split(";")]
        for k in keys:
            if k not in refs:
                sys.exit(f"[@{k}] is not in references.json")
            if k not in order:
                order.append(k)
        return "[" + ", ".join(f'<a href="#ref-{k}" class="rn-cite">{order.index(k) + 1}</a>' for k in keys) + "]"
    return re.sub(r"\[(@[\w]+(?:;\s*@[\w]+)*)\]", sub, body), order


def ref_list(order, refs):
    items = []
    for k in order:
        r = refs[k]
        yr = f" ({e(r['year'])})" if r.get("year") else ""
        items.append(f'<li id="ref-{k}">{e(r["authors"])}{yr}. {e(r["title"])}. <i>{e(r["venue"])}</i>. '
                     f'<a href="{e(r["url"])}" rel="noopener">{e(re.sub(r"^https?://", "", r["url"]))}</a></li>')
    return '<ol class="rn-refs">' + "".join(items) + "</ol>"


CSS = """
body.research-paper { background: var(--ground); background-image: none; }
.rp { max-width: 1120px; margin: 0 auto; padding: 32px 20px 64px; display: grid; grid-template-columns: minmax(0, 1fr); gap: 48px; }
@media (min-width: 1080px) { .rp { grid-template-columns: 200px minmax(0, 700px); justify-content: center; } }
.rp-toc { display: none; font-size: 0.8125rem; line-height: 1.5; }
@media (min-width: 1080px) { .rp-toc { display: block; } .rp-toc nav { position: sticky; top: 96px; } }
.rp-toc p { font-size: 0.75rem; font-weight: 600; color: var(--ink-3); margin: 0 0 10px; }
.rp-toc ol { list-style: none; margin: 0; padding: 0; } .rp-toc li { margin: 0 0 6px; }
.rp-toc li.l3 { padding-left: 1.6em; font-size: 0.75rem; }
.rp-toc a { color: var(--ink-2); text-decoration: none; } .rp-toc a:hover { color: var(--ink); }
.rp-toc .n { font-variant-numeric: tabular-nums; color: var(--ink-3); display: inline-block; min-width: 1.9em; }
.rp-toc li.l3 .n { min-width: 2.7em; }
.rp-main { min-width: 0; font-size: 1.0625rem; line-height: 1.65; color: var(--ink); }
.rp-crumbs { font-size: 0.8125rem; font-weight: 500; color: var(--ink-3); margin: 0 0 28px; }
.rp-crumbs a { color: inherit; }
.rp-series { font-size: 0.8125rem; font-weight: 500; color: var(--ink-3); margin: 0 0 12px; }
.rp-main h1 { font-size: clamp(1.75rem, 4.2vw, 2.4rem); line-height: 1.15; letter-spacing: -0.02em; font-weight: 700; margin: 0 0 18px; }
.rp-by { margin: 0; font-size: 1rem; } .rp-by a { color: var(--ink); }
.rp-aff { margin: 2px 0 0; color: var(--ink-2); font-size: 0.875rem; }
.rp-dates { margin: 14px 0 0; display: flex; flex-wrap: wrap; gap: 4px 18px; font-size: 0.8125rem; font-weight: 500; font-variant-numeric: tabular-nums; color: var(--ink-2); }
.rp-dates b { font-weight: 500; color: var(--ink-3); margin-right: 6px; }
.rp-status { color: var(--accent); }
.rp-links { display: flex; flex-wrap: wrap; gap: 8px; margin: 20px 0 0; padding: 0; list-style: none; }
.rp-links a { display: inline-flex; align-items: center; min-height: 34px; padding: 0 14px; border: 1px solid var(--line); border-radius: var(--radius-control); font-size: 0.8125rem; font-weight: 500; color: var(--ink); text-decoration: none; }
.rp-links a:hover { border-color: var(--ink-3); }
.rp-abs { margin: 32px 0 0; padding: 20px 0; border-top: 1px solid var(--line); border-bottom: 1px solid var(--line); }
.rp-abs h2 { font-size: 0.8125rem !important; color: var(--ink-3); margin: 0 0 8px !important; font-weight: 600; }
.rp-abs p { margin: 0; font-size: 1rem; line-height: 1.6; }
.rp-kw { margin: 12px 0 0 !important; font-size: 0.875rem !important; color: var(--ink-2); } .rp-kw b { font-weight: 600; color: var(--ink); }
.rp-body h2 { font-size: 1.375rem; line-height: 1.3; font-weight: 650; margin: 2.4em 0 0.6em; scroll-margin-top: 90px; }
.rp-body h3 { font-size: 1.0625rem; font-weight: 650; margin: 1.8em 0 0.4em; scroll-margin-top: 90px; }
.rn-sn { font-variant-numeric: tabular-nums; font-weight: 500; color: var(--ink-3); margin-right: 0.7em; }
.rp-body p, .rp-body li { overflow-wrap: break-word; }
.rp-body p, .rp-body ul, .rp-body ol { margin: 0 0 1em; } .rp-body li { margin: 0 0 0.4em; }
.rn-fig .chart.sq { max-width: 440px; }
.rp-body code { font-size: 0.9em; }
.rp-body table { font-size: 0.875rem; line-height: 1.45; border-collapse: collapse; width: 100%; }
.rp-body th { text-align: left; font-weight: 600; border-bottom: 1px solid var(--ink-3); padding: 6px 10px 6px 0; vertical-align: bottom; }
.rp-body td { border-bottom: 1px solid var(--line); padding: 6px 10px 6px 0; vertical-align: top; }
.rn-num td, .rn-num th { font-variant-numeric: tabular-nums; font-size: 0.8125rem; white-space: nowrap; }
.rn-num tr.rn-sum td { border-top: 1px solid var(--ink-3); font-weight: 600; }
.rn-cite { font-variant-numeric: tabular-nums; font-size: 0.85em; text-decoration: none; color: var(--accent); }
.rn-fig { margin: 2em 0; }
.rn-fig figcaption { font-size: 0.875rem; line-height: 1.5; color: var(--ink-2); margin: 10px 0 0; }
.rn-fig.rn-tab figcaption { margin: 0 0 10px; }
.rn-fig figcaption b { color: var(--ink); font-weight: 600; }
.rn-fig .chart { width: 100%; max-width: 620px; height: auto; display: block; margin: 0 auto; font: 500 12.5px var(--font-sans, 'Geist', system-ui, sans-serif); font-variant-numeric: tabular-nums; fill: var(--ink-2); }
.rn-fig .chart .ax { stroke: var(--ink-3); } .rn-fig .chart .grid { stroke: var(--line); }
.rn-fig .chart .axl { fill: var(--ink-3); }
.rn-fig .chart .ln { fill: none; stroke-width: 2; } .rn-fig .chart .dash { stroke-dasharray: 5 4; }
.rn-fig .chart .target { stroke: var(--ink); stroke-dasharray: 3 3; } .rn-fig .chart .floor { stroke: var(--ink-3); stroke-dasharray: 1 3; }
.rn-fig .chart .agg { stroke: var(--accent); stroke-width: 2; }
.rn-fig .chart .tl { fill: var(--ink); } .rn-fig .chart .al { fill: var(--accent); }
.rn-fig .chart .bar { fill: var(--signal); } .rn-fig .chart .bv { fill: var(--ink-2); }
.rn-fig .chart .sl { font-weight: 600; }
.rn-grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.rn-pick { display: flex; flex-wrap: wrap; gap: 8px 10px; align-items: center; font-size: 0.875rem; margin-bottom: 8px; }
.rn-pick select { font: inherit; padding: 6px 8px; border-radius: var(--radius-control); border: 1px solid var(--line); background: var(--ground); color: var(--ink); max-width: 100%; }
.rn-readout { font-size: 0.875rem; color: var(--ink-2); margin: 6px 0 0; min-height: 1.5em; }
.rn-audio { display: grid; gap: 10px; }
.rn-clip { border: 1px solid var(--line); border-radius: var(--radius-card); padding: 10px 12px; }
.rn-clip-h { display: flex; justify-content: space-between; gap: 12px; font-size: 0.875rem; margin-bottom: 6px; }
.rn-db { font-variant-numeric: tabular-nums; color: var(--ink-2); white-space: nowrap; }
.rn-clip audio { width: 100%; height: 36px; }
.rn-drawer { border-top: 1px solid var(--line); border-bottom: 1px solid var(--line); padding: 10px 0; margin: 2em 0; }
.rn-drawer summary { cursor: pointer; font-weight: 600; }
.rn-refs { font-size: 0.875rem; line-height: 1.5; padding-left: 2em; }
.rn-refs li { margin: 0 0 8px; overflow-wrap: anywhere; } .rn-refs li::marker { font-variant-numeric: tabular-nums; color: var(--ink-3); }
.rn-refs li:target { background: color-mix(in srgb, var(--accent) 12%, transparent); }
.rn-bib { font: 500 0.75rem/1.55 var(--font-mono, ui-monospace, monospace); background: color-mix(in srgb, var(--ink) 4%, transparent); border-radius: var(--radius-card); padding: 14px 16px; overflow-x: auto; white-space: pre; }
.rn-prov dl { display: grid; grid-template-columns: max-content 1fr; gap: 4px 16px; font-size: 0.875rem; margin: 0.6em 0; }
.rn-prov dt { color: var(--ink-3); } .rn-prov dd { margin: 0; overflow-wrap: anywhere; }
@media (max-width: 560px) { .rn-fig .chart { font-size: 15px; } .rp-main { font-size: 1rem; } .rn-grid2 { grid-template-columns: 1fr; } .rn-prov dl { grid-template-columns: 1fr; } .rn-prov dt { margin-top: 6px; } }
"""

PRINT_CSS = """
@page { size: A4; margin: 20mm 19mm 20mm 19mm; }
:root { --ground: #fff; }
html, body { background: #fff; margin: 0; }
body { font: 400 9.6pt/1.5 'Geist', system-ui, sans-serif; color: var(--ink); }
.pp { max-width: 100%; }
.pp-series { font-size: 7.6pt; font-weight: 500; color: var(--ink-3); margin: 0 0 8pt; }
h1 { font-size: 19pt; line-height: 1.15; letter-spacing: -0.02em; font-weight: 700; margin: 0 0 10pt; }
.pp-by { margin: 0; font-size: 10pt; } .pp-aff { margin: 1pt 0 0; font-size: 8.6pt; color: var(--ink-2); }
.pp-dates { font-size: 7.6pt; font-weight: 500; font-variant-numeric: tabular-nums; color: var(--ink-2); margin: 8pt 0 0; }
.pp-abs { margin: 14pt 0 0; padding: 10pt 0; border-top: 0.6pt solid var(--ink-3); border-bottom: 0.6pt solid var(--ink-3); }
.pp-abs p { margin: 0; font-size: 9pt; line-height: 1.5; text-align: justify; hyphens: auto; }
.pp-abs .h { font-size: 7.8pt; font-weight: 600; color: var(--ink-3); margin: 0 0 4pt; }
.pp-kw { margin-top: 6pt !important; font-size: 8.4pt !important; color: var(--ink-2); }
.pp-body { margin-top: 6pt; }
.pp-body p, .pp-body li { text-align: justify; hyphens: auto; }
.pp-body h2 { font-size: 11.5pt; font-weight: 700; margin: 16pt 0 5pt; break-after: avoid; }
.pp-body h3 { font-size: 9.8pt; font-weight: 650; margin: 11pt 0 3pt; break-after: avoid; }
.pp-body p { margin: 0 0 6pt; } .pp-body ul { margin: 0 0 6pt; padding-left: 14pt; } .pp-body li { margin: 0 0 3pt; }
.rn-sn { font-variant-numeric: tabular-nums; font-weight: 500; color: var(--ink-3); margin-right: 0.6em; }
code { font: 500 0.9em 'Geist Mono', monospace; }
a { color: inherit; text-decoration: none; } .rn-cite { color: var(--accent); font-variant-numeric: tabular-nums; font-size: 0.85em; }
table { border-collapse: collapse; width: 100%; font-size: 8pt; line-height: 1.4; }
th { text-align: left; font-weight: 600; border-bottom: 0.6pt solid var(--ink); padding: 3pt 6pt 3pt 0; vertical-align: bottom; }
td { border-bottom: 0.4pt solid var(--line); padding: 3pt 6pt 3pt 0; vertical-align: top; }
.rn-num td, .rn-num th { font-variant-numeric: tabular-nums; font-size: 7.4pt; white-space: nowrap; }
.rn-num tr.rn-sum td { border-top: 0.6pt solid var(--ink); font-weight: 600; }
.table-wrap { break-inside: avoid; }
.rn-fig { margin: 10pt 0; break-inside: avoid; }
.rn-fig figcaption { font-size: 8pt; line-height: 1.45; color: var(--ink-2); margin: 5pt 0 0; text-align: justify; }
.rn-fig.rn-tab figcaption { margin: 0 0 5pt; } .rn-fig figcaption b { color: var(--ink); }
.rn-fig .chart { width: 100%; max-width: 118mm; height: auto; display: block; margin: 0 auto; font: 500 12.5px 'Geist', system-ui, sans-serif; font-variant-numeric: tabular-nums; fill: var(--ink-2); }
.rn-grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 4pt 10pt; max-width: 120mm; margin: 0 auto; } .rn-grid2 .chart { max-width: none; } .rn-fig .chart.sq { max-width: 80mm; }
.rn-fig .chart .ax { stroke: var(--ink-3); } .rn-fig .chart .grid { stroke: var(--line); } .rn-fig .chart .axl { fill: var(--ink-3); }
.rn-fig .chart .ln { fill: none; stroke-width: 2; } .rn-fig .chart .dash { stroke-dasharray: 5 4; }
.rn-fig .chart .target { stroke: var(--ink); stroke-dasharray: 3 3; } .rn-fig .chart .agg { stroke: var(--accent); stroke-width: 2; }
.rn-fig .chart .tl { fill: var(--ink); } .rn-fig .chart .al { fill: var(--accent); }
.rn-fig .chart .bar { fill: var(--signal); } .rn-fig .chart .sl { font-weight: 600; }
details.rn-drawer > summary { list-style: none; font-weight: 700; font-size: 9.8pt; margin: 12pt 0 4pt; }
details.rn-drawer > summary::-webkit-details-marker { display: none; }
.rn-refs { font-size: 8pt; line-height: 1.45; padding-left: 16pt; } .rn-refs li { margin: 0 0 3pt; overflow-wrap: anywhere; }
.pp-prov { font-size: 7.6pt; color: var(--ink-2); border-top: 0.4pt solid var(--line); margin-top: 12pt; padding-top: 6pt; }
.pp-prov p { margin: 0 0 2pt; text-align: left; overflow-wrap: anywhere; }
.pp-prov { break-inside: avoid; }
"""


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__); sys.exit(1)
    slug, want_pdf = args[0], "--no-pdf" not in sys.argv
    src = os.path.join(ROOT, "docs", "research", slug)
    data_dir = os.path.join(src, "data")
    paths = {k: os.path.join(src, *p) for k, p in {"readme": ["README.md"], "note": ["web", "note.md"], "prov": ["data", "provenance.json"],
                                                    "figs": ["web", "figures.py"], "refs": ["web", "references.json"]}.items()}
    for p in paths.values():
        if not os.path.exists(p):
            print("missing", os.path.relpath(p, ROOT)); sys.exit(1)
    readme, _ = front(open(paths["readme"], encoding="utf-8").read())
    note, md = front(open(paths["note"], encoding="utf-8").read())
    prov = json.load(open(paths["prov"], encoding="utf-8"))
    refs = json.load(open(paths["refs"], encoding="utf-8"))
    sha = readme.get("commit_sha") or prov.get("commit_sha", "")
    spec = importlib.util.spec_from_file_location("figs", paths["figs"])
    figs = importlib.util.module_from_spec(spec); spec.loader.exec_module(figs)
    TABLES.update(getattr(figs, "TABLES", ()))  # a study's figures.py may name its own tables

    for k, v in figs.numbers(data_dir).items():
        md = md.replace("{{" + k + "}}", v)
        for f in ("abstract", "description", "outcome"):
            if f in note:
                note[f] = note[f].replace("{{" + k + "}}", v)
    left = re.findall(r"\{\{(\w+)\}\}", md)
    if left:
        print("no number for", left); sys.exit(1)

    out_dir = os.path.join(ROOT, "website", "research")
    data_out = os.path.join(out_dir, slug, "data")
    if os.path.isdir(data_out):
        shutil.rmtree(data_out)
    shutil.copytree(data_dir, data_out)

    raw, _ = render_md(md)
    raw, order = cite(raw, refs)
    raw, toc = number_headings(raw)
    n_sec = sum(1 for t in toc if t[0] == 2)
    tail_html = (f'<h2 id="references"><span class="rn-sn">{n_sec + 1}</span>References</h2>' + ref_list(order, refs))
    toc.append((2, "references", str(n_sec + 1), "References"))

    title, desc = readme["title"], note["description"]
    url = f"{BASE}research/{slug}.html"
    pdf_name = f"{slug}.pdf"
    published, prereg = readme["date"], git_date(note.get("prereg_commit", "d38d29b7")) or readme["date"]
    status = readme.get("status", "")
    series, version = note.get("series", "Infinite Research"), note.get("version", "1.0")
    year = published[:4]
    bib = (f"@techreport{{soni{year}{slug.split('-')[0]},\n  author      = {{{AUTHOR}}},\n  title       = {{{title}}},\n"
           f"  institution = {{Infinite}},\n  type        = {{{series.split(' · ')[-1]}}},\n  year        = {{{year}}},\n"
           f"  note        = {{Version {version}, pre-registered study, data and code at commit {sha[:8]}}},\n  url         = {{{url}}}\n}}")
    words = len(re.findall(r"\w+", re.sub(r"<[^>]+>", " ", raw)))

    def body_for(mode):
        b = place_figures(raw, figs.figures(data_dir, mode, f"{slug}/data" if mode == "web" else f"{BASE}research/{slug}/data"), mode)
        left = re.findall(r'<div data-fig="([^"]+)"></div>', b)
        if left:
            print("no figure for", left); sys.exit(1)
        return b

    web_body = body_for("web")
    files = sorted(f for f in os.listdir(data_out) if not f.startswith("."))
    tree = f"{REPO}/tree/{sha}/docs/research/{slug}"
    data_links = " · ".join((f'<a href="{tree}/data/{e(f)}" rel="noopener">{e(f)}/</a>' if os.path.isdir(os.path.join(data_out, f))
                             else f'<a href="{slug}/data/{e(f)}">{e(f)}</a>') for f in files)

    # ---------- web page ----------
    art = {"@context": "https://schema.org", "@type": "TechArticle", "headline": title, "description": desc,
           "abstract": note.get("abstract", ""), "datePublished": published, "dateModified": published, "version": version,
           "url": url, "mainEntityOfPage": url, "isPartOf": {"@type": "Periodical", "name": "Infinite Research"},
           "author": {"@type": "Person", "name": AUTHOR, "alternateName": "n1m21n", "url": "https://github.com/n1m21n"},
           "publisher": {"@type": "Organization", "name": "Infinite", "url": BASE, "logo": {"@type": "ImageObject", "url": BASE + "assets/icon.png"}},
           "keywords": note.get("keywords", ""), "wordCount": words, "inLanguage": "en-GB", "proficiencyLevel": "Expert",
           "isBasedOn": f"{REPO}/tree/{sha}/docs/research/{slug}", "encoding": {"@type": "MediaObject", "encodingFormat": "application/pdf", "contentUrl": f"{BASE}research/{pdf_name}"},
           "citation": [refs[k]["url"] for k in order], "image": [BASE + "assets/og-image.jpg"]}
    extra = build_blog.ld(art) + (
        f'  <link rel="alternate" type="application/json" href="{slug}/data/provenance.json" title="Provenance of the raw data">\n'
        f'  <link rel="alternate" type="application/pdf" href="{pdf_name}" title="PDF">\n'
        f'  <meta name="citation_title" content="{e(title)}">\n  <meta name="citation_author" content="{AUTHOR}">\n'
        f'  <meta name="citation_publication_date" content="{published.replace("-", "/")}">\n  <meta name="citation_technical_report_institution" content="Infinite">\n'
        f'  <meta name="citation_pdf_url" content="{BASE}research/{pdf_name}">\n'
        f"  <style>{CSS}</style>\n")
    s = build_blog.head(note.get("seo_title", title), desc, url, "assets/og-image.jpg", extra, kind="article")
    s = s.replace('data-mode="midnight"', 'data-mode="paper"', 1).replace("INFINITE_DEFAULT_MODE = 'midnight'", "INFINITE_DEFAULT_MODE = 'paper'", 1)
    header, footer = build_blog.chrome()
    toc_html = "".join(f'<li class="l{lvl}"><a href="#{i}"><span class="n">{n}</span>{e(t)}</a></li>' for lvl, i, n, t in toc)
    kw = ", ".join(k.strip() for k in note.get("keywords", "").split(","))
    s += f"""<body class="blog-page research-paper">
  {header}

  <main>
    <div class="rp">
      <aside class="rp-toc" aria-label="Contents"><nav><p>Contents</p><ol>{toc_html}</ol></nav></aside>
      <article class="rp-main">
        <nav class="rp-crumbs" aria-label="Breadcrumb"><a href="../index.html">Infinite</a> / <a href="../blog/">Research</a></nav>
        <header>
          <p class="rp-series">{e(series)}</p>
          <h1>{e(title)}</h1>
          <p class="rp-by"><a href="https://github.com/n1m21n" rel="noopener">{AUTHOR}</a></p>
          <p class="rp-aff">Infinite · infinitedevices.org</p>
          <p class="rp-dates"><span><b>pre-registered</b>{e(prereg)}</span><span><b>published</b>{e(published)}</span><span><b>version</b>{e(version)}</span><span><b>outcome</b><span class="rp-status">{e(note.get("outcome", "H1 not confirmed · H2 " + status))}</span></span></p>
          <ul class="rp-links"><li><a href="{pdf_name}">PDF</a></li><li><a href="{tree}/data" rel="noopener">Data</a></li><li><a href="{REPO}/tree/{e(sha)}/docs/research/{slug}" rel="noopener">Code</a></li><li><a href="#cite">Cite</a></li></ul>
        </header>
        <section class="rp-abs" aria-labelledby="abstract"><h2 id="abstract">Abstract</h2><p>{e(note.get('abstract', ''))}</p>
          <p class="rp-kw"><b>Keywords</b> {e(kw)}</p></section>
        <div class="rp-body">
{web_body}
{tail_html}
          <h2 id="cite">How to cite</h2>
          <pre class="rn-bib">{e(bib)}</pre>
          <section class="rn-prov" aria-labelledby="provenance">
            <h2 id="provenance">Provenance</h2>
            <dl>
              <dt>commit</dt><dd><a href="{REPO}/commit/{e(sha)}" rel="noopener"><code>{e(sha[:8])}</code></a> reproduces every number in this report</dd>
              <dt>machine</dt><dd>{e(prov.get('cpu') or prov.get('hardware', ''))}, {e(prov.get('os', ''))}</dd>
              <dt>toolchain</dt><dd>{e(prov.get('compiler', ''))}</dd>
              <dt>raw data</dt><dd>{data_links}</dd>
              <dt>source</dt><dd><a href="{REPO}/tree/{e(sha)}/docs/research/{slug}" rel="noopener">docs/research/{slug}</a> (README, ARTIFACT.md, scripts)</dd>
            </dl>
          </section>
        </div>
      </article>
    </div>
  </main>

  {footer}
  <script>{getattr(figs, "SCRIPT", "")}</script>
  <script src="../site.js?v=9" defer></script>
</body>
</html>
"""
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, slug + ".html")
    sys.path.insert(0, os.path.join(ROOT, "tools", "website"))
    from sync_chrome import sync  # the site menu, footer and shared scripts, for this page only
    open(out, "w", encoding="utf-8").write(sync(f"research/{slug}.html", s))
    print("wrote", os.path.relpath(out, ROOT), "and", len(files), "data entries")

    if not want_pdf:
        return
    # ---------- PDF: the same text, static figures, print layout ----------
    paper_body = body_for("paper").replace("<details", "<div").replace("</details>", "</div>").replace("<summary", "<p").replace("</summary>", "</p>")
    paper = f"""<!doctype html><html lang="en-GB" data-mode="paper"><head><meta charset="utf-8"><title>{e(title)}</title>
<link href="https://fonts.googleapis.com/css2?family=Geist:wght@400;500;600;700&family=Geist+Mono:wght@400;500;600&display=swap" rel="stylesheet">
<link rel="stylesheet" href="../brand.css"><style>{PRINT_CSS}</style></head><body><div class="pp">
<p class="pp-series">{e(series)} · version {e(version)} · {e(url)}</p>
<h1>{e(title)}</h1>
<p class="pp-by">{AUTHOR}</p><p class="pp-aff">Infinite · infinitedevices.org</p>
<p class="pp-dates">pre-registered {e(prereg)} · published {e(published)} · data and code at commit {e(sha[:8])}</p>
<div class="pp-abs"><p class="h">Abstract</p><p>{e(note.get('abstract', ''))}</p><p class="pp-kw"><b>Keywords:</b> {e(kw)}</p></div>
<div class="pp-body">{paper_body}{tail_html}</div>
<div class="pp-prov"><p>Provenance. Commit {e(sha)} reproduces every number in this report. Machine: {e(prov.get('cpu', ''))}, {e(prov.get('os', ''))}; {e(prov.get('compiler', ''))}.</p>
<p>Raw data: {e(BASE)}research/{e(slug)}/data/ · Source: {e(REPO)}/tree/{e(sha[:8])}/docs/research/{e(slug)} · Interactive version: {e(url)}</p></div>
</div></body></html>"""
    tmp = os.path.join(out_dir, f".{slug}-print.html")
    open(tmp, "w", encoding="utf-8").write(paper)
    pdf = os.path.join(out_dir, pdf_name)
    r = subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", "--virtual-time-budget=15000",
                        f"--print-to-pdf={pdf}", "file://" + tmp], capture_output=True, text=True, timeout=180)
    os.remove(tmp)
    if r.returncode or not os.path.exists(pdf):
        print("PDF failed:", r.stderr[-800:]); sys.exit(2)
    print("wrote", os.path.relpath(pdf, ROOT), f"({os.path.getsize(pdf) // 1024} KB)")


if __name__ == "__main__":
    main()
