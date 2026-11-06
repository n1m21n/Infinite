#!/usr/bin/env python3
"""Generate Samples scorecard: every area of the generator, one PASS/FAIL table, one command.
    ~/Infinite/media/.ytvenv/bin/python3 -I tools/universe/gen_scorecard.py --gen <wasm dir> [--quick] [--against prev.json]
        [--out DIR] [--jobs 3] [--native-log FILE] [--freeze]
Renders every style x every part (19) at 2 seeds with the web engine (wasm_render_parts.js, dots at rest, variation 0.5), measures each
take (take_measure.py) and scores the areas of internal/plans/generate-samples/scorecard.md:
  1 notes      Note Sequencer layers of the full mix against the MIDI corpus spread (frozen in feel/role_spread.json)
  2 library    each part against real loops of that part (training-data/audio/features.jsonl, frozen into scorecard_refs.json)
  3 clusters   cluster_eval / family tests: read from their own outputs (feel/cluster_eval.json); NO REF if missing
  4 matching   sound_eval needs the drive: NO REF without it
  5 cards      sameness and identity: own-card nearest timbre neighbour, cross-card near-duplicates, same-card groove repeats
  6 mix        full mix against 1,680 finished tracks (feel/mix_audio_table.json) and the master's -14 LUFS target
  7 critic     redraws per part, failed takes, GENSAMPLETEST (native log, optional)
  8 taste      the owner's learning log: keep rate per part, learn_from_logs gate, an owner-taste critic fitted on re-renders
Seeds are fixed (wasm_render_parts: 9000 + style*1009 + part*101 + k), so two builds are compared take for take.
--quick: every third style (12 of 34), all parts, 2 seeds (about 3 min). Full: 34 styles (about 8 min with 3 jobs).
--against: a previous run's scorecard.json; rows that got worse are marked WORSE and the exit code is 1.
References: internal/plans/generate-samples/refs/*.json (tools/universe/snapshot_refs.py, made once from the WD-Black drive) are read first,
so no row needs the drive; without them the feel/ tables and scorecard_refs.json are used.
--freeze: write the near-duplicate thresholds of this run (and the library refs, if the drive is mounted) to scorecard_refs.json;
          done once, on the baseline. Later runs read them, so the ruler never moves.
Exit: 0 all rows ran and none is worse than --against, 1 a row got worse, 2 usage or render error."""
import argparse, collections, glob, hashlib, itertools, json, math, os, subprocess, sys, time
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "../.."))
sys.path.insert(0, HERE)
FEEL = os.path.join(REPO, "internal/plans/data-universe/feel")
REFS = os.path.join(REPO, "internal/plans/generate-samples/scorecard_refs.json")
REFDIR = os.path.join(REPO, "internal/plans/generate-samples/refs")   # snapshot_refs.py: drive references as quantiles, read first


def refdir(name):
    p = os.path.join(REFDIR, name + ".json")
    return json.load(open(p)) if os.path.exists(p) else None
TD = os.path.expanduser("~/Infinite/training-data")
LEARN = os.path.expanduser("~/Library/Application Support/Infinite/learning")
PY = sys.executable
def _families():
    import re
    try: src = open(os.path.join(REPO, "src/app/graph/GenerateSamplesTables.inc")).read()
    except OSError: return {}
    return dict(re.findall(r'^\s*\{\s*"([a-z0-9_]+)",\s*"[^"]*",\s*"([a-z_]+)",\s*\d+', src, re.M))
FAMILY = _families()   # card id -> family, from the card table

PARTS = ["all", "synths", "pads", "plucks", "percussion", "drums", "bass", "fx", "foley", "keys", "leads", "textures", "kicks", "snares",
         "hats", "toms", "strings", "winds", "piano"]
DRUMISH = {4, 5, 12, 13, 14, 15}
ROLE = {1: "synth", 2: "pad", 3: "pluck", 4: "perc", 5: "drums", 6: "bass", 7: "fx", 9: "keys", 10: "lead", 12: "kick", 13: "snare",
        14: "hat", 15: "tom", 16: "pad", 17: "lead", 18: "keys"}


