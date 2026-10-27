#!/usr/bin/env python3
"""Stage 2: learn from the piece store, keep a model only if it beats the last one on held-out data.
    learn.py [--store DIR] [--models DIR] [--scope shippable|local] [--synth-max N] [--force]
Models are count tables, no neural net:
  drum    P(hit | lane, step, family, kick here, snare here, this lane one step ago), backed off level by level
  melody  P(next interval | previous interval, previous note in the key, strong beat, the chord just heard)
  params  per node/knob empirical distribution                                  (where settings sit)
Split is by source file (leave-source-out). Synthetic pieces train but are never in the held-out set.
Score = bits saved per cell/step/value against a fixed baseline (higher is better); a candidate is kept only
model by model: each of the five (independent, one per piece type) keeps whichever of the candidate and the last
kept one saves more bits on the same held-out set, so the kept set is never worse on any model.
Scopes: `shippable` trains on owner / CC-BY pieces only; `local` adds third-party and unknown-licence pieces."""
import argparse, collections, glob, hashlib, json, math, os, random, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

CLEAR = ("owner", "cleared", "cc0", "public-domain", "mit", "cc-by-4.0", "cc-by")
LANES, STEPS = 8, 16
MAXI = 12  # melodic steps clipped to +-12
MAJ, MIN = [0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10]


def clear(lic):
    """shippable licences only; anything with NC / ND / SA / GPL / unknown stays local"""
    l = (lic or "owner").lower().strip()
    if any(x in l for x in ("-nc", "-nd", "-sa", "gpl", "unknown", "local")):
        return False
    return l.startswith(CLEAR)


def load(store, scope):
    rows = []
    for f in sorted(glob.glob(os.path.join(os.path.expanduser(store), "*.jsonl"))):
        for ln in open(f):
            r = json.loads(ln)
            if r.get("ok") and (scope == "local" or clear(r.get("license"))):
                rows.append(r)
    return rows


def unit_hash(src):
    return int(hashlib.sha1(src.split("#")[0].encode()).hexdigest(), 16)


def split(real):
    """Held-out = a fifth of the source files per type; if that picks none, the file with the smallest hash."""
    test_units = set()
    for t in {r["type"] for r in real}:
        units = sorted({r["src"].split("#")[0] for r in real if r["type"] == t}, key=lambda u: unit_hash(u))
        pick = {u for u in units if unit_hash(u) % 5 == 0}
        if not pick and len(units) >= 3:
            pick = {units[0]}
        test_units |= {(t, u) for u in pick}
    train = [r for r in real if (r["type"], r["src"].split("#")[0]) not in test_units]
    test = [r for r in real if (r["type"], r["src"].split("#")[0]) in test_units]
    return train, test


# ---- drum, v1: independent cells (kept so an older model can still be scored) ------------------

def cells(p):
    return [[1 if (l and l[s] != ".") else 0 for s in range(STEPS)] for l in p["lanes"]]


def drum_prob_v1(model, fam):
    allp = model["_all"]["p"]
    if fam not in model or fam == "_all":
        return allp
    f = model[fam]
    w = f["n"] / (f["n"] + 5.0)
    return [[w * f["p"][l][s] + (1 - w) * allp[l][s] for s in range(STEPS)] for l in range(LANES)]


def score_drum_v1(model, test):
    lane_p = [sum(model["_all"]["p"][l]) / STEPS for l in range(LANES)]
    tot = base = 0.0
    for p in test:
        P = drum_prob_v1(model, p.get("family", "?"))
        m = cells(p)
        for l in range(LANES):
            for s in range(STEPS):
                x = m[l][s]
                tot += -math.log2(P[l][s] if x else 1 - P[l][s])
                base += -math.log2(lane_p[l] if x else 1 - lane_p[l])
    return (base - tot) / (len(test) * LANES * STEPS)


# ---- drum, v2: a cell sees the kick, the snare and its own lane one step back ------------------

A, LAM1, LAM2, LAM3 = 0.5, 5.0, 4.0, 4.0


