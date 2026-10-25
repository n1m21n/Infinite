#!/usr/bin/env python3
"""Scene universe (v2): shapes and habits taken from the author's own patches.
Audio  = a console: several voices (notes > synth > fx, or a plain audio source) into one Mixer-like joiner, then master fx.
Visual = layers (2D sources, or 3D scenes: primitive > ops > instance/particles > Render 3D) joined by a Blend ladder
         with colour adjustments between, then Output.
Play dial 0..100 = how much of the patch is modulated; 100 = every modulatable param. Params are weighted by how
the author actually plays (cutoff, fine, mix, pitch, sync, resonance, rates, camera, colour ...).
Genomes (one-way, deterministic given the pools):
  A <audio genome with shared mods>            e.g.  A [N1.5>S3.9>E2.4] + [Z0.1>E5.2] => X0.7>E9.3>O ; L2.7@1.cutoff,3.mix
  V <size>.<play>.<seed>                       e.g.  V4.60.12345
  AV <audio genome> ## <size>.<play>.<seed> ;; <link tokens>"""
import json, math, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u

HERE = os.path.dirname(os.path.abspath(__file__))
PROF = json.load(open(os.path.join(HERE, "profile_personal.json")))
NCOUNT = dict(PROF["node_count"])
TPCOUNT = {(a, b): n for a, b, n in PROF["type_param"]}
PKCOUNT = dict(PROF["param_key"])
SRC_COUNT = dict(PROF["mod_source"])

LOVE = ["cutoff", "fine", "semitone", "pitch", "sync", "reson", "q", "mix", "freq", "position", "rate", "beats", "bars", "steps",
        "division", "warp", "drive", "feedback", "decay", "time", "size", "detune", "unison", "shape",
        "cam", "scale", "rotate", "instance", "count", "density", "emit", "radius", "hue", "sat", "bright", "contrast",
        "expos", "gamma", "opacity", "amount", "intensity", "speed", "lifetime", "angle", "offset", "gain", "level"]


try:
    MOVE = json.load(open(os.path.join(HERE, "profile_movement.json")))  # owner's hand moves per (type, param label)
except OSError:
    MOVE = {}
LABEL = {}  # (type, key) -> display label, filled by pools_v2
CODE_BOOST = 6.0  # the language-taking nodes: Claude writes these well, so favour them


def _norm(x):
    return "".join(c for c in x.lower() if c.isalnum())


SCREEN = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "component_screen.json"))) if os.path.exists(os.path.join(os.path.dirname(os.path.abspath(__file__)), "component_screen.json")) else {}


def node_w(name):
    if ":" in name:
        r = SCREEN.get(name)  # screen.py: how often this component works alone; never-ok ones are not composed
        return CODE_BOOST * ((r["ok"] + 0.5) / (r["n"] + 1) if r else 1.0) if not r or r["ok"] else 0.0
    return 0.35 + NCOUNT.get(name, 0) ** 0.8


def param_w(tname, key):
    k = key.lower().split(".")[-1]
    w = 1.0 + (7.0 if any(s in k for s in LOVE) else 0.0)
    lab = LABEL.get((tname, key), key)
    hand = (MOVE.get(tname.split(":")[0]) or {}).get(lab, {}).get("hand", 0)
    return w + 2.5 * TPCOUNT.get((tname, key), 0) + 0.5 * PKCOUNT.get(key, 0) + 3.0 * hand ** 0.5


def wchoice(r, names, k=1, wf=node_w):
    """k distinct weighted picks."""
    names, ws, out = list(names), [wf(n) for n in names], []
    for _ in range(min(k, len(names))):
        i = r.choices(range(len(names)), ws)[0]
        out.append(names.pop(i)); ws.pop(i)
    return out


try:
    BAD = json.load(open(os.path.join(HERE, "bad_params.json")))  # advertised by --describe, refused by the engine
except OSError:
    BAD = {}