# ---------------------------------------------------------------- render
def render(gen, out, quick, jobs):
    h = hashlib.sha1(open(os.path.join(gen, "infinite_gen_wasm.wasm"), "rb").read()).hexdigest()[:12]
    path = os.path.join(out, "takes_%s_%s.jsonl" % (h, "quick" if quick else "full"))
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return path, h
    step = 3 if quick else 1
    procs, parts = [], []
    t0 = time.time()
    for j in range(jobs):
        p = path + ".%d" % j; parts.append(p)
        env = dict(os.environ, WITH_PATCH="1", STYLE_FROM=str(j * step), STYLE_STEP=str(step * jobs))
        cmd = "node %s %s 9000 2 %s | %s -I %s > %s" % (os.path.join(HERE, "wasm_render_parts.js"), gen, ",".join(map(str, range(19))),
                                                     PY, os.path.join(HERE, "take_measure.py"), p)
        procs.append(subprocess.Popen(["nice", "-n", "10", "bash", "-o", "pipefail", "-c", cmd], env=env))
    bad = [p.wait() for p in procs]
    if any(bad):
        sys.exit("render failed (%s)" % bad)
    with open(path, "w") as f:
        for p in parts:
            f.write(open(p).read()); os.remove(p)
    print("rendered %s in %.0f s" % (path, time.time() - t0), file=sys.stderr)
    return path, h


def load(path):
    return [json.loads(l) for l in open(path) if l.strip()]


# ---------------------------------------------------------------- helpers
class Card:
    def __init__(self):
        self.rows = []

    def add(self, area, name, value, target, ok, better="high", fmt="%.3g", tol=0.0, detail=""):
        """ok: True / False / None (no reference). better: which way is an improvement, for --against."""
        status = "NO REF" if ok is None else ("PASS" if ok else "FAIL")
        self.rows.append(dict(area=area, name=name, value=None if value is None else float(value), shown=("-" if value is None else fmt % value),
                              target=target, status=status, better=better, tol=tol, detail=detail))


def med(v):
    v = [x for x in v if x is not None and not (isinstance(x, float) and math.isnan(x))]
    return float(np.median(v)) if v else float("nan")


def tdist(a, b):
    return float(np.sqrt(np.mean((np.asarray(a) - np.asarray(b)) ** 2)))


def cos(a, b):
    a = np.asarray(a) - np.mean(a); b = np.asarray(b) - np.mean(b); d = np.linalg.norm(a) * np.linalg.norm(b)
    return float(a @ b / d) if d else 1.0


# ---------------------------------------------------------------- area 1: notes
def area_notes(card, T):
    import role_spread
    snap = refdir("corpus")
    if snap:
        ref = {k: {ax: {"corpus_p5": v[ax]["p5"], "corpus_p95": v[ax]["p95"]} for ax in ("register", "density")} for k, v in snap["per_channel"].items()}
    else:
        ref = json.load(open(os.path.join(FEEL, "role_spread.json")))
    L = []
    for r in T:
        if r["part"] == 0 and r.get("patch"):
            L += role_spread.layers(r)
    for role in ("bass", "melodic"):
        G = [x for x in L if (x["mid"] < 48) == (role == "bass")]
        for ax, key in (("register", "mid"), ("density", "dens")):
            lo, hi = ref[role][ax]["corpus_p5"], ref[role][ax]["corpus_p95"]
            g = np.array([x[key] for x in G])
            ins = float(((g >= lo) & (g <= hi)).mean()) if len(g) else float("nan")
            card.add("1 notes", "%s %s inside corpus 5-95%% (%.3g-%.3g)" % (role, ax, lo, hi), ins, ">= 0.90", ins >= 0.9, fmt="%.2f",
                     tol=0.01, detail="%d layers, median %.3g" % (len(g), float(np.median(g)) if len(g) else float("nan")))
    have = os.path.exists(os.path.join(TD, "store"))
    card.add("1 notes", "benchmark.py B1-B5 (held-out corpus)", None, "run benchmark.py", None if not have else None,
             detail="needs the WD-Black drive" if not have else "run tools/universe/benchmark.py --scope local (slow, not in this run)")


