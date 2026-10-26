#!/usr/bin/env python3
"""Stage 4: Compositional Proposer + 16-Feature Contextual User Taste Layer.
    proposer.py [--scope shippable|local] [--n 10]
Generates novel music without imitating the corpus by combining:
  - Euclidean pulses E(k, 16), cross-style hybrids, theory arcs, and controlled random mutation
  - Filtered by critic.py (hard validity + wide plausibility floor)
  - Ranked by novelty + contextual user preference vector w^T phi(x) (Thompson-sampled)."""
import argparse, json, math, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import critic as CR
import learn as L
import pieces as P
import synth_music as S


def euclidean(k, n=16, rot=0):
    """Bjorklund / Bresenham Euclidean rhythm E(k, n) rotated by rot steps."""
    if k <= 0:
        return [0] * n
    if k >= n:
        return [1] * n
    row = [1 if ((i * k) % n) < k else 0 for i in range(n)]
    return [row[(i - rot) % n] for i in range(n)]


def propose_drum_raw(rng, fam=None, mix=None):
    """Free drum proposer mixing 3 creative mechanisms:
       (a) Euclidean + polyrhythmic synthesis, (b) cross-genre skeleton hybrid, (c) latent-cluster + mutation."""
    fams = list(S.Z.keys())
    f1 = fam if fam in S.Z else rng.choice(fams)
    f2 = rng.choice(fams)
    mode = rng.choice(("hybrid", "euclidean", "mutant"))

    if mode == "euclidean":
        # Grounded kick + backbeat snare + Euclidean hats/percussion
        k_hits = rng.choice([2, 3, 4, 4, 5])
        k_rot = rng.choice([0, 0, 2, 4])
        kick = euclidean(k_hits, 16, k_rot)
        kick[0] = 1
        snare = [1 if s in (4, 12) else (1 if s in (7, 9, 15) and rng.random() < 0.25 else 0) for s in range(16)]
        # Avoid kick-snare collision on backbeats when desired
        if rng.random() < 0.7:
            for s in (4, 12):
                if snare[s]:
                    kick[s] = 0
        chat = euclidean(rng.choice([3, 5, 7, 9, 11, 13]), 16, rng.randint(0, 3))
        ohat = [1 if s in (2, 6, 10, 14) and rng.random() < 0.6 else 0 for s in range(16)]
        clap = [1 if s in (4, 12) and rng.random() < 0.5 else 0 for s in range(16)]
        tom = euclidean(rng.choice([0, 2, 3]), 16, rng.randint(0, 7)) if rng.random() < 0.45 else [0] * 16
        htom = [1 if s in (11, 14, 15) and rng.random() < 0.25 else 0 for s in range(16)]
        bell = euclidean(rng.choice([0, 2, 3, 5]), 16, rng.randint(0, 5)) if rng.random() < 0.35 else [0] * 16
        mat = [kick, snare, chat, ohat, clap, tom, htom, bell]
    elif mode == "hybrid":
        # Rhythm section (kick/snare) from family f1 + top-end (hats/percussion) from family f2 + novelty flips
        g1 = S.groove(f1, rng)
        g2 = S.groove(f2, rng)
        lanes = [g1[0], g1[1], g2[2], g2[3], g1[4], g2[5], g2[6], g2[7]]
        mat = [[1 if (l and l[s] != ".") else 0 for s in range(16)] for l in lanes]
        # Displace 1-2 hits to create unseen syncopations
        for l in (0, 1, 2, 5):
            if any(mat[l]) and rng.random() < 0.65:
                s = rng.randrange(1, 16)
                mat[l][s] = 1 - mat[l][s]
        if not any(mat[0]):
            mat[0][0] = 1
    else:
        # Mutated theory groove with turnaround fills and ghost displacements
        lanes = S.groove(f1, rng)
        mat = [[1 if (l and l[s] != ".") else 0 for s in range(16)] for l in lanes]
        if not any(mat[0]):
            mat[0][0] = 1
            mat[0][rng.choice([6, 8, 10])] = 1
        for _ in range(rng.randint(2, 5)):
            l = rng.choice([0, 1, 2, 3, 4, 5, 6, 7])
            s = rng.randrange(16)
            if l == 0 and s == 0:
                continue
            mat[l][s] = 1 - mat[l][s]

    str_lanes = ["".join("x" if v else "." for v in row) if any(row) else "" for row in mat]
    return {"type": "drum_groove", "src": "proposer:" + mode, "family": f1, "lanes": str_lanes, "swing": round(rng.uniform(0.0, 0.25), 2), "groove": mode}


