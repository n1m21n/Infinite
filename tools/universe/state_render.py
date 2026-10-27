#!/usr/bin/env python3
"""W7 join test: render one saved plug-in patch (a state chunk) and measure it.

    state_render.py PATCH.inf [--label NAME] [--out ~/Infinite/training-data/audio/state_renders.jsonl]

PATCH.inf is any Infinite patch saved with a Plugin node that has the sound loaded (add the plug-in, pick the patch in its window, save).
Its `s plugin_state` is read and put into the same Keyboard -> Plugin -> Audio Out patch render_pass.py uses, then C2 / C3 / C4 are
rendered headless and measured (32-band row). One row per note, label = the patch name you give, ready to be compared with the
preset's stored tags (next-phase.md W7). Statistics only: the WAV is deleted."""
import argparse, json, os, re, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_pass as rp  # noqa: E402


def read_node(path):
    kv = {}
    for ln in open(path):
        m = re.match(r"\s+s (plugin_format|plugin_id|plugin_name|plugin_state) (.*)$", ln.rstrip("\n"))
        if m:
            kv[m.group(1)] = m.group(2)
    if "plugin_state" not in kv or len(kv["plugin_state"]) < 32:
        sys.exit("no plugin_state in %s: load a sound in the plug-in window and save the patch again" % path)
    return kv


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("patch")
    ap.add_argument("--label")
    ap.add_argument("--out", default="~/Infinite/training-data/audio/state_renders.jsonl")
    a = ap.parse_args()
    kv = read_node(a.patch)
    t = {"instrument": True, "format": kv["plugin_format"], "id": kv["plugin_id"], "name": kv.get("plugin_name", ""), "vendor": ""}
    with tempfile.TemporaryDirectory() as tmp:
        inf = os.path.join(tmp, "p.inf")
        open(inf, "w").write(rp.patch_text(t, "", []).replace("  s plugin_name", "  s plugin_state %s\n  s plugin_name" % kv["plugin_state"], 1))
        binp = rp.infinite_bin()
        ep, mp = rp.write_notes(tmp)
        js, wav = os.path.join(tmp, "s.json"), os.path.join(tmp, "o.wav")
        p = subprocess.run([binp, "--audio-summary", inf, js, "--duration", "%g" % (rp.LEAD + rp.WIN * 3), "--sample-rate", str(rp.SR), "--wav", wav,
                            "--lenient", "--notes", ep, "--note-map", mp], capture_output=True, text=True, timeout=120)
        if p.returncode != 0 or not os.path.exists(wav):
            sys.exit("render failed: " + (p.stderr.strip().splitlines() or ["rc %d" % p.returncode])[-1])
        rows = rp.measure_windows(wav, tmp, [n for _, _, n in rp.NOTES])
    label = a.label or os.path.splitext(os.path.basename(a.patch))[0]
    with open(os.path.expanduser(a.out), "a") as fo:
        for lab, feat in rows:
            row = {"src": "state:%s" % label, "label": label, "plugin": kv.get("plugin_name"), "plugin_id": kv["plugin_id"], "note": lab,
                   "license": "plugin render, local only"}
            row.update({k: v for k, v in (feat or {}).items() if k not in ("src", "pack", "part", "bpm", "key", "loop")})
            fo.write(json.dumps(row, separators=(",", ":")) + "\n")
            print(lab, "centroid", row.get("centroid"), "rms", row.get("rms"))


if __name__ == "__main__":
    main()
