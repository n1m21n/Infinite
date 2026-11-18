#!/usr/bin/env python3
"""PhotoCraft's command surface (menus, shortcuts, context menus, open UX gaps) as a UX ledger for the 2D View.

photocraft_rules.py covers behaviour (test names); this covers what a user reaches: every command with its menu path
and default shortcut, the shell's own commands and tools, the context-menu contract rows and the gaps file's UI/UX
sections. Reads only declarations and docs (ids, labels, menus, keys), never implementation code.

Usage: tools/parity/ux_rules.py --src DIR [--out FILE] [--map TSV] [--unresolved] [--all]
--map (default internal/plans/2d-view/parity/ux-map.tsv in the main checkout): key<TAB>status<TAB>proof, status
ok | fixed | bug | missing | n/a; proof names the VIEW2DTEST check or, for n/a / missing, why.
--unresolved prints the gate (rules without a map row, open bugs, ok/fixed rows without proof).
By default only the menus a 2D View has (File, Edit, Image, Layer, Select, Filter, View, Type) and rows with a shortcut.
"""
import argparse, collections, pathlib, re, sys

CMD = re.compile(r'\b(?:cmd|spec)!\(\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,\s*&?\[([^\]]*)\]\s*,\s*(None|Some\("([^"]*)"\))?', re.S)
STRUCT = re.compile(r'\bid:\s*"([^"]+)"\s*,\s*label:\s*"([^"]*)"\s*,\s*menu:\s*&\[([^\]]*)\]\s*,'
                    r'(?:\s*\w+:\s*[^,]*,)*?\s*shortcut:\s*(None|Some\("([^"]*)"\))', re.S)
SHELL = re.compile(r'\(\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,\s*&\[([^\]]*)\]\s*,\s*(None|Some\("([^"]*)"\))\s*\)')
MENUS_2D = {'File', 'Edit', 'Image', 'Layer', 'Select', 'Filter', 'View', 'Type'}
NOISE = re.compile(r'cmyk|lab|profile|print|3d|video|timeline|actions?\b|droplet|batch|plug-?in|camera raw|artboard|'
                   r'variables|automate|scripts|generative|neural|sky|share|cloud|libraries|spot|duotone|indexed|16|32',
                   re.I)


def strs(s):
    return [m for m in re.findall(r'"([^"]*)"', s)]


def commands(src):
    out = {}
    for f in sorted((src / 'crates/engine/src').rglob('*.rs')):
        if f.name.endswith('_tests.rs') or '/tests' in str(f):
            continue
        t = f.read_text(errors='ignore')
        for rx in (CMD, STRUCT) + ((SHELL,) if f.name == 'shell_commands.rs' else ()):
            for m in rx.finditer(t):
                cid, label, menu, sc = m.group(1), m.group(2), strs(m.group(3)), m.group(5) or ''
                if '.' not in cid:
                    continue
                out.setdefault(cid, (label, menu, sc))
    return out


def doc_rows(path, heads):
    """Table rows (first cell) under the given ## / ### headings."""
    rows, on = [], False
    if not path.exists():
        return rows
    for line in path.read_text(errors='ignore').splitlines():
        if line.startswith('#'):
            on = any(h in line for h in heads)
            continue
        if on and line.startswith('|') and not re.match(r'\|\s*-', line):
            cell = line.strip('|').split('|')[0].strip()
            if cell and cell.lower() not in ('tool(s) in photocraft', 'target', 'item', 'gap', 'area'):
                rows.append(re.sub(r'\s+', ' ', cell)[:90])
        elif on and re.match(r'\s*[-*] \*\*', line):
            rows.append(re.sub(r'\s+', ' ', re.sub(r'[*`]', '', line.strip()[2:]))[:90])
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default=str(pathlib.Path.home() / 'Infinite/perfhunt-state/ref/photocraft'))
    ap.add_argument('--out', default='-')
    ap.add_argument('--map', default=str(pathlib.Path.home() / 'Infinite/repo/internal/plans/2d-view/parity/ux-map.tsv'))
    ap.add_argument('--unresolved', action='store_true')
    ap.add_argument('--all', action='store_true')
    a = ap.parse_args()
    src = pathlib.Path(a.src).expanduser()

    rules = collections.OrderedDict()  # key -> (section, text)
    for cid, (label, menu, sc) in sorted(commands(src).items()):
        top = menu[0] if menu else ''
        if not a.all and (top not in MENUS_2D or NOISE.search(cid + ' ' + label + ' ' + ' '.join(menu))):
            continue
        if not a.all and not menu and not sc:
            continue
        rules['cmd:' + cid] = ('Menu ' + (top or 'shell'), f'{" › ".join(menu + [label])}' + (f'  [{sc}]' if sc else ''))
    for i, r in enumerate(doc_rows(src / 'docs/context-menu-parity.md', ['Canvas tool matrix', 'Panel', 'Layers', 'tab'])):
        rules[f'ctx:{i:03d}'] = ('Context menus', r)
    for g in ('G3.', 'G4.', 'G5.', 'G9.'):
        for i, r in enumerate(doc_rows(src / 'docs/gaps.md', [g])):
            rules[f'gap:{g[:-1]}:{i:03d}'] = ('Gaps ' + g[:-1], r)

    mp = {}
    p = pathlib.Path(a.map).expanduser()
    if p.exists():
        for line in p.read_text().splitlines():
            parts = line.split('\t')
            if len(parts) >= 2 and not line.startswith('#'):
                mp[parts[0]] = (parts[1], parts[2] if len(parts) > 2 else '')

    if a.unresolved:
        nomap = [k for k in rules if k not in mp]
        bugs = [k for k in rules if mp.get(k, ('',))[0] == 'bug']
        noproof = [k for k in rules if mp.get(k, ('', ''))[0] in ('ok', 'fixed') and not mp[k][1]]
        missing = [k for k in rules if mp.get(k, ('',))[0] == 'missing']
        done = len(rules) - len(nomap) - len(bugs) - len(noproof)
        print(f'ux rules {len(rules)}  settled {done}  no-map {len(nomap)}  bug {len(bugs)}  no-proof {len(noproof)}'
              f'  (missing features: {len(missing)})')
        for k in (nomap + bugs + noproof)[:40]:
            print('  ', k, '|', rules[k][1])
        return 1 if (nomap or bugs or noproof) else 0

    lines, sec = ['# PhotoCraft UX ledger (2D View)', ''], None
    for k, (s, text) in rules.items():
        if s != sec:
            lines += ['', f'## {s}', '', '| key | rule | Infinite | proof |', '|---|---|---|---|']
            sec = s
        st, pr = mp.get(k, ('', ''))
        lines.append(f'| {k} | {text.replace("|", "/")} | {st} | {pr.replace("|", "/")} |')
    text = '\n'.join(lines) + '\n'
    (sys.stdout.write(text) if a.out == '-' else pathlib.Path(a.out).write_text(text))
    return 0


if __name__ == '__main__':
    sys.exit(main())
