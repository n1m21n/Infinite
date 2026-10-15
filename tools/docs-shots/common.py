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


# Brand v2 Paper mode (docs/brand/brand.json) laid over the legacy manual variable names, plus Geist from art/brand/fonts.
def brand_css():
    f = lambda n: f"file://{FONTS}/{n}"
    return f"""
@font-face {{ font-family: Geist; font-weight: 400 500; src: url("{f('Geist-Medium.ttf')}"); }}
@font-face {{ font-family: Geist; font-weight: 600; src: url("{f('Geist-SemiBold.ttf')}"); }}
@font-face {{ font-family: Geist; font-weight: 700 900; src: url("{f('Geist-ExtraBold.ttf')}"); }}
@font-face {{ font-family: 'Geist Mono'; font-weight: 400 700; src: url("{f('GeistMono-Medium.ttf')}"); }}
@font-face {{ font-family: Caveat; font-weight: 400 700; src: url("{f('Caveat-Bold.ttf')}"); }}
:root {{ --bg:#FBFAF6; --card:#FFFFFF; --ink:#12152A; --mute:#5A6180; --faint:#8a90a8; --accent:#D9430E; --chip:#EFF0F4;
        --line:#DAD8E6; --green:#3d7a57; --amber:#c98f2e; --purple:#7b68a8; --blue:#4A54D6; }}
html, body {{ background: var(--bg); font-family: Geist, "Helvetica Neue", Helvetica, sans-serif; }}
.mono, code, pre, .tag, .chip, .foot, .kick {{ font-family: "Geist Mono", Menlo, monospace; }}
.hand {{ font-family: Caveat, "Bradley Hand", cursive; }}
.card {{ background: var(--bg) !important; border-color: var(--line) !important; border-radius: 16px !important; }}
.chip {{ border-color: var(--line) !important; border-radius: 999px !important; }}
.shot {{ margin: 0 0 12px; text-align: center; break-inside: avoid; }}
.shot img {{ max-width: 100%; max-height: 4.6in; }}
.shot figcaption {{ font-size: 9.5px; color: var(--mute); margin-top: 4px; }}
.atlas {{ display: grid; grid-template-columns: 1fr 1fr; gap: 14px; }}
.atlas .cell {{ break-inside: avoid; background: var(--bg); border: 1px solid var(--line); border-radius: 16px; padding: 10px 12px 12px; text-align: center; }}
.atlas .cell img {{ max-width: 100%; max-height: 3.3in; }}
.atlas .cell h4 {{ margin: 0 0 6px; font-size: 12.5px; text-align: left; }}
.atlas .cell h4 span {{ font-family: "Geist Mono", monospace; font-size: 8px; letter-spacing: 1px; color: var(--faint); margin-left: 6px; text-transform: uppercase; }}
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
