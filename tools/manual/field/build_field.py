#!/usr/bin/env python3
"""Build Field_Language_Manual.pdf in the Paper brand style from docs/field/MANUAL.md.

    python3 tools/manual/field/build_field.py        # writes the three shipped copies

The v1 manual's HTML source was never committed; docs/field/MANUAL.md is its retyped source, with the post-v1
language additions (formerly an addendum) folded in as Chapter 13. Screenshots come from docs/node_screenshots/.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(ROOT, "tools", "brand"))
import paper_doc as P  # noqa: E402

SRC = os.path.join(ROOT, "docs", "field", "MANUAL.md")
FOOT_R = "INFINITE / FIELD LANGUAGE MANUAL"


def main():
    md = open(SRC, encoding="utf-8").read()
    _, rest = md.split("\n", 1)
    intro, body = rest.split("\n## ", 1)
    sections = []
    main_html = P.convert("## " + body, sections, "Chapter")
    cover = P.cover("AUDIOVISUAL NODE WORKSTATION", "The", "Field", "Language Manual",
                    "Everything you need to write real Field kernels and build real devices: the domains, the syntax, "
                    "the declarations, and the six node types that host them.",
                    [("Covers", "Field v1 and v1.1"), ("Style", "direct, example-driven"),
                     ("Audience", "device builders and AI assistants")], P.LOGO_TILE, art="node_field_pixel")
    cover = cover.rstrip()[:-len("</div>")] + """<div class="panel" style="position:absolute;left:0;right:0;bottom:0">
      <div class="kick">One primitive, five domains</div>
      <p style="color:var(--ink-2);margin:6px 0 10px">A Field kernel is a short body of code, run once per element of a domain. Whether it becomes a shader, a geometry deformer or a DSP effect falls out of which identifiers it touches and which node hosts it.</p>
      <div style="display:grid;grid-template-columns:repeat(5,1fr);font-size:10px;gap:6px;font-family:'Geist Mono',monospace"><span>graph</span><span>frame</span><span>element</span><span>pixel</span><span>sample</span></div></div></div>"""
    toc = "".join(f'<li><span class="n">{n:02d}</span><span class="t">{P.inline(t)}</span></li>' for n, t in sections)
    front = f"""<div style="padding-top:0.05in"><div class="kick">Before you start</div><h1>What Field is</h1>
<div class="panel">{P.convert(intro, [])}</div>
<div class="kick" style="margin-top:22px">Contents</div><h1 style="font-size:26px">{len(sections)} chapters</h1><ul class="toc">{toc}</ul></div>"""
    tmp = tempfile.mkdtemp()
    parts = []
    for name, h in (("front", P.doc(cover + front, "", "", P.PILL_CSS)),
                    ("main", P.doc(main_html, "Field Language Manual", FOOT_R, P.PILL_CSS + ".chap{break-before:page;margin-top:0} .chap:first-child{break-before:auto}"))):
        pdf = os.path.join(tmp, name + ".pdf")
        P.render(h, pdf, tmp)
        parts.append(pdf)
    out = os.path.join(tmp, "out.pdf")
    subprocess.run(["pdfunite"] + parts + [out], check=True)
    for d in ("", "website/assets/", "press_kit/manuals/"):
        shutil.copy(out, os.path.join(ROOT, d, "Field_Language_Manual.pdf"))
    print("wrote Field_Language_Manual.pdf (3 copies)")


if __name__ == "__main__":
    main()