def ctx(m, l, s):
    k = m[0][s] if l > 0 else 0
    b = m[1][s] if l > 1 else 0
    prev = m[l][s - 1] if s > 0 else 0
    return k, b, prev


def drum_fam(p, m, mix=None):
    fam = p.get("family", "?")
    if fam not in ("unlabeled", "?", None) or not mix:
        return fam
    if "_dfam" in p:
        return p["_dfam"]
    best_c, best_ll = 0, -1e30
    pi, lth, l1 = mix["pi"], mix["lth"], mix["l1"]
    for c in range(len(pi)):
        ll = pi[c]
        tc, t1c = lth[c], l1[c]
        for l in range(LANES):
            row = m[l]
            base = l * STEPS
            for s in range(STEPS):
                ll += tc[base + s] if row[s] else t1c[base + s]
        if ll > best_ll:
            best_ll, best_c = ll, c
    res = "c%d" % best_c
    p["_dfam"] = res
    return res


def prep_mix(raw_mix):
    if not raw_mix or "theta" not in raw_mix:
        return None
    pi = [math.log(max(1e-9, x)) for x in raw_mix["pi"]]
    lth = [[math.log(max(1e-6, v)) for v in row] for row in raw_mix["theta"]]
    l1 = [[math.log(max(1e-6, 1.0 - v)) for v in row] for row in raw_mix["theta"]]
    return {"k": raw_mix["k"], "pi": pi, "lth": lth, "l1": l1, "theta": raw_mix["theta"], "raw_pi": raw_mix["pi"]}


def fit_drum(rows, raw_mix=None):
    mix = prep_mix(raw_mix)
    c = {"1": collections.defaultdict(lambda: [0, 0]), "f": collections.defaultdict(lambda: [0, 0]),
         "2": collections.defaultdict(lambda: [0, 0]), "3": collections.defaultdict(lambda: [0, 0])}
    n = 0
    for p in rows:
        if p["type"] != "drum_groove":
            continue
        n += 1
        m = cells(p)
        fam = drum_fam(p, m, mix)
        for l in range(LANES):
            for s in range(STEPS):
                x = m[l][s]
                k, b, prev = ctx(m, l, s)
                for lvl, key in (("1", (l, s)), ("f", (fam, l, s)), ("2", (fam, l, s, k, b)), ("3", (fam, l, s, k, b, prev))):
                    e = c[lvl]["|".join(map(str, key))]
                    e[0] += x; e[1] += 1
    out = {"_v2": True, "_v3": True, "n": n, **{lvl: dict(d) for lvl, d in c.items()}}
    if raw_mix and "theta" in raw_mix:
        out["mix"] = {"k": raw_mix["k"], "pi": raw_mix["pi"], "theta": raw_mix["theta"]}
    return out


def p_cell(model, fam, l, s, k, b, prev, upto=3):
    g = lambda lvl, key: model[lvl].get("|".join(map(str, key)), (0, 0))
    v3 = bool(model.get("_v3"))
    h, t = g("1", (l, s))
    p = (h + A) / (t + 2 * A)
    h, t = g("f", (fam, l, s))
    if upto >= 1 and t: p = (h + LAM1 * p) / (t + LAM1)
    h, t = g("2", (fam, l, s, k, b) if v3 else (l, s, k, b))
    if upto >= 2 and t: p = (h + LAM2 * p) / (t + LAM2)
    h, t = g("3", (fam, l, s, k, b, prev) if v3 else (l, s, k, b, prev))
    if upto >= 3 and t: p = (h + LAM3 * p) / (t + LAM3)
    return min(1 - 1e-4, max(1e-4, p))


