#!/usr/bin/env python3
"""D1 of SOUND-MODEL.md 4b: the next batch of unrendered library presets, as a folder of links for preset_state.py --presets.

    d1_batch.py MAIN.jsonl --done 'audio/preset_diva_d1_*.jsonl' --order order_diva_rest.txt --presets ~/Infinite/scratch_omni/diva_air
                --out ~/Infinite/scratch_omni/diva_d1b [--n 2600]

Takes presets in --order (relative paths under --presets), skipping any already in MAIN or a --done file (by src) and any whose bank
the frozen group split of MAIN holds out (p3_characteristics.py would drop those rows, so rendering them is wasted). Links keep the
relative path, so a rendered row carries its bank."""
import argparse, glob, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sound_data as sd  # noqa: E402
import json  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("main"); ap.add_argument("--done", nargs="*", default=[]); ap.add_argument("--order", required=True)
    ap.add_argument("--presets", required=True); ap.add_argument("--out", required=True); ap.add_argument("--n", type=int, default=2600)
    ap.add_argument("--plugin", default="diva")
    a = ap.parse_args()
    root, out = os.path.expanduser(a.presets), os.path.expanduser(a.out)
    d = sd.load(a.main)
    seen = set(map(str, d["src"]))
    for pat in a.done:
        for p in glob.glob(os.path.expanduser(pat)):
            seen |= {json.loads(l)["src"] for l in open(p) if l.strip()}
    rel = [ln.rstrip("\n") for ln in open(os.path.expanduser(a.order), encoding="utf-8") if ln.strip()]
    cand = ["state:%s:%s" % (a.plugin, r) for r in rel]
    bank = sd.banks(list(map(str, d["src"])) + cand)
    held = set(np.array(bank[:len(d["src"])])[sd.split(d)["group"]])
    pick, skip_held = [], 0
    for r, s, b in zip(rel, cand, bank[len(d["src"]):]):
        if s in seen or not os.path.exists(os.path.join(root, r)):
            continue
        if b in held:
            skip_held += 1; continue
        pick.append(r)
        if len(pick) >= a.n:
            break
    for r in pick:
        dst = os.path.join(out, r); os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not os.path.lexists(dst):
            os.symlink(os.path.join(root, r), dst)
    print("%d presets linked into %s (%d skipped as held-out banks, %d held-out banks)" % (len(pick), out, skip_held, len(held)))


if __name__ == "__main__":
    main()
