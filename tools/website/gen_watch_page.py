#!/usr/bin/env python3
"""Builds website/watch.html, the Apple Watch setup guide, from apps/watch/README.md (the single source).

What: README.md is converted to HTML (headings, ordered lists, tables, code spans, bold, italic) and wrapped in
      the site chrome taken from website/extensions.html, the same way gen_benchmarks.py does. The in-app
      "Set up watch app" window and this page tell the same story because they share that one text.
Screenshots: put website/assets/watch/step-1.png ... step-5.png (any that exist) in; they are shown under the
      matching numbered Install step.
Usage: python3 tools/website/gen_watch_page.py   then   python3 tools/website/gen_seo.py
"""
import html
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W = os.path.join(ROOT, "website")
BASE = "https://infinitedevices.org/"
TITLE = "Infinite Remote: your Apple Watch as a controller"
DESC = "Build the Infinite Remote watch app yourself with a free Apple ID and send your wrist motion, crown and touch pad to Infinite on your Mac."

CSS = """
.watch-doc{max-width:760px;margin:0 auto}
.watch-doc h2{margin:32px 0 10px}
.watch-doc ol,.watch-doc ul{padding-left:1.3em;margin:10px 0}
.watch-doc li{margin:8px 0}
.watch-doc figure{margin:10px 0 14px}
.watch-doc figure img{max-width:100%;border-radius:12px}
.watch-doc code{font-family:'Geist Mono',monospace;font-size:.92em;background:rgba(127,127,160,.16);padding:1px 6px;border-radius:6px}
.watch-doc .tbl{overflow-x:auto}
.watch-doc table{border-collapse:collapse;width:100%;font-size:.95em}
.watch-doc th,.watch-doc td{text-align:left;vertical-align:top;padding:8px 10px;border-bottom:1px solid rgba(127,127,160,.25)}
"""


def inline(t):
    t = html.escape(t, quote=False)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", t)
    t = re.sub(r"\*([^*]+)\*", r"<em>\1</em>", t)
    return t


def figure(n):
    rel = f"assets/watch/step-{n}.png"
    if os.path.exists(os.path.join(W, rel)):
        return f'<figure><img src="{rel}" alt="Step {n}" loading="lazy"></figure>'
    return ""


def convert(md):
    out, lines, i = [], md.splitlines(), 0
    in_install = False
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("# "):
            i += 1
            continue                                   # the page has its own h1
        if ln.startswith("## "):
            in_install = ln[3:].strip() == "Install"
            out.append(f"<h2>{inline(ln[3:])}</h2>")
        elif re.match(r"\d+\. ", ln):
            out.append("<ol>")
            while i < len(lines) and re.match(r"\d+\. ", lines[i]):
                n = int(lines[i].split(".")[0])
                text = re.sub(r"^\d+\. ", "", lines[i])
                fig = figure(n) if in_install else ""
                out.append(f"<li>{inline(text)}{fig}</li>")
                i += 1
            out.append("</ol>")
            continue
        elif ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            head, body = rows[0], [r for r in rows[2:]]
            t = "<div class=\"tbl\"><table><thead><tr>" + "".join(f"<th>{inline(c)}</th>" for c in head) + "</tr></thead><tbody>"
            for r in body:
                t += "<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>"
            out.append(t + "</tbody></table></div>")
            continue
        elif ln.strip():
            out.append(f"<p>{inline(ln)}</p>")
        i += 1
    return "\n".join(out)


def main():
    md = open(os.path.join(ROOT, "apps", "watch", "README.md")).read()
    tpl = open(os.path.join(W, "extensions.html")).read()
    head, rest = tpl.split("</head>", 1)
    head = re.sub(r"<title>.*?</title>", f"<title>{TITLE}</title>", head, flags=re.S)
    head = re.sub(r'(<meta name="description" content=")[^"]*', lambda m: m.group(1) + DESC, head)
    head = head.replace("https://infinitedevices.org/extensions.html", BASE + "watch.html")
    head = re.sub(r'(<meta (?:property="og:title"|name="twitter:title") content=")[^"]*', lambda m: m.group(1) + "Infinite Remote", head)
    head = re.sub(r'(<meta (?:property="og:description"|name="twitter:description") content=")[^"]*', lambda m: m.group(1) + DESC, head)
    head = re.sub(r'(og:image" content=")[^"]*', lambda m: m.group(1) + BASE + "assets/og-image.jpg", head)
    head = re.sub(r'(twitter:image" content=")[^"]*', lambda m: m.group(1) + BASE + "assets/og-image.jpg", head)
    head = head.replace('  <link rel="alternate" type="application/json" href="assets/extensions/extensions.json" title="Extensions">\n', "")
    head = re.sub(r'  <script type="application/ld\+json">.*?</script>\n', "", head, flags=re.S)
    head += f"  <style>{CSS}</style>\n"
    nav_start = rest.index("<body")
    header = rest[nav_start:rest.index("<main>")]
    header = header.replace('class="extensions-page"', 'class="watch-page"').replace(' aria-current="page"', "")
    main_html = (f'<main>\n    <section class="section lib-hero">\n      <div class="container"><div class="watch-doc">\n'
                 f'        <h1 class="slab">Infinite Remote</h1>\n{convert(md)}\n      </div></div>\n    </section>\n  </main>\n')
    tail = rest[rest.index("</main>") + len("</main>"):]
    open(os.path.join(W, "watch.html"), "w").write(head + "</head>" + header + main_html + tail)
    print("watch.html written")


main()
