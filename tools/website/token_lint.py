#!/usr/bin/env python3
"""Token lint for the website CSS: counts raw values that should be design-system tokens, per file, against a
ratchet baseline. A file may hold its count or go down; it may not go up.

  token_lint.py              report and compare with tools/website/token_baseline.json (exit 1 if a count rose)
  token_lint.py --update     write the current counts as the new baseline (only after a count went down)
  token_lint.py --show FILE  list the raw values in one file with line numbers

What counts as raw (in website/*.css and <style> blocks of website/**/*.html; brand.css, system.css and scale.css,
which define the tokens, are exempt):
  font-size   a px/rem/em/clamp value instead of var(--size-*), var(--fs-*) or inherit/em-relative 1em/100%
  radius      border-radius in px other than 0, 50% and 999px, instead of var(--r-*) / var(--web-*-radius)
  duration    a transition or animation time in ms/s other than the brand durations (docs/brand/brand.json motion)
  hex         a #hex colour (brand_lint.py judges whether it is on-brand; here any literal counts)
Generated library/*.html pages share a template and count once, as their worst page.
"""
from __future__ import annotations
import glob, json, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WEB = os.environ.get("SITE_DIR") or os.path.join(ROOT, "website")
BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "token_baseline.json")
EXEMPT = {"brand.css", "system.css", "scale.css"}
B = json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json")))
DURS = {int(v) for v in B["motion"]["durations_ms"].values()}

FS = re.compile(r"font-size\s*:\s*([^;}{]+)")
FONT = re.compile(r"(?<![-\w])font\s*:\s*([^;}{]+)")
RAD = re.compile(r"border(?:-(?:top|bottom)-(?:left|right))?-radius\s*:\s*([^;}{]+)")
TIME = re.compile(r"(?:transition|animation)(?:-duration|-delay)?\s*:\s*([^;}{]+)")
HEX = re.compile(r"#[0-9a-fA-F]{3,8}\b")


def raw_font(v):
    v = v.strip()
    if v.startswith("var(") or v in ("inherit", "1em", "100%", "smaller", "larger", "0"):
        return False
    return bool(re.search(r"\d", v))


def font_shorthand_size(v):
    m = re.search(r"(?:^|\s)([\d.]+(?:px|rem|em)|clamp\([^)]*\))\s*(?:/|\s)", v + " ")
    return m and not v.strip().startswith("var(") and m.group(1) not in ("1em",)


def raw_radius(v):
    for t in re.findall(r"[\d.]+px", v):
        if t not in ("0px", "999px", "9999px"):
            return True
    return False


def raw_time(v):
    for n, u in re.findall(r"(?<![\w-])([\d.]+)(ms|s)\b", v):
        ms = float(n) * (1000 if u == "s" else 1)
        if ms and int(round(ms)) not in DURS:
            return True
    return False


def css_sources():
    for f in sorted(glob.glob(os.path.join(WEB, "*.css"))):
        if os.path.basename(f) not in EXEMPT:
            yield os.path.relpath(f, WEB), open(f, encoding="utf-8").read()
    for f in sorted(glob.glob(os.path.join(WEB, "**", "*.html"), recursive=True)):
        rel = os.path.relpath(f, WEB)
        if rel.startswith(("assets/", "node_modules/")):
            continue
        s = open(f, encoding="utf-8", errors="ignore").read()
        css = "\n".join(re.findall(r"<style[^>]*>(.*?)</style>", s, re.S))
        if css.strip():
            yield rel, css


def scan(css):
    hits = []
    for i, line in enumerate(css.splitlines(), 1):
        if line.strip().startswith(("/*", "*")):
            continue
        for m in FS.finditer(line):
            if raw_font(m.group(1)):
                hits.append(("font-size", i, m.group(0).strip()))
        for m in FONT.finditer(line):
            if font_shorthand_size(m.group(1)):
                hits.append(("font-size", i, m.group(0).strip()[:60]))
        for m in RAD.finditer(line):
            if raw_radius(m.group(1)):
                hits.append(("radius", i, m.group(0).strip()))
        for m in TIME.finditer(line):
            if raw_time(m.group(1)):
                hits.append(("duration", i, m.group(0).strip()[:60]))
        for m in HEX.finditer(line):
            hits.append(("hex", i, m.group(0)))
    return hits


def main():
    if "--show" in sys.argv:
        rel = sys.argv[sys.argv.index("--show") + 1]
        for src_rel, css in css_sources():
            if src_rel == rel:
                for k, i, t in scan(css):
                    print("%-9s %5d  %s" % (k, i, t))
        return 0
    counts = {}
    for rel, css in css_sources():
        c = {}
        for k, _, _ in scan(css):
            c[k] = c.get(k, 0) + 1
        if rel.startswith("library/"):  # generated per-patch pages share one template: count the worst page once
            key = "library/*.html"
            counts[key] = {k: max(v, counts.get(key, {}).get(k, 0)) for k, v in {**counts.get(key, {}), **c}.items()}
        elif c:
            counts[rel] = c
    tot = {}
    for c in counts.values():
        for k, v in c.items():
            tot[k] = tot.get(k, 0) + v
    print("raw values:", ", ".join("%s %d" % kv for kv in sorted(tot.items())), "in", len(counts), "files")
    if "--update" in sys.argv or not os.path.exists(BASE):
        json.dump({"_doc": "Ratchet for tools/website/token_lint.py: per-file counts of raw values. Lower it, never raise it.", "counts": counts},
                  open(BASE, "w"), indent=1, sort_keys=True)
        print("baseline written")
        return 0
    base = json.load(open(BASE))["counts"]
    worse, better = [], []
    for rel, c in counts.items():
        for k, v in c.items():
            was = base.get(rel, {}).get(k, 0)
            if v > was:
                worse.append("%s %s %d -> %d" % (rel, k, was, v))
            elif v < was:
                better.append("%s %s %d -> %d" % (rel, k, was, v))
    for rel, c in base.items():
        for k, was in c.items():
            if k not in counts.get(rel, {}) and was:
                better.append("%s %s %d -> 0" % (rel, k, was))
    for b in better:
        print("better", b)
    for w in worse:
        print("WORSE ", w)
    if better and not worse:
        print("counts went down: run with --update to lock them in")
    return 1 if worse else 0


if __name__ == "__main__":
    sys.exit(main())
