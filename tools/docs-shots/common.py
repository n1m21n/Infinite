"""Shared paths, manifest and brand styling for the docs screenshot pipeline (see tools/docs-shots/README.md)."""
import json
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SHOTS = os.path.join(ROOT, "docs", "shots")
MANIFEST = os.path.join(SHOTS, "manifest.json")
FONTS = os.path.join(ROOT, "art", "brand", "fonts")
DEFAULT_APP = os.environ.get("INFINITE_APP") or os.path.expanduser(
    "~/Infinite/worktrees/v06-time-machine/build/Infinite.app")
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

BUILD_PROCS = ("cmake", "make", "clang", "clang++", "ninja", "cc1plus")


def build_running():
    """Names of compiler/build processes currently running (only one build may run on this Mac at a time)."""
    found = []
    for name in BUILD_PROCS:
        if subprocess.run(["pgrep", "-x", name], capture_output=True).returncode == 0:
            found.append(name)
    return found


def app_binary(app):
    b = os.path.join(app, "Contents", "MacOS", "Infinite")
    if not os.path.exists(b):
        raise SystemExit(f"no Infinite binary at {b} (pass --app or set INFINITE_APP)")
    return b


def app_version(app):
    try:
        out = subprocess.run([app_binary(app), "--version"], capture_output=True, text=True, timeout=30).stdout
        return out.strip().splitlines()[-1] if out.strip() else ""
    except Exception:
        return ""


def load_manifest():
    if os.path.exists(MANIFEST):
        return json.load(open(MANIFEST))
    return {"app": "", "shots": {}}


def save_manifest(m):
    os.makedirs(SHOTS, exist_ok=True)
    with open(MANIFEST, "w") as f:
        json.dump(m, f, indent=1, sort_keys=True)


def slug(s):
    return "".join(c if c.isalnum() else "_" for c in s).strip("_")


# Print layout values from docs/brand/PDF_GUIDELINES.md
PDF = {"page": os.environ.get("PDF_PAGE", "letter").lower(), "margin_x": "18mm", "margin_top": "18mm", "margin_bottom": "22mm"}


def house_text(html):
    """Brand voice: no em dashes."""
    return html.replace(" \u2014 ", ": ").replace("\u2014", ", ").replace("\u270e", "")


