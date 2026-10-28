#!/usr/bin/env python3
"""Step 2h (SIM ONLY): a 'drummer' candidate generator for ProposeGroove, tried in groove_sim.py's copy.
    groove_2h.py [--seeds 6] [--midi]
Moves (each a thing real drummers do to a card): resample hat density from the style family's real hat counts,
add ghost snares on the steps real snares use, vary kick pickups, add a clap/tom lane from the family's step table,
and put a fill in the last quarter-bar from the family's tom/snare table. Stats come from the TRAIN split only
(held-out bars stay the B4 real side). Prints AUC / escape / hats / syncopation / nearest-neighbour distance."""
import os, sys, random, collections, statistics as st, argparse, struct
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import groove_sim as G, learn as L, benchmark as B, critic as CR

LANES = 8
_S = {}
def stats():
    if _S: return _S
    rows = L.load("~/Infinite/training-data/store", "shippable")
    train, test = L.split([r for r in rows if not r.get("synthetic")])
    d = [p for p in train if p["type"] == "drum_groove"]
    seen, uniq = set(), []
    for p in d:                                  # unique bars: a pattern repeated 500 times in one song is one pattern
        k = tuple(p["lanes"])
        if k not in seen: seen.add(k); uniq.append(p)
    fam = collections.defaultdict(list)
    for p in uniq:
        if p.get("family") not in ("unlabeled", "?", None): fam[p["family"]].append(p)
    fam["_all"] = uniq
    def pack(p): return [G.mask(l) if l else 0 for l in p["lanes"]]
    _S["bars"] = np.array([pack(p) for p in uniq], dtype=np.uint16)
    _S["fam"] = {}
    for f, ps in fam.items():
        n = len(ps)
        P = [[(sum(1 for p in ps if p["lanes"][l] and p["lanes"][l][s] != ".") + 0.5) / (n + 1) for s in range(16)] for l in range(LANES)]
        cnt = [[sum(c != "." for c in p["lanes"][l]) if p["lanes"][l] else 0 for p in ps] for l in range(LANES)]
        _S["fam"][f] = dict(n=n, P=P, cnt=cnt)
    # accent map: mean velocity per lane and step, from bars whose velocities are not flat
    acc = [[[] for _ in range(16)] for _ in range(LANES)]
    for p in uniq:
        v = p.get("vel")
        if not v: continue
        vals = {c for x in v for c in x if c != "."}
        if len(vals) < 3: continue
        for l in range(LANES):
            if v[l]:
                for s in range(16):
                    if v[l][s] != ".": acc[l][s].append(int(v[l][s]) / 9.0)
    _S["acc"] = [[(st.mean(a) if len(a) > 20 else 0.75) for a in lane] for lane in acc]
    _S["test"] = test
    return _S
def famstat(f):
    S = stats()["fam"]
    return S[f] if f in S and S[f]["n"] >= 40 else S["_all"]
def wpick(rng, W, k, exclude=0):
    """k distinct steps by weight, skipping bits already in exclude"""
    cand = [s for s in range(16) if not exclude >> s & 1]
    out = 0
    for _ in range(min(k, len(cand))):
        tot = sum(W[s] for s in cand)
        if tot <= 0: break
        u = rng.random() * tot
        for s in cand:
            u -= W[s]
            if u <= 0: out |= 1 << s; cand.remove(s); break
    return out
HATCAP = [4]   # variant knob: cap the resampled hat count at card count + HATCAP
HATP = [1.0]
GLOBALP = [False]  # port variant: step weights from the shipped kDrumStepP instead of the family table
def hats(rng, m, card, F, lane):
    n = rng.choice(F["cnt"][lane])
    if HATCAP[0] is not None: n = min(n, G.pc(card[lane]) + HATCAP[0])
    if n == 0 and m[lane]: n = G.pc(m[lane])
    n = min(n, 14)
    if n == 0: m[lane] = 0; return
    P = G.STEPP[lane] if GLOBALP[0] else F["P"][lane]
    W = [P[s] * (3.0 if card[lane] >> s & 1 else 1.0) for s in range(16)]
    m[lane] = wpick(rng, W, n)
