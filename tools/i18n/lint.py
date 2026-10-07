#!/usr/bin/env python3
"""Flags raw string literals in ImGui label/text calls under src/app/ (docs/plans/i18n block 2).

  tools/i18n/lint.py [--all]      list findings (exit 1 when any); --all also lists allowlisted files

Allowlist (English by design, decision D2 / scope): src/app/bodies/ (param names + dropdown values),
src/app/selftest/, Field reference prose, lines tagged  // i18n-ok , regions between // i18n-skip-begin and // i18n-skip-end, and strings with no letters or only
an ID ("##x"). A finding is the first-argument literal of a widget/text call that is not wrapped.
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from wrap import call_re, has_letters  # same call grammar as the wrapper

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SKIP_DIRS = ('src/app/bodies/', 'src/app/selftest/')
SKIP_FILES = ('src/app/ui/ParamWidgets.cpp',)


def main():
    show_all = '--all' in sys.argv
    found = 0
    for dp, _, fs in os.walk(os.path.join(ROOT, 'src', 'app')):
        for f in sorted(fs):
            if not f.endswith(('.cpp', '.h', '.mm')):
                continue
            path = os.path.join(dp, f)
            rel = os.path.relpath(path, ROOT)
            allow = rel.startswith(SKIP_DIRS) or rel in SKIP_FILES
            if allow and not show_all:
                continue
            src = open(path, encoding='utf-8', errors='replace').read()
            lines = src.split('\n')
            skip = []
            for a, b in zip([m.start() for m in re.finditer('i18n-skip-begin', src)], [m.start() for m in re.finditer('i18n-skip-end', src)]):
                skip.append((a, b))
            for m in call_re.finditer(src):
                if any(a <= m.start() <= b for a, b in skip):
                    continue
                ns, name, lit, sp, term = m.groups()
                if ns is None and name != 'HelpTip':
                    continue
                s = lit[1:-1]
                if not has_letters(lit) or s.startswith('##') or '###' in s:
                    continue
                ln = src.count('\n', 0, m.start()) + 1
                if 'i18n-ok' in lines[ln - 1]:
                    continue
                found += 1
                print(f"{rel}:{ln}: {'(allowlisted) ' if allow else ''}{name}({lit})")
    print(f"{found} unwrapped literal(s)")
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
