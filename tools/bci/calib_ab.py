#!/usr/bin/env python3
"""Block A finding: per-person calibrated weights (shrinkage LDA on per-channel relative
log band power) vs today's averaged alpha. Same-session split halves + HEROIC cross-day.
usage: calib_ab.py heroic|gipsa|physionet DIR   (datasets venv)"""
import sys, numpy as np
sys.path.insert(0, '/Users/namansoni/Infinite/worktrees/bci-architecture/tools/bci')
import batch_validate as bv
ds, d = sys.argv[1], sys.argv[2]
subs = {'heroic': bv.load_heroic, 'gipsa': bv.load_gipsa, 'physionet': bv.load_physionet}[ds](d)
BANDS = [(4,8),(8,10),(10,12),(12,30)]
def F(f, P):
    # per-channel log band powers relative to channel total 2-40 Hz -> (W, 16)
    tot = P[:, :, (f>=2)&(f<=40)].sum(2)
    cols = [np.log10(P[:, :, (f>=lo)&(f<hi)].sum(2) / tot) for lo,hi in BANDS]
    return np.concatenate(cols, 1)
def avgA(f, P):
    a=(f>=8)&(f<=12); return np.log10(P.mean(1)[:, a].sum(1))
def sm(X, n):
    k=np.ones(n)/n
    return np.apply_along_axis(lambda v: np.convolve(v,k,'valid'), 0, X) if X.ndim>1 else np.convolve(X,k,'valid')
def lda(Xc, Xo, shrink=0.3):
    mu1, mu0 = Xc.mean(0), Xo.mean(0)
    S = np.cov(np.vstack([Xc-mu1, Xo-mu0]).T)
    S = (1-shrink)*S + shrink*np.trace(S)/len(S)*np.eye(len(S))
    w = np.linalg.solve(S, mu1-mu0); return w, w@(mu1+mu0)/2
def score(c, o, thr):
    return ((c>thr).sum()+(o<=thr).sum())/(len(c)+len(o))
out = {n: {'avg':[], 'lda':[]} for n in (1,3,5)}
for name, xo, xc, fs in subs:
    f, Po, _ = bv.welch_windows(xo, fs); _, Pc, _ = bv.welch_windows(xc, fs)
    ho, hc = len(Po)//2, len(Pc)//2
    for n in (1,3,5):
        # avg baseline
        ao, ac = sm(avgA(f,Po),n), sm(avgA(f,Pc),n)
        t=(ac[:hc].mean()+ao[:ho].mean())/2
        out[n]['avg'].append(score(ac[hc:], ao[ho:], t))
        Xo, Xc = sm(F(f,Po),n), sm(F(f,Pc),n)
        w, t = lda(Xc[:hc], Xo[:ho])
        out[n]['lda'].append(score(Xc[hc:]@w, Xo[ho:]@w, t))
for n in (1,3,5):
    a, l = np.array(out[n]['avg']), np.array(out[n]['lda'])
    print(f"{ds} span {n+1}s  acc avg {np.median(a):.3f} (>=80%: {100*np.mean(a>=.8):.0f}%)   "
          f"lda {np.median(l):.3f} (>=80%: {100*np.mean(l>=.8):.0f}%)   lda>avg in {100*np.mean(l>a):.0f}% of {len(a)}")

if ds == 'heroic':
    # cross-day: calibrate on a subject's first session, test on each later session
    by = {}
    for name, xo, xc, fs in subs:
        f, Po, _ = bv.welch_windows(xo, fs); _, Pc, _ = bv.welch_windows(xc, fs)
        by.setdefault(name.split('/')[0], []).append((name, f, Po, Pc))
    for n in (3, 5):
        same, cross, avg_cross = [], [], []
        for sub, ss in by.items():
            if len(ss) < 2: continue
            _, f, Po0, Pc0 = ss[0]
            w, t = lda(sm(F(f,Pc0),n), sm(F(f,Po0),n))
            ao0, ac0 = sm(avgA(f,Po0),n), sm(avgA(f,Pc0),n); ta=(ao0.mean()+ac0.mean())/2
            for _, f, Po, Pc in ss[1:]:
                Xo, Xc = sm(F(f,Po),n), sm(F(f,Pc),n)
                cross.append(score(Xc@w, Xo@w, t))
                # threshold re-centred on the new day's data (no labels: midpoint of all windows)
                proj = np.concatenate([Xc@w, Xo@w]); 
                same.append(score(Xc@w, Xo@w, np.median(proj)))
                ao, ac = sm(avgA(f,Po),n), sm(avgA(f,Pc),n)
                avg_cross.append(score(ac, ao, ta))
        c, s2, a = map(np.array, (cross, same, avg_cross))
        print(f"cross-day span {n+1}s  n={len(c)}  avg {np.median(a):.3f}  lda(day-1 weights+thr) {np.median(c):.3f}  "
              f"lda(day-1 weights, median-recentred thr) {np.median(s2):.3f} (>=80%: {100*np.mean(s2>=.8):.0f}%)")
