#!/usr/bin/env python3
"""Patch-variation smoke test: mutate real user patches through the headless engine and find what breaks.

For each patch in the corpus it makes K seeded variants (1-3 mutation operators each), runs every variant once through
the headless CLI, applies the hard checks, and on a failure shrinks the variant to the smallest set of operators that
still fails and saves that as a repro. Serial only; never builds; never touches the originals (variants are written to a
scratch folder).

Usage:
  patch_variants.py --list                          dry run: the patches, their mode, and the variants that would run
  patch_variants.py [--corpus DIR ...] [--variants 5] [--seed 1] [--ops params,env,struct] [--timeout 60]
                    [--only NAME] [--bin PATH] [--date YYYY-MM-DD] [--version v0.6] [--no-findings]
                    [--no-determinism] [--no-canon]
  patch_variants.py --self-test

--corpus defaults to website/assets/community and assets/examples (searched recursively). --ops takes operator names
(nudge min max random enum size rate bpm duration fps bypass delete uncable) or the groups params, env, struct.
Results: docs/release/runs/<date>/variants.tsv (resumable: a patch/variant/seed already in the file is skipped),
docs/release/runs/<date>/repro/ (minimal repros), and one findings entry per distinct failure (tools/release/findings.py).

Hard checks per run: exit 0 (3 = variant invalid and 4 = hardware refused are skips; 2 = rejected argument is logged and
skipped), no signal, no exit 6 or wall-clock hang, a final JSON line, no NaN/inf in frame_stats or the audio summary, the
output size equals the requested size / rate / frame count, audio peak bounded, --canonicalize twice gives identical
bytes (and canonicalizing the result is stable), and the same variant run twice gives identical output
(tools/determinism-by-design.txt is honoured). Elapsed time against the unmutated patch is a FLAG only.
Exit: 0 no hard failures, 1 at least one hard failure (or self-test failed), 2 usage error or refused to run.
"""
import argparse
import datetime
import hashlib
import json
import math
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
RUNS = os.path.join(REPO, "docs", "release", "runs")
DEFAULT_CORPUS = ["website/assets/community", "assets/examples"]
BINARIES = ["build-check/Infinite.app/Contents/MacOS/Infinite", "build/Infinite.app/Contents/MacOS/Infinite"]
BY_DESIGN = os.path.join(REPO, "tools", "determinism-by-design.txt")

PARAM_OPS = ["nudge", "min", "max", "random", "enum"]
ENV_OPS = ["size", "rate", "bpm", "duration", "fps"]
STRUCT_OPS = ["bypass", "delete", "uncable"]
GROUPS = {"params": PARAM_OPS, "env": ENV_OPS, "struct": STRUCT_OPS}
ALL_OPS = PARAM_OPS + ENV_OPS + STRUCT_OPS

SIZES = ["1x1", "3x3", "257x129", "255x255", "3840x2160"]
RATES = [8000, 44100, 96000, 192000]
BPMS = [1, 999]
DURATIONS = [0.01, 0.05, 0.25]
FPSES = [1, 7, 24, 240]
TSV_COLS = ["id", "patch", "n", "seed", "mode", "ops", "status", "exit", "elapsed_ms", "baseline_ms", "failures", "flags", "note"]
WIRES = ("cable", "geo", "aud", "note")
PEAK_HARD = 1000.0
PEAK_FLAG = 16.0
SEV = {"crash": "S1", "hang": "S1", "nan": "S1"}


# ---------------------------------------------------------------- patch text

def parse_patch(text):
    """Return (lines, nodes, wires): nodes[index] = dict(type, cat, start, end, id); wires = list of (line_no, kind, dst, src)."""
    lines = text.split("\n")
    nodes, cur, ids = {}, None, {}
    for i, ln in enumerate(lines):
        m = re.match(r"^node (\d+) (\S+) ?(.*)$", ln)
        if m:
            cur = int(m.group(1))
            nodes[cur] = {"cat": m.group(2), "type": m.group(3).strip(), "start": i, "end": i, "id": None}
        elif cur is not None and ln.strip() == "end":
            nodes[cur]["end"] = i
            cur = None
        elif cur is not None:
            mi = re.match(r"^  id (\S+)\s*$", ln)
            if mi:
                nodes[cur]["id"] = mi.group(1)
                ids[mi.group(1)] = cur
    def res(tok):
        return int(tok) if tok.isdigit() else ids.get(tok)
    wires = []
    for i, ln in enumerate(lines):
        t = ln.split()
        if len(t) >= 4 and t[0] in WIRES:
            wires.append((i, t[0], res(t[1]), res(t[3])))
    return lines, nodes, wires


def refs(line, res_ids):
    """Node indexes a top-level mod/expr/wire line mentions as destination or source."""
    t = line.split()
    out = []
    def res(tok):
        return int(tok) if tok.isdigit() else res_ids.get(tok)
    if len(t) >= 4 and t[0] in WIRES:
        out = [res(t[1]), res(t[3])]
    elif len(t) >= 4 and t[0] == "mod":
        out = [res(t[1]), res(t[3])]
    elif len(t) >= 3 and t[0] == "expr":
        out = [res(t[1])]
    return [x for x in out if x is not None]


def id_map(nodes):
    return {n["id"]: k for k, n in nodes.items() if n["id"]}


