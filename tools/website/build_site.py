#!/usr/bin/env python3
"""Build the deployable site: website/ -> _site/ with the speed work applied. The sources in website/ stay readable.

What it does, in order:
  1. copies website/ to the output folder
  2. joins each page's local stylesheets into one minified bundle-<hash>.css (one request, not a dozen)
  3. loads the Google Fonts sheet without blocking first paint
  4. minifies every .js file with esbuild (npm i esbuild; skipped with a note if missing)
Usage: python3 tools/website/build_site.py [out_dir]     (default _site; the Pages workflow runs this)"""
import hashlib, os, re, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "website"
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "_site"

def min_css(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"\s+", " ", s)
    s = re.sub(r" ?([{};,>]) ?", r"\1", s)
    return s.replace(";}", "}").strip()

LINK = re.compile(r'<link rel="stylesheet" href="((?:\.\./)*)([A-Za-z0-9_.-]+\.css)(?:\?v=[0-9a-f]+)?">\s*')
FONT = re.compile(r'<link href="(https://fonts\.googleapis\.com/css2[^"]+)" rel="stylesheet">')

def bundle_page(html, out):
    head_end = html.find("</head>")
    head, rest = html[:head_end], html[head_end:]
    first_style = head.find("<style")
    cut = first_style if first_style != -1 and LINK.search(head[first_style:]) is None else len(head)
    zone, tail = head[:cut], head[cut:]
    found = list(LINK.finditer(zone))
    if len(found) < 2: return html
    prefix = found[0].group(1)
    names = [m.group(2) for m in found]
    css = "".join(min_css((out / n).read_text()) + "\n" for n in names)
    name = "bundle-" + hashlib.md5(css.encode()).hexdigest()[:8] + ".css"
    (out / name).write_text(css)
    zone = LINK.sub("", zone)
    zone = zone[:found[0].start()] + f'<link rel="stylesheet" href="{prefix}{name}">\n' + zone[found[0].start():]
    return zone + tail + rest

def main():
    if OUT.exists(): shutil.rmtree(OUT)
    shutil.copytree(SRC, OUT, ignore=shutil.ignore_patterns("__pycache__", ".DS_Store"))
    pages = 0
    for f in OUT.rglob("*.html"):
        h = f.read_text(errors="ignore"); n = bundle_page(h, OUT)
        n = FONT.sub(lambda m: f'<link rel="preload" as="style" href="{m.group(1)}" onload="this.onload=null;this.rel=\'stylesheet\'"><noscript><link href="{m.group(1)}" rel="stylesheet"></noscript>', n)
        if n != h: f.write_text(n); pages += 1
    used = set()
    for f in OUT.rglob("bundle-*.css"): used.add(f.name)
    # drop sheets nothing references any more
    refs = "".join(f.read_text(errors="ignore") for f in OUT.rglob("*.html"))
    for f in OUT.glob("*.css"):
        if not f.name.startswith("bundle-") and f.name not in refs: f.unlink()
    esb = Path(os.environ.get("ESBUILD") or ROOT / "node_modules/.bin/esbuild")
    js = [f for f in OUT.glob("*.js")]
    if esb.exists() and js:
        subprocess.run([str(esb), "--minify", "--log-level=warning", "--allow-overwrite", f"--outdir={OUT}", *map(str, js)], check=True)
    else:
        print("note: esbuild not installed, JS left as is (npm i esbuild)")
    print(f"built {OUT}: {pages} pages rewritten, {len(used)} bundles")

main()
