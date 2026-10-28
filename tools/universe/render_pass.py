#!/usr/bin/env python3
"""Render pass (data-universe step 2d): measure every installed plug-in with Infinite's own headless host.

    render_pass.py [--only SUBSTR] [--formats au,vst3] [--kinds instrument,effect] [--sweep-params]
                   [--budget 20] [--resume] [--timeout 60] [--out ~/Infinite/training-data/audio/render_features.jsonl]
                   [--failures <path>] [--list]

Per plug-in, one small .inf patch is written and rendered with
    Infinite --audio-summary p.inf s.json --wav tmp.wav [--notes e.json --note-map m.json] --plugin-params --lenient
  instrument   Keyboard -> Plugin -> Audio Out; plays STIMULUS (C3 v100, C4 v100, C3 v127), the same note test every preset
               route uses, 3 s apart, 1.5 s held (one render, three rows)
  effect       Audio File (fixed test signal, 3 s each: drum loop | pink-noise burst | sine sweep) -> Plugin -> Audio Out
               (one render, three rows); a `(dry)` row per stimulus is written once so a plug-in's change is a difference
  --sweep-params   after the default render, the first 16 parameters one at a time at 0 / 0.5 / 1 of the plug-in's own
               range (the parameter list comes from the default render's --plugin-params), each its own render
Each render's WAV is cut into its note / stimulus windows, every window is measured with audio_features.analyse (the same
32-band row: bands, peak, rms, centroid, rolloff, flatness, low/mid/high, attack/decay/env), and the WAV is deleted.
Only rows are kept. One JSON row per (plug-in, setting, note): plugin, format, id, vendor, kind, setting, note or
stimulus, velocity, the audio_features fields, and "license". A render that crashes, hangs (killed at --timeout, 60 s) or
fails to load goes to render_failures.jsonl and the plug-in is skipped from then on; --resume skips every done
(plug-in, setting) and every logged failure. --budget minutes stops starting new renders (default 20).
Statistics only: no audio leaves the machine; rows are "plugin render, local only"."""
import argparse, glob, json, math, os, plistlib, re, shutil, signal, struct, subprocess, sys, tempfile, time, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "tools", "re-survey"))
sys.path.insert(0, os.path.join(HERE, "re-survey"))  # the Air's flat ~/infinite-harvest layout
import audio_features as af  # noqa: E402

SR = 48000
LEAD = 0.5            # silence before the first window: the plug-in finishes loading
WIN = 3.0             # seconds per note / stimulus window
HOLD = 1.5            # note length
STIMULUS = "48:100:C3,60:100:C4,48:127:C3f"  # midi:velocity:label, the one note test of every route (SOUND-MODEL.md 0.4)
NOTES = [(int(m), int(v), l) for m, v, l in (x.split(":") for x in STIMULUS.split(","))]
SCHEMA = 2            # row v2: schema, route, preset_id, midi, velocity, knobs (SOUND-MODEL.md 0.3)
PARAMS_DIR = "~/Infinite/training-data/params"
MIDI_PROXY = re.compile(r"^CC \d+, CH \d+$")  # VST3 MIDI-CC proxy slots (2,080 of Diva's 2,362): not sound knobs
STIMS = ["drum_loop", "pink_burst", "sine_sweep"]
SWEEP_N, SWEEP_FRACS = 16, (0.0, 0.5, 1.0)
LICENSE = "plugin render, local only"
OUT = "~/Infinite/training-data/audio/render_features.jsonl"
AU_DIRS = ["/Library/Audio/Plug-Ins/Components", os.path.expanduser("~/Library/Audio/Plug-Ins/Components")]
VST3_DIRS = ["/Library/Audio/Plug-Ins/VST3", os.path.expanduser("~/Library/Audio/Plug-Ins/VST3")]


# ------------------------------------------------------------------ binary and plug-in list

