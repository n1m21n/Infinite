#!/usr/bin/env python3
"""PhotoCraft's behaviour tests, as a rule ledger for the 2D View.

PhotoCraft (github.com/storytold/photocraft, MIT/Apache-2.0) is a Photoshop-parity editor with a test for almost every
command. Each test name is a behaviour rule ("with_selection_only_selected_pixels_move"). This tool reads only the test
names (no code), groups them by tool, drops the ones outside a 2D View's reach (budgets, CMYK, HDR, cross-document,
file formats), and writes a markdown ledger with one row per rule and an empty Infinite column to fill.

Usage: tools/parity/photocraft_rules.py [--src DIR] [--out FILE] [--ref main] [--all] [--map TSV] [--unresolved]
--map (default internal/plans/2d-view/parity/photocraft-map.tsv in the main checkout): rule<TAB>status<TAB>proof, where
status is ok | fixed | bug | n/a and proof names the VIEW2DTEST check ("panel: ...") or, for n/a, why.
--unresolved prints the gate: rules with no map row, bugs open, ok/fixed rows with no proof (all 0 = 100%).
--src reads a local (sparse) clone, e.g. ~/Infinite/perfhunt-state/ref/photocraft; without it the gh CLI lists the tree
and test files come from raw.githubusercontent.com (slow: one request per file).
"""
import argparse, collections, concurrent.futures, json, re, subprocess, sys, urllib.request

REPO = 'storytold/photocraft'
# Areas a 2D View should match, by path keyword -> ledger section.
AREAS = [
    ('transform', 'Transform'), ('rotate', 'Transform'), ('flip', 'Transform'), ('warp', 'Transform'), ('free_transform', 'Transform'),
    ('selection', 'Selection'), ('marquee', 'Selection'), ('lasso', 'Selection'), ('wand', 'Selection'), ('quickmask', 'Selection'),
    ('smartselect', 'Selection'), ('refine', 'Selection'),
    ('clone', 'Clone and heal'), ('heal', 'Clone and heal'), ('retouch', 'Clone and heal'), ('patch_', 'Clone and heal'),
    ('mask', 'Masks'), ('clip_group', 'Layers and groups'), ('group', 'Layers and groups'), ('layer', 'Layers and groups'),
    ('flatten', 'Layers and groups'), ('merge', 'Layers and groups'), ('thumbnail', 'Layers and groups'),
    ('crop', 'Image'), ('trim', 'Image'), ('canvas_size', 'Image'), ('image_size', 'Image'), ('image_cmds', 'Image'),
    ('brush', 'Paint'), ('paint', 'Paint'), ('eraser', 'Paint'), ('gradient', 'Paint'), ('fill', 'Paint'), ('stroke', 'Paint'),
    ('text', 'Type'), ('type_', 'Type'), ('vector', 'Vector and pen'), ('pen', 'Vector and pen'), ('shape', 'Vector and pen'),
    ('history', 'Undo'), ('undo', 'Undo'), ('move', 'Transform'), ('align', 'Transform'), ('guide', 'View'), ('zoom', 'View'),
    ('view', 'View'), ('adjust', 'Adjustments'),
]
# Rules about limits a few-user app does not need now (feedback: scale to user base).
NOISE = re.compile(r'budget|gigapixel|oversized|allocation|alloc_|cmyk|hdr|float|16bit|bit_depth|indexed|lab_|profile|cms|'
                   r'cross_document|between_documents|json|legacy|panick|performance|bench|bounded_before|atn|automate|'
                   r'plugin|psd|tiff|raw_|spot|channel|duotone|multichannel|_depth$|^session|^setup|^pixel$|^params$|'
                   r'^coverage$|^column$|^stretch$|^masked$|^mask_at$|^alpha_at$|scene$|^assert_|^old_|_window_tests|'
                   r'refuse|reject|fail_gracefully|fails_without|without_panicking', re.I)


