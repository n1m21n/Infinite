#!/usr/bin/env python3
"""Re-check ledger n/a rows whose reason was a broad tag, not a per-row reason.

Owner 2026-11-27: "not a Type or Vector feature" only scoped the first 2D View version, and the "engine-internal" and
"redraw bookkeeping" tags hid user-visible rules (brush preset size, dab spacing, Background Eraser parity). Cross-document
features are wanted (the 2D View switches documents), so those rows go back to missing.

Allowlist, not a pattern grep: a row stays n/a only when its rule name holds a token from INTERNAL (plumbing a user
can never see: cancellation, tiling, worker counts, caches, scratch memory, command-API journals). Every other row is
listed for review, with the PhotoCraft test file it comes from (names and paths only).

Usage: tools/parity/na_recheck.py [--map TSV] [--src DIR] [--out TSV]
       tools/parity/na_recheck.py --apply DECISIONS.tsv   (rule<TAB>status<TAB>reason, read by hand; rewrites the map rows)
"""
import argparse, collections, pathlib, re

HOME = pathlib.Path.home()
TAGS = {'not a Type or Vector feature': 'scope', 'PhotoCraft engine-internal test': 'internal',
        'damage/redraw bookkeeping': 'redraw', 'cross-document copy': 'crossdoc'}
# Rule-name tokens that only plumbing has. Review each before adding: the token must never name something a user sees.
INTERNAL = {'cancel', 'cancelled', 'cancellation', 'cancels', 'tile', 'tiles', 'tiled', 'tiling', 'worker', 'workers',
            'thread', 'threads', 'parallel', 'fft', 'scratch', 'cache', 'cached', 'caches', 'publish', 'publishing',
            'published', 'journal', 'revision', 'revisions', 'workload', 'registered', 'prepared', 'preparation',
            'frozen', 'reference', 'damage', 'dirty', 'invalidates', 'invalidate', 'rebuild', 'rebuilds', 'reuse',
            'reuses', 'reused', 'allocation', 'memory', 'bytes', 'generation', 'snapshot', 'snapshots', 'deterministic',
            'stable', 'idempotent', 'serial', 'batch', 'batched', 'chunk', 'chunks', 'budget', 'stale', 'epoch'}


def rule_files(src):
    """rule name -> PhotoCraft test file (relative), from the local clone."""
    out = {}
    pat = re.compile(r'#\[test\][^\n]*\n(?:\s*#\[[^\n]*\n)*\s*(?:pub\s+)?fn\s+([a-z0-9_]+)\s*\(')
    for f in src.rglob('*.rs'):
        try:
            t = f.read_text(errors='ignore')
        except OSError:
            continue
        if '#[test]' not in t:
            continue
        for n in pat.findall(t):
            out.setdefault(n, str(f.relative_to(src)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', default=str(HOME / 'Infinite/repo/internal/plans/2d-view/parity/photocraft-map.tsv'))
    ap.add_argument('--src', default=str(HOME / 'Infinite/perfhunt-state/ref/photocraft'))
    ap.add_argument('--out', default='-')
    ap.add_argument('--apply', default=None)
    a = ap.parse_args()
    if a.apply:
        want = {}
        for line in pathlib.Path(a.apply).read_text().splitlines():
            if line.strip() and not line.startswith('#'):
                r, st, why = line.split('\t', 2)
                want[r] = (st, why)
        mp = pathlib.Path(a.map)
        out, n = [], collections.Counter()
        for line in mp.read_text().splitlines():
            f = line.split('\t')
            if not line.startswith('#') and len(f) >= 3 and f[0] in want and f[1] == 'n/a':
                st, why = want.pop(f[0])
                line = f'{f[0]}\t{st}\t{why}'
                n[st] += 1
            out.append(line)
        mp.write_text('\n'.join(out) + '\n')
        print('applied', dict(n), '; not found or not n/a:', sorted(want))
        return
    files = rule_files(pathlib.Path(a.src))
    rows, count = [], collections.Counter()
    for line in pathlib.Path(a.map).read_text().splitlines():
        f = line.split('\t')
        if line.startswith('#') or len(f) < 3 or f[1] != 'n/a':
            continue
        tag = next((v for k, v in TAGS.items() if f[2].startswith(k)), None)
        if not tag:
            continue
        words = set(f[0].split('_'))
        hit = sorted(words & INTERNAL)
        verdict = 'missing' if tag == 'crossdoc' else ('internal' if hit else 'review')
        count[(tag, verdict)] += 1
        rows.append((tag, verdict, f[0], files.get(f[0], '?'), ','.join(hit)))
    text = '\n'.join('\t'.join(r) for r in sorted(rows)) + '\n'
    if a.out == '-':
        print(text, end='')
    else:
        pathlib.Path(a.out).write_text(text)
    for (t, v), n in sorted(count.items()):
        print(f'# {t:9s} {v:9s} {n}')


if __name__ == '__main__':
    main()