def node_value(lines, node, key):
    for ln in lines[node["start"]:node["end"]]:
        m = re.match(r"^  [fi] %s (\S+)\s*$" % re.escape(key), ln)
        if m:
            try:
                return float(m.group(1))
            except ValueError:
                return None
    return None


def is_sink(node):
    return node["type"] in ("Output", "Audio Out")


def mode_of(nodes):
    """image (an Output node), audio (an Audio Out), both (variants alternate), or none (nothing to render)."""
    types = {n["type"] for n in nodes.values()}
    if "Output" in types and "Audio Out" in types:
        return "both"
    if "Output" in types:
        return "image"
    if "Audio Out" in types:
        return "audio"
    return "none"


def mode_for(mode, n):
    return ("image", "audio")[n % 2] if mode == "both" else mode


def has_blank_media(text):
    return bool(re.search(r"^  s (path|lane\d+_path) ?$", text, re.M))


def apply_struct(text, op):
    """Apply one structural op (dict with kind and numbers) to the patch text; None if it no longer applies."""
    lines, nodes, wires = parse_patch(text)
    ids = id_map(nodes)
    k = op["kind"]
    if k == "uncable":
        w = [x for x in wires if x[0] == op["line"]]
        if not w:
            return None
        return "\n".join(l for i, l in enumerate(lines) if i != op["line"])
    x = op["node"]
    if x not in nodes:
        return None
    if k == "delete":
        n = nodes[x]
        drop = set(range(n["start"], n["end"] + 1))
        for i, ln in enumerate(lines):
            if x in refs(ln, ids):
                drop.add(i)
        return "\n".join(l for i, l in enumerate(lines) if i not in drop)
    if k == "bypass":
        incoming = [w for w in wires if w[2] == x]
        if len(incoming) != 1 or incoming[0][3] is None:
            return None
        in_i, in_kind, _, src = incoming[0]
        out = list(lines)
        changed = False
        for (i, kind, dst, s) in wires:
            if s == x and kind == in_kind:
                t = out[i].split()
                t[3] = str(src)
                out[i] = " ".join(t)
                changed = True
        if not changed:
            return None
        del out[in_i]
        return "\n".join(out)
    return None


def struct_candidates(text, kind):
    """The node/line choices a structural op could target, in file order."""
    lines, nodes, wires = parse_patch(text)
    if kind == "uncable":
        return [{"kind": "uncable", "line": w[0]} for w in wires]
    out = []
    for idx, n in nodes.items():
        if is_sink(n):
            continue
        if kind == "delete":
            out.append({"kind": "delete", "node": idx})
        elif kind == "bypass":
            inc = [w for w in wires if w[2] == idx]
            if len(inc) == 1 and any(w[3] == idx and w[1] == inc[0][1] for w in wires):
                out.append({"kind": "bypass", "node": idx})
    return out


# ---------------------------------------------------------------- operator generation

def seed_of(base, patch, n):
    return int(hashlib.sha256(("%s|%s|%d" % (base, patch, n)).encode()).hexdigest()[:8], 16)


def param_candidates(text, describe):
    """(node index, key, kind, min, max, current, options) for every UI control with a range, per `--describe`."""
    lines, nodes, _ = parse_patch(text)
    out = []
    for idx, n in nodes.items():
        d = describe(n["type"])
        if not d or n["cat"] in ("Output",):
            continue
        for r in d.get("rows", []):
            if not r.get("ui") or r.get("min") is None or r.get("max") is None or r["kind"] not in ("int", "float"):
                continue
            out.append((idx, r["key"], r["kind"], float(r["min"]), float(r["max"]), node_value(lines, n, r["key"]),
                        r.get("options") or []))
    return out


def fmt(v, kind):
    if kind == "int":
        return str(int(round(v)))
    return ("%.6g" % v)


def make_op(name, rng, text, mode, cands):
    """One operator as a plain dict (so it can be stored, replayed and dropped by the minimiser); None if not applicable."""
    if name in PARAM_OPS:
        pool = [c for c in cands if (name != "enum" or c[6])]
        if not pool:
            return None
        idx, key, kind, lo, hi, cur, opts = rng.choice(pool)
        span = hi - lo
        if name == "nudge":
            base = cur if cur is not None else (lo + hi) / 2
            v = min(hi, max(lo, base + rng.choice([-1, 1]) * rng.uniform(0.02, 0.15) * span))
        elif name == "min":
            v = lo
        elif name == "max":
            v = hi
        elif name == "random":
            v = rng.uniform(lo, hi)
        else:
            v = lo + rng.randrange(len(opts)) if opts else lo
        return {"kind": "set", "how": name, "node": idx, "key": key, "value": fmt(v, kind)}
    if name == "size":
        return {"kind": "size", "value": rng.choice(SIZES)} if mode == "image" else None
    if name == "fps":
        return {"kind": "fps", "value": rng.choice(FPSES)} if mode == "image" else None
    if name == "rate":
        return {"kind": "rate", "value": rng.choice(RATES)} if mode == "audio" else None
    if name == "bpm":
        return {"kind": "bpm", "value": rng.choice(BPMS)} if mode == "audio" else None
    if name == "duration":
        return {"kind": "duration", "value": rng.choice(DURATIONS)} if mode in ("audio", "image") else None
    if name in STRUCT_OPS:
        c = struct_candidates(text, name)
        return dict(rng.choice(c), how=name) if c else None
    return None


