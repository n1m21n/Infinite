#!/usr/bin/env python3
"""Sweep generator and lossless renderer for Infinite's own synth engines (EXECUTION.md Step 2 + G1).

    own_sweeps.py run    --node Oscillator [--count 2000] [--shards 4] [--budget 15] [--bank-size 20]
    own_sweeps.py check  --node Oscillator [--n 20]            re-render stored recipes, compare to the stored rows
    own_sweeps.py repeat --node Oscillator                     two renders of one point agree (r >= 0.97)
    own_sweeps.py show   --node Oscillator [--n 3]             print the first recipes as patches (for --validate)

Output: ~/Infinite/training-data/audio/own_<engine>.jsonl, row schema 3 (SOUND-MODEL.md 4b). One row per note window. A
preset's rows share preset_id = sha1 of its recipe; the first row also carries `recipe` (every parameter, renders are
deterministic, so the recipe is the lossless record), `knobs` (0-1 position of every swept parameter) and `velocity`.
Windows 0-2 are the compat notes sound_data / m1 / p3 read (C3 v100, C4 v100, C3f v127); the rest (C1, C5, C6, C3 pp, C3 mf)
are stored with the same fields minus the analyse() set.

Designs (src = own:<engine>:bNNNN/<design>_<n>, the bank bNNNN is the group, so a variant never leaks from its base):
  base  every swept parameter drawn uniformly in its range (log for frequencies, log1p for times)       30%
  one   one parameter of a base of the same bank moved by more than 10% of its range                    60%
  pair  two or three parameters of a base moved together                                                10%
Stop: ~/STOP_OWN exists, --budget minutes pass, or --count presets exist. Re-running resumes (ids are content hashes)."""
import argparse, base64, hashlib, json, math, multiprocessing as mp, os, random, re, subprocess, sys, tempfile, time, zlib
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_pass as rp  # noqa: E402
import detail as det  # noqa: E402
import audio_features as af  # noqa: E402

SR, LEAD, WIN, HOLD = rp.SR, rp.LEAD, rp.WIN, rp.HOLD
# midi:velocity:label. The first three are rp.NOTES (the compat windows); the rest widen the pitch and velocity range (G1).
STIM = [(48, 100, "C3"), (60, 100, "C4"), (48, 127, "C3f"), (24, 100, "C1"), (72, 100, "C5"), (84, 100, "C6"), (48, 30, "C3pp"), (48, 64, "C3mf")]
if os.environ.get("INF_STIM"):  # quick rounds: only these windows (`run --notes C3`); the rest are not read by sound_data / p3
    STIM = [t for t in STIM if t[2] in os.environ["INF_STIM"].split(",")]
COMPAT = 3
SCHEMA = 3
OUT_DIR = os.path.expanduser("~/Infinite/training-data/audio")
CACHE_DIR = os.path.expanduser("~/Infinite/training-data/own")
STOP = os.path.expanduser("~/STOP_OWN")
LICENSE = "own engine render, local"
MIX = (("base", 0.30), ("one", 0.60), ("pair", 0.10))
FIXED_KEYS = {"volume": 0.8, "freq": 220, "frequency": 220, "glide": 0, "pitchBend": 0, "pan": 0, "voices": 1}
# per engine, parameters that do nothing in this patch (an input pin we do not connect) or that would move the pitch
FIXED_ENGINE = {"Oscillator": {"fmDepth": 0, "fmMode": 0}, "Analog": {}}
TIME_HINT = re.compile(r"(attack|decay|release|time)$", re.I)
FREQ_HINT = re.compile(r"(cutoff|freq)", re.I)


def binary():
    """The release build when it exists: a debug build renders 10x slower, and its --batch saves less than that costs."""
    b = rp.infinite_bin()
    if b:
        return b
    root = os.path.dirname(os.path.dirname(HERE))
    for b in (os.environ.get("INFINITE_BIN", ""), os.path.join(root, "build-dev/Infinite.app/Contents/MacOS/Infinite")):  # --batch needs a build from 2026-10-28
        if b and os.access(b, os.X_OK):
            return b
    b = rp.infinite_bin()
    if not b:
        raise SystemExit("no Infinite binary (set INFINITE_BIN)")
    return b


