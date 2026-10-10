#!/usr/bin/env python3
"""Real-metrics top bar mockup: Inter + infinite-glyphs.ttf, token sizes, 1600pt wide.
usage: mock_topbar.py <out_prefix>   -> <prefix>_left.png, <prefix>_right.png (3 px/pt)"""
import json, sys
from PIL import Image, ImageDraw, ImageFont
S = 3; W = 1600; H = 30
cp = json.load(open('art/icons/codepoints.json'))
inter = lambda pt, w='Regular': ImageFont.truetype(f'external/fonts/Inter/Inter-{w}.ttf', int(pt*S))
gl = lambda pt: ImageFont.truetype('external/icons/Infinite/infinite-glyphs.ttf', int(pt*S))
BG=(10,11,15); PILL=(22,24,31); HOV=(34,37,46); TXT=(238,240,246); DIM=(128,134,148)
ACC=(74,120,180); GRN=(24,170,110); LINE=(40,43,52)
ICON=18; TILE=26; GAP=2; GROUP_GAP=10; R=6
im = Image.new('RGB', (W*S, H*S), BG); d = ImageDraw.Draw(im)
def rr(x0,y0,x1,y1,r,fill): d.rounded_rectangle([x0*S,y0*S,x1*S,y1*S], r*S, fill=fill)
def glyph(cx, cy, name, col=TXT, size=ICON):
    f = gl(size); d.text((cx*S, (cy+0.3*size)*S), chr(cp[name]), font=f, fill=col, anchor='ms')
def text(x, cy, s, col=TXT, pt=13, w='Regular', anchor='l'):
    d.text((x*S, cy*S), s, font=inter(pt,w), fill=col, anchor=anchor+'m')
def twidth(s, pt=13, w='Regular'): return d.textlength(s, font=inter(pt,w))/S
cy = H/2
def group(x, items):
    """items: list of ('tile', glyph, state) -> returns end x"""
    w = len(items)*TILE + (len(items)-1)*GAP + 4
    rr(x, cy-TILE/2-2, x+w, cy+TILE/2+2, 8, PILL)
    tx = x+2
    for g, st in items:
        if st in ('on','green'):
            rr(tx, cy-TILE/2, tx+TILE, cy+TILE/2, R, ACC if st=='on' else GRN)
        glyph(tx+TILE/2, cy, g, (255,255,255) if st in ('on','green') else TXT)
        tx += TILE+GAP
    return x+w
x = 10
for m in ('File','Edit','Menu'):
    text(x, cy, m); x += twidth(m)+14
x += 6
x = group(x, [('power', 'off'), ('rewind','off'), ('play-fill','off')]) + GROUP_GAP
# display box
parts = [('bar', '001.1'), ('tempo','120.0'), ('sig','4/4'), ('key','C Major')]
bw = 430; rr(x, cy-TILE/2-2, x+bw, cy+TILE/2+2, 8, PILL)
px = x+16
for i,(lab,val) in enumerate(parts):
    text(px, cy-4, val, TXT, 14, 'Medium')
    text(px, cy+8, {'bar':'BAR · BEAT','tempo':'BPM','sig':'SIG','key':'KEY'}[lab], DIM, 8)
    wv = max(twidth(val,14,'Medium'), twidth({'bar':'BAR · BEAT','tempo':'BPM','sig':'SIG','key':'KEY'}[lab],8))
    px += wv+16
    if i < len(parts)-1:
        d.line([px*S-8*S, (cy-9)*S, px*S-8*S, (cy+9)*S], fill=LINE, width=S)
glyph(x+bw-20, cy, 'metronome', DIM)
x += bw + GROUP_GAP
text(x+4, cy, '51 fps   19.6 ms   cpu --', DIM, 12)
# right cluster
rx = W-10
sw = twidth('search',13)+TILE+14
text(rx-sw+TILE+8, cy, 'search', TXT); glyph(rx-sw+TILE/2, cy, 'search', TXT)
rx -= sw+GROUP_GAP
items = [('viewport','off'),('grid-dots','off'),('perform','off'),('cube','on')]
gw = len(items)*TILE+(len(items)-1)*GAP+4
group(rx-gw, items)
im.crop((0,0,800*S,H*S)).save(sys.argv[1]+'_left.png')
im.crop((800*S,0,W*S,H*S)).save(sys.argv[1]+'_right.png')
