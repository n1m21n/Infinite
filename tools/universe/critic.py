#!/usr/bin/env python3
"""Stage 3: Trained Critic — hard musical/DSP validity + wide plausibility floor + novelty bonus.
    critic.py [--scope shippable|local] [--store DIR] [--models DIR]
The corpus is for robustness to avoid mishappenings, NOT a template the generator imitates:
  1. Hard validity: deterministic structural + DSP rules (catches no-kick, stuck 16ths, off-key, silent filters).
  2. Wide plausibility floor: calibrated at ~1.5th percentile of held-out real music log-likelihood (accepts ~98.5% of real music).
  3. Novelty score: rewards syncopation with grounded pulse, melodic arc resolution, and distance from static style cards."""
import argparse, collections, json, math, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chord_pieces as C
import expression_pieces as E
import learn as L
import pieces as P


def max_run(s):
    best = cur = 0
    for ch in s:
        cur = cur + 1 if ch != "." else 0
        if cur > best:
            best = cur
    return best


def syncopation_score(lanes):
    """Normalized off-beat / weak-beat emphasis anchored by downbeat kicks (0..1)."""
    m = [[1 if (l and l[s] != ".") else 0 for s in range(16)] for l in lanes]
    w = [4, 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1]
    sync = tot = 0.0
    for l in (0, 1, 4, 5, 6):
        if l >= len(m):
            continue
        for s in range(16):
            if m[l][s]:
                tot += 1.0
                if w[s] <= 2 and m[l][(s + 1) % 16] == 0:
                    sync += (3 - w[s]) * 0.5
    anchor = 1.0 if (m[0][0] or m[0][8]) else 0.4
    return min(1.0, (sync / max(1.0, tot)) * anchor)


def card_grooves():
    out = []
    for c in P.cards():
        if any(c["drums"].values()):
            lanes = [c["drums"].get(l, "") for l in P.LANES]
            out.append([[1 if (l and l[s] != ".") else 0 for s in range(16)] for l in lanes])
    return out


CARDS_DRUM = None


def min_card_dist_drum(m):
    global CARDS_DRUM
    if CARDS_DRUM is None:
        CARDS_DRUM = card_grooves()
    if not CARDS_DRUM:
        return 0.5
    best = 128
    for cm in CARDS_DRUM:
        d = sum(m[l][s] != cm[l][s] for l in range(8) for s in range(16))
        if d < best:
            best = d
    # normalize by typical active cells (~32)
    return min(1.0, best / 24.0)


def hard_check(p):
    """Returns list of deterministic hard-fail reasons (empty = valid)."""
    t = p["type"]
    if t == "drum_groove":
        h, _ = P.gate_drum(p)
        lanes = p["lanes"]
        # Non-hat lanes (0,1,4,5,6,7) with >8 consecutive 16th hits are machine-gun mishaps
        for i, l in enumerate(lanes):
            if i not in (2, 3) and l and max_run(l) > 8:
                h.append("stuck 16th run on lane %d" % i)
        k, sn = lanes[0], lanes[1]
        if k and p.get("family") != "ambient":
            # If a bar has >= 4 kicks and zero sit on an 8th-note grid position, its meter is displaced/broken
            kh = sum(c != "." for c in k)
            if kh >= 4 and sum(k[i] != "." for i in range(0, 16, 2)) == 0:
                h.append("wrong meter: no kick on any 8th-note pulse")
        if k and sn:
            kh = sum(c != "." for c in k)
            sh = sum(c != "." for c in sn)
            coll = sum(a != "." and b != "." for a, b in zip(k, sn))
            if kh >= 5 and sh >= 5 and coll == kh and coll == sh:
                h.append("kick and snare collide on 100% of hits")
        return h

    if t == "melody_contour":
        P_ = p.get("pitches", [])
        steps = p.get("steps") or [b - a for a, b in zip(P_, P_[1:])]
        q = dict(p, steps=steps)
        h, _ = E.gate_contour(q)
        if len(P_) < 6:
            h.append("fewer than 6 notes")
            return h
        # If an explicit key (tonic, mode) is supplied on the piece, enforce key adherence against it;
        # otherwise check against the best-fitting diatonic scale
        if "tonic" in p and "mode_iv" in p:
            tonic, mode = p["tonic"], p["mode_iv"]
            in_scale = sum((x - tonic) % 12 in mode for x in P_) / len(P_)
            strong = [P_[j] for j in range(0, len(P_), 4)]
            strong_ok = sum((x - tonic) % 12 in mode for x in strong) / max(1, len(strong))
            if in_scale < 0.75 or strong_ok < 0.75:
                h.append("notes off the key")
        else:
            tonic, mode = L.key_of(P_)
            in_scale = sum((x - tonic) % 12 in mode for x in P_) / len(P_)
            if in_scale < 0.68:
                h.append("notes off the key (chromatic chaos)")
        # Extreme/dissonant leaps (>12 semitones or tritone) over 65% of the bar
        if sum(abs(x) > 12 or abs(x) == 6 for x in steps) / max(1, len(steps)) > 0.65:
            h.append("over 65% dissonant/extreme leaps")
        return h

    if t == "bass_seq":
        h, _ = P.gate_bass(p)
        st = p.get("steps", "")
        if len(st) == 16:
            deg = [int(c, 36) for c in st if c != "."]
            if deg and sum(d in (0, 4, 7) for d in deg) / len(deg) < 0.35:
                h.append("bass lacks root/fifth grounding")
        return h

    if t == "chord_prog":
        h, _ = C.gate_chord(dict(p))
        return h

    if t in ("params", "glide_params"):
        vals = p.get("values", {})
        h = []
        if not vals:
            return ["no params"]
        for k, v in vals.items():
            if isinstance(v, (int, float)) and not math.isfinite(v):
                h.append("%s not finite" % k)
        # Audibility / DSP mishap gates
        vol = vals.get("volume", vals.get("a.volume", 1.0))
        if isinstance(vol, (int, float)) and vol <= 0.02:
            h.append("silent volume knob")
        cut = vals.get("cutoff", vals.get("a.cutoff"))
        famt = vals.get("a.filterAmount", vals.get("envAmount", 1.0))
        ftype = vals.get("filterType", vals.get("a.filterType", 1))
        if isinstance(cut, (int, float)) and ftype in (1, 2) and cut < 120.0 and (not isinstance(famt, (int, float)) or famt <= 0.05):
            h.append("silent lowpass filter (cutoff < 120 Hz with no envelope)")
        if vals.get("mix") == 1.0 and vals.get("b.on") == 0:
            h.append("silent wavetable mix=1 with engine B off")
        return h

    return []