def make_variant(base_seed, patch, n, text, mode, ops_pool, describe):
    """Deterministic: the same (seed, patch, n) always gives the same ops list."""
    rng = random.Random(seed_of(base_seed, patch, n))
    cands = param_candidates(text, describe) if any(o in PARAM_OPS for o in ops_pool) else []
    want = rng.randint(1, 3)
    ops, tries = [], 0
    while len(ops) < want and tries < 40:
        tries += 1
        name = rng.choice(ops_pool)
        cur = text
        for o in ops:
            if o["kind"] in STRUCT_OPS:
                cur = apply_struct(cur, o) or cur
        op = make_op(name, rng, cur, mode, cands)
        if op is None:
            continue
        if op["kind"] in ("size", "rate", "bpm", "duration", "fps") and any(o["kind"] == op["kind"] for o in ops):
            continue
        if op["kind"] in STRUCT_OPS and any(o["kind"] in STRUCT_OPS for o in ops):
            continue  # one structural edit per variant keeps the node indexes of the others valid
        ops.append(op)
    return ops


def apply_ops(text, ops):
    """(variant text, extra CLI args) for an ops list; text is None if a structural op no longer applies."""
    for o in ops:
        if o["kind"] in STRUCT_OPS:
            text = apply_struct(text, o)
            if text is None:
                return None
    return text


# ---------------------------------------------------------------- running the engine

def run_proc(cmd, wall, env):
    """Run one child; kill only its own process group on a wall-clock overrun. Returns (rc, stdout, wall_seconds)."""
    t0 = time.time()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env, start_new_session=True)
    try:
        out, _ = p.communicate(timeout=wall)
        rc = p.returncode
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, 9)
        except OSError:
            pass
        out, _ = p.communicate()
        rc = "wallclock"
    return rc, out.decode("utf-8", "replace"), time.time() - t0


def last_json(out):
    for ln in reversed([x for x in out.split("\n") if x.strip()]):
        if ln.lstrip().startswith("{"):
            try:
                return json.loads(ln)
            except ValueError:
                return None
        return None
    return None


def nonfinite(o, path=""):
    if isinstance(o, float) and (math.isnan(o) or math.isinf(o)):
        return path
    if isinstance(o, dict):
        for k, v in o.items():
            r = nonfinite(v, path + "." + k)
            if r:
                return r
    if isinstance(o, list):
        for i, v in enumerate(o):
            r = nonfinite(v, "%s[%d]" % (path, i))
            if r:
                return r
    return None


def png_size(path):
    try:
        with open(path, "rb") as fh:
            h = fh.read(24)
        return struct.unpack(">II", h[16:24])
    except (OSError, struct.error):
        return None