# ------------------------------------------------------------------ parameter table

def describe(node):
    """Rows of `Infinite --describe <node>`: cached, since the table only changes with the app."""
    os.makedirs(CACHE_DIR, exist_ok=True)
    f = os.path.join(CACHE_DIR, "describe_%s.json" % node)
    if not os.path.exists(f):
        subprocess.run([binary(), "--describe", node, "--json", f], capture_output=True, timeout=120)
    t = json.load(open(f))["types"][0]
    return t


def param_table(node):
    """[{key, tag, default, lo, hi, kind}] for every swept parameter; the rest go into `fixed` (key -> value).
    kind: lin | log (frequency) | time (log1p) | enum (integer choices) | bool."""
    t = describe(node)
    sw, fixed = [], {}
    over = dict(FIXED_ENGINE.get(node, {}))
    for r in t["rows"]:
        k, tag, df, lo, hi = r["key"], r["tag"], r["default"], r["min"], r["max"]
        if k in FIXED_KEYS:
            fixed[k] = FIXED_KEYS[k]
        elif k in over:
            fixed[k] = over[k]
        elif lo is None or hi is None or hi == lo:
            fixed[k] = df  # hidden or constant (octave, semi ...): the default
        else:
            kind = "bool" if tag == "b" else "enum" if tag == "i" else \
                "log" if FREQ_HINT.search(k) and lo > 0 else "time" if TIME_HINT.search(k) and lo >= 0 and hi >= 100 else "lin"
            sw.append({"key": k, "tag": tag, "default": df, "lo": float(lo), "hi": float(hi), "kind": kind})
    return t, sw, fixed


def to_value(p, u):
    """0-1 position -> parameter value."""
    lo, hi = p["lo"], p["hi"]
    if p["kind"] in ("enum", "bool"):
        return int(min(hi, lo + math.floor(u * (hi - lo + 1))))
    if p["kind"] == "log":
        return float(lo * (hi / lo) ** u)
    if p["kind"] == "time":
        return float(math.expm1(u * math.log1p(hi - lo)) + lo)
    return float(lo + u * (hi - lo))


def to_u(p, v):
    lo, hi = p["lo"], p["hi"]
    if p["kind"] in ("enum", "bool"):
        return (v - lo + 0.5) / (hi - lo + 1)
    if p["kind"] == "log":
        return math.log(max(v, lo) / lo) / math.log(hi / lo)
    if p["kind"] == "time":
        return math.log1p(max(v - lo, 0)) / math.log1p(hi - lo)
    return (v - lo) / (hi - lo)


def params_hash(node, sw):
    return hashlib.sha1((node + "|" + ",".join("%s:%s:%g:%g" % (p["key"], p["kind"], p["lo"], p["hi"]) for p in sw)).encode()).hexdigest()[:12]


# ------------------------------------------------------------------ designs

def design_bank(node, sw, bank, size):
    """The recipes of one bank, a pure function of (node, bank): [(design, u-vector)]. Order inside a bank is fixed, so a
    bank is complete or absent, and a later run reproduces exactly the same points."""
    rng = random.Random("%s/%d" % (node, bank))
    n_base = max(1, round(size * MIX[0][1])); n_pair = max(1, round(size * MIX[2][1])); n_one = size - n_base - n_pair
    bases = [[rng.random() for _ in sw] for _ in range(n_base)]
    out = [("base", u) for u in bases]
    for _ in range(n_one):
        u = list(rng.choice(bases)); i = rng.randrange(len(sw))
        for _ in range(50):
            x = rng.random()
            if abs(x - u[i]) > 0.10:
                break
        u[i] = x; out.append(("one", u))
    for _ in range(n_pair):
        u = list(rng.choice(bases))
        for i in rng.sample(range(len(sw)), rng.choice((2, 3))):
            u[i] = rng.random()
        out.append(("pair", u))
    return out


