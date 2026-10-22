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

  param_fuzz.py BIN OUTDIR [--kind int|float|text|mod|all] [--only TYPE]... [--jobs N]
                           [--max-timeout S] [--types-from results.tsv]

  --kind mod     one run per type: every modulatable control on a 32-beat LFO,
                 unipolar and bipolar, full depth (catches controls registered
                 against dead storage, NaN through modulation, out-of-range writes)
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
}
PREFIX = {"int": "i", "float": "f", "text": "s"}
HOT_PEAK = 4.0  # +12 dBFS

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
    if kind == "mod":
        return bool(t.get("modulatable"))
    return any(p.get("kind") == kind for p in t["params"])


def patch_for(t, kind, value):
    """(patch text, is_audio_only). value None = the base run."""
    lines = ["infinite-patch 1", f"node 1 {t['category']} {t['type']}", "  id a"]
    if value is not None and kind != "mod":
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
    lines += [BLOCKS[n] for n in used] + wires
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


def run_case(binp, out, tag, text, audio, timeout):
    path = os.path.join(out, tag + ".inf")
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    san = os.path.join(out, tag + ".san")
    env = dict(os.environ, INFINITE_NO_UPDATE_CHECK="1",
               ASAN_OPTIONS=f"detect_leaks=0:log_path={san}",
               UBSAN_OPTIONS=f"print_stacktrace=1:log_path={san}",
               TSAN_OPTIONS=f"log_path={san}")
    cmd = ([binp, "--audio-summary", path, os.path.join(out, tag + ".json"), "--duration", "1.5"] if audio
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
    if audio:
        raw = ""
        try:
            raw = open(os.path.join(out, tag + ".json")).read()
            j = json.loads(raw)
            peak = j.get("sample_peak")
            if bad_num(j) or not isinstance(peak, (int, float)):
                codes += ",NANAUDIO"
            elif peak > HOT_PEAK:
                codes += ",HOTAUDIO"
        except ValueError:
            if re.search(r"[:\[,]\s*-?nan", raw, re.I):
                codes += ",NANAUDIO"
        except OSError:
            pass
    return rc, codes, reports, round(time.time() - t0, 1)


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
        text, audio = patch_for(t, kind, None if v == "base" else v)
        tag = f"{kind}-{safe}-{label.lstrip('-').replace('.', '')}{'n' if label.startswith('-') else ''}"
        rc, codes, reports, secs = run_case(binp, out, tag, text, audio, max_timeout if v == "base" else budget)
        crashed = rc == "TIMEOUT" or (isinstance(rc, int) and (rc < 0 or rc >= 128))
        if v == "base":
            base = ("TIMEOUT" if rc == "TIMEOUT" else codes, secs, rc)
            budget = min(max_timeout, max(20.0, 5.0 * secs))
            bad = crashed or bool(reports)
        else:
            new_codes = set(codes.split(",")) - set(base[0].split(","))
            base_rc = base[2] if len(base) > 2 else rc
            bad = crashed or bool(reports) or bool(new_codes - {""}) or (rc not in (0, 3) and str(rc) != str(base_rc))
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
    kinds = ["int", "float", "text", "mod"] if kind == "all" else [kind]
    total = sum(run_kind(binp, out, k, only, jobs, max_timeout) for k in kinds)
    sys.exit(1 if total else 0)


if __name__ == "__main__":
    main()
