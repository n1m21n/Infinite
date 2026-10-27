#!/usr/bin/env python3
"""data_universe.py: the one entry point per machine for the data universe (README step 2m).

    data_universe.py ROOT [ROOT ...] [--budget 20] [--only presets,audio,...] [--owner-root DIR] [--dry-run]
    data_universe.py --status [ROOT ...]          what is left on this machine (rescans the roots if given)

One walk of the roots (read only). Each file is identified by its magic bytes (extension as the fallback) and
sent to an existing reader, which is run as a subprocess and not rewritten:

  reader               takes                                          how it is called
  preset_reader        Spectrasonics, Ableton .adv/.adg, u-he .h2p,   staged: a tree of symlinks to only the new
                       Alchemy .acp, Logic .pst                       or changed files (rows are mapped back)
  plain_preset_reader  nksf, Valhalla, AU, Phase Plant, Vital, ...    staged, with --clips for Ableton .alc
  binary_preset_reader .agr, .bwpreset, .ngrr/.nkp, .vital            staged
  wt_reader            .wt wavetables                                 staged
  als_reader           Ableton .als sets                              staged, MIDI goes to inbox/<host>_als_*
  exs_reader           Logic/GarageBand .exs                          whole top folder (it resolves sample names)
  steam_reader         Spectrasonics .db2 / .omnisphere               whole top folder
  spitfire_reader      LABS .spitfire/.z*, Kontakt .nkc               whole top folder
  midi copy            .mid                                           copied to inbox/<host>_<folder> for midi_ingest
  audio_features       wav/aiff/flac/ogg/mp3/m4a                      --list --resume --budget (chunked, resumable)

Manifest: training-data/manifest/<host>.jsonl, one line per file: path, size, mtime, reader, rows, status. A re-run
only reads files that are new or whose size or mtime changed. Every format with no reader goes, aggregated by
(extension, first 16 bytes) with a count, to manifest/unknown_<host>.jsonl as the triage list.

Rules it keeps: source roots are read only; only rows leave the machine (copy rows, never audio); a run is capped
by --budget minutes and runs one reader at a time; one heavy job per machine (it refuses to start while another
audio_features / midi_ingest / render_pass runs, unless --force); every row carries a licence; the existing outputs
(presets.jsonl, features.jsonl ...) are never overwritten: reader rows go to training-data/universe/<host>/.
'Not music' comes from collect_check.py (NOT_MUSIC_TOP, LOCKED, NOT_PARSED ...)."""
import argparse, collections, hashlib, json, os, re, shutil, socket, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import collect_check as cc  # the notion of 'not music', locked, not parsed: one copy

TD = os.path.expanduser(os.environ.get("DATA_UNIVERSE_TD", "~/Infinite/training-data"))  # the env var is for tests
HOST = socket.gethostname().split(".")[0]
SKIP_DIRS = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100", "node_modules",
             ".git", "__pycache__", "site-packages", "Caches", "Backup"}
# on the Air (README 'Not music'): app data that is not music, skipped at any depth
NOT_MUSIC_ANYWHERE = {"claude", "imazing", "adobe", "blender", "derivative", "chatglm", "wallpapers", "wd discovery"}
NOT_MUSIC_EXT = {".js", ".pyc", ".ts", ".py", ".map", ".png", ".jpg", ".jpeg", ".gif", ".heic", ".webp", ".svg", ".mp4", ".mov",
                 ".mkv", ".pdf", ".html", ".css", ".dylib", ".so", ".o", ".a", ".h", ".c", ".cpp", ".plist", ".strings",
                 ".nib", ".car", ".icns", ".ttf", ".otf", ".woff", ".woff2", ".ds_store", ".lproj", ".txt", ".md", ".log"}