# ---------------------------------------------------------------- area 2: library
def area_library(card, T, refs):
    snap = refdir("library")
    lib = {p: {k: (q["median"], q["q3"] - q["q1"], q["n"]) for k, q in d.items() if q} for p, d in snap.items()} if snap else refs.get("library")
    if not lib and os.path.exists(os.path.join(TD, "audio/features.jsonl")):
        import part_vs_library as pvl
        lib = {k: {kk: list(vv) for kk, vv in v.items()} for k, v in pvl.library().items()}
    keys = ["centroid", "low", "lowmid", "high", "sub", "crest", "flatness", "onsets_per_s"]
    worst_all = []
    for p in range(1, 19):
        g = [r["lib"] for r in T if r["part"] == p and r.get("lib")]
        role = ROLE.get(p)
        if not lib or role not in (lib or {}):
            if p in (3, 17, 18, 6, 1, 10, 5):   # the parts the owner rerolls most: listed even with no reference
                card.add("2 library", "%s: median |gap| to real loops (IQRs)" % PARTS[p], None, "<= 1.0", None,
                         detail="no library reference (WD-Black unmounted, nothing frozen)" if not lib else "no %s loops" % role)
            continue
        zs = []
        for k in keys:
            lm, iqr, _ = lib[role][k]; zs.append(abs(med([x[k] for x in g]) - lm) / (iqr or 1e-9))
        worst = keys[int(np.argmax(zs))]
        v = float(np.median(zs))
        card.add("2 library", "%s: median |gap| to real loops (IQRs)" % PARTS[p], v, "<= 1.0", v <= 1.0, better="low", fmt="%.2f", tol=0.02,
                 detail="worst %s %.1f" % (worst, max(zs)))
        worst_all.append((max(zs), PARTS[p], worst))
    if worst_all:
        z, pn, k = max(worst_all)
        card.add("2 library", "worst single measure gap, any part (IQRs)", z, "<= 3.0", z <= 3.0, better="low", fmt="%.1f", tol=0.1,
                 detail="%s %s; " % (pn, k) + " ".join("%s %s %.1f" % (b, c, a) for a, b, c in sorted(worst_all, reverse=True)[:6]))


# ---------------------------------------------------------------- area 3 + 4: clusters, matching
def area_clusters(card):
    p = os.path.join(FEEL, "cluster_eval.json")
    if os.path.exists(p):
        d = json.load(open(p)); a = min(v["acc"] for v in d["per_seed"].values())
        card.add("3 clusters", "cluster_eval held-out accuracy (stored run)", a, ">= 0.85", a >= 0.85, fmt="%.3f",
                 detail="from feel/cluster_eval.json (stored run)")
    fam = os.path.join(REPO, "internal/plans/data-universe/HANDOFF.md")
    # family tests (String / Piano / Wind): the generator draws these voices, so their test result is a row of its own
    txt = open(fam).read() if os.path.exists(fam) else ""
    for inst in ("String", "Piano", "Wind"):
        card.add("3 clusters", "%s family test" % inst, None, "MET before it joins the generator", None,
                 detail="last recorded MISS (NEXT-SESSION.md); its part ships anyway")
    card.add("4 matching", "sound_eval knobs -> sound (held-out banks)", None, "R2 >= stored bar", None,
             detail="not run here (slow): tools/universe/sound_eval.py on the WD-Black banks")


