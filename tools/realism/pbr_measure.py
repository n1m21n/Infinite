# Measure what makes a scanned PBR set read as real: albedo level/variation, roughness spread, how height/AO
# drive albedo and roughness (cavity dirt, worn tips), and normal-map slope energy per octave of scale.
# usage: python3 tools/realism/pbr_measure.py DIR   (DIR from pbr_fetch.py)

import sys,os,numpy as np
from PIL import Image
root=sys.argv[1]
def load(p,mode='L'):
    return np.asarray(Image.open(p).convert(mode),dtype=np.float32)/255.0
def lin(c): return np.where(c<=0.04045,c/12.92,((c+0.055)/1.055)**2.4)
def corr(a,b):
    a=a.ravel()-a.mean(); b=b.ravel()-b.mean(); return float((a*b).sum()/np.sqrt((a*a).sum()*(b*b).sum()+1e-12))
def bands(nrm):
    # slope energy of the normal map per octave (wavelength 2..512 px), share of total
    sx=nrm[...,0]*2-1; sy=nrm[...,1]*2-1
    e=[]
    for g in [sx,sy]:
        F=np.abs(np.fft.rfft2(g-g.mean()))**2
        h,w=g.shape; fy=np.fft.fftfreq(h)[:,None]; fx=np.fft.rfftfreq(w)[None,:]; r=np.sqrt(fx*fx+fy*fy)
        e.append([F[(r>=1/2**(k+1))&(r<1/2**k)].sum() for k in range(1,9)])
    e=np.array(e).sum(0); return e/e.sum()
print(f"{'set':22s} alb(lin) albCV sat | rough  rSD  | c(h,alb) c(h,r) c(ao,alb) | slope  octaves 2-4|4-16|16-64|64+px | metal")
for n in sorted(os.listdir(root)):
    d=f"{root}/{n}"
    a=lin(load(f"{d}/diff.jpg",'RGB')); L=a@np.array([.2126,.7152,.0722])
    sat=float(np.mean(a.max(-1)-a.min(-1))/max(np.mean(a.max(-1)),1e-6))
    r=load(f"{d}/rough.jpg") if os.path.exists(f"{d}/rough.jpg") else None
    h=load(f"{d}/disp.jpg"); ao=load(f"{d}/ao.jpg") if os.path.exists(f"{d}/ao.jpg") else None
    nm=load(f"{d}/nor.jpg",'RGB'); sl=float(np.mean(np.sqrt((nm[...,0]*2-1)**2+(nm[...,1]*2-1)**2)))
    b=bands(nm); oc=[b[0],b[1]+b[2],b[3]+b[4],b[5:].sum()]
    met=float((load(f"{d}/arm.jpg",'RGB')[...,2]>0.5).mean()) if os.path.exists(f"{d}/arm.jpg") else -1
    print(f"{n:22s} {L.mean():.3f} {L.std()/L.mean():5.2f} {sat:4.2f} | {r.mean():.2f} {r.std():5.3f} | {corr(h,L):+.2f} {corr(h,r):+.2f} {corr(ao,L) if ao is not None else 0:+.2f}   | {sl:.2f}  {oc[0]:.2f} {oc[1]:.2f} {oc[2]:.2f} {oc[3]:.2f} | {met:.2f}")
