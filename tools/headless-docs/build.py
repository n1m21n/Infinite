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
@page { size: 8.5in 11in; margin: 0; }
:root { --bg:#f7f4ee; --card:#fdfcf9; --ink:#1f1d18; --mute:#73716b; --faint:#b5afa3; --accent:#c1552f;
        --chip:#f0ead9; --line:#e6e0d3; --green:#3d7a57; --amber:#c98f2e; --purple:#7b68a8; --blue:#3f6ab0; }
* { box-sizing: border-box; }
html, body { margin: 0; background: var(--bg); color: var(--ink);
  font-family: "Helvetica Neue", Helvetica, Arial, sans-serif; -webkit-print-color-adjust: exact; print-color-adjust: exact; }
table.w { width: 100%; border-collapse: collapse; }
table.w > tbody > tr > td, table.w > thead td, table.w > tfoot td { padding: 0 0.63in; }
.sp.t { height: 0.6in; } .sp.b { height: 0.8in; }
.mono { font-family: Menlo, "SF Mono", monospace; }
.hand { font-family: "Bradley Hand", "Bradley Hand ITC", cursive; font-weight: bold; }
.kick { font-size: 9.5px; letter-spacing: 2.6px; color: var(--accent); font-weight: bold; text-transform: uppercase; margin-top: 26px; }
h2 { font-size: 24px; margin: 6px 0 10px; letter-spacing: -0.3px; break-after: avoid; }
h3 { font-size: 15.5px; margin: 18px 0 8px; break-after: avoid; }
h3::before { content: ""; display: inline-block; width: 4px; height: 14px; background: var(--accent); border-radius: 2px; margin-right: 9px; vertical-align: -2px; }
p { font-size: 11.8px; line-height: 1.6; margin: 0 0 10px; }
ul, ol { font-size: 11.8px; line-height: 1.6; margin: 0 0 12px; padding-left: 20px; }
li { margin: 3px 0; } li::marker { color: var(--accent); }
code { font-family: Menlo, "SF Mono", monospace; font-size: 9.8px; background: var(--chip); border: 1px solid #e5dcc3; border-radius: 5px; padding: 1px 5px; }
pre { position: relative; background: var(--card); border: 1px solid #ece7dc; border-left: 4px solid var(--accent); border-radius: 9px;
  padding: 12px 16px; margin: 0 0 14px; break-inside: avoid; white-space: pre-wrap; }
pre code { background: none; border: 0; padding: 0; font-size: 9.6px; line-height: 1.55; color: #2b2922; }
table.t { width: 100%; border-collapse: separate; border-spacing: 0; margin: 0 0 14px; font-size: 10.6px; background: var(--card);
  border: 1px solid #ece7dc; border-radius: 9px; overflow: hidden; }
table.t tr { break-inside: avoid; }
table.t th { background: var(--chip); color: var(--mute); font-family: Menlo, monospace; font-size: 8.5px; letter-spacing: 1.2px;
  text-transform: uppercase; text-align: left; padding: 8px 12px; border-bottom: 1px solid #e5dcc3; }
table.t td { padding: 8px 12px; border-top: 1px dashed #e4dfd2; vertical-align: top; line-height: 1.5; }
table.t tr:first-child + tr td { border-top: 0; }
table.t td:first-child { color: #3a3830; }
.foot { position: fixed; left: 0; right: 0; bottom: 0.35in; margin: 0 0.63in; display: flex; justify-content: space-between;
  border-top: 1px solid #e2ddd2; padding-top: 7px; font-family: Menlo, monospace; font-size: 8px; letter-spacing: 1.2px; color: var(--faint); text-transform: uppercase; }
.page { break-after: page; }
.panel { background: var(--card); border: 1px solid #e9e3d6; border-radius: 14px; padding: 22px 26px; }
.pill { border: 1px solid #d9d3c4; border-radius: 20px; padding: 8px 14px; color: var(--mute); background: #fbf9f4; font-size: 10.5px; }
.pill b { color: var(--ink); margin-left: 4px; }
.dot { display: inline-block; width: 20px; height: 6px; border-radius: 3px; margin-right: 10px; vertical-align: middle; }
.toc { list-style: none; padding: 0; margin: 14px 0 0; }
.toc li { display: flex; align-items: baseline; border-bottom: 1px dotted #d9d3c4; padding: 9px 0; font-size: 12.2px; margin: 0; }
.toc .n { width: 30px; font-family: "Bradley Hand", cursive; color: var(--accent); font-style: italic; font-size: 13px; }
.toc .t { font-weight: bold; flex: 1; }
"""


def inline(t):
    t = html.escape(t, quote=False)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", t)
    return t


def convert(md, sections):
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
            t = '<table class="t"><tr>' + "".join(f"<th>{inline(c)}</th>" for c in rows[0]) + "</tr>"
            for r in rows[2:]:
                t += "<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>"
            out.append(t + "</table>")
        elif ln.startswith("## "):
            m = re.match(r"(\d+)\. (.*)", ln[3:])
            num, title = (int(m.group(1)), m.group(2)) if m else (0, ln[3:])
            sections.append((num, title))
            out.append(f'<div class="kick">Section {num:02d}</div><h2>{inline(title)}</h2>')
        elif ln.startswith("### "):
            out.append(f"<h3>{inline(ln[4:])}</h3>")
        elif re.match(r"(- |\d+\. )", ln):
            tag = "ol" if ln[0].isdigit() else "ul"
            items = []
            while i < len(lines) and re.match(r"(- |\d+\. )", lines[i]):
                items.append(re.sub(r"^(- |\d+\. )", "", lines[i]))
                i += 1
            i -= 1
            out.append(f"<{tag}>" + "".join(f"<li>{inline(x)}</li>" for x in items) + f"</{tag}>")
        elif ln.strip():
            keep = ' style="break-after:avoid"' if ln.rstrip().endswith(":") else ""
            out.append(f"<p{keep}>{inline(ln)}</p>")
        i += 1
    return "\n".join(out)


def page(body, footer=True):
    foot = '<div class="foot"><span>Headless Engine · User Guide</span><span>INFINITE</span></div>' if footer else ""
    return (f'<table class="w"><thead><tr><td><div class="sp t"></div></td></tr></thead>'
            f'<tfoot><tr><td><div class="sp b"></div></td></tr></tfoot><tbody><tr><td>{body}</td></tr></tbody></table>{foot}')


def main():
    md = open(SRC, encoding="utf-8").read()
    title, rest = md.split("\n", 1)
    intro, body = rest.split("\n## ", 1)
    sections = []
    main_html = convert("## " + body, sections)
    ver = subprocess.run([os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite"), "--version"],
                         capture_output=True, text=True).stdout.strip().splitlines()[-1:] or [""]
    icon = os.path.join(ROOT, "website", "assets", "icon-96.png")
    cover = f"""
<div class="page" style="padding:0.9in 0.63in 0; height:11in; position:relative">
  <div style="display:flex; gap:7px; margin-bottom:12px"><i style="width:9px;height:9px;border-radius:50%;background:#e0524a"></i><i style="width:9px;height:9px;border-radius:50%;background:#3aa655"></i><i style="width:9px;height:9px;border-radius:50%;background:#3f6ab0"></i></div>
  <div class="mono" style="font-size:9px;letter-spacing:2.5px;color:var(--mute)">INFINITE &nbsp;&middot;&nbsp; AUDIOVISUAL NODE WORKSTATION</div>
  <img src="file://{icon}" style="width:64px;height:64px;margin-top:40px;border-radius:14px">
  <div style="font-size:50px;font-weight:bold;letter-spacing:-1.5px;margin-top:24px;line-height:1">The Headless</div>
  <div class="hand" style="font-size:54px;color:var(--accent);font-style:italic;line-height:1.1;margin-bottom:22px">Engine</div>
  <p style="font-size:15px;line-height:1.55;color:#4a483f;max-width:5.1in">Render pictures, video and sound from a plain text file. No window, no mouse, same Infinite underneath. A friendly guide to the command line, the patch format, and the loop that gets you from idea to picture.</p>
  <div style="display:flex;flex-wrap:wrap;gap:10px;margin:22px 0 30px">
    <span class="pill">Runs on <b>macOS · Windows · Linux</b></span>
    <span class="pill">Version <b>{html.escape(ver[0])}</b></span>
    <span class="pill">Output <b>PNG · MP4 · MOV · WAV</b></span>
  </div>
  <div class="panel">
    <div class="hand" style="font-size:20px;font-style:italic">Five wires, five jobs</div>
    <div style="font-size:10.5px;color:var(--mute);margin:6px 0 16px">The same cable colours you see in the app, now written as one line each in a patch file.</div>
    <div style="display:grid;grid-template-columns:repeat(3,1fr);row-gap:12px;font-size:10.5px">
      <span><i class="dot" style="background:var(--accent)"></i><code>cable</code> image</span>
      <span><i class="dot" style="background:var(--green)"></i><code>geo</code> 3D shapes</span>
      <span><i class="dot" style="background:var(--amber)"></i><code>aud</code> sound</span>
      <span><i class="dot" style="background:var(--purple)"></i><code>note</code> musical events</span>
      <span><i class="dot" style="background:var(--blue)"></i><code>mod</code> a wiggling knob</span>
    </div>
  </div>
  <div style="position:absolute;left:0.63in;right:0.63in;bottom:0.6in;font-size:9.5px;color:var(--mute);display:flex;justify-content:space-between;align-items:flex-end">
    <span>Deterministic by design: the same patch gives the same frames and samples.</span>
    <span class="hand" style="font-size:16px;color:var(--green);font-style:italic">write it, run it ✎</span></div>
</div>"""
    toc = "".join(f'<li><span class="n">{n:02d}</span><span class="t">{inline(t)}</span></li>' for n, t in sections)
    front = f"""<div style="padding-top:0.1in"><div class="kick" style="margin-top:0">Before you start</div>
<h2 style="font-size:30px;margin:10px 0 18px">What is the headless engine?</h2>
<div class="panel">{convert(intro, [])}</div>
<div class="kick">Contents</div><h2 style="font-size:28px;margin:8px 0 0">{len(sections)} sections</h2><ul class="toc">{toc}</ul></div>"""
    doc = (f'<!doctype html><html><head><meta charset="utf-8"><style>{CSS}</style></head><body>{cover}'
           f'{page(front).replace("<table", "<div class=page><table", 1).replace("</table>", "</table></div>", 1)}'
           f'{page(main_html)}</body></html>')
    with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False, encoding="utf-8") as f:
        f.write(doc)
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", "--allow-file-access-from-files",
                    f"--print-to-pdf={OUT}", "file://" + f.name], check=True, capture_output=True)
    os.unlink(f.name)
    print("wrote", OUT)


main()