def infinite_bin():
    cands = [os.environ.get("INFINITE_BIN", ""), "/Applications/Infinite.app/Contents/MacOS/Infinite",
             os.path.join(os.path.dirname(os.path.dirname(HERE)), "build/Infinite.app/Contents/MacOS/Infinite"),
             os.path.expanduser("~/Infinite/repo/build/Infinite.app/Contents/MacOS/Infinite")]
    for c in cands:
        if c and os.access(c, os.X_OK):
            return c
    return None


def list_aus():
    out = []
    for r in AU_DIRS:
        for comp in sorted(glob.glob(os.path.join(r, "*.component"))):
            try:
                with open(os.path.join(comp, "Contents", "Info.plist"), "rb") as f:
                    d = plistlib.load(f)
            except Exception:
                continue
            for c in d.get("AudioComponents", []) or []:
                typ = c.get("type")
                if typ not in ("aufx", "aumf", "aumu"):  # aumi (MIDI effects) make no sound
                    continue
                full = c.get("name", "")
                out.append({"format": "au", "id": "au:%s:%s:%s" % (typ, c.get("subtype"), c.get("manufacturer")),
                            "name": full.split(":", 1)[-1].strip(), "vendor": full.split(":", 1)[0].strip() if ":" in full else "",
                            "instrument": typ == "aumu", "bundle": comp})
    return out


def vst3_bundles():
    out = []
    for r in VST3_DIRS:
        if os.path.isdir(r):
            out += [os.path.join(r, b) for b in sorted(os.listdir(r)) if b.endswith(".vst3")]
    return out


def scan_bundle(binp, bundle, cache, timeout=60):
    """VST3 classes via Infinite's out-of-process scanner; cached because a scan costs about a second."""
    if bundle in cache:
        return cache[bundle]
    try:
        r = subprocess.run([binp, "--vst3-scan-bundle", bundle], capture_output=True, text=True, timeout=timeout)
        rows = [l.split("\t") for l in r.stdout.splitlines() if l.startswith("vst3\t")]
        cls = [{"id": x[3], "name": x[1], "vendor": x[2], "instrument": x[5] == "1"} for x in rows if len(x) == 6]
        res = {"classes": cls, "scan": "crash" if r.returncode < 0 else ("ok" if cls else "no-classes")}
    except subprocess.TimeoutExpired:
        res = {"classes": [], "scan": "hang"}
    cache[bundle] = res
    return res


# ------------------------------------------------------------------ stimulus and notes

def stimulus():
    """9 s mono test signal: drum loop (120 bpm), pink-noise burst, log sine sweep; seeded, so identical on every machine."""
    rng = np.random.RandomState(7)
    n = int(WIN * SR)
    t = np.arange(n) / SR
    drum = np.zeros(n)
    for beat in range(6):
        i = int(beat * 0.5 * SR)
        m = min(int(0.3 * SR), n - i)
        tt = np.arange(m) / SR
        if beat % 2 == 0:  # kick: falling sine
            drum[i:i + m] += 0.8 * np.sin(2 * np.pi * (45 * tt + 90 * (1 - np.exp(-tt * 30)) / 30)) * np.exp(-tt * 14)
        else:              # snare: noise + 200 Hz body
            drum[i:i + m] += 0.45 * rng.randn(m) * np.exp(-tt * 22) + 0.3 * np.sin(2 * np.pi * 200 * tt) * np.exp(-tt * 25)
    for h in range(12):    # closed hats on the 8ths
        i = int(h * 0.25 * SR)
        m = min(int(0.05 * SR), n - i)
        tt = np.arange(m) / SR
        drum[i:i + m] += 0.12 * np.diff(rng.randn(m + 1)) * np.exp(-tt * 70)
    pk = np.abs(drum).max()
    drum *= 0.7 / pk if pk > 0 else 1
    # pink noise by 1/f shaping in the frequency domain, 0.6 s burst with 10 ms fades, then silence
    m = int(0.6 * SR)
    F = np.fft.rfft(rng.randn(m))
    F[1:] /= np.sqrt(np.arange(1, len(F)))
    pink = np.fft.irfft(F, m)
    pink *= 10 ** (-20 / 20) / np.sqrt((pink ** 2).mean())
    fd = int(0.01 * SR)
    pink[:fd] *= np.linspace(0, 1, fd)
    pink[-fd:] *= np.linspace(1, 0, fd)
    burst = np.zeros(n)
    burst[int(0.2 * SR):int(0.2 * SR) + m] = pink
    # sine sweep 50 Hz -> 15 kHz over 2.5 s, -12 dBFS
    ts = np.arange(int(2.5 * SR)) / SR
    k = math.log(15000 / 50) / 2.5
    sweep = np.zeros(n)
    sw = 10 ** (-12 / 20) * np.sin(2 * math.pi * 50 * (np.exp(k * ts) - 1) / k)
    sw[:fd] *= np.linspace(0, 1, fd)
    sw[-fd:] *= np.linspace(1, 0, fd)
    sweep[:len(sw)] = sw
    lead = np.zeros(int(LEAD * SR))
    return np.concatenate([lead, drum, burst, sweep])