def note_bad(msg):
    """Learn a refused index from an E_BAD_PARAM message so later runs skip it."""
    import re
    m = re.search(r"targets parameter (\d+) but (.+?) only has", msg or "")
    if m:
        BAD.setdefault(m.group(2), [])
        if int(m.group(1)) not in BAD[m.group(2)]:
            BAD[m.group(2)].append(int(m.group(1)))
            json.dump(BAD, open(os.path.join(HERE, "bad_params.json"), "w"))


def pools_v2(facts):
    for n, idxs in BAD.items():
        if n in facts:
            facts[n]["modulatable"] = [m for m in facts[n]["modulatable"] if m.get("index") not in idxs]
    for n, t in facts.items():
        for m in t["modulatable"]:
            if m.get("key"):
                LABEL[(n, m["key"])] = m.get("label", m["key"])
                CONT[(n, m["key"])] = not m.get("bool") and not m.get("step")
    P = u.pools(facts)
    P.pop('_V', None)
    P["Z"] = [n for n in sorted(facts) if facts[n]["category"] == "Synths" and not facts[n]["hardware_driven"] and n not in u.NEEDS_LIVE
              and not any(i["kind"] in ("note", "image") for i in facts[n]["inputs"]) and any(o["kind"] == "audio" for o in facts[n]["outputs"])]
    P["L"] = sorted(n for n in facts if facts[n]["category"] in ("Modulators", "Macros", "Prediction") and not facts[n]["hardware_driven"]
                    and all(i["kind"] == "modulator" for i in facts[n]["inputs"])
                    and any(o["kind"] in ("modulator", "predictor") for o in facts[n]["outputs"]) and n not in ("Face Track", "Hand Track", "Pose Track"))
    P["L"] = [n for n in P["L"] if facts[n]["category"] != "Prediction" or n in PRED]
    readers = [n for n in ("Note to CV", "Velocity to CV", "Audio to CV", "Audio Analyze", "Image Analyze") if n in facts]
    P["L_ALL"] = P["L"] + [n for n in readers if n not in P["L"]]  # link modulators; L indexes stay a prefix
    P["_V"] = visual_pools(facts)
    return P


PRED = ("Drift", "Predictive Modulator")  # learned modulators (green cable): continuous params only
CONT = {}  # (type, key) -> param is continuous (not bool/enum/stepped)


def src_w(name):
    return 0.25 * max(SRC_COUNT.values(), default=40) if name in PRED else 0.2 + SRC_COUNT.get(name, 0)


