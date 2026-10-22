#!/usr/bin/env python3
"""Per-node parameter fuzz, headless, under a sanitizer build.

A patch file is outside input: Patch's reader loads `i`/`f`/`s` params with a
bare atoi/atof/copy and modulation writes through every registered control. For
each node type this sets every described param of one kind to an extreme (or
binds every modulatable control to a fast LFO) and runs the node with all its
inputs fed and every output reaching a sink, so it actually cooks.

A "base" run with defaults goes first. An extreme run is BAD on a crash, signal,
hang, sanitizer report, NaN or >+12 dBFS audio the base run did not have, or an
error code the base run did not have.

  param_fuzz.py BIN OUTDIR [--kind int|float|text|mod|signal|all] [--only TYPE]... [--jobs N]
                           [--max-timeout S] [--types-from results.tsv]

  --kind mod     one run per type: every modulatable control on a 32-beat LFO,
                 unipolar and bipolar, full depth (catches controls registered
                 against dead storage, NaN through modulation, out-of-range writes)
  --kind signal  every node with an audio input, fed by an Audio File node
                 playing 0.25 s of an extreme (nan, inf, denormals, +-1e6, DC,
                 a full-scale 30 Hz square) then a clean -12 dBFS sine; the base
                 run plays the sine alone. It looks for state that never
                 recovers: NANSTUCK (NaN long after the burst), HOTSTUCK (last
                 second 20 dB over base and not falling 3 dB/s), SILENT (last second silent where base
                 had sound), SLOW (4x base time). NANPASS (NaN only while the
                 burst plays) is recorded but is not BAD.
  --jobs N       run N cases at once (default 1). Each case is its own process;
                 keep 1 when the machine is also timing something.
  --max-timeout  per-case cap in seconds (default 90). Each type's budget is
                 5x its own base run, at least 20 s, at most this cap.
  --types-from   re-run only the types that have a BAD row in an earlier
                 results file (the verify step after a fix).

Results: OUTDIR/results-<kind>.tsv (type, value, rc, codes, reports, secs,
verdict); resumable, a re-run skips rows already written. Every BAD case keeps
its .inf and sanitizer log next to it; tools/bughunt/triage.py groups them.
Test: `param_fuzz.py selftest`.
"""
import concurrent.futures as cf
import json
import os
import re
import subprocess
import sys
import threading
import time

VALUES = {
    "int": ["2147483647", "-2147483648", "100000", "-100000"],
    "float": ["1e30", "-1e30", "nan", "inf"],
    # label=value; values are already patch-escaped (one line, \\n for newlines)
    "text": ["big=" + "A" * 1000000, "parens=" + "(" * 50000, "brackets=" + "[" * 50000,
             "fmt=" + "%s%n%x%999999d" * 8, "path=../../../../../../../../etc/passwd",
             "lines=" + "\\n" * 100000, "ctrl=\x01\x7fÿ‮�{}$(){{}}"],
    "mod": ["uni=0", "bi=1"],
    "signal": ["nan", "inf", "denorm", "huge", "dc", "square"],
}
PREFIX = {"int": "i", "float": "f", "text": "s"}
HOT_PEAK = 4.0  # +12 dBFS
# --start seeks the transport and Audio File follows it, so the whole file
# renders from 0 and the verdict reads the last whole second's loudness.
SIG_RATE, SIG_SECS, SIG_BURST = 48000, 3.0, 0.25
SIG_GRACE = 0.5  # NaN frames past burst + this are state that never recovered
INFO_CODES = {"", "NANPASS", "NOSIGNAL"}  # recorded, never BAD on their own


