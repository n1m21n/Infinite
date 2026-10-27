#!/usr/bin/env python3
"""Response harness (recipes.md build step 1): what does each knob of Infinite's own nodes do to the sound?

    response_harness.py [--category AudioEffects] [--only TYPE[,TYPE]] [--fracs 0,0.5,1] [--max-params 16]
                        [--budget 20] [--resume] [--timeout 60] [--list] [--out ~/Infinite/training-data/audio/response_native.jsonl]

For each node type: a small .inf (Audio File test signal -> node -> Audio Out; Synths: Keyboard -> node), rendered headless with
`Infinite --audio-summary`. Baseline = every param at its default (mix/wet-like params pinned to 1 so the node is audible),
then each numeric param alone at each fraction of its own range. Each render is cut into the three stimulus windows
(drum loop | pink burst | sine sweep; synths: C2/C3/C4) and measured with audio_features.analyse, same row format as render_pass.py.
One JSON row per (type, param, frac, stimulus); the delta against the baseline row is the node's measured response.
Resumable by (type, setting); failures go to <out>.failures. Statistics only: the WAV is deleted after measuring."""
import argparse, json, math, os, subprocess, sys, tempfile, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_pass as rp  # noqa: E402  (stimulus, notes, windows, binary lookup: one definition of "a render")

TD = os.path.expanduser("~/Infinite/training-data")
LICENSE = "own node render, local only"
WETISH = {"mix", "wet", "amount", "blend"}


def load_types(a):
    d = json.load(open(os.path.join(HERE, "describe_all.json")))["types"]
    want = set(a.only.split(",")) if a.only else None
    return [t for t in d if (want and t["type"] in want) or (not want and t["category"] == a.category)]


def sweepable(t, cap):
    out = []
    for r in t["rows"]:
        if r["tag"] in ("f", "i") and r.get("min") is not None and r.get("max") is not None and r["max"] > r["min"] and r["key"] not in WETISH:
            out.append(r)
    return out[:cap]


def patch(t, wav, values):
    synth = t["category"] in ("Synths",)
    L = ["infinite-patch 1"]
    if synth:
        L += ["node 1 Notes Keyboard", "  id keys", "end"]
    else:
        L += ["node 1 Modulators Audio File", "  id src", "  s path " + wav, "  b loop 0", "  f volume 1", "  f gain 1", "  f attack 0", "  f release 0", "end"]
    L += ["node 2 %s %s" % (t["category"], t["type"]), "  id fx"]
    for k, (tag, v) in values.items():
        L.append("  %s %s %s" % (tag, k, ("%r" % v) if tag == "f" else int(v)))
    L += ["end", "node 3 Utility Audio Out", "  id out", "end", "aud out audio fx", "note fx 1 keys" if synth else "aud fx 0 src"]
    return "\n".join(L) + "\n"


def render(binp, t, values, tmp, stim, timeout):
    inf, js, wav = (os.path.join(tmp, n) for n in ("p.inf", "s.json", "out.wav"))
    open(inf, "w").write(patch(t, stim, values))
    synth = t["category"] == "Synths"
    cmd = [binp, "--audio-summary", inf, js, "--duration", "%g" % (rp.LEAD + rp.WIN * 3), "--sample-rate", str(rp.SR), "--wav", wav, "--lenient",
           "--timeout", str(max(5, int(timeout) - 5))]
    if synth:
        ep, mp = rp.write_notes(tmp)
        cmd += ["--notes", ep, "--note-map", mp]
    if os.path.exists(wav):
        os.remove(wav)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "hang"
    if p.returncode != 0 or not os.path.exists(wav):
        return None, "rc %d: %s" % (p.returncode, (p.stderr.strip().splitlines() or [""])[-1][:120])
    labels = [n for _, _, n in rp.NOTES] if synth else rp.STIMS
    return rp.measure_windows(wav, tmp, labels), None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--category", default="AudioEffects")
    ap.add_argument("--only")
    ap.add_argument("--fracs", default="0,0.5,1")
    ap.add_argument("--max-params", type=int, default=16)
    ap.add_argument("--budget", type=float, default=20)
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--out", default=os.path.join(TD, "audio", "response_native.jsonl"))
    a = ap.parse_args()
    types = load_types(a)
    if a.list:
        for t in types:
            print("%-14s %-22s %2d sweepable params" % (t["category"], t["type"], len(sweepable(t, a.max_params))))
        return
    out = os.path.expanduser(a.out)
    done = set()
    if a.resume and os.path.exists(out):
        for l in open(out):
            r = json.loads(l)
            done.add((r["node"], r["setting"]))
    binp = rp.infinite_bin()
    fr = [float(x) for x in a.fracs.split(",")]
    t_end = time.time() + a.budget * 60
    n_ok = n_bad = 0
    with tempfile.TemporaryDirectory() as tmp, open(out, "a" if a.resume else "w") as fo, open(out + ".failures", "a") as ff:
        stim = os.path.join(tmp, "stim.wav")
        rp.write_wav24(stim, rp.stimulus())
        for t in types:
            base = {r["key"]: (r["tag"], 1.0) for r in t["rows"] if r["key"] in WETISH and r["tag"] == "f"}
            settings = [("default", None, None, dict(base))]
            for r in sweepable(t, a.max_params):
                for f in fr:
                    v = r["min"] + f * (r["max"] - r["min"])
                    vals = dict(base)
                    vals[r["key"]] = (r["tag"], v)
                    settings.append(("%s=%g" % (r["key"], f), r["key"], f, vals))
            for key, param, f, vals in settings:
                if (t["type"], key) in done:
                    continue
                if time.time() > t_end:
                    print("render pass: budget reached; --resume continues")
                    return
                rows, err = render(binp, t, vals, tmp, stim, a.timeout)
                if err or not rows:
                    ff.write(json.dumps({"node": t["type"], "setting": key, "why": err or "no windows"}) + "\n")
                    n_bad += 1
                    if key == "default":
                        break  # the node does not render at all: skip its sweep
                    continue
                for lab, feat in rows:
                    row = {"src": "node:%s:%s" % (t["type"], key), "node": t["type"], "category": t["category"], "setting": key,
                           "stimulus": lab, "param": param, "frac": f}
                    if param:
                        row["value"] = vals[param][1]
                    if feat:
                        for k in ("src", "pack", "part", "bpm", "key", "loop"):
                            feat.pop(k, None)
                        row.update(feat)
                    row["license"] = LICENSE
                    fo.write(json.dumps(row, separators=(",", ":")) + "\n")
                fo.flush()
                n_ok += 1
            print("%-22s done (%d ok, %d failed so far)" % (t["type"], n_ok, n_bad), flush=True)
    print("response harness: %d renders ok, %d failed -> %s" % (n_ok, n_bad, out))


if __name__ == "__main__":
    main()
