#!/usr/bin/env python3
"""Hand-made patches -> pieces, gated like the card and public pieces.
    patch_pieces.py [--out patch_pieces.jsonl]
Reads .inf text (node N Category Type / f|i|b|s key value). Sources:
  personal   ~/Downloads, ~/Desktop, ~/Documents
  template   repo assets/templates (authored by the owner)
Machine-made patches are skipped (a `# genome:` or `# style:` line, or the patch-universe library and
agentlang task folders), so the store never learns from its own generator.
Pieces: note_seq (Note Sequencer), drum_groove (Drum Sequencer), params (every knob of a node type)."""
import argparse, collections, glob, hashlib, json, math, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pieces as P

HOME = os.path.expanduser("~")
ROOTS = [("personal", HOME + "/Downloads"), ("personal", HOME + "/Desktop"), ("personal", HOME + "/Documents"),
         ("template", HOME + "/Infinite/repo/assets/templates")]
SKIP = (".app/", "patch-universe", "agentlang/tasks", "/worktrees/", "/build/", "node_modules")
LANE_WORDS = [("kick", 0), ("snare", 1), ("clap", 4), ("ohat", 3), ("open", 3), ("hat", 2), ("hihat", 2),
              ("htom", 6), ("tom", 5), ("bell", 7), ("cowbell", 7), ("rim", 1)]


def parse(text):
    nodes, cur = [], None
    for ln in text.splitlines():
        m = re.match(r"node (\d+) (\S+) (.+?)\s*$", ln)
        if m:
            cur = {"id": int(m[1]), "cat": m[2], "type": m[3], "p": {}}
            nodes.append(cur)
        elif ln.startswith("end"):
            cur = None
        elif cur is not None:
            m = re.match(r"\s+([fibs]) (\S+) (.*)$", ln)
            if m:
                k, v = m[2], m[3]
                try:
                    cur["p"][k] = float(v) if m[1] != "s" else v
                except ValueError:
                    pass
    return nodes


def seq_piece(n, src, kind):
    p = n["p"]
    steps = int(p.get("steps", 16))
    pitches, grid, vel = [], [], []
    for i in range(steps):
        if p.get("en%d" % i, 1):
            pitches.append(int(p.get("note%d" % i, 60))); grid.append(float(i)); vel.append(p.get("vel%d" % i, 0.8))
    return {"type": "note_seq", "src": src, "family": kind, "pitches": pitches, "grid": grid, "vel": vel,
            "steps": steps, "gate": p.get("gatePercent"), "rate_beats": p.get("rateBeats")}


def lane_index(path):
    low = os.path.basename(path or "").lower()
    for w, i in LANE_WORDS:
        if w in low:
            return i
    return None


def drum_pieces(n, src, kind):
    p = n["p"]
    n_steps = 1 + max([int(m.group(2)) for k in p for m in [re.match(r"lane(\d+)_step(\d+)$", k)] if m] or [-1])
    if n_steps <= 0:
        return
    lanes = [""] * 8
    for li in range(16):
        li_idx = lane_index(p.get("lane%d_path" % li, ""))
        if li_idx is None or lanes[li_idx]:
            continue
        lanes[li_idx] = "".join("x" if p.get("lane%d_step%d" % (li, s), 0) > 0.5 else "." for s in range(n_steps))
    for b in range(0, n_steps - 15, 16):  # one piece per 16-step bar
        bar = [l[b:b + 16] if l else "" for l in lanes]
        bar = [l if "x" in l else "" for l in bar]
        if any(bar):
            yield {"type": "drum_groove", "src": src + "#bar%d" % (b // 16), "family": kind, "lanes": bar, "swing": 0.0, "groove": None}


def gate_params(p):
    h = []
    if not p["values"]:
        return ["no params"], []
    if any(not math.isfinite(v) for v in p["values"].values()):
        h.append("non-finite value")
    if len(p["values"]) < 3:
        return h + ["fewer than 3 params"], []
    return h, []


def gate_notes_patch(p):
    h, s = P.gate_notes(p)
    if len(set(p["pitches"])) == 1 and len(p["pitches"]) > 4:
        h.append("untouched default: every step the same pitch")
    return h, s


def sources():
    seen = set()
    for kind, root in ROOTS:
        for f in sorted(glob.glob(root + "/**/*.inf", recursive=True)):
            if any(s in f for s in SKIP):
                continue
            try:
                t = open(f, errors="replace").read()
            except OSError:
                continue
            if "# genome:" in t or "# style:" in t:
                continue
            h = hashlib.sha1(t.encode()).hexdigest()
            if h in seen:
                continue
            seen.add(h)
            yield kind, f, t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "patch_pieces.jsonl"))
    a = ap.parse_args()
    rows, files = [], collections.Counter()
    for kind, f, t in sources():
        files[kind] += 1
        base = kind + ":" + os.path.relpath(f, HOME)
        for n in parse(t):
            src = "%s#%d" % (base, n["id"])
            if n["type"] == "Note Sequencer":
                rows.append(seq_piece(n, src, kind))
            elif n["type"] == "Drum Sequencer":
                rows += list(drum_pieces(n, src, kind))
            if n["cat"] in ("Synths", "AudioEffects", "Notes", "Modulators", "AudioUtility") and n["type"] not in ("Note Sequencer", "Drum Sequencer"):
                vals = {k: v for k, v in n["p"].items() if not isinstance(v, str)}
                rows.append({"type": "params", "src": src, "family": kind, "node": n["type"], "values": vals})
    seen = set()
    for p in rows:
        gate = {"note_seq": gate_notes_patch, "drum_groove": P.gate_drum, "params": gate_params}[p["type"]]
        h, s = gate(p)
        key = P.digest({k: v for k, v in p.items() if k not in ("src", "family")})
        if not h and key in seen:
            h = ["duplicate of an earlier piece"]
        seen.add(key)
        p.update(id=key, hard=h, soft=s, ok=not h, license="owner")
    with open(a.out, "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    print("patches read:", dict(files))
    print("%-12s %6s %6s  %s" % ("type", "total", "ok", "hard fails"))
    for t in ("note_seq", "drum_groove", "params"):
        r = [x for x in rows if x["type"] == t]
        why = collections.Counter(x for q in r for x in q["hard"])
        print("%-12s %6d %6d  %s" % (t, len(r), sum(q["ok"] for q in r), "; ".join("%s x%d" % kv for kv in why.most_common(5)) or "-"))
    pn = collections.Counter(q["node"] for q in rows if q["type"] == "params" and q["ok"])
    print("params ok by node:", dict(pn.most_common(12)))
    print("wrote", a.out)


if __name__ == "__main__":
    main()