def write_wav24(path, mono):
    v = np.clip(np.round(mono * 8388607.0), -8388608, 8388607).astype("<i4")
    b = np.repeat(v, 2).tobytes()
    raw = b"".join(b[i:i + 3] for i in range(0, len(b), 4))
    w = wave.open(path, "wb")
    w.setnchannels(2)
    w.setsampwidth(3)
    w.setframerate(SR)
    w.writeframes(raw)
    w.close()


def write_notes(d):
    types = {"p%d" % m: {"node": "fx", "pitch": m, "velocity_from": "v", "length_from": "len"} for m, _, _ in NOTES}
    evs = [{"t": LEAD + i * WIN, "type": "p%d" % m, "v": round(v / 127.0, 5), "len": HOLD} for i, (m, v, _) in enumerate(NOTES)]
    ep, mp = os.path.join(d, "events.json"), os.path.join(d, "map.json")
    json.dump(evs, open(ep, "w"))
    json.dump(types, open(mp, "w"))
    return ep, mp


# ------------------------------------------------------------------ patch and render

def patch_text(t, wav, slots):
    """slots: [(address, name, lo, hi, value)] written as the Plugin node's mapped-parameter values."""
    L = ["infinite-patch 1"]
    if t["instrument"]:
        L += ["node 1 Notes Keyboard", "  id keys", "end"]
    else:
        L += ["node 1 Modulators Audio File", "  id src", "  s path " + wav, "  b loop 0", "  f volume 1", "  f gain 1",
              "  f attack 0", "  f release 0", "end"]
    L += ["node 2 AudioEffects Plugin", "  id fx", "  s plugin_format " + t["format"], "  s plugin_id " + t["id"],
          "  s plugin_name " + t["name"]]
    if t["instrument"]:
        L.append("  b plugin_accepts_notes 1")
    if slots:
        L.append("  i map_slots %d" % len(slots))
    for i, (addr, name, lo, hi, v) in enumerate(slots):
        L += ["  b map%d_assigned 1" % i, "  s map%d_addr %s" % (i, addr), "  s map%d_name %s" % (i, name),
              "  f map%d_min %r" % (i, lo), "  f map%d_max %r" % (i, hi), "  f map%d_value %r" % (i, v)]
    L += ["end", "node 3 Utility Audio Out", "  id out", "end", "aud out audio fx"]
    L.append("note fx 1 keys" if t["instrument"] else "aud fx 0 src")
    return "\n".join(L) + "\n"


