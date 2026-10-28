#!/usr/bin/env python3
"""Python copy of SampleStyles.cpp ProposeGroove and the drum step pass (ghosts, fills), to try a critic or generator
change against B4 (realism AUC) and B5 (card escape) before a C++ rebuild.
    groove_sim.py [--seeds 20]
Not bit-exact with the app (its own RNG, no per-part lane picks), so read differences between variants, not the
absolute numbers: on 2026-10-27 the copy scored AUC 0.857 where the app dump scored 0.774. Plug a variant into
run(score=..., gen=...): score(m, card, cdist, sync, surprise, ll) -> float, gen(rng, masks, complexity, fourFloor)."""
import re, os, sys, math, random, json, statistics as st
sys.path.insert(0, os.path.expanduser("~/Infinite/repo/tools/universe"))
import benchmark as B, critic as CR, learn as L
R = os.path.expanduser("~/Infinite/repo/src/app/graph/")
tab = open(R + "GenerateSamplesTables.inc").read()
CARDS = []
for m in re.finditer(r'\{ "([a-z0-9_]+)", "[^"]*", "([a-z_]+)", \d+, \d+, ([\d.]+)f, "[^"]*", \{ ((?:"[^"]*",? ?){8}) \}, "[^"]*", "[^"]*", "[^"]*", "[^"]*", [\d.]+f, (?:true|false), "[^"]*", ([\d.]+)f, ([\d.]+)f', tab):
    CARDS.append(dict(id=m[1], family=m[2], swing=float(m[3]), lanes=re.findall(r'"([^"]*)"', m[4]), groove=float(m[5]), fill=float(m[6])))
crit = open(R + "GenerateSamplesCritic.inc").read()
num = lambda s: [float(x) for x in re.findall(r"-?\d+\.\d+", s)]
FLOOR, MID, HI = [float(re.search(r"%s\s*=\s*(-?[\d.]+)f" % k, crit)[1]) for k in ("kDrumFloor", "kDrumMidLl", "kDrumHiLl")]
sp = num(crit.split("kDrumStepP[8][16] = {")[1].split("};")[0])
STEPP = [sp[l * 16:(l + 1) * 16] for l in range(8)]
cl = [int(x, 16) for x in re.findall(r"0x([0-9a-f]{4})u", crit.split("kDrumClusters[32][8] = {")[1].split("};")[0])]
CLUST = [cl[i * 8:(i + 1) * 8] for i in range(32)]
DIRS = {}  # id -> (complexity, pocket): the 26th and 27th floats of each StyleDirective row
for ln in crit.split("kStyleDirectives[]")[-1].split("\n"):
    m = re.match(r'\s*\{ "([a-z0-9_]+)",(.*)', ln)
    if m:
        f = [float(x) for x in re.findall(r"-?\d+\.\d+(?=f)", m[2])]
        if len(f) >= 27:
            DIRS[m[1]] = f[25:27]
pc = lambda m: bin(m).count("1")
def mask(s): return sum(1 << i for i, c in enumerate(s[:16]) if c != ".")
def lane_of(m, g=0): return "" if m == 0 else "".join(("o" if g >> i & 1 else "x") if m >> i & 1 else "." for i in range(16))
def eucl(k, rot):
    if k <= 0: return 0
    if k >= 16: return 0xffff
    return sum(1 << ((i + rot) & 15) for i in range(16) if (i * k) % 16 < k)
def maxrun(m):
    b = c = 0
    for i in range(16):
        c = c + 1 if m >> i & 1 else 0; b = max(b, c)
    return b
KMETW = [4, 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1]
def judge(m, card, ambient, real=None):
    act = tot = 0
    for l in range(8):
        h = pc(m[l])
        if h: act += 1; tot += h
        if l not in (2, 3) and (h > 12 or maxrun(m[l]) > 8): return None
    if tot == 0: return None
    if tot / max(48, act * 16) > 0.6 or tot / (act * 16) < 0.05: return None
    kh, sh = pc(m[0]), pc(m[1])
    if not ambient and kh == 0: return None
    if not ambient and kh >= 4 and (m[0] & 0x5555) == 0: return None
    if kh >= 5 and sh >= 5 and m[0] == m[1]: return None
    ll = 0.0
    for l in range(8):
        for s in range(16):
            p = min(max(STEPP[l][s], 1e-4), 1 - 1e-4)
            ll += math.log2(p if m[l] >> s & 1 else 1 - p)
    ll /= 128
    if ll < FLOOR: return None
    sync = syt = 0.0
    for l in (0, 1, 4, 5, 6):
        for s in range(16):
            if m[l] >> s & 1:
                syt += 1
                if KMETW[s] <= 2 and not (m[l] >> ((s + 1) & 15) & 1): sync += (3 - KMETW[s]) * 0.5
    anchor = 1.0 if (m[0] & 1 or m[0] & 256) else 0.4
    sy = min(1.0, sync / max(1.0, syt) * anchor)
    diff = sum(pc(m[l] ^ card[l]) for l in range(8))
    cd = min(1.0, diff / 18)
    surp = (min(max(0.5 + 0.5 * (ll - FLOOR) / max(1e-4, MID - FLOOR), 0.2), 1.0) if ll <= MID else
            min(max(1 - 0.7 * (ll - MID) / max(1e-4, HI - MID), 0.15), 1.0))
    return (real or default_score)(m, card, cd, sy, surp, ll), sy
