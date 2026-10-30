#!/usr/bin/env python3
"""Render our bass click (Wavetable) and kick noise click (Analog) layers alone and measure them like SubLab's attacks.
    click_render.py OUTDIR [bass_hp_lo bass_hp_hi kick_lo kick_hi kick_dec_lo kick_dec_hi]  (INF_STIM=C1). Item 16."""
import os, sys, json, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import own_sweeps as o, click_measure as cm
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
AD = (3, 8)
hl, hh, kl, kh = (float(x) for x in sys.argv[2:6]) if len(sys.argv) > 5 else (1200, 3000, 3000, 8000)
r = np.random.RandomState(1); KD = (float(sys.argv[6]), float(sys.argv[7])) if len(sys.argv) > 7 else (5, 15)
def wt(hp, sine):
    k = {"a.on": 1, "a.volume": 1, "b.on": 0, "fmDepth": 0, "glide": 0, "pitchBend": 0, "volume": 0.9, "mix": 0,
         "a.table": 0, "a.position": 0.0 if sine else 0.333, "a.unison": 1, "a.detune": 0, "a.octave": 1, "a.semi": 0, "a.fine": 0,
         "a.phase": 0, "a.warpAmount": 0, "a.pitchAttack": 0, "a.pitchSustain": 0, "a.pitchRelease": 0,
         "a.pitchAmount": r.uniform(30, 44), "a.pitchDecay": r.uniform(2, 6), "a.filterType": 4, "a.cutoff": round(hp),
         "a.resonance": 0, "a.filterAmount": 0, "a.ampAttack": 0, "a.ampDecay": round(r.uniform(*AD)), "a.ampSustain": 0, "a.ampRelease": 6}
    return k
def an(cut):
    d = r.uniform(*KD)
    return {"volume": 0.8, "osc1Vol": 0, "osc2Vol": 0, "sub": 0, "noise": 1, "attack": 0, "decay": d, "sustain": 0, "release": d * 0.5,
            "cutoff": cut, "resonance": 0.1, "voices": 1, "keyTrack": 0, "filterType": 2}
res = {}
for node, recs in (("Wavetable", [wt(r.uniform(hl, hh), i % 2 == 0) for i in range(16)]), ("Analog", [an(r.uniform(kl, kh)) for _ in range(16)])):
    table, sw, fixed = o.param_table(node); tags = o.tag_of(table)
    wavs = o.render_batch(o.binary(), node, tags, recs, out)
    M = [m for m in (cm.measure(w) for w in wavs if w) if m]
    res[node] = {k: [round(float(np.percentile([m[k] for m in M], q)), 3) for q in (10, 50, 90)] for k in M[0]}
print(json.dumps(res))
