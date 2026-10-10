# Shoot every node (or the given categories) alone, params closed and open, cropped to the node.
import subprocess, os, sys
from PIL import Image
import numpy as np
OUT = os.environ.get('OUT', '/tmp/node-gallery'); THEME = os.environ.get('THEME', 'dark')
APP = './build/Infinite.app/Contents/MacOS/Infinite'
os.makedirs(OUT + '/one', exist_ok=True)
lst = OUT + '/list.txt'
if not os.path.exists(lst):
    with open(lst, 'w') as f:
        subprocess.run([APP], env=dict(os.environ, INFINITE_NODELIST='1', INFINITE_EXITAFTER='5'), stdout=f, stderr=subprocess.DEVNULL)
nodes = [l.split(' ', 1)[1].strip().split('|', 1) for l in open(lst) if l.startswith('NODELIST ')]
want = set(sys.argv[1:])

def crop(p):
    im = Image.open(p).convert('RGB'); a = np.asarray(im).astype(int)
    reg = a[110:1990, 10:3080]
    bg = np.median(reg.reshape(-1, 3), axis=0)
    m = np.abs(reg - bg).sum(axis=2) > 60
    ys = np.where(m.sum(axis=1) > 8)[0]; xs = np.where(m.sum(axis=0) > 8)[0]
    if len(ys) == 0: return
    im.crop((xs[0] + 4, ys[0] + 104, xs[-1] + 16, ys[-1] + 116)).save(p)

index = open(OUT + '/one-index.txt', 'w')
for cat, name in nodes:
    if want and cat not in want: continue
    for st in ('c', 'o'):
        f = '%s/one/%s__%s__%s.png' % (OUT, cat, name.replace(' ', '_').replace('/', '_'), st)
        env = dict(os.environ, INFINITE_NODEGALLERY=cat + '|' + name, INFINITE_GALLERYGRID='1,100,100',
                   INFINITE_OPENPANELS=THEME, IMAGERESYNTH_SCREENSHOT=f, INFINITE_SCREENSHOT_FRAME='40')
        if st == 'o': env['INFINITE_GALLERYPARAMS'] = '1'
        subprocess.run([APP], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
        if os.path.exists(f): crop(f)
        index.write(f + '\n'); index.flush(); print(f, flush=True)