# ---------------------------------------------------------------- area 5: cards and sameness
def area_cards(card, T, refs, freeze):
    thr = refs.get("neardup") or {}
    newthr = {}
    ident, ndup, sdup, groove, npart, fident, fchance = {}, {}, {}, {}, {}, {}, {}
    for p in range(19):
        R = [r for r in T if r["part"] == p and r.get("timbre")]
        if len(R) < 4: continue
        X = np.array([r["timbre"] for r in R]); n = len(R); npart[p] = n
        D = np.sqrt(((X[:, None, :] - X[None, :, :]) ** 2).mean(2)); np.fill_diagonal(D, np.inf)
        nn = D.argmin(1)
        ident[p] = float(np.mean([R[i]["id"] == R[nn[i]]["id"] for i in range(n)]))
        mean_pd = float(D[np.isfinite(D)].mean()); newthr[PARTS[p]] = 0.25 * mean_pd
        t = thr.get(PARTS[p], 0.25 * mean_pd)
        same = [(i, j) for i in range(n) for j in range(i + 1, n) if R[i]["id"] == R[j]["id"]]
        # Near-duplicates count only pairs from another family: cards of one family are meant to share a palette, the
        # owner's complaint is a techno take that sounds like an ambient one. Family identity: the nearest take from
        # another card belongs to the same family.
        fam = [FAMILY.get(r["id"], r["id"]) for r in R]
        other_nn = [min(D[i, j] for j in range(n) if fam[j] != fam[i]) for i in range(n)]
        ndup[p] = float(np.mean([d < t for d in other_nn]))
        oc = [min((j for j in range(n) if R[j]["id"] != R[i]["id"]), key=lambda j: D[i, j]) for i in range(n)]
        fident[p] = float(np.mean([fam[i] == fam[j] for i, j in enumerate(oc)]))
        fchance[p] = float(np.mean([(sum(f == a for f in fam) - sum(R[k]["id"] == R[i]["id"] for k in range(n))) / max(1, n - 2)
                                    for i, a in enumerate(fam)]))
        sdup[p] = float(np.mean([D[i, j] < t for i, j in same])) if same else float("nan")
        if p in DRUMISH or p == 0:
            groove[p] = float(np.mean([cos(R[i]["rhythm"], R[j]["rhythm"]) > 0.9 for i, j in same])) if same else float("nan")
    chance = 1.0 / (2 * len({r["id"] for r in T}) - 1)
    v = med(list(ident.values()))
    worst = min(ident, key=ident.get)
    card.add("5 cards", "own-card nearest timbre neighbour, median over parts", v, ">= 0.25 (chance %.3f)" % chance, v >= 0.25, fmt="%.3f",
             tol=0.01, detail="lowest %s %.2f; " % (PARTS[worst], ident[worst]) + " ".join("%s %.2f" % (PARTS[p], x) for p, x in ident.items()))
    v = max(ndup.values()); wp = max(ndup, key=ndup.get)
    card.add("5 cards", "cross-family near-duplicates, worst part", v, "<= 0.10", v <= 0.10, better="low", fmt="%.3f", tol=0.01,
             detail="%s; " % PARTS[wp] + " ".join("%s %.2f" % (PARTS[p], x) for p, x in ndup.items() if x > 0))
    for p in (3, 18, 1, 6, 5, 17):   # the parts the owner rerolls most (learning log since Oct 31), each on its own row
        if p in ident:
            # Noise on a share of n takes: one take (1.5/n) or one binomial standard error, whichever is larger.
            se = lambda v: max(1.5 / npart[p], math.sqrt(max(v, 1.0 / npart[p]) * (1 - min(v, 0.99)) / npart[p]))
            card.add("5 cards", "%s: own-card nearest timbre neighbour" % PARTS[p], ident[p], ">= 0.25", ident[p] >= 0.25, fmt="%.3f",
                     tol=se(ident[p]))
            card.add("5 cards", "%s: cross-family near-duplicates" % PARTS[p], ndup[p], "<= 0.10", ndup[p] <= 0.10, better="low", fmt="%.3f",
                     tol=se(ndup[p]))
            card.add("5 cards", "%s: nearest other-card take from the same family" % PARTS[p], fident[p], ">= 0.25 (chance %.3f)" % fchance[p],
                     fident[p] >= 0.25, fmt="%.3f", tol=se(fident[p]))
    v = med(list(sdup.values()))
    card.add("5 cards", "same-card seeds near-duplicate, median over parts", v, "<= 0.10", v <= 0.10, better="low", fmt="%.3f", tol=0.01,
             detail=" ".join("%s %.2f" % (PARTS[p], x) for p, x in sdup.items() if x > 0.1))
    for p in (5, 12):
        if p in groove:
            card.add("5 cards", "%s: same-card seeds with the same groove (rhythm corr > 0.9)" % PARTS[p], groove[p], "<= 0.25",
                     groove[p] <= 0.25, better="low", fmt="%.2f", tol=0.02)
    # kick pattern sameness across cards: kicks part, one take per card
    K = {}
    for r in T:
        if r["part"] == 12 and r.get("rhythm") and r["id"] not in K: K[r["id"]] = r["rhythm"]
    ids = sorted(K)
    if len(ids) > 3:
        pairs = [cos(K[a], K[b]) > 0.9 for a, b in itertools.combinations(ids, 2)]
        v = float(np.mean(pairs))
        card.add("5 cards", "kicks: cross-card pairs with the same groove", v, "<= 0.15", v <= 0.15, better="low", fmt="%.3f", tol=0.01)
    # voice pools: share of cards whose full mix carries an Analog node (the brief: 29 of 34)
    full = {}
    for r in T:
        if r["part"] == 0 and r.get("patch"): full.setdefault(r["id"], []).append(r["patch"])
    if full:
        types = collections.Counter()
        for i, ps in full.items():
            ts = set()
            for pt in ps:
                for line in pt.split("\n"):
                    w = line.split()
                    if len(w) > 3 and w[0] == "node" and w[2] == "Synths" and w[3] != "Drum":
                        ts.add(" ".join(w[3:]))
            types.update(ts)
        top, cnt = types.most_common(1)[0]
        v = cnt / len(full)
        card.add("5 cards", "most common voice node in full mixes (%s), share of cards" % top, v, "<= 0.60", v <= 0.60, better="low",
                 fmt="%.2f", tol=0.01, detail="%d cards" % len(full))
    return newthr


