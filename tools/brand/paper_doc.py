"""Shared Paper-mode print style for Infinite's PDF documents (BRAND.md sections 2 and 3).

Used by tools/manual/build.py, tools/headless-docs/build.py and tools/manual/field/build_field.py, so the
three manuals look like one family: Paper ground, cool inks, Geist and Geist Mono, one Caveat word per
section in Ember, Signal for structure. Fonts come from art/brand/fonts/ (OFL).
"""
import html
import os
import re
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONTS = os.path.join(ROOT, "art", "brand", "fonts")
LOGO_TILE = os.path.join(ROOT, "docs", "brand", "img", "logo_tile.webp")
ART = os.path.join(ROOT, "website", "assets", "3d")


def _chrome():
    """INFINITE_CHROME, then Google Chrome on macOS, then any Chromium (Playwright's, or on PATH)."""
    import glob
    import shutil
    for c in [os.environ.get("INFINITE_CHROME", ""), "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"] + sorted(glob.glob("/opt/pw-browsers/chromium-*/chrome-linux/chrome")):
        if c and os.path.exists(c):
            return c
    return shutil.which("chromium") or shutil.which("google-chrome") or "chrome"


CHROME = _chrome()

# Cable colours are the app's default light theme (src/core/CategoryColors.cpp, DefaultCableColor).
CABLES = [("Image", "#373E4E"), ("Modulator", "#D7780A"), ("Audio", "#1E64E6"), ("Notes", "#14963C"), ("Palette", "#AA28B4")]


def esc(s):
    return html.escape(s, quote=False)


def _face(family, weight, fname):
    return (f'@font-face {{ font-family:"{family}"; font-weight:{weight}; '
            f'src:url("file://{os.path.join(FONTS, fname)}"); }}\n')