AUDIO = cc.AUDIO
MIDI_EXT = {".mid", ".midi"}
SPECTRA = (".prt_omn", ".mlt_omn", ".prt_key", ".prt_trl", ".mlt_trl", ".mlt_key")
PRESET_EXT = set(SPECTRA) | {".adv", ".adg", ".h2p", ".h2pv", ".acp", ".pst"}
PLAIN_EXT = {".cst", ".vital", ".serumpreset", ".phaseplant", ".multipass", ".snapheap", ".nksf", ".ffp", ".xlfo", ".alc"}
BINARY_EXT = {".agr": "agr", ".bwpreset": "bwpreset", ".ngrr": "guitarrig", ".nkp": "guitarrig", ".vital": "vitalwt"}
SPITFIRE_EXT = {".spitfire", ".zmulti", ".zpreset", ".zconfig", ".nkc"}
STEAM_EXT = {".db2", ".omnisphere"}
WHOLE = {"exs_reader": {".exs"}, "spitfire_reader": SPITFIRE_EXT, "steam_reader": STEAM_EXT}  # readers that take a whole folder
ORDER = ["preset_reader", "plain_preset_reader", "binary_preset_reader", "wt_reader", "als_reader", "exs_reader",
         "steam_reader", "spitfire_reader", "midi_copy", "audio_features"]
GROUPS = {"presets": ["preset_reader", "plain_preset_reader", "binary_preset_reader"], "wt": ["wt_reader"], "als": ["als_reader"],
          "samplers": ["exs_reader", "steam_reader", "spitfire_reader"], "midi": ["midi_copy"], "audio": ["audio_features"]}
DONE = {"read", "empty", "copied", "skipped", "locked", "not_parsed", "incomplete", "unknown", "error"}
AUDIO_FEATURE_FILES = ["audio/features.jsonl", "audio/air_features.jsonl"]  # existing passes: their files count as read
STAGE_EXT = {"preset_reader", "plain_preset_reader", "binary_preset_reader", "wt_reader", "als_reader"}


def host_slug(host=HOST):
    h = host.lower()
    return "air" if "air" in h else "pro" if "pro" in h else re.sub(r"[^a-z0-9]+", "_", h).strip("_")


def slug(s):
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")[:60]


def paths(host=HOST):
    m = os.path.join(TD, "manifest")
    return {"manifest": os.path.join(m, host + ".jsonl"), "unknown": os.path.join(m, "unknown_%s.jsonl" % host),
            "state": os.path.join(m, host + ".state.json"), "lock": os.path.join(m, ".lock-" + host),
            "work": os.path.join(TD, "universe", host)}


# ---------------------------------------------------------------- identify and route
def kind_of(head):
    """what the first bytes say, or None when they say nothing we know"""
    if head[:4] == b"MThd":
        return "midi"
    if head[:4] == b"RIFF":
        return "audio" if head[8:12] in (b"WAVE", b"RMP3") else "nksf" if head[8:12] == b"NIKS" else "riff"
    if head[:4] == b"FORM" and head[8:12] in (b"AIFF", b"AIFC"):
        return "audio"
    if head[:4] in (b"fLaC", b"OggS", b"caff") or head[:3] == b"ID3" or head[4:8] == b"ftyp" or head[:2] in (b"\xff\xfb", b"\xff\xf3", b"\xff\xf2"):
        return "audio"
    if head[:2] == b"\x1f\x8b":
        return "gzip"
    if head[:4] == b"vawt":
        return "wt"
    if head[16:20] in (b"JBOS", b"TBOS", b"SOBT", b"SOBJ"):
        return "exs"
    return None


