"""Brand lint: find colours on brand surfaces that are not in docs/brand/brand.json.

What: scans files for hex and rgb()/rgba() colours, classifies each as brand (any value in brand.json or the
generated brand.css), retired (colour.retired) or unknown, and names the nearest brand colour (deltaE OK).
Why: v1 colours (terracotta, coral, warm inks) drifted into the website and films by hand-copying. A ratchet keeps
the count from growing while surfaces migrate.

Usage:
    python3 tools/brand/brand_lint.py                     # default targets (website/), ratchet against the baseline
    python3 tools/brand/brand_lint.py PATH [PATH ...]     # files or folders
    python3 tools/brand/brand_lint.py --list              # every off-brand colour with file:line and nearest brand value
    python3 tools/brand/brand_lint.py --update-baseline   # accept current counts (after a migration lowers them)

Exit codes: 0 no file got worse and nothing retired is left, 1 a file has more off-brand colours than its baseline
or uses a retired colour, 2 usage error.
Baseline: tools/brand/lint_baseline.json (per-file unknown counts). Art inside a page (canvas demos, shader
palettes) can be skipped with a `brand-lint: off` / `brand-lint: on` comment pair.
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import colour as K  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BRAND = os.path.join(ROOT, "docs", "brand")
BASELINE = os.path.join(os.path.dirname(__file__), "lint_baseline.json")
DEFAULT = ["website"]
EXTS = (".css", ".html", ".js", ".svg", ".md", ".json", ".py")
SKIP_DIRS = {"vendor", "node_modules", ".git", "library"}
HEX = re.compile(r"(?<![\w&])#([0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3,4})\b")
RGB = re.compile(r"rgba?\(\s*(\d{1,3})[\s,]+(\d{1,3})[\s,]+(\d{1,3})")


def norm(h):
    h = h.lstrip("#")
    if len(h) in (3, 4):
        h = "".join(c * 2 for c in h[:3])
    return "#" + h[:6].upper()


def brand_colours():
    allowed = set()
    for f in ("brand.json", "brand.css"):
        p = os.path.join(BRAND, f)
        if os.path.exists(p):
            allowed |= {norm(m.group(0)) for m in HEX.finditer(open(p).read())}
            allowed |= {"#%02X%02X%02X" % tuple(map(int, m.groups())) for m in RGB.finditer(open(p).read())}
    allowed |= {"#000000", "#FFFFFF"}
    B = json.load(open(os.path.join(BRAND, "brand.json")))
    live = set()
    for m in B["colour"]["modes"].values():
        live |= {norm(v) for v in m.values() if isinstance(v, str) and v.startswith("#")}
    live |= {norm(v) for v in B["colour"].get("anchors", {}).values() if isinstance(v, str)}
    retired = {norm(r["hex"]): r for r in B["colour"]["retired"] if norm(r["hex"]) not in live}
    allowed -= set(retired)
    allowed -= {norm(k) for k in B["colour"].get("migrate", {}) if k.startswith("#")}  # sources of a swap are not brand
    core = sorted(live)  # nearest-suggestion pool: the mode roles and anchors
    return allowed, retired, core


def files(paths):
    for p in paths:
        p = os.path.join(ROOT, p) if not os.path.isabs(p) else p
        if os.path.isfile(p):
            yield p
            continue
        for d, ds, fs in os.walk(p):
            ds[:] = [x for x in ds if x not in SKIP_DIRS]
            for f in sorted(fs):
                if f.endswith(EXTS) and ".min." not in f:
                    yield os.path.join(d, f)


def scan(path, allowed, retired):
    out = []
    on = True
    for i, line in enumerate(open(path, errors="ignore"), 1):
        if "brand-lint: off" in line:
            on = False
        if "brand-lint: on" in line:
            on = True
            continue
        if not on:
            continue
        found = [norm(m.group(0)) for m in HEX.finditer(line)]
        found += ["#%02X%02X%02X" % tuple(min(255, int(x)) for x in m.groups()) for m in RGB.finditer(line)]
        for h in found:
            if h in retired:
                out.append((i, h, "retired"))
            elif h not in allowed:
                out.append((i, h, "unknown"))
    return out


def nearest(h, pool):
    return min(pool, key=lambda c: K.delta_e(h, c))


def main():
    args = sys.argv[1:]
    flags = {a for a in args if a.startswith("--")}
    if flags - {"--list", "--update-baseline"}:
        print(__doc__)
        return 2
    paths = [a for a in args if not a.startswith("--")] or DEFAULT
    allowed, retired, core = brand_colours()
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    report, worse, ret = {}, [], []
    for f in files(paths):
        hits = scan(f, allowed, retired)
        rel = os.path.relpath(f, ROOT)
        unk = [h for h in hits if h[2] == "unknown"]
        report[rel] = len(unk)
        if any(h[2] == "retired" for h in hits):
            ret.append((rel, [h for h in hits if h[2] == "retired"]))
        if len(unk) > base.get(rel, 0):
            worse.append((rel, base.get(rel, 0), len(unk)))
        if "--list" in flags:
            for ln, h, kind in hits:
                n = nearest(h, core)
                print(f"{rel}:{ln}  {h}  {kind:8} nearest {n} (dE {K.delta_e(h, n):.1f})"
                      + (f"  -> {retired[h]['now']}" if kind == "retired" else ""))
    total = sum(report.values())
    print(f"brand lint: {len(report)} files, {total} off-brand colours, {sum(len(r[1]) for r in ret)} retired")
    for rel, hits in ret:
        print(f"  retired in {rel}: " + ", ".join(f"{h} line {ln}" for ln, h, _ in hits[:6]) + (" ..." if len(hits) > 6 else ""))
    if "--update-baseline" in flags:
        base.update({k: v for k, v in report.items() if v})
        for k in [k for k in base if report.get(k, None) == 0]:
            del base[k]
        json.dump(dict(sorted(base.items())), open(BASELINE, "w"), indent=1)
        print(f"baseline updated: {os.path.relpath(BASELINE, ROOT)}")
        return 0
    for rel, was, now in worse:
        print(f"  worse: {rel} {was} -> {now}")
    return 1 if worse or ret else 0


if __name__ == "__main__":
    sys.exit(main())