def wav_info(path):
    """(channels, rate, bits, frames) from a RIFF/WAVE file, or None."""
    try:
        with open(path, "rb") as fh:
            b = fh.read()
    except OSError:
        return None
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        return None
    pos, fmt_ = 12, None
    while pos + 8 <= len(b):
        cid, size = b[pos:pos + 4], struct.unpack("<I", b[pos + 4:pos + 8])[0]
        if cid == b"fmt ":
            fmt_ = struct.unpack("<HHIIHH", b[pos + 8:pos + 24])
        if cid == b"data" and fmt_:
            size = min(size, len(b) - pos - 8)
            ch, bits = fmt_[1], fmt_[5]
            return (ch, fmt_[2], bits, size // max(1, ch * bits // 8))
        pos += 8 + size + (size & 1)
    return None


def sha(path):
    try:
        with open(path, "rb") as fh:
            return hashlib.sha256(fh.read()).hexdigest()
    except OSError:
        return None


class Engine:
    def __init__(self, binary, timeout, work, use_determinism=True, use_canon=True):
        self.bin, self.timeout, self.work = binary, timeout, work
        self.det, self.canon = use_determinism, use_canon
        self.env = dict(os.environ, INFINITE_NO_UPDATE_CHECK="1")
        self.cache = {}
        self.by_design = self._by_design()
        self.runs = 0
        self.seconds = 0.0

    @staticmethod
    def _by_design():
        try:
            with open(BY_DESIGN, encoding="utf-8") as fh:
                return [x.strip() for x in fh if x.strip() and not x.startswith("#")]
        except OSError:
            return []

    def call(self, args):
        rc, out, secs = run_proc([self.bin] + args, self.timeout + 15, self.env)
        self.runs += 1
        self.seconds += secs
        return rc, out, secs

    def describe(self, type_name):
        if type_name in self.cache:
            return self.cache[type_name]
        path = os.path.join(self.work, "describe-%s.json" % hashlib.md5(type_name.encode()).hexdigest()[:10])
        d = None
        rc, out, _ = self.call(["--describe", type_name, "--json", path])
        if rc == 0:
            try:
                with open(path, encoding="utf-8") as fh:
                    d = json.load(fh)["types"][0]
            except (OSError, ValueError, KeyError, IndexError):
                d = None
        self.cache[type_name] = d
        return d

    # one engine invocation for a variant; returns the command and a result dict
    def command(self, mode, vpath, ops, outdir, tag, lenient):
        a = {o["kind"]: o["value"] for o in ops if "value" in o and o["kind"] != "set"}
        sets = [x for o in ops if o["kind"] == "set" for x in ("--set", "%d.%s=%s" % (o["node"], o["key"], o["value"]))]
        common = ["--timeout", str(self.timeout)] + (["--lenient"] if lenient else [])
        if mode == "image" and ("fps" in a or "duration" in a):
            d = os.path.join(outdir, tag + "-frames")
            cmd = ["--frames-dir", vpath, d, "--duration", str(a.get("duration", 0.25)), "--fps", str(a.get("fps", 30))]
            if "size" in a:
                cmd += ["--size", a["size"]]
            return cmd + sets + common, d
        if mode == "image":
            out = os.path.join(outdir, tag + ".png")
            cmd = ["--frame", vpath, "0", out, "--size", a.get("size", "256x256")]
            return cmd + sets + common, out
        out = os.path.join(outdir, tag + ".json")
        wav = os.path.join(outdir, tag + ".wav")
        cmd = ["--audio-summary", vpath, out, "--duration", str(a.get("duration", 1)), "--wav", wav]
        if "rate" in a:
            cmd += ["--sample-rate", str(a["rate"])]
        if "bpm" in a:
            cmd += ["--bpm", str(a["bpm"])]
        return cmd + sets + common, wav

    def one(self, mode, vpath, ops, outdir, tag, lenient):
        cmd, artefact = self.command(mode, vpath, ops, outdir, tag, lenient)
        rc, out, secs = self.call(cmd)
        return {"cmd": cmd, "rc": rc, "out": out, "json": last_json(out), "secs": secs, "artefact": artefact}

    def evaluate(self, patch, text, mode, ops, lenient, outdir, baseline_ms=None):
        """Run a variant and apply every check. Returns dict(status pass|fail|skip, fails, flags, note, ...)."""
        vtext = apply_ops(text, ops)
        res = {"status": "pass", "fails": [], "flags": [], "note": "", "exit": None, "elapsed_ms": None}
        if vtext is None:
            res.update(status="skip", note="structural op no longer applies")
            return res
        os.makedirs(outdir, exist_ok=True)
        tag = "v%d" % int(time.time() * 1000 % 1e9)
        vpath = os.path.join(outdir, tag + ".inf")
        with open(vpath, "w", encoding="utf-8") as fh:
            fh.write(vtext)
        r = self.one(mode, vpath, ops, outdir, tag + "a", lenient)
        res["cmd"], res["vpath"], res["exit"] = r["cmd"], vpath, r["rc"]
        j = r["json"]
        res["elapsed_ms"] = j.get("elapsed_ms") if j else None
        rc = r["rc"]
        if rc == "wallclock":
            res["fails"].append(("hang", "no exit within %ds wall clock" % (self.timeout + 15)))
        elif isinstance(rc, int) and rc < 0:
            res["fails"].append(("crash", "killed by signal %d" % -rc))
        elif rc == 6:
            res["fails"].append(("hang", "exit 6 (timeout %ds)" % self.timeout))
        elif rc == 4:
            res.update(status="skip", note="hardware refused (exit 4)")
        elif rc == 3:
            res.update(status="skip", note="variant invalid (exit 3): %s" % first_error(j))
        elif rc == 2:
            res.update(status="skip", note="argument rejected (exit 2): %s" % first_error(j))
        elif rc != 0:
            res["fails"].append(("exit-%s" % rc, first_error(j) or "undocumented exit code"))
        if j is None and not res["fails"] and res["status"] != "skip":
            res["fails"].append(("no-json", "no final JSON line"))
        if res["status"] == "skip":
            return res
        if rc == 0 and j is not None:
            self.check_output(mode, ops, j, r, res)
            if self.det and not any(c in ("crash", "hang") for c, _ in res["fails"]):
                self.check_determinism(patch, mode, vpath, ops, outdir, tag, lenient, r, res)
        if baseline_ms and res["elapsed_ms"] and res["elapsed_ms"] > 10 * baseline_ms + 2000:
            res["flags"].append("slow: %dms vs %dms unmutated" % (res["elapsed_ms"], baseline_ms))
        if rc == 0 and self.canon:
            self.check_canon(vpath, outdir, tag, lenient, res)
        res["status"] = "fail" if res["fails"] else "pass"
        return res

    def check_output(self, mode, ops, j, r, res):
        bad = nonfinite(j.get("frame_stats")) or nonfinite(j.get("audio_summary"))
        if bad:
            res["fails"].append(("nan", "non-finite value at %s" % bad))
        if j.get("ok") is not True:
            res["fails"].append(("ok-false", "exit 0 but ok=false: %s" % first_error(j)))
        a = {o["kind"]: o["value"] for o in ops if "value" in o and o["kind"] != "set"}
        if mode == "image" and ("fps" in a or "duration" in a):
            d = r["artefact"]
            n = len([f for f in os.listdir(d) if f.endswith(".png")]) if os.path.isdir(d) else 0
            if n < 1 or n != j.get("frames"):
                res["fails"].append(("frame-count", "json says %s frames, %d PNGs on disk" % (j.get("frames"), n)))
            want = [int(x) for x in a["size"].split("x")] if "size" in a else None
            if want and j.get("size") != want:
                res["fails"].append(("size", "asked %s, json says %s" % (a["size"], j.get("size"))))
        elif mode == "image":
            w, h = [int(x) for x in a.get("size", "256x256").split("x")]
            got = png_size(r["artefact"])
            if got != (w, h):
                res["fails"].append(("size", "asked %dx%d, PNG is %s" % (w, h, "x".join(map(str, got)) if got else "missing")))
            elif j.get("size") != [w, h]:
                res["fails"].append(("size", "PNG is %dx%d, json says %s" % (w, h, j.get("size"))))
        else:
            rate = a.get("rate", 48000)
            dur = a.get("duration", 1)
            info = wav_info(r["artefact"])
            want_frames = int(round(dur * rate))
            s = j.get("audio_summary") or {}
            if not info:
                res["fails"].append(("size", "WAV missing or unreadable"))
            elif info[1] != rate or abs(info[3] - want_frames) > 1:
                res["fails"].append(("size", "asked %d Hz x %gs (%d frames), WAV is %d Hz x %d frames" % (rate, dur, want_frames, info[1], info[3])))
            elif j.get("audio", {}).get("frames") != info[3]:
                res["fails"].append(("size", "json frames %s but WAV has %d" % (j.get("audio", {}).get("frames"), info[3])))
            pk = s.get("sample_peak")
            if isinstance(pk, (int, float)):
                if abs(pk) > PEAK_HARD:
                    res["fails"].append(("peak", "sample peak %g" % pk))
                elif abs(pk) > PEAK_FLAG:
                    res["flags"].append("peak %g" % pk)

    def check_determinism(self, patch, mode, vpath, ops, outdir, tag, lenient, first, res):
        if any(patch.endswith(x) or x.endswith(os.path.basename(patch)) for x in self.by_design):
            res["flags"].append("determinism skipped (by design)")
            return
        second = self.one(mode, vpath, ops, outdir, tag + "b", lenient)
        if second["rc"] != first["rc"]:
            res["fails"].append(("nondeterministic", "exit %s then %s" % (first["rc"], second["rc"])))
            return
        if mode == "image" and os.path.isdir(first["artefact"]):
            la = sorted(os.listdir(first["artefact"]))
            lb = sorted(os.listdir(second["artefact"])) if os.path.isdir(second["artefact"]) else []
            same = la == lb and all(sha(os.path.join(first["artefact"], f)) == sha(os.path.join(second["artefact"], f)) for f in la)
        else:
            same = sha(first["artefact"]) is not None and sha(first["artefact"]) == sha(second["artefact"])
        if not same:
            res["fails"].append(("nondeterministic", "two runs of the same variant wrote different bytes"))
            res["cmd2"] = second["cmd"]
        elif nodiff_stats(first["json"]) != nodiff_stats(second["json"]):
            res["fails"].append(("nondeterministic", "same files, different frame_stats/audio_summary"))

    def check_canon(self, vpath, outdir, tag, lenient, res):
        outs = []
        src = vpath
        for i in range(3):
            o = os.path.join(outdir, "%s-canon%d.inf" % (tag, i))
            rc, out, _ = self.call(["--canonicalize", src, o] + (["--lenient"] if lenient else []))
            if rc != 0:
                if i == 0 and rc in (2, 3):
                    return  # the engine refuses to canonicalize this variant: that is the validate path, already logged
                res["fails"].append(("canon-exit", "--canonicalize exit %s on pass %d" % (rc, i + 1)))
                return
            outs.append(o)
            if i == 0:
                src = vpath  # second pass: same input again
            elif i == 1:
                src = outs[0]  # third: canonicalize the canonical file
        a, b, c = (open(p, "rb").read() for p in outs)
        if a != b:
            res["fails"].append(("canon-nondeterministic", "--canonicalize twice on the same file gave different bytes"))
        elif a != c:
            res["fails"].append(("canon-unstable", "canonicalizing the canonical file changes it again"))


def nodiff_stats(j):
    return json.dumps([j.get("frame_stats"), j.get("audio_summary"), j.get("frames"), j.get("size")], sort_keys=True)


def first_error(j):
    if j and j.get("errors"):
        e = j["errors"][0]
        return "%s %s" % (e.get("code", ""), e.get("message", ""))[:160]
    return ""


# ---------------------------------------------------------------- minimiser

def minimise(ops, still_fails):
    """Drop operators one at a time while the failure persists; returns the smallest list (greedy, repeated to a fixpoint)."""
    cur = list(ops)
    changed = True
    while changed and len(cur) > 1:
        changed = False
        for i in range(len(cur)):
            trial = cur[:i] + cur[i + 1:]
            if trial and still_fails(trial):
                cur, changed = trial, True
                break
    if len(cur) == 1 and still_fails([]):
        return []
    return cur


def op_str(o):
    if o["kind"] == "set":
        return "%s %d.%s=%s" % (o["how"], o["node"], o["key"], o["value"])
    if o["kind"] in STRUCT_OPS:
        return "%s %s" % (o["how"] if "how" in o else o["kind"], o.get("node", "line %s" % o.get("line")))
    return "%s %s" % (o["kind"], o["value"])


def shell_cmd(binary, cmd):
    q = lambda s: s if re.match(r"^[\w@%+=:,./-]+$", s) else "'" + s.replace("'", "'\\''") + "'"
    return " ".join(q(x) for x in [os.path.relpath(binary, REPO)] + cmd)


# ---------------------------------------------------------------- corpus, TSV, findings

def collect_corpus(dirs, only=None):
    found = []
    for d in dirs:
        root = d if os.path.isabs(d) else os.path.join(REPO, d)
        for dp, _, fs in os.walk(root):
            for f in sorted(fs):
                if f.endswith(".inf"):
                    p = os.path.join(dp, f)
                    found.append(p)
    found = sorted(set(found), key=lambda p: os.path.relpath(p, REPO))
    if only:
        found = [p for p in found if only in os.path.relpath(p, REPO)]
    return found


def read_tsv(path):
    rows = []
    try:
        with open(path, encoding="utf-8") as fh:
            next(fh, None)
            for ln in fh:
                parts = ln.rstrip("\n").split("\t")
                if len(parts) == len(TSV_COLS):
                    rows.append(dict(zip(TSV_COLS, parts)))
    except OSError:
        pass
    return rows


def write_row(path, row):
    new = not os.path.exists(path)
    with open(path, "a", encoding="utf-8") as fh:
        if new:
            fh.write("\t".join(TSV_COLS) + "\n")
        fh.write("\t".join(str(row.get(c, "")).replace("\t", " ").replace("\n", " ") for c in TSV_COLS) + "\n")


def parse_ops_arg(arg):
    if not arg:
        return list(ALL_OPS)
    out = []
    for t in arg.split(","):
        t = t.strip()
        if t in GROUPS:
            out += GROUPS[t]
        elif t in ALL_OPS:
            out.append(t)
        else:
            raise ValueError("unknown operator or group '%s'" % t)
    return out


def file_finding(version, failure_key, check, sev, patch, min_ops, repro_cmd, repro_path, build):
    """Create (or skip when it already exists) one findings entry; returns the path or None."""
    d = os.path.join(REPO, "docs", "release", "findings", version)
    tag = "[pv:%s]" % hashlib.md5(failure_key.encode()).hexdigest()[:8]
    if os.path.isdir(d):
        for f in os.listdir(d):
            try:
                if tag in open(os.path.join(d, f), encoding="utf-8").read(600):
                    return None
            except OSError:
                pass
    title = "Patch variation: %s on %s (%s) %s" % (check, os.path.basename(patch), ", ".join(op_str(o) for o in min_ops) or "unmutated", tag)
    p = subprocess.run([sys.executable, os.path.join(HERE, "findings.py"), "new", "--version", version, "--category", "A",
                        "--title", title, "--severity", sev, "--found-by", "patch_variants.py", "--build", build],
                       capture_output=True, text=True)
    if p.returncode != 0:
        return None
    path = p.stdout.strip().split("\n")[-1]
    try:
        with open(path, encoding="utf-8") as fh:
            body = fh.read()
        body = body.replace("1. \n2. \n3. Frequency: ", "1. Run: `%s`\n2. Variant file: `%s`\n3. Frequency: every run (seeded, reproducible)"
                            % (repro_cmd, os.path.relpath(repro_path, REPO)))
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(body)
    except OSError:
        pass
    return path


def pick_binary(arg):
    if arg:
        return os.path.abspath(arg)
    for b in BINARIES:
        p = os.path.join(REPO, b)
        if os.path.exists(p):
            return p
    return None


def machine_busy():
    p = subprocess.run(["pgrep", "-fl", "cmake --build|make |clang|Infinite.app/Contents/MacOS/Infinite"], capture_output=True, text=True)
    mine = str(os.getpid())
    lines = [l for l in p.stdout.split("\n") if l.strip() and not l.startswith(mine + " ") and "pgrep" not in l and "patch_variants" not in l]
    return lines


def git_head():
    p = subprocess.run(["git", "-C", REPO, "rev-parse", "--short", "HEAD"], capture_output=True, text=True)
    return p.stdout.strip() or "unknown"


# ---------------------------------------------------------------- main

def plan_variants(a, patches, ops_pool, engine):
    """Yield (patch path, n, ops list, mode, text) for every variant without running anything."""
    for p in patches:
        text = open(p, encoding="utf-8").read()
        lines, nodes, _ = parse_patch(text)
        mode = mode_of(nodes)
        rel = os.path.relpath(p, REPO)
        for n in range(a.variants):
            m = mode_for(mode, n)
            ops = make_variant(a.seed, rel, n, text, m, ops_pool, engine.describe) if m != "none" else []
            yield p, rel, n, ops, m, text


def main(argv):
    ap = argparse.ArgumentParser(description="Patch-variation smoke test through the headless engine")
    ap.add_argument("--corpus", action="append")
    ap.add_argument("--variants", type=int, default=5)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--ops")
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--only")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--bin")
    ap.add_argument("--date", default=datetime.date.today().isoformat())
    ap.add_argument("--version", default="v0.6")
    ap.add_argument("--no-findings", action="store_true")
    ap.add_argument("--no-determinism", action="store_true")
    ap.add_argument("--no-canon", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    try:
        ops_pool = parse_ops_arg(a.ops)
    except ValueError as e:
        print(str(e), file=sys.stderr)
        return 2
    binary = pick_binary(a.bin)
    if not binary or not os.path.exists(binary):
        print("no Infinite binary found (looked for %s); pass --bin" % ", ".join(BINARIES), file=sys.stderr)
        return 2
    patches = collect_corpus(a.corpus or DEFAULT_CORPUS, a.only)
    if not patches:
        print("no patches matched", file=sys.stderr)
        return 2
    run_dir = os.path.join(RUNS, a.date)
    work = tempfile.mkdtemp(prefix="patch_variants-")
    engine = Engine(binary, a.timeout, work, not a.no_determinism, not a.no_canon)

    if a.list:
        print("binary: %s   commit: %s   seed: %d   variants/patch: %d" % (os.path.relpath(binary, REPO), git_head(), a.seed, a.variants))
        for p, rel, n, ops, mode, text in plan_variants(a, patches, ops_pool, engine):
            print("%-52s %-6s v%d  %s" % (rel, mode, n, "; ".join(op_str(o) for o in ops) or "-"))
        shutil.rmtree(work, ignore_errors=True)
        return 0

    busy = machine_busy()
    if busy:
        print("refusing to run: a build or an Infinite app is running:\n  " + "\n  ".join(busy), file=sys.stderr)
        return 2
    os.makedirs(os.path.join(run_dir, "repro"), exist_ok=True)
    tsv = os.path.join(run_dir, "variants.tsv")
    done = {(r["patch"], r["n"], r["seed"]) for r in read_tsv(tsv)}
    build = "%s %s" % (os.path.relpath(binary, REPO).split("/")[0], git_head())
    distinct, hard = {}, 0
    baselines, skipped_modes = {}, set()
    print("binary %s, commit %s, %d patches x %d variants, seed %d" % (os.path.relpath(binary, REPO), git_head(), len(patches), a.variants, a.seed))

    for p in patches:
        text = open(p, encoding="utf-8").read()
        rel = os.path.relpath(p, REPO)
        _, nodes, _ = parse_patch(text)
        mode = mode_of(nodes)
        # strict validate once; --lenient only for patches that fail it, and log which
        rc, out, _ = engine.call(["--validate", p])
        lenient = rc != 0
        if lenient:
            print("  %s: fails strict validate (exit %s: %s); running with --lenient" % (rel, rc, first_error(last_json(out))))
        if has_blank_media(text):
            print("  %s: has blanked media paths (runs with empty sources)" % rel)
        if mode == "none":
            write_row(tsv, {"id": "%s:-" % rel, "patch": rel, "n": "-", "seed": "-", "mode": "none", "status": "skip",
                            "note": "no Output or Audio Out node (nothing to render)" + ("; strict-validate-fail" if lenient else "")})
            continue
        for m in (["image", "audio"] if mode == "both" else [mode]):
            base = engine.evaluate(rel, text, m, [], lenient, os.path.join(work, "base"))
            baselines[(rel, m)] = base.get("elapsed_ms")
            if base["status"] == "skip":
                write_row(tsv, {"id": "%s:base:%s" % (rel, m), "patch": rel, "n": "base", "seed": "-", "mode": m, "status": "skip",
                                "exit": base["exit"], "note": base["note"]})
                skipped_modes.add((rel, m))
                print("  %s: unmutated %s run skipped: %s" % (rel, m, base["note"]))
            elif base["fails"]:
                hard += 1
                record_failure(a, engine, distinct, rel, text, m, [], base, lenient, run_dir, build, work, tsv_row=None)
        for n in range(a.variants):
            seed = seed_of(a.seed, rel, n)
            if (rel, str(n), str(seed)) in done:
                continue
            m = mode_for(mode, n)
            if (rel, m) in skipped_modes:
                continue
            ops = make_variant(a.seed, rel, n, text, m, ops_pool, engine.describe)
            if not ops:
                write_row(tsv, {"id": "%s:%d:%d" % (rel, n, seed), "patch": rel, "n": n, "seed": seed, "mode": m, "status": "skip", "note": "no applicable operator"})
                continue
            res = engine.evaluate(rel, text, m, ops, lenient, os.path.join(work, "v"), baselines.get((rel, m)))
            row = {"id": "%s:%d:%d" % (rel, n, seed), "patch": rel, "n": n, "seed": seed, "mode": m,
                   "ops": json.dumps(ops, separators=(",", ":")), "status": res["status"], "exit": res["exit"],
                   "elapsed_ms": res["elapsed_ms"], "baseline_ms": baselines.get((rel, m)),
                   "failures": ";".join("%s=%s" % f for f in res["fails"]), "flags": ";".join(res["flags"]), "note": res["note"]}
            if res["status"] == "fail":
                hard += 1
                key = record_failure(a, engine, distinct, rel, text, m, ops, res, lenient, run_dir, build, work, row)
                row["note"] = (row["note"] + " minimal=" + key).strip()
            write_row(tsv, row)
            print("  %-48s v%d %-5s %s %s" % (rel, n, res["status"], "; ".join(op_str(o) for o in ops), ";".join(c for c, _ in res["fails"])))
    shutil.rmtree(work, ignore_errors=True)
    print("\nruns: %d engine invocations, %.1fs wall (%.2fs each); results: %s" % (engine.runs, engine.seconds, engine.seconds / max(1, engine.runs), os.path.relpath(tsv, REPO)))
    print("distinct failures: %d" % len(distinct))
    for k, v in distinct.items():
        print("  %s  x%d  %s" % (k, v["count"], v["repro"]))
    return 1 if hard else 0


def record_failure(a, engine, distinct, rel, text, mode, ops, res, lenient, run_dir, build, work, tsv_row):
    check = res["fails"][0][0]

    def still(trial):
        r = engine.evaluate(rel, text, mode, trial, lenient, os.path.join(work, "min"))
        return r["status"] == "fail" and any(c == check for c, _ in r["fails"])

    small = minimise(ops, still) if ops else []
    final = engine.evaluate(rel, text, mode, small, lenient, os.path.join(work, "min"))
    if not (final["status"] == "fail" and any(c == check for c, _ in final["fails"])):
        small, final = ops, res
    kinds = ",".join(sorted(o["kind"] if o["kind"] != "set" else "set:" + o["key"] for o in small))
    key = "%s|%s|%s" % (check, mode, kinds)
    if key in distinct:
        distinct[key]["count"] += 1
        return key
    vtext = apply_ops(text, small) or text
    name = "%s-%s.inf" % (re.sub(r"\W+", "-", os.path.splitext(os.path.basename(rel))[0]), check)
    rp = os.path.join(run_dir, "repro", name)
    with open(rp, "w", encoding="utf-8") as fh:
        fh.write(vtext)
    cmd = list(final.get("cmd") or res.get("cmd") or [])
    if cmd and final.get("vpath"):
        cmd = [rp if c == final["vpath"] else c for c in cmd]
    outdir = os.path.join(run_dir, "repro")
    cmd = [outdir + "/out" + os.path.splitext(c)[1] if (c.startswith(work) and "." in os.path.basename(c)) else c for c in cmd]
    cmd = [outdir + "/out-frames" if c.startswith(work) else c for c in cmd]
    command = shell_cmd(engine.bin, cmd)
    sev = SEV.get(check, "S2")
    if not small and sev in ("S1",):
        sev = "S0"  # fails with no mutation at all
    distinct[key] = {"count": 1, "repro": command, "severity": sev, "check": check, "ops": [op_str(o) for o in small], "patch": rel}
    print("  FAIL %s %s (%s) minimal ops: %s\n       repro: %s" % (sev, check, rel, "; ".join(op_str(o) for o in small) or "none (unmutated patch)", command))
    if not a.no_findings:
        file_finding(a.version, key, check, sev, rel, small, command, rp, build)
    return key


# ---------------------------------------------------------------- self-test

def self_test():
    ok = True

    def check(name, cond):
        nonlocal ok
        print("%-4s %s" % ("ok" if cond else "FAIL", name))
        ok = ok and cond

    patch = ("infinite-patch 1\nnode 1 Source Shape\n  id s\n  f sizeX 0.5\nend\nnode 2 Effects blur\n  id b\nend\n"
             "node 3 Utility Output\n  id out\nend\ncable b 0 s\ncable out 0 b\nmod s sizeX lfo 0 0 1 0.5\nexpr b radius sin(t)\n")
    lines, nodes, wires = parse_patch(patch)
    check("parses nodes and wires", sorted(nodes) == [1, 2, 3] and len(wires) == 2 and mode_of(nodes) == "image")
    byp = apply_struct(patch, {"kind": "bypass", "node": 2})
    check("bypass reroutes the consumer to the source", byp is not None and "cable out 0 1" in byp and "cable b 0 s" not in byp)
    dele = apply_struct(patch, {"kind": "delete", "node": 2})
    check("delete removes the node and every line that names it", dele is not None and "node 2" not in dele and "expr b" not in dele and "cable out" not in dele and "node 1" in dele)
    unc = apply_struct(patch, {"kind": "uncable", "line": wires[0][0]})
    check("uncable removes exactly one wire", unc is not None and unc.count("cable") == 1)
    check("seed is stable and varies", seed_of(1, "a", 0) == seed_of(1, "a", 0) and seed_of(1, "a", 0) != seed_of(1, "a", 1))
    describe = lambda t: {"rows": [{"key": "sizeX", "kind": "float", "min": 0.0, "max": 1.0, "ui": True, "options": []},
                                    {"key": "shape", "kind": "int", "min": 0, "max": 3, "ui": True, "options": ["a", "b", "c", "d"]}]}
    v1 = make_variant(1, "p.inf", 0, patch, "image", ALL_OPS, describe)
    v2 = make_variant(1, "p.inf", 0, patch, "image", ALL_OPS, describe)
    check("same seed gives identical operators", v1 == v2 and 1 <= len(v1) <= 3)
    check("params stay inside the described range", all(0 <= float(o["value"]) <= 3 for v in (make_variant(1, "p", n, patch, "image", PARAM_OPS, describe) for n in range(30)) for o in v if o["kind"] == "set"))
    ops = [{"kind": "size", "value": "1x1"}, {"kind": "fps", "value": 7}, {"kind": "duration", "value": 0.01}]
    check("minimiser keeps only the operator that matters", minimise(ops, lambda t: any(o["kind"] == "fps" for o in t)) == [ops[1]])
    check("minimiser reports an unmutated failure", minimise(ops[:1], lambda t: True) == [])
    check("nonfinite finds NaN and inf", nonfinite({"a": [1.0, float("nan")]}) == ".a[1]" and nonfinite({"a": float("inf")}) == ".a" and nonfinite({"a": 1}) is None)
    check("last_json takes the final line only", last_json("noise\n{\"ok\":true}\n") == {"ok": True} and last_json("{\"ok\":true}\ntrailing\n") is None)
    td = tempfile.mkdtemp()
    t = os.path.join(td, "v.tsv")
    write_row(t, {"id": "x", "patch": "p", "n": 1, "seed": 5, "status": "pass"})
    check("tsv round trip for resume", [(r["patch"], r["n"], r["seed"]) for r in read_tsv(t)] == [("p", "1", "5")])
    shutil.rmtree(td, ignore_errors=True)
    try:
        parse_ops_arg("nudge,bogus")
        check("unknown operator rejected", False)
    except ValueError:
        check("unknown operator rejected", True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