def drummer_gen(cr, m, intel, ff, hybrid=False, moves=None):
    c = G.CUR[0]; F = famstat(c["family"]); card = [G.mask(s) for s in c["lanes"]]
    mv = moves or ("hat",)   # recommended; the other four moves measured worse (see groove-2h.md)
    if "hat" in mv and cr.random() < HATP[0]:
        hats(cr, m, card, F, 2)
        if card[3] or cr.random() < 0.3: hats(cr, m, card, F, 3)
    if "ghost" in mv and m[1] and cr.random() < 0.6:
        W = [F["P"][1][s] if s % 2 else 0.0 for s in range(16)]
        m[1] |= wpick(cr, W, cr.randint(1, 3), exclude=m[1])
    if "kick" in mv and cr.random() < 0.55:
        if ff:
            if cr.random() < 0.5: m[0] |= 1 << (14 if cr.random() < .5 else 15)
        else:
            n = cr.choice(F["cnt"][0]); n = max(2, min(n, 7))
            W = [F["P"][0][s] * (3 if card[0] >> s & 1 else 1) for s in range(16)]
            m[0] = (1 if card[0] & 1 else 0) | wpick(cr, W, n - 1, exclude=1)
    if "perc" in mv and cr.random() < 0.4:
        l = cr.choice((4, 5, 6)); n = cr.choice([x for x in F["cnt"][l] if x] or [2]); n = min(n, 5)
        m[l] = wpick(cr, F["P"][l], n)
    if "fill" in mv and cr.random() < 0.3 + 0.7 * c["fill"]:
        for l in (1, 5, 6):
            if cr.random() < 0.6:
                W = [F["P"][l][s] if s >= 12 else 0.0 for s in range(16)]
                m[l] |= wpick(cr, W, cr.randint(1, 3), exclude=0)
def hybrid_gen(cr, m, intel, ff):
    (drummer_gen if cr.random() < 0.5 else G.default_gen)(cr, m, intel, ff)
def nn_dist(gen):
    T = stats()["bars"]
    pcnt = np.array([bin(i).count("1") for i in range(65536)], dtype=np.uint8)
    d = []
    for p in gen:
        g = np.array([G.mask(l) if l else 0 for l in p["lanes"]], dtype=np.uint16)
        d.append(int(pcnt[np.bitwise_xor(T, g)].sum(axis=1).min()))
    return d
def lane_hats(gen): return st.mean((sum(c != "." for c in (p["lanes"][2] or "")) + sum(c != "." for c in (p["lanes"][3] or ""))) for p in gen)
def report(name, gen, rows):
    a, f = G.auc(gen)
    nn = nn_dist(gen)
    rows.append((name, a, G.escape(gen), f[2], f[5], st.median(nn), 100.0 * sum(x <= 2 for x in nn) / len(nn)))