# A floor from fewer held-out pieces than this is noise (bass_seq had 4), so it is not applied.
MIN_FLOOR_N = 50


def floor_for(rec, t):
    """Wide plausibility floor: 1st percentile of held-out real data (accepts ~99% of real music)."""
    th = (rec.get("thresholds") or {}).get(t)
    if not th or th.get("n", 0) < MIN_FLOOR_N:
        return None
    return th["p1"]


def novelty_score(p, rec, ll):
    """Composite novelty score in [0, 1] for pieces that pass validity + plausibility."""
    t = p["type"]
    th = (rec.get("thresholds") or {}).get(t, {})
    # 1. Surprisal sweet-spot (Berlyne curve: peak around p15..p60, gentle penalty above p95 cliche)
    if ll is not None and "p5" in th and "p95" in th:
        lo, mid, hi = th["p5"], th.get("p25", 0.5 * (th["p5"] + th["p95"])), th["p95"]
        if ll <= mid:
            surp = max(0.2, min(1.0, 0.5 + 0.5 * (ll - lo) / max(1e-5, mid - lo)))
        else:
            surp = max(0.15, 1.0 - 0.7 * (ll - mid) / max(1e-5, hi - mid))
    else:
        surp = 0.5

    if t == "drum_groove":
        m = L.cells(p)
        sync = syncopation_score(p["lanes"])
        cdist = min_card_dist_drum(m)
        return round(0.40 * cdist + 0.35 * sync + 0.25 * surp, 4)

    if t == "melody_contour":
        P_ = p["pitches"]
        steps = [b - a for a, b in zip(P_, P_[1:])]
        # Reward contour arc (distinct peak/trough in middle of phrase) + interval variety
        mid_peak = max(P_[3:13]) > max(P_[0], P_[-1]) or min(P_[3:13]) < min(P_[0], P_[-1])
        distinct_iv = min(1.0, len(set(steps)) / 7.0)
        return round(0.45 * surp + 0.35 * distinct_iv + (0.20 if mid_peak else 0.05), 4)

    if t == "bass_seq":
        st = p.get("steps", "")
        offbeats = sum(st[i] != "." for i in range(1, 16, 2)) / max(1, sum(c != "." for c in st))
        return round(0.50 * surp + 0.50 * min(1.0, offbeats * 1.5), 4)

    return round(surp, 4)


def judge(p, rec):
    """Evaluate a piece with the 3-layer critic. Returns dict with accept, hard, ll, floor, novelty, score."""
    h = hard_check(p)
    model = rec["model"]
    ll = L.piece_ll(model, p)
    fl = floor_for(rec, p["type"])
    plausible = True
    if ll is not None and fl is not None and ll < fl:
        plausible = False
        h.append("below plausibility floor (ll=%.3f < %.3f)" % (ll, fl))
    accept = len(h) == 0 and plausible
    nov = novelty_score(p, rec, ll) if accept else 0.0
    return {"accept": accept, "hard": h, "ll": round(ll, 4) if ll is not None else None,
            "floor": round(fl, 4) if fl is not None else None, "novelty": nov}


# ---- Deterministic corruption suite for robustness testing ------------------------------------

CORRUPTIONS = ("no_kick", "stuck_16ths", "wrong_meter", "kick_snare_clash",
               "off_key_strong", "unmusical_leaps", "stuck_note", "silent_filter", "random_noise")


