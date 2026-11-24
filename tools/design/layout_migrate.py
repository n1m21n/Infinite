#!/usr/bin/env python3
"""Move raw ImGui layout calls in panels and chrome onto the enum-only layout parts in src/app/ui/design/UiScope.h
(ui-formalisation Block B). Node bodies are left for Block C (their rows come from param tables).

Rewrites, one call at a time, only when the result is the same ImGui call with the same value (pixel no-op):

  ImGui::Dummy(ImVec2(0, G))         -> Ui::Spacer(Ui::Space::Sx)     G resolves to a space ladder step
  ImGui::SameLine(0, G)              -> Ui::SameRow(Ui::Space::Sx)    G resolves to a ladder step or 0 (None)
  ImGui::SameLine()                  -> Ui::SameRow()
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x) -> Ui::FillNext()
  ImGui::Dummy(ImVec2(w, h)) / Dummy(size), w and h not literals    -> Ui::Claim(...)   a drawn part's own size
  ImGui::Dummy(ImVec2(0, 0))          -> Ui::Settle()       ends a hand-placed row
  SetCursorPosY(GetCursorPosY() -/+ ItemSpacing.y)   -> Ui::JoinBelow() / Ui::ExtraItemGap()
  SetCursorPosX(std::max(GetCursorPosX(), x))        -> Ui::ColumnAt(x)
  ImGui::GetContentRegionAvail()[.x|.y]  -> Ui::Avail() / AvailW() / AvailH()   a size read, no placement

G may be a literal or a tok:: name; tok:: names are resolved from Tokens.gen.h. Anything else (an off-ladder gap,
a computed gap, cursor placement, avail maths) is listed for review and left alone: those need a Row with tracks or
a `// ui-layout: canvas` tag by hand.

    python3 tools/design/layout_migrate.py [--dry] [--file src/...]
"""
import os, re, struct, sys, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_audit  # noqa: E402

ROOT = ui_audit.ROOT
SCOPE = ("src/app/panels/", "src/app/frame/", "src/app/ui/", "src/app/graph/", "src/app/AppShared.h", "src/app/AppCommon.h")
SKIP = (*ui_audit.KIT, "src/app/bodies/", "src/app/selftest/")
INCLUDE = '#include "app/ui/design/UiScope.h"'
NUM = r"[0-9]+(?:\.[0-9]*)?f?"
ZERO = r"0(?:\.0*)?f?"


def f32(v):
    return struct.unpack("f", struct.pack("f", float(v)))[0]


def load_tokens():
    env = {}
    src = (ROOT / "src/app/ui/design/Tokens.gen.h").read_text()
    for m in re.finditer(r"constexpr\s+(?:float|int|double|unsigned)\s+(\w+)\s*=\s*([^;]+);", src):
        n, e = m.groups()
        e = re.sub(r"static_cast<(?:int|double|float)>\((\w+)\)", r"\1", e)
        e = re.sub(r"(\d)[fu]\b", r"\1", e)
        try:
            env[n] = eval(e, {}, env)
        except Exception:
            pass
    return env


TOK = load_tokens()
LADDER = {}   # float value -> enum name
for k, v in TOK.items():
    m = re.fullmatch(r"space_(\d+(?:_\d+)?)", k)
    if m:
        LADDER[f32(v)] = "S" + m.group(1)


def value(expr):
    expr = expr.strip()
    if re.fullmatch(NUM, expr):
        return f32(expr.rstrip("f"))
    m = re.fullmatch(r"tok::(\w+)", expr)
    if m and m.group(1) in TOK:
        return f32(TOK[m.group(1)])
    return None


def step(expr, allow_zero=False):
    v = value(expr)
    if v is None:
        return None
    if v == 0.0:
        return "None" if allow_zero else None
    return LADDER.get(v)


