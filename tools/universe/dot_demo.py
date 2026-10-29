#!/usr/bin/env python3
"""Audible check of the sound model: one 'dot' = one sound axis. Slopes (knob -> axis, in spreads per unit of knob) and which cells are trusted
come from the Analog one-knob pairs (knob_direction.pairs). The model moves the TRUSTED knobs for that axis to hit each target step, one phrase is
rendered per step, steps joined into one wav. Prints the measured change of the real axis so the ear and the numbers can be compared.
    dot_demo.py --axis brightness [--node Analog] [--out DIR]"""
import argparse, os, random, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import knob_direction as kd, own_sweeps as ow, compose as cp, p3_characteristics as p3

def model(node, axis):
    P, nk = kd.pairs(node); means = [n for n in p3.COLS if n.endswith(".mean")]; a = means.index(axis + ".mean")
    num = np.zeros(nk); den = np.zeros(nk); up = np.zeros(nk); n = np.zeros(nk)
    for g, j, dk, mv in P:
        num[j] += mv[a] * dk; den[j] += dk * dk
        if abs(mv[a]) >= 0.3: n[j] += 1; up[j] += np.sign(mv[a]) * np.sign(dk) > 0
    slope = num / np.maximum(den, 1e-9); cons = np.where(n >= 8, np.maximum(up, n - up) / np.maximum(n, 1), 0)
    return slope, cons

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--axis", default="brightness"); ap.add_argument("--node", default="Analog")
    ap.add_argument("--out", default=os.path.expanduser("~/Infinite/render-runs/dot_demo")); a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
    slope, cons = model(a.node, a.axis); table, sw, fixed = ow.param_table(a.node)
    BAN = ("noise", "drive", "fm", "sub")   # a dot may not buy brightness with noise
    key = lambda j: sw[j] if isinstance(sw[j], str) else sw[j].get("key")
    trusted = [j for j in np.argsort(-np.abs(slope)) if cons[j] >= 0.8 and key(j) not in BAN][:3]
    print("trusted knobs for %s:" % a.axis, [(sw[j] if isinstance(sw[j], str) else sw[j].get("key", sw[j]), round(float(slope[j]), 2), round(float(cons[j]), 2)) for j in trusted])
    base = np.full(len(sw), 0.5)
    for j in range(len(sw)):
        k = sw[j] if isinstance(sw[j], str) else sw[j].get("key")
        if k in cp.CLEAN or k in ("noise", "drive", "fm", "sub"): base[j] = 0.0
    rng = random.Random(3); files = sorted(__import__("glob").glob(cp.MIDI_GLOB, recursive=True)); roles = cp.midi_parts(files[3], 40, rng)
    ev = [(t, p, v, l) for t, p, v, l in cp.stretch(roles["pad"], 90) if t < 8]
    lead = [(t, p, v, l) for t, p, v, l in cp.stretch(roles["lead"], 90) if t < 8]
    wavs = []
    for step in (-0.6, -0.3, 0.0, 0.3, 0.6):
        u = base.copy(); need = step
        for j in trusted:  # split the move across the trusted knobs by slope share
            u[j] = float(np.clip(u[j] + need * np.sign(slope[j]) / len(trusted), 0.05, 0.95))
        rec = ow.recipe(a.node, sw, fixed, list(u)); rec.update(cp.CLEAN)
        for j in trusted:
            k = sw[j] if isinstance(sw[j], str) else sw[j].get("key")
            if k in rec and k in cp.CLEAN: rec[k] = cp.CLEAN[k]
        d = os.path.join(a.out, "step%+d" % step)
        wav, log = cp.render([cp.Part("lead", a.node, rec, lead, -6.0, 0.0, 0.2)], cp.Chain(), d, 8)
        print("step %+.0f" % step, "OK" if wav else "FAIL " + log[-120:], cp.measure(wav)["centroid_hz"] if wav else "")
        if wav: wavs.append(wav)
    lst = os.path.join(a.out, "list.txt"); open(lst, "w").write("".join("file '%s'\n" % w for w in wavs))
    out = os.path.join(a.out, "dot_%s.wav" % a.axis)
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "concat", "-safe", "0", "-i", lst, "-af", "loudnorm=I=-20:TP=-2", out], check=True); print("wrote", out)

main()