# ---------------------------------------------------------------- audio
def mod_tokens(r, P, facts, names, play, order_ids=None, id_off=0, cap=90):
    """Shared-source modulation tokens for chain nodes `names` (index k = position in compile order)."""
    cand = []
    for k, n in enumerate(names):
        for m in facts[n]["modulatable"]:
            if m.get("key"):
                cand.append((k, n, m["key"]))
    if not cand or play <= 0:
        return []
    count = min(cap, max(1, round((play / 100.0) ** 1.4 * len(cand))))
    if play >= 100 and len(cand) <= cap:
        chosen = cand
    else:
        chosen, pool, ws = [], list(cand), [param_w(n, key) for _, n, key in cand]
        for _ in range(count):
            i = r.choices(range(len(pool)), ws)[0]
            chosen.append(pool.pop(i)); ws.pop(i)
    nsrc = max(1, min(12, 1 + len(chosen) // 5 + r.randrange(2)))
    srcs = [(P["L"].index(wchoice(r, P["L"], 1, src_w)[0]), u.sd(r)) for _ in range(nsrc)]
    groups = {}
    for k, n, key in chosen:
        g = r.choice(srcs)
        if P["L"][g[0]] in PRED and not CONT.get((n, key)):
            g = next((x for x in srcs if P["L"][x[0]] not in PRED), None)
            if g is None:
                continue
        groups.setdefault(g, []).append("%d.%s" % (k + id_off, key))
    return ["L%d.%d@%s" % (idx, seed, ",".join(t)) for (idx, seed), t in groups.items()]


def audio_genome(r, P, facts, size, play):
    kmin, kmax = {1: (1, 2), 2: (2, 3), 3: (2, 4), 4: (3, 5), 5: (4, 6)}[size]
    jname = "Mixer" if r.random() < 0.65 and "Mixer" in P["X"] else wchoice(r, P["X"], 1)[0]
    slots = sum(1 for i in facts[jname]["inputs"] if i["kind"] == "audio")
    nb = max(1, min(r.randint(kmin, kmax), slots))
    branches = []
    for _ in range(nb):
        toks = []
        if P["Z"] and r.random() < 0.22:
            toks.append("Z%d.%d" % (P["Z"].index(wchoice(r, P["Z"], 1)[0]), u.sd(r)))
        else:
            toks.append("N%d.%d" % (P["G"].index(wchoice(r, P["G"], 1)[0]), u.sd(r)))
            for _ in range(r.choice([0, 0, 1, 1, 2, 3])):
                toks.append("N%d.%d" % (P["N"].index(wchoice(r, P["N"], 1)[0]), u.sd(r)))
            toks.append("S%d.%d" % (P["S"].index(wchoice(r, P["S"], 1)[0]), u.sd(r)))
        for _ in range(r.choice([0, 1, 1, 2, 2, 3, 4])):
            toks.append("E%d.%d" % (P["E"].index(wchoice(r, P["E"], 1)[0]), u.sd(r)))
        branches.append(toks)
    tail = ["X%d.%d" % (P["X"].index(jname), u.sd(r))]
    for _ in range(r.choice([0, 1, 1, 2, 3])):
        tail.append("E%d.%d" % (P["E"].index(wchoice(r, P["E"], 1)[0]), u.sd(r)))
    if "Limiter" in P["E"] and r.random() < 0.5:
        tail.append("E%d.%d" % (P["E"].index("Limiter"), u.sd(r)))
    code = " + ".join("[" + ">".join(b) + "]" for b in branches) + " => " + ">".join(tail) + ">O"
    if play > 0:
        names = u.node_names(code, P)
        toks = mod_tokens(r, P, facts, names, play)
        if toks:
            code += " ; " + " ".join(toks)
    return code


def _pools_audio_names(code, P):
    # node_names knows N/S/E/J/X/L; Z sources come from P["Z"]
    return u.node_names(code, P)


# ---------------------------------------------------------------- visual
class Graph:
    WORD = {"audio": "aud", "note": "note", "image": "cable", "geometry": "geo"}

    def __init__(self, facts, rng):
        self.facts, self.r, self.lines, self.names, self.used, self.n = facts, rng, ["infinite-patch 1"], {}, {}, 0

    def add(self, name, seed=0, force=None):
        self.n += 1
        t = self.facts[name]
        self.lines.append("node %d %s %s" % (self.n, t["category"], name.split(":")[0]))
        ps = u.seeded_params(t, seed); ps.update(force or {})
        for k, (tag, v) in sorted(ps.items()):
            self.lines.append("  %s %s %s" % (tag, k, ("%.6g" % v) if tag == "f" else v if tag == "s" else int(v)))
        self.lines.append("end")
        self.names[self.n] = name
        return self.n

    def connect(self, src, dst, kind=None):
        kind = kind or self.facts[self.names[src]]["outputs"][0]["kind"]
        slots = [i["slot"] for i in self.facts[self.names[dst]]["inputs"] if i["kind"] == kind]
        free = [s for s in slots if (dst, s) not in self.used]
        if not free:
            return False
        self.used[(dst, free[0])] = 1
        self.lines.append("%s %d %d %d" % (self.WORD[kind], dst, free[0], src))
        return True

    def text(self):
        return u.index_code_mods("\n".join(self.lines) + "\n")


def visual_pools(facts):
    V = {"src": [], "flt": [], "geo": [], "gop": [], "blend": [], "color": []}
    for n in sorted(facts):
        t = facts[n]
        if t["hardware_driven"] or n in ("Comment", "Group", "Null", "Plugin", "Output", "Syphon Out", "NDI Out", "Projection",
                                         "Audio Texture", "Optical Flow", "VMPC", "Render 3D", "Viewport"):
            continue
        ins, outs = [i["kind"] for i in t["inputs"]], [o["kind"] for o in t["outputs"]]
        ni = ins.count("image")
        if t["category"] == "Source" and ni == 0 and "image" in outs:
            V["src"].append(n)
        elif t["category"] in ("Effects", "Compositing") and ni == 1 and len(ins) == 1 and "image" in outs:
            V["flt"].append(n)
            if t["category"] == "Compositing" and ("color" in n.lower() or n in ("Curves", "Color Ramp")):
                V["color"].append(n)
        elif t["category"] == "Compositing" and ni >= 2 and all(k == "image" for k in ins) and "image" in outs:
            V["blend"].append(n)
        elif t["category"] == "3D" and "geometry" in outs:
            if "geometry" not in ins:
                V["geo"].append(n)
            elif ins.count("geometry") == 1:
                V["gop"].append(n)
    return V


def build_visual(g, r, V, size, play):
    """Adds the visual scene to graph g; returns (output id, modulatable node ids)."""
    f = g.facts
    nl = {1: (1, 2), 2: (2, 3), 3: (3, 4), 4: (4, 6), 5: (5, 8)}[size]
    layers = []
    for _ in range(r.randint(*nl)):
        if r.random() < 0.42:  # a 3D scene
            prim = g.add(wchoice(r, V["geo"], 1)[0], u.sd(r))
            if f[g.names[prim]]["inputs"] and r.random() < 0.6 and V["src"]:
                g.connect(g.add(wchoice(r, V["src"], 1)[0], u.sd(r)), prim)
            cur = prim
            for _ in range(r.choice([0, 1, 1, 2, 3])):
                op = g.add(wchoice(r, V["gop"], 1)[0], u.sd(r))
                g.connect(cur, op); cur = op
            if "Instance on Points" in f and r.random() < 0.55:
                ip = g.add("Instance on Points", u.sd(r))
                g.connect(cur, ip)
                if r.random() < 0.7:
                    g.connect(g.add(wchoice(r, V["geo"], 1)[0], u.sd(r)), ip)
                cur = ip
            if "Particle System" in f and r.random() < 0.25:
                ps = g.add("Particle System", u.sd(r)); g.connect(cur, ps); cur = ps
            rend = g.add("Render 3D", u.sd(r))
            g.connect(cur, rend)
            cur = rend
        else:
            cur = g.add(wchoice(r, V["src"], 1)[0], u.sd(r))
        for _ in range(r.choice([0, 1, 1, 2, 3])):
            fl = g.add(wchoice(r, V["flt"], 1)[0], u.sd(r))
            g.connect(cur, fl); cur = fl
        layers.append(cur)
    cur = layers[0]
    for lay in layers[1:]:
        bl = g.add("Blend" if "Blend" in V["blend"] and r.random() < 0.7 else wchoice(r, V["blend"], 1)[0], u.sd(r))
        g.connect(cur, bl); g.connect(lay, bl)
        cur = bl
        if V["color"] and r.random() < 0.55:
            ca = g.add("color adjustments" if "color adjustments" in V["color"] and r.random() < 0.7 else wchoice(r, V["color"], 1)[0], u.sd(r))
            g.connect(cur, ca); cur = ca
    for _ in range(r.choice([0, 1, 2])):
        fl = g.add(wchoice(r, V["flt"], 1)[0], u.sd(r))
        g.connect(cur, fl); cur = fl
    chain_ids = list(g.names)
    out = g.add("Output", 0)
    g.connect(cur, out)
    return out, chain_ids


def add_mods(g, r, P, ids, play, srcs=None):
    cand = [(i, g.names[i], m["key"]) for i in ids for m in g.facts[g.names[i]]["modulatable"] if m.get("key")]
    if not cand or play <= 0:
        return
    count = min(300, max(1, round((play / 100.0) ** 1.4 * len(cand))))
    chosen, pool, ws = [], list(cand), [param_w(n, k) for _, n, k in cand]
    for _ in range(min(count, len(pool))):
        j = r.choices(range(len(pool)), ws)[0]
        chosen.append(pool.pop(j)); ws.pop(j)
    nsrc = max(1, min(12, 1 + len(chosen) // 5 + r.randrange(2)))
    mods = [g.add(wchoice(r, P["L"], 1, src_w)[0], u.sd(r)) for _ in range(nsrc)]
    for i, n, key in chosen:
        g.lines.append("mod %d %s %d 0 0 %.3f 0.5" % (i, key, r.choice(mods), r.uniform(0.05, 1)))


# ---------------------------------------------------------------- compile / sample / probe
def shift(text, off):
    out = []
    for l in text.splitlines():
        p = l.split()
        if p and p[0] == "node":
            p[1] = str(int(p[1]) + off); out.append(" ".join(p))
        elif p and p[0] in ("note", "aud", "cable", "geo", "mod") and not l.startswith(" "):
            p[1] = str(int(p[1]) + off); p[3] = str(int(p[3]) + off); out.append(" ".join(p))
        else:
            out.append(l)
    return out


def parse_scene(code):
    kind, _, rest = code.partition(" ")
    if kind == "A":
        return kind, rest, None, ""
    if kind == "V":
        return kind, None, rest, ""
    body, _, links = rest.partition(" ;; ")
    a, _, v = body.partition(" ## ")
    return kind, a.strip(), v.strip(), links.strip()


def compile_visual_part(vcode, facts, V, P):
    size, play, seed = (int(x) for x in vcode.split("."))
    r = random.Random("V:%d" % seed)
    g = Graph(facts, r)
    out, ids = build_visual(g, r, V, size, play)
    add_mods(g, r, P, ids, play)
    return g.text(), ids


def compile_scene(code, facts, P, V=None):
    V = V or P["_V"]
    kind, a, v, links = parse_scene(code)
    if kind == "A":
        return u.compile_inf(a, facts, P)
    if kind == "V":
        return compile_visual_part(v, facts, V, P)[0]
    ao = []
    at = u.compile_inf(a, facts, P, ao)
    vt, vids = compile_visual_part(v, facts, V, P)
    na = sum(1 for l in at.splitlines() if l.startswith("node "))
    lines = at.splitlines() + shift(vt, na)[1:]
    order = ao + [x + na for x in vids]
    names, nid = {}, 0
    for l in lines:
        if l.startswith("node "):
            p = l.split(); names[int(p[1])] = " ".join(p[3:]); nid = max(nid, int(p[1]))
    made = {}
    for tk in links.split():
        src, _, dst = tk.partition("@")
        src, _, frm = src.partition("<")
        idx, _, seed = src[1:].partition(".")
        key_ = (idx, seed, frm)
        if key_ not in made:
            mname = P["L_ALL"][int(idx) % len(P["L_ALL"])]
            t = facts[mname]
            nid += 1
            lines.append("node %d %s %s" % (nid, t["category"], mname.split(":")[0]))
            for kk, (tag, val) in sorted(u.seeded_params(t, int(seed or 0)).items()):
                lines.append("  %s %s %s" % (tag, kk, ("%.6g" % val) if tag == "f" else val if tag == "s" else int(val)))
            lines.append("end")
            if frm:
                sn = order[int(frm) % len(order)]
                sk = facts[names[sn]]["outputs"][0]["kind"]
                slot = next(i["slot"] for i in t["inputs"] if i["kind"] == sk)
                lines.append("%s %d %d %d" % (Graph.WORD[sk], nid, slot, sn))
            made[key_] = nid
        for one in dst.split(","):
            k, _, key = one.partition(".")
            lines.append("mod %d %s %d 0 0 %.3f 0.5" % (order[int(k) % len(order)], key, made[key_], random.Random("%s:%s" % (seed, key)).uniform(0.1, 1)))
    return u.index_code_mods("\n".join(lines) + "\n")


def link_tokens(r, P, facts, an, vn, mode, play):
    names, dom = an + vn, [0] * len(an) + [1] * len(vn)
    toks = []
    def targets(domain, k):
        cand = [(i, n, m["key"]) for i, n in enumerate(names) if dom[i] == domain for m in facts[n]["modulatable"] if m.get("key")]
        out = []
        for _ in range(min(k, len(cand))):
            j = r.choices(range(len(cand)), [param_w(n, key) for _, n, key in cand])[0]
            out.append(cand.pop(j))
        return ["%d.%s" % (i, key) for i, _, key in out]
    nl = max(1, round(1 + play / 100 * 5))
    if mode == "shared":
        for _ in range(r.choice([1, 2, 3])):
            t = targets(0, r.randint(1, nl)) + targets(1, r.randint(1, nl))
            toks.append("L%d.%d@%s" % (P["L"].index(wchoice(r, P["L"], 1, src_w)[0]), u.sd(r), ",".join(t)))
    elif mode == "reader":
        readers = {"note": ["Note to CV", "Velocity to CV"], "audio": ["Audio to CV", "Audio Analyze"], "image": ["Image Analyze"]}
        for _ in range(r.choice([1, 2])):
            j = r.randrange(len(names))
            kind = facts[names[j]]["outputs"][0]["kind"]
            rd = [x for x in readers.get(kind, []) if x in P["L_ALL"]]
            if not rd:
                continue
            t = targets(1 - dom[j], r.randint(1, nl))
            if t:
                toks.append("L%d.%d<%d@%s" % (P["L_ALL"].index(r.choices(rd, [1.0 if "Analyze" not in x else 0.25 for x in rd])[0]), u.sd(r), j, ",".join(t)))
    return toks


def sample_scene(r, P, facts, kind=None, play=None, size=None):
    kind = kind or r.choices(["A", "V", "AV"], [0.3, 0.3, 0.4])[0]
    play = r.choice([0, 5, 15, 30, 50, 70, 90, 100]) if play is None else play
    size = size or r.choices([1, 2, 3, 4, 5], [0.12, 0.22, 0.3, 0.24, 0.12])[0]
    seed = r.randrange(1, 1 << 24)
    vcode = "%d.%d.%d" % (size, play, seed)
    if kind == "V":
        return "V " + vcode
    a = audio_genome(r, P, facts, size, play)
    if kind == "A":
        return "A " + a
    an = u.node_names(a.split(";")[0] + ";", P)  # chain nodes only, for link indexing
    g = Graph(facts, random.Random("V:%d" % seed))
    _, vids = build_visual(g, g.r, P["_V"], size, play)
    vn = [g.names[i] for i in vids]
    mode = r.choices(["separate", "shared", "reader"], [0.3, 0.5, 0.2])[0]
    links = link_tokens(r, P, facts, an, vn, mode, play)
    return "AV %s ## %s%s" % (a, vcode, (" ;; " + " ".join(links)) if links else "")


def probe(code, facts, P, binary, workdir, secs=4):
    import subprocess, time
    from visual import classify_frames
    h = "s%d" % (hash(code) & 0xffffffff)
    path = os.path.join(workdir, h + ".inf")
    res, t0 = {"code": code}, time.time()
    try:
        open(path, "w").write(compile_scene(code, facts, P))
        kind = code.split(" ")[0]
        ac = vc = None
        if kind in ("A", "AV"):
            apath = path
            if kind == "AV":  # --audio-summary also renders the pictures: probe the sound on the audio half alone
                apath = path + ".a.inf"
                open(apath, "w").write(compile_scene("A " + parse_scene(code)[1], facts, P))
            cp = subprocess.run([binary, "--audio-summary", apath, path + ".json", "--duration", str(secs)], capture_output=True, text=True, timeout=45)
            j = json.loads(u.last_json(cp.stdout))
            if cp.returncode or not j.get("ok"):
                ac = "error:" + ((j.get("errors") or [{}])[0].get("code") or "?"); res["msg"] = (j.get("errors") or [{}])[0].get("message"); note_bad(res["msg"])
            else:
                res["a"] = j.get("audio_summary", {}); ac = u.classify({"ok": True, "a": res["a"]})
        if kind in ("V", "AV") and not (ac or "").startswith("error"):
            cp = subprocess.run([binary, "--frame", path, "0", os.path.join(workdir, h + "/"), "--size", "320x180"], capture_output=True, text=True, timeout=60)
            j = json.loads(u.last_json(cp.stdout))
            if cp.returncode or not j.get("ok"):
                vc = "error:" + ((j.get("errors") or [{}])[0].get("code") or "?"); res["msg"] = (j.get("errors") or [{}])[0].get("message"); note_bad(res["msg"])
            else:
                st = j["frame_stats"]; vc = classify_frames(st)
                res["stats"] = [{k: s[k] for k in ("mean_luma", "min_luma", "max_luma", "black_percent")} for s in st]
        res["audio_class"], res["visual_class"] = ac, vc
        good = [c for c in (ac, vc) if c is not None]
        res["class"] = "ok" if all(c in ("ok", "clipped") for c in good) else "+".join(good)
    except subprocess.TimeoutExpired:
        res["class"] = "error:timeout"
    except Exception as e:
        res["class"] = "classify-error:%s:%s" % (type(e).__name__, str(e)[:80])
    res["sec"] = round(time.time() - t0, 2)
    try:
        kind, a, v, links = parse_scene(code)
        names = u.node_names(a, P) if a else []
        if v:
            r = random.Random("V:%d" % int(v.split(".")[2])); g = Graph(facts, r)
            _, ids = build_visual(g, r, P["_V"], *map(int, v.split(".")[:2])); names += [g.names[i] for i in ids]
        res["names"], res["order"] = names, 6
    except Exception:
        pass
    for p in (path, path + ".json", path + ".a.inf"):
        os.path.exists(p) and os.remove(p)
    return res


def main():
    import argparse, tempfile, time
    from concurrent.futures import ThreadPoolExecutor
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["compile", "sample", "run"])
    ap.add_argument("code", nargs="?")
    ap.add_argument("--bin", default=u.DEFAULT_BIN)
    ap.add_argument("-n", type=int, default=50)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--budget", type=float, default=600)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--kind", choices=["A", "V", "AV"])
    ap.add_argument("--play", type=int)
    ap.add_argument("--out", default="atlas_scene.jsonl")
    a = ap.parse_args()
    facts = u.load_facts(); P = pools_v2(facts)
    if a.cmd == "compile":
        sys.stdout.write(compile_scene(a.code, facts, P)); return
    r = random.Random(a.seed)
    codes = [sample_scene(r, P, facts, a.kind, a.play) for _ in range(a.n)]
    if a.cmd == "sample":
        print("\n".join(codes)); return
    t0, done = time.time(), 0
    with tempfile.TemporaryDirectory() as wd, open(a.out, "a") as f, ThreadPoolExecutor(a.workers) as ex:
        futs = [ex.submit(lambda c=c: probe(c, facts, P, a.bin, wd) if time.time() - t0 < a.budget else None) for c in codes]
        for fu in futs:
            res = fu.result()
            if res:
                f.write(json.dumps(res) + "\n"); done += 1
    print("probed", done, "of", a.n, "in", round(time.time() - t0), "s ->", a.out)


if __name__ == "__main__":
    main()
