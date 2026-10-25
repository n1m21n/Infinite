#!/usr/bin/env python3
"""Combined order: an audio genome and a visual genome in one patch, joined by cross modulation.
    <audio genome> ## <visual genome> ; L18.7<2@9.radius ...
`<j` and `@k` index the combined chain order (audio nodes first, then visual nodes).
Audio->visual: Audio Analyze / Audio to CV reads an audio node and drives a visual param.
Visual->audio: Image Analyze reads a visual node and drives an audio param.
Probe = audio summary + three frames; ok only when both sides are alive."""
import argparse, json, os, random, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u
import visual as v


def shift(text, off):
    out = []
    for l in text.splitlines():
        p = l.split()
        if p and p[0] == "node":
            p[1] = str(int(p[1]) + off)
            out.append(" ".join(p))
        elif p and p[0] in ("note", "aud", "cable", "mod") and not l.startswith(" "):
            p[1] = str(int(p[1]) + off)
            p[3] = str(int(p[3]) + off)
            out.append(" ".join(p))
        else:
            out.append(l)
    return out


def split(code):
    body, _, cross = code.partition(" ;;")
    a, _, vis = body.partition(" ## ")
    return a.strip(), vis.strip(), cross.strip()


def compile_combined(code, facts, P, VP):
    a, vis, cross = split(code)
    ao, vo = [], []
    at = u.compile_inf(a, facts, P, ao)
    vt = v.compile_visual(vis, facts, VP, vo)
    na = sum(1 for l in at.splitlines() if l.startswith("node "))
    lines = at.splitlines() + shift(vt, na)[1:]  # drop the second header
    order = ao + [x + na for x in vo]
    names = {}
    for l in lines:
        if l.startswith("node "):
            p = l.split(); names[int(p[1])] = " ".join(p[3:])
    nid = max(names) if names else 0
    for tok_ in cross.split():
        src, _, dst = tok_.partition("@")
        src, _, frm = src.partition("<")
        idx, _, seed = src[1:].partition(".")
        k, _, key = dst.partition(".")
        mname = P["L"][int(idx) % len(P["L"])]
        t = facts[mname]
        nid += 1
        lines.append("node %d %s %s" % (nid, t["category"], mname.split(":")[0]))
        for kk, (tag, val) in sorted(u.seeded_params(t, int(seed or 0)).items()):
            lines.append("  %s %s %s" % (tag, kk, ("%.6g" % val) if tag == "f" else val if tag == "s" else int(val)))
        lines.append("end")
        src_node = order[int(frm) % len(order)]
        skind = facts[names[src_node]]["outputs"][0]["kind"]
        slot = next(i["slot"] for i in t["inputs"] if i["kind"] == skind)
        lines.append("%s %d %d %d" % ({"audio": "aud", "note": "note", "image": "cable"}[skind], nid, slot, src_node))
        lines.append("mod %d %s %d 0 0 %.3f 0.5" % (order[int(k) % len(order)], key, nid, random.Random(int(seed or 0)).uniform(0.1, 1)))
    return u.index_code_mods("\n".join(lines) + "\n")


def sample(r, P, VP, facts, pick, vpick):
    a = u.sample_code(r, P, facts, r.choice([1, 2, 3]), pick)
    vis = v.sample(r, VP, facts, r.choice([1, 2, 3]), vpick)
    an, vn = u.node_names(a, P), v.u.node_names(vis, VP)
    ids = [0] * len(an) + [1] * len(vn)
    names = an + vn
    readers = {"audio": [i for i, n in enumerate(P["L"]) if any(x["kind"] == "audio" for x in facts[n]["inputs"])],
               "image": [i for i, n in enumerate(P["L"]) if any(x["kind"] == "image" for x in facts[n]["inputs"])]}
    toks = []
    for _ in range(r.choice([1, 2, 3])):
        j = r.randrange(len(names))
        kind = facts[names[j]]["outputs"][0]["kind"]
        if kind not in readers or not readers[kind]:
            continue
        tg = [k for k in range(len(names)) if ids[k] != ids[j] and any(m.get("key") for m in facts[names[k]]["modulatable"])]
        if not tg:
            continue
        k = r.choice(tg)
        key = r.choice([m["key"] for m in facts[names[k]]["modulatable"] if m.get("key")])
        toks.append("L%d.%d<%d@%d.%s" % (r.choice(readers[kind]), u.sd(r), j, k, key))
    return a + " ## " + vis + (" ;; " + " ".join(toks) if toks else "")


