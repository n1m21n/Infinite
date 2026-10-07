#!/usr/bin/env python3
"""Wrap literal UI strings in ImGui calls with I18n L() / T().

  tools/i18n/wrap.py [--dry-run] FILE...

Only calls whose FIRST argument is a plain string literal are rewritten:
  widget labels (Button, MenuItem, BeginMenu, Checkbox, Selectable, Slider*, Drag*, Input*, ...) -> L("..")
  plain text (Text*, SetTooltip, SeparatorText, HelpTip, BulletText)                            -> T("..")
Never touched: Begin/BeginPopup*/OpenPopup/PushID (their strings are IDs that other code looks up),
labels that are only an ID ("##x"), strings with no letters, and anything already wrapped.
Printing helpers keep their varargs: Text("hi %d", n) -> Text(T("hi %d"), n); a plain Text("hi")
becomes Text("%s", T("hi")) so -Wformat-security stays quiet.
"""
import re, sys

LABEL = ["Button","SmallButton","MenuItem","BeginMenu","Checkbox","RadioButton","Selectable","BeginCombo",
         "SliderFloat","SliderFloat2","SliderFloat3","SliderFloat4","SliderInt","SliderAngle","DragFloat","DragFloat2",
         "DragFloat3","DragInt","InputText","InputTextMultiline","InputFloat","InputInt","BeginTabItem",
         "CollapsingHeader","TreeNode","TreeNodeEx","ColorEdit3","ColorEdit4","ColorButton","Combo","ListBox",
         "MenuItemEx"]
TEXT = ["Text","TextDisabled","TextWrapped","BulletText","SetTooltip","SeparatorText"]
APP_TEXT = ["HelpTip"]  # app-level helper taking a printf-style first argument

LIT = r'"(?:[^"\\\n]|\\.)*"'
call_re = re.compile(r'\b(ImGui::)?(' + '|'.join(LABEL + TEXT + APP_TEXT) + r')\s*\(\s*(' + LIT + r')(\s*)([,)])')

def has_letters(s):
    body = re.sub(r'%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z]', '', s[1:-1])
    return re.search(r'[A-Za-zÀ-￿]', body) is not None

def process(src):
    n = 0
    out = []
    pos = 0
    for m in call_re.finditer(src):
        ns, name, lit, sp, term = m.groups()
        if ns is None and name not in APP_TEXT:
            continue  # an unqualified Text(...) may be something else
        # adjacent literal concatenation: "a" "b" - leave alone
        after = src[m.end(3):m.end(3)+40].lstrip()
        if after.startswith('"'):
            continue
        body = lit[1:-1]
        if body.startswith('##') or '###' in body or not has_letters(lit):
            continue
        # skip when the previous non-space char shows it is already wrapped, e.g. L("x")
        prefix = src[max(0, m.start(3)-8):m.start(3)]
        if re.search(r'\b[TL]C?\s*\(\s*$', prefix):
            continue
        is_label = name in LABEL
        if is_label:
            new = f'L({lit})'
            rep = f'{(ns or "")}{name}({new}{sp}{term}'
        else:
            if '%' not in body and name in ("Text","TextDisabled","TextWrapped","BulletText","SetTooltip","HelpTip") and term == ')':
                rep = f'{(ns or "")}{name}("%s", T({lit}))'
            elif name == "SeparatorText":
                rep = f'{(ns or "")}{name}(T({lit}){sp}{term}'
            else:
                rep = f'{(ns or "")}{name}(T({lit}){sp}{term}'
        out.append(src[pos:m.start()])
        out.append(rep)
        pos = m.end()
        n += 1
    out.append(src[pos:])
    return ''.join(out), n

def main():
    dry = '--dry-run' in sys.argv
    files = [a for a in sys.argv[1:] if not a.startswith('--')]
    total = 0
    for f in files:
        s = open(f, encoding='utf-8').read()
        new, n = process(s)
        total += n
        print(f"{f}: {n}")
        if n and not dry:
            open(f, 'w', encoding='utf-8').write(new)
    print("total", total)

if __name__ == '__main__':
    main()
