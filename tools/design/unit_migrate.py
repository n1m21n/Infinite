#!/usr/bin/env python3
"""Move printf-style unit formatting onto UiFmt (owner 2026-11-24, internal/plans/ui-unit-table.md).

Finds printf-like calls (snprintf, ImGui::Text, SetTooltip, ...) whose format is one plain string literal, and for
each spec followed by a known unit ("%.1f dB", "%.0f%%", "%.2f kHz", ...) rewrites the spec to "%s" and wraps the
matching argument in the UiFmt function. Formats inside T()/L() (translated text) are left alone: their text is
the catalogue key. Prints what it changed and what it skipped; --dry shows the diff count only.

    python3 tools/design/unit_migrate.py [--dry] [files...]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCAN = ["src/app", "src/nodes"]
SKIP = ["src/app/ui/design/UiFmt.h", "src/app/ui/ParamWidgets.cpp"]
SKIP_DIRS = ["src/app/selftest/"]
# a unit spec right after another number ("%.0f-%.0f Hz", "%.1f / %.1f s") is a range: left for a hand edit
RANGE = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?[a-z]*[dfgiu](?:-|\.\.|\s*/\s*|\s+to\s+)$")

# call name -> index of its format argument
CALLS = {
    "snprintf": 2, "std::snprintf": 2, "sprintf": 1, "std::sprintf": 1, "ImFormatString": 2,
    "ImGui::Text": 0, "ImGui::TextDisabled": 0, "ImGui::TextWrapped": 0, "ImGui::SetTooltip": 0,
    "ImGui::BulletText": 0, "ImGui::TextColored": 1, "ImGui::LabelText": 1,
}

SPEC = re.compile(r"%([-+ #0]*)(\d*)(?:\.(\d+))?(hh|h|ll|l|z|j|t|L)?([diouxXeEfFgGcspn%])")

COUNT_WORDS = ("points|tris|triangles|faces|notes|frames|steps|taps|balls|bodies|links|copies|lines|onsets|poles|"
               "modes|inputs|vertices|verts|edges|voices|samples|events|clips|particles|instances")

# (regex on the text right after a spec, accepted conversions, builder(arg, flags, prec, conv) -> call)
def _pct(a, fl, p, c):
    dec = 1 if (p not in (None, "0") and c in "fFgeE") else 0
    sign = "+" in fl
    if sign:
        return f"UiFmt::Pct({a}, {dec}, true)"
    return f"UiFmt::Pct({a}, 1)" if dec else f"UiFmt::Pct({a})"

UNITS = [
    (re.compile(r" ?kHz"), "fFgeE", lambda a, fl, p, c: f"UiFmt::KHz({a}{', true' if '+' in fl else ''})"),
    (re.compile(r" ?Hz"), "fFgeEdiu", lambda a, fl, p, c: f"UiFmt::Hz({a}{', true' if '+' in fl else ''})"),
    (re.compile(r" ?dB(?![A-Za-z])"), "fFgeE", lambda a, fl, p, c: f"UiFmt::{'DbOffset' if '+' in fl else 'Db'}({a})"),
    (re.compile(r" ?LUFS"), "fFgeE", lambda a, fl, p, c: f"UiFmt::Lufs({a})"),
    (re.compile(r" ?ms(?![A-Za-z])"), "fFgeEdiu", lambda a, fl, p, c: f"UiFmt::Ms({a})"),
    (re.compile(r" ?s(?![A-Za-z0-9])"), "fFgeE", lambda a, fl, p, c: f"UiFmt::Sec({a})"),
    (re.compile(r"%%"), "fFgeEdiu", lambda a, fl, p, c: _pct(a, fl, p, c)),
    (re.compile(r" ?st(?![A-Za-z])"), "fFgeEdi", lambda a, fl, p, c: f"UiFmt::Semi({a})"),
    (re.compile(r" ?(?:BPM|bpm)"), "fFgeEdiu", lambda a, fl, p, c: f"UiFmt::Bpm({a})"),
    (re.compile(r" ?px(?![A-Za-z])"), "fFgeE", lambda a, fl, p, c: f"UiFmt::Px({a})"),
    (re.compile("°|\\\\xc2\\\\xb0|\\\\xC2\\\\xB0"), "fFgeE", lambda a, fl, p, c: f"UiFmt::Deg({a})"),
    (re.compile(r"x(?![A-Za-z0-9%])"), "fFgeE", lambda a, fl, p, c: f"UiFmt::Ratio({a})"),
    (re.compile(r" ?MB(?![A-Za-z])"), "fFgeE", lambda a, fl, p, c: f"UiFmt::MB({a})"),
    (re.compile(r" ?KB(?![A-Za-z])"), "fFgeE", lambda a, fl, p, c: f"UiFmt::KB({a})"),
    (re.compile(r" beats(?![A-Za-z])"), "fFgeE", lambda a, fl, p, c: f"UiFmt::Beats({a})"),
]
COUNT = re.compile(r" (?:" + COUNT_WORDS + r")(?![A-Za-z])")


def split_args(s, i):
    """s[i] == '('; return (list of (start, end) arg spans, index of the closing paren) or None."""
    depth = 0
    spans = []
    start = i + 1
    j = i
    n = len(s)
    while j < n:
        ch = s[j]
        if ch in "\"'":
            q = ch
            j += 1
            while j < n and s[j] != q:
                j += 2 if s[j] == "\\" else 1
        elif s.startswith("//", j):
            j = s.find("\n", j)
            if j < 0:
                return None
            continue
        elif s.startswith("/*", j):
            j = s.find("*/", j) + 1
            if j <= 0:
                return None
        elif ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                spans.append((start, j))
                return spans, j
        elif ch == "," and depth == 1:
            spans.append((start, j))
            start = j + 1
        j += 1
    return None


LITERAL = re.compile(r'\s*("(?:[^"\\\n]|\\.)*")\s*$', re.S)


def rewrite_format(lit):
    """lit is the literal body (no quotes). Returns (new_lit, [(spec_index, builder, flags, prec, conv)])."""
    out = []
    plan = []
    pos = 0
    idx = 0
    for m in SPEC.finditer(lit):
        if m.start() < pos:
            continue
        flags, width, prec, length, conv = m.groups()
        if conv == "%":
            continue
        tail = lit[m.end():]
        hit = None
        if "*" not in (width or "") and not width:
            for rx, convs, build in UNITS:
                um = rx.match(tail)
                if um and conv in convs:
                    # "x" as a ratio only right after a float; never in "%dx%d" sizes
                    hit = (um, build)
                    break
            if hit is None and conv in "diu":
                um = COUNT.match(tail)
                if um and length != "ll":
                    hit = (None, "count")
        if hit and RANGE.search(lit[:m.start()]):
            return lit, [], -1
        if hit:
            um, build = hit
            out.append(lit[pos:m.start()])
            out.append("%s")
            if um is not None:
                pos = m.end() + len(um.group(0))
            else:
                pos = m.end()
            plan.append((idx, build, flags or "", prec, conv))
        idx += 1
    out.append(lit[pos:])
    return "".join(out), plan, idx


def process(text):
    changes = []
    skipped = 0
    pat = re.compile(r"(?<![\w:])(" + "|".join(re.escape(k) for k in sorted(CALLS, key=len, reverse=True)) + r")\s*\(")
    pos = 0
    res = []
    while True:
        m = pat.search(text, pos)
        if not m:
            break
        name = m.group(1)
        paren = m.end() - 1
        sp = split_args(text, paren)
        if not sp:
            pos = m.end()
            continue
        spans, close = sp
        fi = CALLS[name]
        if fi >= len(spans):
            pos = m.end()
            continue
        fs, fe = spans[fi]
        lm = LITERAL.match(text[fs:fe])
        if not lm:
            pos = m.end()
            continue
        body = lm.group(1)[1:-1]
        if "=%" in body or body.startswith("[") or body.endswith("\\n"):
            pos = m.end()
            continue
        new_body, plan, nspec = rewrite_format(body)
        args = spans[fi + 1:]
        if not plan:
            pos = m.end()
            continue
        if nspec != len(args):
            skipped += 1
            pos = m.end()
            continue
        # build the new call text
        pieces = [text[m.start():fs], text[fs:fs + lm.start(1)] + '"' + new_body + '"' + text[fs + lm.end(1):fe]]
        planned = {p[0]: p for p in plan}
        for k, (a0, a1) in enumerate(args):
            raw = text[a0:a1]
            lead = raw[:len(raw) - len(raw.lstrip())]
            trail = raw[len(raw.rstrip()):]
            core = raw.strip()
            if k in planned:
                _, build, flags, prec, conv = planned[k]
                if build == "count":
                    core = f"UiFmt::Count((long long)({core})).c"
                else:
                    core = build(core, flags, prec, conv) + ".c"
            pieces.append("," + lead + core + trail)
        pieces.append(")")
        new_call = "".join(pieces)
        res.append((m.start(), close + 1, new_call))
        changes.append((text.count("\n", 0, m.start()) + 1, body, new_body))
        pos = close + 1
    # apply from the end
    for a, b, new in reversed(res):
        text = text[:a] + new + text[b:]
    return text, changes, skipped


def ensure_include(text):
    if "app/ui/design/UiFmt.h" in text:
        return text
    lines = text.split("\n")
    last = -1
    for i, l in enumerate(lines):
        if l.startswith("#include"):
            last = i
    lines.insert(last + 1, '#include "app/ui/design/UiFmt.h"')
    return "\n".join(lines)


def main():
    dry = "--dry" in sys.argv
    files = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not files:
        for d in SCAN:
            for r, _, fs in os.walk(os.path.join(ROOT, d)):
                for f in fs:
                    if f.endswith((".cpp", ".h")):
                        files.append(os.path.join(r, f))
    total = 0
    tskip = 0
    for p in sorted(files):
        if any(p.endswith(s) for s in SKIP) or any(d in p for d in SKIP_DIRS):
            continue
        text = open(p, encoding="utf-8", errors="surrogateescape").read()
        new, changes, skipped = process(text)
        tskip += skipped
        if not changes:
            continue
        total += len(changes)
        rel = os.path.relpath(p, ROOT)
        for ln, old, nb in changes:
            print(f"{rel}:{ln}: \"{old}\" -> \"{nb}\"")
        if not dry:
            open(p, "w", encoding="utf-8", errors="surrogateescape").write(ensure_include(new))
    print(f"{total} calls rewritten, {tskip} skipped (argument count did not match)", file=sys.stderr)


if __name__ == "__main__":
    main()
