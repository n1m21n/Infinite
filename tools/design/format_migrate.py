#!/usr/bin/env python3
"""Move every chosen number precision in a printf format (ui_audit.py `format`) onto UiFmt (src/app/ui/design/UiFmt.h).

    snprintf(buf, n, "Q %.2f", q)            ->  snprintf(buf, n, "Q %s", UiFmt::F2(q).c)
    ImGui::SliderFloat("##x", &v, 0, 1, "%.2f") ->  ImGui::SliderFloat("##x", &v, 0, 1, UiFmt::Spec::F2)

%.Nf -> FN, %+.Nf -> SFN, %g / %.Ng -> G / GN, %0Nd -> PadN, %+d -> SInt, %0Nx -> HexN, %02X -> HexU2.
Plain %s / %d slots are not findings (ui_audit `placeholder`). Skipped and printed: a format inside T()/HelpTip
(its text is a translation key: changing it would drop the translations), a spec with no helper, a format whose
arguments are not all on the statement's lines, adjacent literals.

    python3 tools/design/format_migrate.py [--dry]
"""
import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_audit  # noqa: E402

ROOT = ui_audit.ROOT
SPEC = re.compile(r"%%|%([-+ #0]*)(\d*)(?:\.(\d+))?(hh|h|ll|l|z|j|t|L)?([diouxXeEfgGaAcsp])")
WIDGET = {"SliderFloat", "DragFloat", "SliderFloat2", "DragFloat2", "SliderFloat3", "DragFloat3", "InputFloat", "VSliderFloat",
          "DragFloatRange2", "SliderAngle"}
CALLS = {"snprintf", "Text", "TextDisabled", "TextColored", "TextWrapped", "Show", "ImFormatString", "SetTooltip", "BulletText"}
INC = '#include "app/ui/design/UiFmt.h"'


def helper(flags, width, prec, length, conv):
    if conv == "f" and not width:
        p = int(prec) if prec is not None else 6
        if "+" in flags and p <= 2: return f"SF{p}"
        if not flags and p <= 4: return f"F{p}"
    if conv == "g" and not flags and not width:
        if prec is None: return "G"
        if prec in ("3", "4", "5", "9"): return "G" + prec
    if conv in "di" and flags == "0" and width in ("2", "3") and prec is None: return "Pad" + width
    if conv in "di" and flags == "+" and not width and prec is None: return "SInt"
    if conv == "x" and flags == "0" and width in ("2", "3", "4", "8", "12"): return "Hex" + width
    if conv == "X" and flags == "0" and width == "2": return "HexU2"
    return None


def split_args(src, start):
    """args of the call whose '(' is at start: list of (a, b) spans, and the index of the closing ')'"""
    depth, i, a, out = 0, start + 1, start + 1, []
    n = len(src)
    while i < n:
        c = src[i]
        if c in "\"'":
            q = c; i += 1
            while i < n and src[i] != q: i += 2 if src[i] == "\\" else 1
        elif c in "([{": depth += 1
        elif c in ")]}":
            if depth == 0:
                out.append((a, i)); return out, i
            depth -= 1
        elif c == "," and depth == 0:
            out.append((a, i)); a = i + 1
        i += 1
    return None, None


def main():
    dry = "--dry" in sys.argv
    findings, _, _ = ui_audit.run()
    by_file = {}
    for f in findings:
        if f[0] == "format" and not f[4].startswith(ui_audit.KIT):
            by_file.setdefault(f[4], []).append(f)
    done = skipped = 0
    for rel, fs in sorted(by_file.items()):
        path = ROOT / rel
        src = path.read_text(encoding="utf-8")
        line_at = [0]
        for m in re.finditer("\n", src): line_at.append(m.end())
        edits = []   # (a, b, text)
        for f in fs:
            call, text, ln = f[1], f[3], f[5]
            if call not in CALLS and call not in WIDGET:
                print(f"skip {rel}:{ln} in {call} (translation key or helper)"); skipped += 1; continue
            lo, hi = line_at[ln - 1], line_at[ln] if ln < len(line_at) else len(src)
            m = None
            for mm in re.finditer(r'"(?:[^"\\\n]|\\.)*"', src[lo:hi]):
                if mm.group(0)[1:-1].startswith(text[:40]): m = mm; break
            if m is None:
                print(f"skip {rel}:{ln} literal not found"); skipped += 1; continue
            la, lb = lo + m.start(), lo + m.end()
            if src[lb:].lstrip().startswith('"') or src[:la].rstrip().endswith('"'):
                print(f"skip {rel}:{ln} adjacent literals"); skipped += 1; continue
            # enclosing call: nearest unmatched '(' before the literal
            depth, k = 0, la - 1
            while k >= 0:
                c = src[k]
                if c == ")": depth += 1
                elif c == "(":
                    if depth == 0: break
                    depth -= 1
                k -= 1
            if k < 0 or src[k - len(call):k].strip() != call and not src[:k].rstrip().endswith(call):
                print(f"skip {rel}:{ln} not a direct argument of {call}"); skipped += 1; continue
            lit = src[la:lb]
            if call in WIDGET:
                body = lit[1:-1]
                mm = SPEC.fullmatch(body)
                h = helper(*mm.groups()) if mm and mm.group(0) != "%%" else None
                if h and h in ("F0", "F1", "F2", "F3", "F4", "G", "G3"):
                    edits.append((la, lb, f"UiFmt::Spec::{h}")); done += 1
                else:
                    print(f"skip {rel}:{ln} widget format {body!r}"); skipped += 1
                continue
            args, close = split_args(src, k)
            if args is None:
                print(f"skip {rel}:{ln} unbalanced"); skipped += 1; continue
            fi = next((i for i, (a, b) in enumerate(args) if a <= la and lb <= b), None)
            if fi is None:
                skipped += 1; continue
            if src[args[fi][0]:args[fi][1]].strip() != lit:
                print(f"skip {rel}:{ln} format is an expression"); skipped += 1; continue
            rest = args[fi + 1:]
            specs = [s for s in SPEC.finditer(lit) if s.group(0) != "%%"]
            if any("*" in s.group(0) for s in specs) or len(specs) > len(rest):
                print(f"skip {rel}:{ln} argument count"); skipped += 1; continue
            hs = [helper(*s.groups()) if not ui_audit.PLAIN_SPEC.fullmatch(s.group(0)) else "" for s in specs]
            if any(h is None for h in hs):
                print(f"skip {rel}:{ln} no helper for {[s.group(0) for s, h in zip(specs, hs) if h is None]}"); skipped += 1; continue
            newlit, pos = "", 0
            for s, h in zip(specs, hs):
                newlit += lit[pos:s.start()] + (s.group(0) if not h else "%s"); pos = s.end()
            newlit += lit[pos:]
            edits.append((la, lb, newlit))
            for (a, b), h in zip(rest, hs):
                if h:
                    expr = src[a:b]
                    lead = expr[:len(expr) - len(expr.lstrip())]
                    tail = expr[len(expr.rstrip()):]
                    edits.append((a, b, f"{lead}UiFmt::{h}({expr.strip()}).c{tail}"))
            done += 1
        if not edits:
            continue
        for a, b, t in sorted(edits, reverse=True):
            src = src[:a] + t + src[b:]
        if INC not in src and "UiFmt.h" not in src:
            m = re.search(r"^#include .*$", src, re.M)
            src = src[:m.end()] + "\n" + INC + src[m.end():]
        if not dry:
            path.write_text(src, encoding="utf-8")
    print(f"format_migrate: {done} formats moved, {skipped} skipped")


if __name__ == "__main__":
    main()
