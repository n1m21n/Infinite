#!/usr/bin/env python3
"""Set every `name.css?v=` in the website and its generators to a short hash of that CSS file, so a changed
stylesheet is never served stale from cache. Run after editing any website/*.css, then re-run the generators."""
import glob, hashlib, os, re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W = os.path.join(ROOT, "website")
hashes = {os.path.basename(f): hashlib.md5(open(f, "rb").read()).hexdigest()[:8] for f in glob.glob(os.path.join(W, "*.css"))}
targets = glob.glob(os.path.join(W, "**", "*.html"), recursive=True) + glob.glob(os.path.join(ROOT, "tools", "website", "*.py")) \
    + glob.glob(os.path.join(ROOT, "tools", "website", "*.html")) + glob.glob(os.path.join(ROOT, "tools", "seo", "*.py"))
pat = re.compile(r"((?:/|\")?\b([a-z0-9_-]+\.css))\?v=[A-Za-z0-9]+")
n = 0
for f in targets:
    s = open(f, encoding="utf-8").read()
    t = pat.sub(lambda m: m.group(1) + "?v=" + hashes[m.group(2)] if m.group(2) in hashes else m.group(0), s)
    if t != s:
        open(f, "w", encoding="utf-8").write(t); n += 1
print("css versions updated in %d files" % n)
