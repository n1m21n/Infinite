#!/usr/bin/env python3
"""Snap website CSS to the brand scales, so spacing, type and motion are the same everywhere.

What it rewrites (literal values only; var(), calc() and clamp() are left alone):
  padding, gap          px values -> nearest of the brand space scale (4 8 12 16 24 32 48 64 96 128)
  font-size             px or rem -> nearest web type step (11, 11.5, 13.5, 16, 18, 20, 25, ...)
  transition            literal durations -> the --dur-* tokens; missing or generic easing -> --ease-out
Run without --write to see the count per file. brand.json is the source of the scales.
"""
import glob, json, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
B = json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json"), encoding="utf-8"))
SPACE = B["shape"]["space"]
STEPS = sorted({11, 11.5, 13.5, 16, 18, 20, 25, 32, 39, 40, 61, 96} | {s["min"] for s in B["type"]["web_scale"]} | {s["max"] for s in B["type"]["web_scale"]})
DURS = [(125, "micro"), (188, "small"), (333, "medium"), (500, "large"), (667, "page")]
TIMING = re.compile(r"^(ease|ease-in|ease-out|ease-in-out|linear|steps\(.*|cubic-bezier\(.*|linear\(.*|var\(--(ease|spring)[a-z-]*\))$")


def nearest(v, scale):
    return min(scale, key=lambda s: (abs(s - v), -s))   # ties go up


def snap_space(m):
    v = float(m.group(1))
    if v == 0 or v in SPACE or v <= 2:
        return m.group(0)
    return "%gpx" % nearest(v, SPACE)


def decl_value(css, prop_re, fn, keep_vars=False, deep=False):
    out, n = [], 0
    def rep(m):
        nonlocal n
        val = m.group(2)
        if not deep and re.search(r"calc\(|clamp\(|min\(|max\(|env\(" if keep_vars else r"var\(|calc\(|clamp\(|min\(|max\(|env\(", val):
            return m.group(0)
        new = fn(val)
        if new != val:
            n += 1
        return m.group(1) + new
    return re.sub(r"(?<![-\w])(" + prop_re + r"\s*:\s*)([^;}{]+)", rep, css), n


def padding_fn(val):
    return re.sub(r"(\d+(?:\.\d+)?)px", snap_space, val)


def font_fn(val):
    m = re.fullmatch(r"\s*(\d*\.?\d+)(px|rem)\s*(!important)?\s*", val)
    if not m:
        return val
    px = float(m.group(1)) * (16 if m.group(2) == "rem" else 1)
    if px < 8 or px > 100 or px in STEPS:
        return val
    to = nearest(px, STEPS)
    out = ("%gpx" % to) if m.group(2) == "px" else ("%grem" % round(to / 16, 4))
    return " " + out + (" !important" if m.group(3) else "") if val.startswith(" ") else out + (" !important" if m.group(3) else "")


def split_top(s):
    parts, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur); cur = ""
        else:
            cur += ch
    parts.append(cur)
    return parts


def dur_token(ms):
    if ms == 0 or ms >= 1000:
        return None
    d = min(DURS, key=lambda x: (abs(x[0] - ms), -x[0]))
    return "var(--dur-%s)" % d[1]


def transition_item(item):
    toks = re.findall(r"[^\s(]+(?:\([^)]*\))?", item.strip())
    if not toks or toks[0] in ("none", "initial", "inherit"):
        return item
    out, seen_dur, has_timing = [], False, False
    for t in toks:
        m = re.fullmatch(r"(\d*\.?\d+)(ms|s)", t)
        if m and not seen_dur:
            ms = float(m.group(1)) * (1000 if m.group(2) == "s" else 1)
            out.append(dur_token(ms) or t); seen_dur = True; continue
        if t.startswith("var(--dur") or t.startswith("var(--spring") and t.endswith("-ms)"):
            seen_dur = True
        if TIMING.match(t):
            has_timing = True
            t = {"ease": "var(--ease-out)", "ease-out": "var(--ease-out)", "ease-in-out": "var(--ease-in-out)", "ease-in": "var(--ease-in)"}.get(t, t)
        out.append(t)
    if seen_dur and not has_timing:
        out.append("var(--ease-out)")
    return " " + " ".join(out)


def transition_fn(val):
    if "{" in val:
        return val
    return ",".join(transition_item(p) for p in split_top(val)).lstrip() if not val.startswith(" ") else ",".join(transition_item(p) for p in split_top(val))


def space_deep(val):
    """padding/gap inside clamp()/calc(): snap the px literals, leave vw/em/env alone."""
    return re.sub(r"(?<![\w.])(\d+(?:\.\d+)?)px", snap_space, val)


def font_shorthand(m):
    px = float(m.group(2)) * 16
    if px in STEPS or px < 8:
        return m.group(0)
    return m.group(1) + "%grem" % round(nearest(px, STEPS) / 16, 4) + m.group(3)


def font_em(m):
    em = float(m.group(2))
    if em >= 1 or em <= 0.5:
        return m.group(0)
    to = min((11.5, 13.5), key=lambda x: abs(x / 16 - em))
    return m.group(1) + "%gem" % round(to / 16, 4) + m.group(3)


def main():
    write = "--write" in sys.argv
    files = sorted(glob.glob(os.path.join(ROOT, "website", "*.css")))
    files += sorted(glob.glob(os.path.join(ROOT, "tools", "website", "gen_*.py")))
    skip = {"brand.css", "gen_seo.py"}
    total = 0
    for f in files:
        if os.path.basename(f) in skip:
            continue
        s = open(f, encoding="utf-8").read(); o = s; counts = {}
        s, counts["space"] = decl_value(s, r"padding(?:-(?:top|right|bottom|left|inline|block))?|(?:row-|column-)?gap", padding_fn)
        s, counts["font"] = decl_value(s, r"font-size", font_fn)
        s2 = re.sub(r"(font:\s*(?:italic\s+)?\d{3}\s+)(0?\.\d+)rem((?:/|\s))", font_shorthand, s)
        s2 = re.sub(r"(font-size:\s*)(0?\.\d+)em(\s*[;}])", font_em, s2)
        s2 = re.sub(r"(font:\s*\d{3}\s+)(0?\.\d+)em((?:/|\s))", font_em, s2)
        counts["font"] += int(s2 != s); s = s2
        before = s
        s, _ = decl_value(s, r"padding(?:-(?:top|right|bottom|left|inline|block))?|(?:row-|column-)?gap", space_deep, deep=True)
        counts["space"] += int(s != before)
        s, counts["motion"] = decl_value(s, r"transition", transition_fn, keep_vars=True)
        n = sum(counts.values()); total += n
        if n:
            print("%-28s %s" % (os.path.relpath(f, ROOT), counts))
        if write and s != o:
            open(f, "w", encoding="utf-8").write(s)
    print("%d declarations %s" % (total, "rewritten" if write else "would change (use --write)"))


if __name__ == "__main__":
    main()