def midi(path, lanes_v, bpm, swing, rep=2):
    NOTE = [36, 38, 42, 46, 39, 45, 50, 56]
    T = 480 // 4; ev = []
    for r in range(rep):
        for b, bar in enumerate(lanes_v):
            for l in range(LANES):
                for s in range(16):
                    v = bar[l][s]
                    if v > 0:
                        t = (r * len(lanes_v) + b) * 16 * T + s * T + (int(T * swing * 0.5) if s % 2 else 0)
                        vel = max(1, min(127, int(v * 127)))
                        ev.append((t, 1, NOTE[l], vel)); ev.append((t + T // 2, 0, NOTE[l], 0))
    ev.sort(key=lambda e: (e[0], e[1]))
    trk = b"\x00\xff\x51\x03" + int(60000000 / bpm).to_bytes(3, "big"); last = 0
    def vl(n):
        o = [n & 127]; n >>= 7
        while n: o.append(128 | n & 127); n >>= 7
        return bytes(reversed(o))
    for t, on, n, v in ev:
        trk += vl(t - last) + bytes([0x99, n, v]); last = t
    trk += vl(0) + b"\xff\x2f\x00"
    open(path, "wb").write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + b"MTrk" + struct.pack(">I", len(trk)) + trk)
def take(card, seed, gen):
    cr = random.Random("ab:%s:%d" % (card["id"], seed))
    lanes = G.propose(cr, card, None, gen)
    bars = G.bars(card, lanes, cr)
    return bars
def write_ab(out):
    os.makedirs(out, exist_ok=True)
    acc = stats()["acc"]
    BPM = {"house": 122, "techno": 128, "drum_and_bass": 172, "breaks": 132, "bass_music": 134, "downtempo": 88}
    for cid in ("deep_house", "detroit_techno", "neurofunk", "breakbeat", "uk_garage", "lofi_beats"):
        card = next(c for c in G.CARDS if c["id"] == cid)
        for s in range(3):
            old = take(card, s, None)
            # old = the app's mapping: hit 1.0, card ghost 0.45, step-pass ghost 0.2, fill 0.8
            midi(os.path.join(out, "old_%s_%d.mid" % (cid, s + 1)), [[[int(round(x * 120)) / 127.0 for x in bb["v"][l]] for l in range(LANES)] for bb in old],
                 BPM.get(card["family"], 120), card["swing"])
            new = take(card, s, drummer_gen)
            nb = []
            rng = random.Random("v:%s:%d" % (cid, s))
            for bb in new:
                bar = []
                for l in range(LANES):
                    row = []
                    for k in range(16):
                        v = bb["v"][l][k]
                        if v == 0: row.append(0.0)
                        elif v <= 0.25: row.append(0.22)                                    # step-pass ghost
                        else: row.append(max(0.15, min(1.0, acc[l][k] * (0.55 if v < 0.5 else 1.0) * 1.15 + rng.uniform(-0.06, 0.06))))
                    bar.append(row)
                nb.append(bar)
            midi(os.path.join(out, "new_%s_%d.mid" % (cid, s + 1)), nb, BPM.get(card["family"], 120), card["swing"])
if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--seeds", type=int, default=6); ap.add_argument("--midi", action="store_true")
    a = ap.parse_args()
    stats()
    rows = []
    real = random.Random(5).sample([q for q in stats()["test"] if q["type"] == "drum_groove"], 250)
    report("real held-out", real, rows)
    report("app dump (C++)", B.app_drum_bars("~/Infinite/training-data/renders/app_drums.jsonl"), rows)
    report("old proposer (sim)", G.run(seeds=a.seeds), rows)
    ALL = ("hat", "ghost", "kick", "perc", "fill")
    for nm, g in (("new: hat density (rec.)", drummer_gen),
                  ("new: hats + fill", lambda *x: drummer_gen(*x, moves=("hat", "fill"))),
                  ("new: hats + ghost", lambda *x: drummer_gen(*x, moves=("hat", "ghost"))),
                  ("new: hats + perc", lambda *x: drummer_gen(*x, moves=("hat", "perc"))),
                  ("new: all five moves", lambda *x: drummer_gen(*x, moves=ALL)),
                  ("new: no hat move", lambda *x: drummer_gen(*x, moves=ALL[1:]))):
        report(nm, G.run(gen=g, seeds=a.seeds), rows)
    HATCAP[0] = None
    report("new: hats uncapped", G.run(gen=drummer_gen, seeds=a.seeds), rows)
    print("%-22s %6s %8s %6s %6s %8s %9s" % ("variant", "AUC", "escape%", "hats", "sync", "NN med", "NN<=2 %"))
    for r in rows: print("%-22s %6.3f %8.1f %6.2f %6.2f %8.1f %9.1f" % r)
    if a.midi: write_ab(os.path.expanduser("~/Infinite/training-data/eval/groove_ab"))