def route(kind, ext, path):
    """-> (readers, status, reason). Magic first, the extension where the magic is silent."""
    if kind == "midi":
        return ["midi_copy"], None, None
    if kind == "audio":
        return ["audio_features"], None, None
    if kind == "wt":
        return ["wt_reader"], None, None
    if kind == "exs":
        return ["exs_reader"], None, None
    if kind == "nksf":
        return ["plain_preset_reader"], None, None
    if ext in MIDI_EXT:
        return ["midi_copy"], None, None
    if ext in AUDIO:
        return ["audio_features"], None, None
    if ext == ".als":
        return ["als_reader"], None, None
    if ext == ".wt":
        return ["wt_reader"], None, None
    if ext == ".exs":
        return ["exs_reader"], None, None
    if ext in SPITFIRE_EXT:
        return ["spitfire_reader"], None, None
    if ext in STEAM_EXT:
        return ["steam_reader"], None, None
    if ext == ".db":
        if "/Patches/" in path or "/Multis/" in path:
            return ["preset_reader"], None, None
        if any(("/" + t + "/") in path for t in cc.SPECTRA_TOPS):
            return ["steam_reader"], None, None
        return [], "not_parsed", cc.LOCKED[".db_app"]
    rs = []
    if ext in PRESET_EXT:
        rs.append("preset_reader")
    if ext in PLAIN_EXT:
        rs.append("plain_preset_reader")
    if ext in BINARY_EXT:
        rs.append("binary_preset_reader")
    if rs:
        return rs, None, None
    if ext in cc.LOCKED:
        return [], "locked", cc.LOCKED[ext]
    if ext in cc.NOT_PARSED:
        return [], "not_parsed", cc.NOT_PARSED[ext]
    if ext in cc.INCOMPLETE:
        return [], "incomplete", "unfinished download"
    return [], "unknown", None


# ---------------------------------------------------------------- manifest
def load_manifest(p):
    """{path: (size, mtime, status, reader, rows)}, last line per path wins; compacts a file that is mostly stale lines"""
    m, lines = {}, 0
    if os.path.exists(p["manifest"]):
        for ln in open(p["manifest"]):
            try:
                r = json.loads(ln)
            except ValueError:
                continue
            lines += 1
            m[r["path"]] = (r["size"], r["mtime"], r["status"], r.get("reader"), r.get("rows"))
    if lines > 3 * max(len(m), 1000):
        tmp = p["manifest"] + ".tmp"
        with open(tmp, "w") as f:
            for k, (sz, mt, st, rd, rw) in m.items():
                f.write(json.dumps({"path": k, "size": sz, "mtime": mt, "reader": rd, "rows": rw, "status": st}, separators=(",", ":")) + "\n")
        os.replace(tmp, p["manifest"])
    return m


def adopt_audio():
    """srcs already measured by earlier audio passes (their own files stay where they are)"""
    done = set()
    rx = re.compile(r'"src":"((?:[^"\\]|\\.)*)"')
    for rel in AUDIO_FEATURE_FILES + ["audio/%s_features.jsonl" % HOST]:
        f = os.path.join(TD, rel)
        if os.path.exists(f):
            for ln in open(f, errors="replace"):
                mm = rx.search(ln)
                if mm:
                    try:
                        done.add(json.loads('"%s"' % mm[1]))
                    except ValueError:
                        pass
    return done


