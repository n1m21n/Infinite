#!/usr/bin/env python3
"""compose.py: the one place that writes a whole patch (INTEGRATION.md). Notes + recipe + chain + mix in one .inf, rendered
headless, loudness-measured.
    compose.py make --seed 7 [--secs 16] [--out DIR]      one composed sample: DIR/sample.inf, events.json, map.json, sample.wav, report.json
    compose.py batch --n 8 --seed 100 [--out DIR]          n samples, seeds seed..seed+n-1
Layers hand on only their fields: Part.events (note layer), Part.recipe (sound layer), Chain (effects layer), Part.gain/pan/send (mix layer)."""
import argparse, glob, json, os, random, subprocess, sys
from dataclasses import dataclass, field
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import own_sweeps as ow

MIDI_GLOB = os.path.expanduser("~/Infinite/datasets/midi/data/chord_progressions/**/*.mid")
SR = 44100
# role shaping: values forced on the recipe after the draw (only keys the engine has). The sound layer replaces this with a target point.
CLEAN = {"noise": 0.0, "drive": 0.0, "fm": 0.0, "resonance": 0.15, "detune": 8.0, "sync": 0, "filterType": 3}   # random draws put noise/drive/fm in every voice
ROLE = {
    "bass": {"oct1": -1, "sub": 0.6, "cutoff": 500.0, "resonance": 0.2, "attack": 0.01, "release": 0.25, "voices": 1, "detune": 0.0},
    "pad":  {"attack": 0.6, "release": 1.5, "sustain": 0.8, "cutoff": 3500.0, "voices": 3, "spread": 0.6},
    "lead": {"attack": 0.02, "release": 0.4, "sustain": 0.7, "cutoff": 6000.0, "voices": 2},
}
MIX = {"bass": (-4.0, 0.0, 0.0), "pad": (-9.0, 0.0, 0.45), "lead": (-6.0, 0.15, 0.3)}  # gain dB, pan, reverb send


@dataclass
class Part:
    role: str
    node: str
    recipe: dict
    events: list            # (t, pitch, vel 0-1, len)
    gain: float = -6.0
    pan: float = 0.0
    send: float = 0.2


@dataclass
class Chain:
    reverb: dict = field(default_factory=lambda: {"size": 0.7, "decay": 2.8, "damping": 0.5, "mix": 1.0, "width": 1.0})
    limiter: dict = field(default_factory=lambda: {"threshold": -3.0, "outGain": 0.0})