def score_drum(model, test, upto=3):
    """bits saved per cell vs one Bernoulli per lane (a groove with no step shape)."""
    if not test:
        return None
    if "_v2" not in model:
        return score_drum_v1(model, test)
    mix = prep_mix(model.get("mix"))
    lane_p = []
    for l in range(LANES):
        hs = sum(model["1"].get("%d|%d" % (l, s), (0, 0))[0] for s in range(STEPS))
        ts = sum(model["1"].get("%d|%d" % (l, s), (0, 0))[1] for s in range(STEPS))
        lane_p.append(min(1 - 1e-4, max(1e-4, (hs + A) / (ts + 2 * A))))
    tot = base = 0.0
    for p in test:
        m = cells(p)
        fam = drum_fam(p, m, mix)
        for l in range(LANES):
            for s in range(STEPS):
                x = m[l][s]
                q = p_cell(model, fam, l, s, *ctx(m, l, s), upto=upto)
                tot += -math.log2(q if x else 1 - q)
                base += -math.log2(lane_p[l] if x else 1 - lane_p[l])
    return (base - tot) / (len(test) * LANES * STEPS)


def sample_groove(model, fam, rng):
    m = [[0] * STEPS for _ in range(LANES)]
    for l in range(LANES):
        for s in range(STEPS):
            m[l][s] = 1 if rng.random() < p_cell(model, fam, l, s, *ctx(m, l, s)) else 0
    return ["".join("x" if v else "." for v in row) for row in m]


# ---- melody ------------------------------------------------------------------------------------

def clip(x):
    return max(-MAXI, min(MAXI, x))


TRIADS_MAJ = [(MAJ[d], MAJ[(d + 2) % 7], MAJ[(d + 4) % 7]) for d in range(7)]
TRIADS_MIN = [(MIN[d], MIN[(d + 2) % 7], MIN[(d + 4) % 7]) for d in range(7)]


def key_of(pitches):
    pc = [0] * 12
    for p in pitches:
        pc[p % 12] += 1
    best_sc, best_t, best_m = -1.0, 0, MAJ
    for t in range(12):
        for mode in (MAJ, MIN):
            sc = 2.0 * sum(pc[(t + m) % 12] for m in mode) + pc[t] + 0.5 * pc[(t + 7) % 12]
            if sc > best_sc:
                best_sc, best_t, best_m = sc, t, mode
    return best_t, best_m


def chord_of(rel, mode):
    """Diatonic triad (root degree 0..6) that covers most of the recent notes (pitch classes relative to the tonic)."""
    triads = TRIADS_MAJ if mode is MAJ or mode == MAJ else TRIADS_MIN
    best, bd = -1, 0
    for d, (t0, t1, t2) in enumerate(triads):
        sc = sum(1 for r in rel if r == t0 or r == t1 or r == t2)
        if sc > best:
            best, bd = sc, d
    return bd


def mel_steps(p, v3=True):
    """(prev interval, k1, k2, k3, interval) for every step of a window; the contexts only look backwards."""
    ck = "_ms3" if v3 else "_ms2"
    if ck in p:
        return p[ck]
    P_ = p["pitches"]
    t, mode = key_of(P_)
    rel = [(x - t) % 12 for x in P_]
    out = []
    for j in range(len(P_) - 1):
        pi = clip(P_[j] - P_[j - 1]) if j else "s"
        strong = int((j + 1) % 4 == 0)
        d = chord_of(rel[max(0, j - 3):j + 1], mode)
        if v3:
            # Harmonic function: 0=Tonic(I,vi), 1=Predominant(ii,IV), 2=Dominant(V,iii,vii) + 4-step contour arc
            hf = 0 if d in (0, 5) else (1 if d in (1, 3) else 2)
            diff = P_[j] - P_[max(0, j - 4)]
            arc = 1 if diff > 2 else (-1 if diff < -2 else 0)
            k3 = "%s|%d|%d|%d|%d" % (pi, rel[j], strong, hf, arc)
        else:
            k3 = "%s|%d|%d|%d" % (pi, d, (rel[j] - mode[d]) % 12, strong)
        out.append((str(pi), "%s|%d|%d" % (pi, rel[j], strong), k3, clip(P_[j + 1] - P_[j])))
    p[ck] = out
    return out