def recipe(node, sw, fixed, u):
    """Every parameter of the patch, swept ones from the 0-1 vector, rounded so the .inf text and the record agree."""
    rec = dict(fixed)
    for p, x in zip(sw, u):
        v = to_value(p, x)
        rec[p["key"]] = v if p["tag"] == "i" or p["tag"] == "b" else float("%.6g" % v)
    return dict(sorted(rec.items()))


def rid(node, rec):
    return hashlib.sha1((node + json.dumps(rec, sort_keys=True)).encode()).hexdigest()[:16]


def tag_of(table):
    return {r["key"]: r["tag"] for r in table["rows"]}


def patch_text(node, tags, rec):
    L = ["infinite-patch 1", "node 1 Notes Keyboard", "  id keys", "end", "node 2 Synths " + node, "  id syn"]
    for k, v in rec.items():
        tg = tags.get(k, "f")
        L.append("  %s %s %s" % (tg, k, int(v) if tg in ("i", "b") else repr(float(v))))
    L += ["end", "node 3 Utility Audio Out", "  id out", "end", "aud out audio syn", "note syn 0 keys"]
    return "\n".join(L) + "\n"


# ------------------------------------------------------------------ rendering and measuring

def write_notes(d):
    types = {"p%d" % i: {"node": "syn", "pitch": m, "velocity_from": "v", "length_from": "len"} for i, (m, _, _) in enumerate(STIM)}
    evs = [{"t": LEAD + i * WIN, "type": "p%d" % i, "v": round(v / 127.0, 5), "len": HOLD} for i, (_, v, _) in enumerate(STIM)]
    ep, mp_ = os.path.join(d, "events.json"), os.path.join(d, "map.json")
    json.dump(evs, open(ep, "w")); json.dump(types, open(mp_, "w"))
    return ep, mp_


_batch = {}


def has_batch(binp):
    if binp not in _batch:
        r = subprocess.run([binp, "--audio-summary", "x", "y", "--batch", "z"], capture_output=True, text=True, timeout=60)
        _batch[binp] = "unknown option" not in r.stdout
    return _batch[binp]


def render_batch(binp, node, tags, recs, tmp, timeout=300):
    """One Infinite process renders every recipe (--batch). Returns a wav path or None per recipe."""
    ep, mp_ = write_notes(tmp)
    jobs = []
    for k, rec in enumerate(recs):
        inf, js, wav = (os.path.join(tmp, "%s%d.%s" % (n, k, e)) for n, e in (("p", "inf"), ("s", "json"), ("o", "wav")))
        open(inf, "w").write(patch_text(node, tags, rec))
        jobs.append((inf, js, wav))
    res = [None] * len(jobs); todo = list(range(len(jobs)))
    dur = LEAD + WIN * len(STIM)
    if not has_batch(binp):  # one process per patch: still faster than a debug build's --batch
        for k, (inf, js, wav) in enumerate(jobs):
            try:
                subprocess.run([binp, "--audio-summary", inf, js, "--duration", "%g" % dur, "--sample-rate", str(SR), "--wav", wav,
                                "--lenient", "--notes", ep, "--note-map", mp_], capture_output=True, text=True, timeout=120)
            except subprocess.TimeoutExpired:
                continue
            if os.path.exists(wav) and os.path.getsize(wav) > 1000:
                res[k] = wav
        return res
    while todo:
        lst = os.path.join(tmp, "batch.txt")
        open(lst, "w").write("".join("%s\t%s\t%s\n" % jobs[k] for k in todo[1:]))
        inf, js, wav = jobs[todo[0]]
        dur = LEAD + WIN * len(STIM)
        try:
            subprocess.run([binp, "--audio-summary", inf, js, "--duration", "%g" % dur, "--sample-rate", str(SR), "--wav", wav,
                            "--lenient", "--notes", ep, "--note-map", mp_, "--batch", lst, "--timeout", "%d" % timeout],
                           capture_output=True, text=True, timeout=timeout + 30)
        except subprocess.TimeoutExpired:
            pass
        left = []
        for k in todo:
            w = jobs[k][2]
            if left or not os.path.exists(w) or os.path.getsize(w) < 1000:
                left.append(k); continue
            res[k] = w
        todo = left[1:]  # the job the process died on stays None; the rest rerun
    return res