def midi_parts(path, secs, rng):
    """A chord-progression file (mood in its name) -> bass (lowest note of each chord), pad (the chord), lead (seeded
    arpeggio / motif over the chord tones). Times in seconds at 120 bpm; stretch() rescales."""
    import mido
    mf = mido.MidiFile(path); t = 0.0; on = {}; notes = []
    for m in mf:
        t += m.time
        if m.type == "note_on" and m.velocity > 0: on[m.note] = t
        elif m.type in ("note_off", "note_on") and m.note in on: t0 = on.pop(m.note); notes.append((t0, m.note, t - t0))
    if not notes: raise ValueError("empty")
    chords = {}
    for t0, p, l in notes: chords.setdefault(round(t0, 2), []).append((p, l))
    starts = sorted(chords); pad, bass, lead = [], [], []
    loop = max(t0 + max(l for _, l in chords[t0]) for t0 in starts)
    for rep in range(int(secs // loop) + 1):
        off = rep * loop
        for i, t0 in enumerate(starts):
            ps = sorted(p for p, _ in chords[t0]); ln = max(l for _, l in chords[t0]); T = off + t0
            pad += [(T, p, 0.7, ln) for p in ps]; bass.append((T, ps[0], 0.9, ln))
            step = ln / 4.0 if rng.random() < 0.5 else ln / 8.0; n = int(round(ln / step))
            for k in range(n):
                if rng.random() < 0.25: continue
                lead.append((T + k * step, rng.choice(ps) + 12 * rng.choice((0, 1, 1)), rng.uniform(0.55, 0.95), step * 0.9))
    return {"bass": bass, "pad": pad, "lead": lead, "mood": os.path.basename(path)}


def stretch(ev, tempo, base=0.5):
    """Scale time so the file's tempo (120 bpm = 0.5 s a beat) becomes `tempo` bpm."""
    k = (60.0 / tempo) / base
    return [(t * k, p, v, l * k) for t, p, v, l in ev]


MOOD = None   # (V, E), each +1 / -1: valence (major / minor source) and energy (tempo, density, brightness, velocity); None = unasked


def build_parts(rng, secs):
    files = sorted(glob.glob(MIDI_GLOB, recursive=True)); tempo = rng.choice([70, 84, 96, 110])
    if MOOD:
        V, E = MOOD; want = "Major" if V > 0 else "Minor"
        files = [f for f in files if "/%s/" % want in f] or files
        tempo = rng.choice([118, 128, 138]) if E > 0 else rng.choice([58, 66, 74])
    for _ in range(50):
        try: roles = midi_parts(rng.choice(files), secs * 2, rng); break
        except Exception as e: err = e; continue
    else: raise SystemExit("no usable MIDI: %s" % err)
    shift = rng.choice([-5, -2, 0, 3, 5]); parts = []; stretch_ = stretch
    engines = {"bass": ["Analog"], "pad": ["Analog", "Wavetable"], "lead": ["Analog", "FM"]}
    for role in ("bass", "pad", "lead"):
        node = rng.choice(engines[role]); table, sw, fixed = ow.param_table(node)
        rec = ow.recipe(node, sw, fixed, [rng.random() for _ in sw])
        for k, v in {**CLEAN, **ROLE[role]}.items():
            if k in rec: rec[k] = v
        if MOOD and "cutoff" in rec: rec["cutoff"] = rec["cutoff"] * (1.6 if MOOD[1] > 0 else 0.55)
        oct_ = {"bass": -12, "pad": 0, "lead": 0}[role]
        ev = [(t, p + shift + oct_, v, l) for t, p, v, l in stretch(roles[role], tempo) if t < secs]
        if MOOD: ev = [(t, p, min(1.0, v * (1.1 if MOOD[1] > 0 else 0.7)), l) for t, p, v, l in ev]
        g, pn, s = MIX[role]
        parts.append(Part(role, node, rec, ev, g, pn + rng.uniform(-0.1, 0.1), s))
    return parts, tempo, shift, roles["mood"]


def patch_text(parts, chain):
    L = ["infinite-patch 1", "node 1 Notes Keyboard", "  id keys", "end"]; n = 2
    tagc = {}
    for i, p in enumerate(parts):
        tags = tagc.setdefault(p.node, ow.tag_of(ow.param_table(p.node)[0]))
        L += ["node %d Synths %s" % (n, p.node), "  id s%d" % i]
        for k, v in p.recipe.items():
            tg = tags.get(k, "f"); L.append("  %s %s %s" % (tg, k, int(v) if tg in ("i", "b") else repr(float(v))))
        L += ["end"]; n += 1
    # dry mixer: part gain and pan
    L += ["node %d Utility Mixer" % n, "  id dry"]
    for i, p in enumerate(parts):
        L += ["  f gain%d %r" % (i, p.gain), "  f pan%d %r" % (i, p.pan)]
    L += ["end"]; n += 1
    # send mixer: reverb send level per part
    L += ["node %d Utility Mixer" % n, "  id snd"]
    for i, p in enumerate(parts):
        L += ["  f gain%d %r" % (i, 20 * np.log10(max(p.send, 1e-3)) + p.gain)]
    L += ["end"]; n += 1
    L += ["node %d AudioEffects Reverb" % n, "  id rev"] + ["  f %s %r" % kv for kv in chain.reverb.items()] + ["end"]; n += 1
    L += ["node %d Utility Mixer" % n, "  id sum", "end"]; n += 1
    L += ["node %d AudioEffects Limiter" % n, "  id lim"] + ["  f %s %r" % kv for kv in chain.limiter.items()] + ["end"]; n += 1
    L += ["node %d Utility Audio Out" % n, "  id out", "end"]
    for i in range(len(parts)):
        L += ["aud dry in_%d s%d" % (i + 1, i), "aud snd in_%d s%d" % (i + 1, i), "note s%d 0 keys" % i]
    L += ["aud rev audio snd", "aud sum in_1 dry", "aud sum in_2 rev", "aud lim audio sum", "aud out audio lim"]
    return "\n".join(L) + "\n"


def write_events(parts, d):
    evs, types = [], {}
    for i, p in enumerate(parts):
        for pitch in {e[1] for e in p.events}:
            types["s%d_p%d" % (i, pitch)] = {"node": "s%d" % i, "pitch": int(pitch), "velocity_from": "v", "length_from": "len"}
        evs += [{"t": round(t, 4), "type": "s%d_p%d" % (i, pitch), "v": round(v, 4), "len": round(l, 4)} for t, pitch, v, l in p.events]
    evs.sort(key=lambda e: e["t"])
    ep, mp_ = os.path.join(d, "events.json"), os.path.join(d, "map.json")
    json.dump(evs, open(ep, "w")); json.dump(types, open(mp_, "w"))
    return ep, mp_


def render(parts, chain, d, secs):
    os.makedirs(d, exist_ok=True)
    inf, js, wav = (os.path.join(d, n) for n in ("sample.inf", "report_raw.json", "sample.wav"))
    open(inf, "w").write(patch_text(parts, chain)); ep, mp_ = write_events(parts, d)
    r = subprocess.run([ow.binary(), "--audio-summary", inf, js, "--duration", str(secs + 3), "--sample-rate", str(SR), "--wav", wav,
                        "--lenient", "--notes", ep, "--note-map", mp_], capture_output=True, text=True, timeout=300)
    return wav if os.path.exists(wav) and os.path.getsize(wav) > 1000 else None, r.stdout[-400:] + r.stderr[-400:]


def measure(wav):
    import soundfile as sf
    x, sr = sf.read(wav, always_2d=True); m = x.mean(1)
    peak = float(np.abs(x).max()); rms = float(np.sqrt((m ** 2).mean()))
    S = np.abs(np.fft.rfft(m[: sr * 8] * np.hanning(min(len(m), sr * 8)))); f = np.fft.rfftfreq(min(len(m), sr * 8), 1 / sr)
    cen = float((S * f).sum() / max(S.sum(), 1e-9)); sil = float((np.abs(m) < 1e-4).mean())
    side = float(np.sqrt(((x[:, 0] - x[:, -1]) ** 2).mean())) if x.shape[1] > 1 else 0.0
    return {"peak_db": 20 * np.log10(max(peak, 1e-9)), "rms_db": 20 * np.log10(max(rms, 1e-9)), "centroid_hz": cen, "silence_frac": sil, "side_rms": side,
            "ok": bool(peak > 0.02 and sil < 0.5 and peak <= 1.0)}


def make(seed, secs, out):
    rng = random.Random(seed); parts, tempo, shift, mood = build_parts(rng, secs); chain = Chain()
    wav, log = render(parts, chain, out, secs)
    rep = {"seed": seed, "tempo": tempo, "transpose": shift, "source": mood, "parts": [{"role": p.role, "node": p.node, "notes": len(p.events), "gain": p.gain, "pan": round(p.pan, 2), "send": p.send} for p in parts]}
    if wav: rep["measure"] = measure(wav)
    else: rep["error"] = log
    json.dump(rep, open(os.path.join(out, "report.json"), "w"), indent=1)
    return rep


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["make", "batch"]); ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--n", type=int, default=8); ap.add_argument("--secs", type=float, default=16); ap.add_argument("--out", default=os.path.expanduser("~/Infinite/render-runs/compose"))
    a = ap.parse_args()
    seeds = [a.seed] if a.cmd == "make" else range(a.seed, a.seed + a.n)
    for s in seeds:
        d = a.out if a.cmd == "make" else os.path.join(a.out, "s%d" % s)
        r = make(s, a.secs, d); print(s, "OK" if r.get("measure", {}).get("ok") else "FAIL", json.dumps(r.get("measure", r.get("error")))[:200])



def blind(n, seed, secs, site):
    """Whole-sample blind test: n composed vs `site` samples of today's site generator, loudness-matched, shuffled. Key stays in OUT."""
    import listen_c3 as lc
    d = os.path.join(lc.OUT, "compose_blind"); os.makedirs(d, exist_ok=True); clips = []
    for s in range(seed, seed + n):
        out = os.path.join(d, "_s%d" % s); r = make(s, secs, out)
        if r.get("measure", {}).get("ok"): clips.append(("composed", os.path.join(out, "sample.wav")))
    for src in sorted(glob.glob(os.path.expanduser("~/Infinite/patch-universe-library/samples/audio/*/sample.wav")))[:site]: clips.append(("site", src))
    random.Random(5).shuffle(clips); key = {}
    for i, (side, src) in enumerate(clips, 1):
        cid = "clip_%02d" % i
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", src, "-t", "10", "-ac", "2", "-ar", "44100", "-af", "loudnorm=I=-20:TP=-2:LRA=11",
                        os.path.join(d, cid + ".wav")], check=True); key[cid] = {"side": side}
    json.dump(key, open(os.path.join(lc.OUT, "compose_blind_key.json"), "w"))
    open(os.path.join(d, "index.html"), "w").write(lc.PAGE.replace("__N__", str(len(key))))
    print("built %d clips (%d composed, %d site) in %s" % (len(key), sum(1 for c in clips if c[0] == "composed"), sum(1 for c in clips if c[0] == "site"), d))


if __name__ == "__main__" and len(sys.argv) > 1 and sys.argv[1] == "blind":
    ap = argparse.ArgumentParser(); ap.add_argument("cmd"); ap.add_argument("--n", type=int, default=8); ap.add_argument("--seed", type=int, default=200)
    ap.add_argument("--secs", type=float, default=16); ap.add_argument("--site", type=int, default=6); a = ap.parse_args(); blind(a.n, a.seed, a.secs, a.site); sys.exit(0)


if __name__ == "__main__":
    main()