def fit_melody(rows):
    uni = collections.Counter()
    lv = [collections.defaultdict(collections.Counter) for _ in range(3)]
    for p in rows:
        if p["type"] != "melody_contour" or len(p["pitches"]) < 6:
            continue
        for k1, k2, k3, x in mel_steps(p, v3=True):
            uni[x] += 1
            lv[0][k1][x] += 1; lv[1][k2][x] += 1; lv[2][k3][x] += 1
    return {"_v2": True, "_v3": True, "uni": {str(k): v for k, v in uni.items()},
            "lv": [{k: {str(x): c for x, c in d.items()} for k, d in l.items()} for l in lv]}


def mel_p(model, ks, x, upto=3, lam=(8.0, 8.0, 12.0)):
    uni = model["uni"]
    p = (uni.get(str(x), 0) + 0.5) / (sum(uni.values()) + 25 * 0.5)
    for i in range(upto):
        d = model["lv"][i].get(ks[i])
        if d:
            p = (d.get(str(x), 0) + lam[i] * p) / (sum(d.values()) + lam[i])
    return p


def score_melody(model, test, upto=3):
    """bits saved per step vs the unigram of intervals (no context at all)."""
    if not test or not model.get("uni"):
        return None
    if "_v2" not in model:
        return None  # an older melody model has no key/chord tables; it cannot be compared fairly
    v3 = bool(model.get("_v3"))
    tot = base = cnt = 0.0
    utot = sum(model["uni"].values()) + 25 * 0.5
    for p in test:
        if len(p["pitches"]) < 6:
            continue
        for k1, k2, k3, x in mel_steps(p, v3=v3):
            tot += -math.log2(mel_p(model, (k1, k2, k3), x, upto))
            base += -math.log2((model["uni"].get(str(x), 0) + 0.5) / utot)
            cnt += 1
    return (base - tot) / cnt if cnt else None


def sample_melody(model, rng, start=60, n=16, tonic=0, mode=MIN):
    out = [start]
    v3 = bool(model.get("_v3"))
    for j in range(n - 1):
        pi = clip(out[-1] - out[-2]) if j else "s"
        rel = [(x - tonic) % 12 for x in out]
        strong = int((j + 1) % 4 == 0)
        d = chord_of(rel[max(0, j - 3):j + 1], mode)
        if v3:
            hf = 0 if d in (0, 5) else (1 if d in (1, 3) else 2)
            diff = out[-1] - out[max(0, len(out) - 5)]
            arc = 1 if diff > 2 else (-1 if diff < -2 else 0)
            k3 = "%s|%d|%d|%d|%d" % (pi, rel[-1], strong, hf, arc)
        else:
            k3 = "%s|%d|%d|%d" % (pi, d, (rel[-1] - mode[d]) % 12, strong)
        ks = (str(pi), "%s|%d|%d" % (pi, rel[-1], strong), k3)
        ws = [mel_p(model, ks, x) for x in range(-MAXI, MAXI + 1)]
        nxt = out[-1] + rng.choices(range(-MAXI, MAXI + 1), weights=ws)[0]
        out.append(max(36, min(96, nxt)))
    return out


# ---- bass: 16-step scale-degree + rest sequence conditioned on step position & previous symbol --

