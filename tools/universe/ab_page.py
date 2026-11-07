#!/usr/bin/env python3
"""Blind A/B page: ab_page.py out_dir name1=before1.wav,after1.wav name2=... ; loudness-matched (ffmpeg loudnorm -23 LUFS),
embedded as mp3, A/B order shuffled (key in pairs.json). Open index.html, pick per row, 'copy picks'."""
import base64, json, os, random, subprocess, sys, tempfile
out = sys.argv[1]; os.makedirs(out, exist_ok=True); rows = []; key = {}
rng = random.Random(7)
def enc(p, d):
    o = os.path.join(d, os.path.basename(p) + ".mp3")
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", p, "-af", "loudnorm=I=-23:TP=-3:LRA=11", "-ar", "44100", "-b:a", "128k", o], check=True)
    return base64.b64encode(open(o, "rb").read()).decode()
with tempfile.TemporaryDirectory() as d:
    for arg in sys.argv[2:]:
        name, pr = arg.split("="); b, a = pr.split(","); flip = rng.random() < 0.5
        x, y = (a, b) if flip else (b, a); key[name] = {"A": "after" if flip else "before", "B": "before" if flip else "after"}
        rows.append((name, enc(x, d), enc(y, d)))
html = ["<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>A/B</title><style>body{font:16px system-ui;margin:24px;max-width:640px}div{margin:18px 0}audio{width:100%}button{margin-right:8px}</style><h2>Blind A/B, same phrase, loudness-matched</h2>"]
for i, (n, a, b) in enumerate(rows):
    html.append("<div><b>%s</b><br>A <audio controls src='data:audio/mpeg;base64,%s'></audio>B <audio controls src='data:audio/mpeg;base64,%s'></audio>" % (n, a, b))
    html.append("".join("<button onclick=\"P['%s']='%s';this.parentNode.dataset.p='%s';s()\">%s</button>" % (n, v, v, l) for v, l in (("A", "A better"), ("B", "B better"), ("same", "same"))) + "<span id=r%d></span></div>" % i)
html.append("<pre id=o></pre><script>var P={};function s(){o.textContent=JSON.stringify(P)}</script>")
open(os.path.join(out, "index.html"), "w").write("".join(html)); json.dump(key, open(os.path.join(out, "pairs.json"), "w"))
print("wrote", out, os.path.getsize(os.path.join(out, "index.html")) // 1024, "KB")