def default_score(m, card, cd, sy, surp, ll): return 0.38 * cd + 0.35 * sy + 0.27 * surp
CUR = [None]  # the card being proposed for, so a generator variant can read its family
def propose(cr, card, score=None, gen=None):
    CUR[0] = card
    d = DIRS.get(card["id"]); intel = d[0] if d else 0.5; pocket = d[1] if d else 0.5
    base = [mask(s) for s in card["lanes"]]
    ghost = [sum(1 << i for i, c in enumerate(s[:16]) if c == "o") for s in card["lanes"]]
    amb = card["family"] == "ambient"; ff = card["family"] in ("house", "techno", "trance", "hard")
    best = list(base); r = judge(base, base, amb, score); bs = r[0] if r else -1.0
    for cand in range(8):
        m = list(base)
        (gen or default_gen)(cr, m, intel, ff)
        if not amb and m[0] == 0: m[0] = 0x0101
        r = judge(m, base, amb, score)
        if r:
            sc = r[0] + 0.10 * r[1] * pocket
            if sc > bs: bs, best = sc, m
    return [lane_of(best[l], ghost[l]) for l in range(8)]
def default_gen(cr, m, intel, ff):
    mode = 1 if (intel > 0.65 and cr.random() < 0.45) else cr.randrange(3)
    c = cr.randrange(32)
    if mode == 0:
        if not ff and CLUST[c][0] & 1: m[0] = CLUST[c][0]
        elif ff and cr.random() < 0.65:
            m[0] = 0x1111 | (1 << (14 if cr.randrange(2) else 15)) | ((1 << 6) if cr.random() < 0.4 else 0)
        m[2] = CLUST[c][2] or eucl(cr.randint(5, 11), cr.randrange(4))
        if cr.random() < 0.5: m[5] = CLUST[c][5] & 0x6c6c
    elif mode == 1:
        m[2] = eucl(cr.randint(5, 13), cr.randrange(4))
        if m[3] == 0 and cr.random() < 0.6: m[3] = 0x4444
        if m[1]: m[1] |= 1 << (7 if cr.randrange(2) else 15)
        if cr.random() < 0.45 + 0.25 * intel: m[5] = eucl(cr.randint(2, 5), cr.randrange(8))
    else:
        for _ in range(3):
            l = cr.randrange(6); s = 1 + cr.randrange(15)
            if l in (0, 1) and s in (0, 4, 8, 12) and ff: continue
            m[l] ^= 1 << s
def bars(card, lanes, rng):
    """the step pass: ghosts on odd steps of lanes 1-4, a 2-4 step fill; two bars out"""
    fill_i = next((i for i in (1, 4, 5, 2) if lanes[i]), None)
    fn = 2 + rng.randrange(3) if fill_i is not None and rng.random() < card["fill"] else 0
    steps = [[0.0] * 32 for _ in range(8)]
    for i in range(8):
        pat = lanes[i]
        for k in range(32):
            c = pat[k % 16] if pat else "."
            v = 1.0 if c == "x" else 0.45 if c == "o" else 0.0
            if v == 0 and pat and i in (1, 2, 3, 4) and k % 2 == 1 and rng.random() < 0.22 * card["groove"]: v = 0.2
            if fn and k >= 32 - fn:
                if i == fill_i: v = 0.8
                elif i == 0 and k == 31: v = 0.0
            steps[i][k] = v
    out = []
    for b in range(2):
        out.append({"lanes": ["".join("x" if steps[i][b * 16 + k] else "." for k in range(16)) if any(steps[i]) else "" for i in range(8)],
                    "vel": [""] * 8, "off": [""] * 8, "v": [steps[i][b * 16:(b + 1) * 16] for i in range(8)]})
    return out
def run(score=None, gen=None, seeds=20, skip_propose=False):
    out = []
    for c in CARDS:
        if c["family"] == "ambient": continue
        for s in range(seeds):
            cr = random.Random("%s:%d" % (c["id"], s))
            lanes = c["lanes"] if skip_propose else propose(cr, c, score, gen)
            out += bars(c, lanes, cr)
    return out
_REAL = []
def real_feats():
    """B4's real side: 250 held-out drum bars, drawn the way benchmark.py draws them"""
    if not _REAL:
        rows = L.load("~/Infinite/training-data/store", "shippable")
        _, test = L.split([r for r in rows if not r.get("synthetic")])
        d = [p for p in test if p["type"] == "drum_groove"]
        _REAL.extend(B.struct_features_drum(p) for p in random.Random(101).sample(d, min(1000, len(d)))[:250])
    return _REAL
def auc(gen):
    G = [B.struct_features_drum(p) for p in random.Random(7).sample(gen, min(250, len(gen)))]
    return round(B.cross_val_auc(real_feats(), G), 3), [round(st.mean(x[i] for x in G), 2) for i in range(6)]
def escape(gen):
    return round(100.0 * sum(CR.min_card_dist_drum(L.cells(p)) >= 0.25 for p in gen) / len(gen), 1)
if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", type=int, default=20)
    a = ap.parse_args()
    print("%d cards, %d directives; features: kick, snare, hats, kicks on beats, longest run, syncopation" % (len(CARDS), len(DIRS)))
    print("real          ", [round(st.mean(x[i] for x in real_feats()), 2) for i in range(6)])
    app = B.app_drum_bars("~/Infinite/training-data/renders/app_drums.jsonl")
    for name, gen in (("app dump", app), ("cards + pass", run(seeds=a.seeds, skip_propose=True)), ("copy", run(seeds=a.seeds))):
        print("%-14s AUC %.3f %s  escape %.1f%%" % (name, *auc(gen), escape(gen)))