def run_render(binp, t, tmp, slots, timeout, stim_wav):
    """One headless render. Returns (state, info): state ok | crash | hang | fail | not-ready | silent-load."""
    inf, js, wav = (os.path.join(tmp, n) for n in ("p.inf", "s.json", "out.wav"))
    open(inf, "w").write(patch_text(t, stim_wav, slots))
    dur = LEAD + WIN * 3 + (0.0 if t["instrument"] else 0.0)
    cmd = [binp, "--audio-summary", inf, js, "--duration", "%g" % dur, "--sample-rate", str(SR), "--wav", wav,
           "--plugin-params", "--lenient", "--timeout", str(max(5, int(timeout) - 5))]
    if t["instrument"]:
        ep, mp = write_notes(tmp)
        cmd += ["--notes", ep, "--note-map", mp]
    if os.path.exists(wav):
        os.remove(wav)
    t0 = time.time()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
    try:
        out, err = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except OSError:
            p.kill()
        p.communicate()
        return "hang", {"wall_s": round(time.time() - t0, 1), "why": "killed after %d s" % timeout}
    info = {"wall_s": round(time.time() - t0, 1), "rc": p.returncode}
    if p.returncode < 0 or p.returncode > 128:
        sig = -p.returncode if p.returncode < 0 else p.returncode - 128
        info.update(signal=sig, why="killed by signal %d" % sig)
        return "crash", info
    st = {}
    for line in reversed(out.strip().splitlines()):
        try:
            st = json.loads(re.sub(r":(-?)(inf|nan)\b", ":null", line))
            break
        except ValueError:
            continue
    if p.returncode != 0 or not st.get("ok"):
        errs = st.get("errors") or [{"message": (err.strip().splitlines() or ["rc %d" % p.returncode])[-1]}]
        info["why"] = str(errs[0].get("message"))[:200]
        return "fail", info
    plug = (st.get("plugins") or [{}])[0]
    info["params"] = plug.get("params") or []
    info["param_count"] = plug.get("param_count")
    if not plug.get("ready"):
        warn = [w.get("message", "") for w in st.get("warnings") or [] if w.get("code") == "W_PLUGIN_NOT_LOADED"]
        info["why"] = "plug-in not ready: " + (warn[0] if warn else str(plug.get("error", "")))[:200]
        return "not-ready", info
    if t["instrument"] and (st.get("notes", {}).get("injected") != len(NOTES)):
        info["why"] = "only %s of %d notes injected" % (st.get("notes", {}).get("injected"), len(NOTES))
        return "fail", info
    if not os.path.exists(wav):
        info["why"] = "no wav written"
        return "fail", info
    info["wav"] = wav
    return "ok", info


# ------------------------------------------------------------------ measuring

def status_line(out):
    """The headless status JSON: the last stdout line that parses."""
    for line in reversed(out.strip().splitlines()):
        try:
            return json.loads(re.sub(r":(-?)(inf|nan)\b", ":null", line))
        except ValueError:
            continue
    return {}