def propose_melody_raw(rng, fam="house", tonic=None, mode_name=None):
    """Compositional call-and-response melody proposer with chromatic passing tension and cadence."""
    scale_name = mode_name if mode_name in S.SCALES else rng.choice(S.FAM_SCALE.get(fam, ["minor", "dorian"]))
    scale = S.SCALES[scale_name]
    tonic = rng.randrange(12) if tonic is None else tonic
    base_oct = rng.choice([48, 60, 60, 72])
    pos = rng.choice([0, 2, 4])
    arc_type = rng.choice(["arch", "wave", "climb", "cascade"])
    pitches = []
    for i in range(16):
        if i == 0:
            deg = pos
        elif i == 7:
            # Half-cadence tension at end of Call (scale degree 1, 3, 4, or 6)
            deg = pos + rng.choice([1, 3, 4, -1])
        elif i == 15:
            # Full cadence resolution to tonic or fifth at end of Response
            deg = round(pos / 7.0) * 7 + rng.choice([0, 0, 4, 2])
        else:
            bias = 0.25 if (arc_type == "arch" and i < 8) or (arc_type == "climb") else (-0.25 if arc_type == "cascade" else 0.0)
            step = rng.choices([-3, -2, -1, 0, 1, 2, 3, 4], weights=[2, 5, 11, 5, 11, 5, 2, 1])[0]
            if rng.random() < abs(bias):
                step = abs(step) if bias > 0 else -abs(step)
            if len(pitches) >= 2 and abs(pitches[-1] - pitches[-2]) >= 7 and rng.random() < 0.20:
                step = -1 if pitches[-1] > pitches[-2] else 1
            deg = max(-4, min(14, pos + step))
        pos = deg
        note = base_oct + tonic + 12 * (deg // 7) + scale[deg % 7]
        if i not in (0, 15) and rng.random() < (0.04 if i % 4 == 0 else 0.05):
            note += rng.choice([-1, 1])
        pitches.append(max(36, min(96, note)))
    steps = [b - a for a, b in zip(pitches, pitches[1:])]
    return {"type": "melody_contour", "src": "proposer:" + arc_type, "family": fam, "role": "lead",
            "pitches": pitches, "steps": steps}


def propose_bass_raw(rng, kick_lane="x...x...x...x...", fam="house"):
    """Coupled bass proposer that locks to or syncopates around the kick lane."""
    style = rng.choice(["lock", "offbeat_pocket", "syncopated_answer", "walking"])
    chars = []
    prev = 0
    for s in range(16):
        k_on = bool(kick_lane and kick_lane[s] != ".")
        if s == 0:
            on = True
            deg = 0
        elif style == "lock":
            on = k_on or (s in (6, 10, 14) and rng.random() < 0.35)
            deg = 0 if k_on and rng.random() < 0.7 else rng.choice([0, 2, 3, 4, 6])
        elif style == "offbeat_pocket":
            on = (s % 4 == 2) or (k_on and s in (0, 8)) or (s in (7, 15) and rng.random() < 0.3)
            deg = 0 if s % 4 == 0 else rng.choice([0, 0, 3, 4, 6, 7])
        else:
            on = (k_on and rng.random() < 0.6) or (not k_on and s % 2 == 1 and rng.random() < 0.3)
            deg = rng.choice([0, 0, 2, 3, 4, 4, 5, 6])
        if on:
            if abs(deg - prev) > 5:
                deg = 4 if deg > prev else 0
            prev = deg
            chars.append(str(deg))
        else:
            chars.append(".")
    return {"type": "bass_seq", "src": "proposer:" + style, "family": fam, "steps": "".join(chars)}


def propose_filtered(kind, rec, rng, k=12, **kw):
    """Best-of-K proposal: generate K candidates, keep those passing Critic, pick highest novelty/score."""
    cands = []
    for _ in range(k * 2):
        if kind == "drum_groove":
            p = propose_drum_raw(rng, fam=kw.get("fam"))
        elif kind == "melody_contour":
            p = propose_melody_raw(rng, fam=kw.get("fam", "house"), tonic=kw.get("tonic"), mode_name=kw.get("mode_name"))
        elif kind == "bass_seq":
            p = propose_bass_raw(rng, kick_lane=kw.get("kick_lane", "x...x...x...x..."), fam=kw.get("fam", "house"))
        else:
            raise ValueError("unknown kind " + kind)
        res = CR.judge(p, rec)
        p["_critic"] = res
        if res["accept"]:
            cands.append(p)
            if len(cands) >= k:
                break
    if not cands:
        return p
    cands.sort(key=lambda x: x["_critic"]["novelty"], reverse=True)
    return cands[0]


# ---- 16-Dimensional Contextual User Taste Layer (Personal Critic) -----------------------------

def extract_features(loop):
    """Extract 16 normalized musical features phi(x) in [-1, 1] from a proposed loop dict."""
    drum = loop["drum"]
    mel = loop["melody"]
    bass = loop["bass"]
    m = L.cells(drum)
    k_dens = sum(m[0]) / 8.0 - 1.0
    hat_dens = (sum(m[2]) + sum(m[3])) / 12.0 - 1.0
    sync = CR.syncopation_score(drum["lanes"]) * 2.0 - 1.0
    swing = drum.get("swing", 0.0) / 0.2 - 1.0

    P_ = mel["pitches"]
    steps = mel["steps"]
    minor_dark = 1.0 if (3 in mel.get("mode_iv", [0, 2, 3])) else -1.0
    leap_ratio = (sum(abs(s) >= 4 for s in steps) / max(1, len(steps))) * 2.0 - 1.0
    mel_span = min(1.0, (max(P_) - min(P_)) / 18.0) * 2.0 - 1.0
    mel_reg = (sum(P_) / len(P_) - 60.0) / 18.0

    bst = bass["steps"]
    b_dens = (sum(c != "." for c in bst) / 8.0) - 1.0
    b_off = (sum(bst[i] != "." for i in range(1, 16, 2)) / max(1, sum(c != "." for c in bst))) * 2.0 - 1.0

    # Timbral / DSP & novelty features
    bright = loop.get("brightness", 0.0)
    plucky = loop.get("plucky", 0.0)
    mod_depth = loop.get("mod_depth", 0.0)
    space_wet = loop.get("space_wet", 0.0)
    nov_d = drum.get("_critic", {}).get("novelty", 0.5) * 2.0 - 1.0
    nov_m = mel.get("_critic", {}).get("novelty", 0.5) * 2.0 - 1.0

    return [k_dens, hat_dens, sync, swing, minor_dark, leap_ratio, mel_span, mel_reg,
            b_dens, b_off, bright, plucky, mod_depth, space_wet, nov_d, nov_m]


class ContextualUserTaste:
    """Online Bayesian linear/logistic preference learner over 16 musical features with Thompson sampling."""
    def __init__(self, dim=16, n0=6.0):
        self.dim = dim
        self.n0 = n0
        self.w = [0.0] * dim
        self.prec = [2.0] * dim
        self.n = 0

    def trust(self):
        return min(0.9, self.n / (self.n + self.n0))

    def score(self, loop, rng=None, explore=0.25):
        phi = extract_features(loop)
        t = self.trust()
        nov = 0.5 * (loop["drum"]["_critic"]["novelty"] + loop["melody"]["_critic"]["novelty"])
        if t <= 1e-6:
            return nov
        u = 0.0
        for i in range(self.dim):
            wi = self.w[i] + (rng.gauss(0.0, explore / math.sqrt(self.prec[i])) if rng else 0.0)
            u += wi * phi[i]
        return (1.0 - t) * nov + t * (1.0 / (1.0 + math.exp(-max(-8.0, min(8.0, u)))))

    def update(self, loop, kept):
        phi = extract_features(loop)
        y = 1.0 if kept else -1.0
        dot = max(-8.0, min(8.0, sum(w * x for w, x in zip(self.w, phi))))
        pred = 1.0 / (1.0 + math.exp(-y * dot))
        err = (1.0 - pred) * y
        lr = 0.85
        for i in range(self.dim):
            self.prec[i] += 0.15 * phi[i] * phi[i]
            self.w[i] += (lr / math.sqrt(self.prec[i])) * err * phi[i]
        self.n += 1


def propose_loop(rec, rng, taste=None, k=8):
    """Propose K complete critic-verified loops and pick the one matching ContextualUserTaste."""
    loops = []
    for _ in range(k):
        fam = rng.choice(list(S.Z.keys()))
        d = propose_filtered("drum_groove", rec, rng, k=4, fam=fam)
        m = propose_filtered("melody_contour", rec, rng, k=4, fam=fam)
        b = propose_filtered("bass_seq", rec, rng, k=4, kick_lane=d["lanes"][0], fam=fam)
        loops.append({
            "family": fam, "drum": d, "melody": m, "bass": b,
            "brightness": rng.uniform(-1.0, 1.0), "plucky": rng.uniform(-1.0, 1.0),
            "mod_depth": rng.uniform(-1.0, 1.0), "space_wet": rng.uniform(-1.0, 1.0),
        })
    if taste is not None:
        loops.sort(key=lambda lp: taste.score(lp, rng=rng), reverse=True)
    else:
        loops.sort(key=lambda lp: lp["drum"]["_critic"]["novelty"] + lp["melody"]["_critic"]["novelty"], reverse=True)
    return loops[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scope", default="shippable", choices=["shippable", "local"])
    ap.add_argument("--models", default="~/Infinite/training-data/models")
    ap.add_argument("--n", type=int, default=4)
    a = ap.parse_args()
    rec = json.load(open(os.path.join(os.path.expanduser(a.models), a.scope, "current.json")))
    rng = random.Random(7)
    for i in range(a.n):
        lp = propose_loop(rec, rng)
        print("=== Proposed Loop #%d (family=%s, drum_nov=%.3f, mel_nov=%.3f) ===" % (
            i + 1, lp["family"], lp["drum"]["_critic"]["novelty"], lp["melody"]["_critic"]["novelty"]))
        print("  kick :", lp["drum"]["lanes"][0] or "................")
        print("  snare:", lp["drum"]["lanes"][1] or "................")
        print("  chat :", lp["drum"]["lanes"][2] or "................")
        print("  bass :", lp["bass"]["steps"])
        print("  mel  :", lp["melody"]["pitches"])


if __name__ == "__main__":
    main()