CSS = (
    _face("Geist", 500, "Geist-Medium.ttf") + _face("Geist", 400, "Geist-Medium.ttf")
    + _face("Geist", 650, "Geist-SemiBold.ttf") + _face("Geist", 600, "Geist-SemiBold.ttf")
    + _face("Geist", 800, "Geist-ExtraBold.ttf") + _face("Geist", 700, "Geist-ExtraBold.ttf")
    + _face("Geist Mono", 500, "GeistMono-Medium.ttf") + _face("Geist Mono", 400, "GeistMono-Medium.ttf")
    + _face("Caveat", 700, "Caveat-Bold.ttf")
    + """
@page { size: 8.5in 11in; margin: 0; }
:root { --lift: none; --well: #F1F2F8; --ground:#FBFAF6; --deep:#EFF0F4; --surface:#F6F7FB; --surface-3:#F5F5F8; --line:#DAD8E6;
        --ink:#12152A; --ink-2:#404660; --ink-3:#5A6180; --signal:#4A54D6; --accent:#D9430E; --accent-ink:#B53700; }
* { box-sizing: border-box; }
html, body { margin: 0; background: var(--ground); color: var(--ink);
  font-family: "Geist", system-ui, sans-serif; font-weight: 500; -webkit-print-color-adjust: exact; print-color-adjust: exact; }
table.w { width: 100%; border-collapse: collapse; }
table.w > tbody > tr > td, table.w > thead td, table.w > tfoot td { padding: 0 0.7in; }
.sp.t { height: 0.62in; } .sp.b { height: 0.85in; }
.mono { font-family: "Geist Mono", ui-monospace, monospace; }
.hand { font-family: "Caveat", cursive; font-weight: 700; color: var(--accent-ink); }
.folio { font-family: "Geist Mono", monospace; font-size: 8.5px; letter-spacing: 0.12em; text-transform: uppercase; color: var(--ink-3); }
.kick { font-family: "Geist Mono", monospace; font-size: 9px; letter-spacing: 0.12em; color: var(--signal); text-transform: uppercase; }
.chap { display: flex; align-items: center; gap: 10px; }
.chap::before { content: ""; width: 28px; height: 4px; border-radius: 4px; background: var(--signal); }
h1 { font-weight: 750; font-size: 32px; line-height: 1.06; letter-spacing: -0.035em; margin: 6px 0 12px; break-after: avoid; }
h2 { font-weight: 700; font-size: 22px; line-height: 1.1; letter-spacing: -0.025em; margin: 22px 0 8px; break-after: avoid; }
h3 { font-weight: 650; font-size: 14.5px; line-height: 1.2; letter-spacing: -0.015em; margin: 16px 0 6px; break-after: avoid; }
p { font-size: 11.4px; line-height: 1.55; margin: 0 0 9px; color: var(--ink); }
.lead { font-size: 13.5px; line-height: 1.45; color: var(--ink-2); max-width: 6.1in; margin: 0 0 18px; }
ul, ol { font-size: 11.4px; line-height: 1.55; margin: 0 0 10px; padding-left: 19px; }
li { margin: 2px 0; } li::marker { color: var(--signal); }
code { font-family: "Geist Mono", monospace; font-size: 9.6px; background: var(--deep); border-radius: 4px; padding: 1px 5px; }
pre { background: #151930; color: #EEF1FA; border-radius: 14px; padding: 14px 18px; margin: 0 0 12px; break-inside: avoid; white-space: pre-wrap; }
pre code { background: none; padding: 0; font-size: 9.3px; line-height: 1.6; color: #EEF1FA; }
table.t { width: 100%; border-collapse: separate; border-spacing: 0; margin: 0 0 14px; font-size: 10.3px; background: var(--surface);
  box-shadow: var(--lift); border-radius: 14px; overflow: hidden; }
table.t th { text-align: left; font-family: "Geist Mono", monospace; font-size: 8.5px; letter-spacing: 0.1em; text-transform: uppercase;
  color: var(--signal); background: #E1E7FF; padding: 8px 10px; font-weight: 500; }
table.t td { padding: 6px 10px; border-top: 1px solid var(--line); vertical-align: top; line-height: 1.45; }
table.t tr { break-inside: avoid; }
.card { display: flex; gap: 18px; align-items: flex-start; background: var(--surface); box-shadow: var(--lift); border-radius: 20px;
  padding: 18px 20px; margin: 0 0 12px; break-inside: avoid; }
.card .txt { flex: 1; min-width: 0; }
.card .top { display: flex; justify-content: space-between; align-items: baseline; gap: 10px; }
.card h2 { font-size: 15.5px; margin: 0 0 7px; letter-spacing: -0.015em; }
.tag { font-family: "Geist Mono", monospace; font-size: 8px; letter-spacing: 0.1em; color: var(--ink-3); text-transform: uppercase; white-space: nowrap; }
.card p { font-size: 11px; line-height: 1.5; margin: 0 0 9px; }
.try { border-top: 1px solid var(--line); padding-top: 8px; font-size: 10.6px; line-height: 1.45; color: var(--ink-2); }
.try b { font-family: "Geist Mono", monospace; font-size: 8px; letter-spacing: 0.1em; text-transform: uppercase; color: var(--signal); font-weight: 500; margin-right: 6px; }
.chain { margin-top: 8px; display: flex; flex-wrap: wrap; align-items: center; gap: 5px; }
.chip { font-family: "Geist Mono", monospace; font-size: 8.8px; color: #3A3E9D; background: #E1E7FF; border-radius: 99px; padding: 3px 9px; }
.arrow { color: var(--ink-3); font-size: 10px; }
.shot { flex: none; background: #E6E8F2; border-radius: 12px; padding: 10px; display: flex; flex-direction: column; align-items: center; gap: 8px; }
.shot img { display: block; image-rendering: auto; }
.cap { font-family: "Geist Mono", monospace; font-size: 7.5px; letter-spacing: 0.08em; text-transform: uppercase; color: var(--ink-3); text-align: center; }
.row { display: flex; gap: 14px; align-items: flex-start; padding: 10px 0; border-top: 1px solid var(--line); break-inside: avoid; }
.row .shot { padding: 6px; border-radius: 9px; }
.row h3 { margin: 0 0 3px; font-size: 12.5px; }
.row p { margin: 0; font-size: 10.6px; color: var(--ink-2); }
.panel { background: var(--surface); box-shadow: var(--lift); border-radius: 20px; padding: 22px 24px; }
.panel p:last-child { margin: 0; }
.toc { list-style: none; padding: 0; margin: 10px 0 0; }
.toc li { break-inside: avoid; display: flex; align-items: baseline; border-bottom: 1px solid var(--line); padding: 7px 0; font-size: 11.4px; margin: 0; }
.toc .n { width: 30px; font-family: "Geist Mono", monospace; font-size: 10px; color: var(--signal); }
.toc .t { font-weight: 650; flex: 1; }
.toc .d { color: var(--ink-3); font-size: 10px; max-width: 3.3in; text-align: right; }
.foot { position: fixed; left: 0; right: 0; bottom: 0.38in; margin: 0 0.7in; display: flex; justify-content: space-between;
  border-top: 1px solid var(--line); padding-top: 7px; }
.page { break-after: page; }
.grid { display: flex; flex-wrap: wrap; align-items: flex-start; gap: 8px; margin: 6px 0 14px; }
.cell { background: #E6E8F2; border-radius: 9px; padding: 7px 7px 6px; display: flex; flex-direction: column; align-items: center; justify-content: flex-start; gap: 6px; break-inside: avoid; min-width: 58px; max-width: 150px; }
.cell img { display: block; }
.cell .nm { font-size: 8.8px; font-weight: 600; text-align: center; line-height: 1.2; color: var(--ink); }
.fig { display: inline-block; vertical-align: top; margin: 0 10px 14px 0; break-inside: avoid; }
.fig .shot { padding: 12px; }
.fig figcaption { font-size: 10px; color: var(--ink-2); margin-top: 6px; line-height: 1.4; }
"""
)


