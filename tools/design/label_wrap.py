#!/usr/bin/env python3
"""Wrap the visible strings ui_audit.py lists as `label` in the i18n calls (T for text, L for labels).

Reads the findings from ui_audit.py itself, so it touches exactly what the audit counts:
  text calls (Text, TextColored, TextWrapped, TextDisabled, TextUnformatted, AddText, Heading, Show, InputTextWithHint)
      -> T("..."); a printf call whose only argument is the literal becomes Text("%s", T("...")) (-Wformat-security)
  label calls (Item, Draw, Row, Label, Combo, Slider*, ...) -> L("...")
Skipped and printed: window titles (Begin: the title is also the window's saved id), adjacent literals, a literal
not found on its line.

    python3 tools/design/label_wrap.py [--dry]
"""
import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_audit  # noqa: E402

ROOT = ui_audit.ROOT
TEXT = {"Text", "TextColored", "TextWrapped", "TextDisabled", "TextUnformatted", "AddText", "Heading", "Show",
        "InputTextWithHint", "SetTooltip", "BulletText", "Caption", "Notice"}
PRINTF = {"Text", "TextColored", "TextWrapped", "TextDisabled", "BulletText", "SetTooltip"}
SKIP = {"Begin"}
LIT = re.compile(r'"(?:[^"\\\n]|\\.)*"')


def main():
    dry = "--dry" in sys.argv
    findings, _, _ = ui_audit.run()
    by_file = {}
    for f in findings:
        if f[0] == "label" and not f[4].startswith(ui_audit.KIT):
            by_file.setdefault(f[4], []).append(f)
    done = skipped = 0
    for rel, fs in sorted(by_file.items()):
        path = ROOT / rel
        lines = path.read_text(encoding="utf-8").split("\n")
        for f in fs:
            call, text, ln = f[1], f[3], f[5]
            if call in SKIP:
                print(f"skip {rel}:{ln} {call} (window title is its id)"); skipped += 1; continue
            l = lines[ln - 1]
            hit = None
            for m in LIT.finditer(l):
                body = m.group(0)[1:-1]
                if body.startswith(text[:40]) and not re.search(r"\b(?:L|T|TC|LC)\(\s*$", l[:m.start()]):
                    hit = m; break
            if hit is None:
                print(f"skip {rel}:{ln} not found: {text[:40]!r}"); skipped += 1; continue
            after = l[hit.end():].lstrip()
            before = l[:hit.start()].rstrip()
            if after.startswith('"') or before.endswith('"'):
                print(f"skip {rel}:{ln} adjacent literals"); skipped += 1; continue
            lit = hit.group(0)
            if call in TEXT:
                rep = f"T({lit})"
                if call in PRINTF and after.startswith(")") and (before.endswith("(") or before.endswith(",")):
                    # the literal is the whole format: print it through %s ("%%" was an escaped percent sign)
                    rep = f'"%s", T({lit.replace("%%", "%")})'
            else:
                rep = f"L({lit})"
            lines[ln - 1] = l[:hit.start()] + rep + l[hit.end():]
            done += 1
        if not dry:
            path.write_text("\n".join(lines), encoding="utf-8")
    print(f"label_wrap: {done} wrapped, {skipped} skipped")


if __name__ == "__main__":
    main()
