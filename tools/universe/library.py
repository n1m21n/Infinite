#!/usr/bin/env python3
"""Sample library (Splice-style) from the scene atlas.
    library.py atlas_scene.jsonl [more.jsonl] --n 60 --out ~/Infinite/patch-universe-library/samples
For each picked genome: compile -> lay out -> --validate, then render the sample the patch makes:
    <out>/<kind>/<id>/patch.inf  sample.wav (audio)  poster.png + preview.mp4 (visual; mp4 carries the sound for AV)  meta.json
and one index.jsonl row per entry (genome, node families, tags, loudness, play dial) so the folder is searchable.
Selection is greedy farthest-point on node-name bags with a per-node cap, so the set is varied, not the 60 best of one shape."""
import argparse, json, os, random, subprocess, sys, shutil, tempfile
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u
import scene
import switches

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def select(rows, n, cap=0.12):
    """Farthest-point on node bags; no node type appears in more than `cap` of the picks (code families exempt: they are the point)."""
    rows = [r for r in rows if r.get("names")]
    bags = [frozenset(r["names"]) for r in rows]
    picked, count = [], {}
    limit = max(3, int(cap * n))
    dist = [1.0] * len(rows)
    for _ in range(min(n, len(rows))):
        best, bi = -1, None
        for i, r in enumerate(rows):
            if i in picked or any(count.get(x, 0) >= limit for x in bags[i] if ":" not in x and x != "Audio Out"):
                continue
            if dist[i] > best:
                best, bi = dist[i], i
        if bi is None:
            break
        picked.append(bi)
        for x in bags[bi]:
            count[x] = count.get(x, 0) + 1
        for i in range(len(rows)):
            j = len(bags[i] | bags[bi]) or 1
            dist[i] = min(dist[i], 1 - len(bags[i] & bags[bi]) / j)
    return [rows[i] for i in picked]


def tags_of(r):
    t = set()
    for n in r.get("names", []):
        if ":" in n:
            b, f = n.split(":")
            t.update([f, b.lower().replace(" ", "-"), "code"])
    kind = r["code"].split(" ")[0]
    t.add({"A": "audio", "V": "visual", "AV": "audiovisual"}[kind])
    if len(r.get("names", [])) >= 25:
        t.add("dense")
    elif len(r.get("names", [])) <= 8:
        t.add("small")
    a = r.get("a") or {}
    if a.get("integrated_lufs") is not None:
        t.add("loud" if a["integrated_lufs"] > -16 else "quiet")
    if a.get("onset_count_estimate", 0) >= 12:
        t.add("rhythmic")
    return sorted(t)


def interest(png):
    """Cheap visual-interest score of a poster: luminance spread, edge density and colour variety, 0..1. Bland posters (flat fields, one blob) score low."""
    from PIL import Image, ImageFilter, ImageStat
    im = Image.open(png).convert("RGB").resize((240, 135))
    lum = im.convert("L")
    spread = min(1.0, ImageStat.Stat(lum).stddev[0] / 60.0)
    edges = min(1.0, ImageStat.Stat(lum.filter(ImageFilter.FIND_EDGES)).mean[0] / 18.0)
    colours = min(1.0, len(set(im.quantize(16).getdata())) / 12.0)
    return round(0.4 * spread + 0.4 * edges + 0.2 * colours, 3)


MIN_INTEREST = 0.2


def run(cmd, timeout):
    try:
        cp = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return cp.returncode == 0
    except subprocess.TimeoutExpired:
        return False


FAMILY_P = 0.4          # share of samples that also get switch siblings
MIN_AUDIO_DIFF = 0.01   # rms of (alt - base) must clear this, or the switch did nothing audible
MIN_FRAME_DIFF = 1.5    # mean abs pixel difference of the posters


def _wav(path):
    import wave
    import numpy as np
    w = wave.open(path)
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768


def _differs(kind, base_dir, alt_dir):
    import numpy as np
    if kind in ("A", "AV"):
        a, b = _wav(os.path.join(base_dir, "sample.wav")), _wav(os.path.join(alt_dir, "sample.wav"))
        m = min(len(a), len(b))
        if m and float(np.sqrt(np.mean((a[:m] - b[:m]) ** 2))) >= MIN_AUDIO_DIFF:
            return True
    if kind in ("V", "AV"):
        from PIL import Image
        a = np.asarray(Image.open(os.path.join(base_dir, "poster.png")).convert("RGB"), float)
        b = np.asarray(Image.open(os.path.join(alt_dir, "poster.png")).convert("RGB"), float)
        if a.shape == b.shape and float(np.abs(a - b).mean()) >= MIN_FRAME_DIFF:
            return True
    return False


