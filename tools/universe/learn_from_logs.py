#!/usr/bin/env python3
"""Models learn from the local learning log (PLAN-2026-10-30 item 21, close doc section 5.3).
    learn_from_logs.py learn [--dir D]     read D/takes, fit pick weights (note bank lines and progressions, voices) and the
                                           Vital/designed share per part, gate them on held-out sessions, and write
                                           D/profile/learned_weights.json only when the gate passes
    learn_from_logs.py selftest            two synthetic logs: one with a hidden preference (the gate must pass and find it),
                                           one with none (the gate must refuse)
D defaults to ~/Library/Application Support/Infinite/learning. A take is one Generate Samples render; its picks are what
the compiler drew (Compiled::picks). Gate: on the last quarter of sessions, the self-normalised importance estimate of the
keep rate under the new weights beats the old keep rate, and so does its bootstrap 5th percentile. Too few takes: no
weights, said so."""
import argparse, glob, json, math, os, random, shutil, sys, tempfile
from collections import defaultdict

DEFAULT = os.path.expanduser("~/Library/Application Support/Infinite/learning")
MIN_TRAIN, MIN_HELD = 200, 60
PRIOR = 20.0           # pseudo-takes at the base keep rate behind every weight
CLIP = (0.5, 2.0)      # no pick is ever ruled out, none more than doubled


def load_takes(d):
    takes, order = {}, []
    for f in sorted(glob.glob(os.path.join(d, "takes", "*.jsonl"))):
        for line in open(f):
            try:
                j = json.loads(line)
            except ValueError:
                continue
            i = j.get("id")
            if not i:
                continue
            if i not in takes:
                takes[i] = {}; order.append(i)
            t = takes[i]
            kept = t.get("outcome") == "kept"
            t.update({k: v for k, v in j.items() if k != "outcome"})
            if "outcome" in j:
                t["outcome"] = "kept" if kept or j["outcome"] == "kept" else j["outcome"]
    return [takes[i] for i in order if "outcome" in takes[i] and "part" in takes[i]]


def kind(pick):
    """The pool a pick was drawn from: weights are compared only inside it."""
    p = pick.split(":")
    return ":".join(p[:3]) if p[0] == "voice" and p[1] == "vital" else ":".join(p[:2]) if p[0] == "voice" else p[0]


def keys(t):
    return ["%s|%s" % (t["part"], p) for p in t.get("picks", [])]


def fit(train):
    base = sum(t["outcome"] == "kept" for t in train) / max(1, len(train))
    n, k = defaultdict(float), defaultdict(float)
    for t in train:
        for key in set(keys(t)):
            n[key] += 1; k[key] += t["outcome"] == "kept"
    w = {}
    for key in n:
        rate = (k[key] + PRIOR * base) / (n[key] + PRIOR)
        w[key] = min(CLIP[1], max(CLIP[0], rate / max(base, 1e-6)))
    # Normalise inside each pool so the weights only move draws between its members.
    pools = defaultdict(list)
    for key in w:
        part, pick = key.split("|", 1); pools[(part, kind(pick))].append(key)
    for members in pools.values():
        m = sum(w[x] for x in members) / len(members)
        for x in members:
            w[x] /= m
    return base, w, {key: int(n[key]) for key in n}


def ratio(t, w):
    r = 1.0
    for key in set(keys(t)):
        r *= w.get(key, 1.0)
    return r


def estimate(held, w):
    r = [ratio(t, w) for t in held]
    return sum(ri * (t["outcome"] == "kept") for ri, t in zip(r, held)) / max(1e-9, sum(r))


def voice_shares(train):
    out = {}
    for part in sorted({t["part"] for t in train}):
        rows = defaultdict(lambda: [0, 0])
        for t in train:
            if t["part"] != part:
                continue
            vs = [p for p in t.get("picks", []) if p.startswith("voice:")]
            if not vs:
                continue
            src = vs[-1].split(":")[1]  # the later voice plays
            rows[src][0] += 1; rows[src][1] += t["outcome"] == "kept"
        if rows:
            out[part] = {s: {"n": n, "kept": k, "rate": round(k / n, 3)} for s, (n, k) in rows.items()}
    return out


