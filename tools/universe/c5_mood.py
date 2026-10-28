#!/usr/bin/env python3
"""C5 listening test: does the listener hear the mood that was asked for? 40 blind clips = 20 real EMOPIA pieces (labelled Q1-Q4) rendered
through the same Analog patch + 20 compose.py clips asked for the same four moods (5 each). Listener picks one of four moods per clip.
    c5_mood.py build [--secs 12]     render + loudness-match + shuffle -> eval/listen/c5_mood/ (index.html); key in c5_mood_key.json
    c5_mood.py score FILE            agreement with the intended mood, split real / composed. Bar (goal item 5): >= 28 of 40."""
import argparse, glob, json, os, random, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compose as cp, own_sweeps as ow, fit_ve as fv
OUT = os.path.expanduser("~/Infinite/training-data/eval/listen")
QV = {"Q1": (1, 1), "Q2": (-1, 1), "Q3": (-1, -1), "Q4": (1, -1)}
LABEL = {"Q1": "Happy / excited", "Q2": "Tense / angry", "Q3": "Sad / gloomy", "Q4": "Calm / relaxed"}
PAGE = """<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Mood check</title><style>body{font:16px system-ui;max-width:720px;margin:24px auto;padding:0 16px}
.r{display:flex;gap:8px;align-items:center;margin:10px 0;padding:8px;border:1px solid #8884;border-radius:8px;flex-wrap:wrap}
audio{flex:1;min-width:220px}button{padding:6px 10px;border-radius:6px;border:1px solid #888;background:none;color:inherit;font:inherit}
button.on{background:#2a7;color:#fff}</style><h2>What mood is this?</h2><p>40 clips, volume matched. Pick the one mood that fits best, then Save.</p>
<div id=l></div><p><button onclick=save()>Save answers</button> <span id=m></span></p>
<script>const N=__N__,A={},L=__L__;const l=document.getElementById('l');
for(let i=1;i<=N;i++){const id='clip_'+String(i).padStart(2,'0');const d=document.createElement('div');d.className='r';
d.innerHTML='<b>'+i+'</b><audio controls preload=none src="'+id+'.wav"></audio>'+Object.keys(L).map(k=>'<button data-v='+k+'>'+L[k]+'</button>').join('');
d.querySelectorAll('button').forEach(b=>b.onclick=()=>{A[id]=b.dataset.v;d.querySelectorAll('button').forEach(x=>x.classList.toggle('on',x===b))});l.append(d)}
function save(){const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(A)],{type:'application/json'}));a.download='c5_answers.json';a.click();
document.getElementById('m').textContent=Object.keys(A).length+' of '+N+' answered'}</script>"""


def real_part(path, secs):
    import mido
    t = 0.0; on = {}; ev = []
    for m in mido.MidiFile(path):
        t += m.time
        if m.type == "note_on" and m.velocity > 0: on[m.note] = (t, m.velocity)
        elif m.type in ("note_off", "note_on") and m.note in on:
            t0, v = on.pop(m.note)
            if t0 < secs: ev.append((t0, m.note, v / 127.0, max(t - t0, 0.05)))
    table, sw, fixed = ow.param_table("Analog"); rec = ow.recipe("Analog", sw, fixed, [0.5] * len(sw))
    rec.update({**cp.CLEAN, "wave1": 0, "wave2": 2, "oscMix": 0.3, "osc1Vol": 0.8, "osc2Vol": 0.5, "sub": 0.0, "voices": 1, "cutoff": 5000.0, "attack": 8.0, "decay": 500.0, "sustain": 0.4, "release": 350.0, "keyTrack": 0.5})
    return [cp.Part("pad", "Analog", rec, ev, -8.0, 0.0, 0.25)]


def cmd_build(a):
    d = os.path.join(OUT, "c5_mood"); os.makedirs(d, exist_ok=True); rng = random.Random(11); clips = []
    held = sorted(glob.glob(os.path.join(fv.ROOT, "**", "Q[1-4]_*.mid"), recursive=True))
    for q in QV:
        pool = [p for p in held if os.path.basename(p).startswith(q)]; rng.shuffle(pool); got = 0
        for p in pool:
            if got == 5: break
            wav, _ = cp.render(real_part(p, a.secs), cp.Chain(), os.path.join(d, "_r_%s_%d" % (q, got)), a.secs)
            if wav and cp.measure(wav)["ok"]: clips.append(("real", q, wav)); got += 1
        got = 0; cp.MOOD = QV[q]
        for s in range(300, 400):
            if got == 5: break
            r = cp.make(s + 100 * int(q[1]), a.secs, os.path.join(d, "_c_%s_%d" % (q, got)))
            if r.get("measure", {}).get("ok"): clips.append(("composed", q, os.path.join(d, "_c_%s_%d" % (q, got), "sample.wav"))); got += 1
        cp.MOOD = None
    rng.shuffle(clips); key = {}
    for i, (side, q, src) in enumerate(clips, 1):
        cid = "clip_%02d" % i
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", src, "-t", str(a.secs), "-ac", "2", "-ar", "44100", "-af", "loudnorm=I=-20:TP=-2:LRA=11",
                        os.path.join(d, cid + ".wav")], check=True); key[cid] = {"side": side, "mood": q}
    json.dump(key, open(os.path.join(OUT, "c5_mood_key.json"), "w"), indent=1)
    open(os.path.join(d, "index.html"), "w").write(PAGE.replace("__N__", str(len(key))).replace("__L__", json.dumps(LABEL)))
    print("built %d clips in %s" % (len(key), d))


def cmd_score(a):
    key = json.load(open(os.path.join(OUT, "c5_mood_key.json"))); ans = json.load(open(a.file)); by = {}
    for cid, say in ans.items():
        k = key[cid]; by.setdefault(k["side"], []).append(say == k["mood"])
    tot = sum(sum(v) for v in by.values())
    for s, v in by.items(): print("%-9s %d / %d" % (s, sum(v), len(v)))
    print("TOTAL %d / %d   bar >= 28 of 40: %s" % (tot, len(ans), "PASS" if tot >= 28 and len(ans) == 40 else "not met"))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["build", "score"]); ap.add_argument("file", nargs="?"); ap.add_argument("--secs", type=float, default=12)
    a = ap.parse_args(); (cmd_build if a.cmd == "build" else cmd_score)(a)