def sh(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def area_of(path):
    p = path.lower()
    for key, name in AREAS:
        if key in p.rsplit('/', 1)[-1] or f'/{key}' in p:
            return name
    for key, name in AREAS:
        if key in p:
            return name
    return None


def fetch(ref, path):
    url = f'https://raw.githubusercontent.com/{REPO}/{ref}/{path}'
    with urllib.request.urlopen(url, timeout=30) as r:
        return r.read().decode('utf-8', 'replace')


def tests_in(text):
    # A rule = a #[test] fn (attributes may sit between).
    return re.findall(r'#\[test\][^\n]*\n(?:\s*#\[[^\n]*\n)*\s*(?:pub\s+)?fn\s+([a-z0-9_]+)\s*\(', text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='-')
    ap.add_argument('--ref', default='main')
    ap.add_argument('--src', default=None)
    ap.add_argument('--map', default=str(__import__('pathlib').Path.home() / 'Infinite/repo/internal/plans/2d-view/parity/photocraft-map.tsv'))
    ap.add_argument('--unresolved', action='store_true')
    ap.add_argument('--all', action='store_true', help='keep the rules the noise filter drops')
    a = ap.parse_args()
    if a.src:
        import pathlib
        root = pathlib.Path(a.src).expanduser()
        tree = [{'type': 'blob', 'path': str(f.relative_to(root))} for f in root.rglob('*.rs')]
        read = lambda p: (root / p).read_text('utf-8', 'replace')
    else:
        tree = json.loads(sh(['gh', 'api', f'repos/{REPO}/git/trees/{a.ref}?recursive=1']))['tree']
        read = lambda p: fetch(a.ref, p)
    paths = [t['path'] for t in tree if t['type'] == 'blob' and t['path'].endswith('.rs')
             and t['path'].startswith(('crates/engine/', 'crates/algo/', 'crates/compose/', 'crates/paint/', 'crates/vector/',
                                       'crates/raster/', 'crates/doc/', 'crates/text/'))]
    paths = [p for p in paths if area_of(p)]
    rules = collections.defaultdict(list)   # area -> [(rule, file)]
    with concurrent.futures.ThreadPoolExecutor(12) as ex:
        for path, text in zip(paths, ex.map(read, paths)):
            for name in tests_in(text):
                if a.all or not NOISE.search(name):
                    rules[area_of(path)].append((name, path))
    order = list(dict.fromkeys(n for _, n in AREAS))
    mp = {}
    try:
        for line in open(a.map):
            f = line.rstrip('\n').split('\t')
            if len(f) >= 2 and not line.startswith('#'):
                mp[f[0]] = (f[1], f[2] if len(f) > 2 else '')
    except FileNotFoundError:
        pass
    if a.unresolved:
        names = {n for rows in rules.values() for n, _ in rows}
        unmapped = sorted(n for n in names if n not in mp)
        bugs = sorted(n for n in names if mp.get(n, ('',))[0] == 'bug')
        unproven = sorted(n for n in names if mp.get(n, ('', ''))[0] in ('ok', 'fixed') and not mp[n][1])
        done = sum(1 for n in names if mp.get(n, ('',))[0] in ('ok', 'fixed', 'n/a') and (mp[n][0] == 'n/a' or mp[n][1]))
        print(f'{done}/{len(names)} rules settled ({100.0 * done / max(1, len(names)):.1f}%); '
              f'unmapped {len(unmapped)}, open bugs {len(bugs)}, ok without proof {len(unproven)}')
        for area in order:
            an = {n for n, _ in rules.get(area, [])}
            if an:
                ad = sum(1 for n in an if n in mp and mp[n][0] in ('ok', 'fixed', 'n/a') and (mp[n][0] == 'n/a' or mp[n][1]))
                print(f'  {area}: {ad}/{len(an)}')
        sys.exit(0 if not unmapped and not bugs and not unproven else 1)
    out = ['# PhotoCraft behaviour rules for the 2D View', '',
           f'Source: github.com/{REPO} ({a.ref}), MIT/Apache-2.0. Test names only, read as rules; no code copied.',
           'Infinite column: ok = behaves the same (with the VIEW2DTEST check that proves it), bug = exists but wrong,',
           'missing = not built, n/a = outside the 2D View.', '']
    total = 0
    for area in order:
        rows = sorted(set(rules.get(area, [])))
        if not rows:
            continue
        total += len(rows)
        out += [f'## {area} ({len(rows)})', '', '| Rule | PhotoCraft file | Infinite |', '|---|---|---|']
        out += [f'| {n.replace("_", " ")} | `{p.split("crates/", 1)[-1]}` | {" ".join(mp.get(n, ("", ""))).strip()} |' for n, p in rows]
        out.append('')
    out.insert(2, f'{total} rules after the noise filter.')
    text = '\n'.join(out) + '\n'
    if a.out == '-':
        sys.stdout.write(text)
    else:
        open(a.out, 'w').write(text)
        print(f'{total} rules -> {a.out}')


if __name__ == '__main__':
    main()
