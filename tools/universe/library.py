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


def run(cmd, timeout):
    try:
        cp = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return cp.returncode == 0
    except subprocess.TimeoutExpired:
        return False


def publish(args):
    r, i, out, facts, P, binary, secs = args
    kind = r["code"].split(" ")[0]
    folder = {"A": "audio", "V": "visual", "AV": "audiovisual"}[kind]
    eid = "s%04d" % i
    d = os.path.join(out, folder, eid)
    os.makedirs(d, exist_ok=True)
    inf = os.path.join(d, "patch.inf")
    open(inf, "w").write(scene.compile_scene(r["code"], facts, P))
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "patch-layout.py"), inf], capture_output=True)
    body = open(inf).read()
    head, _, rest = body.partition("\n")
    open(inf, "w").write("%s\n# genome: %s\n%s" % (head, r["code"], rest))
    if subprocess.run([binary, "--validate", inf], capture_output=True, text=True).returncode:
        shutil.rmtree(d); return None
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
        shutil.rmtree(d); return None
    meta = {"id": "%s/%s" % (folder, eid), "kind": kind, "genome": r["code"], "nodes": r["names"], "tags": tags_of(r),
            "class": r["class"], "audio": {k: v for k, v in (r.get("a") or {}).items() if k in ("integrated_lufs", "sample_peak_dbfs", "onset_count_estimate", "silence_ratio")},
            "files": sorted(f for f in os.listdir(d))}
    json.dump(meta, open(os.path.join(d, "meta.json"), "w"), indent=1)
    return meta


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
    picked = select(rows, a.n)
    print("candidates %d  picked %d" % (len(rows), len(picked)))
    os.makedirs(a.out, exist_ok=True)
    start = sum(len(os.listdir(os.path.join(a.out, f))) for f in ("audio", "visual", "audiovisual") if os.path.isdir(os.path.join(a.out, f)))
    with ThreadPoolExecutor(a.workers) as ex, open(os.path.join(a.out, "index.jsonl"), "a") as idx:
        for m in ex.map(publish, [(r, start + i, a.out, facts, P, a.bin, a.secs) for i, r in enumerate(picked)]):
            if m:
                idx.write(json.dumps(m) + "\n"); idx.flush()
    print("published into", a.out)


if __name__ == "__main__":
    main()