# ---------------------------------------------------------------- scan
def scan(roots, man, a, adopted):
    """one walk. -> plan {reader: [(path, size, mtime, root)]}, counters, unknown aggregate, manifest lines to add"""
    plan = collections.defaultdict(list)
    n = collections.Counter()
    unk = {}
    add = []
    t0 = time.time()
    for root in roots:
        root = os.path.abspath(os.path.expanduser(root))
        for d, subs, fs in os.walk(root):
            if d == root:
                subs[:] = [s for s in subs if s not in cc.NOT_MUSIC_TOP]
            subs[:] = sorted(s for s in subs if s not in SKIP_DIRS and not s.startswith(".") and s.lower() not in NOT_MUSIC_ANYWHERE)
            audio_here = []
            for f in sorted(fs):
                if f.startswith("."):
                    continue
                p = os.path.join(d, f)
                ext = os.path.splitext(f)[1].lower()
                if ext in NOT_MUSIC_EXT:
                    n["skip: not music (extension)"] += 1
                    continue
                try:
                    st = os.lstat(p)
                except OSError:
                    continue
                if not os.path.isfile(p) or os.path.islink(p):
                    continue
                size, mt = st.st_size, int(st.st_mtime)
                n["files"] += 1
                m = man.get(p)
                if m and m[0] == size and m[1] == mt and m[2] in DONE:
                    n["fresh (already in manifest)"] += 1
                    continue
                if ext in AUDIO and p in adopted:
                    add.append({"path": p, "size": size, "mtime": mt, "reader": "audio_features", "rows": 1, "status": "read", "adopted": True})
                    n["fresh (earlier audio pass)"] += 1
                    continue
                try:
                    with open(p, "rb") as fh:
                        head = fh.read(16)
                except OSError:
                    n["unreadable"] += 1
                    continue
                readers, status, reason = route(kind_of(head), ext, p)
                if readers == ["audio_features"]:
                    audio_here.append((p, size, mt, root))
                    continue
                if readers:
                    for r in readers:
                        plan[r].append((p, size, mt, root))
                    continue
                e = {"path": p, "size": size, "mtime": mt, "reader": None, "rows": 0, "status": status}
                if reason:
                    e["reason"] = reason
                add.append(e)
                n["%s" % status] += 1
                if status == "unknown":
                    k = (ext or "(none)", head.hex())
                    u = unk.setdefault(k, {"extension": k[0], "first16": k[1], "size": 0, "count": 0, "example": p})
                    u["count"] += 1
                    u["size"] += size
            # audio: at most --per-folder files per folder, spread over the folder (the rest are sampled out, as in audio_features)
            if a.per_folder and len(audio_here) > a.per_folder:
                keep = [audio_here[i * len(audio_here) // a.per_folder] for i in range(a.per_folder)]
                for p, size, mt, rt in audio_here:
                    if (p, size, mt, rt) not in keep:
                        add.append({"path": p, "size": size, "mtime": mt, "reader": "audio_features", "rows": 0, "status": "skipped",
                                    "reason": "sampled out (per-folder %d)" % a.per_folder})
                        n["skip: audio sampled out"] += 1
                audio_here = keep
            plan["audio_features"] += audio_here
    n["scan seconds"] = int(time.time() - t0)
    return plan, n, unk, add


# ---------------------------------------------------------------- running readers
def lic(path, owner_roots):
    return "owner" if any(path.startswith(o) for o in owner_roots) else "sample pack, local only"


def run(cmd, seconds):
    """a reader as a subprocess with a hard time limit; -> (ok, output tail)"""
    try:
        r = subprocess.run([sys.executable] + cmd, capture_output=True, text=True, timeout=max(30, seconds))
        return r.returncode == 0, (r.stdout + r.stderr)[-1500:]
    except subprocess.TimeoutExpired:
        return False, "timeout"


def finalize(tmp, final, repl, owner_roots):
    """stream a reader's rows into the final file: map staged paths back, add a licence when missing -> {src: rows}"""
    per = collections.Counter()
    if not os.path.exists(tmp):
        return per
    with open(tmp, errors="replace") as fi, open(final, "a") as fo:
        for ln in fi:
            for a_, b_ in repl:
                ln = ln.replace(a_, b_)
            try:
                r = json.loads(ln)
            except ValueError:
                continue
            src = r.get("src") if isinstance(r.get("src"), str) else ""
            if "license" not in r and "licence" not in r:
                r["license"] = lic(src, owner_roots)
            fo.write(json.dumps(r, separators=(",", ":")) + "\n")
            per[src.split("#")[0]] += 1
    return per


def stage(files, root_map, sdir):
    """symlink only these files into sdir/r<i>/<root name>/<relative path>: the reader walks a tree of just the todo"""
    repl = []
    for i, (root, base) in enumerate(root_map):
        repl.append((json.dumps(os.path.join(sdir, "r%d" % i, base))[1:-1], json.dumps(root.rstrip("/"))[1:-1]))
    for p, size, mt, root in files:
        i = [r for r, _ in root_map].index(root)
        dest = os.path.join(sdir, "r%d" % i, root_map[i][1], os.path.relpath(p, root))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        try:
            os.symlink(p, dest)
        except FileExistsError:
            pass
    return repl


def container(path, root):
    rel = os.path.relpath(path, root).split(os.sep)
    return os.path.join(root, rel[0]) if len(rel) > 1 else root


def run_reader(name, files, roots, a, W, left, owner_roots, tag):
    """-> (processed {path: (status, rows)}, note). left() returns seconds still allowed in this run"""
    script = os.path.join(HERE, name + ".py")
    final_dir = os.path.join(W["work"], name)
    os.makedirs(final_dir, exist_ok=True)
    root_map = []
    for r in roots:
        b = os.path.basename(r.rstrip("/")) or "root"
        root_map.append((r, b))
    res, notes = {}, []
    if name in STAGE_EXT:
        sdir = os.path.join(W["work"], "stage", name)
        shutil.rmtree(sdir, ignore_errors=True)
        repl = stage(files, root_map, sdir)
        sroots = [os.path.join(sdir, "r%d" % i, b) for i, (_, b) in enumerate(root_map) if os.path.isdir(os.path.join(sdir, "r%d" % i, b))]
        tmp = os.path.join(W["work"], name + ".tmp.jsonl")
        budget = max(60, left())
        if name == "preset_reader":
            cmd = [script] + sroots + ["--out", tmp]
        elif name == "plain_preset_reader":
            cmd = [script] + sroots + ["--out", tmp, "--budget", str(budget), "--clips", os.path.join(TD, "inbox", host_slug() + "_ableton_clips")]
        elif name == "binary_preset_reader":
            tdir = os.path.join(W["work"], name + "_tmp")
            os.makedirs(tdir, exist_ok=True)
            cmd = [script, "--root"] + sroots + ["--out", tdir, "--budget", str(budget)]
        elif name == "wt_reader":
            cmd = [script] + sroots + ["--out", tmp]
        else:  # als_reader
            tdir = os.path.join(W["work"], "als_inbox")
            cmd = [script] + sroots + ["--inbox", tdir, "--arcs", tmp, "--budget", str(budget)]
            if any(r in owner_roots for r in roots):
                cmd.append("--owner")
        ok, out = run(cmd, budget + 120)
        partial = "budget hit" in out
        per = collections.Counter()
        final = os.path.join(final_dir, "%s_%s.jsonl" % (name, tag))
        if name == "binary_preset_reader":
            for k in sorted(os.listdir(tdir)):
                if k.endswith(".jsonl"):
                    per.update(finalize(os.path.join(tdir, k), final, repl, owner_roots))
            shutil.rmtree(tdir, ignore_errors=True)
        else:
            per.update(finalize(tmp, final, repl, owner_roots))
            if os.path.exists(tmp):
                os.remove(tmp)
        if name == "als_reader":  # MIDI written for midi_ingest goes to inbox/<host>_als_*
            for who in ("als_owner", "als_packs"):
                src = os.path.join(tdir, who)
                if os.path.isdir(src):
                    dst = os.path.join(TD, "inbox", host_slug() + "_" + who)
                    os.makedirs(dst, exist_ok=True)
                    for f in os.listdir(src):
                        shutil.move(os.path.join(src, f), os.path.join(dst, f))
            shutil.rmtree(tdir, ignore_errors=True)
        shutil.rmtree(sdir, ignore_errors=True)
        if not ok:
            return res, "failed: " + out[-300:].replace("\n", " | ")
        for p, size, mt, root in files:
            if per.get(p):
                res[p] = ("read", per[p])
            elif not partial:
                res[p] = ("empty", 0)  # the reader finished and this file gave no row (duplicate, unreadable, no data)
        return res, "partial: budget hit" if partial else ""
    # whole-folder readers: each top folder is one unit, its output file is a full snapshot that replaces the last one
    byc = collections.defaultdict(list)
    for f in files:
        byc[container(f[0], f[3])].append(f)
    for c, fl in sorted(byc.items()):
        if left() < 60:
            notes.append("out of time before %s" % c)
            break
        odir = os.path.join(final_dir, slug(os.path.basename(c)) or "root")
        tmp = os.path.join(odir + "_tmp")
        shutil.rmtree(tmp, ignore_errors=True)
        os.makedirs(tmp)
        if name == "exs_reader":
            cmd = [script, c, "--factory", "--out", os.path.join(tmp, "exs.jsonl")]
        else:
            cmd = [script, c, "--out-dir", tmp]
        ok, out = run(cmd, left() + 60)
        if not ok:
            notes.append("%s failed: %s" % (c, out[-200:].replace("\n", " | ")))
            shutil.rmtree(tmp, ignore_errors=True)
            continue
        shutil.rmtree(odir, ignore_errors=True)
        os.makedirs(odir)
        per = collections.Counter()
        for k in sorted(os.listdir(tmp)):
            if k.endswith(".jsonl"):
                per.update(finalize(os.path.join(tmp, k), os.path.join(odir, k), [], owner_roots))
        shutil.rmtree(tmp, ignore_errors=True)
        total = sum(per.values())
        for p, size, mt, root in fl:
            res[p] = ("read", per.get(p, 0) or None) if total else ("empty", 0)
    return res, "; ".join(notes)


def run_midi(files, a, W, left, owner_roots):
    res = {}
    hs = host_slug()
    made = set()
    for p, size, mt, root in files:
        if left() < 5:
            break
        top = os.path.relpath(p, root).split(os.sep)[0]
        d = os.path.join(TD, "inbox", "%s_%s" % (hs, slug(top if os.sep in os.path.relpath(p, root) else os.path.basename(root))))
        if d not in made:
            os.makedirs(d, exist_ok=True)
            sf = os.path.join(d, "SOURCE.txt")
            if not os.path.exists(sf):
                with open(sf, "w") as f:
                    f.write("license: %s\norigin: %s\n" % ("owner" if lic(p, owner_roots) == "owner" else "sample library, local only", HOST))
            made.add(d)
        if size > 4 << 20:
            res[p] = ("skipped", 0)
            continue
        h = hashlib.sha1(os.path.relpath(p, root).encode()).hexdigest()[:8]
        try:
            shutil.copyfile(p, os.path.join(d, "%s_%s" % (h, re.sub(r"[^A-Za-z0-9._-]+", "_", os.path.basename(p))[-120:])))
            res[p] = ("copied", 1)
        except OSError:
            res[p] = ("error", 0)
    return res


def run_audio(files, roots, a, W, left, out_path):
    lst = os.path.join(W["work"], "audio_list.txt")
    with open(lst, "w") as f:
        f.write("\n".join(p for p, *_ in files) + "\n")
    mins = max(1.0, left() / 60.0)
    cmd = [os.path.join(HERE, "audio_features.py")] + roots + ["--list", lst, "--resume", "--out", out_path, "--budget", "%.1f" % mins,
                                                               "--jobs", str(a.jobs), "--seconds", str(a.seconds)]
    ok, out = run(cmd, int(mins * 60) + 180)
    got = {}
    if os.path.exists(out_path):
        rx = re.compile(r'"src":"((?:[^"\\]|\\.)*)"')
        want = {json.dumps(p)[1:-1]: p for p, *_ in files}
        for ln in open(out_path, errors="replace"):
            mm = rx.search(ln)
            if mm and mm[1] in want:
                got[want[mm[1]]] = "error" if '"error"' in ln else "read"
    res = {p: (st, 1) for p, st in got.items()}
    return res, ("" if ok else "audio_features ended early: " + out[-200:].replace("\n", " | "))


# ---------------------------------------------------------------- guards
def other_heavy_jobs():
    r = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True, text=True).stdout
    me = str(os.getpid())
    hits = []
    for ln in r.splitlines():
        pid, _, cmdline = ln.strip().partition(" ")
        if pid != me and re.search(r"(audio_features|midi_ingest|render_pass|ibuild)\.py", cmdline) and "data_universe" not in cmdline:
            hits.append(ln.strip()[:120])
    return hits


def take_lock(p):
    if os.path.exists(p["lock"]):
        try:
            pid = int(open(p["lock"]).read().strip())
            os.kill(pid, 0)
            return False
        except (ValueError, OSError):
            pass
    os.makedirs(os.path.dirname(p["lock"]), exist_ok=True)
    open(p["lock"], "w").write(str(os.getpid()))
    return True


# ---------------------------------------------------------------- status
def status(p, roots, a, man):
    st = json.load(open(p["state"])) if os.path.exists(p["state"]) else {}
    todo = st.get("left", {})
    if roots:
        plan, n, unk, add = scan(roots, man, a, adopt_audio())
        todo = {k: len(v) for k, v in plan.items()}
        st.update(roots=roots, scanned=time.strftime("%Y-%m-%d %H:%M"), skipped=dict(n))
    elif not st:
        print("no scan yet on %s: run `data_universe.py ROOT ... --dry-run` first (or --status ROOT ...)" % HOST)
    per = collections.defaultdict(lambda: collections.Counter())
    for path, (sz, mt, status_, rd, rows) in man.items():
        per[rd or "(no reader)"][status_] += 1
        per[rd or "(no reader)"]["rows"] += rows or 0
    print("host %s   manifest %d files   roots %s   last scan %s" % (a.host, len(man), st.get("roots", "-"), st.get("scanned", "-")))
    print("\n%-22s %9s %9s %10s  %s" % ("reader", "read", "left", "rows", "other statuses"))
    for r in ORDER:
        c = per.get(r, collections.Counter())
        done = c["read"] + c["empty"] + c["copied"]
        other = ", ".join("%s %d" % kv for kv in c.items() if kv[0] not in ("read", "empty", "copied", "rows"))
        print("%-22s %9d %9s %10d  %s" % (r, done, todo.get(r, 0) if st.get("scanned") else "?", c["rows"], other))
    print("\nnot read on purpose / cannot be read yet:")
    why = collections.Counter()
    for path, (sz, mt, st_, rd, rows) in man.items():
        if rd is None and st_ in ("locked", "not_parsed", "incomplete", "unknown"):
            why[(st_, os.path.splitext(path)[1].lower() or "(none)")] += 1
    for (s, e), c in why.most_common(14):
        print("  %-11s %-14s %7d" % (s, e, c))
    if os.path.exists(p["unknown"]):
        agg = collections.Counter()
        for ln in open(p["unknown"]):
            try:
                r = json.loads(ln)
                agg[(r["extension"], r["first16"])] += r["count"]
            except (ValueError, KeyError):
                pass
        print("\nunknown formats (triage list %s):" % os.path.basename(p["unknown"]))
        for (e, h), c in agg.most_common(10):
            print("  %-12s %s  x%d" % (e, h[:32], c))
    if st.get("skipped"):
        print("\nlast scan skipped:", ", ".join("%s %d" % kv for kv in sorted(st["skipped"].items())))


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("roots", nargs="*")
    ap.add_argument("--status", action="store_true", help="print what is left on this machine (rescans ROOTs when given)")
    ap.add_argument("--dry-run", action="store_true", help="walk and classify only; write nothing")
    ap.add_argument("--budget", type=float, default=20, help="minutes for the whole run (default 20)")
    ap.add_argument("--only", help="comma list of: " + ",".join(GROUPS))
    ap.add_argument("--owner-root", action="append", default=[], help="a folder whose files are the owner's own (licence 'owner')")
    ap.add_argument("--per-folder", type=int, default=40, help="audio files per folder, 0 = every file")
    ap.add_argument("--audio-out", help="audio rows file (default training-data/audio/<host>_features.jsonl; pass audio/features.jsonl to continue the Pro's pass)")
    ap.add_argument("--jobs", type=int, default=2, help="audio_features workers")
    ap.add_argument("--seconds", type=float, default=30, help="seconds of each audio file analysed")
    ap.add_argument("--ingest", action="store_true", help="after the readers, run midi_ingest --resume on the inbox with the time left")
    ap.add_argument("--force", action="store_true", help="start although another heavy job is running")
    ap.add_argument("--host", default=HOST)
    a = ap.parse_args()
    p = paths(a.host)
    man = load_manifest(p)
    if a.status:
        return status(p, [os.path.abspath(os.path.expanduser(r)) for r in a.roots], a, man)
    if not a.roots:
        ap.error("give at least one ROOT (or --status)")
    roots = [os.path.abspath(os.path.expanduser(r)) for r in a.roots]
    for r in roots:
        if not os.path.isdir(r):
            sys.exit("not a folder: " + r)
    owner_roots = [os.path.abspath(os.path.expanduser(o)) for o in a.owner_root]
    want = set(sum((GROUPS[g] for g in a.only.split(",")), [])) if a.only else set(ORDER)
    t0 = time.time()
    left = lambda: a.budget * 60 - (time.time() - t0)
    plan, n, unk, add = scan(roots, man, a, adopt_audio())
    print("scanned %d roots in %ds: %d files seen" % (len(roots), n.pop("scan seconds", 0), n.get("files", 0)))
    print("\n%-22s %10s" % ("reader", "to read"))
    for r in ORDER:
        print("%-22s %10d%s" % (r, len(plan.get(r, [])), "" if r in want else "   (not selected)"))
    print("\nskipped / left aside:", ", ".join("%s %d" % kv for kv in sorted(n.items()) if kv[0] != "files") or "-")
    if unk:
        print("unknown formats this scan (top 8 of %d):" % len(unk))
        for u in sorted(unk.values(), key=lambda u: -u["count"])[:8]:
            print("  %-12s %s x%d  e.g. %s" % (u["extension"], u["first16"][:24], u["count"], u["example"]))
    if a.dry_run:
        print("\ndry run: nothing written")
        return
    hot = other_heavy_jobs()
    if hot and not a.force:
        sys.exit("another heavy job is running (one heavy job per machine):\n  " + "\n  ".join(hot) + "\nwait for it, or --force")
    if not take_lock(p):
        sys.exit("another data_universe run holds " + p["lock"])
    try:
        os.makedirs(p["work"], exist_ok=True)
        tag = time.strftime("%Y%m%d_%H%M%S")
        out_manifest = open(p["manifest"], "a")
        wr = lambda e: out_manifest.write(json.dumps(e, separators=(",", ":")) + "\n")
        for e in add:
            wr(e)
        if unk:
            with open(p["unknown"], "a") as f:
                for u in sorted(unk.values(), key=lambda u: -u["count"]):
                    f.write(json.dumps(dict(u, host=a.host, run=tag), separators=(",", ":")) + "\n")
        meta = {f[0]: f for fl in plan.values() for f in fl}
        W = {"work": p["work"]}
        done = collections.Counter()
        report = []
        for r in ORDER:
            files = plan.get(r, [])
            if not files or r not in want:
                continue
            if left() < 30:
                report.append((r, "no time left, %d files wait" % len(files)))
                continue
            if r == "midi_copy":
                res, note = run_midi(files, a, W, left, owner_roots), ""
            elif r == "audio_features":
                out_path = os.path.expanduser(a.audio_out) if a.audio_out else os.path.join(TD, "audio", "%s_features.jsonl" % a.host)
                res, note = run_audio(files, roots, a, W, left, out_path)
            else:
                res, note = run_reader(r, files, roots, a, W, left, owner_roots, tag)
            for path, (stt, rows) in res.items():
                sz, mt = meta[path][1], meta[path][2]
                wr({"path": path, "size": sz, "mtime": mt, "reader": r, "rows": rows, "status": stt})
                done[r] += 1
            out_manifest.flush()
            report.append((r, "%d of %d files done%s" % (len(res), len(files), ("  [" + note + "]") if note else "")))
        if a.ingest and left() > 120:
            ok, out = run([os.path.join(HERE, "midi_ingest.py"), "--resume", "--budget", "%.1f" % (left() / 60 - 1), "--jobs", str(a.jobs)], left())
            report.append(("midi_ingest", "ok" if ok else "stopped: " + out[-200:].replace("\n", " | ")))
        out_manifest.close()
        state = {"roots": roots, "scanned": time.strftime("%Y-%m-%d %H:%M"), "skipped": dict(n),
                 "left": {r: len(plan.get(r, [])) - done[r] for r in ORDER if plan.get(r)}}
        json.dump(state, open(p["state"], "w"), indent=1)
        print("\nrun summary (%.1f of %.0f min):" % ((time.time() - t0) / 60, a.budget))
        for r, s in report:
            print("  %-22s %s" % (r, s))
        print("left:", {k: v for k, v in state["left"].items() if v} or "nothing")
        print("manifest", p["manifest"])
    finally:
        try:
            os.remove(p["lock"])
        except OSError:
            pass


if __name__ == "__main__":
    main()
