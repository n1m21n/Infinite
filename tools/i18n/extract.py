#!/usr/bin/env python3
"""Key extraction + table report for the interface translation tables.

  tools/i18n/extract.py keys              print every English key found in src/ (one per line, TSV-escaped)
  tools/i18n/extract.py report            per language: missing / unused / printf-spec mismatches
  tools/i18n/extract.py stub LANG         print missing keys as "key<TAB>" lines for a translator

A key is the string literal inside T("..") / L("..") / TC("ctx","..") / LC("ctx","..").
For L() the visible part (text before "##") is the key, so "Save##a" and "Save##b" share one entry.
Context keys are written  ctx\\ckey  (\\c = the 0x04 separator) in the TSV.
Exit code 1 from `report` when any language has missing keys or format mismatches.
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
LIT = r'"((?:[^"\\\n]|\\.)*)"'
CALL = re.compile(r'(?<![\w:.])(?:I18n::)?(TC|LC|T|L)\s*\(\s*' + LIT + r'(?:\s*,\s*' + LIT + r')?')
LANGS = ['es', 'de', 'zh', 'ja', 'ru']


def unesc(s):
    return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t', '"': '"', '\\': '\\'}.get(m.group(1), '\\' + m.group(1)), s)


def esc(s):
    return s.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t').replace('\x04', '\\c')


def tsv_unesc(s):
    out, i = [], 0
    while i < len(s):
        if s[i] == '\\' and i + 1 < len(s):
            n = s[i + 1]
            out.append({'n': '\n', 't': '\t', 'c': '\x04', '\\': '\\'}.get(n, '\\' + n))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return ''.join(out)


def keys_in_src():
    keys = {}
    for dirpath, _, files in os.walk(os.path.join(ROOT, 'src')):
        for f in files:
            if not f.endswith(('.cpp', '.h', '.mm')):
                continue
            path = os.path.join(dirpath, f)
            text = open(path, encoding='utf-8', errors='replace').read()
            for m in CALL.finditer(text):
                fn, a, b = m.group(1), m.group(2), m.group(3)
                if fn in ('TC', 'LC'):
                    if b is None:
                        continue
                    ctx, key = unesc(a), unesc(b)
                else:
                    ctx, key = None, unesc(a)
                if fn in ('L', 'LC') and '##' in key:
                    key = key.split('##')[0]
                if not key.strip():
                    continue
                full = (ctx + '\x04' + key) if ctx else key
                keys.setdefault(full, os.path.relpath(path, ROOT))
    return keys


def load_table(code):
    p = os.path.join(ROOT, 'resources', 'lang', code + '.tsv')
    if not os.path.exists(p):
        return None
    t = {}
    for line in open(p, encoding='utf-8'):
        line = line.rstrip('\n').rstrip('\r')
        if not line or line.startswith('#') or '\t' not in line:
            continue
        k, v = line.split('\t', 1)
        t[tsv_unesc(k)] = tsv_unesc(v)
    return t


def fmt_sig(s):
    return [m.group(2) + (m.group(1) or '')
            for m in re.finditer(r'%(?!%)[-+ #0]*\d*(\.\d+)?(?:l|ll|h|z)?([a-zA-Z])', s)]


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'report'
    keys = keys_in_src()
    if cmd == 'keys':
        for k in sorted(keys):
            print(esc(k))
        return 0
    if cmd == 'stub':
        t = load_table(sys.argv[2]) or {}
        for k in sorted(keys):
            if k not in t:
                print(esc(k) + '\t')
        return 0
    bad = 0
    print(f"{len(keys)} keys in src/")
    for code in LANGS:
        t = load_table(code)
        if t is None:
            print(f"  {code}: no table")
            bad = 1
            continue
        missing = [k for k in keys if k not in t]
        unused = [k for k in t if k not in keys]
        fmt = [k for k in t if k in keys and t[k] != '=en' and fmt_sig(k.split('\x04')[-1]) != fmt_sig(t[k])]
        print(f"  {code}: {len(t)} entries, {len(missing)} missing, {len(unused)} unused, {len(fmt)} format mismatches")
        for k in missing[:5]:
            print("      missing:", esc(k))
        for k in fmt[:5]:
            print("      format :", esc(k), '=>', t[k])
        if missing or fmt:
            bad = 1
    return bad


if __name__ == '__main__':
    sys.exit(main())
