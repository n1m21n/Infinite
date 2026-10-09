# widen.py sizes.txt: nodes whose width grows when params open, worst first.
import sys
w = {}
for l in open(sys.argv[1]):
    p = l.strip().split('|')
    if len(p) < 5: continue
    cat, op, name, width = p[0], p[1], p[2], float(p[3])
    w.setdefault((cat, name), {})[op] = width
rows = sorted(((v['1'] - v['0'], c, n) for (c, n), v in w.items() if '0' in v and '1' in v and v['1'] > v['0'] + 0.5), reverse=True)
for d, c, n in rows: print('%+6.0f  %s | %s' % (d, c, n))
print('%d of %d nodes widen' % (len(rows), len(w)))
