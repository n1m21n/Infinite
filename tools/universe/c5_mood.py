#!/usr/bin/env python3
"""C5 listening test: does the listener hear the mood that was asked for? 40 blind clips = 20 real EMOPIA pieces (labelled Q1-Q4) played
on an FM electric piano (EMOPIA is piano) + 20 Keys parts (chords, no drums) from the live generator (website wasm, the node's own mood / energy dots) asked
for the same four moods (5 each). The same 5 styles appear in every mood, so the style never gives the mood away; each clip has its own
seed, so voices differ. Listener picks one of four moods per clip.
Round 2 (one Analog voice for all 40, compose.py) is kept at eval/listen/c5_mood_r2 + c5_mood_key_r2.json: the voice coloured every clip.
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
    table, sw, fixed = ow.param_table("FM"); rec = ow.recipe("FM", sw, fixed, [0.5] * len(sw))
    rec.update(EPIANO)
    return [cp.Part("pad", "FM", rec, ev, -8.0, 0.0, 0.25)]


# FM electric piano on "4>3, 2>1": 2>1 is the body (1:1, the bend fades as the note dies), 4>3 the tine (14:1 click that dies in ~0.1 s)
EPIANO = {"algorithm": 4, "feedback": 0.0, **{"fixed%d" % o: 0 for o in (1, 2, 3, 4)},
          "ratio1": 1.0, "level1": 0.9, "attack1": 2.0, "decay1": 3000.0, "sustain1": 0.0, "release1": 500.0,
          "ratio2": 1.0, "level2": 0.35, "attack2": 2.0, "decay2": 1500.0, "sustain2": 0.1, "release2": 500.0,
          "ratio3": 1.0, "level3": 0.4, "attack3": 2.0, "decay3": 1200.0, "sustain3": 0.0, "release3": 300.0,
          "ratio4": 14.0, "level4": 0.25, "attack4": 1.0, "decay4": 120.0, "sustain4": 0.0, "release4": 100.0}
STYLES = [0, 11, 22, 24, 29]   # deep_house, liquid_dnb, downtempo, synthwave, neo_classical: no style that is a mood by itself
DOTS = {"Q1": (0.8, 0.7), "Q2": (-0.8, 0.7), "Q3": (-0.8, -0.7), "Q4": (0.8, -0.7)}   # (mood, energy) asked of the node
GEN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../website/gen")


def composed(d, rng):
    """5 clips per mood from the live generator, one per style, each with its own seed. Part 9 = Keys (chords on a piano / tine / organ / clav
    voice): the full mix is drum-led and buries the notes (HANDOFF, round 3 finding), so no drums."""
    jobs = [{"style": s, "part": 9, "seed": rng.randrange(100000), "bars": 8, "mood": DOTS[q][0], "energy": DOTS[q][1], "out": os.path.join(d, "_c_%s_%d.wav" % (q, i)), "q": q}
            for q in QV for i, s in enumerate(STYLES)]
    jf = os.path.join(d, "_jobs.json"); json.dump(jobs, open(jf, "w"))
    r = subprocess.run(["node", os.path.join(os.path.dirname(os.path.abspath(__file__)), "wasm_render_mood.js"), GEN, jf], capture_output=True, text=True, check=True)
    done = {json.loads(l)["out"]: json.loads(l) for l in r.stdout.splitlines() if l.startswith("{")}
    return [("composed", j["q"], j["out"]) for j in jobs if done.get(j["out"], {}).get("frames")]


def cmd_build(a):
    d = os.path.join(OUT, "c5_mood"); os.makedirs(d, exist_ok=True); rng = random.Random(11); clips = []
    held = sorted(glob.glob(os.path.join(fv.ROOT, "**", "Q[1-4]_*.mid"), recursive=True))
    for q in QV:
        pool = [p for p in held if os.path.basename(p).startswith(q)]; rng.shuffle(pool); got = 0
        for p in pool:
            if got == 5: break
            wav, _ = cp.render(real_part(p, a.secs), cp.Chain(), os.path.join(d, "_r_%s_%d" % (q, got)), a.secs)
            if wav and cp.measure(wav)["ok"]: clips.append(("real", q, wav)); got += 1
    clips += composed(d, rng)
    rng.shuffle(clips); key = {}
    for i, (side, q, src) in enumerate(clips, 1):
        cid = "clip_%02d" % i
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", src, "-t", str(a.secs), "-ac", "2", "-ar", "44100", "-af", "loudnorm=I=-20:TP=-2:LRA=11",
                        os.path.join(d, cid + ".wav")], check=True); key[cid] = {"side": side, "mood": q, "src": os.path.relpath(src, d)}
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
