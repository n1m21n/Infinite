#!/usr/bin/env python3
"""Do switch slots hold up on the engine? Proposes a switch on N random audio genomes, expands, validates every alternative,
and for the first --render of them renders each alternative to check the switch really changes the sound.
    switch_test.py --n 40 --render 6"""
import argparse, json, os, random, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u, scene, switches

ap = argparse.ArgumentParser(); ap.add_argument("--n", type=int, default=40); ap.add_argument("--render", type=int, default=6)
ap.add_argument("--bin", default="/Users/namansoni/Infinite/worktrees/main/build/Infinite.app/Contents/MacOS/Infinite"); a = ap.parse_args()
f = u.load_facts(); P = scene.pools_v2(f)
fails, made, kinds, rendered = [], 0, {"type": 0, "mode": 0}, []
for s in range(a.n):
    r = random.Random(1000 + s)
    g = scene.audio_genome(r, P, f, r.randint(2, 4), 70)
    sw = switches.propose(g, f, P, r)
    if not sw:
        continue
    made += 1
    alts = switches.expand(sw)
    kinds["mode" if "~" in sw else "type"] += 1
    for j, c in enumerate(alts):
        p = "/tmp/sw_%d_%d.inf" % (s, j); open(p, "w").write(u.compile_inf(c, f, P))
        cp = subprocess.run([a.bin, "--validate", p], capture_output=True, text=True)
        if cp.returncode:
            fails.append((c, u.last_json(cp.stdout)[:200]))
    if len(rendered) < a.render:
        res = []
        for j, c in enumerate(alts):
            p = "/tmp/sw_%d_%d.inf" % (s, j)
            subprocess.run([a.bin, "--audio-summary", p, p + ".json", "--duration", "4"], capture_output=True, timeout=120)
            try:
                d = json.load(open(p + ".json")); res.append((d.get("integrated_lufs"), d.get("spectral_centroid_hz") or d.get("spectral_centroid")))
            except Exception:
                res.append(None)
        rendered.append((sw, res))
print("switch genomes %d  (type %d, mode %d)  invalid alternatives %d" % (made, kinds["type"], kinds["mode"], len(fails)))
for c, m in fails[:5]:
    print("FAIL", c[:160], m)
for sw, res in rendered:
    print(sw[:110], "->", res)
