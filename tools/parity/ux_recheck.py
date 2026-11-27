#!/usr/bin/env python3
"""Re-check the UX map's `missing` rows against what the 2D View offers today.

ux_seed.py matched any string literal once, on 2026-11-18. This scanner lists every place a user can reach a command
in View2DPanel.cpp and passes a row only when its PhotoCraft label is one of them (allowlist, not a pattern grep):

  menu    a MenuParts::Item / SubMenu / Choice / Toggle line: its T("...") / L("...") labels, the literal arrays it
          indexes or loops over (1D or 2D), and the *Label() functions it calls (their literal strings)
  chip    a TextChip / ZoomChip line: its label (text before ##) and its tip's lead words ("Grow: ...", "Zoom in (Ctrl++)")
  effect  a label in kNodeEffects (the effect stack's Add sheet: Photoshop's Filter menu)

Guards against look-alikes:
  - a leaf PhotoCraft uses under more than one submenu passes only inside a SubMenu matching the PhotoCraft parent;
  - a one-word chip passes only for a two-level path (Select › Grow), never for a deeper one (Arrange › Reverse is
    not the gradient's Reverse chip), unless the row is in REVIEWED;
Only `missing` rows change; ok / n/a / fixed rows are never touched. Without --write it prints the table only.

Usage: tools/parity/ux_recheck.py [--map TSV] [--write] [--all]   (--all also lists rows that stay missing)
"""
import argparse, collections, pathlib, re, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
PANEL = ROOT / 'src/app/panels/View2DPanel.cpp'
sys.path.insert(0, str(HERE))
from ux_seed import ALIAS as _ALIAS  # noqa: E402

ALIAS = {k.lower(): v for k, v in _ALIAS.items()}
# The 2D View's word for a PhotoCraft label, where they differ (review each before adding: the item must do the same).
RENAME = {'horizontally': 'horizontal spacing', 'vertically': 'vertical spacing', '100%': '100%',
          'rotate view left': 'rotate left', 'rotate view right': 'rotate right'}
# Rows checked by hand: key -> the 2D View label that does it (one-word chips deeper than two levels).
REVIEWED = {'cmd:type.orientation.vertical': 'vertical',
            'cmd:type.openType.numerals.lining': 'lining figures', 'cmd:type.openType.numerals.oldstyle': 'oldstyle figures',
            'cmd:type.openType.numerals.proportional': 'proportional figures',
            'cmd:type.openType.numerals.tabular': 'tabular figures', 'cmd:type.openType.oldstyle': 'oldstyle figures'}
SPELL = ((r'centre', 'center'), (r'colour', 'color'), (r'grey', 'gray'))

CALL = re.compile(r'MenuParts::(Item|SubMenu|Choice|Toggle)\(|(?:Text|Zoom)Chip\(')
LITS = re.compile(r'\b[TL]\(\s*"([^"]+)"|TextChip\(\s*"([^"]+)"')
ARR_REF = re.compile(r'\b(k\w+)\s*\[')
LOOP_REF = re.compile(r'for\s*\([^:;]*:\s*(k\w+)\s*\)')
FUNC_REF = re.compile(r'\b(\w+Label)\(')
ARR_HEAD = re.compile(r'const\s+char\s*\*\s*(?:const\s+)?(k\w+)\s*(?:\[[^\]]*\])+\s*=\s*\{')
TIP = re.compile(r'T\("([A-Z][^"]{2,40}?)(?::\s|\s\()')
SUB_SPAN = 80   # lines: an item this close below a SubMenu is taken as inside it


def norm(s):
    s = s.split('##')[0]
    for a_, b_ in SPELL:
        s = re.sub(a_, b_, s, flags=re.I)
    s = re.sub(r'\s*\[.*\]$', '', s)
    return s.replace('…', '').replace('...', '').strip().lower()


def arrays(text):
    """Every literal const char* array (1D or 2D): name -> its strings, braces balanced."""
    out = {}
    for m in ARR_HEAD.finditer(text):
        i, depth = m.end(), 1
        while depth and i < len(text):
            depth += {'{': 1, '}': -1}.get(text[i], 0)
            i += 1
        out[m.group(1)] = re.findall(r'"([^"]*)"', text[m.end():i])
    return out


def label_funcs():
    """Functions named *Label in the 2D node sources: name -> the literal strings in their bodies."""
    out = {}
    for f in (ROOT / 'src/nodes').glob('Scene2D*'):
        t = f.read_text(errors='ignore')
        for m in re.finditer(r'\b(\w+Label)\s*\([^;{)]*\)\s*(?:const\s*)?\{', t):
            body = t[m.end():t.find('\n}', m.end())]
            out.setdefault(m.group(1), re.findall(r'"([^"]{2,40})"', body))
    return out


