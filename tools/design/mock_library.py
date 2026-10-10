#!/usr/bin/env python3
"""Real-metrics Library panel mockup, current vs proposed (Inter + infinite-glyphs.ttf, token sizes).
usage: mock_library.py <out.png>   (3 px/pt, panel 280 pt wide, 560 pt tall)"""
import json, sys
from PIL import Image, ImageDraw, ImageFont
S = 3; PW = 280; H = 560; GAP = 40
cp = json.load(open('art/icons/codepoints.json'))
inter = lambda pt, w='Regular': ImageFont.truetype(f'external/fonts/Inter/Inter-{w}.ttf', int(pt*S))
gl = lambda pt: ImageFont.truetype('external/icons/Infinite/infinite-glyphs.ttf', int(pt*S))
BG=(10,11,15); PANEL=(24,27,36); FIELD=(33,37,48); HOV=(38,42,54); TXT=(238,240,246); DIM=(128,134,148)
ACC=(74,120,180); LINE=(44,48,60); DOT=(150,120,210)
im = Image.new('RGB', ((PW*2+GAP*3)*S, (H+60)*S), BG); d = ImageDraw.Draw(im)
def rr(x0,y0,x1,y1,r,fill,ox=0): d.rounded_rectangle([(ox+x0)*S,y0*S,(ox+x1)*S,y1*S], r*S, fill=fill)
def text(x,cy,s,col=TXT,pt=13,w='Regular',a='l',ox=0): d.text(((ox+x)*S,cy*S),s,font=inter(pt,w),fill=col,anchor=a+'m')
def tw(s,pt=13,w='Regular'): return d.textlength(s,font=inter(pt,w))/S
def glyph(cx,cy,name,col=TXT,size=16,ox=0): d.text(((ox+cx)*S,(cy+0.3*size)*S),chr(cp[name]),font=gl(size),fill=col,anchor='ms')
names=["note strum","bouncing balls","note capturer","note stack","chorder","midi file","random note generator","note sequencer","arpeggiator","note switcher","note merge","note router"]
TABS=["Modules","Field","Samples","Media","Plugins"]
# ---- current
ox=GAP; text(0,24,"CURRENT",DIM,11,'Medium',ox=ox)
rr(0,40,PW,H+40,0,PANEL,ox)
x=8
for i,t in enumerate(TABS):
    w=tw(t)+12
    if i==0: rr(x,48,x+w,70,4,ACC,ox)
    text(x+w/2,59,t,TXT,13,a='m',ox=ox); x+=w+6
d.line([(ox)*S,76*S,(ox+PW)*S,76*S],fill=LINE,width=S)
rr(8,84,PW-8,106,4,FIELD,ox); text(14,95,"search modules...",DIM,13,ox=ox)
rr(8,112,128,134,4,FIELD,ox); text(68,123,"Category",TXT,13,a='m',ox=ox)
rr(134,112,152,134,4,FIELD,ox); rr(158,112,PW-8,134,4,FIELD,ox); text(158+(PW-166)/2,123,"notes",TXT,13,a='m',ox=ox)
text(8,152,"— notes ————————",DIM,13,ox=ox)
for i,n in enumerate(names): text(8,176+i*22,n,TXT,13,ox=ox)
# ---- proposed
ox=GAP*2+PW; text(0,24,"PROPOSED",DIM,11,'Medium',ox=ox)
rr(0,40,PW,H+40,0,PANEL,ox)
# header: title + count of modes
text(12,60,"Library",TXT,15,'SemiBold',ox=ox)
# segmented tabs (sliding pill), 5 equal segments
sx0,sx1,sy0,sy1=12,PW-12,76,102
rr(sx0,sy0,sx1,sy1,8,FIELD,ox)
seg=(sx1-sx0-4)/5
rr(sx0+2,sy0+2,sx0+2+seg,sy1-2,6,ACC,ox)
for i,t in enumerate(TABS):
    text(sx0+2+seg*i+seg/2,(sy0+sy1)/2,t,(255,255,255) if i==0 else DIM,12,'Medium' if i==0 else 'Regular',a='m',ox=ox)
# search field 32pt with glyph + shortcut hint
rr(12,112,PW-12,144,8,FIELD,ox); glyph(26,128,'search',DIM,16,ox=ox); text(40,128,"Search modules",DIM,13,ox=ox)
rr(PW-46,120,PW-18,136,4,HOV,ox); text(PW-32,128,"⌘F",DIM,11,a='m',ox=ox)
# filter chips
def chip(x0,x1,label,val=None):
    rr(x0,154,x1,178,8,None or (30,34,44),ox); text(x0+10,166,label,DIM,12,ox=ox)
    if val: text(x0+10+tw(label,12)+6,166,val,TXT,12,ox=ox)
    glyph(x1-12,166,'chevron-down',DIM,12,ox=ox)
chip(12,132,"Sort","Category"); chip(138,PW-12,"Filter","All")
# section header
text(12,198,"NOTES",DIM,11,'SemiBold',ox=ox); text(PW-12,198,"21",DIM,11,a='r',ox=ox)
d.line([(ox+12)*S,208*S,(ox+PW-12)*S,208*S],fill=LINE,width=S)
for i,n in enumerate(names):
    cy=224+i*28
    if i==2: rr(6,cy-13,PW-6,cy+13,6,HOV,ox)
    d.ellipse([(ox+16)*S,(cy-3)*S,(ox+22)*S,(cy+3)*S],fill=DOT)
    text(32,cy,n,TXT,13,ox=ox)
    if i==2: glyph(PW-22,cy,'plus',TXT,16,ox=ox)
    elif i in (0,4): glyph(PW-22,cy,'star-fill',(220,180,70),14,ox=ox)
im.save(sys.argv[1])