def window_row(seg2, rate, midi, vel, lab, tmp, full):
    """One note window -> its fields. full: also the analyse() set the compat readers need."""
    peak = float(np.abs(seg2).max())
    if peak < 1e-5 or not np.isfinite(seg2).all():
        return {"silent": peak < 1e-5, "nonfinite": bool(not np.isfinite(seg2).all()), "peak": round(peak, 6) if np.isfinite(peak) else None}
    row = {}
    if full:
        sp = os.path.join(tmp, "seg.wav")
        import wave
        w = wave.open(sp, "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(int(rate))
        w.writeframes((np.clip(seg2.T, -1, 1) * 32767).astype("<i2").tobytes()); w.close()
        try:
            row.update(af.analyse(sp, tmp, WIN + 1))
        except Exception as e:  # a window the analyser cannot read keeps the detail below
            row["error"] = str(e)[:120]
        for k in ("src", "pack", "part", "bpm", "key", "loop", "license"):
            row.pop(k, None)
        row["mel"] = rp.logmel(seg2, rate)
    else:
        x = seg2.mean(0)
        row["peak"] = round(peak, 4); row["rms"] = float("%.4g" % np.sqrt((x ** 2).mean()))
    d = det.measure(seg2.mean(0), rate, midi, HOLD)
    if d:
        row["detail"] = {k: v for k, v in d.items() if k not in ("partials", "noise", "tail", "mel128")}
        for k in ("partials", "noise", "tail", "mel128"):
            if k in d:
                row[k] = d[k]
    return row


def velocity_response(wins):
    """C3 at velocity 30 / 64 / 100 / 127: level (dB) and brightness (Hz, centroid of the held part) per velocity, and the slope
    of level against velocity (dB per 10 steps). The 'does the key hit change the sound' answer of G1."""
    order = {"C3pp": 30, "C3mf": 64, "C3": 100, "C3f": 127}
    rows = []
    for lab, vel in order.items():
        s = wins.get(lab)
        if s is None:
            return None
        x = s.mean(0)[: int(HOLD * SR)]
        db = 20 * math.log10(max(float(np.sqrt((x ** 2).mean())), 1e-9))
        sp = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
        f = np.fft.rfftfreq(len(x), 1 / SR)
        rows.append((vel, db, float((f * sp).sum() / max(sp.sum(), 1e-30))))
    v = np.array([r[0] for r in rows], float); db = np.array([r[1] for r in rows])
    slope = float(np.polyfit(v, db, 1)[0] * 10)
    return {"vel": [int(r[0]) for r in rows], "rms_db": [round(r[1], 2) for r in rows], "centroid": [round(r[2], 1) for r in rows],
            "slope_db_per_10": round(slope, 2)}


def rows_for(node, sw, fixed, tags, wav, tmp, meta):
    r = af.read_pcm(wav, 60)
    if r is None:
        return None
    x, rate = r
    if x.shape[0] == 1:
        x = np.vstack([x[0], x[0]])
    wins = {}
    for i, (m, v, lab) in enumerate(STIM):
        a = int(round((LEAD + i * WIN) * rate))
        seg = x[:2, a:a + int(round(WIN * rate))]
        if seg.shape[1] >= 2048:
            wins[lab] = seg
    rows = []
    for i, (m, v, lab) in enumerate(STIM):
        base = {"schema": SCHEMA, "route": 3, "src": meta["src"], "preset_id": meta["id"], "label": meta["label"], "plugin": node,
                "plugin_id": "own:" + node, "note": lab, "midi": m, "velocity": v, "tags": {"design": meta["design"], "bank": meta["bank"]},
                "license": LICENSE}
        if i == 0:
            base.update(recipe=meta["rec"], knobs=meta["knobs"], params_hash=meta["ph"], knob_keys=[p["key"] for p in sw])
            vr = velocity_response(wins)
            if vr:
                base["velocity_response"] = vr
        if lab in wins:
            base.update(window_row(wins[lab], rate, m, v, lab, tmp, i < COMPAT))
        else:
            base["silent"] = True
        rows.append(base)
    return rows


def shard_job(args):
    node, items = args
    binp = binary(); table, sw, fixed = param_table(node); tags = tag_of(table); ph = params_hash(node, sw)
    out = []
    with tempfile.TemporaryDirectory(prefix="own_") as tmp:
        wavs = render_batch(binp, node, tags, [it["rec"] for it in items], tmp)
        for it, wav in zip(items, wavs):
            if wav is None:
                out.append((it["id"], None)); continue
            meta = dict(it, ph=ph)
            try:
                out.append((it["id"], rows_for(node, sw, fixed, tags, wav, tmp, meta)))
            except Exception as e:  # one bad render must not lose its batch
                out.append((it["id"], None))
            os.remove(wav)
    return out


# ------------------------------------------------------------------ commands

def out_path(node):
    return os.path.join(OUT_DIR, "own_%s.jsonl" % node.lower())


def existing_ids(path):
    ids = set()
    if os.path.exists(path):
        for ln in open(path):
            if '"knobs"' in ln:
                ids.add(json.loads(ln)["preset_id"])
    return ids


def items_for_bank(node, sw, fixed, bank, size):
    its = []
    for n, (design, u) in enumerate(design_bank(node, sw, bank, size)):
        rec = recipe(node, sw, fixed, u); i = rid(node, rec)
        # knobs: the 0-1 position of the value as stored (rounding and enum snapping included)
        kn = [round(to_u(p, rec[p["key"]]), 4) for p in sw]
        its.append({"id": i, "rec": rec, "knobs": kn, "design": design, "bank": "b%04d" % bank,
                    "src": "own:%s:b%04d/%s_%02d" % (node.lower(), bank, design, n), "label": "%s %s %02d" % (node, design, n)})
    return its


def cmd_run(a):
    node = a.node; table, sw, fixed = param_table(node); path = out_path(node)
    os.makedirs(OUT_DIR, exist_ok=True)
    have = existing_ids(path); t0 = time.time(); added = 0; failed = 0
    print("%s: %d swept, %d fixed, %d presets stored" % (node, len(sw), len(fixed), len(have)), flush=True)
    work, bank = [], 0
    if a.queue:
        q = json.load(open(os.path.expanduser(a.queue)))
        for n, it in enumerate(q["items"]):
            rec = recipe(node, sw, fixed, it["u"]); i = rid(node, rec)
            if i in have or any(w["id"] == i for w in work):
                continue
            b = "%sb%04d" % (q["round"], n // a.bank_size)
            work.append({"id": i, "rec": rec, "knobs": [round(to_u(p, rec[p["key"]]), 4) for p in sw], "design": it["design"], "bank": b,
                         "src": "own:%s:%s/%s_%03d" % (node.lower(), b, it["design"], n), "label": "%s %s %s" % (node, q["round"], it["design"])})
        a.count = len(have) + len(work)
    while len(have) + len(work) < a.count:
        seen = {w["id"] for w in work}
        its = [it for it in items_for_bank(node, sw, fixed, bank, a.bank_size) if it["id"] not in have and it["id"] not in seen]
        work += its; bank += 1
        if bank > 100000:
            break
    work = work[: a.count - len(have)]
    chunks = [work[i:i + a.chunk] for i in range(0, len(work), a.chunk)]
    ctx = mp.get_context("spawn")
    with ctx.Pool(a.shards) as pool, open(path, "a") as f:
        pending = iter([(node, c) for c in chunks]); live = []
        def stop():
            return os.path.exists(STOP) or (a.budget and time.time() - t0 > a.budget * 60)
        for _ in range(a.shards):
            c = next(pending, None)
            if c: live.append(pool.apply_async(shard_job, (c,)))
        while live:
            time.sleep(0.5)
            for r in [r for r in live if r.ready()]:
                live.remove(r)
                for i, rows in r.get():
                    if rows:
                        for row in rows:
                            f.write(json.dumps(row, separators=(",", ":")) + "\n")
                        added += 1
                    else:
                        failed += 1
                f.flush()
                print("  %d new, %d failed, %.0f s" % (added, failed, time.time() - t0), flush=True)
                c = None if stop() else next(pending, None)
                if c: live.append(pool.apply_async(shard_job, (c,)))
    print("done: %d new presets (%d failed) in %.0f s; file %s" % (added, failed, time.time() - t0, path), flush=True)


def rows_of(path):
    by = {}
    for ln in open(path):
        r = json.loads(ln)
        by.setdefault(r["preset_id"], []).append(r)
    return by


def corr(a, b):
    a, b = np.asarray(a, float).ravel(), np.asarray(b, float).ravel()
    if a.std() == 0 or b.std() == 0:
        return 1.0 if np.array_equal(a, b) else 0.0
    return float(np.corrcoef(a, b)[0, 1])


def rerender(node, recs):
    table, sw, fixed = param_table(node); tags = tag_of(table); out = []
    with tempfile.TemporaryDirectory(prefix="own_chk_") as tmp:
        wavs = render_batch(binary(), node, tags, recs, tmp)
        for k, (rec, wav) in enumerate(zip(recs, wavs)):
            meta = {"src": "check", "id": "check%d" % k, "label": "check", "design": "check", "bank": "check", "rec": rec, "knobs": [], "ph": ""}
            out.append(rows_for(node, sw, fixed, tags, wav, tmp, meta) if wav else None)
    return out


def mel_of(r):
    return np.frombuffer(base64.b64decode(r["mel"]), np.uint8).astype(float)


def cmd_check(a):
    by = rows_of(out_path(a.node)); ids = sorted(by)[:: max(1, len(by) // a.n)][: a.n]
    recs = [by[i][0]["recipe"] for i in ids]
    new = rerender(a.node, recs); worst = 1.0; exact = 0
    for i, rows in zip(ids, new):
        if not rows:
            print("FAIL: %s did not render" % i); worst = 0.0; continue
        for old, nw in zip(by[i], rows):
            if old.get("mel") and nw.get("mel"):
                r = corr(mel_of(old), mel_of(nw)); worst = min(worst, r); exact += old["mel"] == nw["mel"]
    print("check %s: %d recipes, worst mel r = %.5f, %d windows bit-identical" % (a.node, len(ids), worst, exact))
    return 0 if worst >= 0.999 else 1


def cmd_repeat(a):
    table, sw, fixed = param_table(a.node)
    u = design_bank(a.node, sw, 0, 20)[0][1]; rec = recipe(a.node, sw, fixed, u)
    r1, r2 = rerender(a.node, [rec]) [0], rerender(a.node, [rec])[0]
    rs = [corr(mel_of(x), mel_of(y)) for x, y in zip(r1[:COMPAT], r2[:COMPAT])]
    print("repeat %s: mel r per window %s" % (a.node, ["%.5f" % r for r in rs]))
    return 0 if min(rs) >= 0.97 else 1


def cmd_show(a):
    table, sw, fixed = param_table(a.node); tags = tag_of(table)
    for it in items_for_bank(a.node, sw, fixed, 0, 20)[: a.n]:
        print("# " + it["src"]); print(patch_text(a.node, tags, it["rec"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["run", "check", "repeat", "show"])
    ap.add_argument("--node", required=True)
    ap.add_argument("--count", type=int, default=2000, help="presets wanted in the file after this run")
    ap.add_argument("--shards", type=int, default=4)
    ap.add_argument("--chunk", type=int, default=20, help="presets per Infinite process")
    ap.add_argument("--bank-size", type=int, default=20)
    ap.add_argument("--budget", type=float, default=0, help="minutes; 0 = none")
    ap.add_argument("--n", type=int, default=20)
    ap.add_argument("--notes", default="", help="comma list of windows to render (e.g. C3); default all 8. Same audio for the windows kept")
    ap.add_argument("--queue", default="", help="a propose.py queue file: render exactly these points (Step 4)")
    a = ap.parse_args()
    if a.notes:
        os.environ["INF_STIM"] = a.notes; STIM[:] = [t for t in STIM if t[2] in a.notes.split(",")]
    sys.exit({"run": cmd_run, "check": cmd_check, "repeat": cmd_repeat, "show": cmd_show}[a.cmd](a) or 0)


if __name__ == "__main__":
    main()