def offered():
    """label -> list of (kind, line, parent submenu label or '')."""
    text = PANEL.read_text(errors='ignore')
    lines = text.splitlines()
    arr, funcs = arrays(text), label_funcs()
    out = collections.defaultdict(list)
    subs = []  # (line, label)
    for i, line in enumerate(lines, 1):
        m = CALL.search(line)
        if not m:
            continue
        kind = 'chip' if m.group(0).endswith('Chip(') else 'menu'
        parent = next((l for (n, l) in reversed(subs) if i - n <= SUB_SPAN), '')
        tail = line[m.start():]
        labs = [a or b for a, b in LITS.findall(tail)]
        names = ARR_REF.findall(tail) + [n for k in range(max(0, i - 5), i) for n in LOOP_REF.findall(lines[k])]
        for n in names:
            labs += [s for s in arr.get(n, []) if s]
        for fn in FUNC_REF.findall(tail):
            labs += funcs.get(fn, [])
        if kind == 'chip':
            labs += TIP.findall(tail)
        for lab in labs:
            lab = norm(lab)
            if not lab:
                continue
            if m.group(1) == 'SubMenu':
                subs.append((i, lab))
            out[lab].append((kind, i, parent))
    eff = re.search(r'kNodeEffects\[\]\s*=\s*\{(.*?)\n\s*\};', text, re.S)
    if eff:
        start = text[:eff.start()].count('\n') + 1
        for j, line in enumerate(eff.group(1).splitlines()):
            m = re.match(r'\s*\{\s*"([^"]+)"\s*,\s*"([^"]*)"', line)
            if m:
                out[norm(m.group(1))].append(('effect', start + j, norm(m.group(2))))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', default=str(pathlib.Path.home() / 'Infinite/repo/internal/plans/2d-view/parity/ux-map.tsv'))
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--all', action='store_true')
    a = ap.parse_args()
    rules = re.findall(r'^\| (cmd:[^ ]+) \| (.*?) \|', subprocess.run(
        [sys.executable, str(HERE / 'ux_rules.py')], capture_output=True, text=True).stdout, re.M)
    paths = {k: [norm(p) for p in re.sub(r'\s*\[.*\]$', '', t).split(' › ')] for k, t in rules}
    leaf_parents = collections.defaultdict(set)
    for p in paths.values():
        leaf_parents[p[-1]].add(p[-2] if len(p) > 1 else '')
    have = offered()

    p = pathlib.Path(a.map).expanduser()
    head, rows = [], []
    for line in p.read_text().splitlines():
        (head if line.startswith('#') else rows).append(line)
    flipped, kept, new_rows = [], [], []
    for line in rows:
        f = line.split('\t')
        key, status = f[0], f[1] if len(f) > 1 else ''
        if status != 'missing' or key not in paths:
            new_rows.append(line)
            continue
        path = paths[key]
        leaf = norm(REVIEWED.get(key) or RENAME.get(path[-1], ALIAS.get(path[-1], path[-1])))
        parent = path[-2] if len(path) > 1 else ''
        hits = have.get(leaf, [])
        if key not in REVIEWED:
            if len(leaf_parents[path[-1]]) > 1:   # generic leaf: the parent submenu must match too
                hits = [h for h in hits if h[2] and (h[2] == parent or h[2] in parent or parent in h[2])]
            if len(path) > 2 and ' ' not in leaf:   # one-word label deep in a menu: a chip is too loose
                hits = [h for h in hits if h[0] != 'chip']
        if hits:
            kind, ln, par = hits[0]
            proof = f'{kind} present: View2DPanel.cpp:{ln}' + (f' (under {par})' if par else '') + ' [ux_recheck 2026-11-27]'
            new_rows.append(f'{key}\tok\t{proof}')
            flipped.append((key, ' › '.join(path), kind, ln, par))
        else:
            new_rows.append(line)
            kept.append((key, ' › '.join(path)))
    for k, t, kind, ln, par in flipped:
        print(f'ok       {k:50s} {t:50s} {kind}:{ln} {par}')
    if a.all:
        for k, t in kept:
            print(f'missing  {k:50s} {t}')
    print(f'{len(flipped)} missing rows now found, {len(kept)} still missing' + ('' if a.write else ' (dry run)'))
    if a.write:
        p.write_text('\n'.join(head + new_rows) + '\n')


if __name__ == '__main__':
    main()
