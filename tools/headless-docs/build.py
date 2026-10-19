#!/usr/bin/env python3
"""Build Infinite_Headless_User_Guide.pdf from docs/headless/GUIDE.md (headless Chrome prints the HTML).

    python3 tools/headless-docs/build.py
"""
import html
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "docs", "headless", "GUIDE.md")
OUT = os.path.join(ROOT, "docs", "headless", "Infinite_Headless_User_Guide.pdf")
sys.path.insert(0, os.path.join(ROOT, "tools", "brand"))
import paper_doc as P  # noqa: E402

CSS = P.CSS + P.PILL_CSS

inline, convert = P.inline, P.convert


def page(body, footer=True):
    return P.doc(body, "Headless Engine / User Guide" if footer else "", "INFINITE", P.PILL_CSS)


def main():
    md = open(SRC, encoding="utf-8").read()
    title, rest = md.split("\n", 1)
    intro, body = rest.split("\n## ", 1)
    sections = []
    main_html = convert("## " + body, sections)
    try:
        ver = subprocess.run([os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite"), "--version"],
                             capture_output=True, text=True).stdout.strip().splitlines()[-1:] or [""]
    except OSError:                       # no local build (cloud container): keep the version already printed in the guide
        ver = ["v0.6.0"]
    cover = P.cover("AUDIOVISUAL NODE WORKSTATION", "The Headless", "Engine", "", 
                    "Render pictures, video and sound from a plain text file. No window, no mouse, same Infinite underneath. "
                    "A friendly guide to the command line, the patch format, and the loop that gets you from idea to picture.",
                    [("Runs on", "macOS, Windows, Linux"), ("Version", ver[0]), ("Output", "PNG, MP4, MOV, WAV")], P.LOGO_TILE, art="patch")
    cables = "".join(f'<span><i style="display:inline-block;width:22px;height:6px;border-radius:3px;margin-right:9px;vertical-align:middle;background:{c}"></i><code>{k}</code> {n}</span>'
                     for (n, c), k in zip(P.CABLES, ["cable", "mod", "aud", "note", "pal"]))
    cover = cover.rstrip()[:-len("</div>")] + f"""<div class="panel" style="position:absolute;left:0;right:0;bottom:0">
      <div class="kick">Five cables, five jobs</div>
      <p style="color:var(--ink-3);margin:6px 0 12px">The same cable colours you see in the app, now written as one line each in a patch file.</p>
      <div style="display:grid;grid-template-columns:repeat(3,1fr);row-gap:10px;font-size:10px">{cables}</div></div></div>"""
    toc = "".join(f'<li><span class="n">{n:02d}</span><span class="t">{inline(t)}</span></li>' for n, t in sections)
    front = f"""<div style="padding-top:0.05in"><div class="kick">Before you start</div>
<h1>What is the headless engine?</h1>
<div class="panel">{convert(intro, [])}</div>
<div class="kick" style="margin-top:22px">Contents</div><h1 style="font-size:26px">{len(sections)} sections</h1><ul class="toc">{toc}</ul></div>"""
    tmp = tempfile.mkdtemp()
    P_ = P.doc(cover + front, "", "", P.PILL_CSS)
    parts = []
    for name, h in (("front", P_), ("main", page(main_html))):
        pdf = os.path.join(tmp, name + ".pdf")
        P.render(h, pdf, tmp)
        parts.append(pdf)
    subprocess.run(["pdfunite"] + parts + [OUT], check=True)
    print("wrote", OUT)


main()
