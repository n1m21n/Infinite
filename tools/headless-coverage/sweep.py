#!/usr/bin/env python3
"""Headless coverage sweep (H1): does every param of every node change the headless output?

For each node type from `--describe --json`, the patch is auto-wired the same way the census wires it
(scripts/node_census.py build()). The node under test is node 1. Each param then gets:

  v1, v2   two values written into the .inf (not --set), so save/load is part of the test
  mod      an LFO bound to the param through a `mod` record (params with a modulatable index)

Every variant is rendered headless next to two default renders (A and B: determinism) and compared:
images by pixels at two times (--frame --batch), audio and notes by samples (--audio-summary --batch --wav).

Each param ends in one class:
  effective        a written value changed the output
  mod-only         only the LFO changed it (the written values did not)
  conditional      no change in the default patch, but a change once another switch is set (or with no
                   input feeds): `needs` names that context (context pass, below)
  silent-default   the type's default output is silent or blank, so the param could not be judged
  no-effect        nothing changed in any context: dead or needs context the sweep does not try (review list)
  live-only        hardware-driven type: refuses headless by design
  text             text params (paths, code): not swept here
  error            the variant patch failed to load or render
and modulation separately: mod-effective / mod-no-effect / not-modulatable.

Context pass (for params still no-effect): each enum/bool switch of the node is set to its sweep values,
plus a "bare" context with the cables into node 1 removed. In each context the sweep renders the context
alone and the context with every no-effect param moved at once; only contexts that moved get a per-param
round. Renders run in whole-type chunks, so a type's defaults and its variants share one process.

Usage:
  tools/headless-coverage/sweep.py [--bin PATH] [--out DIR] [--procs N] [--chunk N] [Type ...]

Writes DIR/coverage.json (one row per type, per-param classes) and DIR/report.md (tables).
Re-running reuses finished renders in DIR (their sidecars), so an interrupted sweep resumes.
"""
import argparse, json, os, subprocess, sys, time, wave
import concurrent.futures as cf
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from node_census import build  # noqa: E402  same auto-wiring as the census

TIMES = "0.3,1.15"           # two times, not symmetric about an LFO zero crossing
AUDIO_SECONDS = "1.2"
LFO = "Modulators LFO"
SIZE = "128x128"
IMAGE_KINDS = {"image", "geometry", "palette", "camera", "light", "environment", "none"}


# Content for nodes that are silent or blank until a file is loaded (repo assets only)
_A = lambda *p: os.path.join(ROOT, *p)
KIT = [_A("assets/drumkits/infinite-basic", f) for f in ("01-kick.wav", "02-snare.wav", "03-closed-hat.wav",
       "04-open-hat.wav", "05-clap.wav", "06-low-tom.wav", "07-high-tom.wav", "08-bell.wav")]
TAKE = _A("assets/templates/media/studio-take.wav")
CONTENT = {  # type -> file for its path param(s); pad/lane params take the kit in order
    "Sampler": TAKE, "Slicer": TAKE, "PaulStretch": TAKE, "Molder": TAKE, "Grain Molder": TAKE,
    "Granular": TAKE, "Audio File": TAKE, "Partials": TAKE, "MPC": KIT, "Drum Sequencer": KIT, "VMPC": KIT,
    "Image Source": _A("assets/icon_1024.png"), "Palette": _A("assets/icon_1024.png"),
    "Video": _A("assets/videos/die.mov"), "Model 3D": _A("tests/render3d/skinned_two_bone.glb"),
    "HDRI": _A("bench/assets/scenes/boat-moon/boat-moon media/infinite_bench_boat_moon.hdr"),
}


def with_content(patch, t):
    """Load the CONTENT file(s) into a type's path params; the patch unchanged for other types."""
    f = CONTENT.get(t["type"])
    if f is None:
        return patch
    for row in t.get("rows", []):
        k = row["key"]
        if row["kind"] != "text" or not (k in ("path", "pathB", "filePath") or k.endswith("_path")):
            continue
        if isinstance(f, list):
            digits = "".join(c for c in k if c.isdigit())
            v = f[int(digits) % len(f)] if digits else f[0]
        else:
            v = f
        patch = with_param(patch, f"s {k} {v}")
    return patch