def signal_wav(out, name):
    """Stereo float32 WAV: SIG_BURST s of the named extreme, then a -12 dBFS sine."""
    import math
    import struct
    path = os.path.join(out, "signals", name + ".wav")
    if os.path.exists(path):
        return path
    os.makedirs(os.path.dirname(path), exist_ok=True)
    burst = {"nan": lambda i: float("nan"), "inf": lambda i: float("inf") if i & 1 else float("-inf"),
             "denorm": lambda i: 1e-40 if i & 1 else -1e-40, "huge": lambda i: 1e6 if (i // 400) & 1 else -1e6,
             "dc": lambda i: 1.0, "square": lambda i: 1.0 if (i // 800) & 1 else -1.0}.get(name)
    n = int(SIG_RATE * SIG_SECS)
    nb = int(SIG_RATE * SIG_BURST) if burst else 0
    frames = []
    for i in range(n):
        v = burst(i) if i < nb else 0.25 * math.sin(2 * math.pi * 440 * i / SIG_RATE)
        frames.append(struct.pack("<ff", v, v))
    data = b"".join(frames)
    fmt = struct.pack("<HHIIHH", 3, 2, SIG_RATE, SIG_RATE * 8, 8, 32)  # IEEE float
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<I", len(fmt)) + fmt)
        f.write(b"data" + struct.pack("<I", len(data)) + data)
    return path

BLOCKS = {
    "fnoise": "node 101 Source Noise\n  id fnoise\nend",
    "fseq": "node 102 Notes Note Sequencer\n  id fseq\nend",
    "fsyn": "node 103 Synths Analog\n  id fsyn\nend",
    "fgeo": "node 104 3D Curve\n  id fgeo\nend",
    "out": "node 105 Utility Output\n  id out\nend",
    "spk": "node 106 Utility Audio Out\n  id spk\nend",
    "rend": "node 107 3D Render 3D\n  id rend\nend",
    "nsink": "node 108 Synths Analog\n  id nsink\nend",
    "lfo": "node 109 Modulators LFO\n  id lfo\n  f rateBeats 32\nend",
}


def label_of(kind, v):
    return v.split("=", 1)[0] if kind in ("text", "mod") else v


def has_params(t, kind):
    if kind == "signal":
        return any(i.get("kind") == "audio" for i in t.get("inputs") or [])
    if kind == "mod":
        return bool(t.get("modulatable"))
    return any(p.get("kind") == kind for p in t["params"])


def patch_for(t, kind, value, sig_path=None):
    """(patch text, is_audio_only). value None = the base run. kind signal
    feeds audio inputs from sig_path instead of the synth."""
    lines = ["infinite-patch 1", f"node 1 {t['category']} {t['type']}", "  id a"]
    if value is not None and kind not in ("mod", "signal"):
        v = value.split("=", 1)[1] if kind == "text" else value
        for p in t["params"]:
            if p.get("kind") == kind:
                lines.append(f"  {PREFIX[kind]} {p['key']} {v}")
    lines.append("end")
    used, wires = [], []

    def use(*names):
        for n in names:
            if n not in used:
                used.append(n)

    for i in t.get("inputs") or []:
        k, s = i["kind"], i["slot"]
        if k == "image":
            use("fnoise"); wires.append(f"cable a {s} fnoise 0")
        elif k == "audio" and kind == "signal":
            use("fsig"); wires.append(f"aud a {s} fsig")
        elif k == "audio":
            use("fseq", "fsyn"); wires.append(f"aud a {s} fsyn")
        elif k == "note":
            use("fseq"); wires.append(f"note a {s} fseq 0")
        elif k == "geometry":
            use("fgeo"); wires.append(f"geo a {s} fgeo 0")
    if "fsyn" in used:
        wires.append("note fsyn 0 fseq 0")
    outs = {s.get("kind") for s in t.get("outputs") or []}
    if "image" in outs:
        use("out"); wires.append("cable out 0 a 0")
    if "audio" in outs:
        use("spk"); wires.append("aud spk 0 a")
    if "geometry" in outs and "image" not in outs:
        use("rend", "out"); wires += ["geo rend 0 a 0", "cable out 0 rend 0"]
    if "note" in outs and "audio" not in outs:
        use("nsink", "spk"); wires += ["note nsink 0 a 0", "aud spk 0 nsink"]
    if value is not None and kind == "mod":
        use("lfo")
        pol = value.split("=", 1)[1]
        for m in t.get("modulatable") or []:
            if m.get("key"):
                wires.append(f"mod a {m['key']} lfo 0 {pol} 1 0.5")
    blocks = dict(BLOCKS)
    if sig_path:
        blocks["fsig"] = (f"node 110 Modulators Audio File\n  id fsig\n  s path {sig_path}\n"
                          "  b loop 0\n  f volume 1\nend")  # followTransport 0 waits for a manual play
    lines += [blocks[n] for n in used] + wires
    # No image sink: measure audio. Sinks with no outputs (Audio Out, MIDI Out,
    # OSC Send, Comment) would only ever fail --frame with E_RENDER.
    audio = "out" not in used
    return "\n".join(lines) + "\n", audio


def bad_num(x):
    if isinstance(x, float):
        return x != x
    if isinstance(x, dict):
        return any(bad_num(v) for k, v in x.items() if k != "integrated_lufs")
    if isinstance(x, list):
        return any(bad_num(v) for v in x)
    return False


def run_case(binp, out, tag, text, audio, timeout, signal=False):
    """(rc, codes, sanitizer reports, secs, last whole second's LUFS or None)."""
    path = os.path.join(out, tag + ".inf")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    san = os.path.join(out, tag + ".san")
    env = dict(os.environ, INFINITE_NO_UPDATE_CHECK="1",
               ASAN_OPTIONS=f"detect_leaks=0:log_path={san}",
               UBSAN_OPTIONS=f"print_stacktrace=1:log_path={san}",
               TSAN_OPTIONS=f"log_path={san}")
    dur = SIG_SECS if signal else 1.5
    cmd = ([binp, "--audio-summary", path, os.path.join(out, tag + ".json"), "--duration", str(dur)] if audio
           else [binp, "--frame", path, "0.5", os.path.join(out, tag + ".png")])
    cmd += ["--lenient", "--timeout", str(int(timeout))]
    t0 = time.time()
    codes = ""
    try:
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=timeout + 15)
        rc = r.returncode
        for line in reversed((r.stdout or "").splitlines()):
            if line.startswith("{"):
                try:
                    j = json.loads(line)
                    codes = ",".join(sorted({e.get("code", "?") for e in j.get("errors", [])}))
                except ValueError:
                    pass
                break
    except subprocess.TimeoutExpired:
        rc = "TIMEOUT"
    if rc == 6:
        rc = "TIMEOUT"
    reports = [f for f in os.listdir(out) if f.startswith(tag + ".san")]
    tail = prev = None
    if audio:
        raw = ""
        try:
            raw = open(os.path.join(out, tag + ".json")).read()
            j = json.loads(raw)
            peak = j.get("sample_peak")
            nonfinite = j.get("non_finite_frames") or 0
            lps = j.get("loudness_per_second") or []
            tail = lps[-1] if lps and isinstance(lps[-1], (int, float)) else None
            if tail is not None and len(lps) > 1 and isinstance(lps[-2], (int, float)):
                prev = lps[-2]
            if signal:
                if nonfinite > SIG_RATE * (SIG_BURST + SIG_GRACE):
                    codes += ",NANSTUCK"
                elif nonfinite:
                    codes += ",NANPASS"
            elif nonfinite or bad_num(j) or not isinstance(peak, (int, float)):
                codes += ",NANAUDIO"
            elif peak > HOT_PEAK:
                codes += ",HOTAUDIO"
        except ValueError:
            if re.search(r"[:\[,]\s*-?nan", raw, re.I):
                codes += ",NANAUDIO"
        except OSError:
            pass
    return rc, codes, reports, round(time.time() - t0, 1), tail, prev


def describe(binp, out):
    p = os.path.join(out, "describe.json")
    if not os.path.exists(p):
        subprocess.run([binp, "--describe", "--json", p], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=600)
    return json.load(open(p))["types"]


def bad_types(path):
    return {l.split("\t")[0] for l in open(path) if l.rstrip("\n").endswith("\tBAD")}


def fuzz_type(binp, out, kind, t, prior, max_timeout, write):
    """All cases of one type, base first (its time sets the others' budget)."""
    safe = re.sub(r"[^A-Za-z0-9]+", "_", t["type"])
    base = prior.get("base")
    budget = max_timeout
    if base is not None and base[0] != "TIMEOUT":
        budget = min(max_timeout, max(20.0, 5.0 * base[1]))
    for v in ["base"] + VALUES[kind]:
        label = label_of(kind, v)
        if label in prior:
            continue
        if v != "base" and base is not None and base[0] == "TIMEOUT":
            write(t["type"], label, "SKIP", "(base timed out)", 0, 0, "ok")
            continue
        sig = signal_wav(out, "base" if v == "base" else v) if kind == "signal" else None
        text, audio = patch_for(t, kind, None if v == "base" else v, sig)
        tag = f"{kind}-{safe}-{label.lstrip('-').replace('.', '')}{'n' if label.startswith('-') else ''}"
        signal = kind == "signal" and audio
        rc, codes, reports, secs, tail, prev = run_case(binp, out, tag, text, audio,
                                                  max_timeout if v == "base" else budget, signal)
        if signal and v == "base" and tail is None:
            codes += ",NOSIGNAL"  # the sine never reached the sink: this type's rows prove nothing
        if signal and v != "base" and base is not None:
            base_tail = base[3] if len(base) > 3 else None
            if secs > 4 * max(base[1], 1.0):
                codes += ",SLOW"
            if base_tail is not None and tail is None and "NANSTUCK" not in codes:
                codes += ",SILENT"
            elif (base_tail is not None and tail is not None and tail > base_tail + 20
                  and (prev is None or tail > prev - 3)):
                # a reverb or delay tail still falling is decay, not a stuck state
                codes += ",HOTSTUCK"
        crashed = rc == "TIMEOUT" or (isinstance(rc, int) and (rc < 0 or rc >= 128))
        if v == "base":
            base = ("TIMEOUT" if rc == "TIMEOUT" else codes, secs, rc, tail)
            budget = min(max_timeout, max(20.0, 5.0 * secs))
            bad = crashed or bool(reports)
        else:
            new_codes = set(codes.split(",")) - set(base[0].split(","))
            base_rc = base[2] if len(base) > 2 else rc
            bad = crashed or bool(reports) or bool(new_codes - INFO_CODES) or (rc not in (0, 3) and str(rc) != str(base_rc))
        write(t["type"], label, rc, codes, len(reports), secs, "BAD" if bad else "ok")
        if not bad:
            for ext in (".inf", ".json", ".png"):
                try:
                    os.remove(os.path.join(out, tag + ext))
                except OSError:
                    pass


def run_kind(binp, out, kind, only, jobs, max_timeout):
    types = describe(binp, out)
    res_path = os.path.join(out, f"results-{kind}.tsv")
    prior = {}
    if os.path.exists(res_path):
        for l in open(res_path):
            f = l.rstrip("\n").split("\t")
            if len(f) >= 7:
                prior.setdefault(f[0], {})[f[1]] = ("TIMEOUT" if f[2] == "TIMEOUT" else f[3], float(f[5] or 0), f[2])
    todo = [t for t in sorted(types, key=lambda t: (t["category"], t["type"]))
            if (not only or t["type"] in only) and not t.get("hardware_driven") and has_params(t, kind)]
    lock = threading.Lock()
    res = open(res_path, "a")

    def write(*row):
        with lock:
            res.write("\t".join(str(x) for x in row) + "\n")
            res.flush()

    with cf.ThreadPoolExecutor(max(1, jobs)) as ex:
        futs = [ex.submit(fuzz_type, binp, out, kind, t, prior.get(t["type"], {}), max_timeout, write) for t in todo]
        for f in cf.as_completed(futs):
            f.result()
    res.close()
    rows = [l.rstrip("\n").split("\t") for l in open(res_path)]
    nbad = sum(1 for r in rows if r[-1] == "BAD")
    print(f"{kind}: {len(rows)} rows, {nbad} BAD -> {res_path}")
    return nbad


def selftest():
    t = {"type": "X", "category": "C", "params": [{"key": "k", "kind": "int"}, {"key": "f", "kind": "float"}],
         "inputs": [{"kind": "audio", "slot": 0}], "outputs": [{"kind": "audio"}],
         "modulatable": [{"key": "f"}]}
    text, audio = patch_for(t, "int", "5")
    assert "  i k 5" in text and "  f f" not in text and audio, text
    assert "aud a 0 fsyn" in text and "note fsyn 0 fseq 0" in text
    text, _ = patch_for(t, "mod", "bi=1")
    assert "mod a f lfo 0 1 1 0.5" in text and "id lfo" in text, text
    text, _ = patch_for(t, "mod", None)
    assert "lfo" not in text
    sink = {"type": "S", "category": "C", "params": [], "inputs": [{"kind": "audio", "slot": 0}], "outputs": []}
    assert patch_for(sink, "int", None)[1], "a sink with no outputs runs as audio"
    assert bad_num({"a": [1.0, float("nan")]}) and not bad_num({"integrated_lufs": float("nan")})
    text, _ = patch_for(t, "signal", "nan", "/x/nan.wav")
    assert "aud a 0 fsig" in text and "s path /x/nan.wav" in text and "fsyn" not in text, text
    assert has_params(t, "signal") and not has_params(sink | {"inputs": []}, "signal")
    assert label_of("text", "big=AAA") == "big" and label_of("int", "-5") == "-5"
    print("param_fuzz selftest ok")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "selftest":
        return selftest()
    a = sys.argv
    if len(a) < 3:
        sys.exit(__doc__)
    binp, out = a[1], a[2]
    kind = a[a.index("--kind") + 1] if "--kind" in a else "int"
    only = {a[i + 1] for i, x in enumerate(a) if x == "--only"}
    if "--types-from" in a:
        only |= bad_types(a[a.index("--types-from") + 1])
    jobs = int(a[a.index("--jobs") + 1]) if "--jobs" in a else 1
    max_timeout = float(a[a.index("--max-timeout") + 1]) if "--max-timeout" in a else 90.0
    os.makedirs(out, exist_ok=True)
    kinds = ["int", "float", "text", "mod", "signal"] if kind == "all" else [kind]
    total = sum(run_kind(binp, out, k, only, jobs, max_timeout) for k in kinds)
    sys.exit(1 if total else 0)


if __name__ == "__main__":
    main()
