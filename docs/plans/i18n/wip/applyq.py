import sys,os,glob
sys.path.insert(0,'/Users/namansoni/infinte/tools/i18n')
import extract
os.chdir('/Users/namansoni/infinte')
S='/private/tmp/claude-501/-Users-namansoni-infinte/bb6fbb4f-a0cb-4962-beae-f4b1de38059f/scratchpad/tr'
keys=[l.rstrip('\n') for l in open(S+'/missing3.txt',encoding='utf-8')]
L=['es','de','zh','ja','ru']
have={c:set(extract.load_table(c) or {}) for c in L}
add={c:[] for c in L}
for f in sorted(glob.glob(S+'/q*.txt')):
    for l in open(f,encoding='utf-8'):
        l=l.rstrip('\n')
        if not l.strip(): continue
        p=l.split('¦'); assert len(p)==6,(l[:40],len(p))
        k=keys[int(p[0])-1]; ku=extract.tsv_unesc(k)
        for i,c in enumerate(L):
            if ku not in have[c]:
                have[c].add(ku); add[c].append(k+'\t'+p[i+1])
for c in L:
    with open(f'resources/lang/{c}.tsv','a',encoding='utf-8') as o:
        if add[c]: o.write('\n'.join(add[c])+'\n')
    print(c,len(add[c]))