def values_for(row):
    """Two values that differ from the default, as .inf text; [] when the param is not swept."""
    kind, d = row["kind"], row.get("default")
    lo, hi = row.get("min"), row.get("max")
    if kind == "bool":
        return ["0" if str(d) in ("1", "true") else "1"]
    if kind == "color":
        return ["1 0 0", "0 0.3 1"]
    if kind == "int":
        dv = int(float(d or 0))
        opts = row.get("options") or []
        if opts:
            n = len(opts)
            vals = {(dv + 1) % n, (dv + n // 2) % n} - {dv}
            return [str(v) for v in sorted(vals)]
        if lo is not None and hi is not None and hi > lo:
            span = max(1, int((hi - lo) / 4))
            vals = {min(hi, dv + span), max(lo, dv - span)} - {dv}
            return [str(int(v)) for v in sorted(vals)] or [str(int(hi))]
        return [str(dv + 1), str(dv + 4)]
    if kind == "float":
        dv = float(d or 0)
        if lo is not None and hi is not None and hi > lo:
            span = (hi - lo) / 4
            vals = [min(hi, dv + span), max(lo, dv - span)]
            vals = [v for v in vals if abs(v - dv) > 1e-6] or [hi if abs(hi - dv) > 1e-6 else lo]
        else:
            vals = [dv * 2 + 0.5, dv * 0.5 - 0.5]
        return [f"{v:.6g}" for v in vals]
    return []


def with_param(patch, line):
    head = patch.split("\nend", 1)
    return head[0] + "\n  " + line + "\nend" + head[1]


def with_mod(patch, row):
    lo, hi = row.get("min"), row.get("max")
    rng = f" {lo:.6g} {hi:.6g} 1" if lo is not None and hi is not None and hi > lo else ""
    lfo = f"node 900 {LFO}\n  f rate 1.7\nend\n"
    return patch.rstrip("\n") + "\n" + lfo + f"mod 1 {row['modulatable_index']} 900 0 0 1 0{rng}\n"


LINKS = ("cable", "note", "aud")


def bare(patch):
    """The patch with every link into node 1 removed (cables, notes, audio): the node on its own."""
    def into_node1(l):
        w = l.split()
        return len(w) > 1 and w[0] in LINKS and w[1] == "1"
    return "".join(l for l in patch.splitlines(True) if not into_node1(l))


def is_switch(row):
    return row["kind"] == "bool" or (row["kind"] == "int" and row.get("options"))


MAX_CONTEXTS = 12


def sidecar(out):
    """The JSON a batch job leaves: <dir>.json for an image job's out_dir/, the out .json itself for audio."""
    return out if out.endswith(".json") else out.rstrip("/") + ".json"


def safe(s):
    return "".join(c if c.isalnum() else "_" for c in s)


class Sweep:
    def __init__(self, a):
        self.bin, self.out, self.procs, self.chunk, self.verbose = a.bin, a.out, a.procs, a.chunk, a.verbose
        self.deadline = time.time() + a.budget * 60 if a.budget else None
        self.out_of_time = False

    # ---- job generation -------------------------------------------------
    def jobs_for(self, t):
        """(job list, per-param plan) for one type. A job is (id, patch text, channel)."""
        patch, kind = build(t)
        patch = with_content(patch, t)
        # Seeded nodes (LFO, Random) draw their seed from a process-wide spawn counter when the patch omits it,
        # so a seedless patch renders differently by its place in a --batch: pin the default seed.
        for row in t.get("rows", []):
            if row["key"] == "seed" and row.get("default") is not None:
                patch = with_param(patch, f"{row['tag']} seed {float(row['default']):.6g}")
        chan = "audio" if kind in ("audio", "note") else "image"
        d = os.path.join(self.out, "types", safe(t["type"]))
        jobs = [("defA", patch), ("defB", patch)]
        plan = []
        for i, row in enumerate(t.get("rows", [])):
            p = {"key": row["key"], "kind": row["kind"], "ui": row.get("ui"), "jobs": [], "mod": None}
            if row["kind"] == "text":
                p["class"] = "text"
            else:
                for k, v in enumerate(values_for(row)):
                    jid = f"p{i}v{k}"
                    jobs.append((jid, with_param(patch, f"{row['tag']} {row['key']} {v}")))
                    p["jobs"].append((jid, v))
                if row.get("modulatable_index") is not None:
                    jid = f"p{i}m"
                    jobs.append((jid, with_mod(patch, row)))
                    p["mod"] = jid
            plan.append(p)
        jobs.append(("defC", patch))  # after every variant: catches state leaking between batch jobs
        return d, chan, kind, jobs, plan

    # ---- running ----------------------------------------------------------
    def out_path(self, d, jid, chan):
        return os.path.join(d, jid + ("/" if chan == "image" else ".json"))

    def done(self, d, jid, chan):
        if chan == "image":
            return os.path.exists(os.path.join(d, jid + ".json"))
        return os.path.exists(os.path.join(d, jid + ".json"))

    def run_chunk(self, chan, lines):
        """lines: [(patch file, out, wav)]. Runs them in one process; a crash restarts after the crashing line."""
        failed = []
        while lines:
            if self.deadline and time.time() > self.deadline:
                self.out_of_time = True  # leave the rest unrendered: the next run resumes from the sidecars
                return failed
            first, rest = lines[0], lines[1:]
            lst = sidecar(first[1]) + ".batch.txt"
            with open(lst, "w") as f:
                for p, o, w in rest:
                    f.write(f"{p}\t{o}" + (f"\t{w}" if w else "") + "\n")
            # --timeout (seconds) covers the whole batch process: ~3 s a job is 10x the slowest seen
            budget = str(30 + 3 * len(lines))
            if chan == "image":
                cmd = [self.bin, "--frame", first[0], TIMES, first[1], "--size", SIZE, "--lenient",
                       "--timeout", budget, "--batch", lst]
            else:
                cmd = [self.bin, "--audio-summary", first[0], first[1], "--duration", AUDIO_SECONDS,
                       "--wav", first[2], "--lenient", "--timeout", budget, "--batch", lst]
            t0 = time.time()
            try:
                p = subprocess.run(cmd, capture_output=True, text=True, timeout=60 + 4 * len(lines))
                tail = (p.stdout.strip().splitlines() or [""])[-1]
            except subprocess.TimeoutExpired:
                tail = "TIMEOUT"
            if self.verbose:
                print(f"    [{chan}] {len(lines)} jobs from {os.path.basename(first[0])}: {time.time() - t0:.1f} s  {tail[:160]}", flush=True)
            os.remove(lst)
            sc = sidecar(first[1])
            if not os.path.exists(sc) and tail.startswith("{"):
                # the first (command-line) job failed and the app stopped: keep its error report as the sidecar
                try:
                    rep = json.loads(tail)
                    rep.pop("attribution", None)
                    with open(sc, "w") as f:
                        json.dump(rep, f)
                except (ValueError, NameError):
                    pass
            left = [x for x in lines if not os.path.exists(sidecar(x[1]))]
            if left:
                failed.append(left[0])  # the process stopped here: crash, timeout or a fatal first-job error
                left = left[1:]
            lines = left
        return failed

    # ---- comparing -------------------------------------------------------
    @staticmethod
    def load_image(d, jid):
        try:
            stats = json.load(open(os.path.join(d, jid + ".json")))["frame_stats"]
            if len(stats) != 2:
                return None
            return [np.asarray(Image.open(s["file"]).convert("RGBA"), np.float32) for s in stats]
        except Exception:
            return None

    @staticmethod
    def load_audio(d, jid):
        try:
            with wave.open(os.path.join(d, jid + ".wav")) as w:
                n, sw = w.getnframes(), w.getsampwidth()
                raw = w.readframes(n)
            dt = {2: np.int16, 4: np.int32}.get(sw)
            if dt is None:
                return None
            return [np.frombuffer(raw, dt).astype(np.float32) / float(np.iinfo(dt).max)]
        except Exception:
            # 32-bit float WAVs: the wave module refuses format 3; read the data chunk directly
            try:
                b = open(os.path.join(d, jid + ".wav"), "rb").read()
                i = b.find(b"data")
                return [np.frombuffer(b[i + 8:], np.float32)]
            except Exception:
                return None

    @staticmethod
    def diff(a, b):
        if a is None or b is None:
            return None
        if len(a) != len(b) or any(x.shape != y.shape for x, y in zip(a, b)):
            return float("inf")
        return max(float(np.abs(x - y).mean()) for x, y in zip(a, b))

    def analyse(self, t, d, chan, kind, plan):
        load = self.load_image if chan == "image" else self.load_audio
        cache = {}
        get = lambda j: cache.setdefault(j, load(d, j))
        A, B, C = get("defA"), get("defB"), get("defC")
        row = {"type": t["type"], "cat": t["category"], "out": kind, "channel": chan, "params": []}
        if A is None:
            row["error"] = "default-render-failed"
            err = os.path.join(d, "defA.json")
            if os.path.exists(err):
                try:
                    row["error_detail"] = json.load(open(err)).get("errors", [])[:1]
                except ValueError:
                    pass
            # every param of a type that does not render by default counts as an error, not as missing
            row["params"] = [{"key": q["key"], "kind": q["kind"], "ui": q["ui"],
                              "class": "text" if q.get("class") == "text" else "error"} for q in plan]
            return row
        noise = max(self.diff(A, B) or 0, self.diff(A, C) or 0)
        row["noise"] = round(noise, 5)
        if self.diff(A, C) and self.diff(A, C) > (self.diff(A, B) or 0) * 3 + 1e-6:
            row["batch_leak"] = round(self.diff(A, C), 5)
        thr = max(noise * 3, 0.02 if chan == "image" else 1e-5)
        row["threshold"] = thr
        # a silent or blank default can't show an effect: its untested params are silent-default, not no-effect
        level = (float(np.sqrt((A[0] ** 2).mean())) if chan == "audio"
                 else float(np.mean([a[..., :3].std() for a in A])))
        row["default_level"] = round(level, 5)
        silent = level < (1e-4 if chan == "audio" else 0.5)
        if silent:
            row["silent_default"] = True
        for p in plan:
            r = {"key": p["key"], "kind": p["kind"], "ui": p["ui"]}
            if p.get("class") == "text":
                r["class"] = "text"
            else:
                ds = [self.diff(A, get(j)) for j, _ in p["jobs"]]
                md = self.diff(A, get(p["mod"])) if p["mod"] else None
                if p["jobs"] and all(x is None for x in ds):
                    r["class"] = "error"
                elif any(x is not None and x > thr for x in ds):
                    r["class"] = "effective"
                elif md is not None and md > thr:
                    r["class"] = "mod-only"
                else:
                    r["class"] = "silent-default" if silent else "no-effect"
                r["delta"] = [None if x is None else round(min(x, 1e9), 4) for x in ds]
                if p["mod"]:
                    r["mod"] = "error" if md is None else ("mod-effective" if md > thr else "mod-no-effect")
                    r["mod_delta"] = None if md is None else round(min(md, 1e9), 4)
                else:
                    r["mod"] = "not-modulatable"
            row["params"].append(r)
        return row

    # ---- rendering rounds ----------------------------------------------
    def render(self, staged_jobs, label):
        """staged_jobs: [(dir, chan, [(jid, patch text)])]. Writes the patches and renders what is not done,
        one chunk per run of whole types (a type larger than --chunk is a chunk of its own)."""
        chunks = {"image": [[]], "audio": [[]]}
        for d, chan, jobs in staged_jobs:
            os.makedirs(d, exist_ok=True)
            lines = []
            for jid, text in jobs:
                pf = os.path.join(d, jid + ".inf")
                with open(pf, "w") as f:
                    f.write(text)
                if not self.done(d, jid, chan):
                    wav = os.path.join(d, jid + ".wav") if chan == "audio" else ""
                    lines.append((pf, self.out_path(d, jid, chan), wav))
            if not lines:
                continue
            cur = chunks[chan][-1]
            if cur and len(cur) + len(lines) > self.chunk:
                chunks[chan].append([])
            chunks[chan][-1].extend(lines)
        flat = [(c, l) for c, ls in chunks.items() for l in ls if l]
        total = sum(len(l) for _, l in flat)
        print(f"[{label}] {total} renders in {len(flat)} chunks, {self.procs} processes", flush=True)
        if not flat:
            return []
        t0, done, failed = time.time(), 0, []
        with cf.ThreadPoolExecutor(self.procs) as ex:
            futs = {ex.submit(self.run_chunk, c, l): len(l) for c, l in flat}
            for f in cf.as_completed(futs):
                done += futs[f]
                failed += f.result()
                el = time.time() - t0
                print(f"  {done}/{total} renders, {el:.0f} s, {el / max(done, 1) * 1000:.0f} ms each", flush=True)
        return failed

    def contexts_for(self, t, base, dead):
        """[(label, context patch)] for a type whose params in `dead` (row indices) showed no effect:
        bare (no links in), every option of short menus (<= 6; else the two sweep values), bools flipped,
        and knobs that default to their minimum ("off" by default: unison 1, depth 0) set to mid-range."""
        out = []
        b = bare(base)
        if b != base:
            out.append(("bare", b))
        rows = t.get("rows", [])
        for i, row in enumerate(rows):
            if not is_switch(row) or (i in dead and len(dead) == 1):
                continue
            opts = row.get("options") or []
            d = int(float(row.get("default") or 0))
            vals = [str(k) for k in range(len(opts)) if k != d] if 0 < len(opts) <= 6 else values_for(row)
            for v in vals:
                out.append((f"{row['key']}={v}", with_param(base, f"{row['tag']} {row['key']} {v}")))
        for i, row in enumerate(rows):
            lo, hi, d = row.get("min"), row.get("max"), row.get("default")
            if row["kind"] not in ("int", "float") or is_switch(row) or lo is None or hi is None or hi <= lo:
                continue
            if d is None or abs(float(d) - lo) > 1e-6 or (i in dead and len(dead) == 1):
                continue
            mid = (lo + hi) / 2
            v = str(int(round(mid))) if row["kind"] == "int" else f"{mid:.6g}"
            out.append((f"{row['key']}={v}", with_param(base, f"{row['tag']} {row['key']} {v}")))
        return out[:MAX_CONTEXTS]

    def context_pass(self, staged, rows_by_type):
        """Rounds 2 and 3: re-test no-effect params under other switch settings and with no input feeds."""
        work, plans = [], []
        for t, d, chan, kind, plan, base in staged:
            row = rows_by_type.get(t["type"])
            if not row or row.get("error"):
                continue
            dead = [i for i, p in enumerate(row["params"]) if p["class"] == "no-effect"]
            if not dead:
                continue
            ctxs = self.contexts_for(t, base, set(dead))
            rws = t["rows"]
            jobs, cplan = [], []
            for label, cp in ctxs:
                moved = [i for i in dead if not label.startswith(rws[i]["key"] + "=")]
                vals = {i: values_for(rws[i]) for i in moved}
                moved = [i for i in moved if vals[i]]
                if not moved:
                    continue
                allp = cp
                for i in moved:
                    allp = with_param(allp, f"{rws[i]['tag']} {rws[i]['key']} {vals[i][0]}")
                j = "c_" + safe(label)
                jobs += [(f"{j}base", cp), (f"{j}all", allp), (f"{j}base2", cp)]
                cplan.append((j, label, cp, moved, vals))
            if jobs:
                work.append((d, chan, jobs))
                plans.append((t, d, chan, row, cplan))
        if not work:
            return
        self.render(work, "contexts")

        # round 3: per-param renders only in the contexts where moving them all changed the output
        work3, checks = [], []
        for t, d, chan, row, cplan in plans:
            load = self.load_image if chan == "image" else self.load_audio
            thr = row["threshold"]
            jobs = []
            for j, label, cp, moved, vals in cplan:
                cthr = max(thr, 3 * (self.diff(load(d, f"{j}base"), load(d, f"{j}base2")) or 0))
                row.setdefault("context_threshold", {})[label] = round(min(cthr, 1e9), 5)
                dv = self.diff(load(d, f"{j}base"), load(d, f"{j}all"))
                if dv is None or dv <= cthr:
                    continue
                for i in moved:
                    jobs.append((f"{j}p{i}", with_param(cp, f"{t['rows'][i]['tag']} {t['rows'][i]['key']} {vals[i][0]}")))
                    checks.append((row, d, chan, j, i, label))
            if jobs:
                work3.append((d, chan, jobs))
        self.render(work3, "per-param in contexts")
        for row, d, chan, j, i, label in checks:
            load = self.load_image if chan == "image" else self.load_audio
            dv = self.diff(load(d, f"{j}base"), load(d, f"{j}p{i}"))
            p = row["params"][i]
            if dv is not None and dv > row["context_threshold"][label]:
                p["class"] = "conditional"
                p.setdefault("needs", []).append(label)
                p["context_delta"] = max(p.get("context_delta", 0), round(min(dv, 1e9), 4))

    # ---- main ------------------------------------------------------------
    def run(self, types, contexts=True):
        rows, staged, work = [], [], []
        for t in types:
            if t.get("hardware_driven"):
                rows.append({"type": t["type"], "cat": t["category"], "live_only": True,
                             "params": [{"key": r["key"], "kind": r["kind"], "class": "live-only"} for r in t.get("rows", [])]})
                continue
            d, chan, kind, jobs, plan = self.jobs_for(t)
            work.append((d, chan, jobs))
            staged.append((t, d, chan, kind, plan, jobs[0][1]))
        failed = self.render(work, "main")
        with open(os.path.join(self.out, "crashed.txt"), "w") as f:
            f.writelines(x[0] + "\n" for x in failed)
        if self.out_of_time:
            print("budget used up in the main round: re-run the same command to resume", flush=True)
            return None

        swept = {}
        for t, d, chan, kind, plan, base in staged:
            swept[t["type"]] = self.analyse(t, d, chan, kind, plan)
        if contexts:
            self.context_pass(staged, swept)
            if self.out_of_time:
                print("budget used up in the context pass: re-run the same command to resume", flush=True)
                return None
        rows += swept.values()
        rows.sort(key=lambda r: (r["cat"], r["type"]))
        with open(os.path.join(self.out, "coverage.json"), "w") as f:
            json.dump(rows, f, indent=1)
        return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(ROOT, "build-dev/Infinite.app/Contents/MacOS/Infinite"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build/headless-coverage"))
    ap.add_argument("--describe", help="reuse a describe.json instead of asking the app (~70 s)")
    ap.add_argument("--procs", type=int, default=3)
    ap.add_argument("--chunk", type=int, default=150)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--budget", type=float, help="minutes; stop starting chunks after this (resume by re-running)")
    ap.add_argument("--category", action="append", help="sweep only these categories (repeatable)")
    ap.add_argument("--skip-category", action="append", help="skip these categories (repeatable)")
    ap.add_argument("--no-contexts", action="store_true", help="skip the context pass (rounds 2 and 3)")
    ap.add_argument("types", nargs="*")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    schema = a.describe or os.path.join(a.out, "describe.json")
    if not os.path.exists(schema):
        subprocess.run([a.bin, "--describe", "--json", schema], capture_output=True)
    types = json.load(open(schema))["types"]
    if a.types:
        types = [t for t in types if t["type"] in a.types]
    if a.category:
        types = [t for t in types if t["category"] in a.category]
    if a.skip_category:
        types = [t for t in types if t["category"] not in a.skip_category]
    rows = Sweep(a).run(types, contexts=not a.no_contexts)
    if rows is None:
        sys.exit(3)
    subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "report.py"),
                    os.path.join(a.out, "coverage.json")])
    print(f"{len(rows)} types -> {os.path.join(a.out, 'coverage.json')}")


if __name__ == "__main__":
    main()