ARG = r"((?:[^()]|\([^()]*\))+?)"
RULES = [
    ("settle", re.compile(r"ImGui::Dummy\(ImVec2\(\s*" + ZERO + r"\s*,\s*" + ZERO + r"\s*\)\)"), lambda m: "Ui::Settle()"),
    ("join_below", re.compile(r"ImGui::SetCursorPosY\(\s*ImGui::GetCursorPosY\(\)\s*-\s*ImGui::GetStyle\(\)\.ItemSpacing\.y\s*\)"),
     lambda m: "Ui::JoinBelow()"),
    ("extra_item_gap", re.compile(r"ImGui::SetCursorPosY\(\s*ImGui::GetCursorPosY\(\)\s*\+\s*ImGui::GetStyle\(\)\.ItemSpacing\.y\s*\)"),
     lambda m: "Ui::ExtraItemGap()"),
    ("column_at", re.compile(r"ImGui::SetCursorPosX\(\s*std::max\(\s*ImGui::GetCursorPosX\(\)\s*,\s*" + ARG + r"\s*\)\s*\)"),
     lambda m: f"Ui::ColumnAt({m.group(1).strip()})"),
    ("spacer", re.compile(r"ImGui::Dummy\(ImVec2\(\s*" + ZERO + r"\s*,\s*" + ARG + r"\s*\)\)"),
     lambda m: (lambda s: s and f"Ui::Spacer(Ui::Space::{s})")(step(m.group(1)))),
    ("same_row_gap", re.compile(r"ImGui::SameLine\(\s*" + ZERO + r"\s*,\s*" + ARG + r"\s*\)"),
     lambda m: (lambda s: s and f"Ui::SameRow(Ui::Space::{s})")(step(m.group(1), allow_zero=True))),
    ("same_row", re.compile(r"ImGui::SameLine\(\s*\)"), lambda m: "Ui::SameRow()"),
    ("fill_next", re.compile(r"ImGui::SetNextItemWidth\(\s*ImGui::GetContentRegionAvail\(\)\.x\s*\)"), lambda m: "Ui::FillNext()"),
    ("avail_w", re.compile(r"ImGui::GetContentRegionAvail\(\)\.x\b"), lambda m: "Ui::AvailW()"),
    ("avail_h", re.compile(r"ImGui::GetContentRegionAvail\(\)\.y\b"), lambda m: "Ui::AvailH()"),
    ("avail", re.compile(r"ImGui::GetContentRegionAvail\(\)"), lambda m: "Ui::Avail()"),
    ("claim", re.compile(r"ImGui::Dummy\(" + ARG + r"\)"), None),   # handled below
]


def claim(m):
    arg = m.group(1).strip()
    v = re.fullmatch(r"ImVec2\(\s*" + ARG + r"\s*,\s*" + ARG + r"\s*\)", arg)
    if v:
        a, b = v.group(1).strip(), v.group(2).strip()
        nonzero = lambda e: value(e) is not None and value(e) != 0.0
        if nonzero(a) or nonzero(b) or (value(a) == 0.0 and value(b) == 0.0):   # a literal / token side: a gap, not a part's size
            return None
    elif value(arg) is not None:
        return None
    return f"Ui::Claim({arg})"


def migrate(text):
    hits, left = collections.Counter(), []
    out = []
    for n, line in enumerate(text.split("\n"), 1):
        code, sep, comment = line.partition("//")
        if "ui-layout: canvas" in comment:
            out.append(line); continue
        for name, rx, fn in RULES:
            def sub(m, name=name, fn=fn):
                r = claim(m) if name == "claim" else fn(m)
                if r:
                    hits[name] += 1
                    return r
                return m.group(0)
            code = rx.sub(sub, code)
        if re.search(r"\bImGui::(" + "|".join(ui_audit.LAYOUT_CALLS) + r")\s*\(", code):
            left.append((n, line.strip()))
        out.append(code + sep + comment)
    return "\n".join(out), hits, left


def add_include(text):
    if INCLUDE in text:
        return text
    lines = text.split("\n")
    head = []   # the leading include block only: a header may include more files further down, after code using Ui::
    for i, l in enumerate(lines):
        t = l.strip()
        if t and not t.startswith(("#", "//", "/*", "*")):
            break
        head.append(i)
    last = max((i for i in head if lines[i].startswith("#include")), default=None)
    if last is None:
        return text
    design = [i for i in head if lines[i].startswith('#include "app/ui/design/')]
    at = (design[-1] if design else last) + 1
    lines.insert(at, INCLUDE)
    return "\n".join(lines)


def main():
    dry = "--dry" in sys.argv
    only = sys.argv[sys.argv.index("--file") + 1] if "--file" in sys.argv else None
    total, leftover = collections.Counter(), collections.Counter()
    for rel, src in ui_audit.ui_files():
        if not rel.startswith(SCOPE) or rel.startswith(SKIP) or (only and rel != only):
            continue
        new, hits, left = migrate(src)
        leftover[rel] = len(left)
        if hits:
            total.update(hits)
            if not dry:
                (ROOT / rel).write_text(add_include(new), encoding="utf-8")
    print("rewritten:", dict(total), "=", sum(total.values()))
    print("left for review (panels/chrome):", sum(leftover.values()))
    for f, n in leftover.most_common(15):
        if n:
            print(f"  {n:4d}  {f}")


if __name__ == "__main__":
    main()
