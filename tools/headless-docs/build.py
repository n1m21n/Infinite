#!/usr/bin/env python3
"""Build Infinite_Headless_User_Guide.pdf from docs/headless/GUIDE.md (headless Chrome prints the HTML).

    python3 tools/headless-docs/build.py
"""
import html
import os
import re
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "docs", "headless", "GUIDE.md")
OUT = os.path.join(ROOT, "docs", "headless", "Infinite_Headless_User_Guide.pdf")
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

CSS = """
@page { size: A4; margin: 18mm 16mm; }
body { font: 10.5pt/1.55 -apple-system, 'Helvetica Neue', Arial, sans-serif; color: #1d2330; }
h1 { font-size: 26pt; margin: 0 0 4pt; letter-spacing: -0.5pt; }
h2 { font-size: 16pt; margin: 22pt 0 6pt; padding-bottom: 3pt; border-bottom: 1.5pt solid #5b6ee1; break-after: avoid; }
h3 { font-size: 12pt; margin: 14pt 0 4pt; color: #3a4aa8; break-after: avoid; }
p { margin: 5pt 0; } li { margin: 2pt 0; }
code { font: 9pt 'SF Mono', Menlo, monospace; background: #eef0f7; padding: 0.5pt 3pt; border-radius: 3pt; }
pre { background: #161a26; color: #e6e9f5; padding: 8pt 10pt; border-radius: 6pt; break-inside: avoid; white-space: pre-wrap; }
pre code { background: none; color: inherit; padding: 0; font-size: 8.8pt; }
table { border-collapse: collapse; width: 100%; margin: 8pt 0; font-size: 9.3pt; break-inside: auto; }
tr { break-inside: avoid; }
th { background: #5b6ee1; color: #fff; text-align: left; padding: 4pt 6pt; }
td { padding: 4pt 6pt; border-bottom: 0.5pt solid #d5d9ea; vertical-align: top; }
tr:nth-child(even) td { background: #f6f7fc; }
.cover { text-align: left; padding-top: 70mm; break-after: page; }
.cover .sub { font-size: 14pt; color: #5b6ee1; margin-top: 6pt; }
.cover .meta { margin-top: 40mm; color: #6b7390; font-size: 10pt; }
"""


def inline(t):
    t = html.escape(t, quote=False)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", t)
    return t


def convert(md):
    out, lines, i = [], md.split("\n"), 0
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("```"):
            buf = []
            i += 1
            while not lines[i].startswith("```"):
                buf.append(lines[i])
                i += 1
            out.append("<pre><code>" + html.escape("\n".join(buf)) + "</code></pre>")
        elif ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            i -= 1
            head, body = rows[0], rows[2:]
            t = "<table><tr>" + "".join(f"<th>{inline(c)}</th>" for c in head) + "</tr>"
            for r in body:
                t += "<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>"
            out.append(t + "</table>")
        elif re.match(r"#{1,3} ", ln):
            n = len(ln) - len(ln.lstrip("#"))
            out.append(f"<h{n}>{inline(ln[n + 1:])}</h{n}>")
        elif re.match(r"(- |\d+\. )", ln):
            tag = "ol" if ln[0].isdigit() else "ul"
            items = []
            while i < len(lines) and re.match(r"(- |\d+\. )", lines[i]):
                items.append(re.sub(r"^(- |\d+\. )", "", lines[i]))
                i += 1
            i -= 1
            out.append(f"<{tag}>" + "".join(f"<li>{inline(x)}</li>" for x in items) + f"</{tag}>")
        elif ln.strip():
            out.append(f"<p>{inline(ln)}</p>")
        i += 1
    return "\n".join(out)


def main():
    md = open(SRC, encoding="utf-8").read()
    title, body = md.split("\n", 1)
    ver = subprocess.run([os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite"), "--version"],
                         capture_output=True, text=True).stdout.strip().splitlines()[-1:] or [""]
    cover = (f'<div class="cover"><h1>{inline(title[2:])}</h1>'
             f'<div class="sub">Pictures, video and sound from a text file</div>'
             f'<div class="meta">Infinite {ver[0]}</div></div>')
    doc = f"<!doctype html><meta charset=utf-8><style>{CSS}</style>{cover}{convert(body)}"
    with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False, encoding="utf-8") as f:
        f.write(doc)
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                    f"--print-to-pdf={OUT}", "file://" + f.name], check=True, capture_output=True)
    os.unlink(f.name)
    print("wrote", OUT)


main()