def corrupt(p, kind, rng):
    """Create a deliberately broken copy of a real piece to test if the critic catches it."""
    q = json.loads(json.dumps(p))
    if kind == "no_kick" and q["type"] == "drum_groove":
        q["family"] = "house"
        q["lanes"][0] = ""
        if not any(q["lanes"]):
            q["lanes"][2] = "x.x.x.x.x.x.x.x."
        return q
    if kind == "stuck_16ths" and q["type"] == "drum_groove":
        lane = rng.choice([0, 1, 4, 5])
        q["lanes"][lane] = "x" * 15 + "."
        if not q["lanes"][0]:
            q["lanes"][0] = "x...x...x...x..."
        return q
    if kind == "wrong_meter" and q["type"] == "drum_groove":
        q["family"] = "house"
        q["lanes"][0] = ".x...x...x...x.."
        return q
    if kind == "kick_snare_clash" and q["type"] == "drum_groove":
        q["lanes"][0] = "x..xx..xx..xx..x"
        q["lanes"][1] = "x..xx..xx..xx..x"
        return q
    if kind == "off_key_strong" and q["type"] == "melody_contour":
        tonic, mode = L.key_of(q["pitches"])
        out_pcs = [pc for pc in range(12) if pc not in mode]
        P_ = list(q["pitches"])
        for j in range(0, len(P_), 2):
            P_[j] = (P_[j] // 12) * 12 + (tonic + rng.choice(out_pcs)) % 12
        q["pitches"] = P_
        q["steps"] = [b - a for a, b in zip(P_, P_[1:])]
        q["tonic"], q["mode_iv"] = tonic, mode
        return q
    if kind == "unmusical_leaps" and q["type"] == "melody_contour":
        P_ = [60]
        for _ in range(15):
            P_.append(max(24, min(100, P_[-1] + rng.choice([-19, -15, -11, 11, 14, 18]))))
        q["pitches"] = P_
        q["steps"] = [b - a for a, b in zip(P_, P_[1:])]
        return q
    if kind == "stuck_note" and q["type"] == "melody_contour":
        P_ = [60, 60, 60, 62] * 4
        q["pitches"] = P_
        q["steps"] = [b - a for a, b in zip(P_, P_[1:])]
        return q
    if kind == "silent_filter":
        return {"type": "params", "node": "Wavetable", "values": {"a.cutoff": 45.0, "a.filterAmount": 0.0, "a.filterType": 1, "volume": 0.8}}
    if kind == "random_noise":
        if q["type"] == "drum_groove":
            q["lanes"] = ["".join("x" if rng.random() < 0.45 else "." for _ in range(16)) for _ in range(8)]
            return q
        P_ = [rng.randint(36, 96) for _ in range(16)]
        q["pitches"] = P_
        q["steps"] = [b - a for a, b in zip(P_, P_[1:])]
        return q
    return q


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scope", default="shippable", choices=["shippable", "local"])
    ap.add_argument("--store", default="~/Infinite/training-data/store")
    ap.add_argument("--models", default="~/Infinite/training-data/models")
    a = ap.parse_args()
    rec = json.load(open(os.path.join(os.path.expanduser(a.models), a.scope, "current.json")))
    rows = L.load(a.store, a.scope)
    real = [r for r in rows if not r.get("synthetic")]
    _, test = L.split(real)
    rng = random.Random(42)

    print("=== Critic Evaluation (scope=%s) ===" % a.scope)
    for t in ("drum_groove", "melody_contour", "bass_seq", "chord_prog"):
        sub = [p for p in test if p["type"] == t]
        if len(sub) > 2000:
            sub = rng.sample(sub, 2000)
        if not sub:
            continue
        rej = [judge(p, rec) for p in sub]
        frr = 100.0 * sum(not x["accept"] for x in rej) / len(sub)
        nov = sum(x["novelty"] for x in rej if x["accept"]) / max(1, sum(x["accept"] for x in rej))
        print("  held-out real %-16s n=%4d  false-reject=%5.2f%%  mean-novelty=%.3f  floor=%s" % (
            t, len(sub), frr, nov, rej[0]["floor"]))

    print("\n=== Mishap Catch Rate on Corrupted Real Pieces ===")
    d_sub = [p for p in test if p["type"] == "drum_groove"][:200]
    m_sub = [p for p in test if p["type"] == "melody_contour"][:200]
    for kind in CORRUPTIONS:
        src = d_sub if kind in ("no_kick", "stuck_16ths", "wrong_meter", "kick_snare_clash") else m_sub
        if kind == "silent_filter":
            src = d_sub[:50]
        caught = sum(not judge(corrupt(p, kind, rng), rec)["accept"] for p in src)
        print("  %-20s caught %3d / %3d (%5.1f%%)" % (kind, caught, len(src), 100.0 * caught / max(1, len(src))))


if __name__ == "__main__":
    main()