def learn(d, write=True, quiet=False):
    takes = load_takes(d)
    sessions = []
    for t in takes:
        if t.get("session") not in sessions:
            sessions.append(t.get("session"))
    cut = sessions[: max(1, len(sessions) - max(1, len(sessions) // 4))] if len(sessions) > 1 else sessions
    train = [t for t in takes if t.get("session") in cut]
    held = [t for t in takes if t.get("session") not in cut]
    res = {"takes": len(takes), "sessions": len(sessions), "train": len(train), "held_out": len(held)}
    if len(train) < MIN_TRAIN or len(held) < MIN_HELD:
        res.update(verdict="not enough data", need={"train": MIN_TRAIN, "held_out": MIN_HELD})
        if not quiet:
            print(json.dumps(res))
        return res
    base, w, n = fit(train)
    old = sum(t["outcome"] == "kept" for t in held) / len(held)
    new = estimate(held, w)
    rng = random.Random(7); boots = []
    for _ in range(400):
        s = [held[rng.randrange(len(held))] for _ in held]
        boots.append(estimate(s, w) - sum(t["outcome"] == "kept" for t in s) / len(s))
    boots.sort(); lo = boots[int(0.05 * len(boots))]
    passed = new > old and lo > 0.0
    top = sorted(w.items(), key=lambda kv: -kv[1])[:8]
    res.update(base_keep=round(base, 3), held_keep=round(old, 3), held_keep_new=round(new, 3), lift_p5=round(lo, 4),
               verdict="PASS" if passed else "MISS", top=[(k, round(v, 2), n[k]) for k, v in top],
               voice_shares=voice_shares(train))
    kept_audio = sum(1 for f in glob.glob(os.path.join(d, "audio", "*.jsonl")) for _ in open(f))
    res["kept_audio_features"] = kept_audio
    res["spectral_and_critic_refit"] = "needs >= 200 kept takes with features (have %d); not fitted here" % kept_audio
    if passed and write:
        os.makedirs(os.path.join(d, "profile"), exist_ok=True)
        json.dump({"weights": {k: round(v, 4) for k, v in w.items()}, "gate": {k: res[k] for k in
                   ("held_keep", "held_keep_new", "lift_p5", "train", "held_out")}},
                  open(os.path.join(d, "profile", "learned_weights.json"), "w"), indent=1)
    if not quiet:
        print(json.dumps(res, indent=1))
    return res


def fixture(d, prefer, sessions=24, per=20, seed=3):
    """Synthetic takes: synths parts drawing line 0..7 and a Vital or designed voice; `prefer` line keeps 55%, others
    20% (prefer None: every take 25%)."""
    rng = random.Random(seed); os.makedirs(os.path.join(d, "takes"))
    with open(os.path.join(d, "takes", "20261030.jsonl"), "w") as f:
        for s in range(sessions):
            for i in range(per):
                line = rng.randrange(8); vital = rng.random() < 0.5
                p = 0.25 if prefer is None else (0.55 if line == prefer else 0.20)
                picks = ["line:%d" % line, "voice:vital:synth:%d" % rng.randrange(5) if vital else "voice:designed:%d" % rng.randrange(6)]
                tid = "s%02d-%d" % (s, i)
                f.write(json.dumps({"id": tid, "session": "s%02d" % s, "part": "synths", "style": "deep_house", "picks": picks}) + "\n")
                f.write(json.dumps({"id": tid, "outcome": "kept" if rng.random() < p else rng.choice(["rerolled", "ignored", "edited"])}) + "\n")


def selftest():
    fails = 0
    for name, prefer, want in (("preference", 3, "PASS"), ("none", None, "MISS")):
        d = tempfile.mkdtemp(prefix="learn_fixture_")
        try:
            fixture(d, prefer)
            r = learn(d, write=True, quiet=True)
            found = prefer is None or (r.get("top") and r["top"][0][0] == "synths|line:%d" % prefer)
            wrote = os.path.exists(os.path.join(d, "profile", "learned_weights.json"))
            ok = r.get("verdict") == want and found and wrote == (want == "PASS")
            fails += not ok
            print("%-10s verdict %s (want %s), held keep %s -> %s, lift p5 %s, top %s, wrote %s  %s" % (
                name, r.get("verdict"), want, r.get("held_keep"), r.get("held_keep_new"), r.get("lift_p5"),
                r.get("top", [[None]])[0][0], wrote, "ok" if ok else "FAIL"))
        finally:
            shutil.rmtree(d, ignore_errors=True)
    d = tempfile.mkdtemp(prefix="learn_fixture_")
    try:
        fixture(d, 3, sessions=4, per=10)
        r = learn(d, quiet=True); ok = r["verdict"] == "not enough data"; fails += not ok
        print("small      verdict %s  %s" % (r["verdict"], "ok" if ok else "FAIL"))
    finally:
        shutil.rmtree(d, ignore_errors=True)
    print("LEARN SELFTEST %s" % ("PASS" if fails == 0 else "FAIL"))
    return fails == 0


def main():
    ap = argparse.ArgumentParser(); sp = ap.add_subparsers(dest="cmd", required=True)
    l = sp.add_parser("learn"); l.add_argument("--dir", default=DEFAULT)
    sp.add_parser("selftest")
    a = ap.parse_args()
    if a.cmd == "learn":
        learn(a.dir)
    else:
        sys.exit(0 if selftest() else 1)


if __name__ == "__main__":
    main()
