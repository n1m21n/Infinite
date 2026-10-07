#!/usr/bin/env python3
"""Subset Noto Sans SC / JP to the glyphs the zh/ja tables (and native names) use.

  tools/i18n/subset_fonts.py SRC_DIR     SRC_DIR holds NotoSansSC-Regular.otf and NotoSansJP-Regular.otf
Output: external/fonts/Noto/NotoSans{SC,JP}-Subset.otf  (committed; rerun when zh.tsv / ja.tsv change)
Requires: pip install fonttools
"""
import os, sys
from fontTools import subset

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'external', 'fonts', 'Noto')
NATIVE = '中文简体日本語Español Deutsch Русский'
EXTRA = list(range(0x3000, 0x3100)) + list(range(0xFF00, 0xFFF0)) + [0x2014, 0x2026, 0x00B7]


def table_chars(code):
    cps = set()
    p = os.path.join(ROOT, 'resources', 'lang', code + '.tsv')
    for line in open(p, encoding='utf-8'):
        if line.startswith('#'):
            continue
        cps.update(ord(c) for c in line if ord(c) >= 0x2E00)
    return cps


def build(src, code, name):
    cps = table_chars(code) | {ord(c) for c in NATIVE if ord(c) >= 0x2E00} | set(EXTRA)
    opt = subset.Options()
    opt.layout_features = []
    opt.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
    opt.notdef_outline = True
    opt.hinting = False
    font = subset.load_font(src, opt)
    s = subset.Subsetter(opt)
    s.populate(unicodes=sorted(cps))
    s.subset(font)
    out = os.path.join(OUT, name)
    subset.save_font(font, out, opt)
    print(f"{name}: {len(cps)} codepoints, {os.path.getsize(out)//1024} KB")


if __name__ == '__main__':
    d = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    build(os.path.join(d, 'NotoSansSC-Regular.otf'), 'zh', 'NotoSansSC-Subset.otf')
    build(os.path.join(d, 'NotoSansJP-Regular.otf'), 'ja', 'NotoSansJP-Subset.otf')