def doc(body, footer_left, footer_right, extra_css=""):
    foot = (f'<div class="foot folio"><span>{esc(footer_left)}</span><span>{esc(footer_right)}</span></div>'
            if footer_left else "")
    return (f'<!doctype html><html data-mode="paper"><head><meta charset="utf-8"><style>{CSS}{extra_css}</style></head><body>'
            '<table class="w"><thead><tr><td><div class="sp t"></div></td></tr></thead>'
            '<tfoot><tr><td><div class="sp b"></div></td></tr></tfoot><tbody><tr><td>'
            f'{body}</td></tr></tbody></table>{foot}</body></html>')


def render(html_text, out_pdf, workdir):
    src = os.path.join(workdir, os.path.basename(out_pdf) + ".html")
    with open(src, "w", encoding="utf-8") as f:
        f.write(html_text)
    subprocess.run([CHROME, "--headless=new", "--disable-gpu"] + (["--no-sandbox"] if hasattr(os, "geteuid") and os.geteuid() == 0 else []) + [ "--no-pdf-header-footer", "--virtual-time-budget=4000",
                    "--allow-file-access-from-files", f"--print-to-pdf={out_pdf}", "file://" + src],
                   check=True, capture_output=True, timeout=300)


def cover(kicker, title_a, hand_word, title_b, lead, pills, logo_png=None, art="node_hero"):
    """Full-bleed Midnight cover. One Caveat word in Ember is the look-here; the 3D render is the instrument.
    The caller may append a panel before the final </div>: it sits at the bottom of the page as a white card."""
    logo = f'<img src="file://{logo_png}" style="width:54px;height:54px;border-radius:12px">' if logo_png else ""
    pill_html = "".join(f'<span class="pill">{esc(k)} <b>{esc(v)}</b></span>' for k, v in pills)
    img = os.path.join(ART, art + ".webp")
    art_html = f'<img src="file://{img}" style="position:absolute;right:-0.15in;top:3.1in;width:5.6in;filter:drop-shadow(0 26px 30px rgba(4,5,14,.6))">' if os.path.exists(img) else ""
    return f"""
<div class="page" style="height:9.45in; position:relative">
  <div style="position:absolute; left:-0.7in; top:-0.62in; width:8.5in; height:11in; background:#151930; overflow:hidden; color:#EEF1FA">
    <div style="position:absolute;left:0;top:0;right:0;bottom:0;background-image:radial-gradient(rgba(167,177,255,.06) 1px, transparent 1.2px);background-size:22px 22px"></div>
    <div style="position:absolute; left:0.7in; top:0.62in; display:flex; align-items:center; gap:14px">{logo}<div class="folio" style="color:#A7B1FF">INFINITE &nbsp;/&nbsp; {esc(kicker)}</div></div>
    {art_html}
    <div style="position:absolute; left:0.7in; top:1.5in; width:6.4in">
      <div style="font-weight:800; font-size:60px; letter-spacing:-0.04em; line-height:0.98">{esc(title_a)}</div>
      <div style="display:flex; align-items:baseline; gap:16px; flex-wrap:wrap">
        <span class="hand" style="font-size:92px; line-height:1; color:#FF6B35">{esc(hand_word)}</span>
        <span style="font-weight:800; font-size:60px; letter-spacing:-0.04em; line-height:0.98">{esc(title_b)}</span></div>
      <p class="lead" style="margin-top:26px; max-width:4.4in; color:#C9CFE8">{esc(lead)}</p>
      <div style="display:flex; flex-wrap:wrap; gap:8px; margin:8px 0 0; max-width:5.2in">{pill_html}</div>
    </div>
  </div>
</div>"""


PILL_CSS = (".pill{font-family:'Geist Mono',monospace;font-size:8.8px;letter-spacing:.06em;color:#A7B1FF;"
            "border-radius:99px;padding:7px 13px;background:#20263F} .pill b{color:#EEF1FA;font-weight:500;margin-left:4px}")


def inline(t):
    t = html.escape(t, quote=False)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", t)
    return t


def figure(caption, ref):
    """![caption](shot:Node Name) or ![caption](path/from/repo/root.png): a screenshot in a Paper well."""
    from PIL import Image
    if ref.startswith("shot:"):
        path = os.path.join(ROOT, "docs", "node_screenshots", ref[5:] + ".png")
    else:
        path = os.path.join(ROOT, ref)
    with Image.open(path) as im:
        w, h = im.size
    k = min(1.0, 220 / (w / 2), 330 / (h / 2)) * 0.5
    return (f'<figure class="fig"><div class="shot"><img src="file://{path}" width="{round(w * k)}" height="{round(h * k)}"></div>'
            f'<figcaption>{html.escape(caption, quote=False)}</figcaption></figure>')


def convert(md, sections, section_label="Section"):
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
        elif ln.startswith("!["):
            m = re.match(r"!\[(.*?)\]\((.*?)\)", ln)
            out.append(figure(m.group(1), m.group(2)))
        elif ln.startswith("## "):
            m = re.match(r"(\d+)\. (.*)", ln[3:])
            num, title = (int(m.group(1)), m.group(2)) if m else (0, ln[3:])
            sections.append((num, title))
            out.append(f'<div class="kick chap">{section_label} {num:02d}</div><h2>{inline(title)}</h2>')
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


