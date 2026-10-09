# Contact sheets for one category: sheet.py <Cat> <c|o>
import sys, os
from PIL import Image, ImageDraw
OUT = os.environ.get('OUT', '/tmp/node-gallery')
cat, st = sys.argv[1], sys.argv[2]
W, H, pad = 3200, 2000, 24
files = [l.strip() for l in open(OUT + '/one-index.txt') if '/%s__' % cat in l and l.strip().endswith('__%s.png' % st)]
ims = []
for f in files:
    if not os.path.exists(f): continue
    im = Image.open(f)
    s = min(1.0, (H - 60) / im.height, (W - 2 * pad) / im.width)
    if s < 1: im = im.resize((int(im.width * s), int(im.height * s)), Image.LANCZOS)
    ims.append((os.path.basename(f).split('__')[1], im))
sheets, cur, x, y, rowh = [], [], pad, pad, 0
for name, im in ims:
    if x + im.width > W - pad: x = pad; y += rowh + pad + 30; rowh = 0
    if y + im.height + 30 > H and cur: sheets.append(cur); cur = []; x = pad; y = pad; rowh = 0
    cur.append((name, im, x, y)); x += im.width + pad; rowh = max(rowh, im.height)
if cur: sheets.append(cur)
os.makedirs(OUT + '/sheets', exist_ok=True)
for i, sh in enumerate(sheets):
    c = Image.new('RGB', (W, H), (40, 40, 48)); d = ImageDraw.Draw(c)
    for name, im, x, y in sh:
        c.paste(im, (x, y + 30)); d.text((x, y + 4), name, fill=(255, 200, 80))
    p = '%s/sheets/%s-%s-%d.png' % (OUT, cat, st, i); c.save(p); print(p, [n for n, _, _, _ in sh])