def siblings(base_meta, d, out, facts, P, binary, secs, seed):
    """Switch family: re-render the sample with one slot turned over to each alternative; keep the ones that really differ."""
    rr = random.Random(seed)
    code = base_meta["genome"]
    kind = base_meta["kind"]
    if kind == "A":
        sw = switches.propose(code.partition(" ")[2], facts, P, rr)
        sw = "A " + sw if sw else None
    elif kind == "V":
        sw = switches.propose_inf(code, facts, P, rr)
    else:
        sw = None  # AV: the audio and picture halves are switched in their own samples
    if not sw:
        return []
    kept = []
    for j, alt in enumerate(switches.expand(sw)[1:], 1):
        sub = "%s%s" % (os.path.basename(d), "bcdef"[j - 1])
        row, _ = publish((dict(code=alt, names=base_meta["nodes"], a=None, **{"class": base_meta["class"]}), sub, out, facts, P, binary, secs))
        if not row:
            continue
        sd = os.path.join(out, row["id"].split("/")[0], sub)
        if not _differs(kind, d, sd):
            shutil.rmtree(sd); continue
        row["family"] = base_meta["id"]; row["switch"] = sw
        json.dump(row, open(os.path.join(sd, "meta.json"), "w"), indent=1)
        kept.append(row)
    return kept


def publish(args):
    r, i, out, facts, P, binary, secs = args
    kind = r["code"].split(" ")[0]
    folder = {"A": "audio", "V": "visual", "AV": "audiovisual"}[kind]
    eid = i if isinstance(i, str) else "s%04d" % i
    d = os.path.join(out, folder, eid)
    os.makedirs(d, exist_ok=True)
    inf = os.path.join(d, "patch.inf")
    open(inf, "w").write(scene.compile_scene(r["code"], facts, P))
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "patch-layout.py"), inf], capture_output=True)
    body = open(inf).read()
    head, _, rest = body.partition("\n")
    open(inf, "w").write("%s\n# genome: %s\n%s" % (head, r["code"], rest))
    if subprocess.run([binary, "--validate", inf], capture_output=True, text=True).returncode:
        shutil.rmtree(d); return None, []
    made = {}
    if kind in ("A", "AV"):
        made["wav"] = run([binary, "--audio-summary", inf, os.path.join(d, "audio.json"), "--duration", str(secs), "--wav", os.path.join(d, "sample.wav")], 120)
    if kind in ("V", "AV"):
        made["png"] = run([binary, "--frame", inf, "0", d + "/", "--size", "960x540"], 120)
        for f in os.listdir(d):  # the engine names frames itself
            if f.endswith(".png") and f != "poster.png":
                os.replace(os.path.join(d, f), os.path.join(d, "poster.png"))
        made["mp4"] = run([binary, "--render", inf, os.path.join(d, "preview.mp4"), "--duration", "3", "--fps", "24"], 240)
    if os.path.exists(os.path.join(d, "audio.json")):
        os.remove(os.path.join(d, "audio.json"))
    want = {"wav": ("A", "AV"), "png": ("V", "AV"), "mp4": ("V", "AV")}
    if not all(made.get(k) for k, ks in want.items() if kind in ks):
        shutil.rmtree(d); return None, []
    score = None
    if kind in ("V", "AV"):
        score = interest(os.path.join(d, "poster.png"))
        if score < MIN_INTEREST:
            shutil.rmtree(d); return None, []
    meta = {"id": "%s/%s" % (folder, eid), "kind": kind, "genome": r["code"], "nodes": r["names"], "tags": tags_of(r),
            "class": r["class"], "interest": score, "audio": {k: v for k, v in (r.get("a") or {}).items() if k in ("integrated_lufs", "sample_peak_dbfs", "onset_count_estimate", "silence_ratio")},
            "files": sorted(f for f in os.listdir(d))}
    json.dump(meta, open(os.path.join(d, "meta.json"), "w"), indent=1)
    sibs = []
    if not isinstance(i, str) and random.Random(eid).random() < FAMILY_P:
        sibs = siblings(meta, d, out, facts, P, binary, secs, i)
        meta["siblings"] = [m["id"] for m in sibs]
        json.dump(meta, open(os.path.join(d, "meta.json"), "w"), indent=1)
    return meta, sibs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("atlas", nargs="+")
    ap.add_argument("--n", type=int, default=60)
    ap.add_argument("--out", default=os.path.expanduser("~/Infinite/patch-universe-library/samples"))
    ap.add_argument("--bin", default=u.DEFAULT_BIN)
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--secs", type=int, default=8)
    a = ap.parse_args()
    facts = u.load_facts(); P = scene.pools_v2(facts)
    rows, seen = [], set()
    for p in a.atlas:
        for l in open(p):
            r = json.loads(l)
            if r.get("class") == "ok" and r.get("names") and r["code"] not in seen:
                seen.add(r["code"]); rows.append(r)
    picked = select(rows, int(a.n * 1.4))  # over-pick: the interest gate drops bland posters
    print("candidates %d  picked %d" % (len(rows), len(picked)))
    os.makedirs(a.out, exist_ok=True)
    start = sum(len(os.listdir(os.path.join(a.out, f))) for f in ("audio", "visual", "audiovisual") if os.path.isdir(os.path.join(a.out, f)))
    with ThreadPoolExecutor(a.workers) as ex, open(os.path.join(a.out, "index.jsonl"), "a") as idx:
        for m in ex.map(publish, [(r, start + i, a.out, facts, P, a.bin, a.secs) for i, r in enumerate(picked)]):
            meta, sibs = m
            for row in ([meta] if meta else []) + sibs:
                idx.write(json.dumps(row) + "\n"); idx.flush()
    print("published into", a.out)


if __name__ == "__main__":
    main()
