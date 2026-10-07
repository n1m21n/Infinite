"""Append out_<lang>.txt (N¦text) to resources/lang/<lang>.tsv using keys_final.txt. Idempotent."""
import sys, os
ROOT = '/Users/namansoni/infinte'
sys.path.insert(0, ROOT + '/tools/i18n')
import extract
os.chdir(ROOT)
W = 'docs/plans/i18n/wip/'
keys = {}
for l in open(W + 'keys_final.txt', encoding='utf-8'):
    n, k = l.rstrip('\n').split('¦', 1)
    keys[int(n)] = k
for c in ['es', 'de', 'zh', 'ja', 'ru']:
    have = set(extract.load_table(c) or {})
    add = []
    got = set()
    for l in open(W + f'out_{c}.txt', encoding='utf-8'):
        l = l.rstrip('\n')
        if not l.strip():
            continue
        n, t = l.split('¦', 1)
        n = int(n); got.add(n)
        assert '\t' not in t, (c, n)
        k = keys[n]
        if extract.tsv_unesc(k) not in have:
            add.append(k + '\t' + t)
    miss = set(keys) - got
    print(c, 'adding', len(add), 'missing', sorted(miss))
    if '--write' in sys.argv and not miss and add:
        with open(f'resources/lang/{c}.tsv', 'a', encoding='utf-8') as o:
            o.write('\n'.join(add) + '\n')