BASS_SYMS = [".", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"]


def fit_bass(rows):
    uni = collections.Counter()
    pos = collections.defaultdict(collections.Counter)
    ctx = collections.defaultdict(collections.Counter)
    for p in rows:
        if p["type"] == "bass_seq" and len(p.get("steps", "")) == 16:
            st = p["steps"]
            for s, ch in enumerate(st):
                if ch not in BASS_SYMS:
                    continue
                prev = "s" if s == 0 else st[s - 1]
                uni[ch] += 1
                pos[str(s % 4)][ch] += 1
                ctx["%d|%s" % (s % 4, prev)][ch] += 1
    return {"uni": dict(uni), "pos": {k: dict(v) for k, v in pos.items()}, "ctx": {k: dict(v) for k, v in ctx.items()}}


def bass_p(model, s, prev, ch, lam1=4.0, lam2=6.0):
    u = model.get("uni", {})
    nsym = len(BASS_SYMS)
    p = (u.get(ch, 0) + 0.5) / (sum(u.values()) + nsym * 0.5)
    d1 = model.get("pos", {}).get(str(s % 4))
    if d1:
        p = (d1.get(ch, 0) + lam1 * p) / (sum(d1.values()) + lam1)
    d2 = model.get("ctx", {}).get("%d|%s" % (s % 4, prev))
    if d2:
        p = (d2.get(ch, 0) + lam2 * p) / (sum(d2.values()) + lam2)
    return p


def score_bass(model, test):
    """bits saved per step vs symbol unigram"""
    if not model or not model.get("uni"):
        return None
    u = model["uni"]
    utot = sum(u.values()) + len(BASS_SYMS) * 0.5
    tot = base = n = 0.0
    for p in test:
        if p["type"] != "bass_seq" or len(p.get("steps", "")) != 16:
            continue
        st = p["steps"]
        for s, ch in enumerate(st):
            if ch not in BASS_SYMS:
                continue
            prev = "s" if s == 0 else st[s - 1]
            tot += -math.log2(bass_p(model, s, prev, ch))
            base += -math.log2((u.get(ch, 0) + 0.5) / utot)
            n += 1
    return (base - tot) / n if n else None


# ---- params -----------------------------------------------------------------------------------

def fit_params(rows):
    d = collections.defaultdict(list)
    for p in rows:
        if p["type"] in ("params", "glide_params"):
            for k, v in p["values"].items():
                if isinstance(v, (int, float)) and math.isfinite(v):
                    d["%s|%s" % (p.get("node", "?"), k)].append(v)
    out = {}
    for k, vs in d.items():
        vs = sorted(vs)
        out[k] = {"n": len(vs), "vals": vs[:: max(1, len(vs) // 200)], "lo": vs[0], "hi": vs[-1]}
    return out


def score_params(model, test):
    """bits saved per value vs uniform over the observed train range (16 bins plus a floor)."""
    tot = cnt = 0.0
    for p in test:
        if p["type"] not in ("params", "glide_params"):
            continue
        for k, v in p["values"].items():
            m = model.get("%s|%s" % (p.get("node", "?"), k))
            if not m or m["n"] < 8 or m["hi"] <= m["lo"] or not isinstance(v, (int, float)) or not math.isfinite(v):
                continue
            w = (m["hi"] - m["lo"]) / 16.0
            b = min(15, max(0, int((v - m["lo"]) / w)))
            hist = sum(1 for x in m["vals"] if min(15, int((x - m["lo"]) / w)) == b)
            q = (hist + 0.5) / (len(m["vals"]) + 8.0)
            tot += math.log2(16.0) - (-math.log2(q))
            cnt += 1
    return tot / cnt if cnt else None


# ---- chords: next chord given the previous one, per mode ------------------------------------

def csym(c):
    return "%d%s" % (c["deg"], "m" if c["minor"] else "M")


def chord_steps(p):
    syms = [csym(c) for c in p["chords"]]
    return [(p["mode"], "start" if j == 0 else syms[j - 1], syms[j]) for j in range(len(syms))]


def fit_chord(rows):
    uni, bi = collections.defaultdict(collections.Counter), collections.defaultdict(collections.Counter)
    for p in rows:
        if p["type"] != "chord_prog":
            continue
        for mode, prev, x in chord_steps(p):
            uni[mode][x] += 1
            bi["%s|%s" % (mode, prev)][x] += 1
    return {"uni": {k: dict(v) for k, v in uni.items()}, "bi": {k: dict(v) for k, v in bi.items()}}


def chord_p(model, mode, prev, x, lam=4.0, nsym=14):
    u = model["uni"].get(mode, {})
    p = (u.get(x, 0) + 0.5) / (sum(u.values()) + nsym * 0.5)
    b = model["bi"].get("%s|%s" % (mode, prev))
    if b:
        p = (b.get(x, 0) + lam * p) / (sum(b.values()) + lam)
    return p


def score_chord(model, test):
    """bits saved per chord vs the per-mode unigram"""
    tot = base = n = 0.0
    for p in test:
        if p["type"] != "chord_prog" or not model.get("uni"):
            continue
        for mode, prev, x in chord_steps(p):
            u = model["uni"].get(mode, {})
            tot += -math.log2(chord_p(model, mode, prev, x))
            base += -math.log2((u.get(x, 0) + 0.5) / (sum(u.values()) + 7.0))
            n += 1
    return (base - tot) / n if n else None


def sample_progression(model, mode, rng, n=4):
    out, prev = [], "start"
    for _ in range(n):
        syms = sorted({k for v in model["uni"].get(mode, {}) for k in [v]})
        ws = [chord_p(model, mode, prev, x) for x in syms]
        prev = rng.choices(syms, weights=ws)[0]
        out.append(prev)
    return out


# ---- per-piece log-likelihoods & held-out percentile calibration for critic.py ----------------

def piece_ll(model, p):
    """Mean per-step/cell log2-likelihood of a single piece under the learned model."""
    t = p["type"]
    if t == "drum_groove":
        dm = model.get("drum", {})
        if "_v2" not in dm:
            return None
        m = cells(p)
        fam = drum_fam(p, m, prep_mix(dm.get("mix")))
        tot = 0.0
        for l in range(LANES):
            for s in range(STEPS):
                q = p_cell(dm, fam, l, s, *ctx(m, l, s))
                tot += math.log2(q if m[l][s] else 1.0 - q)
        return tot / (LANES * STEPS)
    if t == "melody_contour":
        mm = model.get("melody", {})
        if "_v2" not in mm or len(p.get("pitches", [])) < 6:
            return None
        steps = mel_steps(p, v3=bool(mm.get("_v3")))
        return sum(math.log2(mel_p(mm, (k1, k2, k3), x)) for k1, k2, k3, x in steps) / max(1, len(steps))
    if t == "bass_seq":
        bm = model.get("bass", {})
        st = p.get("steps", "")
        if not bm.get("uni") or len(st) != 16:
            return None
        vals = [math.log2(bass_p(bm, s, "s" if s == 0 else st[s - 1], ch)) for s, ch in enumerate(st) if ch in BASS_SYMS]
        return sum(vals) / len(vals) if vals else None
    if t == "chord_prog":
        cm = model.get("chord", {})
        if not cm.get("uni") or not p.get("chords"):
            return None
        steps = chord_steps(p)
        return sum(math.log2(chord_p(cm, mode, prev, x)) for mode, prev, x in steps) / max(1, len(steps))
    return None


def calibrate_thresholds(model, test):
    out = {}
    pcts = (1, 3, 5, 10, 25, 50, 75, 90, 95, 99)
    rng = random.Random(17)
    for t in ("drum_groove", "melody_contour", "bass_seq", "chord_prog"):
        sub = [p for p in test if p["type"] == t]
        if len(sub) > 4000:
            sub = rng.sample(sub, 4000)
        vals = sorted(v for p in sub for v in [piece_ll(model, p)] if v is not None)
        if vals:
            out[t] = {f"p{q}": round(vals[min(len(vals) - 1, max(0, int(len(vals) * q / 100.0)))], 5) for q in pcts}
            out[t]["n"] = len(vals)
    return out


# ---- run --------------------------------------------------------------------------------------

def evaluate(model, test):
    d = [p for p in test if p["type"] == "drum_groove"]
    m = [p for p in test if p["type"] == "melody_contour"]
    return {"drum": score_drum(model["drum"], d), "melody": score_melody(model["melody"], m),
            "bass": score_bass(model.get("bass", {}), test), "params": score_params(model["params"], test),
            "chord": score_chord(model.get("chord", {}), test)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--store", default="~/Infinite/training-data/store")
    ap.add_argument("--models", default="~/Infinite/training-data/models")
    ap.add_argument("--scope", default="shippable", choices=["shippable", "local"])
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--synth-max", type=int, default=0, help="synthetic pieces per type added to training (never to the test set)")
    a = ap.parse_args()
    rows = load(a.store, a.scope)
    real = [r for r in rows if not r.get("synthetic")]
    syn = collections.defaultdict(list)
    for r in rows:
        if r.get("synthetic"):
            syn[r["type"]].append(r)
    train, test = split(real)
    for t, rs in syn.items():
        random.Random(3).shuffle(rs)
        train += rs[: a.synth_max]
    cnt = collections.Counter(r["type"] for r in train)
    tcnt = collections.Counter(r["type"] for r in test)
    print("scope %s: train %d  test %d" % (a.scope, len(train), len(test)))
    print("  train", dict(cnt)); print("  test ", dict(tcnt))
    mdir = os.path.join(os.path.expanduser(a.models), a.scope)
    os.makedirs(mdir, exist_ok=True)
    mix_f = os.path.join(mdir, "drum_mix.json")
    raw_mix = json.load(open(mix_f)) if os.path.exists(mix_f) else None
    cand = {"drum": fit_drum(train, raw_mix=raw_mix), "melody": fit_melody(train), "bass": fit_bass(train),
            "params": fit_params(train), "chord": fit_chord(train)}
    cand_s = evaluate(cand, test)
    thresholds = calibrate_thresholds(cand, test)
    last_f = os.path.join(mdir, "current.json")
    last = json.load(open(last_f)) if os.path.exists(last_f) else None
    last_s = evaluate(last["model"], test) if last else None
    fmt = lambda x: "n/a" if x is None else "%+.3f" % x
    print("\nheld-out bits saved vs baseline (higher is better)")
    print("%-8s %10s %10s" % ("model", "candidate", "last kept"))
    ok = True
    kept_old = []
    for k in cand_s:
        print("%-8s %10s %10s" % (k, fmt(cand_s[k]), fmt(last_s[k]) if last_s else "-"))
        if last_s and cand_s[k] is not None and last_s[k] is not None and cand_s[k] < last_s[k] - 5e-4 and k in last["model"]:
            cand[k], cand_s[k] = last["model"][k], last_s[k]
            kept_old.append(k)
    if kept_old:
        print("kept the last model for: %s (candidate was worse on held-out)" % ", ".join(kept_old))
        thresholds = calibrate_thresholds(cand, test)
    dm = [p for p in test if p["type"] == "drum_groove"]
    mm = [p for p in test if p["type"] == "melody_contour"]
    print("\nwhat context adds (same model, levels switched off):")
    print("  drum   family only %s | + kick/snare %s | + lane history %s" % (
        fmt(score_drum(cand["drum"], dm, 1)), fmt(score_drum(cand["drum"], dm, 2)), fmt(score_drum(cand["drum"], dm, 3))))
    print("  melody prev interval %s | + note in key %s | + chord %s" % (
        fmt(score_melody(cand["melody"], mm, 1)), fmt(score_melody(cand["melody"], mm, 2)), fmt(score_melody(cand["melody"], mm, 3))))
    if all(v is None for v in cand_s.values()):
        ok = False
        print("no held-out data in any model: nothing to judge")
    if ok or a.force:
        rec = {"time": time.strftime("%Y-%m-%d %H:%M:%S"), "scope": a.scope, "train": dict(cnt), "test": dict(tcnt),
               "scores": cand_s, "thresholds": thresholds, "model": cand}
        json.dump(rec, open(last_f, "w"))
        json.dump({k: v for k, v in rec.items() if k != "model"}, open(os.path.join(mdir, "log-%d.json" % int(time.time())), "w"))
        print("\nKEPT: wrote", last_f)
    else:
        print("\nREJECTED: nothing to keep")
    rng = random.Random(1)
    for f in ("house", "drum_and_bass"):
        print("\nsample groove for %s (kick, snare, chat, ohat, clap, tom, htom, bell):" % f)
        for ln in sample_groove(cand["drum"], f, rng):
            print("   ", ln)
    print("\nsample progressions:", {m: sample_progression(cand["chord"], m, rng) for m in ("Major", "Minor", "Modal") if cand["chord"]["uni"].get(m)})
    print("\nsample melody (A minor, MIDI notes):", sample_melody(cand["melody"], rng, start=69, tonic=9))


if __name__ == "__main__":
    main()