def render_state(binp, t, state_b64, tmp, timeout=120):
    """Render one saved plug-in state (a preset spliced into the plug-in's own state) over NOTES. Every preset route calls this.
    Returns (windows, params) or (None, why): windows from measure_windows, params the host's list with current values."""
    inf, js, wav = (os.path.join(tmp, n) for n in ("p.inf", "s.json", "o.wav"))
    open(inf, "w").write(patch_text(t, "", []).replace("  s plugin_name", "  s plugin_state %s\n  s plugin_name" % state_b64, 1))
    ep, mp = write_notes(tmp)
    try:
        p = subprocess.run([binp, "--audio-summary", inf, js, "--duration", "%g" % (LEAD + WIN * len(NOTES)), "--sample-rate", str(SR),
                            "--wav", wav, "--lenient", "--plugin-params", "--notes", ep, "--note-map", mp],
                           capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "timeout"
    plug = (status_line(p.stdout).get("plugins") or [{}])[0]
    if p.returncode != 0 or not os.path.exists(wav):
        return None, "rc %d: %s" % (p.returncode, (p.stderr.strip().splitlines() or [""])[-1][:100])
    if not plug.get("ready"):
        return None, "plug-in not ready"
    win = measure_windows(wav, tmp, [x for _, _, x in NOTES])
    params = [q for q in plug.get("params") or [] if not MIDI_PROXY.match(q.get("name", ""))]
    return (win, params) if win else (None, "unreadable wav")


def param_table(plugin_id, params):
    """Write the plug-in's parameter table once (address, name, unit, range, default) and return its hash. knobs[] in every row
    is aligned to this table, so the hash says which table a row's knob list belongs to (a plug-in update changes it)."""
    import hashlib
    tab = [{k: p.get(k) for k in ("address", "name", "unit", "min", "max", "default")} for p in params]
    h = hashlib.sha1(json.dumps(tab, sort_keys=True).encode()).hexdigest()[:12]
    d = os.path.expanduser(PARAMS_DIR); os.makedirs(d, exist_ok=True)
    path = os.path.join(d, "%s_%s.json" % (re.sub(r"\W+", "_", plugin_id), h))
    if not os.path.exists(path):
        json.dump({"plugin_id": plugin_id, "hash": h, "params": tab}, open(path, "w"), indent=0)
    return h


def knob_vector(params):
    """Current value of each parameter normalised to 0-1 of its own range, 4 decimals; None where the host gave no value."""
    out = []
    for p in params:
        v, lo, hi = p.get("value"), p.get("min"), p.get("max")
        out.append(None if not finite(v) or not finite(lo) or not finite(hi) else round((v - lo) / (hi - lo), 4) if hi > lo else 0.0)
    return out


def state_rows(t, route, src, label, preset_bytes, windows, params, tags):
    """Row v2 for one rendered preset: one row per note window. Knobs ride on the first row only (the others share preset_id)."""
    import hashlib
    pid = hashlib.sha1(preset_bytes).hexdigest()[:16]
    ph = param_table(t["id"], params) if params else None
    rows = []
    for i, ((midi, vel, lab), (_, feat)) in enumerate(zip(NOTES, windows)):
        row = {"schema": SCHEMA, "route": route, "src": src, "preset_id": pid, "label": label, "plugin": t["name"], "plugin_id": t["id"],
               "note": lab, "midi": midi, "velocity": vel, "tags": tags or {}, "license": LICENSE}
        if i == 0 and params:
            row["params_hash"] = ph
            row["knobs"] = knob_vector(params)
        row.update({k: v for k, v in (feat or {}).items() if k not in ("src", "pack", "part", "bpm", "key", "loop", "license")})
        rows.append(row)
    return rows


def finite(v):
    return isinstance(v, (int, float)) and math.isfinite(v)


MEL_BINS, MEL_FRAMES = 64, 48   # log-mel over one window: 64 bands 20 Hz - 16 kHz, 48 frames (62.5 ms each over 3 s)
_melfb = {}


def mel_fb(rate, nfft):
    """Slaney-style triangular mel filterbank (HTK mel formula), MEL_BINS x (nfft/2+1)."""
    k = (rate, nfft)
    if k not in _melfb:
        hz = lambda m: 700 * (10 ** (m / 2595) - 1)
        mel = lambda f: 2595 * math.log10(1 + f / 700)
        edges = np.array([hz(m) for m in np.linspace(mel(20), mel(min(16000, rate / 2)), MEL_BINS + 2)])
        f = np.fft.rfftfreq(nfft, 1 / rate)
        lo, mid, hi = edges[:-2, None], edges[1:-1, None], edges[2:, None]
        fb = np.maximum(0, np.minimum((f - lo) / (mid - lo), (hi - f) / (hi - mid)))
        _melfb[k] = fb * (2 / (hi - lo))
    return _melfb[k]


def logmel(seg, rate):
    """Log-mel spectrogram of a window (channels x samples), stored compact: base64 of uint8, 0.5 dB steps, 0 = -120 dB
    (relative level: the filterbank is area-normalised, so compare rows with rows, not with dBFS meters).
    Decode: np.frombuffer(base64.b64decode(s), np.uint8).reshape(MEL_BINS, MEL_FRAMES) / 2 - 120. The target the
    sound models learn (industry practice: mel frames over time, not one averaged spectrum)."""
    import base64
    x = seg.mean(0); nfft, hop = 2048, 512
    if len(x) < nfft:
        return None
    n = 1 + (len(x) - nfft) // hop
    fr = np.lib.stride_tricks.as_strided(x, (n, nfft), (x.strides[0] * hop, x.strides[0])) * np.hanning(nfft)
    p = (np.abs(np.fft.rfft(fr, axis=1)) ** 2) / (np.hanning(nfft).sum() ** 2 / 4)
    m = mel_fb(rate, nfft) @ p.T
    m = np.stack([c.mean(1) for c in np.array_split(m, MEL_FRAMES, axis=1)], 1)
    q = np.clip(np.round((10 * np.log10(np.maximum(m, 1e-12)) + 120) * 2), 0, 255).astype(np.uint8)
    return base64.b64encode(q.tobytes()).decode()


def measure_windows(wav, tmp, labels):
    """Cut the render into its 3 windows and measure each with audio_features.analyse. [(label, row or None)]"""
    r = af.read_pcm(wav, 60)
    if r is None:
        return None
    x, rate = r
    if x.shape[0] == 1:
        x = np.vstack([x[0], x[0]])
    rows = []
    for i, lab in enumerate(labels):
        a = int(round((LEAD + i * WIN) * rate))
        seg = x[:2, a:a + int(round(WIN * rate))]
        if seg.shape[1] < 2048:
            rows.append((lab, None))
            continue
        peak = float(np.abs(seg).max())
        if peak < 1e-5 or not np.isfinite(seg).all():
            rows.append((lab, {"silent": peak < 1e-5, "nonfinite": bool(not np.isfinite(seg).all()), "peak": round(peak, 6) if finite(peak) else None}))
            continue
        sp = os.path.join(tmp, "seg_%d.wav" % i)  # neutral name: no part / bpm / loop words for the tagger to pick up
        w = wave.open(sp, "wb")
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(int(rate))
        w.writeframes((np.clip(seg.T, -1, 1) * 32767).astype("<i2").tobytes())
        w.close()
        try:
            row = af.analyse(sp, tmp, WIN + 1)
        except Exception as e:
            row = {"error": str(e)[:120]}
        if "error" not in row:
            row["mel"] = logmel(seg, rate)
        os.remove(sp)
        rows.append((lab, row))
    return rows


def make_row(t, setting, lab, feat, extra):
    row = {"src": "plugin:%s:%s" % (t["id"], setting["key"]), "plugin": t["name"], "vendor": t["vendor"], "format": t["format"],
           "plugin_id": t["id"], "kind": "instrument" if t["instrument"] else "effect", "setting": setting["key"], "schema": SCHEMA, "route": 1}
    if setting.get("param"):
        row["param"] = setting["param"]
        row["frac"] = setting["frac"]
        row["value"] = setting["value"]
    row.update(extra)
    if feat:
        for k in ("src", "pack", "part", "bpm", "key", "loop"):
            feat.pop(k, None)
        row.update(feat)
    row["license"] = LICENSE
    return row


# ------------------------------------------------------------------ run

def sweep_settings(params):
    out = []
    for p in params[:SWEEP_N]:
        lo, hi, df = p.get("min"), p.get("max"), p.get("default")
        if not (finite(lo) and finite(hi)) or hi == lo:
            continue
        for fr in SWEEP_FRACS:
            v = lo + fr * (hi - lo)
            if finite(df) and abs(v - df) <= 1e-6 * max(1.0, abs(hi - lo)):
                continue  # the default render already is this setting
            out.append({"key": "%s=%g" % (p["name"][:40], fr), "param": p["name"], "frac": fr, "value": v,
                        "slot": (p["address"], p["name"], float(lo), float(hi), float(v))})
    return out


def read_jsonl(path):
    if os.path.exists(path):
        for line in open(path):
            try:
                yield json.loads(line)
            except ValueError:
                continue


def work_items(binp, args, cache):
    """Yield plug-in dicts lazily (VST3 bundles are scanned when their turn comes)."""
    only = (args.only or "").lower()
    kinds = set(args.kinds.split(","))
    fmts = set(args.formats.split(","))
    retry_ids, retry_names = set(), set()
    if args.retry_from:
        for r in read_jsonl(os.path.expanduser(args.retry_from)):
            if r.get("setting") == "default":
                retry_ids.add(r.get("plugin_id"))
                retry_names.add((r.get("plugin") or "").lower())
    def keep(t):
        if args.retry_from and t["id"] not in retry_ids and t["name"].lower() not in retry_names:
            return False
        return ("instrument" if t["instrument"] else "effect") in kinds and (not only or only in (t["name"] + " " + t["vendor"] + " " + t["bundle"]).lower())
    if "au" in fmts:
        for t in list_aus():
            if keep(t):
                yield t
    if "vst3" in fmts:
        for b in vst3_bundles():
            if only and only not in b.lower():
                continue
            for c in scan_bundle(binp, b, cache)["classes"]:
                t = {"format": "vst3", "id": c["id"], "name": c["name"], "vendor": c["vendor"], "instrument": c["instrument"], "bundle": b}
                if keep(t):
                    yield t


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--failures", default=None, help="default: render_failures.jsonl next to --out")
    ap.add_argument("--only", help="plug-in name / vendor / bundle substring")
    ap.add_argument("--formats", default="au,vst3")
    ap.add_argument("--kinds", default="instrument,effect")
    ap.add_argument("--sweep-params", action="store_true")
    ap.add_argument("--budget", type=float, default=20, help="minutes; no new render starts after it")
    ap.add_argument("--timeout", type=int, default=60, help="seconds per render")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--retry-from", help="a render_failures.jsonl from another machine: render only the plug-ins that failed to load or hung "
                    "at their default setting there (typically a library that lives on a drive attached to this machine)")
    ap.add_argument("--list", action="store_true", help="print the plug-ins and exit")
    ap.add_argument("--scratch", default=None)
    args = ap.parse_args()
    out = os.path.expanduser(args.out)
    fails = os.path.expanduser(args.failures) if args.failures else os.path.join(os.path.dirname(out), "render_failures.jsonl")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    binp = infinite_bin()
    if not binp:
        print("no Infinite binary (set INFINITE_BIN)", file=sys.stderr)
        return 2
    cache_path = os.path.join(os.path.dirname(out), ".render_vst3_scan.json")
    try:
        cache = json.load(open(cache_path))
    except (OSError, ValueError):
        cache = {}
    if args.list:
        for t in work_items(binp, args, cache):
            print("%-5s %-10s %-34s %s" % (t["format"], "instrument" if t["instrument"] else "effect", t["name"][:34], t["id"]))
        json.dump(cache, open(cache_path, "w"))
        return 0
    prior = list(read_jsonl(out))
    done = {(r["plugin_id"], r.get("setting")) for r in prior if r.get("plugin_id")} if args.resume else set()
    bad = {r.get("plugin_id") for r in read_jsonl(fails)} if args.resume else set()
    have_dry = any(r.get("plugin_id") == "dry" for r in prior)
    tmp = tempfile.mkdtemp(prefix="render_pass_", dir=args.scratch)
    stim_wav = os.path.join(tmp, "stim.wav")
    write_wav24(stim_wav, stimulus())
    t_end = time.time() + args.budget * 60
    n_rows = n_ok = n_fail = n_skip = 0
    fo = open(out, "a")
    ff = open(fails, "a")
    stopped = False

    def emit(r):
        nonlocal n_rows
        fo.write(json.dumps(r, separators=(",", ":")) + "\n")
        fo.flush()
        n_rows += 1

    def fail(t, setting, state, info):
        nonlocal n_fail
        n_fail += 1
        rec = {"plugin_id": t["id"], "plugin": t["name"], "vendor": t["vendor"], "format": t["format"], "bundle": t["bundle"],
               "kind": "instrument" if t["instrument"] else "effect", "setting": setting["key"], "state": state,
               "why": info.get("why"), "rc": info.get("rc"), "signal": info.get("signal"), "wall_s": info.get("wall_s"),
               "date": time.strftime("%Y-%m-%d %H:%M")}
        ff.write(json.dumps({k: v for k, v in rec.items() if v is not None}) + "\n")
        ff.flush()
        print("  FAIL %s [%s]: %s %s" % (t["name"], setting["key"], state, info.get("why") or ""), flush=True)

    def one(t, setting):
        """Render one setting; returns (state, info) and writes its rows."""
        nonlocal n_ok
        slots = [setting["slot"]] if setting.get("slot") else []
        state, info = run_render(binp, t, tmp, slots, args.timeout, stim_wav)
        if state != "ok":
            return state, info
        labels = [n for _, _, n in NOTES] if t["instrument"] else STIMS
        meas = measure_windows(info["wav"], tmp, labels)
        os.remove(info["wav"])
        if meas is None:
            return "fail", dict(info, why="could not read the render wav")
        for i, (lab, feat) in enumerate(meas):
            extra = {"note": lab, "midi": NOTES[i][0], "velocity": NOTES[i][1]} if t["instrument"] else {"stimulus": lab}
            emit(make_row(t, setting, lab, feat, extra))
        n_ok += 1
        return "ok", info

    try:
        # the rig's own floor: the stimulus through Audio File -> Audio Out, once per output file
        if "effect" in args.kinds and not have_dry:
            dry = {"format": "none", "id": "dry", "name": "(dry)", "vendor": "", "instrument": False, "bundle": ""}
            inf = os.path.join(tmp, "dry.inf")
            open(inf, "w").write("infinite-patch 1\nnode 1 Modulators Audio File\n  id src\n  s path %s\n  b loop 0\n  f volume 1\n  f gain 1\n"
                                 "  f attack 0\n  f release 0\nend\nnode 2 Utility Audio Out\n  id out\nend\naud out audio src\n" % stim_wav)
            w = os.path.join(tmp, "dry.wav")
            r = subprocess.run([binp, "--audio-summary", inf, os.path.join(tmp, "d.json"), "--duration", "%g" % (LEAD + 9), "--sample-rate", str(SR),
                                "--wav", w, "--lenient"], capture_output=True, text=True, timeout=args.timeout)
            if r.returncode == 0 and os.path.exists(w):
                for lab, feat in measure_windows(w, tmp, STIMS) or []:
                    emit(make_row(dry, {"key": "default"}, lab, feat, {"stimulus": lab}))
                os.remove(w)
        for t in work_items(binp, args, cache):
            if time.time() >= t_end:
                stopped = True
                break
            if t["id"] in bad:
                n_skip += 1
                continue
            t_start = time.time()
            default = {"key": "default"}
            params = []
            if (t["id"], "default") in done:
                # the parameter list is only in a fresh render; a sweep resumed after a done default needs one short probe
                if args.sweep_params:
                    state, info = run_render(binp, t, tmp, [], args.timeout, stim_wav)
                    if state == "ok":
                        params = info["params"]
                        if info.get("wav") and os.path.exists(info["wav"]):
                            os.remove(info["wav"])
                    else:
                        fail(t, default, state, info)
                        bad.add(t["id"])
                        continue
                else:
                    n_skip += 1
                    continue
            else:
                state, info = one(t, default)
                if state != "ok":
                    fail(t, default, state, info)
                    bad.add(t["id"])
                    continue
                params = info["params"]
            print("%-5s %-10s %-34s ok %4.1f s  %d params" % (t["format"], "instrument" if t["instrument"] else "effect", t["name"][:34],
                                                          time.time() - t_start, len(params)), flush=True)
            if args.sweep_params:
                for s in sweep_settings(params):
                    if time.time() >= t_end:
                        stopped = True
                        break
                    if (t["id"], s["key"]) in done:
                        continue
                    state, info = one(t, s)
                    if state != "ok":
                        fail(t, s, state, info)
                        if state in ("crash", "hang"):
                            bad.add(t["id"])  # a plug-in that dies on a setting is logged once and left alone
                            break
                if stopped:
                    break
    finally:
        fo.close()
        ff.close()
        json.dump(cache, open(cache_path, "w"))
        shutil.rmtree(tmp, ignore_errors=True)
    print("render pass: %d renders ok, %d failed, %d skipped, %d rows -> %s%s" %
          (n_ok, n_fail, n_skip, n_rows, out, "  (budget reached; --resume continues)" if stopped else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