def brand_css():
    """Brand v2 Paper styling and print layout laid over the legacy builder selectors (see PDF_GUIDELINES.md)."""
    f = lambda n: f"file://{FONTS}/{n}"
    size = "210mm 297mm" if PDF["page"] == "a4" else "8.5in 11in"
    return f"""
@font-face {{ font-family: Geist; font-weight: 400 500; src: url("{f('Geist-Medium.ttf')}"); }}
@font-face {{ font-family: Geist; font-weight: 600; src: url("{f('Geist-SemiBold.ttf')}"); }}
@font-face {{ font-family: Geist; font-weight: 700 900; src: url("{f('Geist-ExtraBold.ttf')}"); }}
@font-face {{ font-family: 'Geist Mono'; font-weight: 400 700; src: url("{f('GeistMono-Medium.ttf')}"); }}
@font-face {{ font-family: Caveat; font-weight: 400 700; src: url("{f('Caveat-Bold.ttf')}"); }}
@page {{ size: {size}; margin: 0; }}
:root {{ --bg:#FBFAF6; --card:#FFFFFF; --deep:#EFF0F4; --ink:#12152A; --ink2:#404660; --mute:#5A6180; --faint:#5A6180; --accent:#D9430E;
        --chip:#EFF0F4; --line:#DAD8E6; --green:#4A54D6; --amber:#D9430E; --purple:#4A54D6; --blue:#4A54D6; }}
html, body {{ background: var(--bg); color: var(--ink); font: 400 10pt/16pt Geist, sans-serif; font-feature-settings: "tnum"; }}
table.w > tbody > tr > td, table.w > thead td, table.w > tfoot td, table.w td {{ padding: 0 {PDF['margin_x']}; }}
.sp.t {{ height: {PDF['margin_top']}; }} .sp.b {{ height: {PDF['margin_bottom']}; }}
.mono, code, pre, .tag, .chip, .foot, .kick, th {{ font-family: "Geist Mono", monospace; font-weight: 500; }}
.hand {{ font-family: Caveat, Geist, cursive; font-weight: 700; color: var(--accent); }}
h1 {{ font: 750 28pt/32pt Geist; letter-spacing: -0.035em; margin: 0 0 12pt; break-after: avoid; }}
h2 {{ font: 700 18pt/24pt Geist; letter-spacing: -0.025em; margin: 24pt 0 8pt; break-after: avoid; }}
h3 {{ font: 650 13pt/20pt Geist; letter-spacing: -0.015em; margin: 16pt 0 4pt; break-after: avoid; }}
h3::before {{ display: none !important; }}
p, .panel p {{ font: 400 10pt/16pt Geist; margin: 0 0 8pt; max-width: 34em; }}
.intro {{ font: 450 12pt/20pt Geist; color: var(--ink2); max-width: 34em; margin: 0 0 16pt; }}
ul, ol {{ font: 400 10pt/16pt Geist; margin: 0 0 8pt; padding-left: 16pt; }} li {{ margin: 0 0 4pt; }} li::marker {{ color: var(--accent); }}
.kick, .chap {{ font: 500 7pt/12pt "Geist Mono"; letter-spacing: 0.12em; text-transform: uppercase; color: var(--accent); font-style: normal; margin: 0 0 4pt; }}
.card, .panel, .atlas .cell {{ background: var(--card) !important; border: 0 !important; border-radius: 12pt !important; padding: 12pt 16pt !important; margin: 0 0 12pt !important; break-inside: avoid; }}
.card h2 {{ font: 650 13pt/20pt Geist; margin: 0 0 4pt; }}
.card p {{ font: 400 10pt/16pt Geist; }}
.tag, .foot {{ font: 500 7pt/12pt "Geist Mono"; letter-spacing: 0.12em; text-transform: uppercase; color: var(--mute); }}
.foot {{ bottom: 10mm; margin: 0 {PDF['margin_x']}; border-top: 0.5pt solid var(--line); padding-top: 4pt; }}
.try {{ border-top: 0.5pt solid var(--line) !important; padding-top: 8pt !important; font: 400 10pt/16pt Geist; color: var(--ink2); }}
.try b {{ font: 700 14pt/16pt Caveat, Geist; color: var(--accent); }}
.chip {{ background: var(--deep) !important; border: 0 !important; border-radius: 999px !important; padding: 2pt 8pt !important; font-size: 8pt; }}
code {{ font: 500 8.5pt/12pt "Geist Mono"; background: var(--deep) !important; border: 0 !important; border-radius: 2pt !important; padding: 0 3pt !important; }}
pre {{ background: var(--deep) !important; border: 0 !important; border-radius: 8pt !important; padding: 12pt !important; margin: 0 0 16pt !important; break-inside: avoid; }}
pre code {{ font: 500 8.5pt/12pt "Geist Mono"; background: none !important; padding: 0 !important; }}
table.t {{ background: none !important; border: 0 !important; border-radius: 0 !important; font: 400 9pt/14pt Geist; margin: 0 0 16pt !important; }}
table.t th {{ background: var(--deep) !important; color: var(--mute); font: 500 7pt/12pt "Geist Mono"; letter-spacing: 0.12em; padding: 6pt 8pt !important; border: 0 !important; }}
table.t td {{ padding: 8pt !important; border: 0 !important; border-top: 0.5pt solid var(--line) !important; }}
table.t tr {{ break-inside: avoid; }}
.toc li {{ border-bottom: 0.5pt dotted var(--line) !important; }} .toc .n {{ font: 700 14pt/16pt Caveat, Geist; color: var(--accent); }} .toc .t {{ font-weight: 650; }}
.shot {{ margin: 0 0 16pt; text-align: center; break-inside: avoid; }}
.shot img {{ max-width: 100%; max-height: 4.6in; border-radius: 8pt; }}
.shot figcaption {{ font: 500 8pt/12pt Geist; color: var(--ink2); margin-top: 4pt; }}
.atlas {{ display: grid; grid-template-columns: 1fr 1fr; gap: 12pt; }}
.atlas .cell {{ text-align: center; margin: 0 !important; background: var(--card); }}
.atlas .cell img {{ max-width: 100%; max-height: 3.3in; border-radius: 8pt; }}
.atlas .cell h4 {{ margin: 0 0 8pt; font: 650 13pt/20pt Geist; text-align: left; }}
.atlas .cell h4 span {{ font: 500 7pt/12pt "Geist Mono"; letter-spacing: 0.12em; color: var(--mute); margin-left: 8pt; text-transform: uppercase; }}
"""


def figure(shot_id, caption=None, manifest=None):
    """HTML <figure> for a manifest shot (the PDF-sized image); empty string when the shot was not captured."""
    m = manifest or load_manifest()
    s = m["shots"].get(shot_id)
    if not s or not s.get("pdf"):
        return ""
    cap = f"<figcaption>{caption}</figcaption>" if caption else ""
    return f'<figure class="shot"><img src="file://{os.path.join(ROOT, s["pdf"])}">{cap}</figure>'


def find_by_name(name, state="o", manifest=None):
    m = manifest or load_manifest()
    for sid, s in m["shots"].items():
        if s.get("kind") == "node" and s["name"] == name and s["state"] == state:
            return sid
    return None