def probe(code, facts, P, VP, binary, workdir):
    h = "c%d" % (hash(code) & 0xffffffff)
    path = os.path.join(workdir, h + ".inf")
    t0 = time.time()
    res = {"code": code}
    try:
        open(path, "w").write(compile_combined(code, facts, P, VP))
        cp = subprocess.run([binary, "--audio-summary", path, path + ".json", "--duration", "4"], capture_output=True, text=True, timeout=60)
        j = json.loads(u.last_json(cp.stdout))
        if cp.returncode or not j.get("ok"):
            res["class"] = "error:" + ((j.get("errors") or [{}])[0].get("code") or "?")
            res["msg"] = (j.get("errors") or [{}])[0].get("message")
        else:
            res["a"] = j.get("audio_summary", {})
            ac = u.classify({"ok": True, "a": res["a"]})
            cp2 = subprocess.run([binary, "--frame", path, "0,1,2", os.path.join(workdir, h + "/")], capture_output=True, text=True, timeout=90)
            j2 = json.loads(u.last_json(cp2.stdout))
            vc = v.classify_frames(j2["frame_stats"]) if j2.get("ok") else "error:" + ((j2.get("errors") or [{}])[0].get("code") or "?")
            res["audio_class"], res["visual_class"] = ac, vc
            res["moves"] = len({round(s["mean_luma"], 3) for s in j2.get("frame_stats", [])}) > 1
            res["class"] = "ok" if ac in ("ok", "clipped") and vc == "ok" else "%s+%s" % (ac, vc)
    except subprocess.TimeoutExpired:
        res["class"] = "error:timeout"
    except Exception as e:
        res["class"] = "classify-error:" + type(e).__name__ + ":" + str(e)[:60]
    res["sec"] = round(time.time() - t0, 2)
    try:
        a, vis, _ = split(code)
        res["names"] = u.node_names(a, P) + u.node_names(vis, VP)
        res["order"] = 5
    except Exception:
        pass
    for p in (path, path + ".json"):
        os.path.exists(p) and os.remove(p)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["compile", "run"])
    ap.add_argument("code", nargs="?")
    ap.add_argument("--bin", default=u.DEFAULT_BIN)
    ap.add_argument("-n", type=int, default=50)
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--budget", type=float, default=600)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", default="atlas_combined.jsonl")
    a = ap.parse_args()
    facts = u.load_facts()
    P, VP = u.pools(facts), v.vpools(facts)
    if a.cmd == "compile":
        sys.stdout.write(compile_combined(a.code, facts, P, VP)); return
    r = random.Random(a.seed)
    pick, vpick = u.Pick(r, P, True), u.Pick(r, VP, True)
    codes = [sample(r, P, VP, facts, pick, vpick) for _ in range(a.n)]
    t0, done = time.time(), 0
    with tempfile.TemporaryDirectory() as wd, open(a.out, "a") as f, ThreadPoolExecutor(a.workers) as ex:
        futs = [ex.submit(lambda c=c: probe(c, facts, P, VP, a.bin, wd) if time.time() - t0 < a.budget else None) for c in codes]
        for fu in futs:
            res = fu.result()
            if res:
                f.write(json.dumps(res) + "\n"); done += 1
    print("probed", done, "of", a.n, "in", round(time.time() - t0), "s ->", a.out)


if __name__ == "__main__":
    main()