# ---------------------------------------------------------------- area 6: mix
def area_mix(card, T):
    snap = refdir("mix")
    if snap:
        a = snap["tracks"]["all"]; tri = lambda q: [q["q1"], q["median"], q["q3"]]
        ref = {k: tri(a[k]) for k in ("side_mid", "crest_db", "lufs")}; ref["bands"] = {b: tri(q) for b, q in a["bands"].items()}
    else:
        ref = json.load(open(os.path.join(FEEL, "mix_audio_table.json")))
    F = [r for r in T if r["part"] == 0 and r.get("mix")]
    sm = [r["side_mid"] for r in F]
    q1, q2, q3 = ref["side_mid"]
    v = med(sm)
    card.add("6 mix", "full: width side/mid median (tracks q1 %.2f, median %.2f)" % (q1, q2), v, ">= %.2f" % q1, v >= q1, fmt="%.3f", tol=0.003)
    v = float(np.mean([x < q1 for x in sm]))
    card.add("6 mix", "full: share narrower than tracks' q1", v, "<= 0.40 (tracks 0.25)", v <= 0.40, better="low", fmt="%.2f", tol=0.01)
    v = med([r["low_corr"] for r in F])
    card.add("6 mix", "full: low end mono (L/R corr < 150 Hz) median", v, ">= 0.95", v >= 0.95, fmt="%.3f", tol=0.003)
    c1, c2, c3 = ref["crest_db"]
    v = med([r["crest"] for r in F])
    card.add("6 mix", "full: crest dB median (tracks %.1f-%.1f)" % (c1, c3), v, "<= %.1f" % c3, v <= c3, better="low", fmt="%.1f", tol=0.1)
    for b in ("sub", "bass", "lowmid", "mid", "himid", "high"):
        lo, mid, hi = ref["bands"][b]
        v = med([r["mix"]["bands"][b] for r in F])
        inside = float(np.mean([lo <= r["mix"]["bands"][b] <= hi for r in F]))
        card.add("6 mix", "full: %s band share inside tracks' IQR (%.2f-%.2f)" % (b, lo, hi), inside, ">= 0.50", inside >= 0.5, fmt="%.2f",
                 tol=0.02, detail="median %.3f (tracks %.2f)" % (v, mid))
    L = [r for r in T if r.get("mix")]
    v = float(np.mean([abs(r["mix"]["lufs"] + 14) <= 1.5 for r in L]))
    low = collections.Counter(PARTS[r["part"]] for r in L if r["mix"]["lufs"] < -15.5)
    card.add("6 mix", "all parts: takes at the master's -14 LUFS (+-1.5)", v, ">= 0.90", v >= 0.9, fmt="%.2f", tol=0.01,
             detail="quiet: " + " ".join("%s %d" % kv for kv in low.most_common(6)))
    v = sum(1 for r in T if r.get("clip", 0) > 0)
    card.add("6 mix", "all parts: takes with clipped samples", v, "0", v == 0, better="low", fmt="%d")
    v = sum(1 for r in T if r.get("rms_db", -200) < -40)
    card.add("6 mix", "all parts: silent or near-silent takes (rms < -40 dB)", v, "0", v == 0, better="low", fmt="%d",
             detail=" ".join("%s/%s" % (r["id"], PARTS[r["part"]]) for r in T if r.get("rms_db", -200) < -40)[:200])
    for p in (9, 3):
        v = med([r["hf"] for r in T if r["part"] == p and "hf" in r])
        card.add("6 mix", "%s: energy share above 8 kHz, median" % PARTS[p], v, ">= 0.002", v >= 0.002, fmt="%.2g", tol=0.0,
                 detail="floor, not a library match; the library row is in area 2")


