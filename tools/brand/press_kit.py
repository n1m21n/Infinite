"""Press kit: one zip with everything a journalist, streamer or partner needs, built from the brand sources.

What: (re)builds the logo files (build_logo.py) and the fact sheet (templates.py) when missing or --fresh, then zips
    logo/        svg/ png/ favicon/ (every mark, tile and lockup)
    images/      3D renders and transparent cutouts (art/brand/3d/renders), channel cards (art/brand/channels/<set>)
    screenshots/ app screenshots (docs/brand/img/screenshots, made per the BRAND.md screenshot standard)
    fact-sheet.pdf, README.txt (what is inside, name and logo rules, links)
into dist/press/Infinite-press-kit-<version>.zip. The version is the latest gh release (git tag fallback).
Why: press requests arrived one file at a time; the kit is one link per release and always matches the brand book.

Usage:
    python3 tools/brand/press_kit.py [--channels a-daw-for-you] [--fresh] [--out dist/press]
Exit codes: 0 built, 1 a required input is missing or a build step failed, 2 usage error.
"""
import argparse
import glob
import os
import subprocess
import sys
import zipfile

sys.path.insert(0, os.path.dirname(__file__))
from templates import facts  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
LOGO = os.path.join(ROOT, "art", "brand", "logo")
RENDERS = os.path.join(ROOT, "art", "brand", "3d", "renders")
CHANNELS = os.path.join(ROOT, "art", "brand", "channels")
SHOTS = os.path.join(ROOT, "docs", "brand", "img", "screenshots")
SHEET = os.path.join(ROOT, "docs", "brand", "templates", "fact-sheet.pdf")


def step(cmd):
    print("  $ " + " ".join(os.path.relpath(c, ROOT) if os.path.isabs(c) else c for c in cmd))
    return subprocess.run(cmd, cwd=ROOT).returncode == 0


def readme(f, counts):
    return f"""Infinite press kit, {f['version']}

{f['name']} is a live instrument and audio-visual workstation: visuals, music and code on one canvas, with modulation
that reaches every control. {f['price']} and open source ({f['licence']}). Runs on {f['platforms']}.

Inside
  logo/          {counts['logo']} files. svg/ is the master; png/ for documents; favicon/ for the web.
                 mark = the lemniscate; tile = app icon; lockup-on-dark / lockup-on-light = mark + wordmark.
  images/        {counts['images']} files. *_cutout.png have transparent backgrounds.
  screenshots/   {counts['screenshots']} files, taken at 2x on the default theme.
  fact-sheet.pdf one page of facts.

Please
  - Write the name as "Infinite": one word, capital I.
  - Use the logo files as they are: no recolouring, outlines, shadows or gradients. Clear space = 4 stroke widths.
  - Credit screenshots and renders as "Infinite".

Links
  Website    {f['site']}
  Source     {f['repo']}
  Community  {f['discord']}
"""


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--channels", default="a-daw-for-you")
    ap.add_argument("--fresh", action="store_true")
    ap.add_argument("--out", default=os.path.join(ROOT, "dist", "press"))
    ap.add_argument("-h", "--help", action="store_true")
    a, rest = ap.parse_known_args()
    if a.help or rest:
        print(__doc__)
        return 2
    py = sys.executable
    if a.fresh or not os.path.exists(os.path.join(LOGO, "svg", "mark.svg")):
        if not step([py, os.path.join(HERE, "build_logo.py")]):
            return 1
    if a.fresh or not os.path.exists(SHEET):
        if not step([py, os.path.join(HERE, "templates.py"), "--only", "fact-sheet"]):
            return 1
    f = facts()
    files = []  # (source, path in zip)
    for sub in ("svg", "png", "favicon"):
        files += [(p, f"logo/{sub}/{os.path.basename(p)}") for p in sorted(glob.glob(os.path.join(LOGO, sub, "*")))]
    files += [(p, f"images/{os.path.basename(p)}") for p in sorted(glob.glob(os.path.join(RENDERS, "*.png")))]
    ch = os.path.join(CHANNELS, a.channels)
    files += [(p, f"images/cards/{os.path.basename(p)}") for p in sorted(glob.glob(os.path.join(ch, "*.png")))]
    files += [(p, f"screenshots/{os.path.basename(p)}") for p in sorted(glob.glob(os.path.join(SHOTS, "*.png")))]
    if not any(z.startswith("logo/svg/") for _, z in files) or not os.path.exists(SHEET):
        print("missing logo svgs or the fact sheet")
        return 1
    counts = {k: sum(1 for _, z in files if z.startswith(k + "/")) for k in ("logo", "images", "screenshots")}
    os.makedirs(a.out, exist_ok=True)
    zp = os.path.join(a.out, f"Infinite-press-kit-{f['version']}.zip")
    root = f"Infinite-press-kit-{f['version']}/"
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
        for src, dst in files:
            z.write(src, root + dst)
        z.write(SHEET, root + "fact-sheet.pdf")
        z.writestr(root + "README.txt", readme(f, counts))
    mb = os.path.getsize(zp) / 1e6
    print(f"press kit: {os.path.relpath(zp, ROOT)}  {len(files) + 2} files, {mb:.1f} MB "
          f"(logo {counts['logo']}, images {counts['images']}, screenshots {counts['screenshots']})")
    if not counts["screenshots"]:
        print("  note: no screenshots yet; see BRAND.md 'Screenshot standard'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