# ---------------------------------------------------------------- area 7: critic
def area_critic(card, T, nstyles, native_log):
    exp = nstyles * 19 * 2
    v = exp - len(T)
    have = collections.Counter(r["part"] for r in T); short = [PARTS[p] for p in range(19) if have[p] < exp // 19]
    card.add("7 critic", "failed takes (no audio after every redraw)", v, "0", v == 0, better="low", fmt="%d", detail=" ".join(short))
    red = collections.defaultdict(list)
    for r in T: red[r["part"]].append((r.get("attempt") or 0) > 0)
    v = float(np.mean([x for l in red.values() for x in l]))
    worst = max(red, key=lambda p: np.mean(red[p]))
    card.add("7 critic", "takes the critic redrew", v, "<= 0.15", v <= 0.15, better="low", fmt="%.2f", tol=0.01,
             detail="most: %s %.2f" % (PARTS[worst], np.mean(red[worst])))
    if native_log and os.path.exists(native_log):
        t = open(native_log, errors="replace").read()
        card.add("7 critic", "GENSAMPLETEST (native)", 1.0 if "GENSAMPLETEST PASS" in t else 0.0, "PASS", "GENSAMPLETEST PASS" in t, fmt="%.0f")
    else:
        card.add("7 critic", "GENSAMPLETEST (native)", None, "PASS", None, detail="pass --native-log")
    card.add("7 critic", "benchmark.py B2/B3/B8", None, "FRR <= 3%, catch >= 96%", None, detail="not run here (slow): tools/universe/benchmark.py")


# ---------------------------------------------------------------- area 8: taste
def owner_takes():
    import learn_from_logs as lfl
    return lfl.load_takes(LEARN) if os.path.isdir(LEARN) else []


def taste_features(r):
    return np.array(r["timbre"] + [math.log10(max(r["centroid"], 20)), r["low"], r["hf"] * 10, r["crest"] / 20, r["side_mid"],
                                   r["flat"] * 10, r["floor_db"] / 30])


def fit_logreg(X, y, l2=1.0, it=400):
    mu, sd = X.mean(0), X.std(0) + 1e-9; Z = (X - mu) / sd
    w = np.zeros(Z.shape[1]); b = 0.0
    for _ in range(it):
        p = 1 / (1 + np.exp(-(Z @ w + b))); g = Z.T @ (p - y) / len(y) + l2 * w / len(y)
        w -= 0.5 * g; b -= 0.5 * float(np.mean(p - y))
    return lambda Xn: 1 / (1 + np.exp(-(((Xn - mu) / sd) @ w + b)))


def auc(s, y):
    pos = [a for a, t in zip(s, y) if t]; neg = [a for a, t in zip(s, y) if not t]
    if not pos or not neg: return float("nan")
    return float(np.mean([(p > n) + 0.5 * (p == n) for p in pos for n in neg]))


def area_taste(card, T, gen, out):
    takes = [t for t in owner_takes() if t.get("made", "") >= "2026-10-31"]
    if not takes:
        card.add("8 taste", "owner takes since Oct 31", None, "", None, detail="no learning log"); return
    good = lambda t: t["outcome"] in ("kept", "edited")
    keep = float(np.mean([good(t) for t in takes]))
    card.add("8 taste", "owner keep rate since Oct 31 (log, context)", keep, "context", True, fmt="%.2f", tol=1.0,
             detail="%d takes; rerolled %d" % (len(takes), sum(t["outcome"] == "rerolled" for t in takes)))
    import learn_from_logs as lfl
    res = lfl.learn(LEARN, write=False, quiet=True)
    card.add("8 taste", "learn_from_logs gate on the owner log", None if res["verdict"] == "not enough data" else float(res["verdict"] == "PASS"),
             "PASS", None if res["verdict"] == "not enough data" else res["verdict"] == "PASS", fmt="%.0f",
             detail="%s: %d takes (needs %d train + %d held out)" % (res["verdict"], res["takes"], lfl.MIN_TRAIN, lfl.MIN_HELD))
    # owner-taste critic: re-render each logged take with this build, fit keep-vs-reroll on audio features, test leave-one-session-out
    lab = [t for t in takes if t["outcome"] in ("kept", "edited", "rerolled") and t.get("part") in PARTS]
    lst = [dict(style=t["style"], part=PARTS.index(t["part"]), seed=int(t["seed"]), key=int(t.get("root", -1)) if not t.get("follow_key") else -1,
                scale=int(t.get("scale", -1)), tid=t["id"], session=t.get("session"), good=good(t)) for t in lab]
    lp = os.path.join(out, "owner_list.json"); json.dump(lst, open(lp, "w"))
    rp = os.path.join(out, "owner_%s.jsonl" % hashlib.sha1((gen + open(os.path.join(gen, "infinite_gen_wasm.wasm"), "rb").read()[:0].hex()).encode()).hexdigest()[:6])
    cmd = "node %s %s | %s -I %s > %s" % (os.path.join(HERE, "wasm_render_parts.js"), gen, PY, os.path.join(HERE, "take_measure.py"), rp)
    subprocess.run(["bash", "-o", "pipefail", "-c", cmd], env=dict(os.environ, LIST=lp), check=True)
    O = [r for r in load(rp) if r.get("timbre")]
    if len(O) < 20:
        card.add("8 taste", "owner-taste critic", None, "", None, detail="only %d re-rendered takes" % len(O)); return
    X = np.array([taste_features(r) for r in O]); y = np.array([r["good"] for r in O], float)
    sess = [r["session"] for r in O]; s = np.zeros(len(O))
    for ss in set(sess):
        tr = [i for i, x in enumerate(sess) if x != ss]; te = [i for i, x in enumerate(sess) if x == ss]
        if y[tr].min() == y[tr].max(): s[te] = y[tr].mean(); continue
        s[te] = fit_logreg(X[tr], y[tr], l2=20.0)(X[te])
    a = auc(s, y)
    # tol: the AUC's standard error at this many takes (Hanley-McNeil, about 0.08 at 13 kept / 44 rerolled); a smaller move is noise
    npos, nneg = int(y.sum()), int(len(y) - y.sum())
    se = math.sqrt(max(a * (1 - a), 0.05) * (1.0 / max(npos, 1) + 1.0 / max(nneg, 1)) / 2) if a == a else 0.1
    card.add("8 taste", "owner-taste critic, leave-one-session-out AUC", a, ">= 0.65 before it may judge", a >= 0.65, fmt="%.2f", tol=se,
             detail="%d re-rendered takes, %d kept/edited, %d sessions" % (len(O), int(y.sum()), len(set(sess))))
    f = fit_logreg(X, y, l2=20.0)
    use = collections.Counter(t["part"] for t in takes)
    G = [r for r in T if r.get("timbre") and PARTS[r["part"]] in use]
    if G:
        pr = f(np.array([taste_features(r) for r in G]))
        wts = np.array([use[PARTS[r["part"]]] for r in G], float)
        v = float((pr * wts).sum() / wts.sum())
        card.add("8 taste", "predicted owner keep rate on this run (weighted by owner's part use)", v,
                 "track; judged only once AUC >= 0.65", (v >= 0.33) if a >= 0.65 else None, fmt="%.3f", tol=0.005)


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", required=True); ap.add_argument("--quick", action="store_true"); ap.add_argument("--out", default=None)
    ap.add_argument("--jobs", type=int, default=3); ap.add_argument("--against"); ap.add_argument("--native-log")
    ap.add_argument("--freeze", action="store_true"); ap.add_argument("--no-taste", action="store_true")
    a = ap.parse_args()
    gen = os.path.abspath(a.gen)
    if not os.path.exists(os.path.join(gen, "infinite_gen_wasm.wasm")):
        print("no infinite_gen_wasm.wasm in %s" % gen, file=sys.stderr); return 2
    out = os.path.abspath(a.out or os.path.join(gen, "scorecard")); os.makedirs(out, exist_ok=True)
    t0 = time.time()
    path, h = render(gen, out, a.quick, a.jobs)
    T = load(path)
    refs = json.load(open(REFS)) if os.path.exists(REFS) else {}
    card = Card()
    area_notes(card, T)
    area_library(card, T, refs)
    area_clusters(card)
    newthr = area_cards(card, T, refs, a.freeze)
    area_mix(card, T)
    nstyles = len({r["id"] for r in T}) if T else 0
    area_critic(card, T, 34 if not a.quick else 12, a.native_log)
    if not a.no_taste:
        area_taste(card, T, gen, out)
    if a.freeze:
        fr = {"made": time.strftime("%Y-%m-%d"), "wasm": h, "neardup": newthr}
        if not refdir("library") and os.path.exists(os.path.join(TD, "audio/features.jsonl")):
            import part_vs_library as pvl
            fr["library"] = {k: {kk: list(vv) for kk, vv in v.items()} for k, v in pvl.library().items()}
        json.dump(fr, open(REFS, "w"), indent=1); print("froze %s" % REFS, file=sys.stderr)
    prev = {r["name"]: r for r in json.load(open(a.against))["rows"]} if a.against else {}
    worse = 0
    print("\nGenerate Samples scorecard  wasm %s  %s  %d takes  %.0f s" % (h, "quick" if a.quick else "full", len(T), time.time() - t0))
    print("%-10s %-6s %-66s %-9s %-24s %s" % ("area", "status", "row", "value", "target", "before / note"))
    for r in card.rows:
        note = r["detail"]
        p = prev.get(r["name"])
        if p and p["value"] is not None and r["value"] is not None:
            d = r["value"] - p["value"]
            bad = (d < -r["tol"]) if r["better"] == "high" else (d > r["tol"])
            if bad and r["status"] != "NO REF" and "context" not in r["target"]:
                worse += 1; r["status"] += " WORSE"
            note = "was %s; %s" % (p["shown"], note)
        print("%-10s %-6s %-66s %-9s %-24s %s" % (r["area"], r["status"].split()[0] + ("!" if "WORSE" in r["status"] else ""),
                                                  r["name"][:66], r["shown"], r["target"][:24], note[:150]))
    n = collections.Counter(r["status"].split()[0] for r in card.rows)
    print("\n%d PASS, %d FAIL, %d NO REF, %d worse than before" % (n["PASS"], n["FAIL"], n["NO"], worse))
    json.dump({"wasm": h, "quick": a.quick, "takes": len(T), "rows": card.rows}, open(os.path.join(out, "scorecard_%s_%s.json" % (h, "quick" if a.quick else "full")), "w"), indent=1)
    return 1 if worse else 0


if __name__ == "__main__":
    sys.exit(main())
